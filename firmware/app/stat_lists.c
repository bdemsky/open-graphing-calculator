/*
 * Statistics lists: L1..L6, RESID and named lists (stat.h).
 *
 * The values live here (doubles, a pool carved from the arena by app_init),
 * saved with the session ("stat.lists"). They are also CAS variables: before
 * a home-screen evaluation every list named in the input is (re)defined in the
 * CAS ("L1:=[1,2,3]"), and after an assignment the lists named in the input
 * are read back, so "L1*2→L3", "mean(L1)", "sort(L2)→L2" work. A list can
 * carry a column formula (editor: "L1*2" in quotes) that is re-evaluated
 * whenever another list changes.
 *
 * Storage. lists[] has SL_MAX fixed slots: 0..5 are L1..L6, SL_RESID (6) is
 * RESID (filled by the regressions), 7..SL_MAX-1 hold named lists created on
 * demand (`used` marks a slot in use; L1..L6 and RESID are always in use).
 * stat_mem_init() carves one contiguous block from the app arena:
 *
 *     [ SL_MAX lists x cap doubles ][ st_scratch: 3*cap doubles ][ text: 32 KB ]
 *
 * with cap = 999 elements per list normally (BIG_CAP) and 32 elements and a
 * 2 KB text buffer in the hardware's small fallback arena (SDRAM failure).
 * st_scratch (stat_math.c) is the work area of every statistics computation
 * that needs sorted copies or transformed data (quartiles, regressions,
 * sorting in the STAT menu, the normal probability plot). `text` is the
 * buffer for CAS input/output when lists are sent to or read back from the
 * CAS; stat_text() shares it with the rest of the app.
 *
 * Other state: sl_cols[] / sl_ncols are the editor's columns (SetUpEditor);
 * stat_last / stat_last_reg are the last CALC/TESTS results shown under
 * VARS > Statistics. Persistence ("stat.lists", lists_save/lists_load) keeps
 * every used list (name, formula, values) and the editor columns.
 *
 * All functions run in the main loop (no interrupt context).
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: size over speed (not time-critical) */
#endif
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "registry.h"
#include "session.h"
#include "stat.h"

static slist_t lists[SL_MAX];
static int cap;          /* elements per list */
static char *text;       /* CAS text buffer (sync, formulas) */
static size_t text_cap;
int sl_cols[SL_MAX];
int sl_ncols;
sres_t stat_last;
sreg_t stat_last_reg;
bool stat_have_reg;

#define BIG_CAP 999  /* elements per list with the normal arena */
#define SMALL_CAP 32 /* elements per list with the fallback arena */

/* Factory state: L1..L6 and RESID empty and named, no named lists, no
 * formulas, editor columns L1..L6, no remembered results. Used at init, by
 * MEM > Reset all and when a saved blob turns out to be corrupt. */
static void defaults(void)
{
    for (int i = 0; i < SL_MAX; i++) {
        slist_t *l = &lists[i];
        l->len = 0;
        l->formula[0] = 0;
        l->used = i <= SL_RESID;
        if (i < 6)
            snprintf(l->name, sizeof l->name, "L%d", i + 1);
        else if (i == SL_RESID)
            snprintf(l->name, sizeof l->name, "RESID");
        else
            l->name[0] = 0;
    }
    sl_editor_default();
    memset(&stat_last, 0, sizeof stat_last);
    stat_have_reg = false;
}

/* Called once from app_init() with the next free part of the arena. `big`
 * selects the normal or the fallback-arena sizes (see the file header). Sets
 * the list value pointers, st_scratch / st_scratch_cap and the text buffer,
 * resets lists and stat plots to their defaults, and returns the number of
 * bytes consumed so the caller can advance its arena pointer. */
size_t stat_mem_init(void *mem, bool big)
{
    cap = big ? BIG_CAP : SMALL_CAP;
    double *p = mem;
    for (int i = 0; i < SL_MAX; i++, p += cap)
        lists[i].v = p;
    st_scratch = p;
    st_scratch_cap = 3 * cap;
    p += st_scratch_cap;
    text = (char *)p;
    text_cap = big ? 32u << 10 : 2u << 10;
    defaults();
    stat_plot_init();
    return (size_t)((char *)p - (char *)mem) + text_cap;
}

