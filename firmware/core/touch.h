/*
 * Touch input: contact reports in, gestures out.
 *
 * Producers report the current set of contacts in SCREEN coordinates
 * (0..LCD_WIDTH-1, 0..LCD_HEIGHT-1, landscape, as drawn by the app):
 *   - hardware: the FocalTech FT6236 driver (platform/stm32h743/touch_focaltech.c), which
 *     maps panel coordinates to the screen (TOUCH_ORIENTATION in board.h),
 *   - the simulators: console `touch` command, web_touch() in the browser.
 * The recognizer turns contact frames into gesture events (tap, drag, pinch)
 * that the main loop hands to the app (app_touch_event). Main-context only:
 * the hardware driver calls touch_report() from hal_service(), not an ISR.
 *
 * Recognizer states (touch.c):
 *   IDLE --1 finger--> ONE (emits DOWN)
 *   ONE  --moved >= TOUCH_SLOP_PX from start--> DRAG (emits DRAG)
 *   ONE  --lifted--> TAP if held < TOUCH_TAP_MS, else UP
 *   DRAG --lifted--> UP, then SWIPE if the drag was a swipe (below)
 *   IDLE/ONE/DRAG --2nd finger--> PINCH (ONE/DRAG emit CANCEL first)
 *   PINCH --one finger lifted--> PINCH_END, then ignore input until all up
 *   PINCH --all lifted at once--> PINCH_END
 * A swipe is a fast, mostly straight one-finger drag: at least TOUCH_SWIPE_PX from
 * start to end, lifted within TOUCH_SWIPE_MS of touching down, at TOUCH_SWIPE_SPEED
 * px/s or more, and its main axis at least TOUCH_SWIPE_RATIO times the other (a
 * diagonal is not a swipe). It comes after the UP of the drag, so screens that scroll
 * by dragging keep working; a screen that uses swipes ignores that gesture's drags (it
 * can tell at the SWIPE). A swipe is never a TAP (taps never move past the slop).
 * Events are queued (31 deep); consecutive DRAG or PINCH events still in the
 * queue are coalesced, so a slow consumer sees fewer, larger steps rather than
 * losing the gesture end.
 */
#ifndef TOUCH_H
#define TOUCH_H
#include <stdbool.h>
#include <stdint.h>

#define TOUCH_MAX_CONTACTS 5 /* contacts tracked (FT6x36: 2 points; FT5x06-class: 5) */
#define TOUCH_SLOP_PX 8     /* movement before a press becomes a drag */
#define TOUCH_TAP_MS 600    /* longer presses without movement are not taps */
#define TOUCH_SWIPE_PX 48      /* a swipe moves at least this far ... */
#define TOUCH_SWIPE_MS 500     /* ... lifted within this long after touching down ... */
#define TOUCH_SWIPE_SPEED 200  /* ... at this many px/s or more ... */
#define TOUCH_SWIPE_RATIO 2    /* ... and mostly along one axis (main >= 2 x the other) */

/* One contact: screen pixels, and the controller's track id (stable while
 * the finger stays down). */
typedef struct {
    int16_t x, y;
    uint8_t id;
} touch_point_t;

enum touch_ev_type {
    TEV_NONE = 0,
    TEV_DOWN,       /* first finger down at (x, y) */
    TEV_DRAG,       /* one finger moved: (x, y) now, (x0, y0) start, (dx, dy) since previous event */
    TEV_UP,         /* finger lifted after a drag or a long press */
    TEV_TAP,        /* short press without movement at (x0, y0) */
    TEV_PINCH,      /* two fingers: scale = distance / start distance, (x, y) = centre */
    TEV_PINCH_END,  /* pinch finished (a finger lifted), final scale and centre */
    TEV_CANCEL,     /* a one-finger gesture (DOWN, drags) was interrupted by a second finger: undo any preview */
    TEV_SWIPE,      /* after the UP of a fast straight drag: (x0, y0) start, (x, y) end, (dx, dy) the whole
                     * movement, scale = speed in px/s; touch_swipe_dir() gives the direction */
};
enum { SWIPE_LEFT = 'L', SWIPE_RIGHT = 'R', SWIPE_UP = 'U', SWIPE_DOWN = 'D' };

typedef struct {
    uint8_t type;
    int16_t x, y;   /* current position (pinch: centre) */
    int16_t x0, y0; /* where the gesture started (pinch: start centre) */
    int16_t dx, dy; /* movement since the previous event of this gesture */
    float scale;    /* pinch only */
} touch_event_t;

/* The direction of a TEV_SWIPE event (SWIPE_*), 0 for other events. */
int touch_swipe_dir(const touch_event_t *ev);

/* Report all current contacts (n = 0 when nothing touches; pts may then be
 * NULL). n is clamped to TOUCH_MAX_CONTACTS and coordinates to the screen.
 * Call on every change of the contact set (and while fingers are down, as
 * often as positions are sampled); the time of the call, from hal_millis(),
 * is what the tap timeout measures. */
void touch_report(const touch_point_t *pts, int n);
/* Single-contact update for simulators / the console: state 1 = down, 2 = move,
 * 0 = up (same codes as web_touch). Keeps a contact table and calls touch_report. */
void touch_inject(int id, int x, int y, int state);
/* Dequeue the oldest gesture event (main loop). false if none. */
bool touch_pop(touch_event_t *ev);
bool touch_queue_empty(void);
/* Drop contacts, gesture state and queued events (called by the main loop
 * when waking from the simulators' off state, and by unit tests). No
 * event is emitted for a gesture in progress. Statistics are kept. */
void touch_reset(void);
/* Lower-case name of an event type ("tap", "pinch_end", ...), "?" if invalid. */
const char *touch_event_name(int type);

/* Statistics / last event for the console and tests. The count includes
 * events that were later coalesced or dropped from a full queue. */
uint32_t touch_event_count(void);
const touch_event_t *touch_last_event(void);
int touch_contacts(touch_point_t *out, int max); /* current contacts as last reported */

#endif
