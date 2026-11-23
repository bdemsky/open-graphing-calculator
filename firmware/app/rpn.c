/*
 * RPN mode: a stack-based home screen in the style of classic RPN
 * calculators (rpn.h, help/rpn.txt). Selected with MODE > ENTRY; when on,
 * ui_set_home(&scr_rpn) makes it the home screen (QUIT, CLEAR, power-on).
 *
 * Values are CAS values: every level is a hidden CAS variable _rpnN holding
 * the value as an assignment stores it (_rpnN:=(expression), like x:=2±0.1
 * on the home screen), plus the printed text for display. Every operation builds a CAS expression
 * from the variables of the levels it consumes, e.g. levels 2:"x" 1:"2.5"
 * and ^ evaluate "(x)^(1*_rpn7)", so RPN gives the results algebraic entry
 * gives: uncertainty sources stay correlated (x DUP - is 0±0), floats keep
 * the digits the display rounds away, exact values stay exact.
 *
 * Keys (command line empty unless noted):
 *   ENTER push the command line / DUP      ⌫   drop level 1
 *   CLEAR clear the line / the stack       )   SWAP
 *   ,     STACK menu (all commands)        SHIFT ENTER UNDO, SHIFT (-) LASTARG
 *   UP    interactive stack                DOWN edit level 1
 *   SHIFT CLEAR clear the stack
 *   soft keys: F1..F5 SWAP DROP ROLL PICK UNDO, SHIFT F1..F5 DUP OVER ROT
 *   simplify rewrite (result actions on level 1, rpn_result_action; LASTARG
 *   stays on SHIFT (-), STACK on ,); in the interactive stack ECHO PICK ROLL
 *   ROLLD DROPN, SHIFT F1 EDIT. MODE SIMPLIFY AUTO: eval_level gives each new
 *   level the autosimplify form.
 *   + - * / ^, x², x⁻¹, √, sin.. ln, 10^x, e^x, (-): apply to the stack; with a
 *   number being typed on the command line it is pushed first: 2 ENTER 3 +.
 *   →     with a name on the line: store level 1 into it.
 * Menus (CAS, MATH, CATALOG, VARS) chosen on an empty line (or after a number)
 * apply their command to the stack; see apply_fn() for the argument
 * convention of multi-argument commands.
 *
 * Touch: tap a level to select it, tap it again to echo it to the command
 * line, drag to scroll the stack, tap the command line to move the cursor.
 *
 * Framework hooks: scr_rpn (soft keys from rpn_softkeys(): the bar changes in
 * the interactive stack), the MODE row mode_entry (ENTRY ALGEBRAIC/RPN), the
 * persist blob persist_rpn, the STACK menu menu_rpn_stack, and rpn_state() for
 * console tests. mode_entry and persist_rpn are registered in app/registry.c.
 *
 * State: file-scope statics below. The stack, its UNDO copy and the scratch
 * buffers live in the RPN_WORK area that app_init carves from the arena
 * (rpn_init); nothing is malloc'ed. Without a work area every key is ignored
 * and RPN cannot be selected.
 *
 * UNDO: before the first change made by a key, snapshot() copies the whole
 * stack into undo_st (once per key, guarded by snap_taken, which every key /
 * touch / menu return clears). UNDO swaps the two stacks, so a second UNDO is
 * a redo. LASTARG: commands that consume levels first copy them into lbuf
 * (save_lastarg); LASTARG pushes them back in their original order.
 *
 * Evaluation: every operation builds linear CAS text in xbuf from the levels
 * it consumes (lvl_ref: the level's variable, or its text for a bare name),
 * each wrapped in parentheses, evaluates it with cas_eval (so `ans` follows
 * the last result as on the home screen) into rbuf, assigning it to a new
 * hidden variable (eval_level: "_rpnN:=(...)") and replaces those levels by the result
 * (replace_top). The MODE number format is applied only for display
 * (level_disp).
 *
 * Level variables: a stack entry is the text "N\x1fTEXT": N is the number of
 * the CAS variable _rpnN holding the value (0: none, the level is just its
 * text, e.g. DEPTH, or a value the CAS could not store), TEXT the CAS output.
 * Stack operations (DUP, SWAP, ROLL, PICK, LASTARG, UNDO) move entries, so
 * copies of a level share its variable, which is what keeps x DUP - at 0±0.
 * A variable may be referenced by the stack, the UNDO snapshot and the
 * LASTARG copies; gc() purges the ones none of them references any more
 * (after every key), so dropped levels leave no CAS variables behind. The
 * number of variables is limited (vmax: 1024, 128 in the small work area);
 * when all are in use the UNDO snapshot is given up, then "Stack full".
 * MEM > Clear CAS variables deletes them: the sentinel _rpnok is then
 * missing and ensure_vars() rebuilds the variables from the level texts.
 * Saving: the level texts and, via cas_save_prefix, the variables' values
 * in full precision with their uncertainty sources (_uncN, which the CAS
 * session blob shares), see rpn_save.
 */
#include "rpn.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "forms_ui.h"
#include "registry.h"
#include "session.h"
#include "ui.h"

/* ------------------------------------------------------------ state */
static bool rpn_on;                 /* MODE > ENTRY is RPN (saved) */
static rstack_t st, undo_st;        /* the stack and the UNDO snapshot (two halves of the work area) */
static bool undo_valid, snap_taken; /* undo_st holds a snapshot; snapshot already taken for this key */
static char *xbuf;          /* expression being built */
static char *rbuf;          /* CAS result */
static char *fbuf, *fbuf2;  /* display formatting */
static char *lbuf;          /* LASTARG entries, oldest first */
static char *ebuf;          /* one stack entry being composed ("N\x1fTEXT") */
static size_t lvl_max, xcap, fcap, lcap, llen; /* longest level text (+NUL), buffer sizes; bytes in lbuf */
static int nlast;                               /* number of LASTARG texts in lbuf (0: none) */
static editor_t ed, saved_ed; /* command line; its state at the last draw */
static bool saved_numeric; /* saved_ed was a plain number (menu_returned may push it) */
static int edit_level; /* > 0: the command line edits this level in place */
static uint32_t edit_hash; /* hash of the command line when the edit began (unchanged: keep the value) */
static int sel;        /* > 0: interactive stack, selected level */
static int scroll;     /* levels hidden below the visible ones */
static bool follow_sel; /* next draw: adjust `scroll` so that the selected level is visible */
static char err[80];    /* error of the last key, shown over the top of the stack until the next key */
static const rpn_renderer_t *renderer; /* rpn_set_renderer(); NULL: text_renderer */
static bool args_i;     /* the levels used by the last build() contain i (COMPLEX REAL check) */

/* Level variables _rpn1.._rpn<vmax> (see the top of the file): vlive marks
 * the ones defined in the CAS. */
#define VAR_PREFIX "_rpn"
#define VMAX_BIG 1024
#define SEP '\x1f' /* between the variable number and the text of an entry */
static uint8_t vlive[VMAX_BIG / 8 + 1]; /* bit id: ids 1..vmax */
static int vmax;

/* Layout: the stack fills the body above a one-row command line (LINE_Y, a
 * large-font row just above the soft-key bar); a 1 px separator line (and a
 * 2 px blue bar when levels are scrolled out below) sits between them. */
#define LINE_Y (UI_BOTTOM - CH)
#define STK_Y BODY_Y
#define STK_H (LINE_Y - 2 - STK_Y)

bool rpn_active(void) { return rpn_on; }

static bool vis_live(int id) { return id > 0 && id <= vmax && (vlive[id >> 3] >> (id & 7) & 1); }
static void vset(uint8_t *m, int id, bool on)
{
    if (on)
        m[id >> 3] |= (uint8_t)(1u << (id & 7));
    else
        m[id >> 3] &= (uint8_t)~(1u << (id & 7));
}

/* Purge the CAS variable _rpn<id> and forget it. */
static void purge_var(int id)
{
    char cmd[32];
    snprintf(cmd, sizeof cmd, "purge(" VAR_PREFIX "%d)", id);
    cas_eval_quiet(g.cas, cmd, NULL, 0);
    vset(vlive, id, false);
}

/* Empty stack, no UNDO/LASTARG, empty command line, leave every sub-mode.
 * The level variables are purged (when there is a CAS: rpn_init runs before
 * cas_init, on a fresh CAS). */
static void reset_stack(void)
{
    for (int id = 1; id <= vmax; id++)
        if (vis_live(id) && g.cas)
            purge_var(id);
    memset(vlive, 0, sizeof vlive);
    rs_clear(&st);
    rs_clear(&undo_st);
    undo_valid = false;
    nlast = 0;
    llen = 0;
    ed_clear(&ed);
    saved_ed = ed;
    saved_numeric = false;
    edit_level = sel = scroll = 0;
    err[0] = 0;
}

/* Carve the work area (RPN_WORK bytes from the arena, see app_init) into the
 * scratch buffers and the two stacks:
 *   xbuf   4*lvl_max+256   CAS expression being built (several levels + operators)
 *   rbuf   lvl_max         CAS result, i.e. the longest text one level can hold
 *   ebuf   lvl_max+16      a stack entry being composed (variable number + text)
 *   fbuf, fbuf2  2*lvl_max each: display formatting (numfmt, prettifying)
 *   lbuf   2*lvl_max       LASTARG copies
 *   the rest, split in two equal halves: the stack and the UNDO snapshot.
 * lvl_max is 2048 for the full 256 KB area, 512 for the small fallback. A work
 * area that cannot hold the scratch buffers plus 1 KB leaves everything NULL
 * (RPN disabled). Also selects algebraic entry (the saved choice is restored
 * later by rpn_load). Called once from app_init. */
