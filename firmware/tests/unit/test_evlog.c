/* Host test for the event log (core/evlog.c) and its session blob "evlog"
 * (persist_evlog in app/session.c): the ring wraps at EVLOG_N keeping the
 * newest, a repeated text counts up instead of taking a slot, the boot counter
 * counts app starts, a merge of the saved copy keeps each event once (same
 * boot, time and text; the larger repeat count), renumbers this boot after the
 * saved ones when the RAM record was lost, and the blob round-trips.
 * Built and run by `make test` (tests/unit/test_*.c, see Makefile). */
#include <stdio.h>
#include <string.h>
#include "evlog.h"
#include "session.h"

extern const persist_def_t persist_evlog;

static int fails, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if (!(c)) {                                                               \
            fails++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

static const char *text(int i) { return evlog_get(i) ? evlog_get(i)->text : ""; }

static void test_ring(void)
{
    evlog_test_reset();
    evlog_boot();
    CHECK(evlog_boots() == 1 && evlog_count() == 0);
    evlog_add("qspi: write %06x+%d failed (%d): reset and retry", 0x3f00, 4, -2);
    evlog_add("qspi: write %06x+%d failed (%d): reset and retry", 0x3f00, 4, -2);
    evlog_add("qspi: write %06x+%d failed (%d): reset and retry", 0x3f00, 4, -2);
    CHECK(evlog_count() == 1 && evlog_get(0)->count == 3 && evlog_get(0)->boot == 1);
    CHECK(!strcmp(text(0), "qspi: write 003f00+4 failed (-2): reset and retry"));
    char line[112];
    evlog_format(evlog_get(0), line, sizeof line);
    CHECK(strstr(line, "boot 1 +") == line);
    CHECK(strstr(line, " s: qspi: write 003f00+4 failed (-2): reset and retry (3 times)") != NULL);
    /* a long text is cut, not overrun */
    evlog_put("%s", "0123456789012345678901234567890123456789012345678901234567890123456789");
    CHECK(strlen(text(1)) == EVLOG_TEXT - 1);
    /* wrap: the oldest go */
    for (int i = 0; i < EVLOG_N + 3; i++)
        evlog_put("event %d", i);
    CHECK(evlog_count() == EVLOG_N);
    CHECK(!strcmp(text(0), "event 3") && !strcmp(text(EVLOG_N - 1), "event 18"));
    /* the same text in a later boot is a new event */
    evlog_boot();
    CHECK(evlog_boots() == 2);
    evlog_put("event 18");
    CHECK(evlog_count() == EVLOG_N && evlog_get(EVLOG_N - 1)->boot == 2 && evlog_get(EVLOG_N - 1)->count == 1);
    evlog_clear();
    CHECK(evlog_count() == 0 && evlog_boots() == 2 && evlog_unsaved());
    /* a power loss forgets the RAM record */
    evlog_put("x");
    evlog_power_lost();
    evlog_boot();
    CHECK(evlog_count() == 0 && evlog_boots() == 1);
}

static evlog_rec_t rec(uint32_t boot, uint32_t ms, uint16_t count, const char *t)
{
    evlog_rec_t r;
    memset(&r, 0, sizeof r);
    r.boot = boot, r.ms = ms, r.count = count;
    snprintf(r.text, sizeof r.text, "%s", t);
    return r;
}

static void test_merge(void)
{
    /* after a reset: the RAM ring has the saved events (one counted further) and newer ones */
    evlog_test_reset();
    evlog_boot(); /* boot 1 */
    evlog_boot(); /* boot 2 */
    evlog_boot(); /* boot 3 */
    evlog_rec_t saved[3] = {rec(1, 100, 1, "a"), rec(2, 50, 2, "b"), rec(2, 70, 1, "c")};
    evlog_merge(saved, 3, 2); /* RAM empty: all of them come in */
    CHECK(evlog_count() == 3 && evlog_boots() == 3);
    evlog_rec_t again[2] = {rec(2, 50, 5, "b"), rec(1, 100, 1, "a")};
    evlog_merge(again, 2, 2); /* duplicates: kept once, the larger count */
    CHECK(evlog_count() == 3);
    CHECK(!strcmp(text(0), "a") && !strcmp(text(1), "b") && !strcmp(text(2), "c"));
    CHECK(evlog_get(1)->count == 5);
    /* the same text at another time is another event */
    evlog_rec_t other[1] = {rec(2, 60, 1, "b")};
    evlog_merge(other, 1, 2);
    CHECK(evlog_count() == 4 && !strcmp(text(2), "b") && evlog_get(2)->ms == 60);

    /* after a power loss: this boot's events are renumbered after the saved boots */
    evlog_test_reset();
    evlog_boot(); /* boot 1 of an empty record */
    evlog_put("early");
    CHECK(evlog_get(0)->boot == 1);
    evlog_rec_t old[2] = {rec(6, 10, 1, "six"), rec(7, 20, 1, "seven")};
    evlog_merge(old, 2, 7);
    CHECK(evlog_boots() == 8 && evlog_count() == 3);
    CHECK(!strcmp(text(0), "six") && !strcmp(text(2), "early") && evlog_get(2)->boot == 8);

    /* more than EVLOG_N together: the newest stay */
    evlog_test_reset();
    evlog_boot();
    evlog_rec_t many[EVLOG_N];
    for (int i = 0; i < EVLOG_N; i++) {
        char t[16];
        snprintf(t, sizeof t, "s%d", i);
        many[i] = rec(0, (uint32_t)i, 1, t);
    }
    evlog_put("ram");
    evlog_merge(many, EVLOG_N, 0);
    CHECK(evlog_count() == EVLOG_N && !strcmp(text(0), "s1") && !strcmp(text(EVLOG_N - 1), "ram"));
}

static void test_persist(void)
{
    static uint8_t blob[4096];
    evlog_test_reset();
    evlog_boot();
    evlog_boot();
    evlog_put("qspi: still busy (sr=00000020)");
    evlog_put("qspi: still busy (sr=00000020)");
    evlog_put("session: save failed (-1)");
    CHECK(evlog_unsaved());
    pw_t w = {blob, sizeof blob, 0, false};
    persist_evlog.save(&w);
    CHECK(!w.overflow && !evlog_unsaved());
    /* a battery change: the RAM record is gone, the session brings the events back */
    evlog_power_lost();
    evlog_boot();
    evlog_put("start: watchdog (the firmware hung)");
    pr_t r = {blob, w.len, 0, false};
    persist_evlog.load(&r, persist_evlog.version);
    CHECK(evlog_boots() == 3 && evlog_count() == 3);
    CHECK(!strcmp(text(0), "qspi: still busy (sr=00000020)") && evlog_get(0)->count == 2 && evlog_get(0)->boot == 2);
    CHECK(!strcmp(text(2), "start: watchdog (the firmware hung)") && evlog_get(2)->boot == 3);
    /* loading the same copy again changes nothing */
    pr_t r2 = {blob, w.len, 0, false};
    persist_evlog.load(&r2, persist_evlog.version);
    CHECK(evlog_count() == 3 && evlog_boots() == 3);
    /* a truncated or foreign blob is ignored */
    pr_t r3 = {blob, w.len / 2, 0, false};
    persist_evlog.load(&r3, persist_evlog.version);
    pr_t r4 = {blob, w.len, 0, false};
    persist_evlog.load(&r4, 99);
    CHECK(evlog_count() == 3);
    CHECK(persist_evlog.reset == NULL); /* Reset all and the load's reset keep the log */
}

int main(void)
{
    test_ring();
    test_merge();
    test_persist();
    printf("test_evlog: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