int sl_cap(void) { return cap; }

/* The shared CAS text buffer; *n gets its size. Its content is overwritten by
 * every list definition / readback, so callers must not hold on to it. */
char *stat_text(size_t *n)
{
    *n = text_cap;
    return text;
}

slist_t *sl_at(int slot) { return slot >= 0 && slot < SL_MAX && lists[slot].used ? &lists[slot] : NULL; }

slist_t *sl_find(const char *name)
{
    for (int i = 0; i < SL_MAX; i++)
        if (lists[i].used && !strcmp(lists[i].name, name))
            return &lists[i];
    return NULL;
}

/* A named list's name: 1..SL_NAME-1 letters/digits starting with a letter,
 * and not a name the CAS gives a meaning (it becomes a CAS variable). */
bool sl_valid_name(const char *name)
{
    size_t n = strlen(name);
    if (n < 1 || n >= SL_NAME || !isalpha((unsigned char)name[0]))
        return false;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)name[i]))
            return false;
    /* names the CAS reserves */
    static const char *const bad[] = {"x", "e", "i", "pi", "ans", "inf", "undef", "true", "false"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        if (!strcmp(name, bad[i]))
            return false;
    return true;
}

/* Find or create the list `name`. New lists take the first free named slot
 * (after RESID) and start empty; they are not added to the editor columns
 * here (the callers do that when they want the list shown). NULL if the name
 * is invalid or all SL_MAX - 7 named slots are taken. */
slist_t *sl_create(const char *name)
{
    slist_t *l = sl_find(name);
    if (l)
        return l;
    if (!sl_valid_name(name))
        return NULL;
    for (int i = SL_RESID + 1; i < SL_MAX; i++)
        if (!lists[i].used) {
            l = &lists[i];
            memset(l->name, 0, sizeof l->name);
            snprintf(l->name, sizeof l->name, "%s", name);
            l->len = 0;
            l->formula[0] = 0;
            l->used = true;
            return l;
        }
    return NULL;
}

/* ClrList / DelList: L1..L6 and RESID are only emptied; a named list is also
 * purged from the CAS, its slot freed and its editor column removed. Calls
 * sl_changed() so dependent formulas and the graph are refreshed. */
void sl_delete(slist_t *l)
{
    int slot = (int)(l - lists);
    l->len = 0;
    l->formula[0] = 0;
    if (slot > SL_RESID) {
        char cmd[32], out[16];
        snprintf(cmd, sizeof cmd, "purge(%s)", l->name);
        cas_eval_quiet(g.cas, cmd, out, sizeof out);
        l->used = false;
        for (int i = 0; i < sl_ncols; i++)
            if (sl_cols[i] == slot) {
                memmove(&sl_cols[i], &sl_cols[i + 1], sizeof(int) * (size_t)(sl_ncols - i - 1));
                sl_ncols--;
                break;
            }
    }
    sl_changed(l);
}

/* Parse a list of list names separated by commas and/or spaces ("L1,L2 DATA")
 * into slot numbers (at most `max` are stored, extra names are checked but
 * dropped). Names longer than SL_NAME characters are cut and then not found.
 * Returns the count, or -1 with a message in err if a name is unknown. */
int sl_parse(const char *s, int *slots, int max, char *err, size_t errn)
{
    int n = 0;
    char name[SL_NAME + 1];
    while (*s) {
        size_t k = strcspn(s, ", ");
        if (k) {
            snprintf(name, sizeof name, "%.*s", (int)(k < SL_NAME ? k : SL_NAME), s);
            slist_t *l = sl_find(name);
            if (!l) {
                snprintf(err, errn, "list '%s' not found", name);
                return -1;
            }
            if (n < max)
                slots[n++] = (int)(l - lists);
        }
        s += k;
        while (*s == ',' || *s == ' ')
            s++;
    }
    return n;
}

/* SetUpEditor with no arguments: columns L1..L6 followed by every named list
 * (RESID is not shown by default). */
void sl_editor_default(void)
{
    sl_ncols = 0;
    for (int i = 0; i < 6; i++)
        sl_cols[sl_ncols++] = i;
    for (int i = SL_RESID + 1; i < SL_MAX; i++)
        if (lists[i].used)
            sl_cols[sl_ncols++] = i;
}