void rpn_init(void *work, size_t size)
{
    lvl_max = size >= (128u << 10) ? 2048 : 512;
    vmax = size >= (128u << 10) ? VMAX_BIG : 128;
    memset(vlive, 0, sizeof vlive); /* the CAS is about to be created afresh */
    xcap = 4 * lvl_max + 256;
    fcap = 2 * lvl_max;
    lcap = 2 * lvl_max;
    size_t fixed = xcap + 2 * lvl_max + 16 + 2 * fcap + lcap;
    char *p = work;
    if (!p || size < fixed + 1024) {
        rs_init(&st, NULL, 0);
        rs_init(&undo_st, NULL, 0);
        xbuf = rbuf = ebuf = fbuf = fbuf2 = lbuf = NULL;
    } else {
        xbuf = p, p += xcap;
        rbuf = p, p += lvl_max;
        ebuf = p, p += lvl_max + 16;
        fbuf = p, p += fcap;
        fbuf2 = p, p += fcap;
        lbuf = p, p += lcap;
        size_t half = (size - fixed) / 2;
        rs_init(&st, p, half);
        rs_init(&undo_st, p + half, half);
    }
    reset_stack();
    rpn_on = false;
    ui_set_home(NULL);
}

void rpn_set_renderer(const rpn_renderer_t *r) { renderer = r; }

