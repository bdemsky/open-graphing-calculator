/*
 * Spreadsheet model (see sheet.h for the cell model and reference syntax):
 * storage, references, recalculation, fill/copy, links to the statistics
 * lists, CSV and persistence. No drawing here (sheet_ui.c), so the unit test
 * (tests/unit/test_sheet.c) drives it directly.
 *
 * Memory: the cell pool (SH_BIG_CELLS cells, SH_SMALL_CELLS in the fallback
 * arena), an index array for sorting range members and the DFS stack, a
 * value buffer for the list links and the CAS input buffer are carved from
 * the arena by sheet_mem_init() (app_init). Cells are found by linear search
 * over the pool: with at most a thousand cells that is a few microseconds
 * and keeps the structure trivially consistent.
 *
 * Formula -> CAS. A cell's text is converted to CAS syntax with the token
 * editor (ed_set_text + ed_cas, as for every other entry line), then each
 * reference is replaced: A1 -> _sA1 (an empty cell -> 0), A1:B2 -> the list
 * [_sA1,_sB1,_sA2,_sB2] of the non-empty, non-label cells, row by row. A
 * reference to a cell with an error makes the formula fail with ERR:REF.
 * The whole input becomes "_sA1:=(...)", evaluated with cas_eval_quiet so
 * `ans` is not touched.
 *
 * Recalculation (dependency graph). Edges are implicit: a cell depends on the
 * cells inside the rectangles of its references (sheet_refs, re-parsed from
 * the text when needed: the text is the single source of truth). After an
 * edit, the changed cell and every cell that depends on it, transitively, are
 * marked SC_DIRTY (a fixpoint over the pool); then each dirty cell is
 * evaluated after its dirty dependencies by an iterative depth-first search
 * with an explicit stack (no recursion: chains can be ~1000 long). A
 * dependency that is already on the stack closes a cycle: every cell of the
 * cycle gets ERR:CYCLE, and cells that use them get ERR:REF. This is the
 * usual topological evaluation (Tarjan-style DFS with grey/black marks;
 * SC_STACK = grey, SC_DONE = black).
 *
 * Saved state: blob "sheet" (persist_sheet): the cell inputs, the column
 * links and the selection. Results are not saved; the variables _sXn are
 * rebuilt by sheet_ensure() when the sheet is used after a boot.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: size over speed */
#endif
#include "sheet.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fat.h"
#include "session.h"

#define SH_BIG_CELLS 1000
#define SH_SMALL_CELLS 48
#define SH_CASBUF_BIG (16u << 10)
#define SH_CASBUF_SMALL (2u << 10)
#define NSEED 32 /* deleted cells remembered for the next recalc */
#define MAXRECT 32

static scell_t *pool;
static int cap;
static uint16_t *idx;   /* cap entries: the DFS stack */
static uint16_t *ord;   /* cap entries: range members / linked cells, sorted */
static double *vbuf;    /* SH_ROWS values for the list links */
static char *casbuf;    /* CAS input of one cell */
static size_t casbuf_n;
static char links[SH_COLS][SL_NAME];
static bool need_all = true; /* the CAS variables must be rebuilt */
static struct { int16_t c, r; } seeds[NSEED];
static int nseeds;
static bool seed_overflow;

/* Carve the pool and buffers from mem (app_init); returns the bytes used. */
size_t sheet_mem_init(void *mem, bool big)
{
    uint8_t *m = mem;
    cap = big ? SH_BIG_CELLS : SH_SMALL_CELLS;
    casbuf_n = big ? SH_CASBUF_BIG : SH_CASBUF_SMALL;
    size_t used = 0;
    pool = (scell_t *)(m + used);
    used += sizeof(scell_t) * (size_t)cap;
    vbuf = (double *)(m + used);
    used += sizeof(double) * SH_ROWS;
    idx = (uint16_t *)(m + used);
    used += sizeof(uint16_t) * (size_t)cap;
    ord = (uint16_t *)(m + used);
    used += sizeof(uint16_t) * (size_t)cap;
    casbuf = (char *)(m + used);
    used += casbuf_n;
    used = (used + 7) & ~(size_t)7;
    memset(pool, 0, sizeof(scell_t) * (size_t)cap);
    memset(links, 0, sizeof links);
    need_all = true;
    nseeds = 0;
    seed_overflow = false;
    return used;
}

