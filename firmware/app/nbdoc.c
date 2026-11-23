/* The notebook document (app/nbdoc.h): memory, the string arena, cells, and
 * the table the Python module uses (nbdoc_api). The home screen (app/home.c)
 * and the graph cells (app/gcell.c) work on it directly. */
#include "nbdoc.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "editor.h"
#include "hal.h"
#include "py_core.h"

nb_t *nbd;
static uint8_t *arena;  /* NB_ARENA bytes (SDRAM) */
static size_t arena_cap, top;
static uint8_t *stash_mem; /* a copy of the document and its arena (nbd_stash) */
static size_t stash_top;
static bool stashed;   /* (no arena: a document without strings, so nothing crashes) */

typedef struct {
    uint32_t size, used; /* payload bytes (a multiple of 8) */
} bh_t;


size_t nbdoc_mem_init(void *mem, bool big)
{
    uint8_t *a = mem;
    size_t used = 0;
    nbd = (nb_t *)(void *)a;
    used = (sizeof(nb_t) + 7) & ~(size_t)7;
    arena = a + used;
    arena_cap = big ? NB_ARENA : NB_ARENA_SMALL;
    used += arena_cap;
    memset(nbd, 0, sizeof *nbd);
    top = 0;
    if (!big) { /* (the SDRAM failed: no copy for the stash, no graph cell images) */
        stash_mem = NULL;
        gcell_mem_init(NULL, false);
        nbhtml_mem_init(NULL, false);
        return used;
    }
    stash_mem = a + used;
    used += ((sizeof(nb_t) + 7) & ~(size_t)7) + NB_ARENA;
    used += gcell_mem_init(a + used, big);
    used += (nbhtml_mem_init(a + used, big) + 7) & ~(size_t)7;
    memset(nbd, 0, sizeof *nbd);
    top = 0;
    return used;
}

static bh_t *next_blk(bh_t *b) { return (bh_t *)(void *)((uint8_t *)b + sizeof *b + b->size); }

/* first fit over the blocks, merging free neighbours; the free tail is given back */
void *nbd_alloc(size_t n)
{
    if (!arena)
        return NULL;
    n = (n + 7) & ~(size_t)7;
    for (size_t o = 0; o < top;) {
        bh_t *b = (bh_t *)(void *)(arena + o);
        if (!b->used) {
            bh_t *nx;
            while ((uint8_t *)(nx = next_blk(b)) < arena + top && !nx->used)
                b->size += (uint32_t)sizeof *nx + nx->size;
            if ((uint8_t *)next_blk(b) == arena + top) {
                top = o;
                break;
            }
            if (b->size >= n) {
                if (b->size >= n + 2 * sizeof *b) {
                    bh_t *rest = (bh_t *)(void *)((uint8_t *)b + sizeof *b + n);
                    rest->size = b->size - (uint32_t)(n + sizeof *b), rest->used = 0;
                    b->size = (uint32_t)n;
                }
                b->used = 1;
                return b + 1;
            }
        }
        o += sizeof *b + b->size;
    }
    if (top + sizeof(bh_t) + n > arena_cap)
        return NULL;
    bh_t *b = (bh_t *)(void *)(arena + top);
    b->size = (uint32_t)n, b->used = 1;
    top += sizeof *b + n;
    return b + 1;
}
void nbd_free(void *p)
{
    if (p)
        ((bh_t *)p - 1)->used = 0;
}
size_t nbd_used(void) { return top; }

void nbd_reset(void)
{
    top = 0;
    nbd->ncells = 0;
    nbd->counter = 0;
    nbd->note[0] = 0;
    memset(nbd->cell, 0, sizeof nbd->cell);
    nbd->gen++;
}

void nbd_scratch(void)
{
    nbd_reset();
    nbd->name[0] = 0;
    nbd->where = 0;
    nbd->open = nbd->dirty = false;
}

/* The arena's strings are at the same addresses after a copy back, so a plain copy of the document
 * and the used part of the arena is a complete snapshot. */
void nbd_stash(void)
{
    if (!stash_mem)
        return;
    size_t d = (sizeof(nb_t) + 7) & ~(size_t)7;
    memcpy(stash_mem, nbd, sizeof *nbd);
    memcpy(stash_mem + d, arena, top);
    stash_top = top;
    stashed = true;
}
void nbd_unstash(void)
{
    if (!stash_mem || !stashed)
        return;
    size_t d = (sizeof(nb_t) + 7) & ~(size_t)7;
    memcpy(nbd, stash_mem, sizeof *nbd);
    memcpy(arena, stash_mem + d, stash_top);
    top = stash_top;
    stashed = false;
    nbd->gen++;
    for (int i = 0; i < nbd->ncells; i++)
        nbd->cell[i].hh[0] = nbd->cell[i].hh[1] = -1;
}