/* ------------------------------------------------------------ helpers */
static bool ident_char(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* true if s contains the imaginary unit `i` as a whole word (not inside a
 * name such as `sin` or `i2`) */
static bool has_i(const char *s)
{
    for (const char *p = s; *p; p++)
        if (*p == 'i' && (p == s || !ident_char(p[-1])) && !ident_char(p[1]))
            return true;
    return false;
}

/* a variable name: identifier syntax, and not a built-in constant such as pi */
static bool is_ident(const char *s)
{
    static const char *const consts[] = {"pi", "e", "i", "inf", "infinity", "undef", "true", "false",
                                         "euler_gamma", "ans"};
    if (!isalpha((unsigned char)s[0]) && s[0] != '_')
        return false;
    for (const char *p = s; *p; p++)
        if (!ident_char(*p))
            return false;
    for (size_t k = 0; k < sizeof consts / sizeof consts[0]; k++)
        if (!strcmp(s, consts[k]))
            return false;
    return true;
}

/* w is one of the space-separated words of list (list starts and ends with a
 * space, so every word has a space on each side) */
static bool in_list(const char *list, const char *w) /* list: " a b c " */
{
    size_t l = strlen(w);
    for (const char *p = strstr(list, w); p; p = strstr(p + 1, w))
        if (p[-1] == ' ' && p[l] == ' ')
            return true;
    return false;
}

static void set_err(const char *msg) { snprintf(err, sizeof err, "%s", msg); }

/* the stack has at least n levels (else the error "Too few arguments") */
static bool need(int n)
{
    if (st.depth >= n)
        return true;
    set_err("Too few arguments");
    return false;
}

/* ------------------------------------------------------------ level variables */
/* variable number of a stack entry "N\x1fTEXT" (0: none) */
static int ent_id(const char *e) { return atoi(e); }
/* the text of a stack entry */
static const char *ent_text(const char *e)
{
    const char *p = strchr(e, SEP);
    return p ? p + 1 : e;
}
/* text of level lv (NULL if there is no such level) */
static const char *lvl(int lv)
{
    const char *e = rs_get(&st, lv);
    return e ? ent_text(e) : NULL;
}
static int lvl_id(int lv) { return ent_id(rs_get(&st, lv)); }

/* Compose the entry for variable id and text into ebuf; NULL if the text is
 * longer than a level may be. */
static const char *make_entry(int id, const char *text)
{
    size_t l = strlen(text);
    if (l >= lvl_max)
        return NULL;
    int k = snprintf(ebuf, 16, "%d%c", id, SEP);
    memcpy(ebuf + k, text, l + 1);
    return ebuf;
}

/* How level lv appears in a CAS expression: its variable, as 1*_rpnN (not
 * the bare name: commands such as subs and quo hold the names given after
 * their first argument, as they do for x in diff(f, x)); a level that is a
 * bare name (x) or has no variable as its text. Returns a static buffer or
 * the level text. */
static const char *lvl_ref(int lv)
{
    static char b[24];
    const char *t = lvl(lv);
    int id = lvl_id(lv);
    if (!vis_live(id) || is_ident(t))
        return t;
    snprintf(b, sizeof b, "1*" VAR_PREFIX "%d", id);
    return b;
}

/* mark the variables referenced by the entries in b[0..len) */
static void mark_entries(uint8_t *m, const char *b, size_t len)
{
    for (const char *p = b; p && p < b + len; p += strlen(p) + 1) {
        int id = ent_id(p);
        if (id > 0 && id <= vmax)
            vset(m, id, true);
    }
}

/* Purge the variables that neither the stack, nor the UNDO snapshot, nor the
 * LASTARG copies reference (run after every key: dropped levels leave no CAS
 * variables behind). */
static void gc(void)
{
    if (!g.cas)
        return;
    uint8_t m[sizeof vlive];
    memset(m, 0, sizeof m);
    mark_entries(m, st.buf, st.len);
    if (undo_valid)
        mark_entries(m, undo_st.buf, undo_st.len);
    if (nlast)
        mark_entries(m, lbuf, llen);
    for (int id = 1; id <= vmax; id++)
        if (vis_live(id) && !(m[id >> 3] >> (id & 7) & 1))
            purge_var(id);
}

static int free_id(void)
{
    for (int id = 1; id <= vmax; id++)
        if (!vis_live(id))
            return id;
    return 0;
}

/* Define _rpn<id> := (text); false if the CAS refuses it. */
static bool define_from_text(int id, const char *text)
{
    snprintf(xbuf, xcap, VAR_PREFIX "%d:=(%s)", id, text);
    if (cas_eval_quiet(g.cas, xbuf, NULL, 0) != CAS_OK)
        return false;
    vset(vlive, id, true);
    return true;
}

/* A free level variable number: after garbage collection and, if still none,
 * giving up the UNDO snapshot; -1 if every variable is in use. */
static int take_id(void)
{
    int id = free_id();
    if (!id) {
        gc();
        id = free_id();
    }
    if (!id && undo_valid) {
        undo_valid = false;
        gc();
        id = free_id();
    }
    return id ? id : -1;
}

/* A new level variable holding the value of CAS text src (a variable name:
 * the same value, unlinearised, as the variable holds); 0 if the CAS refuses,
 * -1 if none is free. */
static int var_from(const char *src)
{
    int id = take_id();
    if (id < 0)
        return -1;
    snprintf(xbuf, xcap, VAR_PREFIX "%d:=%s", id, src);
    if (cas_eval_quiet(g.cas, xbuf, NULL, 0) != CAS_OK)
        return 0;
    vset(vlive, id, true);
    return id;
}

/* Give every stack level whose variable is missing a variable made from its
 * text; a level for which that fails keeps only its text (variable 0). With
 * `check` (rpn_load) the CAS is asked first which variables exist (restored
 * from the session), so that no new variable takes the number of a restored
 * one. The UNDO snapshot and LASTARG are given up when an entry changes. */
static void rebuild_levels(bool check)
{
    char out[8], nm[16];
    for (int lv = 1; check && lv <= st.depth; lv++) {
        int id = lvl_id(lv);
        if (id <= 0 || id > vmax || vis_live(id))
            continue;
        snprintf(nm, sizeof nm, VAR_PREFIX "%d", id);
        cas_status s = cas_eval_quiet(g.cas, nm, out, sizeof out);
        if ((s == CAS_OK && strcmp(out, nm)) || s == CAS_ERR_OUTPUT_TRUNC)
            vset(vlive, id, true); /* restored by rpn_load */
    }
    for (int lv = 1; lv <= st.depth; lv++) {
        int id = lvl_id(lv);
        bool valid = id > 0 && id <= vmax;
        if (valid && vis_live(id))
            continue; /* defined (or another level with the same variable was rebuilt) */
        if (valid && define_from_text(id, lvl(lv)))
            continue;
        int nid = valid ? 0 : free_id();
        id = nid && define_from_text(nid, lvl(lv)) ? nid : 0;
        const char *e = make_entry(id, lvl(lv));
        if (e && strcmp(e, rs_get(&st, lv))) {
            rs_replace(&st, lv, e);
            undo_valid = false;
            nlast = 0;
            llen = 0;
        }
    }
}

/* MEM > Clear CAS variables (cas_reset) also deletes the level variables:
 * when the sentinel _rpnok is missing, rebuild them from the level texts
 * (the levels then are new values, as if typed again). Called before every
 * key; the sentinel is defined here first. */
static void ensure_vars(void)
{
    char out[8];
    if (!g.cas || !st.buf)
        return;
    if (cas_eval_quiet(g.cas, VAR_PREFIX "ok", out, sizeof out) == CAS_OK && !strcmp(out, "1"))
        return;
    memset(vlive, 0, sizeof vlive);
    undo_valid = false;
    nlast = 0;
    llen = 0;
    rebuild_levels(false);
    cas_eval_quiet(g.cas, VAR_PREFIX "ok:=1", NULL, 0);
}

/* Save the stack for UNDO before the first change made by the current key
 * (later changes by the same key, e.g. several words on one command line, are
 * undone together). undo_valid is false when the copy did not fit. */
static void snapshot(void)
{
    if (snap_taken)
        return;
    snap_taken = true;
    undo_valid = rs_copy(&undo_st, &st);
}

/* Remember levels n..1 (oldest first) as the LASTARG arguments (their
 * entries, so LASTARG brings back the same values); arguments
 * too big for lbuf leave no LASTARG at all rather than a partial one. */
static void save_lastarg(int n)
{
    llen = 0;
    nlast = 0;
    for (int lv = n; lv >= 1; lv--) {
        const char *s = rs_get(&st, lv);
        size_t l = strlen(s) + 1;
        if (llen + l > lcap) { /* too big to keep: no LASTARG */
            llen = 0;
            nlast = 0;
            return;
        }
        memcpy(lbuf + llen, s, l);
        llen += l;
        nlast++;
    }
}

/* bytes (with NULs) used by levels 1..n */
static size_t top_bytes(int n)
{
    size_t b = 0;
    for (int lv = 1; lv <= n; lv++)
        b += strlen(rs_get(&st, lv)) + 1;
    return b;
}

/* Replace levels 1..n by the value with text `val` (n = 0: push) held by
 * level variable id (eval_level; 0: the level is just the text, DEPTH).
 * Undoable; LASTARG = the levels. The size check counts the bytes the
 * dropped levels free, so the stack is never left half-changed. */
static bool replace_top(int n, const char *val, int id)
{
    if (!need(n))
        return false;
    if (strlen(val) + 16 > rs_free(&st) + top_bytes(n) || strlen(val) >= lvl_max) {
        set_err("Stack full");
        return false;
    }
    snapshot();
    if (n)
        save_lastarg(n);
    rs_drop(&st, n);
    rs_push(&st, make_entry(id, val));
    return true;
}

/* cas_status names for error messages (cas.h order; OUTPUT_TRUNC is reported
 * separately by eval_cas) */
static const char *const status_names[] = {"OK", "SYNTAX", "DOMAIN", "UNDEFINED", "MEMORY",
                                           "BREAK", "UNSUPPORTED", "OVERFLOW"};

/* Evaluate CAS text into rbuf (errors: err). Like the home screen: the Y=
 * functions are defined first (yvars_sync) and the evaluation updates `ans`.
 * In MODE COMPLEX REAL a result containing i is an error unless the input
 * already contained i (the user asked for a complex number): in the text, or
 * in_i (the levels it refers to by their variables contain i). */
static cas_status last_status; /* of the last eval_cas */
static bool eval_cas(const char *in, bool in_i)
{
    yvars_sync();
    cas_status s = cas_eval(g.cas, in, rbuf, lvl_max);
    last_status = s;
    if (s == CAS_ERR_OUTPUT_TRUNC) {
        set_err("ERR:MEMORY result too long");
        return false;
    }
    if (s != CAS_OK) {
        const char *m = cas_last_error(g.cas);
        snprintf(err, sizeof err, "ERR:%s %s", (unsigned)s < 8 ? status_names[s] : "?", m ? m : "");
        return false;
    }
    if (g.cplx == CPLX_REAL && numfmt_nonreal(rbuf) && !in_i && !has_i(in)) {
        set_err("ERR:NONREAL ANS (MODE: a+bi)");
        return false;
    }
    return true;
}

/* Evaluate CAS text `in` (it may be xbuf) as the value of a new level:
 * "_rpnN:=(in)" stores the value as an assignment does (unlinearised, as
 * x:=2±0.1 keeps it, so that x-x is 0±0) and prints it into rbuf; *id is
 * the variable. If the CAS cannot assign the result (e.g. the stub CAS's
 * solve, or its variable table is full), it is evaluated as it is and the
 * level gets no variable (*id = 0). false (err set) on errors,
 * also "Stack full" when every variable is in use. */
static bool eval_level(const char *in, bool in_i, int *id)
{
    *id = take_id();
    if (*id < 0) {
        set_err("Stack full");
        return false;
    }
    char pre[24];
    size_t k = (size_t)snprintf(pre, sizeof pre, VAR_PREFIX "%d:=(", *id), l = strlen(in);
    if (k + l + 2 > xcap) {
        set_err("ERR:MEMORY expression too long");
        return false;
    }
    memmove(xbuf + k, in, l + 1); /* in may be xbuf */
    memcpy(xbuf, pre, k);
    memcpy(xbuf + k + l, ")", 2);
    bool ok = eval_cas(xbuf, in_i);
    if (last_status == CAS_OK || last_status == CAS_ERR_OUTPUT_TRUNC)
        vset(vlive, *id, true); /* assigned (gc purges it if the level is not pushed) */
    if (ok) {
        /* MODE SIMPLIFY AUTO: the level's variable gets the autosimplify
         * form (app/result_actions.c); fbuf is free here (display only) */
        if (fbuf && autosimp_wanted(xbuf + k, rbuf)) {
            char var[16];
            snprintf(var, sizeof var, VAR_PREFIX "%d", *id);
            snprintf(fbuf, lvl_max, "%s", rbuf);
            if (autosimp_var(xbuf + k, var, fbuf, lvl_max))
                snprintf(rbuf, lvl_max, "%s", fbuf);
        }
        return true;
    }
    memmove(xbuf, xbuf + k, l);
    xbuf[l] = 0;
    if (last_status == CAS_OK || last_status == CAS_ERR_OUTPUT_TRUNC || last_status == CAS_ERR_INTERRUPTED)
        return false; /* assigned, but too long or not real (gc purges the variable), or ON pressed */
    *id = 0;
    return eval_cas(xbuf, in_i);
}

/* append s to xbuf at *o; false if xbuf (xcap) would overflow */
static bool cat(size_t *o, const char *s)
{
    size_t l = strlen(s);
    if (*o + l + 1 > xcap)
        return false;
    memcpy(xbuf + *o, s, l + 1);
    *o += l;
    return true;
}

/* xbuf = pre (Ln) [, extra] sep (Ln-1) sep ... (L1) post, each level as
 * lvl_ref gives it (its variable)
 * Builds the CAS expression of an operation on levels n..1 in stack order
 * (the oldest level first), each level parenthesized so that operator
 * precedence inside the values cannot change the meaning: 2: a+b 1: c with
 * pre "", sep "*" gives "(a+b)*(c)". `extra` follows the first argument (the
 * template's variable: "solve((x^2-2),x)"). */
static bool build(int n, const char *pre, const char *sep, const char *post, const char *extra)
{
    size_t o = 0;
    bool ok = cat(&o, pre);
    args_i = false;
    for (int lv = n; lv >= 1 && ok; lv--) {
        args_i |= has_i(lvl(lv));
        ok = cat(&o, "(") && cat(&o, lvl_ref(lv)) && cat(&o, ")");
        if (ok && lv == n && extra)
            ok = cat(&o, ",") && cat(&o, extra);
        if (ok && lv > 1)
            ok = cat(&o, sep);
    }
    ok = ok && cat(&o, post);
    if (!ok)
        set_err("ERR:MEMORY expression too long");
    return ok;
}

/* evaluate xbuf and replace levels 1..n by the result */
static bool run(int n)
{
    int id;
    return eval_level(xbuf, args_i, &id) && replace_top(n, rbuf, id);
}

/* the usual operation: check the depth, build "pre (Ln) sep ... (L1) post", run */
static bool apply(int n, const char *pre, const char *sep, const char *post)
{
    return need(n) && build(n, pre, sep, post, NULL) && run(n);
}

/* Display text of a stack value: complex mode, number format, √ π ᴇ.
 * In MODE r∠θ a complex value is shown as abs∠arg, computed by two quiet CAS
 * evaluations (in degrees when ANGLE is DEG); if either is not a plain real the
 * value is shown as it is. Returns fbuf2 (valid until the next call). Works
 * only in its own buffers fbuf / fbuf2 (2*lvl_max each: the polar
 * expressions and the argument go to the halves of fbuf, the modulus to
 * fbuf2), so it may run at any time, also while xbuf / rbuf hold an
 * expression being built. */
static const char *level_disp(const char *raw)
{
    if (!fbuf)
        return raw;
    const char *src = raw;
    char *expr = fbuf, *argv = fbuf + lvl_max; /* fbuf halves: expression, argument */
    if (g.cplx == CPLX_POLAR && numfmt_nonreal(raw) && strlen(raw) + 32 < lvl_max) {
        snprintf(expr, lvl_max, "abs(%s)", raw);
        if (cas_eval_quiet(g.cas, expr, fbuf2, fcap / 2) == CAS_OK) {
            snprintf(expr, lvl_max, g.cs.angle == CAS_ANGLE_DEG ? "arg(%s)*180/pi" : "arg(%s)", raw);
            if (cas_eval_quiet(g.cas, expr, argv, lvl_max) == CAS_OK && !has_i(fbuf2) && !has_i(argv) &&
                !strstr(argv, "arg(")) {
                size_t l = strlen(fbuf2);
                snprintf(fbuf2 + l, fcap - l, "∠%s", argv);
                src = fbuf2;
            }
        }
    }
    numfmt_apply(src, fbuf, fcap);
    pretty_from_cas(fbuf, fbuf2, fcap);
    return fbuf2;
}

/* ------------------------------------------------------------ commands */
/* stack commands (STACK menu actions, soft keys, words typed on the command
 * line: cmd_names, case-insensitive) */
enum {
    S_DUP, S_DUP2, S_DUPN, S_DROP, S_DROP2, S_DROPN, S_SWAP, S_OVER, S_ROT, S_ROLL, S_ROLLD, S_PICK,
    S_DEPTH, S_CLEAR, S_LASTARG, S_UNDO, S_NUM, S_NCMD
};
static const char *const cmd_names[S_NCMD] = {"DUP",  "DUP2",  "DUPN", "DROP",  "DROP2", "DROPN",
                                              "SWAP", "OVER",  "ROT",  "ROLL",  "ROLLD", "PICK",
                                              "DEPTH", "CLEAR", "LASTARG", "UNDO", "→NUM"};

/* Command index of a word typed on the command line, or -1. "NUM" and
 * "→NUM" (the → key's \x01 marker followed by NUM) both mean →NUM. */
static int find_cmd(const char *w)
{
    if (!strcasecmp(w, "num") || !strcasecmp(w, "\x01num"))
        return S_NUM;
    for (int i = 0; i < S_NCMD; i++)
        if (!strcasecmp(w, cmd_names[i]))
            return i;
    return -1;
}

/* UNDO: swap the stack with the snapshot (the rstack_t structs, i.e. the
 * buffer pointers, are swapped: no copying). */
static void do_undo(void)
{
    if (!undo_valid) {
        set_err("No UNDO");
        return;
    }
    rstack_t t = st; /* swap: UNDO twice is redo */
    st = undo_st;
    undo_st = t;
    edit_level = 0;
}

/* Pop the count n from level 1 (ROLL, PICK, DUPN, DROPN): an integer
 * min..depth-1 (the levels left once the count is gone). On success the count
 * is LASTARG and the snapshot is taken, so UNDO restores it too. */
static bool pop_count(int *n, int min)
{
    if (!need(1))
        return false;
    const char *s = lvl(1);
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s || *end || v < min || v > st.depth - 1) {
        set_err("Bad argument value");
        return false;
    }
    *n = (int)v;
    snapshot();
    save_lastarg(1);
    rs_drop(&st, 1);
    return true;
}