int sheet_cap(void) { return cap; }
scell_t *sheet_at(int i) { return i >= 0 && i < cap && (pool[i].flags & SC_USED) ? &pool[i] : NULL; }

scell_t *sheet_get(int col, int row)
{
    for (int i = 0; i < cap; i++)
        if ((pool[i].flags & SC_USED) && pool[i].col == col && pool[i].row == row)
            return &pool[i];
    return NULL;
}

int sheet_count(void)
{
    int n = 0;
    for (int i = 0; i < cap; i++)
        n += (pool[i].flags & SC_USED) != 0;
    return n;
}

/* ------------------------------------------------------------ references */
static bool identc(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* One reference at s: optional '$', a letter, optional '$', a row 1..999
 * (no leading zero), not followed by a name character or '('. Returns its
 * length (0: none) and the parts. */
static int ref_at(const char *s, int *col, int *row, bool *absc, bool *absr)
{
    int p = 0;
    *absc = *absr = false;
    if (s[p] == '$')
        *absc = true, p++;
    if (!isalpha((unsigned char)s[p]))
        return 0;
    *col = toupper((unsigned char)s[p]) - 'A';
    p++;
    if (s[p] == '$')
        *absr = true, p++;
    if (s[p] < '1' || s[p] > '9')
        return 0;
    int r = 0, nd = 0;
    while (isdigit((unsigned char)s[p]) && nd < 4)
        r = r * 10 + (s[p++] - '0'), nd++;
    if (nd > 3 || identc(s[p]) || s[p] == '(' || s[p] == '$')
        return 0;
    *row = r - 1;
    return p;
}

int sheet_parse_ref(const char *s, int *col, int *row)
{
    bool a, b;
    return ref_at(s, col, row, &a, &b);
}

void sheet_ref_name(int col, int row, char *out, size_t n) { snprintf(out, n, "%c%d", 'A' + col, row + 1); }

/* Can a reference start at s[i]? Not inside a name ("log2", "_m2", "x1y")
 * and not inside a string (the caller tracks quotes). */
static bool ref_start_ok(const char *s, size_t i) { return i == 0 || (!identc(s[i - 1]) && s[i - 1] != '$'); }

/* Walk the references of text: calls fn for each single reference and each
 * range (rect), skipping quoted strings. */
static void walk_refs(const char *text, void (*fn)(const srect_t *r, void *u), void *u)
{
    bool str = false;
    for (size_t i = 0; text[i]; i++) {
        if (text[i] == '"') {
            str = !str;
            continue;
        }
        int c, r, c2, r2;
        bool a, b;
        int len;
        if (str || !ref_start_ok(text, i) || !(len = ref_at(text + i, &c, &r, &a, &b)))
            continue;
        srect_t rc = {c, r, c, r};
        int len2 = 0;
        if (text[i + len] == ':' && (len2 = ref_at(text + i + len + 1, &c2, &r2, &a, &b))) {
            rc.c0 = c < c2 ? c : c2, rc.c1 = c < c2 ? c2 : c;
            rc.r0 = r < r2 ? r : r2, rc.r1 = r < r2 ? r2 : r;
            len += 1 + len2;
        }
        fn(&rc, u);
        i += (size_t)len - 1;
    }
}

typedef struct {
    srect_t *out;
    int n, max;
} refs_acc;
static void add_rect(const srect_t *r, void *u)
{
    refs_acc *a = u;
    if (a->n < a->max)
        a->out[a->n++] = *r;
}

int sheet_refs(const char *text, srect_t *out, int max)
{
    refs_acc a = {out, 0, max};
    walk_refs(text, add_rect, &a);
    return a.n;
}

/* Copy text to out with each relative reference part moved by (dc, dr);
 * absolute parts ($) stay. A reference moved off the sheet becomes "#REF",
 * which the CAS rejects, so the cell shows an error. */
void sheet_shift_refs(const char *text, int dc, int dr, char *out, size_t n)
{
    size_t o = 0;
    bool str = false;
    for (size_t i = 0; text[i] && o + 1 < n;) {
        int c, r, len;
        bool a, b;
        if (text[i] == '"')
            str = !str;
        if (!str && ref_start_ok(text, i) && (len = ref_at(text + i, &c, &r, &a, &b))) {
            int nc = a ? c : c + dc, nr = b ? r : r + dr;
            char t[16];
            if (nc < 0 || nc >= SH_COLS || nr < 0 || nr >= SH_ROWS)
                snprintf(t, sizeof t, "#REF");
            else
                snprintf(t, sizeof t, "%s%c%s%d", a ? "$" : "", (isupper((unsigned char)text[i + a]) ? 'A' : 'a') + nc,
                         b ? "$" : "", nr + 1);
            for (const char *p = t; *p && o + 1 < n; p++)
                out[o++] = *p;
            i += (size_t)len;
            continue;
        }
        out[o++] = text[i++];
    }
    out[o] = 0;
}

static bool in_rect(const srect_t *r, int c, int row) { return c >= r->c0 && c <= r->c1 && row >= r->r0 && row <= r->r1; }

/* ------------------------------------------------------------ evaluation */
/* "_sB12" */
static void var_name(int col, int row, char *out, size_t n) { snprintf(out, n, "_s%c%d", 'A' + col, row + 1); }

/* Append s to the CAS buffer at *o; false if it does not fit. */
static bool put(size_t *o, const char *s)
{
    size_t l = strlen(s);
    if (*o + l + 1 >= casbuf_n)
        return false;
    memcpy(casbuf + *o, s, l + 1);
    *o += l;
    return true;
}

/* Sort ord[0..n) by row, then column (insertion sort; n is small). */
static void sort_ord(int n)
{
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0; j--) {
            const scell_t *a = &pool[ord[j - 1]], *b = &pool[ord[j]];
            if (a->row < b->row || (a->row == b->row && a->col <= b->col))
                break;
            uint16_t t = ord[j];
            ord[j] = ord[j - 1];
            ord[j - 1] = t;
        }
}