/* ------------------------------------------------------------ CAS text */
/* "NAME:=[v1,v2,...]" into text; false if it does not fit. Values are written
 * by st_num_text() with enough digits to round-trip the double exactly
 * (exponents as (m*10^(e)), which the CAS parses); NaN cells (empty / non-
 * numeric elements) become `undef`. A full 999-element list needs about
 * 20 KB, well inside the 32 KB buffer. */
static bool define_text(const slist_t *l)
{
    size_t o = (size_t)snprintf(text, text_cap, "%s:=[", l->name);
    for (int i = 0; i < l->len; i++) {
        char b[48];
        st_num_text(b, sizeof b, l->v[i]);
        if (isnan(l->v[i]))
            snprintf(b, sizeof b, "undef");
        size_t k = strlen(b);
        if (o + k + 3 >= text_cap)
            return false;
        if (i)
            text[o++] = ',';
        memcpy(text + o, b, k);
        o += k;
    }
    text[o++] = ']';
    text[o] = 0;
    return true;
}

/* Define the list as a CAS variable with cas_eval_quiet (`ans` untouched).
 * A list too long for the text buffer is silently not (re)defined. */
static void cas_define(const slist_t *l)
{
    char out[8];
    if (define_text(l))
        cas_eval_quiet(g.cas, text, out, sizeof out);
}

/* call fn for every identifier in CAS text that names a list. Identifiers are
 * maximal runs of letters, digits and '_' starting with a letter, so "L12" is
 * not L1 and "sin" is never a list unless the user created one by that name.
 * fn runs once per occurrence (a list named twice is visited twice). */
static void each_list_ref(const char *s, void (*fn)(slist_t *))
{
    while (*s) {
        if (isalpha((unsigned char)*s)) {
            const char *b = s;
            while (isalnum((unsigned char)*s) || *s == '_')
                s++;
            char id[SL_NAME + 1];
            size_t n = (size_t)(s - b);
            if (n < sizeof id) {
                memcpy(id, b, n);
                id[n] = 0;
                slist_t *l = sl_find(id);
                if (l)
                    fn(l);
            }
        } else {
            s++;
        }
    }
}

/* parse "[1.0,2.5e-3,...]" (approximate CAS output) into v; returns count or -1.
 * At most maxn values are stored (the rest are parsed and dropped). An element
 * that is not a plain number (symbolic, `undef`, parenthesised, complex such
 * as 1+2*i) is skipped up to the next top-level ',' or ']' and stored as NaN.
 * -1 means the text is not a list (e.g. a scalar result) or is malformed. */
static int parse_list(const char *s, double *v, int maxn)
{
    while (*s == ' ')
        s++;
    if (*s++ != '[')
        return -1;
    int n = 0;
    while (*s == ' ')
        s++;
    if (*s == ']')
        return 0;
    for (;;) {
        char *end;
        double d = strtod(s, &end);
        const char *t = end;
        while (*t == ' ')
            t++;
        if (end == s || (*t != ',' && *t != ']')) { /* not a plain number (symbolic, complex): skip the element */
            int depth = 0;
            while (*s && !(depth == 0 && (*s == ',' || *s == ']'))) {
                if (*s == '(' || *s == '[') depth++;
                if (*s == ')' || *s == ']') depth--;
                s++;
            }
            d = DNAN;
            end = (char *)s;
        }
        if (n < maxn)
            v[n++] = d;
        s = end;
        while (*s == ' ')
            s++;
        if (*s == ']')
            return n;
        if (*s++ != ',')
            return -1;
    }
}

/* evaluate CAS text approximately (17 digits) into text; CAS_OK or error.
 * The user's settings (exact/approx, digits) are switched temporarily so
 * the result is a list of decimals that parse_list() can read with full
 * double precision, and restored afterwards. Uses cas_eval_quiet: `ans` is
 * not changed. Lists hold plain numbers, so values with an uncertainty
 * (printed v±s, the value rounded to the uncertainty) are evaluated again
 * as nominal(in): their unrounded values. */