char *nbd_strdup(const char *s, size_t n)
{
    char *d = nbd_alloc(n + 1);
    if (d) {
        memcpy(d, s, n);
        d[n] = 0;
    }
    return d;
}
void nbd_set(nb_cell_t *c, int f, const char *s)
{
    char *old = c->f[f];
    c->f[f] = s && *s ? nbd_strdup(s, strlen(s)) : NULL;
    nbd_free(old);
    c->h = c->hh[0] = c->hh[1] = -1;
}
const char *nbd_get(const nb_cell_t *c, int f) { return c->f[f] ? c->f[f] : ""; }

static void new_id(char *id)
{
    static uint32_t seed;
    for (;;) {
        seed = seed * 1664525u + 1013904223u + hal_millis();
        static const char hex[] = "0123456789abcdef";
        for (int i = 0; i < 8; i++)
            id[i] = hex[(seed >> (4 * i)) & 15];
        id[8] = 0;
        bool dup = false;
        for (int i = 0; i < nbd->ncells && !dup; i++)
            dup = nbd->cell[i].id != id && !strcmp(nbd->cell[i].id, id);
        if (!dup)
            return;
    }
}

int nbd_find(const char *id)
{
    for (int i = 0; i < nbd->ncells; i++)
        if (!strcmp(nbd->cell[i].id, id))
            return i;
    return -1;
}

nb_cell_t *nbd_insert(int at, int kind)
{
    if (nbd->ncells >= NB_MAX_CELLS)
        return NULL;
    if (at < 0 || at > nbd->ncells)
        at = nbd->ncells;
    memmove(&nbd->cell[at + 1], &nbd->cell[at], (size_t)(nbd->ncells - at) * sizeof nbd->cell[0]);
    nbd->ncells++;
    nb_cell_t *c = &nbd->cell[at];
    memset(c, 0, sizeof *c);
    c->kind = (uint8_t)kind;
    c->h = c->hh[0] = c->hh[1] = -1;
    c->xmin = -10, c->xmax = 10;
    new_id(c->id);
    return c;
}

void nbd_delete(int i)
{
    if (i < 0 || i >= nbd->ncells)
        return;
    for (int f = 0; f < NBF_N; f++)
        nbd_set(&nbd->cell[i], f, NULL);
    memmove(&nbd->cell[i], &nbd->cell[i + 1], (size_t)(nbd->ncells - i - 1) * sizeof nbd->cell[0]);
    nbd->ncells--;
}

void nbd_clear_out(nb_cell_t *c)
{
    for (int f = NBF_OUT; f < NBF_NOUT; f++)
        nbd_set(c, f, NULL);
    c->flags &= (uint8_t) ~(NBC_RAN | NBC_IMAGE);
    c->count = 0;
}

/* A change from the core (the home screen): the module hears of it when a named notebook is open
 * (its idle save, the browser page's events; the module is bound for that) */
void nbd_changed(void)
{
    nbd->gen++;
    if (nbd->open)
        nbd->dirty = true;
    py_nb_call("touched", NULL); /* (the module tells the browser page; named or not) */
}

bool nbd_special(void)
{
    for (int i = 0; i < nbd->ncells; i++)
        if (nbd->cell[i].kind != NBK_MATH)
            return true;
    return false;
}

static void api_reset(void) { nbd_reset(); }
static void api_touched(void)
{
    nbd->gen++;
    for (int i = 0; i < nbd->ncells; i++)
        nbd->cell[i].hh[0] = nbd->cell[i].hh[1] = -1;
}
static nb_t *api_doc(void) { return nbd; }

/* ------------------------------------------------------------ Ans and Out[n] */
static bool has_value(const nb_cell_t *c)
{
    return (c->kind == NBK_MATH || c->kind == NBK_PY) && c->count > 0 && !c->f[NBF_ENAME] && c->f[NBF_VAL];
}

/* (several cells of that count, after a restart of the numbering: the last one in the notebook) */
int nbd_result(int count)
{
    int best = -1;
    for (int i = 0; i < nbd->ncells; i++) {
        const nb_cell_t *c = &nbd->cell[i];
        if (has_value(c) && (count > 0 ? c->count == count : best < 0 || c->count >= nbd->cell[best].count))
            best = i;
    }
    return best;
}
const char *nbd_value(int i) { return i >= 0 && i < nbd->ncells ? nbd_get(&nbd->cell[i], NBF_VAL) : ""; }