/* Rewrite the CAS text `src` of cell `self` into casbuf as "_sXn:=(...)".
 * Returns 0, 1 if a referenced cell has an error, 2 if the text is too long. */
static int rewrite(const scell_t *self, const char *src)
{
    size_t o = 0;
    char vn[16];
    var_name(self->col, self->row, vn, sizeof vn);
    casbuf[0] = 0;
    if (!put(&o, vn) || !put(&o, ":=("))
        return 2;
    bool str = false;
    for (size_t i = 0; src[i];) {
        int c, r, c2, r2, len, len2;
        bool a, b;
        if (src[i] == '"')
            str = !str;
        if (str || !ref_start_ok(src, i) || !(len = ref_at(src + i, &c, &r, &a, &b))) {
            char ch[2] = {src[i++], 0};
            if (!put(&o, ch))
                return 2;
            continue;
        }
        if (src[i + len] == ':' && (len2 = ref_at(src + i + len + 1, &c2, &r2, &a, &b))) {
            srect_t rc = {c < c2 ? c : c2, r < r2 ? r : r2, c < c2 ? c2 : c, r < r2 ? r2 : r};
            int n = 0;
            for (int k = 0; k < cap; k++) {
                const scell_t *x = &pool[k];
                if ((x->flags & SC_USED) && in_rect(&rc, x->col, x->row)) {
                    if (x->flags & SC_ERR)
                        return 1;
                    if (!(x->flags & SC_LABEL))
                        ord[n++] = (uint16_t)k;
                }
            }
            sort_ord(n);
            if (!put(&o, "["))
                return 2;
            for (int k = 0; k < n; k++) {
                var_name(pool[ord[k]].col, pool[ord[k]].row, vn, sizeof vn);
                if ((k && !put(&o, ",")) || !put(&o, vn))
                    return 2;
            }
            if (!put(&o, "]"))
                return 2;
            i += (size_t)(len + 1 + len2);
            continue;
        }
        const scell_t *x = sheet_get(c, r);
        if (x && (x->flags & SC_ERR))
            return 1;
        var_name(c, r, vn, sizeof vn);
        if (!put(&o, x ? vn : "0"))
            return 2;
        i += (size_t)len;
    }
    return put(&o, ")") ? 0 : 2;
}

/* Is s entirely a number (strtod, no trailing text)? */
static bool whole_number(const char *s, double *v)
{
    char *end;
    *v = strtod(s, &end);
    return end != s && !*end;
}

