/*
 * Drag to scroll (see ui.h): turns TEV_DRAG deltas into whole-row (or
 * whole-column) steps for every scrolling screen and menu, so they all
 * scroll the same way: the content follows the finger, one row per row
 * height of movement, clamped at both ends, the focused item pulled along.
 *
 * One gesture is active at a time, so the accumulators are shared;
 * app_touch_event() restarts them on TEV_DOWN.
 */
#include "ui.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

static int acc[2]; /* px moved along x / y and not yet turned into steps */

void ui_drag_reset(void) { acc[0] = acc[1] = 0; }

int ui_drag_steps(const touch_event_t *ev, int axis, int step, int lo, int hi)
{
    if (ev->type != TEV_DRAG || step <= 0)
        return 0;
    int *a = &acc[axis == UI_DRAG_X ? 0 : 1];
    *a += axis == UI_DRAG_X ? ev->dx : ev->dy;
    int n = -(*a / step); /* finger up (negative delta): later content */
    if (n < lo || n > hi) {
        n = n < lo ? lo : hi;
        *a %= step; /* drop the movement past the end */
    } else {
        *a += n * step;
    }
    return n;
}

bool ui_drag_scroll(const touch_event_t *ev, int axis, int step, int *top, int max_top)
{
    if (max_top < 0)
        max_top = 0;
    int t = *top < 0 ? 0 : *top > max_top ? max_top : *top;
    t += ui_drag_steps(ev, axis, step, -t, max_top - t);
    bool changed = t != *top;
    *top = t;
    return changed;
}

int ui_drag_focus(int focus, int top, int nvis)
{
    if (focus < top)
        return top;
    if (nvis > 0 && focus >= top + nvis)
        return top + nvis - 1;
    return focus;
}

bool ui_drag_list(const touch_event_t *ev, int step, int *top, int count, int nvis, int *sel)
{
    bool changed = ui_drag_scroll(ev, UI_DRAG_Y, step, top, count - nvis);
    if (sel && count > 0) {
        int s = ui_drag_focus(*sel, *top, nvis);
        if (s >= count)
            s = count - 1;
        changed |= s != *sel;
        *sel = s;
    }
    return changed;
}