static cas_status eval_approx(const char *in)
{
    cas_settings saved, s;
    cas_get_settings(g.cas, &saved);
    s = saved;
    s.exact = 0;
    s.digits = 17;
    cas_set_settings(g.cas, &s);
    cas_status st = cas_eval_quiet(g.cas, in, text, text_cap);
    if (st == CAS_OK && strstr(text, "\xc2\xb1")) {
        size_t n = strlen(in) + 10;
        if (2 * n <= text_cap) { /* nominal(in) at the end of the buffer, the result before it */
            char *w = text + text_cap - n;
            snprintf(w, n, "nominal(%s)", in);
            st = cas_eval_quiet(g.cas, w, text, text_cap - n);
        }
    }
    cas_set_settings(g.cas, &saved);
    return st;
}

/* After a home-screen assignment: copy the CAS variable of the same name back
 * into the list (e.g. "sort(L1)→L1"). Nothing happens if the variable is not
 * a list any more (purged, or a scalar was stored) or holds the same values.
 * The values are parsed into st_scratch first so a malformed result leaves
 * the list intact. An assigned list loses its column formula. */
static void readback(slist_t *l)
{
    char in[SL_NAME];
    snprintf(in, sizeof in, "%s", l->name);
    if (eval_approx(in) != CAS_OK)
        return;
    if (text[0] != '[') /* not a list (purged, or assigned a scalar): keep ours */
        return;
    double *tmp = st_scratch;
    int n = parse_list(text, tmp, st_scratch_cap < cap ? st_scratch_cap : cap);
    if (n < 0)
        return;
    bool same = n == l->len;
    for (int i = 0; same && i < n; i++)
        same = tmp[i] == l->v[i] || (isnan(tmp[i]) && isnan(l->v[i]));
    if (same)
        return;
    memcpy(l->v, tmp, sizeof(double) * (size_t)n);
    l->len = n;
    l->formula[0] = 0; /* assigned: no longer a formula column */
    sl_changed(l);
}

static void define_ref(slist_t *l) { cas_define(l); }

/* Before app_eval() evaluates `cas_text`: (re)define in the CAS every list it
 * names, so the CAS sees the current values. Only the named lists are sent,
 * which keeps a plain "2+3" free of any list traffic. */
void stat_pre_eval(const char *cas_text) { each_list_ref(cas_text, define_ref); }

/* After app_eval(): if the input could have changed variables (an assignment,
 * purge or restart), read every list it names back from the CAS. The test is
 * a cheap substring check; a false positive only costs a readback. */
void stat_post_eval(const char *cas_text)
{
    if (strstr(cas_text, ":=") || strstr(cas_text, "purge") || strstr(cas_text, "restart"))
        each_list_ref(cas_text, readback);
}

/* ------------------------------------------------------------ formulas */
static int depth; /* > 0 while sl_recompute() runs (re-entry guard) */

/* Evaluate an expression typed in the list editor (display form, e.g.
 * "L1*2" or "seq(k^2,k,1,10)") and store the resulting list in `l`.
 * Steps: convert the display text to CAS syntax through a temporary editor
 * (ed_cas), define every list it names in the CAS, evaluate approximately,
 * then parse the result; a scalar result becomes a one-element list.
 * Results longer than the list capacity are truncated. Does NOT call
 * sl_changed() and does not touch l->formula: the caller decides whether the
 * expression becomes a column formula. Returns 0, or -1 with err set (the
 * list is then unchanged). */
int sl_eval_into(slist_t *l, const char *disp_expr, char *err, size_t errn)
{
    editor_t ed;
    ed_set_text(&ed, disp_expr);
    char cas[ED_MAX * 4];
    ed_cas(&ed, cas, sizeof cas);
    each_list_ref(cas, define_ref);
    cas_status st = eval_approx(cas);
    if (st != CAS_OK) {
        snprintf(err, errn, "%s", cas_last_error(g.cas) ? cas_last_error(g.cas) : "error");
        return -1;
    }
    double *tmp = st_scratch;
    int n = parse_list(text, tmp, st_scratch_cap < cap ? st_scratch_cap : cap);
    if (n < 0) {
        char *end;
        double d = strtod(text, &end);
        if (end == text || *end) {
            snprintf(err, errn, "result is not a list");
            return -1;
        }
        tmp[0] = d, n = 1;
    }
    memcpy(l->v, tmp, sizeof(double) * (size_t)n);
    l->len = n;
    return 0;
}