/* Evaluate one cell into its CAS variable and res/val/flags. */
static void eval_cell(scell_t *c)
{
    char vn[16], out[512];
    c->flags &= (uint8_t)~(SC_ERR | SC_LABEL);
    c->val = NAN;
    var_name(c->col, c->row, vn, sizeof vn);
    const char *t = c->text;
    if (t[0] == '"') { /* label: shown without the quotes, a string in the CAS */
        c->flags |= SC_LABEL;
        size_t l = strlen(t + 1);
        if (l && t[l] == '"')
            l--;
        snprintf(c->res, sizeof c->res, "%.*s", (int)l, t + 1);
        snprintf(casbuf, casbuf_n, "%s:=\"%s\"", vn, c->res);
        cas_eval_quiet(g.cas, casbuf, out, sizeof out);
        return;
    }
    if (t[0] == '=')
        t++;
    editor_t conv; /* display text -> CAS text (on the stack: eval_cell does not recurse) */
    ed_set_text(&conv, t);
    ed_cas(&conv, out, sizeof out);
    int rc = rewrite(c, out);
    if (rc) {
        snprintf(c->res, sizeof c->res, rc == 1 ? "ERR:REF" : "ERR:TOO LONG");
        c->flags |= SC_ERR;
        return;
    }
    cas_status st = cas_eval_quiet(g.cas, casbuf, out, sizeof out);
    if (st != CAS_OK && st != CAS_ERR_OUTPUT_TRUNC) {
        const char *m = cas_last_error(g.cas);
        snprintf(c->res, sizeof c->res, "ERR:%s", m && m[0] ? m : "SYNTAX");
        c->flags |= SC_ERR;
        return;
    }
    snprintf(c->res, sizeof c->res, "%.*s", (int)sizeof c->res - 1, out);
    double v;
    if (whole_number(out, &v)) {
        c->val = v;
        return;
    }
    snprintf(casbuf, casbuf_n, "approx(%s(%s))", strstr(out, "\xc2\xb1") ? "nominal" : "", vn);
    if (cas_eval_quiet(g.cas, casbuf, out, sizeof out) == CAS_OK && whole_number(out, &v))
        c->val = v;
}

/* Does cell x reference cell (col,row)? */
static bool depends_on(const scell_t *x, int col, int row)
{
    srect_t r[MAXRECT];
    int n = sheet_refs(x->text, r, MAXRECT);
    for (int i = 0; i < n; i++)
        if (in_rect(&r[i], col, row))
            return true;
    return false;
}

/* Mark every cell that depends (transitively) on a dirty cell or a seed. */
static void mark_dependents(void)
{
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < cap; i++) {
            scell_t *x = &pool[i];
            if (!(x->flags & SC_USED) || (x->flags & SC_DIRTY))
                continue;
            bool dep = false;
            for (int s = 0; s < nseeds && !dep; s++)
                dep = depends_on(x, seeds[s].c, seeds[s].r);
            for (int j = 0; j < cap && !dep; j++)
                if ((pool[j].flags & (SC_USED | SC_DIRTY)) == (SC_USED | SC_DIRTY))
                    dep = depends_on(x, pool[j].col, pool[j].row);
            if (dep)
                x->flags |= SC_DIRTY, changed = true;
        }
    }
}

/* The first dirty, not yet evaluated cell that x depends on (-1: none).
 * *cyc is set if that cell is already on the stack. */
static int pending_dep(const scell_t *x, bool *cyc)
{
    srect_t r[MAXRECT];
    int n = sheet_refs(x->text, r, MAXRECT);
    *cyc = false;
    for (int k = 0; k < n; k++)
        for (int i = 0; i < cap; i++) {
            const scell_t *d = &pool[i];
            if ((d->flags & (SC_USED | SC_DIRTY)) != (SC_USED | SC_DIRTY) || (d->flags & SC_DONE) ||
                !in_rect(&r[k], d->col, d->row))
                continue;
            *cyc = (d->flags & SC_STACK) != 0;
            return i;
        }
    return -1;
}

/* Evaluate the dirty cells in dependency order (file header), then clear the
 * marks, update the list links and the sentinel. */
