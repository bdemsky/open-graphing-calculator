/* Host test for core/touch.c: the gesture recognizer used by every platform.
 *
 * The recognizer turns raw contact reports (finger id, x, y, down/move/up)
 * into gesture events (TEV_DOWN, TEV_TAP, TEV_DRAG, TEV_UP, TEV_PINCH,
 * TEV_PINCH_END, TEV_CANCEL) in a small queue that the UI pops. This test
 * feeds contacts through touch_inject() (the same entry point the simulators'
 * mouse and the console `touch` command use, so no touch controller is
 * needed) and checks the event sequences of the basic gestures, the drag
 * coalescing that keeps the 32-entry queue from overflowing, and clamping to
 * the LCD. Built and run by `make test` (tests/unit/test_*.c, see Makefile). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "hal.h"
#include "touch.h"

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

/* Pop every queued event; the first `max` are copied into evs (the rest are
 * discarded so the queue is always empty afterwards). Returns the number
 * copied. */
static int drain(touch_event_t *evs, int max)
{
    int n = 0;
    touch_event_t e;
    while (touch_pop(&e))
        if (n < max)
            evs[n++] = e;
    return n;
}

int main(void)
{
    touch_event_t ev[16];
    int n;

    /* touch_inject(id, x, y, state): state 1 = finger down, 2 = move, 0 = up */

    /* tap: down + up without movement (moves within TOUCH_SLOP_PX = 8 px of
     * the press still count as a tap, and the tap reports the press point).
     * A tap also needs the release within TOUCH_TAP_MS of the press; the
     * host clock (hal_millis) does not advance between these calls. */
    touch_reset();
    touch_inject(0, 100, 50, 1);
    touch_inject(0, 102, 51, 2); /* inside the slop: still a tap */
    touch_inject(0, 102, 51, 0);
    n = drain(ev, 16);
    CHECK(n == 2 && ev[0].type == TEV_DOWN && ev[1].type == TEV_TAP, "tap: n=%d", n);
    CHECK(n == 2 && ev[1].x == 100 && ev[1].y == 50, "tap position is the press position");

    /* drag: movement beyond the slop, deltas add up, UP at the end. Both
     * moves are queued before anything is popped, so push() merges them into
     * one TEV_DRAG whose delta is the sum (20,0) + (20,10) = (40,10) and
     * whose x0/y0 is the press point. */
    touch_reset();
    touch_inject(0, 10, 10, 1);
    touch_inject(0, 30, 10, 2);
    touch_inject(0, 50, 20, 2);
    touch_inject(0, 50, 20, 0);
    n = drain(ev, 16);
    CHECK(n == 3 && ev[1].type == TEV_DRAG && ev[2].type == TEV_UP, "drag sequence n=%d", n);
    if (n == 3) {
        CHECK(ev[1].dx == 40 && ev[1].dy == 10, "coalesced drag delta %d,%d", ev[1].dx, ev[1].dy);
        CHECK(ev[1].x0 == 10 && ev[1].y0 == 10 && ev[2].x == 50, "drag start/end");
    }

    /* pinch: two fingers, distance 40 -> 80 = scale 2, then PINCH_END. The
     * second finger closes the first one's DOWN with a CANCEL.
     * Fingers at x 140/180 (centre 160) spread to 120/200; the two moves
     * coalesce into one TEV_PINCH. The PINCH_END is emitted when the first
     * finger lifts (the other one is then ignored until it is up too); it
     * carries the last scale relative to the start distance and the
     * starting centre (160, 100) in x0/y0. */
    touch_reset();
    touch_inject(0, 140, 100, 1);
    touch_inject(1, 180, 100, 1);
    touch_inject(0, 120, 100, 2);
    touch_inject(1, 200, 100, 2);
    touch_inject(1, 200, 100, 0);
    touch_inject(0, 120, 100, 0);
    n = drain(ev, 16);
    CHECK(n == 4 && ev[0].type == TEV_DOWN && ev[1].type == TEV_CANCEL && ev[2].type == TEV_PINCH &&
              ev[3].type == TEV_PINCH_END,
          "pinch sequence n=%d", n);
    if (n == 4) {
        CHECK(fabsf(ev[3].scale - 2.0f) < 1e-3f, "pinch scale %f", (double)ev[3].scale);
        CHECK(ev[3].x0 == 160 && ev[3].y0 == 100, "pinch centre");
    }

    /* a second finger during a drag cancels the drag (DOWN, DRAG, CANCEL);
     * the gesture then becomes a pinch, which ends with PINCH_END as soon as
     * one finger lifts */
    touch_reset();
    touch_inject(0, 10, 10, 1);
    touch_inject(0, 60, 10, 2);
    touch_inject(1, 200, 10, 1);
    n = drain(ev, 16);
    CHECK(n == 3 && ev[2].type == TEV_CANCEL, "cancel n=%d type=%d", n, n ? ev[n - 1].type : -1);
    touch_inject(1, 200, 10, 0);
    touch_inject(0, 60, 10, 0);
    n = drain(ev, 16);
    CHECK(n == 1 && ev[0].type == TEV_PINCH_END, "remaining finger is ignored until all are up (n=%d)", n);

    /* coordinates are clamped to the screen */
    touch_reset();
    touch_inject(0, -20, 999, 1);
    touch_inject(0, -20, 999, 0);
    n = drain(ev, 16);
    CHECK(n == 2 && ev[1].x == 0 && ev[1].y == LCD_HEIGHT - 1, "clamp");

    /* the queue never overflows on a long drag: 2000 moves without popping
     * would overflow the 32-entry queue, but consecutive drags coalesce, so
     * the final UP is still queued */
    touch_reset();
    touch_inject(0, 0, 0, 1);
    for (int i = 1; i < 2000; i++)
        touch_inject(0, i % LCD_WIDTH, (i / 7) % LCD_HEIGHT, 2);
    touch_inject(0, 5, 5, 0);
    n = drain(ev, 16);
    CHECK(n >= 3 && ev[n - 1].type == TEV_UP, "long drag ends with UP (n=%d)", n);

    /* swipes: a fast straight drag gets a SWIPE after its UP (never a TAP), with its direction */
    static const struct {
        int x1, y1, dir;
    } sw[4] = {{220, 100, SWIPE_RIGHT}, {20, 100, SWIPE_LEFT}, {120, 20, SWIPE_UP}, {120, 200, SWIPE_DOWN}};
    for (int k = 0; k < 4; k++) {
        touch_reset();
        touch_inject(0, 120, 100, 1);
        touch_inject(0, (120 + sw[k].x1) / 2, (100 + sw[k].y1) / 2, 2);
        touch_inject(0, sw[k].x1, sw[k].y1, 2);
        touch_inject(0, sw[k].x1, sw[k].y1, 0);
        n = drain(ev, 16);
        CHECK(n == 4 && ev[0].type == TEV_DOWN && ev[1].type == TEV_DRAG && ev[2].type == TEV_UP &&
                  ev[3].type == TEV_SWIPE,
              "swipe %c: n=%d", sw[k].dir, n);
        if (n == 4) {
            CHECK(touch_swipe_dir(&ev[3]) == sw[k].dir, "swipe direction %c", touch_swipe_dir(&ev[3]));
            CHECK(ev[3].x0 == 120 && ev[3].y0 == 100 && ev[3].scale >= TOUCH_SWIPE_SPEED, "swipe start and speed");
        }
        for (int i = 0; i < n; i++)
            CHECK(ev[i].type != TEV_TAP, "a swipe is never a tap");
    }
    CHECK(touch_swipe_dir(&ev[0]) == 0, "not a swipe: no direction");
    /* a diagonal is not a swipe */
    touch_reset();
    touch_inject(0, 100, 100, 1);
    touch_inject(0, 160, 150, 2);
    touch_inject(0, 160, 150, 0);
    n = drain(ev, 16);
    CHECK(n == 3 && ev[2].type == TEV_UP, "diagonal: no swipe (n=%d)", n);
    /* too short */
    touch_reset();
    touch_inject(0, 100, 100, 1);
    touch_inject(0, 100 + TOUCH_SWIPE_PX - 8, 100, 2);
    touch_inject(0, 100 + TOUCH_SWIPE_PX - 8, 100, 0);
    n = drain(ev, 16);
    CHECK(n == 3 && ev[2].type == TEV_UP, "short drag: no swipe (n=%d)", n);
    /* a slow drag (released after TOUCH_SWIPE_MS) is not a swipe */
    touch_reset();
    touch_inject(0, 40, 100, 1);
    touch_inject(0, 200, 100, 2);
    hal_delay_ms(TOUCH_SWIPE_MS + 100);
    touch_inject(0, 200, 100, 0);
    n = drain(ev, 16);
    CHECK(n == 3 && ev[2].type == TEV_UP, "slow drag: no swipe (n=%d)", n);
    CHECK(!strcmp(touch_event_name(TEV_SWIPE), "swipe"), "swipe name");

    printf("test_touch: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