/* Re-evaluate every formula column. There is no dependency graph: lists are
 * evaluated in slot order, twice, so a formula that uses a formula list in a
 * later slot also sees its new values (chains deeper than that, or cycles,
 * settle on the next change). A formula that fails empties its list (the
 * formula itself is kept). `depth` makes nested calls (sl_changed() from
 * inside an evaluation) return immediately. */
void sl_recompute(void)
{
    if (depth > 0)
        return;
    depth++;
    for (int pass = 0; pass < 2; pass++) /* formulas of formulas */
        for (int i = 0; i < SL_MAX; i++) {
            slist_t *l = &lists[i];
            if (!l->used || !l->formula[0])
                continue;
            char err[64];
            if (sl_eval_into(l, l->formula, err, sizeof err))
                l->len = 0;
        }
    depth--;
}

void sl_changed(slist_t *l)
{
    (void)l;
    g.graph_valid = false; /* stat plots */
    sl_recompute();
}

/* `v` may overlap the list's own storage (memmove). */
void sl_set(slist_t *l, const double *v, int n)
{
    if (n > cap)
        n = cap;
    memmove(l->v, v, sizeof(double) * (size_t)n);
    l->len = n;
    sl_changed(l);
}

/* ------------------------------------------------------------ persistence */
/* Blob "stat.lists", version 2 (version 1: the same, but the editor columns
 * are slot numbers, which no longer match once the named lists are packed
 * into the first free slots on loading):
 *   u8 count of used lists, then per list:
 *     str name, str formula, u16 len,
 *     len x { u8 tag; tag 1: u32 (int32 value) | tag 0: f64 }
 *   u8 editor column count, then that many u8 list numbers: the index of the
 *   column's list among the saved lists (0 = the first one saved).
 * Integral values below 2e9 in magnitude (most data: counts, years, scores)
 * take 5 bytes instead of 9. -0.0 is stored as f64 so its sign survives;
 * NaN and infinities fail the integer test and are stored as f64. */
static void lists_save(pw_t *w)
{
    int n = 0;
    for (int i = 0; i < SL_MAX; i++)
        n += lists[i].used;
    pw_u8(w, (uint32_t)n);
    for (int i = 0; i < SL_MAX; i++) {
        const slist_t *l = &lists[i];
        if (!l->used)
            continue;
        pw_str(w, l->name);
        pw_str(w, l->formula);
        pw_u16(w, (uint32_t)l->len);
        for (int k = 0; k < l->len; k++) {
            double v = l->v[k];
            if (v == floor(v) && fabs(v) < 2e9 && !(v == 0 && signbit(v))) {
                pw_u8(w, 1); /* small integer */
                pw_u32(w, (uint32_t)(int32_t)v);
            } else {
                pw_u8(w, 0);
                pw_f64(w, v);
            }
        }
    }
    pw_u8(w, (uint32_t)sl_ncols);
    for (int i = 0; i < sl_ncols; i++) {
        int k = 0; /* lists saved before slot sl_cols[i] */
        for (int s = 0; s < sl_cols[i]; s++)
            k += lists[s].used;
        pw_u8(w, (uint32_t)k);
    }
}

/* Inverse of lists_save. Lists are matched by name; named lists are
 * recreated in the first free slots in saved order, so they are compacted.
 * slot_of[j] remembers where the j-th saved list went, which maps the saved
 * editor columns (version 2) to the new slots; version 1 columns are slot
 * numbers, accepted only if that slot is used (correct unless the saved
 * slots had gaps). Values beyond the current capacity (a session saved with
 * the big arena, loaded with the fallback one) are read and dropped.
 * Formulas are restored but not re-evaluated: the saved values are their
 * last results. Any read error restores the defaults. */