static void run(void)
{
    for (int s = 0; s < cap; s++) {
        if ((pool[s].flags & (SC_USED | SC_DIRTY)) != (SC_USED | SC_DIRTY) || (pool[s].flags & SC_DONE))
            continue;
        int sp = 0;
        idx[sp++] = (uint16_t)s;
        pool[s].flags |= SC_STACK;
        while (sp) {
            scell_t *t = &pool[idx[sp - 1]];
            bool cyc;
            int d = pending_dep(t, &cyc);
            if (d >= 0 && cyc) { /* the cycle is the stack from d to the top */
                int k = sp - 1;
                while (k > 0 && idx[k] != d)
                    k--;
                for (int j = k; j < sp; j++) {
                    scell_t *x = &pool[idx[j]];
                    snprintf(x->res, sizeof x->res, "ERR:CYCLE");
                    x->flags = (uint8_t)((x->flags & ~(SC_STACK | SC_LABEL)) | SC_ERR | SC_DONE);
                    x->val = NAN;
                }
                sp = k;
                continue;
            }
            if (d >= 0) {
                idx[sp++] = (uint16_t)d;
                pool[d].flags |= SC_STACK;
                continue;
            }
            eval_cell(t);
            t->flags = (uint8_t)((t->flags & ~SC_STACK) | SC_DONE);
            sp--;
        }
    }
    for (int i = 0; i < cap; i++)
        pool[i].flags &= (uint8_t)~(SC_DIRTY | SC_DONE | SC_STACK);
    nseeds = 0;
    seed_overflow = false;
    char out[16];
    cas_eval_quiet(g.cas, "_shok:=1", out, sizeof out);
    sheet_push_links();
}

void sheet_recalc_all(void)
{
    for (int i = 0; i < cap; i++)
        if (pool[i].flags & SC_USED)
            pool[i].flags |= SC_DIRTY;
    need_all = false;
    run();
}

void sheet_invalidate(void) { need_all = true; }

/* The cell variables exist only in the CAS (not saved, gone after MEM >
 * Clear CAS variables): rebuild them when the sentinel _shok is missing. */
void sheet_ensure(void)
{
    char out[32];
    if (!need_all && cas_eval_quiet(g.cas, "_shok", out, sizeof out) == CAS_OK && !strcmp(out, "1"))
        return;
    sheet_recalc_all();
}

/* Recalculate after edits: dirty cells and seeds plus their dependents. */
static void recalc_dirty(void)
{
    if (seed_overflow) {
        sheet_recalc_all();
        return;
    }
    mark_dependents();
    run();
}

/* Set a cell's text without recalculating (marks it dirty / seeds a
 * deletion). Returns 0 or -1 if the pool is full. */
static int put_text(int col, int row, const char *text)
{
    scell_t *c = sheet_get(col, row);
    if (!text[0]) {
        if (c) {
            char vn[16], cmd[32], out[32];
            var_name(col, row, vn, sizeof vn);
            snprintf(cmd, sizeof cmd, "purge(%s)", vn);
            cas_eval_quiet(g.cas, cmd, out, sizeof out);
            c->flags = 0;
        }
        if (nseeds < NSEED)
            seeds[nseeds].c = (int16_t)col, seeds[nseeds++].r = (int16_t)row;
        else
            seed_overflow = true;
        return 0;
    }
    if (!c) {
        for (int i = 0; i < cap && !c; i++)
            if (!(pool[i].flags & SC_USED))
                c = &pool[i];
        if (!c)
            return -1;
        memset(c, 0, sizeof *c);
        c->col = (uint8_t)col;
        c->row = (uint16_t)row;
        c->val = NAN;
    }
    snprintf(c->text, sizeof c->text, "%s", text);
    c->flags = SC_USED | SC_DIRTY;
    return 0;
}

int sheet_set(int col, int row, const char *text)
{
    if (col < 0 || col >= SH_COLS || row < 0 || row >= SH_ROWS)
        return -1;
    sheet_ensure();
    int r = put_text(col, row, text);
    recalc_dirty();
    return r;
}

void sheet_clear_all(void)
{
    char out[32];
    for (int i = 0; i < cap; i++)
        if (pool[i].flags & SC_USED) {
            char vn[16], cmd[32];
            var_name(pool[i].col, pool[i].row, vn, sizeof vn);
            snprintf(cmd, sizeof cmd, "purge(%s)", vn);
            if (g.cas)
                cas_eval_quiet(g.cas, cmd, out, sizeof out);
            pool[i].flags = 0;
        }
    nseeds = 0;
    seed_overflow = false;
}

int sheet_copy(int c0, int r0, int c1, int r1)
{
    const scell_t *s = sheet_get(c0, r0);
    char t[SH_TEXT];
    if (s)
        sheet_shift_refs(s->text, c1 - c0, r1 - r0, t, sizeof t);
    else
        t[0] = 0;
    return sheet_set(c1, r1, t);
}