/* pass ok through; on failure report "Stack full" */
static bool full(bool ok)
{
    if (!ok)
        set_err("Stack full");
    return ok;
}

/* Push a copy of level n (DUP, OVER, PICK): checked before the UNDO snapshot,
 * so a "Stack full" refusal leaves the previous UNDO in place. */
static bool pick(int n)
{
    if (strlen(rs_get(&st, n)) + 1 > rs_free(&st))
        return full(false);
    snapshot();
    return full(rs_pick(&st, n));
}

/* Run stack command c (S_*). Errors go to err; returns false on failure.
 * Pure rearrangements (DUP, SWAP, ROLL, ...) do not involve the CAS; only
 * →NUM evaluates (approx). */
static bool stack_cmd(int c)
{
    int n;
    switch (c) {
    case S_DUP:
        return need(1) && pick(1);
    case S_DUP2:
        if (!need(2))
            return false;
        snapshot();
        return full(rs_pick(&st, 2) && rs_pick(&st, 2));
    case S_DUPN:
        if (!pop_count(&n, 0))
            return false;
        for (int i = 0; i < n; i++) /* picking level n n times copies levels n..1 in order */
            if (!full(rs_pick(&st, n)))
                return false;
        return true;
    case S_DROP:
    case S_DROP2:
        n = c == S_DROP ? 1 : 2;
        if (!need(n))
            return false;
        snapshot();
        save_lastarg(n);
        return rs_drop(&st, n);
    case S_DROPN:
        return pop_count(&n, 0) && rs_drop(&st, n);
    case S_SWAP:
    case S_ROT:
        n = c == S_SWAP ? 2 : 3;
        if (!need(n))
            return false;
        snapshot();
        save_lastarg(n);
        return rs_roll(&st, n);
    case S_OVER:
        return need(2) && pick(2);
    case S_ROLL:
        return pop_count(&n, 0) && (n == 0 || rs_roll(&st, n));
    case S_ROLLD:
        return pop_count(&n, 0) && (n == 0 || rs_rolld(&st, n));
    case S_PICK:
        return pop_count(&n, 1) && full(rs_pick(&st, n));
    case S_DEPTH: {
        char b[16];
        snprintf(b, sizeof b, "%d", st.depth);
        return replace_top(0, b, 0);
    }
    case S_CLEAR:
        if (st.depth) {
            snapshot();
            rs_clear(&st);
        }
        return true;
    case S_LASTARG: {
        if (!nlast) {
            set_err("No LASTARG");
            return false;
        }
        if (llen > rs_free(&st))
            return full(false);
        snapshot();
        for (const char *p = lbuf; p < lbuf + llen; p += strlen(p) + 1)
            rs_push(&st, p);
        return true;
    }
    case S_UNDO:
        do_undo();
        return true;
    case S_NUM:
        return apply(1, "approx(", "", ")");
    }
    return false;
}

/* ------------------------------------------------------------ functions
 * Argument convention (the usual RPN convention): arguments in stack order, the
 * last argument in level 1: f(a,b) takes a from level 2 and b from level 1.
 * Commands whose menu template has a variable (`solve( ,x)`, `diff( ,x)`,
 * `integrate( ,x,0,1)`, `limit( ,x,0)`) take the variable from the stack when
 * that level holds a name (2: x^2-2  1: x  solve), else use the template's
 * variable and take the other arguments from the stack (1: x^2-2  solve, or
 * 3: x^2  2: 0  1: 1  integrate(,x,a,b)). */
static const char two_args[] = " mod min max gcd lcm comb perm binomial nCr nPr atan2 iquo irem invmod iegcd "
                               "dot cross concat subs quo rem resultant powmod pm ucov convert ";
static const char one_arg[] = " factor expand simplify approx exact sqrt abs sin cos tan asin acos atan sinh "
                              "cosh tanh asinh acosh atanh ln log exp floor ceil round frac sign re im conj "
                              "arg det inv transpose ifactor isprime numer denom normal together partfrac "
                              "cfactor collect sort reverse mean median stddev variance size length sum "
                              "product trigsimp texpand tcollect ratsimp lhs rhs left right eigenvals rref "
                              "rank trace norm nextprime prevprime divisors factorial gamma erf nominal uncert ";
static const struct {
    const char *name, *defaults;
} var_fns[] = {
    {"diff", ",x)"},   {"integrate", ",x)"}, {"int", ",x)"},       {"solve", ",x)"},
    {"csolve", ",x)"}, {"fsolve", ",x)"},    {"nsolve", ",x)"},    {"roots", ",x)"},
    {"limit", ",x,0)"}, {"nderiv", ",x,0)"}, {"tangent", ",x,0)"}, {"nint", ",x,0,1)"},
};

/* w is a CAS function that the command line applies to the stack when typed
 * as a bare word (`5 factor`, `x^2-2 solve`) */
static bool fn_known(const char *w)
{
    for (size_t i = 0; i < sizeof var_fns / sizeof var_fns[0]; i++)
        if (!strcmp(w, var_fns[i].name))
            return true;
    return in_list(two_args, w) || in_list(one_arg, w);
}

/* Apply CAS function `name` to the stack; `suffix` is the menu template text
 * after the argument (",x)", ",x,0,1)", ")" or "").
 *
 * When the template (or, for a bare name, the var_fns default) has extra
 * arguments ",x,a,b)", k = their number and `var` = the first one. If var is a
 * name: when level k holds a name too, it is the variable and k+1 levels are
 * consumed (template integrate(,x,a,b) with 4: x^2 3: t 2: 0 1: 1 ->
 * integrate((x^2),(t),(0),(1))); otherwise the template's variable is spliced
 * in after the first of k levels (3: x^2 2: 0 1: 1 -> integrate((x^2),x,(0),(1));
 * for ",x)" one level: solve((x^2-2),x)). Functions without a variable take 2
 * levels if listed in two_args, else 1. */
static bool apply_fn(const char *name, const char *suffix)
{
    char pre[48];
    snprintf(pre, sizeof pre, "%s(", name);
    const char *d = suffix ? suffix : "";
    if (*d != ',')
        for (size_t i = 0; i < sizeof var_fns / sizeof var_fns[0]; i++)
            if (!strcmp(name, var_fns[i].name))
                d = var_fns[i].defaults;
    if (*d == ',') {
        int k = 0, depth = 0; /* k: top-level commas in d = number of extra arguments */
        char var[24] = "";
        for (const char *p = d; *p; p++) {
            if (*p == '(' || *p == '[')
                depth++;
            else if ((*p == ')' || *p == ']') && depth)
                depth--;
            else if (*p == ',' && !depth && k++ == 0) {
                size_t l = strcspn(p + 1, ",)");
                if (l < sizeof var)
                    memcpy(var, p + 1, l), var[l] = 0;
            }
        }
        if (is_ident(var)) {
            if (st.depth >= k + 1 && is_ident(lvl(k)))
                return apply(k + 1, pre, ",", ")");
            return need(k) && build(k, pre, ",", ")", var) && run(k);
        }
    }
    return apply(in_list(two_args, name) ? 2 : 1, pre, ",", ")");
}

/* Operator / function text of a key or menu entry: 1 binary, 2 function
 * "name(", 3 postfix, 0 none. Binary operators are recognised by their CAS
 * text, postfix ones (², ⁻¹, !, °) by their display text, functions by a
 * display text ending in "(". */
static int op_kind(const char *disp, const char *cas)
{
    static const char *const bin[] = {"+", "-", "*", "/", "^", "=", "!=", ">", ">=", "<", "<="};
    static const char *const post[] = {"²", "³", "⁻¹", "!", "°", "ʳ"};
    for (size_t i = 0; i < sizeof bin / sizeof bin[0]; i++)
        if (!strcmp(cas, bin[i]))
            return 1;
    for (size_t i = 0; i < sizeof post / sizeof post[0]; i++)
        if (!strcmp(disp, post[i]))
            return 3;
    size_t l = strlen(disp);
    if (l > 1 && disp[l - 1] == '(')
        return 2;
    return 0;
}

/* Apply an operator of op_kind `kind` with CAS text `cas` to the stack:
 * binary (L2)op(L1), postfix (L1)op, function op((L1)). */
static bool apply_op(int kind, const char *cas)
{
    if (kind == 1)
        return apply(2, "", cas, "");
    if (kind == 3)
        return apply(1, "", "", cas);
    return apply(1, cas, "", ")"); /* "sin(" -> sin((a)) */
}

/* The command line holds something a menu inserted (on an empty line):
 * apply it to the stack if it is a command template. dry: only classify.
 * The tokens are split at the cursor (where the menu left it, i.e. the hole of
 * the template) into `before` and `after`, as CAS text. Recognised shapes:
 *   one function token + rest    "solve(" ",x)"  -> apply_fn (named functions)
 *                                "10^(" ")"      -> 10^((L1))
 *   one operator / postfix token "=", "!", "³"   -> apply_op
 *   a template with a hole       "(" ")^(1/3)"   -> before(L1)after
 *     starting with an operator  "^(1/" ")"      -> (L2)before(L1)after
 * Anything else (plain text such as a variable name) is not a command and
 * stays on the line. */
