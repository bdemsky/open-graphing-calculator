/* Host test for the notebook document (app/nbdoc.h): the string arena (first fit, merging, the
 * free tail given back), cells (insert, delete, ids unique), outputs cleared, the stash (a snapshot
 * of the document and its strings put back as it was), and the home screen's session blob "home"
 * (version 2: the notebook; version 1: the old history, read as math cells), including a notebook
 * larger than the blob's budget (the newest cells are kept).
 * Built and run by `make test` (tests/unit/test_*.c, see Makefile). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nbdoc.h"
#include "session.h"

extern const persist_def_t persist_home;

static int fails, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if (!(c)) {                                                               \
            fails++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

static void test_arena(void)
{
    nbd_scratch();
    CHECK(nbd_used() == 0);
    char *a = nbd_strdup("alpha", 5), *b = nbd_strdup("beta", 4), *c = nbd_strdup("gamma", 5);
    CHECK(a && b && c && !strcmp(a, "alpha") && !strcmp(c, "gamma"));
    size_t u = nbd_used();
    nbd_free(b);
    char *d = nbd_strdup("bet", 3); /* fits where beta was */
    CHECK(d == b);
    CHECK(nbd_used() == u);
    nbd_free(c);
    nbd_free(d);
    char *e = nbd_strdup("x", 1); /* the free tail is given back: first fit after a */
    CHECK(e && e > a);
    nbd_free(a), nbd_free(e);
    void *big = nbd_alloc(NB_ARENA); /* more than there is */
    CHECK(big == NULL);
    nbd_reset();
    CHECK(nbd_used() == 0);
}

static void test_cells(void)
{
    nbd_scratch();
    for (int i = 0; i < NB_MAX_CELLS; i++)
        CHECK(nbd_insert(nbd->ncells, i % 2 ? NBK_TEXT : NBK_MATH) != NULL);
    CHECK(nbd_insert(0, NBK_MATH) == NULL); /* full */
    int dup = 0;
    for (int i = 0; i < nbd->ncells; i++)
        for (int j = i + 1; j < nbd->ncells; j++)
            dup += !strcmp(nbd->cell[i].id, nbd->cell[j].id);
    CHECK(dup == 0);
    CHECK(strlen(nbd->cell[5].id) == 8);
    char id[24];
    snprintf(id, sizeof id, "%s", nbd->cell[7].id);
    nbd_delete(3);
    CHECK(nbd->ncells == NB_MAX_CELLS - 1);
    CHECK(nbd_find(id) == 6);
    nb_cell_t *c = &nbd->cell[0];
    nbd_set(c, NBF_SRC, "1+1");
    nbd_set(c, NBF_VAL, "2");
    nbd_set(c, NBF_GRAPH, "w -1 1 -1 1 1 1");
    c->count = 3;
    nbd_clear_out(c);
    CHECK(!strcmp(nbd_get(c, NBF_SRC), "1+1"));
    CHECK(!strcmp(nbd_get(c, NBF_VAL), ""));
    CHECK(c->f[NBF_GRAPH] != NULL); /* (the spec is not an output) */
    CHECK(c->count == 0);
    CHECK(!nbd_special() || nbd->ncells > 1);
    nbd_scratch();
    CHECK(nbd->ncells == 0 && !nbd_special());
}

static void test_stash(void)
{
    nbd_scratch();
    nb_cell_t *c = nbd_insert(0, NBK_MATH);
    nbd_set(c, NBF_SRC, "kept");
    snprintf(nbd->name, sizeof nbd->name, "A");
    nbd_stash();
    nbd_reset();
    snprintf(nbd->name, sizeof nbd->name, "B");
    for (int i = 0; i < 20; i++)
        nbd_set(nbd_insert(nbd->ncells, NBK_PY), NBF_SRC, "print('another notebook')");
    nbd_unstash();
    CHECK(nbd->ncells == 1);
    CHECK(!strcmp(nbd->name, "A"));
    CHECK(!strcmp(nbd_get(&nbd->cell[0], NBF_SRC), "kept"));
    nbd_set(&nbd->cell[0], NBF_VAL, "still works"); /* the arena is consistent */
    CHECK(!strcmp(nbd_get(&nbd->cell[0], NBF_VAL), "still works"));
}

static uint8_t buf[256 << 10];

static size_t save_home(void)
{
    pw_t w = {buf, sizeof buf, 0, false};
    persist_home.save(&w);
    CHECK(!w.overflow);
    return w.len;
}
static void load_home(size_t n, unsigned version)
{
    pr_t r = {buf, n, 0, false};
    persist_home.load(&r, version);
}