int sheet_fill_down(int col, int r0, int r1)
{
    const scell_t *s = sheet_get(col, r0);
    if (!s || r1 <= r0 || r1 >= SH_ROWS)
        return -1;
    sheet_ensure();
    char src[SH_TEXT], t[SH_TEXT];
    snprintf(src, sizeof src, "%s", s->text);
    int rc = 0;
    for (int r = r0 + 1; r <= r1 && !rc; r++) {
        sheet_shift_refs(src, 0, r - r0, t, sizeof t);
        rc = put_text(col, r, t);
    }
    recalc_dirty();
    return rc;
}

/* ------------------------------------------------------------ display */
/* Copy at most `width` codepoints of s into out. */
static void cut(const char *s, int width, char *out, size_t n)
{
    size_t o = 0;
    for (int k = 0; *s && k < width;) {
        size_t l = 1;
        while ((s[l] & 0xC0) == 0x80)
            l++;
        if (o + l + 1 > n)
            break;
        memcpy(out + o, s, l);
        o += l, s += l, k++;
    }
    out[o] = 0;
}

void sheet_display(const scell_t *c, int width, char *out, size_t n)
{
    out[0] = 0;
    if (!c)
        return;
    char a[160], b[160];
    if (c->flags & (SC_ERR | SC_LABEL)) {
        cut(c->res, width, out, n);
        return;
    }
    numfmt_apply(c->res, a, sizeof a);
    pretty_from_cas(a, b, sizeof b);
    if (utf8_len(b) <= width) {
        snprintf(out, n, "%s", b);
        return;
    }
    if (isfinite(c->val) && !strchr(c->res, '_') && !strstr(c->res, "\xc2\xb1")) {
        fmt_num(c->val, width, out, n);
        return;
    }
    cut(b, width - 1, out, n);
    size_t l = strlen(out);
    if (l + 2 < n)
        out[l] = '~', out[l + 1] = 0;
}

/* ------------------------------------------------------------ list links */
int sheet_link(int col, const char *name)
{
    if (col < 0 || col >= SH_COLS || strlen(name) >= SL_NAME || (name[0] && !sl_valid_name(name) && !sl_find(name)))
        return -1;
    snprintf(links[col], SL_NAME, "%s", name);
    sheet_push_links();
    return 0;
}

const char *sheet_link_name(int col) { return col >= 0 && col < SH_COLS ? links[col] : ""; }

/* Every linked column -> its list (numeric cells in row order); lists are
 * only rewritten when the values changed (sl_set redraws plots, resyncs the
 * CAS). */
void sheet_push_links(void)
{
    for (int col = 0; col < SH_COLS; col++) {
        if (!links[col][0])
            continue;
        int n = 0;
        for (int i = 0; i < cap; i++) {
            const scell_t *x = &pool[i];
            if ((x->flags & SC_USED) && x->col == col && !(x->flags & (SC_LABEL | SC_ERR)) && isfinite(x->val))
                ord[n++] = (uint16_t)i;
        }
        sort_ord(n);
        if (n > sl_cap())
            n = sl_cap();
        for (int k = 0; k < n; k++)
            vbuf[k] = pool[ord[k]].val;
        slist_t *l = sl_create(links[col]);
        if (!l)
            continue;
        if (l->len == n && !memcmp(l->v, vbuf, sizeof(double) * (size_t)n))
            continue;
        l->formula[0] = 0;
        sl_set(l, vbuf, n);
    }
}

int sheet_from_list(int col, const char *name)
{
    slist_t *l = sl_find(name);
    if (!l || col < 0 || col >= SH_COLS)
        return -1;
    sheet_ensure();
    for (int i = 0; i < cap; i++)
        if ((pool[i].flags & SC_USED) && pool[i].col == col)
            put_text(col, pool[i].row, "");
    int rc = 0;
    for (int i = 0; i < l->len && i < SH_ROWS && !rc; i++) {
        char t[32];
        snprintf(t, sizeof t, "%.15g", l->v[i]);
        if (strtod(t, NULL) != l->v[i])
            snprintf(t, sizeof t, "%.17g", l->v[i]);
        rc = isnan(l->v[i]) ? 0 : put_text(col, i, t);
    }
    recalc_dirty();
    return rc;
}

/* ------------------------------------------------------------ CSV */
/* Write s as one CSV field: quoted (with "" for quotes) if it contains a
 * separator, quote or space at an end. */
