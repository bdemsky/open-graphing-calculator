/*
 * Touch gesture recognizer (see touch.h for the state diagram).
 *
 * Input is a stream of complete contact frames (touch_report); the recognizer
 * looks only at the number of contacts and their positions, never at the
 * controller's own gesture register, so the hardware driver, the console and
 * the web simulator all behave identically. Output is a small ring buffer of
 * touch_event_t drained by the main loop (core/calc.c). Everything runs in
 * main context; there is no locking.
 */
#include "touch.h"
#include <math.h>
#include <string.h>
#include "hal.h"

/* G_WAIT_UP: a pinch ended because one finger lifted; ignore the remaining
 * finger(s) until all are up so the leftover finger does not start a drag. */
enum { G_IDLE, G_ONE, G_DRAG, G_PINCH, G_WAIT_UP };

/* Ring buffer: head = next write, tail = next read, one slot kept free to
 * tell full from empty (capacity QSIZE - 1). */
#define QSIZE 32
static touch_event_t q[QSIZE];
static unsigned qhead, qtail; /* main-context only */

static int gstate;
static uint8_t g_id;               /* the tracked finger (one-finger gestures) */
static int16_t sx, sy, lx, ly;     /* start and last position */
static uint32_t t_down;            /* hal_millis() of the first contact */
static float pinch_d0, pinch_last; /* finger distance at pinch start (px), last scale emitted */
static int16_t pinch_cx0, pinch_cy0, pinch_cx, pinch_cy; /* centre at start / last emitted */

static touch_point_t cur[TOUCH_MAX_CONTACTS]; /* last reported frame (console status) */
static int ncur;

/* contact table for touch_inject */
static struct {
    bool on;
    int16_t x, y;
} inj[TOUCH_MAX_CONTACTS];

static uint32_t nevents;
static touch_event_t last;

static int16_t clampx(int v) { return (int16_t)(v < 0 ? 0 : v >= LCD_WIDTH ? LCD_WIDTH - 1 : v); }
static int16_t clampy(int v) { return (int16_t)(v < 0 ? 0 : v >= LCD_HEIGHT ? LCD_HEIGHT - 1 : v); }

/* Enqueue an event, coalescing it into the newest queued event when both are
 * DRAG (positions replaced, deltas summed, start kept) or both PINCH (newest
 * state wins). Only the newest queued event is considered, so ordering
 * relative to DOWN/UP/TAP is preserved. Drops the event if the queue is full. */
static void push(const touch_event_t *e)
{
    nevents++;
    last = *e;
    /* Coalesce consecutive drags / pinches so a slow consumer never overflows. */
    if (qhead != qtail) {
        touch_event_t *p = &q[(qhead + QSIZE - 1) % QSIZE];
        if (p->type == e->type && e->type == TEV_DRAG) {
            p->x = e->x, p->y = e->y;
            p->dx = (int16_t)(p->dx + e->dx), p->dy = (int16_t)(p->dy + e->dy);
            return;
        }
        if (p->type == e->type && e->type == TEV_PINCH) {
            *p = *e;
            return;
        }
    }
    if ((qhead + 1) % QSIZE == qtail)
        return; /* full: drop */
    q[qhead] = *e;
    qhead = (qhead + 1) % QSIZE;
}

/* Build and queue an event. (x0, y0) is the one-finger start position, or the
 * pinch start centre for PINCH / PINCH_END. */
static void emit(int type, int x, int y, int dx, int dy, float scale)
{
    touch_event_t e = {(uint8_t)type, (int16_t)x, (int16_t)y, sx, sy, (int16_t)dx, (int16_t)dy, scale};
    if (type == TEV_PINCH || type == TEV_PINCH_END)
        e.x0 = pinch_cx0, e.y0 = pinch_cy0;
    push(&e);
}

static const touch_point_t *find(const touch_point_t *pts, int n, uint8_t id)
{
    for (int i = 0; i < n; i++)
        if (pts[i].id == id)
            return &pts[i];
    return NULL;
}

/* Distance (px) and midpoint of the two contacts with the lowest ids; n >= 2.
 * The distance is floored at 10 px so the scale ratio d / pinch_d0 can never
 * divide by zero (or explode) when two reported points nearly coincide. */