static bool apply_template(const editor_t *e, bool dry)
{
    if (!e->len)
        return false;
    char before[ED_MAX * 2], after[ED_MAX * 2];
    size_t bo = 0, ao = 0;
    before[0] = after[0] = 0;
    for (int i = 0; i < e->len; i++) {
        const char *c = tok_cas(e->t[i]);
        size_t l = strlen(c);
        char *b = i < e->cur ? before : after;
        size_t *o = i < e->cur ? &bo : &ao;
        if (*o + l + 1 > sizeof before)
            return false;
        memcpy(b + *o, c, l + 1);
        *o += l;
    }
    const char *d0 = tok_disp(e->t[0]);
    if (e->cur == 1 && op_kind(d0, before) == 2) { /* "name(" + template rest */
        if (dry)
            return true;
        before[bo - 1] = 0;
        if (is_ident(before))
            return apply_fn(before, after);
        return apply(1, tok_cas(e->t[0]), "", ao ? after : ")"); /* "10^(" */
    }
    if (e->len == 1 && e->cur == 1) { /* operator or postfix: =, ≠, !, ³, ° */
        int k = op_kind(d0, before);
        if (k == 1 || k == 3)
            return dry || apply_op(k, before);
        return false;
    }
    if (!ao || !bo)
        return false;
    if (dry) /* a template with a hole: "(" ")^(1/3)", "^(1/" ")" */
        return true;
    if (strchr("^*/+-", before[0]))
        return apply(2, "", before, after);
    return apply(1, before, "", after);
}

/* ------------------------------------------------------------ command line */
/* The command line is a plain number being typed: digits, '.', ᴇ, spaces and
 * (-) only at the start of a number or exponent (spaces separate several
 * numbers: "1 2 3"). Then operator keys push it first and act on the stack;
 * otherwise they are typed into the line like on the home screen. */
static bool line_numeric(const editor_t *e)
{
    bool digit = false;
    for (int i = 0; i < e->len; i++) {
        int t = e->t[i];
        if (t >= '0' && t <= '9')
            digit = true;
        else if (t == TOK_NEG && i > 0 && e->t[i - 1] != TOK_EE && e->t[i - 1] != ' ')
            return false;
        else if (t != '.' && t != ' ' && t != TOK_EE && t != TOK_NEG)
            return false;
    }
    return digit && e->t[e->len - 1] != TOK_EE;
}

/* One word of the command line: an operator, a stack command, a function
 * name or a value (expression; "expr→name" stores). The → key's CAS text is
 * the \x01 marker, rewritten here to "name:=expr"; the assignment's result
 * is pushed like any value. */
static bool exec_word(char *w)
{
    if (!w[1] && strchr("+-*/^", w[0]))
        return apply(2, "", w, "");
    int c = find_cmd(w);
    if (c >= 0)
        return stack_cmd(c);
    if (is_ident(w) && fn_known(w))
        return apply_fn(w, "");
    char *sto = strchr(w, '\x01');
    if (sto) {
        *sto = 0;
        const char *var = sto + 1;
        while (*var == ' ')
            var++;
        snprintf(xbuf, xcap, "%s:=%s", var, w);
        *sto = '\x01';
    } else {
        snprintf(xbuf, xcap, "%s", w);
    }
    int id;
    if (!sto)
        return eval_level(xbuf, false, &id) && replace_top(0, rbuf, id);
    if (!eval_cas(xbuf, false)) /* name:=expr, then the level is a copy of the variable's value */
        return false;
    if ((id = var_from(sto + 1)) < 0)
        return full(false);
    return replace_top(0, rbuf, id);
}

/* FNV-1a hash of the command line's tokens */
static uint32_t line_hash(void)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < ed.len; i++)
        h = (h ^ ed.t[i]) * 16777619u;
    return h;
}

/* ENTER: evaluate the command line (space-separated words, left to right),
 * or replace the level being edited. On an error the failing word and the
 * rest stay on the line. Words are split at spaces outside brackets, so
 * "f(1, 2)" stays one word. When editing a level, a "→name" part is ignored
 * and the edited value replaces the level (undoable). Returns false on error. */
static bool commit_line(void)
{
    char cas[ED_MAX * 4 + 16];
    ed_cas(&ed, cas, sizeof cas);
    if (edit_level && line_hash() == edit_hash) { /* unchanged: keep the value */
        edit_level = 0;
        ed_clear(&ed);
        return true;
    }
    if (edit_level) {
        char *sto = strchr(cas, '\x01');
        if (sto)
            *sto = 0;
        int id;
        if (edit_level > st.depth || !eval_level(cas, false, &id))
            return false;
        size_t old = strlen(rs_get(&st, edit_level));
        if (strlen(rbuf) + 16 > old && strlen(rbuf) + 16 - old > rs_free(&st))
            return full(false);
        snapshot();
        rs_replace(&st, edit_level, make_entry(id, rbuf));
        edit_level = 0;
        ed_clear(&ed);
        return true;
    }
    char *p = cas;
    int done = 0;
    while (*p) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        char *w = p;
        int depth = 0;
        while (*p && (*p != ' ' || depth > 0)) {
            if (strchr("([{", *p))
                depth++;
            else if (strchr(")]}", *p) && depth)
                depth--;
            p++;
        }
        char sv = *p;
        *p = 0;
        bool ok = exec_word(w);
        *p = sv;
        if (!ok) {
            if (done) { /* keep what failed */
                pretty_from_cas(w, fbuf, fcap);
                ed_set_text(&ed, fbuf);
            }
            return false;
        }
        done++;
    }
    ed_clear(&ed);
    return true;
}

/* insert the display text of level lv at the command line's cursor (ECHO) */
static void level_to_line(int lv)
{
    pretty_from_cas(lvl(lv), fbuf, fcap);
    ed_insert_text(&ed, fbuf, NULL);
}

/* Edit level lv in place: its text goes on the command line; ENTER replaces
 * the level (commit_line) by the value of the edited text, unless the text
 * was not changed (the level keeps its value, which may hold more than its
 * text shows); CLEAR abandons the edit. Leaves the interactive stack. */
static void begin_edit(int lv)
{
    ed_clear(&ed);
    level_to_line(lv);
    edit_level = lv;
    edit_hash = line_hash();
    sel = 0;
}

/* (-) on a number being typed changes its sign (or the exponent's after ᴇ) */
static void toggle_sign(void)
{
    int pos = 0;
    for (int i = 0; i < ed.len; i++)
        if (ed.t[i] == ' ' || ed.t[i] == TOK_EE)
            pos = i + 1;
    if (pos < ed.len && ed.t[pos] == TOK_NEG) {
        memmove(&ed.t[pos], &ed.t[pos + 1], (size_t)(ed.len - pos - 1) * sizeof ed.t[0]);
        ed.len--;
    } else if (ed.len < ED_MAX) {
        memmove(&ed.t[pos + 1], &ed.t[pos], (size_t)(ed.len - pos) * sizeof ed.t[0]);
        ed.t[pos] = (uint16_t)TOK_NEG;
        ed.len++;
    }
    ed.cur = ed.len;
}

/* the command line holds just a variable name (→ then stores level 1) */
static bool is_ident_line(void)
{
    char b[ED_MAX * 4];
    ed_cas(&ed, b, sizeof b);
    return is_ident(b);
}

/* → (STO): with a name on the command line store level 1 into it; on an
 * empty line level 1 must be a name and level 2 the value. The consumed
 * levels are dropped (LASTARG, undoable); the variable is defined by a CAS
 * assignment `name:=(value)`. */
static void store(void)
{
    char name[ED_MAX * 4];
    ed_cas(&ed, name, sizeof name);
    bool on_line = !ed_empty(&ed);
    int n = on_line ? 1 : 2;
    if (!on_line && !(st.depth >= 2 && is_ident(lvl(1)))) {
        set_err("STO: type a name first");
        return;
    }
    if (!need(n))
        return;
    if (!on_line)
        snprintf(name, sizeof name, "%s", lvl(1));
    snprintf(xbuf, xcap, "%s:=(%s)", name, lvl_ref(n)); /* the value itself: correlations are kept */
    if (!eval_cas(xbuf, has_i(lvl(n))))
        return;
    snapshot();
    save_lastarg(n);
    rs_drop(&st, n);
    ed_clear(&ed);
}

/* ------------------------------------------------------------ STACK menu */
/* STACK menu item chosen: run the command, then return to the RPN screen
 * without the menu-return logic of rpn_enter (ui_returning is forced only for
 * this switch, so the screen keeps its state). */
static void stack_select(const menu_item_t *it)
{
    snap_taken = false;
    err[0] = 0;
    ensure_vars();
    stack_cmd(it->action);
    gc();
    ui_returning = true;
    ui_switch(&scr_rpn);
    ui_returning = false;
}

static const menu_item_t stack_items[] = {
    {"DUP", NULL, NULL, S_DUP, "rpn-stack"},       {"SWAP", NULL, NULL, S_SWAP, "rpn-stack"},
    {"DROP", NULL, NULL, S_DROP, "rpn-stack"},     {"OVER", NULL, NULL, S_OVER, "rpn-stack"},
    {"ROT", NULL, NULL, S_ROT, "rpn-stack"},       {"ROLL  (n)", NULL, NULL, S_ROLL, "rpn-stack"},
    {"ROLLD (n)", NULL, NULL, S_ROLLD, "rpn-stack"}, {"PICK  (n)", NULL, NULL, S_PICK, "rpn-stack"},
    {"DUP2", NULL, NULL, S_DUP2, "rpn-stack"},     {"DUPN  (n)", NULL, NULL, S_DUPN, "rpn-stack"},
    {"DROP2", NULL, NULL, S_DROP2, "rpn-stack"},   {"DROPN (n)", NULL, NULL, S_DROPN, "rpn-stack"},
    {"DEPTH", NULL, NULL, S_DEPTH, "rpn-stack"},   {"CLEAR", NULL, NULL, S_CLEAR, "rpn-stack"},
    {"LASTARG", NULL, NULL, S_LASTARG, "rpn-stack"}, {"UNDO", NULL, NULL, S_UNDO, "rpn-stack"},
    {"→NUM", NULL, NULL, S_NUM, "rpn-stack"},
};
const menu_def_t menu_rpn_stack = {"STACK", stack_items, sizeof stack_items / sizeof stack_items[0],
                                   stack_select, false, "rpn-stack"};