static int put_field(fs_file_t *f, const char *s)
{
    bool q = strpbrk(s, ",;\"\t") != NULL;
    char b[2 * SH_TEXT + 4];
    size_t o = 0;
    if (q)
        b[o++] = '"';
    for (; *s && o + 3 < sizeof b; s++) {
        if (*s == '"')
            b[o++] = '"';
        b[o++] = *s;
    }
    if (q)
        b[o++] = '"';
    return fs_write(f, b, o) == (int)o ? 0 : -1;
}

int sheet_csv_export(const char *path, bool formulas, char *msg, size_t msgn)
{
    sheet_ensure();
    int maxc = -1, maxr = -1;
    for (int i = 0; i < cap; i++)
        if (pool[i].flags & SC_USED) {
            if (pool[i].col > maxc)
                maxc = pool[i].col;
            if (pool[i].row > maxr)
                maxr = pool[i].row;
        }
    fs_file_t f;
    int r = fs_open(&f, path, FS_WRITE);
    if (r) {
        snprintf(msg, msgn, "%s: %s.", path, fs_strerror(r));
        fs_unmount();
        return r;
    }
    int bad = 0;
    for (int row = 0; row <= maxr && !bad; row++) {
        for (int col = 0; col <= maxc && !bad; col++) {
            const scell_t *c = sheet_get(col, row);
            char v[SH_TEXT + 8];
            v[0] = 0;
            double num;
            if (c && formulas) { /* an expression gets '=' so that import does not take it for a label */
                bool plain = c->text[0] == '=' || c->text[0] == '"' || whole_number(c->text, &num);
                snprintf(v, sizeof v, "%s%s", plain ? "" : "=", c->text);
            } else if (c && (c->flags & (SC_LABEL | SC_ERR))) {
                snprintf(v, sizeof v, "%s", c->res);
            } else if (c && isfinite(c->val) && !strchr(c->res, '_') && !strstr(c->res, "\xc2\xb1")) {
                snprintf(v, sizeof v, "%.15g", c->val);
                if (strtod(v, NULL) != c->val)
                    snprintf(v, sizeof v, "%.17g", c->val);
            } else if (c) {
                snprintf(v, sizeof v, "%s", c->res);
            }
            bad = (col && fs_write(&f, ",", 1) != 1) || put_field(&f, v);
        }
        bad = bad || fs_write(&f, "\r\n", 2) != 2;
    }
    if (bad) {
        fs_abort(&f);
        snprintf(msg, msgn, "Write failed (card full or removed?).");
        fs_unmount();
        return FS_EIO;
    }
    r = fs_close(&f);
    if (r)
        snprintf(msg, msgn, "%s: %s.", path, fs_strerror(r));
    else
        snprintf(msg, msgn, "%d rows x %d columns -> %s.", maxr + 1, maxc + 1, path);
    fs_unmount();
    return r;
}

/* Split one CSV line in place into at most max fields (RFC 4180 quotes;
 * spaces around unquoted fields trimmed). Returns the count. */
static int split_csv(char *s, char sep, char **cells, int max)
{
    int n = 0;
    for (;;) {
        while (*s == ' ')
            s++;
        char *start = s, *w = s;
        if (*s == '"') {
            start = w = ++s;
            while (*s) {
                if (*s == '"') {
                    if (s[1] != '"') {
                        s++;
                        break;
                    }
                    s++;
                }
                *w++ = *s++;
            }
            while (*s && *s != sep)
                s++;
        } else {
            while (*s && *s != sep)
                s++;
            w = s;
            while (w > start && w[-1] == ' ')
                w--;
        }
        char c = *s;
        *w = 0;
        if (n < max)
            cells[n++] = start;
        if (!c)
            return n;
        s++;
    }
}

/* Read path into the sheet from A1 (the sheet is cleared first). A field
 * that starts with '=' or '"' is kept as typed (formula / label, as written
 * by the formula export), a number is a number, other text becomes a label.
 * The separator is TAB, ';' or ',' (the most frequent in the first line). */