static void pinch_measure(const touch_point_t *pts, int n, float *d, int16_t *cx, int16_t *cy)
{
    /* the two lowest ids (stable while both stay down) */
    int a = 0, b = 1;
    for (int i = 0; i < n; i++)
        if (pts[i].id < pts[a].id)
            a = i;
    b = a == 0 ? 1 : 0;
    for (int i = 0; i < n; i++)
        if (i != a && pts[i].id < pts[b].id)
            b = i;
    float dx = (float)(pts[a].x - pts[b].x), dy = (float)(pts[a].y - pts[b].y);
    *d = sqrtf(dx * dx + dy * dy);
    if (*d < 10.0f)
        *d = 10.0f;
    *cx = (int16_t)((pts[a].x + pts[b].x) / 2);
    *cy = (int16_t)((pts[a].y + pts[b].y) / 2);
}

void touch_report(const touch_point_t *pts_in, int n)
{
    if (n < 0)
        n = 0;
    if (n > TOUCH_MAX_CONTACTS)
        n = TOUCH_MAX_CONTACTS;
    touch_point_t pts[TOUCH_MAX_CONTACTS];
    for (int i = 0; i < n; i++) {
        pts[i] = pts_in[i];
        pts[i].x = clampx(pts[i].x);
        pts[i].y = clampy(pts[i].y);
    }
    memcpy(cur, pts, sizeof(touch_point_t) * (size_t)n);
    ncur = n;
    uint32_t now = hal_millis();

    if (n == 0) { /* all fingers up: finish whatever gesture was running */
        switch (gstate) {
        case G_ONE: /* never moved beyond the slop */
            if (now - t_down < TOUCH_TAP_MS) /* unsigned: wrap-safe */
                emit(TEV_TAP, sx, sy, 0, 0, 1.0f);
            else
                emit(TEV_UP, lx, ly, 0, 0, 1.0f);
            break;
        case G_DRAG: {
            emit(TEV_UP, lx, ly, 0, 0, 1.0f);
            /* a swipe: far, fast, soon and straight */
            int dx = lx - sx, dy = ly - sy, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            int main_ = ax > ay ? ax : ay, other = ax > ay ? ay : ax;
            uint32_t ms = now - t_down;
            float speed = (float)main_ * 1000.0f / (float)(ms ? ms : 1);
            if (main_ >= TOUCH_SWIPE_PX && ms <= TOUCH_SWIPE_MS && speed >= TOUCH_SWIPE_SPEED &&
                main_ >= TOUCH_SWIPE_RATIO * other)
                emit(TEV_SWIPE, lx, ly, dx, dy, speed);
            break;
        }
        case G_PINCH:
            emit(TEV_PINCH_END, pinch_cx, pinch_cy, 0, 0, pinch_last);
            break;
        default:
            break;
        }
        gstate = G_IDLE;
        return;
    }

    if (n >= 2) {
        if (gstate == G_IDLE || gstate == G_ONE || gstate == G_DRAG) {
            /* Second finger: start a pinch. The first frame only records the
             * reference distance/centre; PINCH events follow on movement. A
             * one-finger gesture in progress (DOWN already sent, maybe drags)
             * is cancelled so the app can undo its preview. */
            if (gstate == G_ONE || gstate == G_DRAG)
                emit(TEV_CANCEL, lx, ly, 0, 0, 1.0f);
            pinch_measure(pts, n, &pinch_d0, &pinch_cx0, &pinch_cy0);
            pinch_cx = pinch_cx0, pinch_cy = pinch_cy0;
            pinch_last = 1.0f;
            gstate = G_PINCH;
            return;
        }
        if (gstate == G_PINCH) {
            float d;
            int16_t cx, cy;
            pinch_measure(pts, n, &d, &cx, &cy);
            float s = d / pinch_d0;
            /* emit only on a >1 % scale change or a moved centre, to avoid
             * flooding the app with identical frames from a polled controller */
            if (fabsf(s - pinch_last) > 0.01f || cx != pinch_cx || cy != pinch_cy) {
                pinch_last = s;
                pinch_cx = cx, pinch_cy = cy;
                emit(TEV_PINCH, cx, cy, 0, 0, s);
            }
        }
        return; /* G_WAIT_UP: ignore until all fingers are up */
    }

    /* n == 1 */
    switch (gstate) {
    case G_IDLE:
        g_id = pts[0].id;
        sx = lx = pts[0].x;
        sy = ly = pts[0].y;
        t_down = now;
        gstate = G_ONE;
        emit(TEV_DOWN, sx, sy, 0, 0, 1.0f);
        break;
    case G_ONE:
    case G_DRAG: {
        const touch_point_t *p = find(pts, n, g_id);
        if (!p)
            p = &pts[0]; /* controller renumbered: follow the remaining contact */
        int dx = p->x - lx, dy = p->y - ly;
        if (gstate == G_ONE) {
            /* still a potential tap until the finger leaves a circle of
             * radius TOUCH_SLOP_PX around the start (squared distance) */
            int tx = p->x - sx, ty = p->y - sy;
            if (tx * tx + ty * ty < TOUCH_SLOP_PX * TOUCH_SLOP_PX)
                break;
            gstate = G_DRAG;
            /* the first DRAG carries the whole movement since the start
             * (lx, ly were never advanced while within the slop) */
            dx = tx, dy = ty;
        }
        if (dx || dy) {
            lx = p->x, ly = p->y;
            emit(TEV_DRAG, lx, ly, dx, dy, 1.0f);
        }
        break;
    }
    case G_PINCH: /* one of the two fingers lifted: the pinch is over */
        emit(TEV_PINCH_END, pinch_cx, pinch_cy, 0, 0, pinch_last);
        gstate = G_WAIT_UP;
        break;
    default: /* G_WAIT_UP */
        break;
    }
}