/* ------------------------------------------------------------ drawing */
/* Built-in renderer: rows of CW-wide characters needed for disp at width w
 * (cols receives the characters per row), at most RPN_TEXT_ROWS. */
static int text_rows(const char *disp, int w, int *cols)
{
    int c = w / CW > 0 ? w / CW : 1;
    int n = utf8_len(disp);
    int rows = n <= c ? 1 : (n + c - 1) / c;
    *cols = c;
    return rows > RPN_TEXT_ROWS ? RPN_TEXT_ROWS : rows;
}

static int text_measure(const char *cas, const char *disp, int w)
{
    int c;
    return text_rows(disp, w, &c) * CH;
}

/* Built-in renderer: one row right-aligned; longer values left-aligned and
 * wrapped character by character (UTF-8 aware), the last row ending in "..."
 * when the text needs more than RPN_TEXT_ROWS rows. */
static void text_draw(const char *cas, const char *disp, int x, int y, int w, int h, uint16_t fg, uint16_t bg)
{
    int cols, rows = text_rows(disp, w, &cols);
    if (rows == 1) {
        gfx_text(FONT, x + w - gfx_text_width(FONT, disp), y, disp, fg, GFX_TRANSPARENT);
        return;
    }
    bool trunc = utf8_len(disp) > rows * cols;
    const char *s = disp;
    char line[COLS * 4 + 8];
    for (int r = 0; r < rows && *s; r++) {
        int lim = trunc && r == rows - 1 ? cols - 3 : cols;
        const char *p = s;
        for (int k = 0; k < lim && *p; k++)
            utf8_next(&p);
        size_t l = (size_t)(p - s);
        memcpy(line, s, l);
        strcpy(line + l, trunc && r == rows - 1 ? "..." : "");
        gfx_text(FONT, x, y + r * CH, line, fg, GFX_TRANSPARENT);
        s = p;
    }
}

static const rpn_renderer_t text_renderer = {text_measure, text_draw};

/* The levels on screen after the last layout(): level number, top y and
 * height in pixels (also used to hit-test taps), and the renderer that
 * measured it (the installed one, or the built-in text when that one returned
 * -1 or none is installed). vis[0] is the lowest. */
typedef struct {
    int level, y, h;
    const rpn_renderer_t *r;
} vis_t;
#define MAXVIS 16
static vis_t vis[MAXVIS];
static int nvis;

/* width of the "n:" level label column plus padding */
static int label_w(int lv)
{
    char b[12];
    return snprintf(b, sizeof b, "%d:", lv) * 7 + 8; /* font_medium is 7 px wide */
}

/* levels from scroll+1 upwards, bottom to top: each level is measured by the
 * renderer (falling back to the built-in text when it returns -1) at the
 * width right of its label, clamped to [CH, STK_H], and
 * stacked upwards from the bottom of the stack area until the top is reached;
 * the topmost level may be partly above STK_Y (clipped when drawn) */
static void layout(void)
{
    nvis = 0;
    int y = STK_Y + STK_H;
    for (int lv = scroll + 1; lv <= st.depth && y > STK_Y && nvis < MAXVIS; lv++) {
        const char *raw = lvl(lv), *disp = level_disp(raw);
        const rpn_renderer_t *r = renderer ? renderer : &text_renderer;
        int w = LCD_WIDTH - 2 - label_w(lv), h = r->measure(raw, disp, w);
        if (h < 0)
            r = &text_renderer, h = text_measure(raw, disp, w);
        if (h < CH)
            h = CH;
        if (h > STK_H)
            h = STK_H;
        y -= h;
        vis[nvis++] = (vis_t){lv, y, h, r};
    }
}

/* Draw the stack, the error line and the command line (or, in the interactive
 * stack, a hint instead of the command line). First clamps sel / edit_level /
 * scroll to the current depth, since commands may have shrunk the stack. Also
 * records the command line (saved_ed) for menu_returned. */
static void rpn_draw(void)
{
    if (sel > st.depth)
        sel = st.depth;
    if (edit_level > st.depth)
        edit_level = 0;
    if (scroll > st.depth - 1)
        scroll = st.depth > 0 ? st.depth - 1 : 0;
    if (sel > 0 && follow_sel) { /* scroll so that the selected level is visible */
        if (sel <= scroll)
            scroll = sel - 1;
        for (;;) { /* scroll up until the selected level is fully inside the stack area */
            layout();
            bool seen = false;
            for (int i = 0; i < nvis; i++)
                seen |= vis[i].level == sel && vis[i].y >= STK_Y;
            if (seen || scroll >= sel - 1)
                break;
            scroll++;
        }
    }
    follow_sel = false;
    layout();
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    gfx_set_clip(0, STK_Y, LCD_WIDTH, STK_H);
    for (int i = 0; i < nvis; i++) {
        const vis_t *v = &vis[i];
        bool hl = v->level == sel;
        uint16_t bg = hl ? C_SEL_BG : v->level == edit_level ? C_LTGRAY : C_WHITE;
        uint16_t fg = hl ? C_WHITE : C_BLACK;
        if (bg != C_WHITE)
            gfx_fill_rect(0, v->y, LCD_WIDTH, v->h, bg);
        char lab[12];
        snprintf(lab, sizeof lab, "%d:", v->level);
        gfx_text(&font_medium, 2, v->y + 3, lab, hl ? C_WHITE : C_DKGRAY, GFX_TRANSPARENT);
        const char *raw = lvl(v->level);
        int x0 = label_w(v->level);
        v->r->draw(raw, level_disp(raw), x0, v->y, LCD_WIDTH - 2 - x0, v->h, fg, bg);
    }
    gfx_reset_clip();
    gfx_hline(0, LINE_Y - 1, LCD_WIDTH, C_LTGRAY);
    if (scroll > 0) /* levels hidden below: blue bar */
        gfx_fill_rect(0, LINE_Y - 2, LCD_WIDTH, 2, C_SEL_BG);
    if (err[0]) {
        gfx_fill_rect(0, STK_Y, LCD_WIDTH, CH, C_LTGRAY);
        gfx_text(&font_medium, 4, STK_Y + 3, err, C_RED, GFX_TRANSPARENT);
    }
    if (sel > 0) { /* interactive stack: the soft keys act on the selected level */
        ui_cursor_hide();
        gfx_fill_rect(0, LINE_Y, LCD_WIDTH, CH, C_WHITE);
        gfx_text(&font_small, 4, LINE_Y + 4, "level selected: ENTER echo, ⌫ drop, F1-F5", C_GRAY,
                 GFX_TRANSPARENT);
    } else {
        ui_draw_editor_line(&ed, 0, LINE_Y, COLS, C_BLACK, C_WHITE, true);
    }
    saved_ed = ed;
    saved_numeric = line_numeric(&ed);
}

/* ------------------------------------------------------------ keys */
static void rpn_key(int key, int mod);

/* Keys in the interactive stack (sel > 0; entered with UP on an empty line or
 * a tap on a level). The selected level is n = sel:
 *   ENTER/1/F1 ECHO (copy to the command line)   2/F2 PICK   3/F3 ROLL
 *   4/F4 ROLLD   5/F5 DROPN (levels 1..n)   6/SHIFT F1 EDIT in place
 *   UP/DOWN move the selection (DOWN from level 1 leaves)   ⌫ drop level n
 *   CLEAR leave; any other key leaves and is handled as a normal key.
 * Each of the numbered actions also leaves the interactive stack. */
static void interactive(int key, int mod)
{
    int n = sel;
    int action = key == KEY_ENTER ? 1 : 0;
    static const uint8_t digit_keys[6] = {KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6};
    for (int i = 0; i < 6; i++)
        if (key == digit_keys[i] && mod == MOD_NONE)
            action = i + 1;
    if (KEY_IS_SOFT(key)) /* F1..F5: ECHO PICK ROLL ROLLD DROPN, SHIFT F1: EDIT */
        action = mod == MOD_SHIFT ? (key == KEY_F1 ? 6 : 0) : KEY_SOFT_INDEX(key) + 1;
    switch (action) {
    case 1: /* ECHO: copy to the command line */
        sel = 0;
        level_to_line(n);
        return;
    case 2: /* PICK */
        pick(n);
        sel = 0;
        return;
    case 3: /* ROLL */
        snapshot();
        rs_roll(&st, n);
        sel = 0;
        return;
    case 4: /* ROLLD */
        snapshot();
        rs_rolld(&st, n);
        sel = 0;
        return;
    case 5: /* DROPN: levels 1..n */
        snapshot();
        rs_drop(&st, n);
        sel = 0;
        return;
    case 6: /* EDIT in place */
        begin_edit(n);
        return;
    default:
        break;
    }
    switch (key) {
    case KEY_UP:
        if (sel < st.depth)
            sel++;
        follow_sel = true;
        break;
    case KEY_DOWN:
        sel--;
        follow_sel = true;
        if (!sel)
            scroll = 0;
        break;
    case KEY_DEL: /* drop the selected level only */
        snapshot();
        rs_roll(&st, n);
        save_lastarg(1);
        rs_drop(&st, 1);
        if (sel > st.depth)
            sel = st.depth;
        follow_sel = true;
        break;
    case KEY_CLEAR:
        sel = scroll = 0;
        break;
    default: /* any other key leaves the interactive stack and does its job */
        sel = scroll = 0;
        rpn_key(key, mod);
        break;
    }
}