int sheet_csv_import(const char *path, char *msg, size_t msgn)
{
    fs_file_t f;
    int r = fs_open(&f, path, FS_READ);
    if (r) {
        snprintf(msg, msgn, "%s: %s.", path, fs_strerror(r));
        fs_unmount();
        return r;
    }
    sheet_ensure();
    sheet_clear_all();
    char line[1024], blk[512], sep = 0;
    int bl = 0, bp = 0, row = 0, ncells = 0, err = 0;
    bool eof = false, full = false;
    while (!eof && row < SH_ROWS) {
        size_t o = 0;
        bool any = false;
        for (;;) { /* one line */
            if (bp >= bl) {
                bl = fs_read(&f, blk, sizeof blk);
                bp = 0;
                if (bl <= 0) {
                    if (bl < 0)
                        err = bl;
                    eof = true;
                    bl = 0;
                    break;
                }
            }
            char ch = blk[bp++];
            any = true;
            if (ch == '\n')
                break;
            if (ch != '\r' && o + 1 < sizeof line)
                line[o++] = ch;
        }
        line[o] = 0;
        if (!any)
            break;
        if (!sep) {
            int nc = 0, ns = 0, nt = 0;
            for (char *p = line; *p; p++)
                nc += *p == ',', ns += *p == ';', nt += *p == '\t';
            sep = nt ? '\t' : ns > nc ? ';' : ',';
        }
        char *cells[SH_COLS];
        int n = split_csv(line, sep, cells, SH_COLS);
        for (int col = 0; col < n; col++) {
            char *s = cells[col], t[SH_TEXT];
            double v;
            if (!s[0])
                continue;
            if (s[0] == '=' || s[0] == '"' || whole_number(s, &v))
                snprintf(t, sizeof t, "%s", s);
            else
                snprintf(t, sizeof t, "\"%.*s\"", SH_TEXT - 3, s);
            if (put_text(col, row, t))
                full = true;
            else
                ncells++;
        }
        row++;
    }
    fs_close(&f);
    fs_unmount();
    sheet_recalc_all();
    if (err)
        snprintf(msg, msgn, "%s: %s after %d rows.", path, fs_strerror(err), row);
    else
        snprintf(msg, msgn, "%d cells in %d rows%s.", ncells, row, full ? " (sheet full)" : "");
    return err ? FS_EIO : 0;
}

/* ------------------------------------------------------------ persistence */
/* Blob "sheet", version 1:
 *   u16 cells; per cell: u8 col, u16 row, str text
 *   26 x str link (list name or "")
 *   u8 selected column, u16 selected row */
static void sh_save(pw_t *w)
{
    pw_u16(w, (uint32_t)sheet_count());
    for (int i = 0; i < cap; i++)
        if (pool[i].flags & SC_USED) {
            pw_u8(w, pool[i].col);
            pw_u16(w, pool[i].row);
            pw_str(w, pool[i].text);
        }
    for (int c = 0; c < SH_COLS; c++)
        pw_str(w, links[c]);
    pw_u8(w, (uint32_t)sheet_sel_col());
    pw_u16(w, (uint32_t)sheet_sel_row());
}

static void sh_reset(void)
{
    for (int i = 0; i < cap; i++)
        pool[i].flags = 0;
    memset(links, 0, sizeof links);
    need_all = true;
    sheet_select(0, 0);
}

/* Load the cells (the pool is filled in stored order; cells beyond the
 * capacity or outside the sheet are skipped) and links; the values are
 * recomputed on first use (sheet_ensure), after the CAS blob is loaded. */
static void sh_load(pr_t *r, unsigned version)
{
    sh_reset();
    unsigned n = pr_u16(r);
    int k = 0;
    for (unsigned i = 0; i < n && !r->err; i++) {
        unsigned col = pr_u8(r), row = pr_u16(r);
        char t[SH_TEXT];
        pr_str(r, t, sizeof t);
        if (r->err || col >= SH_COLS || row >= SH_ROWS || !t[0] || k >= cap)
            continue;
        scell_t *c = &pool[k++];
        memset(c, 0, sizeof *c);
        c->col = (uint8_t)col, c->row = (uint16_t)row, c->val = NAN, c->flags = SC_USED;
        snprintf(c->text, sizeof c->text, "%s", t);
    }
    for (int c = 0; c < SH_COLS; c++)
        pr_str(r, links[c], SL_NAME);
    unsigned sc = pr_u8(r), sr = pr_u16(r);
    if (r->err) { /* truncated: keep what was read, drop half-read links */
        memset(links, 0, sizeof links);
        return;
    }
    sheet_select((int)sc, (int)sr);
}

const persist_def_t persist_sheet = {"sheet", 1, sh_save, sh_load, sh_reset};
