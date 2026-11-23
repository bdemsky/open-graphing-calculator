/* Host test for the input comforts (app/input_*.c, interface app/input.h):
 * the fuzzy matcher's ranking tiers, the undo log (one step per event, word
 * runs coalesced, redo dropped by a new change, per-editor histories, the
 * pool dropping its oldest records), the clipboard ring (dedup, capacity,
 * the "clip" persist blob), the matrix-literal parser used by paste, and the
 * typed query line. The modules are driven directly: input_event_begin/end
 * stand in for app.c's key dispatch.
 * Built and run by `make test` (tests/unit/test_*.c, see Makefile). */
#include <stdio.h>
#include <string.h>
#include "input.h"
#include "session.h"

extern const persist_def_t persist_clip;

static int failures, checks;
#define CHECK(cond, ...)                                \
    do {                                                \
        checks++;                                       \
        if (!(cond)) {                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
            failures++;                                 \
        }                                               \
    } while (0)

static const char *text(const editor_t *e)
{
    static char b[ED_MAX * 4];
    ed_display(e, b, sizeof b);
    return b;
}

/* One key event that inserts `tok` into e (key decides word coalescing). */
static void type_tok(editor_t *e, int key, int tok)
{
    input_event_begin(key, MOD_NONE);
    ed_insert(e, tok);
    input_event_end();
}

static void test_fuzzy(void)
{
    CHECK(fuzzy_score("sin", "sin(") == 1000, "exact with (");
    CHECK(fuzzy_score("SIN", "sin(") == 1000, "case-insensitive");
    CHECK(fuzzy_score("sin", "sinh(") == 900, "prefix");
    CHECK(fuzzy_score("sin", "asin(") >= 550 && fuzzy_score("sin", "asin(") < 900, "substring");
    CHECK(fuzzy_score("int", "∫ integrate(") >= 690, "word start after a symbol: %d", fuzzy_score("int", "∫ integrate("));
    CHECK(fuzzy_score("fctr", "factor(") > fuzzy_score("fctr", "factorial("), "shorter subsequence wins");
    CHECK(fuzzy_score("fctr", "factor(") > 0 && fuzzy_score("fctr", "factor(") < 500, "subsequence tier");
    CHECK(fuzzy_score("xyz", "factor(") == 0, "no match");
    CHECK(fuzzy_score("", "abc") == 1, "empty query matches all");
    CHECK(fuzzy_score("100", "100-1") > fuzzy_score("100", "2^100"), "prefix beats substring");
}

static void test_undo(void)
{
    static editor_t a, b;
    ed_clear(&a);
    ed_clear(&b);
    type_tok(&a, KEY_1, '1');
    type_tok(&a, KEY_2, '2');
    type_tok(&a, KEY_ADD, '+');
    type_tok(&a, KEY_3, '3');
    CHECK(!strcmp(text(&a), "12+3"), "typed %s", text(&a));
    CHECK(input_undo_count(&a) == 3, "steps: 12, +, 3 (%d)", input_undo_count(&a));
    CHECK(input_undo(&a) && !strcmp(text(&a), "12+"), "undo 3: %s", text(&a));
    CHECK(input_undo(&a) && !strcmp(text(&a), "12"), "undo +: %s", text(&a));
    CHECK(a.cur == 2, "cursor restored");
    CHECK(input_undo(&a) && !strcmp(text(&a), ""), "undo 12 (one step)");
    CHECK(!input_undo(&a), "nothing left");
    CHECK(input_redo(&a) && !strcmp(text(&a), "12"), "redo: %s", text(&a));
    CHECK(input_redo_count(&a) == 2, "two redo steps left");
    /* a new change drops the redo steps of this editor only */
    type_tok(&b, KEY_7, '7');
    type_tok(&a, KEY_MUL, '*');
    CHECK(input_redo_count(&a) == 0 && !input_redo(&a), "redo dropped");
    CHECK(input_undo(&b) && ed_empty(&b) && !strcmp(text(&a), "12*"), "editors are independent");
    /* one event = one step, however many primitive changes */
    input_event_begin(KEY_SIN, MOD_NONE);
    ed_insert_text(&a, "sin(", ")");
    input_event_end();
    CHECK(!strcmp(text(&a), "12*sin()"), "template: %s", text(&a));
    CHECK(input_undo(&a) && !strcmp(text(&a), "12*"), "template undone at once: %s", text(&a));
    /* a cursor move ends a word run */
    type_tok(&a, KEY_4, '4');
    input_event_begin(KEY_LEFT, MOD_NONE);
    ed_left(&a);
    input_event_end();
    type_tok(&a, KEY_5, '5');
    CHECK(!strcmp(text(&a), "12*54"), "%s", text(&a));
    CHECK(input_undo(&a) && !strcmp(text(&a), "12*4"), "run split: %s", text(&a));
    /* a shared editor moving to another field forgets its steps */
    input_undo_new_field(&a);
    CHECK(ed_empty(&a) && input_undo_count(&a) == 0 && input_redo_count(&a) == 0, "new field");
    type_tok(&a, KEY_8, '8');
    CHECK(input_undo(&a) && ed_empty(&a) && !input_undo(&a), "first step goes back to empty only");
    ed_set_text(&a, "12*4");
    /* changes outside events and while paused are not recorded */
    int n = input_undo_count(NULL);
    ed_insert(&a, '9');
    input_event_begin(KEY_9, MOD_NONE);
    input_record_pause(true);
    ed_insert(&a, '9');
    input_record_pause(false);
    input_event_end();
    CHECK(input_undo_count(NULL) == n, "not recorded");
    /* the pool drops the oldest records when full */
    for (int i = 0; i < 200; i++) {
        input_event_begin(KEY_ADD, MOD_NONE);
        ed_set_text(&b, "0123456789012345678901234567890123456789");
        input_event_end();
    }
    CHECK(input_undo_count(&b) > 10 && input_undo_count(&b) < 200, "bounded: %d", input_undo_count(&b));
    while (input_undo(&b))
        ;
    CHECK(!strcmp(text(&b), "0123456789012345678901234567890123456789"), "oldest kept step");
}