static struct {
    char id[24];
    int count;
    uint32_t serial;
} ans_of; /* the cell whose result the CAS's ans holds (count 0: none) */
void nbd_ans_mark(const nb_cell_t *c)
{
    snprintf(ans_of.id, sizeof ans_of.id, "%s", c->id);
    ans_of.count = c->count;
    ans_of.serial = cas_ans_serial(app_cas());
}
static bool ans_holds(const nb_cell_t *c)
{
    return ans_of.count && c->kind == NBK_MATH && c->count == ans_of.count && !strcmp(c->id, ans_of.id) &&
           cas_ans_serial(app_cas()) == ans_of.serial;
}

bool nbd_to_cas(const char *disp, char *out, size_t n)
{
    static editor_t tmp;
    bool ascii = true;
    for (const char *p = disp; *p && ascii; p++)
        ascii = !((unsigned char)*p & 0x80);
    if (ascii) { /* (a result as the CAS printed it) */
        snprintf(out, n, "%s", disp);
        return strlen(disp) < n;
    }
    void (*hook)(editor_t *) = ed_change_hook; /* (a scratch line: not an undo step) */
    ed_change_hook = NULL;
    ed_set_text(&tmp, disp);
    ed_cas(&tmp, out, n);
    ed_change_hook = hook;
    return tmp.len < ED_MAX;
}

bool nbd_value_cas(int i, char *out, size_t n, char *msg, size_t mn)
{
    if (i < 0 || i >= nbd->ncells || !has_value(&nbd->cell[i])) {
        snprintf(msg, mn, "no such result");
        return false;
    }
    if (ans_holds(&nbd->cell[i])) {
        snprintf(out, n, "(ans)");
        return true;
    }
    if (n < 4 || !nbd_to_cas(nbd_value(i), out + 1, n - 2) || strlen(out + 1) + 3 > n) {
        snprintf(msg, mn, "the result is too long");
        return false;
    }
    out[0] = '(';
    strcat(out, ")");
    return true;
}

static bool idch(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

bool nbd_refs(const char *in, char *out, size_t n, char *msg, size_t mn)
{
    static char v[2048];
    char why[40];
    size_t o = 0;
    bool str = false;
    if (!n)
        return false;
    out[0] = 0;
    for (const char *p = in; *p;) {
        bool start = !str && (p == in || !idch(p[-1]));
        if (start && (!strncmp(p, "Out", 3) || !strncmp(p, "out", 3)) && !idch(p[3])) {
            const char *q = p + 3;
            while (*q == ' ' || *q == '*') /* (Out*[7]: the editor's implicit product) */
                q++;
            if (*q == '[') {
                int k = 0, d = 0;
                for (q++; *q == ' '; q++)
                    ;
                for (; *q >= '0' && *q <= '9' && d < 6; q++, d++)
                    k = k * 10 + (*q - '0');
                while (*q == ' ')
                    q++;
                if (!d || *q != ']') {
                    snprintf(msg, mn, "Out[n]: n is the number [n] of a cell");
                    return false;
                }
                if (!nbd_value_cas(nbd_result(k), v, sizeof v, why, sizeof why)) {
                    snprintf(msg, mn, "Out[%d]: %s", k, why);
                    return false;
                }
                p = q + 1;
                goto put;
            }
        }
        if (start && (!strncmp(p, "ans", 3) || !strncmp(p, "Ans", 3)) && !idch(p[3])) {
            int i = nbd_result(0);
            p += 3;
            if (i < 0) {
                snprintf(v, sizeof v, "ans"); /* (no result in the notebook yet: the CAS's own) */
            } else if (!nbd_value_cas(i, v, sizeof v, why, sizeof why)) {
                snprintf(msg, mn, "Ans: %s", why);
                return false;
            }
            goto put;
        }
        if (*p == '"')
            str = !str;
        if (o + 1 >= n)
            goto full;
        out[o++] = *p++;
        out[o] = 0;
        continue;
    put:
        if (o + strlen(v) >= n)
            goto full;
        strcpy(out + o, v);
        o += strlen(v);
    }
    return true;
full:
    snprintf(msg, mn, "the input is too long");
    return false;
}
const nbdoc_api_t nbdoc_api = {api_doc, nbd_alloc, nbd_free, nbd_used, api_reset, gcell_draw_api,
                               gcell_image, gcell_height, api_touched, nbd_stash, nbd_unstash,
                               gcell_set_spec};
