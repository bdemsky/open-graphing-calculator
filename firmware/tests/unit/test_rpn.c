/* Host test for app/rpn_stack.c: the RPN stack storage (levels as texts).
 *
 * The RPN screen keeps its stack as NUL-terminated level texts packed one
 * after another in a single caller-supplied byte buffer (level 1, the top, is
 * the last string). rpn_stack.c has no UI or CAS dependencies, so it is
 * tested directly: push/get, ROLL/ROLLD (in-place rotations of the buffer),
 * PICK/DROP, in-place replacement, the "buffer full" failures (which must
 * leave the stack unchanged), copying for UNDO, and rs_levels_within(), which
 * decides how many top levels fit in the session save budget.
 * Built and run by `make test` (tests/unit/test_*.c, see Makefile). */
#include <stdio.h>
#include <string.h>
#include "rpn.h"

static int failures, checks;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        checks++;                                         \
        if (!(cond)) {                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
            failures++;                                   \
        }                                                 \
    } while (0)

/* levels top-down joined with ',' ("c,b,a" for a b c pushed in that order) */
static const char *dump(const rstack_t *s)
{
    static char b[512];
    b[0] = 0;
    for (int lv = 1; lv <= s->depth; lv++) {
        if (lv > 1)
            strcat(b, ",");
        strcat(b, rs_get(s, lv));
    }
    return b;
}

/* Reset s and push v[0..n-1] in order (v[n-1] ends up on level 1). */
static void fill(rstack_t *s, const char *const *v, int n)
{
    rs_clear(s);
    for (int i = 0; i < n; i++)
        rs_push(s, v[i]);
}

int main(void)
{
    static char buf[64], buf2[64], small[8];
    rstack_t s, u, t;
    static const char *const abcde[] = {"a", "bb", "ccc", "dd", "e"};

    /* s: 64-byte stack for most checks; u: copy target for the undo
     * snapshot; t: an 8-byte stack to exercise the "full" paths */
    rs_init(&s, buf, sizeof buf);
    CHECK(s.depth == 0 && rs_get(&s, 1) == NULL, "empty");
    CHECK(!rs_drop(&s, 1) && !rs_roll(&s, 1) && !rs_pick(&s, 1), "ops on an empty stack fail");
    CHECK(rs_drop(&s, 0), "drop 0 is fine");

    fill(&s, abcde, 5);
    CHECK(s.depth == 5 && !strcmp(dump(&s), "e,dd,ccc,bb,a"), "push: %s", dump(&s));
    CHECK(!strcmp(rs_get(&s, 3), "ccc") && rs_get(&s, 6) == NULL && rs_get(&s, 0) == NULL, "get");
    CHECK(s.len == 2 + 3 + 4 + 3 + 2, "len %zu", s.len); /* each level is strlen + its NUL */

    /* SWAP = roll 2, ROT = roll 3, ROLL n */
    CHECK(rs_roll(&s, 2) && !strcmp(dump(&s), "dd,e,ccc,bb,a"), "swap: %s", dump(&s));
    CHECK(rs_roll(&s, 3) && !strcmp(dump(&s), "ccc,dd,e,bb,a"), "rot: %s", dump(&s));
    CHECK(rs_roll(&s, 5) && !strcmp(dump(&s), "a,ccc,dd,e,bb"), "roll 5: %s", dump(&s));
    CHECK(rs_roll(&s, 1) && !strcmp(dump(&s), "a,ccc,dd,e,bb"), "roll 1 is a no-op");
    CHECK(!rs_roll(&s, 6) && !strcmp(dump(&s), "a,ccc,dd,e,bb"), "roll past the depth fails");

    /* ROLLD undoes ROLL */
    CHECK(rs_rolld(&s, 5) && !strcmp(dump(&s), "ccc,dd,e,bb,a"), "rolld 5: %s", dump(&s));
    CHECK(rs_rolld(&s, 2) && !strcmp(dump(&s), "dd,ccc,e,bb,a"), "rolld 2: %s", dump(&s));
    CHECK(s.len == 14 && s.depth == 5, "rolls keep the size");

    /* DUP = pick 1, OVER = pick 2 */
    CHECK(rs_pick(&s, 1) && !strcmp(dump(&s), "dd,dd,ccc,e,bb,a"), "dup: %s", dump(&s));
    CHECK(rs_pick(&s, 3) && !strcmp(dump(&s), "ccc,dd,dd,ccc,e,bb,a"), "pick 3: %s", dump(&s));
    CHECK(rs_drop(&s, 3) && !strcmp(dump(&s), "ccc,e,bb,a"), "drop 3: %s", dump(&s));

    /* replace a level in place (shorter, longer) */
    CHECK(rs_replace(&s, 2, "x") && !strcmp(dump(&s), "ccc,x,bb,a"), "replace: %s", dump(&s));
    CHECK(rs_replace(&s, 4, "a long one") && !strcmp(dump(&s), "ccc,x,bb,a long one"), "replace: %s", dump(&s));
    CHECK(rs_replace(&s, 1, "") && !strcmp(dump(&s), ",x,bb,a long one"), "empty text: %s", dump(&s));
    CHECK(!rs_replace(&s, 5, "z"), "replace past the depth fails");

    /* full buffer: push / pick / replace fail without changing anything.
     * "abc" + "d" use 4 + 2 = 6 of the 8 bytes, so "ef" (3) does not fit but
     * "e" (2) fills the buffer exactly. */
    rs_init(&t, small, sizeof small);
    CHECK(rs_push(&t, "abc") && rs_push(&t, "d") && !rs_push(&t, "ef"), "small push");
    CHECK(rs_push(&t, "e") && t.len == 8 && !rs_pick(&t, 1) && !rs_replace(&t, 1, "ee"),
          "small full: %s", dump(&t));
    CHECK(!strcmp(dump(&t), "e,d,abc"), "small unchanged: %s", dump(&t));

    /* undo snapshot: copy and swap */
    rs_init(&u, buf2, sizeof buf2);
    fill(&s, abcde, 5);
    CHECK(rs_copy(&u, &s), "copy");
    rs_drop(&s, 4);
    CHECK(!strcmp(dump(&u), "e,dd,ccc,bb,a") && !strcmp(dump(&s), "a"), "copy is independent");
    CHECK(!rs_copy(&t, &u), "copy into a too small stack fails");

    /* saving: the top levels that fit in a byte budget (oldest dropped first).
     * Sizes count the NULs: "e" = 2 bytes, "dd" = 3, so 5 bytes hold two
     * levels and 4 bytes one. */
    fill(&s, abcde, 5);
    CHECK(rs_levels_within(&s, 1000) == 5, "all fit");
    CHECK(rs_levels_within(&s, 5) == 2, "e + dd = 5 bytes: %d", rs_levels_within(&s, 5));
    CHECK(rs_levels_within(&s, 4) == 1, "4 bytes: %d", rs_levels_within(&s, 4));
    CHECK(rs_levels_within(&s, 1) == 0, "nothing fits");

    /* random roll/rolld pairs keep the contents (n cycles through 1..5;
     * ROLLD n is the inverse of ROLL n) */
    fill(&s, abcde, 5);
    for (int i = 0; i < 200; i++) {
        int n = 1 + (i * 7) % 5;
        rs_roll(&s, n);
        rs_rolld(&s, n);
    }
    CHECK(!strcmp(dump(&s), "e,dd,ccc,bb,a"), "roll/rolld round trip: %s", dump(&s));

    printf("test_rpn: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