static void test_clip(void)
{
    clip_clear();
    CHECK(clip_count() == 0 && !strcmp(clip_get(0), ""), "empty");
    clip_put("");
    CHECK(clip_count() == 0, "empty text ignored");
    clip_put("a");
    clip_put("b");
    clip_put("c");
    CHECK(clip_count() == 3 && !strcmp(clip_get(0), "c") && !strcmp(clip_get(2), "a"), "newest first");
    clip_put("a");
    CHECK(clip_count() == 3 && !strcmp(clip_get(0), "a") && !strcmp(clip_get(1), "c"), "dedup moves to front");
    for (int i = 0; i < 20; i++) {
        char t[8];
        snprintf(t, sizeof t, "x%d", i);
        clip_put(t);
    }
    CHECK(clip_count() == CLIP_N && !strcmp(clip_get(0), "x19") && !strcmp(clip_get(CLIP_N - 1), "x12"),
          "capacity");
    char big[600];
    memset(big, 'z', sizeof big - 1);
    big[sizeof big - 1] = 0;
    clip_put(big);
    CHECK(strlen(clip_get(0)) == CLIP_TEXT - 1, "long text truncated");
    clip_put("π√(2)");
    /* persist round trip */
    static uint8_t buf[4096];
    pw_t w = {buf, sizeof buf, 0, false};
    persist_clip.save(&w);
    CHECK(!w.overflow, "saved");
    clip_clear();
    pr_t r = {buf, w.len, 0, false};
    persist_clip.load(&r, persist_clip.version);
    CHECK(clip_count() == CLIP_N && !strcmp(clip_get(0), "π√(2)") && !strcmp(clip_get(2), "x19"), "loaded");
    pr_t t = {buf, 5, 0, false}; /* truncated blob: no crash, partial */
    persist_clip.load(&t, persist_clip.version);
    CHECK(clip_count() <= 1, "truncated: %d", clip_count());
}

static void test_matrix(void)
{
    uint16_t off[16], len[16];
    int r, c;
    const char *m = "[[1,2],[3, 4]]";
    CHECK(clip_parse_matrix(m, 4, 4, &r, &c, off, len) && r == 2 && c == 2, "2x2");
    CHECK(len[1 * 4 + 1] == 1 && m[off[1 * 4 + 1]] == '4', "element 2,2 trimmed");
    const char *n = "[[f(1,2),[5]],[√(3),-1]]";
    CHECK(clip_parse_matrix(n, 4, 4, &r, &c, off, len) && r == 2 && c == 2, "nested brackets");
    CHECK(len[0] == 6 && !strncmp(n + off[0], "f(1,2)", 6), "element with commas");
    CHECK(!clip_parse_matrix("[[1,2],[3]]", 4, 4, &r, &c, off, len), "ragged");
    CHECK(!clip_parse_matrix("[1,2]", 4, 4, &r, &c, off, len), "vector");
    CHECK(!clip_parse_matrix("[[1,2,3,4,5]]", 4, 4, &r, &c, off, len), "too wide");
    CHECK(!clip_parse_matrix("[[1,]]", 4, 4, &r, &c, off, len), "empty element");
    CHECK(!clip_parse_matrix("[[1]]x", 4, 4, &r, &c, off, len), "trailing text");
    CHECK(!clip_parse_matrix("12", 4, 4, &r, &c, off, len), "not a matrix");
}

static void test_query(void)
{
    query_t q;
    query_clear(&q);
    CHECK(query_key(&q, KEY_1, MOD_NONE) && query_key(&q, KEY_SIN, MOD_NONE), "keys");
    CHECK(!strcmp(q.s, "1sin("), "%s", q.s);
    CHECK(query_key(&q, KEY_DEL, MOD_NONE) && !strcmp(q.s, "1"), "DEL removes one key's text: %s", q.s);
    CHECK(!query_key(&q, KEY_UP, MOD_NONE), "navigation keys are not text");
    query_key(&q, KEY_2, MOD_NONE);
    CHECK(query_key(&q, KEY_DEL, MOD_SHIFT) && !q.s[0], "SHIFT DEL clears");
    CHECK(!query_key(&q, KEY_DEL, MOD_NONE), "DEL on an empty query");
}

int main(void)
{
    static uint8_t work[INPUT_WORK];
    input_init(work, true);
    test_fuzzy();
    test_undo();
    test_clip();
    test_matrix();
    test_query();
    printf("test_input: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