static void lists_load(pr_t *r, unsigned version)
{
    defaults();
    int slot_of[256];
    unsigned n = pr_u8(r);
    for (unsigned j = 0; j < n && !r->err; j++) {
        char name[SL_NAME], formula[SL_FORMULA];
        pr_str(r, name, sizeof name);
        pr_str(r, formula, sizeof formula);
        unsigned len = pr_u16(r);
        slist_t *l = sl_find(name);
        if (!l)
            l = sl_create(name);
        for (unsigned k = 0; k < len && !r->err; k++) {
            double v = pr_u8(r) ? (double)(int32_t)pr_u32(r) : pr_f64(r);
            if (l && (int)k < cap)
                l->v[k] = v;
        }
        if (l) {
            l->len = (int)len < cap ? (int)len : cap;
            snprintf(l->formula, sizeof l->formula, "%s", formula);
        }
        slot_of[j] = l ? (int)(l - lists) : -1;
    }
    unsigned nc = pr_u8(r);
    if (!r->err && nc > 0 && nc <= SL_MAX) {
        int k = 0;
        for (unsigned i = 0; i < nc; i++) {
            unsigned s = pr_u8(r);
            if (version >= 2)
                s = s < n ? (unsigned)slot_of[s] : SL_MAX; /* -1 (list not restored) -> skipped */
            if (s < SL_MAX && lists[s].used)
                sl_cols[k++] = (int)s;
        }
        if (!r->err && k)
            sl_ncols = k;
    }
    if (r->err)
        defaults();
}

static void lists_reset(void) { defaults(); }

const persist_def_t persist_stat_lists = {"stat.lists", 2, lists_save, lists_load, lists_reset};

/* ------------------------------------------------------------ VARS */
/* VARS > Lists: L1..L6 and every named list always, RESID only when it holds
 * residuals. Selecting an entry inserts the list's name (a CAS variable
 * during home evaluations, see stat_pre_eval). vl_get repeats the same filter
 * to map the visible index to a slot. */
static int vl_count(void)
{
    int n = 0;
    for (int i = 0; i < SL_MAX; i++)
        n += lists[i].used && (i < 6 || lists[i].len > 0 || i > SL_RESID);
    return n;
}

static void vl_get(int idx, menu_item_t *out)
{
    static char label[40];
    for (int i = 0; i < SL_MAX; i++) {
        if (!(lists[i].used && (i < 6 || lists[i].len > 0 || i > SL_RESID)))
            continue;
        if (idx-- == 0) {
            snprintf(label, sizeof label, "%s  (%d)", lists[i].name, lists[i].len);
            *out = (menu_item_t){label, lists[i].name, NULL, i, "stat-lists"};
            return;
        }
    }
}

const vars_provider_t vars_stat_lists = {"Lists", vl_count, vl_get};

/* last results: values inserted as numbers */
static int vr_count(void) { return stat_last.n + (stat_last.eq[0] ? 1 : 0); }

/* VARS > Statistics item i: "label = value" (MODE format) inserting the
 * full-precision number; the extra last item RegEQ inserts the regression
 * equation's CAS text. Strings live in static buffers (valid until the next
 * call, as the dynamic-menu contract requires). */
static void vr_get(int i, menu_item_t *out)
{
    static char label[64], ins[48];
    if (stat_last.eq[0] && i == stat_last.n) {
        snprintf(label, sizeof label, "RegEQ");
        *out = (menu_item_t){label, stat_last.eq, NULL, i, "stat-results"};
        return;
    }
    char num[32];
    stat_fmt(stat_last.val[i], num, sizeof num);
    snprintf(label, sizeof label, "%s = %s", stat_last.lab[i], num);
    st_num_text(ins, sizeof ins, stat_last.val[i]);
    *out = (menu_item_t){label, ins, NULL, i, "stat-results"};
}

const vars_provider_t vars_stat_results = {"Statistics", vr_count, vr_get};

/* Console `state` keys of the statistics app (called from app_state_foreach):
 * the list editor position, the title of the last result and each stat
 * plot's settings as "on|off TYPE XLIST YLIST FREQ MARK COLOR". */
void stat_state(void (*kv)(const char *, const char *, void *), void *user)
{
    stat_edit_state(kv, user);
    kv("stat_last", stat_last.title ? stat_last.title : "", user);
    for (int i = 0; i < SP_N; i++) {
        const splot_t *p = &stat_plots[i];
        char k[12], v[48];
        snprintf(k, sizeof k, "statplot%d", i + 1);
        snprintf(v, sizeof v, "%s %d %s %s %s %d %d", p->on ? "on" : "off", p->type, p->xl, p->yl, p->fl, p->mark,
                 p->color);
        kv(k, v, user);
    }
}