/* Key handler of scr_rpn. "plain" = the line is empty or a number being typed
 * and no level is being edited: then operators, soft keys, ) and , act on the
 * stack (a typed number is pushed first by commit_line). Otherwise keys edit
 * the command line like on the home screen. Errors are shown until the next
 * key. Redraws the screen. */
static void rpn_key(int key, int mod)
{
    if (!st.buf)
        return;
    snap_taken = false;
    err[0] = 0;
    if (sel > 0) {
        interactive(key, mod);
        rpn_draw();
        return;
    }
    scroll = 0;
    bool empty = ed_empty(&ed);
    bool plain = edit_level == 0 && (empty || line_numeric(&ed)); /* operators act on the stack */
    char t[16];
    int tok, kind;
    if (KEY_IS_SOFT(key)) { /* soft keys: stack commands (a number being typed is pushed first) */
        static const uint8_t cmds[2][5] = {{S_SWAP, S_DROP, S_ROLL, S_PICK, S_UNDO},
                                           {S_DUP, S_OVER, S_ROT, S_NCMD, S_NCMD}};
        int c = cmds[mod == MOD_SHIFT][KEY_SOFT_INDEX(key)];
        if (c == S_NCMD) { /* SHIFT F4 simplify level 1, SHIFT F5 the REWRITE menu (app/result_actions.c) */
            if (key == KEY_F4)
                rpn_result_action("simplify", "");
            else if (!edit_level && (empty || commit_line())) /* the REWRITE menu for level 1 */
                rewrite_open(st.depth ? level_disp(lvl(1)) : "");
            else
                rpn_draw();
            return;
        }
        if (!edit_level && (empty || (plain && commit_line())))
            stack_cmd(c);
        else if (!err[0]) /* text that is not a number (or a level being edited) */
            set_err("Press ENTER first");
        rpn_draw();
        return;
    }
    switch (key) {
    case KEY_ENTER:
        if (mod == MOD_SHIFT) {
            if (empty)
                do_undo();
        } else if (empty) {
            stack_cmd(S_DUP);
        } else {
            commit_line();
        }
        break;
    case KEY_CLEAR:
        if (mod == MOD_SHIFT) { /* clear all */
            ed_clear(&ed);
            edit_level = 0;
            stack_cmd(S_CLEAR);
        } else if (!empty || edit_level) {
            ed_clear(&ed);
            edit_level = 0;
        } else {
            stack_cmd(S_CLEAR);
        }
        break;
    case KEY_DEL:
        if (empty && mod == MOD_NONE && !edit_level)
            stack_cmd(S_DROP);
        else
            ui_editor_key(&ed, key, mod);
        break;
    case KEY_UP:
        if (empty && st.depth && !edit_level) {
            sel = 1;
            follow_sel = true;
        }
        break;
    case KEY_DOWN:
        if (empty && st.depth && !edit_level)
            begin_edit(1);
        break;
    case KEY_ASSIGN:
        if (mod == MOD_NONE && !edit_level && (empty || is_ident_line()))
            store();
        else
            ui_editor_key(&ed, key, mod);
        break;
    default:
        if (plain && empty && mod == MOD_NONE && key == KEY_RPAREN) {
            stack_cmd(S_SWAP);
        } else if (plain && empty && mod == MOD_NONE && key == KEY_COMMA) {
            menu_open(&menu_rpn_stack);
            return;
        } else if (plain && empty && mod == MOD_SHIFT && key == KEY_NEG) {
            stack_cmd(S_LASTARG);
        } else if (plain && mod == MOD_NONE && key == KEY_NEG) {
            if (empty)
                apply(1, "-", "", "");
            else
                toggle_sign();
        } else if (plain && mod != MOD_ALPHA && ui_key_text(key, mod, 0, t, sizeof t) &&
                   (tok = tok_find_disp(t)) >= 0 && (kind = op_kind(t, tok_cas(tok))) != 0) {
            if (empty || commit_line()) /* a number being typed is pushed first */
                apply_op(kind, tok_cas(tok));
        } else if (!ui_editor_key(&ed, key, mod)) {
            return;
        }
    }
    rpn_draw();
}

/* scr_rpn's key handler: rpn_key between the level-variable upkeep (rebuild
 * them after MEM > Clear CAS variables; purge the ones no longer used). */
static void rpn_key_top(int key, int mod)
{
    if (!st.buf)
        return;
    ensure_vars();
    rpn_key(key, mod);
    gc();
}

static editor_t *rpn_editor(void) { return &ed; }

bool rpn_clip_text(char *out, size_t n)
{
    out[0] = 0;
    const char *raw = sel > 0 ? lvl(sel) : NULL;
    if (!raw && !ed_empty(&ed)) {
        ed_display(&ed, out, n);
        return out[0] != 0;
    }
    if (!raw && st.depth > 0)
        raw = lvl(1);
    if (raw)
        snprintf(out, n, "%s", level_disp(raw));
    return out[0] != 0;
}

/* Back from a menu: if it inserted a command template into an empty line (or
 * after a number being typed), apply the command to the stack.
 * The line before the menu (saved_ed, recorded by the last draw) must be a
 * prefix of the current line with the cursor at its end, i.e. the menu only
 * appended. The appended tokens are classified by apply_template; if they are
 * a command, the line is restored, a typed number is pushed (commit_line), and
 * the command is applied. Otherwise the inserted text stays on the line. */
static void menu_returned(void)
{
    snap_taken = false;
    ensure_vars();
    if (ed.len != saved_ed.len)
        sel = 0; /* something was inserted: leave the interactive stack */
    if (edit_level || saved_ed.cur != saved_ed.len || ed.len <= saved_ed.len ||
        memcmp(ed.t, saved_ed.t, (size_t)saved_ed.len * sizeof ed.t[0]))
        return;
    if (saved_ed.len && !saved_numeric)
        return;
    editor_t ins;
    ins.len = ed.len - saved_ed.len;
    ins.cur = ed.cur - saved_ed.len;
    ins.scroll = 0;
    memcpy(ins.t, ed.t + saved_ed.len, (size_t)ins.len * sizeof ed.t[0]);
    if (ins.cur < 0 || !apply_template(&ins, true))
        return;
    err[0] = 0;
    ed = saved_ed;
    if (saved_ed.len && !commit_line())
        return;
    apply_template(&ins, false);
    ed_clear(&ed);
    gc();
}

/* enter: returning from a menu/message runs menu_returned; a fresh visit
 * leaves the interactive stack and clears the error (the stack is kept) */
static void rpn_enter(void)
{
    if (ui_returning) {
        menu_returned();
        return;
    }
    sel = scroll = 0;
    err[0] = 0;
}

/* Touch: tap a level to select it (interactive stack), tap it again to copy
 * it to the command line; drag to scroll; in the interactive stack the bottom
 * soft-key bar acts on the selected level. */
static int drag_acc; /* drag distance not yet converted into whole CH-high scroll steps */

static void rpn_touch(const touch_event_t *ev)
{
    if (!st.buf)
        return;
    snap_taken = false;
    switch (ev->type) {
    case TEV_DOWN:
        drag_acc = 0;
        return;
    case TEV_DRAG:
        drag_acc += ev->dy; /* dragging down reveals higher (older) levels */
        while (drag_acc >= CH && scroll < st.depth - 1)
            scroll++, drag_acc -= CH;
        while (drag_acc <= -CH && scroll > 0)
            scroll--, drag_acc += CH;
        break;
    case TEV_TAP:
        err[0] = 0;
        if (ev->y >= LINE_Y) {
            if (sel == 0)
                ui_editor_tap(&ed, ed.scroll + ev->x / CW);
            break;
        }
        {
            int lv = 0;
            for (int i = 0; i < nvis; i++)
                if (ev->y >= vis[i].y && ev->y < vis[i].y + vis[i].h)
                    lv = vis[i].level;
            if (lv && lv == sel) {
                interactive(KEY_ENTER, MOD_NONE);
            } else if (lv && ed_empty(&ed) && !edit_level) {
                sel = lv;
            } else {
                sel = 0;
            }
        }
        break;
    default:
        return;
    }
    rpn_draw();
}

/* Soft keys: labels only, so they reach rpn_key / interactive as KEY_F1..F5
 * and share the "push the typed number first" logic. */
static const softkeys_t rpn_soft = {
    {{"swap"}, {"drop"}, {"roll"}, {"pick"}, {"undo"}},
    {{"dup"}, {"over"}, {"rot"}, {"simplify"}, {"rewrite"}},
};

/* REWRITE "convert to (unit)" in RPN: convert(level 2, level 1) when level 1
 * is a unit (_ft, pushed from the UNITS menu or typed); otherwise an error
 * that says so. A line being typed is pushed first. Redraws. */
void rpn_result_convert(void)
{
    if (!st.buf)
        return;
    snap_taken = false;
    err[0] = 0;
    sel = 0;
    ensure_vars();
    if (edit_level)
        set_err("Press ENTER first");
    else if ((ed_empty(&ed) || commit_line()) && need(1)) {
        if (st.depth >= 2 && lvl(1)[0] == '_')
            apply(2, "convert(", ",", ")");
        else
            set_err("convert: push the unit first (SHIFT VARS)");
    }
    gc();
    if (ui_current() == &scr_rpn)
        rpn_draw();
}

/* Result action (SHIFT F4 simplify, the REWRITE menu; app/result_actions.c):
 * apply CAS command fn to level 1 like a command from a menu (apply_fn: one
 * level, or level 2 with the variable in level 1 for collect(,x)); fn NULL:
 * approx or exact, depending on whether level 1 is a decimal. A line being
 * typed is pushed first (any expression, not only a number); a level being
 * edited must be entered first. Undoable, LASTARG keeps the argument.
 * Redraws. */