/* Update one slot of the injection contact table and report the whole table
 * as a frame, contacts in slot order with id = slot. Out-of-range ids are
 * ignored. */
void touch_inject(int id, int x, int y, int state)
{
    if (id < 0 || id >= TOUCH_MAX_CONTACTS)
        return;
    if (state) {
        inj[id].on = true;
        inj[id].x = clampx(x);
        inj[id].y = clampy(y);
    } else {
        inj[id].on = false;
    }
    touch_point_t pts[TOUCH_MAX_CONTACTS];
    int n = 0;
    for (int i = 0; i < TOUCH_MAX_CONTACTS; i++)
        if (inj[i].on)
            pts[n++] = (touch_point_t){inj[i].x, inj[i].y, (uint8_t)i};
    touch_report(pts, n);
}

bool touch_pop(touch_event_t *ev)
{
    if (qhead == qtail)
        return false;
    *ev = q[qtail];
    qtail = (qtail + 1) % QSIZE;
    return true;
}

bool touch_queue_empty(void) { return qhead == qtail; }

void touch_reset(void)
{
    qhead = qtail = 0;
    gstate = G_IDLE;
    ncur = 0;
    memset(inj, 0, sizeof inj);
}

const char *touch_event_name(int type)
{
    static const char *const names[] = {"none", "down", "drag", "up", "tap", "pinch", "pinch_end", "cancel", "swipe"};
    return type >= 0 && type <= TEV_SWIPE ? names[type] : "?";
}

int touch_swipe_dir(const touch_event_t *ev)
{
    if (!ev || ev->type != TEV_SWIPE)
        return 0;
    int ax = ev->dx < 0 ? -ev->dx : ev->dx, ay = ev->dy < 0 ? -ev->dy : ev->dy;
    return ax >= ay ? (ev->dx < 0 ? SWIPE_LEFT : SWIPE_RIGHT) : (ev->dy < 0 ? SWIPE_UP : SWIPE_DOWN);
}

uint32_t touch_event_count(void) { return nevents; }
const touch_event_t *touch_last_event(void) { return &last; }

int touch_contacts(touch_point_t *out, int max)
{
    int n = ncur < max ? ncur : max;
    memcpy(out, cur, sizeof(touch_point_t) * (size_t)n);
    return n;
}