static void test_session(void)
{
    nbd_scratch();
    nb_cell_t *c = nbd_insert(0, NBK_MATH);
    nbd_set(c, NBF_SRC, "√(2)");
    nbd_set(c, NBF_VAL, "1.41421356237");
    c = nbd_insert(1, NBK_GRAPH);
    nbd_set(c, NBF_GRAPH, "w -5 5 -2 2 1 1\no 0 1 90\nf 1 0 0 sin(x)");
    nbd_set(c, NBF_SRC, "sin(x), -5, 5");
    c = nbd_insert(2, NBK_TEXT);
    nbd_set(c, NBF_SRC, "# heading\nline");
    c = nbd_insert(3, NBK_MATH);
    nbd_set(c, NBF_SRC, "1/0");
    nbd_set(c, NBF_ENAME, "CASError");
    nbd_set(c, NBF_EVALUE, "ERR:DIV0 x");
    char ids[4][24];
    for (int i = 0; i < 4; i++)
        snprintf(ids[i], 24, "%s", nbd->cell[i].id);
    snprintf(nbd->name, sizeof nbd->name, "Lab");
    nbd->open = true;
    size_t n = save_home();
    nbd->open = false; /* (no module to save it at the reset) */
    nbd_scratch();
    load_home(n, persist_home.version);
    CHECK(nbd->ncells == 4);
    CHECK(nbd->open && !strcmp(nbd->name, "Lab"));
    for (int i = 0; i < 4 && nbd->ncells == 4; i++)
        CHECK(!strcmp(nbd->cell[i].id, ids[i]));
    CHECK(nbd->cell[1].kind == NBK_GRAPH && !strcmp(nbd_get(&nbd->cell[1], NBF_GRAPH),
                                                    "w -5 5 -2 2 1 1\no 0 1 90\nf 1 0 0 sin(x)"));
    CHECK(!strcmp(nbd_get(&nbd->cell[2], NBF_SRC), "# heading\nline"));
    CHECK(!strcmp(nbd_get(&nbd->cell[3], NBF_EVALUE), "ERR:DIV0 x"));
    CHECK(!strcmp(nbd_get(&nbd->cell[0], NBF_SRC), "√(2)"));
    nbd->open = false;

    /* more than the blob's budget: the newest cells are kept, in order */
    nbd_scratch();
    static char big[1800];
    memset(big, 'a', sizeof big - 1);
    for (int i = 0; i < 60; i++) {
        c = nbd_insert(nbd->ncells, NBK_MATH);
        big[0] = (char)('A' + i % 26);
        nbd_set(c, NBF_SRC, big);
        nbd_set(c, NBF_VAL, big);
    }
    n = save_home();
    CHECK(n < (64u << 10));
    load_home(n, persist_home.version);
    CHECK(nbd->ncells > 10 && nbd->ncells < 60);
    CHECK(nbd->ncells > 0 && nbd_get(&nbd->cell[nbd->ncells - 1], NBF_SRC)[0] == 'A' + 59 % 26);

    /* version 1: the history (u16 items; input, output, u8 error; the entry line) */
    pw_t w = {buf, sizeof buf, 0, false};
    pw_u16(&w, 2);
    pw_str(&w, "6*7"), pw_str(&w, "42"), pw_u8(&w, 0);
    pw_str(&w, "1/0"), pw_str(&w, "ERR:DIV0"), pw_u8(&w, 1);
    pw_str(&w, "");
    load_home(w.len, 1);
    CHECK(nbd->ncells == 2);
    CHECK(nbd->cell[0].kind == NBK_MATH && !strcmp(nbd_get(&nbd->cell[0], NBF_VAL), "42"));
    CHECK(!strcmp(nbd_get(&nbd->cell[1], NBF_ENAME), "CASError"));
    CHECK(!nbd->open);
}

/* a cell of kind k with execution count n and value v (err: an error instead) */
static void cell(int k, int n, const char *v, bool err)
{
    nb_cell_t *c = nbd_insert(nbd->ncells, k);
    nbd_set(c, NBF_SRC, "in");
    nbd_set(c, err ? NBF_EVALUE : NBF_VAL, v);
    if (err)
        nbd_set(c, NBF_ENAME, "CASError");
    c->count = (int16_t)n;
}
static const char *refs(const char *in)
{
    static char out[256], msg[80];
    if (!nbd_refs(in, out, sizeof out, msg, sizeof msg))
        snprintf(out, sizeof out, "!%s", msg);
    return out;
}

/* Ans and Out[n] (nbd_refs): the latest result, the cell of count n, errors and cells without a
 * value skipped, whole words only, strings left alone, a missing n */
static void test_refs(void)
{
    nbd_scratch();
    CHECK(!strcmp(refs("ans+1"), "ans+1")); /* no result yet: the CAS's own ans */
    cell(NBK_MATH, 1, "x^2+1", false);
    cell(NBK_TEXT, 0, NULL, false);
    cell(NBK_MATH, 3, "7", false);
    cell(NBK_PY, 4, "[1, 2]", false);
    cell(NBK_MATH, 5, "ERR:DOMAIN", true);
    cell(NBK_PY, 6, NULL, false); /* (no value) */
    CHECK(nbd_result(0) == 3);
    CHECK(nbd_result(3) == 2);
    CHECK(nbd_result(5) == -1);
    CHECK(!strcmp(refs("Out[1]*2"), "(x^2+1)*2"));
    CHECK(!strcmp(refs("out[ 3 ]+Out*[1]"), "(7)+(x^2+1)"));
    CHECK(!strcmp(refs("(Ans)+ans"), "(([1, 2]))+([1, 2])"));
    CHECK(!strcmp(refs("answer+Outer[1]+\"Out[1]\""), "answer+Outer[1]+\"Out[1]\""));
    CHECK(!strcmp(refs("Out[5]"), "!Out[5]: no such result"));
    CHECK(!strcmp(refs("Out[42]+1"), "!Out[42]: no such result"));
    CHECK(!strcmp(refs("Out[x]"), "!Out[n]: n is the number [n] of a cell"));
    CHECK(!strcmp(refs("Out"), "Out")); /* (a name of its own) */
    char small[8], msg[80];
    CHECK(!nbd_refs("Out[1]+Out[1]", small, sizeof small, msg, sizeof msg));
    nbd_scratch();
}

int main(void)
{
    void *mem = malloc(8u << 20);
    nbdoc_mem_init(mem, true);
    test_arena();
    test_cells();
    test_stash();
    test_session();
    test_refs();
    printf("test_nbdoc: %d checks, %d failures\n", checks, fails);
    free(mem);
    return fails ? 1 : 0;
}