void rpn_result_action(const char *fn, const char *extra)
{
    if (!st.buf)
        return;
    snap_taken = false;
    err[0] = 0;
    sel = 0;
    ensure_vars();
    if (edit_level)
        set_err("Press ENTER first");
    else if ((ed_empty(&ed) || commit_line()) && need(1)) {
        if (!fn)
            fn = result_is_approx(level_disp(lvl(1))) ? "exact" : "approx";
        char suffix[16];
        snprintf(suffix, sizeof suffix, "%s)", extra ? extra : "");
        apply_fn(fn, suffix);
    }
    gc();
    if (ui_current() == &scr_rpn)
        rpn_draw();
}
/* REWRITE "forms" taking a form: it replaces level 1 (undoable, LASTARG
 * keeps the old level), evaluated like a typed expression. */
static void rpn_forms_paste(const char *in, const char *value)
{
    (void)in;
    if (!st.buf)
        return;
    snap_taken = false;
    err[0] = 0;
    sel = 0;
    ensure_vars();
    size_t o = 0;
    if (need(1) && cat(&o, value)) {
        args_i = has_i(lvl(1));
        run(1);
    }
    gc();
}

/* REWRITE "forms" in RPN: the alternate forms browser (app/forms_ui.c) for
 * level 1 (a line being typed was pushed when the menu opened). */
void rpn_result_forms(void)
{
    if (!st.buf)
        return;
    err[0] = 0;
    ensure_vars();
    if (edit_level)
        set_err("Press ENTER first");
    else if (need(1)) {
        forms_open(lvl_ref(1), "forms", rpn_forms_paste); /* copies the text at once */
        return;
    }
    if (ui_current() == &scr_rpn)
        rpn_draw();
}

static const softkeys_t rpn_soft_level = {
    {{"echo"}, {"pick"}, {"roll"}, {"rolld"}, {"dropn"}},
    {{"edit"}},
};
static const softkeys_t *rpn_softkeys(void) { return sel > 0 ? &rpn_soft_level : &rpn_soft; }

const screen_t scr_rpn = {"rpn", "RPN", rpn_enter, rpn_draw, rpn_key_top, NULL, rpn_editor, rpn_touch, "rpn",
                          NULL, rpn_softkeys};

/* ------------------------------------------------------------ MODE row */
static int entry_get(void) { return rpn_on; }
/* RPN can only be switched on when rpn_init got a work area */
static void entry_set(int c)
{
    rpn_on = c && st.buf != NULL;
    ui_set_home(rpn_on ? &scr_rpn : NULL);
}
const mode_row_t mode_entry = {"ENTRY", {"ALGEBRAIC", "RPN"}, 2, entry_get, entry_set, "rpn"};

/* ------------------------------------------------------------ persistence
 * Version 2: u8 rpn on, str command line (ed_serialize), u16 level being
 * edited, u16 n, n entries ("N\x1fTEXT") from level n down to level 1, then
 * u32 len and len bytes of CAS session records (cas_save_prefix: "v _rpnN
 * EXPR" lines, floats with 17 digits and uncertainty sources kept as _uncN)
 * for the variables of those levels. The sources are shared with the CAS
 * session blob, so after a power cycle the levels stay correlated with each
 * other and with the user's variables. At most RPN_SAVE_MAX bytes of levels
 * are kept (the oldest, highest levels are dropped first) and at most
 * RPN_SAVE_MAX bytes of records; a level whose record is missing is rebuilt
 * from its text on load. Version 1 (texts only) is still read. */
#define RPN_SAVE_MAX (32u << 10)

typedef struct {
    pw_t *w;
    size_t start;        /* w->len when the records started */
    const uint8_t *keep; /* variables of the saved levels */
} rec_sink_t;

/* cas_save_prefix writer: keep the records of the saved levels' variables
 * that fit (the sentinel _rpnok and unreferenced variables are dropped). */
static int rec_sink(void *user, const char *data, size_t n)
{
    rec_sink_t *s = user;
    size_t pl = strlen(VAR_PREFIX);
    for (const char *p = data, *end = data + n; p < end;) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t l = nl ? (size_t)(nl - p) + 1 : (size_t)(end - p);
        int id = l > 2 + pl && !memcmp(p + 2, VAR_PREFIX, pl) ? atoi(p + 2 + pl) : 0;
        if (id > 0 && id <= vmax && (s->keep[id >> 3] >> (id & 7) & 1) &&
            s->w->len + l - s->start <= RPN_SAVE_MAX && s->w->len + l + 64 <= s->w->cap)
            pw_bytes(s->w, p, l);
        p += l;
    }
    return 0;
}

static void rpn_save(pw_t *w)
{
    char b[ED_MAX * 8];
    pw_u8(w, rpn_on);
    ed_serialize(&ed, b, sizeof b);
    pw_str(w, b);
    int n = rs_levels_within(&st, RPN_SAVE_MAX);
    pw_u16(w, (uint32_t)(edit_level <= n ? edit_level : 0));
    pw_u16(w, (uint32_t)n);
    const char *p = n ? rs_get(&st, n) : NULL;
    for (int i = 0; i < n; i++, p += strlen(p) + 1)
        pw_str(w, p);
    size_t at = w->len;
    pw_u32(w, 0);
    if (!g.cas || !n || w->overflow)
        return;
    uint8_t keep[sizeof vlive];
    memset(keep, 0, sizeof keep);
    const char *first = rs_get(&st, n);
    mark_entries(keep, first, (size_t)(st.buf + st.len - first));
    rec_sink_t s = {w, w->len, keep};
    cas_save_prefix(g.cas, VAR_PREFIX, rec_sink, &s);
    uint32_t len = (uint32_t)(w->len - at - 4);
    for (int k = 0; k < 4; k++)
        w->buf[at + (size_t)k] = (uint8_t)(len >> (8 * k));
}

/* Restore the blob (version 1: texts, which become new values; version 2:
 * entries and the variables' records). A level whose stored length does not
 * fit (saved with a larger work area) is skipped, and so is a level that no
 * longer fits the stack; the level being edited is then forgotten, since the
 * level numbers have shifted. A corrupt blob leaves an empty stack. Levels
 * whose variable was not restored get one made from their text. */
static void rpn_load(pr_t *r, unsigned version)
{
    char b[ED_MAX * 8];
    reset_stack();
    bool on = pr_u8(r) != 0;
    pr_str(r, b, sizeof b);
    unsigned el = pr_u16(r), n = pr_u16(r);
    bool skipped = false;
    for (unsigned i = 0; i < n && !r->err && rbuf; i++) {
        /* the u16 length pr_str is about to read */
        size_t len = pr_left(r) >= 2 ? (size_t)(r->buf[r->pos] | r->buf[r->pos + 1] << 8) : 0;
        pr_str(r, ebuf, lvl_max + 16);
        if (r->err)
            break;
        const char *sep = strchr(ebuf, SEP);
        const char *e = NULL;
        if (len < lvl_max + 16) {
            snprintf(rbuf, lvl_max, "%s", version >= 2 && sep ? sep + 1 : ebuf);
            e = make_entry(version >= 2 && sep ? atoi(ebuf) : 0, rbuf);
        }
        if (!e || !rs_push(&st, e))
            skipped = true;
    }
    if (r->err)
        rs_clear(&st);
    uint32_t rl = version >= 2 && !r->err ? pr_u32(r) : 0;
    if (g.cas && st.buf) {
        if (rl && !r->err && rl <= pr_left(r))
            cas_restore(g.cas, (const char *)r->buf + r->pos, rl);
        rebuild_levels(true);
        cas_eval_quiet(g.cas, VAR_PREFIX "ok:=1", NULL, 0);
    }
    ed_deserialize(&ed, b);
    edit_level = (int)el <= st.depth && !skipped ? (int)el : 0;
    entry_set(on);
}

/* MEM > Reset all: empty stack, algebraic entry */
static void rpn_reset(void)
{
    reset_stack();
    entry_set(0);
}

const persist_def_t persist_rpn = {"rpn", 2, rpn_save, rpn_load, rpn_reset};

/* ------------------------------------------------------------ test state */
/* cas_save_prefix writer counting the "v _rpnN" records (rpn_cas_vars) */
static int count_sink(void *user, const char *data, size_t n)
{
    for (const char *p = data; p < data + n; p++)
        if ((p == data || p[-1] == '\n') && !strncmp(p, "v " VAR_PREFIX, 2 + strlen(VAR_PREFIX)) &&
            p[2 + strlen(VAR_PREFIX)] >= '0' && p[2 + strlen(VAR_PREFIX)] <= '9')
            ++*(int *)user;
    return 0;
}

void rpn_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[ED_MAX * 4 + 8];
    kv("rpn", rpn_on ? "1" : "0", user);
    snprintf(b, sizeof b, "%d", st.depth);
    kv("rpn_depth", b, user);
    for (int lv = 1; lv <= 6; lv++) {
        char k[12];
        snprintf(k, sizeof k, "rpn_%d", lv);
        const char *raw = lvl(lv);
        kv(k, raw ? level_disp(raw) : "", user);
    }
    ed_display(&ed, b, sizeof b);
    kv("rpn_line", b, user);
    snprintf(b, sizeof b, "%d", sel);
    kv("rpn_sel", b, user);
    snprintf(b, sizeof b, "%d", scroll);
    kv("rpn_scroll", b, user);
    snprintf(b, sizeof b, "%d", edit_level);
    kv("rpn_edit", b, user);
    kv("rpn_error", err, user);
    int nv = 0, ncas = 0;
    for (int id = 1; id <= vmax; id++)
        nv += vis_live(id);
    snprintf(b, sizeof b, "%d", nv);
    kv("rpn_vars", b, user);
    if (g.cas)
        cas_save_prefix(g.cas, VAR_PREFIX, count_sink, &ncas);
    snprintf(b, sizeof b, "%d", ncas);
    kv("rpn_cas_vars", b, user);
}
