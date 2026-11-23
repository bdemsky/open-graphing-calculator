/*
 * Math Quest: the widgets -- one set of controls for every game, so a convention is made once
 * (docs/MATHQUEST.md "Widgets"; the audit's C1, C2, C5 and its widget proposal).
 *
 * Immediate mode: a game lays out its widgets from its state (mq_wg_begin, mq_wg_add ...) before it
 * draws and before it reads input, then hands every input to the set (mq_wg_touch, _swipe, _scroll,
 * _key) and acts on the event that comes back (mq_wev_t: which widget, why, the new value). The set
 * keeps what lives between frames: the focus (by the widget's id, so it survives a new layout), the
 * finger on a widget, a thing being carried, the value at the finger's start.
 *
 * Every widget, whatever its kind:
 *   - a hit area of at least MQ_WG_HIT (48 logical px, about 10 mm on the 3.2" panel) each way around
 *     its drawn box; where hit areas overlap, the widget whose centre is nearest wins;
 *   - the standard gestures of its kind (below), on the calculator's touch panel and keys and with the
 *     Mac's mouse, wheel / trackpad scroll (MODX_EV_SCROLL), trackpad pinch and keyboard;
 *   - pressed and drag feedback, a focus ring for the keys (the order of creation is the focus order;
 *     the arrows move to the nearest widget that way), and its own first-time demo (mq_wg_demo: a
 *     ghost hand doing its gesture).
 *
 *   BUTTON    tap (or ENTER when focused): pressed look, then the action
 *   CHOICE    an answer to pick (a bubble, a hole, a gate, a coin): tap, ENTER, or carried onto it
 *   WHEEL     a digit on a wheel: tap its up / down arrow or above / below the middle, drag up or down
 *             (a digit every 14 px, it snaps), flick (three at once), scroll, UP / DOWN; a digit key
 *             sets it and the focus moves right; DEL sets 0
 *   SLIDER    a value along a track (WF_DIAL: an angle round a centre): tap a spot, drag, flick or scroll
 *             a step, LEFT / RIGHT (UP / DOWN for a dial)
 *   NUMLINE   a slider whose ticks are a number line (the frog hops along it)
 *   DRAG      a thing to carry (a card, a coin): pressed it lifts (a shadow), follows the finger, drops
 *             on the DROP under it; or tap it, then tap where it goes; ENTER picks it up, the arrows
 *             choose the target, ENTER drops it
 *   DROP      where a carried thing goes: it glows under the thing
 *   KEY       a key of a digit pad: tap, or the same key on the keyboard
 *   CLOCKHAND a hand of a clock: drag round (the hand nearest the finger's angle is the one that moves),
 *             a tap on the face points the last hand used there, scroll a step, LEFT / RIGHT
 *   STEER     a catcher or a lane: drag, tap a side to step that way, flick to the far side, scroll,
 *             LEFT / RIGHT (UP / DOWN when WF_VERT)
 *   AREA      a field the game reads itself (a pie's slices, a rug's corner, the map): every touch with
 *             its coordinates, scroll and the arrows passed on
 */
#include "mq.h"

enum { HK_UP = 12, HK_LEFT = 13, HK_RIGHT = 14, HK_DOWN = 15, HK_DEL = 8, HK_ENTER = 49 };
enum { TV_DOWN = 1, TV_DRAG, TV_UP, TV_TAP, TV_PINCH, TV_PINCH_END, TV_CANCEL };

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int a) { return a < 0 ? -a : a; }

mq_wgs_t *mq_wgs(mq_t *q) { return &q->wg; }

/* B3 (CR2-07): a new screen, order or overlay starts with nothing pressed, carried, picked or hovered (the set's
 * indices are the old layout's; the keyboard focus is kept by its id as ever) */
void mq_wg_reset(mq_t *q)
{
    mq_wgs_t *s = &q->wg;
    s->press = s->carry = s->picked = s->hover = s->dragged = -1;
    s->downpick = false, s->flash_t = 0;
}

void mq_wg_begin(mq_t *q)
{
    mq_wgs_t *s = &q->wg;
    if (s->focus >= 0 && s->focus < s->n)
        s->focus_id = s->w[s->focus].id, s->focus_arg = s->w[s->focus].arg;
    s->n = 0;
    s->focus = -1;
}

int mq_wg_add(mq_t *q, int kind, int id, int arg, int x, int y, int w, int h)
{
    mq_wgs_t *s = &q->wg;
    if (s->n >= MQ_NWG)
        return -1;
    int i = s->n++;
    mq_wg_t *g = &s->w[i];
    for (size_t k = 0; k < sizeof *g; k++)
        ((uint8_t *)g)[k] = 0;
    g->kind = (uint8_t)kind, g->id = (uint8_t)id, g->arg = (uint8_t)arg;
    g->x = (int16_t)x, g->y = (int16_t)y, g->w = (int16_t)w, g->h = (int16_t)h;
    g->hi = 9, g->step = 1;
    if (s->focus < 0 && s->focus_id == id && s->focus_arg == arg && s->focus_id)
        s->focus = (int8_t)i;
    return i;
}
mq_wg_t *mq_wg(mq_t *q, int i) { return i >= 0 && i < q->wg.n ? &q->wg.w[i] : NULL; }
void mq_wg_range(mq_t *q, int i, int value, int lo, int hi, int step)
{
    mq_wg_t *g = mq_wg(q, i);
    if (g)
        g->value = (int16_t)value, g->lo = (int16_t)lo, g->hi = (int16_t)hi, g->step = (int16_t)(step ? step : 1);
}
void mq_wg_label(mq_t *q, int i, const char *s, uint16_t col)
{
    mq_wg_t *g = mq_wg(q, i);
    if (!g)
        return;
    int k = 0;
    for (; s && s[k] && k < (int)sizeof g->label - 1; k++)
        g->label[k] = s[k];
    g->label[k] = 0;
    g->col = col;
}
void mq_wg_flags(mq_t *q, int i, int flags)
{
    mq_wg_t *g = mq_wg(q, i);
    if (g)
        g->flags = (uint8_t)(g->flags | flags);
}

/* the hit area: the drawn box grown to MQ_WG_HIT each way, centred on it */
void mq_wg_hitbox(const mq_wg_t *g, int *x, int *y, int *w, int *h)
{
    *w = imax(g->w, MQ_WG_HIT), *h = imax(g->h, MQ_WG_HIT);
    if ((g->kind == WG_NUMLINE || g->kind == WG_SLIDER) && !(g->flags & (WF_DIAL | WF_VERT)))
        *w += 24; /* (the ends: a finger on the last stone, half of it past the track, still counts) */
    *x = g->x + g->w / 2 - *w / 2, *y = g->y + g->h / 2 - *h / 2;
}
static bool focusable(const mq_wg_t *g) { return !(g->flags & (WF_DISABLED | WF_NOFOCUS)) && g->kind != WG_DROP; }

/* the angle of (dx, dy) clockwise from 12 o'clock (0..359) */
static int clock_angle(int dx, int dy) { return mq_angle_of(-dy, dx); }
static int adist(int a, int b)
{
    int d = iabs(a - b) % 360;
    return d > 180 ? 360 - d : d;
}

/* the widget under (x, y): the nearest centre among the hit areas there (clock hands: the hand whose
 * angle is nearest the finger's); -1 none */
int mq_wg_at(mq_t *q, int x, int y)
{
    mq_wgs_t *s = &q->wg;
    int best = -1;
    int32_t bd = 0x7FFFFFFF;
    for (int i = 0; i < s->n; i++) {
        const mq_wg_t *g = &s->w[i];
        if (g->flags & WF_DISABLED)
            continue;
        int hx, hy, hw, hh;
        mq_wg_hitbox(g, &hx, &hy, &hw, &hh);
        if (x < hx || x >= hx + hw || y < hy || y >= hy + hh)
            continue;
        int cx = g->x + g->w / 2, cy = g->y + g->h / 2;
        int32_t d;
        if (g->kind == WG_CLOCKHAND) /* (w, h: the face; the hand's angle is its value) */
            d = adist(clock_angle(x - cx, y - cy), g->value * 360 / imax(1, g->hi + 1)) * 4;
        else if (g->kind == WG_AREA)
            d = 0x7FFFFFF0; /* (a field: only where nothing else is) */
        else if (g->kind == WG_STEER)
            d = 0x7FFFFFE0; /* (a steering field: a falling answer over it is the tap's, wherever on it) */
        else {
            int dx = (x - cx) * 256 / imax(8, g->w), dy = (y - cy) * 256 / imax(8, g->h);
            d = (int32_t)dx * dx + (int32_t)dy * dy;
        }
        if (d < bd)
            bd = d, best = i;
    }
    return best;
}

static int wrapv(const mq_wg_t *g, int v)
{
    int span = g->hi - g->lo + 1;
    if (span <= 0)
        return g->lo;
    return g->lo + ((v - g->lo) % span + span) % span;
}
static int clampv(const mq_wg_t *g, int v) { return v < g->lo ? g->lo : v > g->hi ? g->hi : v; }

/* a slider's value at x (or a dial's at the angle round its centre: x, y) */
static int slider_value(const mq_wg_t *g, int x, int y)
{
    if (g->flags & WF_DIAL) { /* an angle, 0 to the right, counter-clockwise; the centre at the box's bottom middle */
        int a = mq_angle_of(x - (g->x + g->w / 2), g->y + g->h - y);
        a = a <= 180 ? a : a > 270 ? 0 : 180;
        return clampv(g, (a + g->step / 2) / g->step * g->step);
    }
    if (g->flags & WF_VERT) {
        int span = imax(1, g->h), v = g->lo + ((y - g->y) * (g->hi - g->lo) + span / 2) / span;
        return clampv(g, v);
    }
    int span = imax(1, g->w), v = g->lo + ((x - g->x) * (g->hi - g->lo) + span / 2) / span;
    return clampv(g, v / g->step * g->step);
}
static int hand_value(const mq_wg_t *g, int x, int y)
{
    int a = clock_angle(x - (g->x + g->w / 2), y - (g->y + g->h / 2)), n = g->hi + 1;
    return (a * n + 180) / 360 % n;
}

static void ev_set(mq_wev_t *ev, int i, const mq_wg_t *g, int why, int value, int x, int y)
{
    ev->idx = (int8_t)i, ev->kind = g ? g->kind : 0, ev->id = g ? g->id : 0, ev->arg = g ? g->arg : 0;
    ev->why = (uint8_t)why, ev->value = (int16_t)value, ev->x = (int16_t)x, ev->y = (int16_t)y, ev->target = -1;
}

/* A touch (TV_*). True when a widget took it; *ev says what happened (why WHY_NONE: only feedback). */
bool mq_wg_touch(mq_t *q, int type, int x, int y, mq_wev_t *ev)
{
    mq_wgs_t *s = &q->wg;
    ev_set(ev, -1, NULL, WHY_NONE, 0, x, y);
    s->px = (int16_t)x, s->py = (int16_t)y;
    if (type == TV_DOWN) {
        s->downpick = false;
        int i = mq_wg_at(q, x, y);
        s->press = (int8_t)i, s->ox = (int16_t)x, s->oy = (int16_t)y, s->press_t = mq_mark(q), s->moved = 0, s->dragged = -1;
        s->carry = -1;
        if (i < 0)
            return false;
        mq_wg_t *g = &s->w[i];
        s->v0 = g->value;
        s->kbd = false;
        if (focusable(g))
            s->focus = (int8_t)i;
        switch (g->kind) {
        case WG_DRAG: s->carry = (int8_t)i, s->hover = -1; break; /* lifted at once */
        case WG_SLIDER: case WG_NUMLINE:
            ev_set(ev, i, g, WHY_DRAG, slider_value(g, x, y), x, y);
            break;
        case WG_STEER:
            ev_set(ev, i, g, WHY_DRAG, clampv(g, (g->flags & WF_VERT) ? slider_value(g, x, y) : x), x, y);
            break;
        case WG_CLOCKHAND: ev_set(ev, i, g, WHY_DRAG, hand_value(g, x, y), x, y); break;
        case WG_AREA: ev_set(ev, i, g, WHY_PRESS, 0, x, y); break;
        case WG_CHOICE:
            if (g->flags & WF_ONDOWN) /* (answers that move: picked the moment the finger lands) */
                ev_set(ev, i, g, WHY_TAP, g->value, x, y), s->press = -1, s->downpick = true;
            break;
        default: break;
        }
        return true;
    }
    if (type == TV_DRAG) {
        if (iabs(x - s->ox) + iabs(y - s->oy) > 4)
            s->moved = 1;
        if (s->carry >= 0 && s->carry < s->n) {
            int t = -1;
            for (int i = 0; i < s->n; i++) { /* the drop target under the thing */
                const mq_wg_t *d = &s->w[i];
                int hx, hy, hw, hh;
                mq_wg_hitbox(d, &hx, &hy, &hw, &hh);
                if (d->kind == WG_DROP && !(d->flags & WF_DISABLED) && x >= hx && x < hx + hw && y >= hy && y < hy + hh)
                    t = i;
            }
            s->hover = (int8_t)t;
            ev_set(ev, s->carry, &s->w[s->carry], WHY_DRAG, t >= 0 ? s->w[t].arg : -1, x, y);
            ev->target = (int8_t)t;
            return true;
        }
        int i = s->press;
        if (i < 0 || i >= s->n)
            return false;
        mq_wg_t *g = &s->w[i];
        switch (g->kind) {
        case WG_WHEEL: { /* a digit every 14 px, up is more */
            int v = wrapv(g, s->v0 + (s->oy - y) / 14);
            if (v != g->value)
                ev_set(ev, i, g, WHY_DRAG, v, x, y), g->value = (int16_t)v;
            return true;
        }
        case WG_SLIDER: case WG_NUMLINE: ev_set(ev, i, g, WHY_DRAG, slider_value(g, x, y), x, y); return true;
        case WG_STEER:
            ev_set(ev, i, g, WHY_DRAG, clampv(g, (g->flags & WF_VERT) ? slider_value(g, x, y) : x), x, y);
            return true;
        case WG_CLOCKHAND: ev_set(ev, i, g, WHY_DRAG, hand_value(g, x, y), x, y); return true;
        case WG_AREA: ev_set(ev, i, g, WHY_DRAG, 0, x, y); return true;
        default: return true;
        }
    }
    if (type == TV_UP || type == TV_CANCEL) {
        s->downpick = false;
        int c = s->carry, h = s->hover;
        s->carry = -1, s->hover = -1;
        int i = s->press;
        s->press = -1;
        if (c >= 0 && c < s->n) {
            if (type == TV_UP && h >= 0 && s->moved) {
                ev_set(ev, c, &s->w[c], WHY_DROP, s->w[h].arg, x, y);
                ev->target = (int8_t)h;
            } else
                ev_set(ev, c, &s->w[c], WHY_RELEASE, -1, x, y);
            return true;
        }
        if (i >= 0 && i < s->n && s->w[i].kind == WG_AREA)
            ev_set(ev, i, &s->w[i], WHY_RELEASE, 0, x, y);
        s->dragged = -1;
        if (i >= 0 && i < s->n && s->moved &&
            (s->w[i].kind == WG_SLIDER || s->w[i].kind == WG_NUMLINE || s->w[i].kind == WG_CLOCKHAND))
            s->dragged = (int8_t)i;
        return i >= 0;
    }
    if (type == TV_TAP) {
        s->press = -1, s->carry = -1;
        bool dp = s->downpick && mq_since(q, s->press_t) < 1500; /* (its DOWN, just now) */
        s->downpick = false;
        int i = mq_wg_at(q, x, y);
        if (i < 0) {
            s->picked = -1;
            return false;
        }
        mq_wg_t *g = &s->w[i];
        s->flash = (uint8_t)i, s->flash_t = mq_mark(q);
        switch (g->kind) {
        case WG_BUTTON: case WG_KEY: ev_set(ev, i, g, WHY_TAP, g->value, x, y); break;
        case WG_CHOICE:
            if (!(g->flags & WF_ONDOWN) || !dp) /* (a tap with no DOWN before it -- a test, a mouse path -- picks too) */
                ev_set(ev, i, g, WHY_TAP, g->value, x, y);
            if (s->picked >= 0 && s->picked < s->n) /* (a carried coin tapped onto an answer) */
                s->picked = -1;
            break;
        case WG_WHEEL: { /* the top half (its arrow) rolls it up, the bottom half down */
            int v = wrapv(g, g->value + (y < g->y + g->h / 2 ? 1 : -1));
            ev_set(ev, i, g, WHY_TAP, v, x, y), g->value = (int16_t)v;
            break;
        }
        case WG_SLIDER: case WG_NUMLINE: ev_set(ev, i, g, WHY_TAP, slider_value(g, x, y), x, y); break;
        case WG_STEER: { /* a tap steps toward it */
            int at = (g->flags & WF_VERT) ? slider_value(g, x, y) : x;
            int v = at > g->value ? imin(at, g->value + g->step) : at < g->value ? imax(at, g->value - g->step) : at;
            ev_set(ev, i, g, WHY_TAP, clampv(g, (g->flags & WF_VERT) ? at : v), x, y);
            break;
        }
        case WG_CLOCKHAND: ev_set(ev, i, g, WHY_TAP, hand_value(g, x, y), x, y); break;
        case WG_DRAG: /* tap, then tap where it goes */
            s->picked = (int8_t)i;
            ev_set(ev, i, g, WHY_PRESS, 0, x, y);
            break;
        case WG_DROP:
            if (s->picked >= 0 && s->picked < s->n && s->w[s->picked].kind == WG_DRAG) { /* (still a thing to carry) */
                ev_set(ev, s->picked, &s->w[s->picked], WHY_DROP, g->arg, x, y);
                ev->target = (int8_t)i;
                s->picked = -1;
            } else
                ev_set(ev, i, g, WHY_TAP, g->arg, x, y);
            break;
        case WG_AREA: ev_set(ev, i, g, WHY_TAP, 0, x, y); break;
        default: break;
        }
        return true;
    }
    return false;
}

/* A flick (dir 0 left, 1 right, 2 up, 3 down) that began at (x0, y0) */
bool mq_wg_swipe(mq_t *q, int dir, int x0, int y0, mq_wev_t *ev)
{
    int i = mq_wg_at(q, x0, y0);
    ev_set(ev, -1, NULL, WHY_NONE, 0, x0, y0);
    if (i < 0)
        return false;
    mq_wg_t *g = &q->wg.w[i];
    int d = dir == 1 || dir == 2 ? 1 : -1;
    if (q->wg.dragged == i) { /* the swipe of a drag that already set it (Hop Along: the frog stepped back a stone) */
        q->wg.dragged = -1;
        return false;
    }
    switch (g->kind) {
    case WG_WHEEL:
        if (dir < 2)
            return false;
        ev_set(ev, i, g, WHY_FLICK, wrapv(g, g->value + 3 * d), x0, y0); /* it spins on */
        g->value = ev->value;
        return true;
    case WG_SLIDER: case WG_NUMLINE: case WG_CLOCKHAND:
        ev_set(ev, i, g, WHY_FLICK, clampv(g, g->value + (dir == 0 || dir == 3 ? -1 : 1) * g->step), x0, y0);
        return true;
    case WG_STEER: { /* to the far side */
        bool vert = g->flags & WF_VERT;
        if ((dir < 2) == vert)
            return false;
        ev_set(ev, i, g, WHY_FLICK, (vert ? dir == 3 : dir == 1) ? g->hi : g->lo, x0, y0);
        return true;
    }
    default: return false;
    }
}

/* A scroll wheel (dy notches, + down) at (x, y): only an adjustable widget under the pointer (a wheel, slider,
 * line, clock hand; an area passes it on) -- never the focused one elsewhere, never steering */
bool mq_wg_scroll(mq_t *q, int dy, int x, int y, mq_wev_t *ev)
{
    mq_wgs_t *s = &q->wg;
    int i = mq_wg_at(q, x, y); /* (only the widget under the pointer: never the focused one elsewhere) */
    ev_set(ev, -1, NULL, WHY_NONE, 0, x, y);
    if (i < 0 || i >= s->n || !dy)
        return false;
    mq_wg_t *g = &s->w[i];
    switch (g->kind) {
    case WG_WHEEL: /* scrolling up rolls the digit up */
        ev_set(ev, i, g, WHY_SCROLL, wrapv(g, g->value - dy), x, y);
        g->value = ev->value;
        return true;
    case WG_SLIDER: case WG_NUMLINE: case WG_CLOCKHAND:
        ev_set(ev, i, g, WHY_SCROLL, g->kind == WG_CLOCKHAND ? wrapv(g, g->value + dy * g->step) : clampv(g, g->value + dy * g->step),
               x, y);
        return true;
    case WG_AREA: ev_set(ev, i, g, WHY_SCROLL, dy, x, y); return true;
    default: return false;
    }
}

/* the focusable widget nearest the focused one in a direction (dir as the swipes) */
static int focus_step(mq_t *q, int dir)
{
    mq_wgs_t *s = &q->wg;
    int f = s->focus;
    if (f < 0 || f >= s->n) {
        for (int i = 0; i < s->n; i++)
            if (focusable(&s->w[i]))
                return i;
        return -1;
    }
    const mq_wg_t *a = &s->w[f];
    int ax = a->x + a->w / 2, ay = a->y + a->h / 2, best = -1;
    int32_t bd = 0x7FFFFFFF;
    for (int i = 0; i < s->n; i++) {
        const mq_wg_t *g = &s->w[i];
        if (i == f || !focusable(g))
            continue;
        int dx = g->x + g->w / 2 - ax, dy = g->y + g->h / 2 - ay;
        bool ok = dir == 0 ? dx < -2 : dir == 1 ? dx > 2 : dir == 2 ? dy < -2 : dy > 2;
        if (!ok)
            continue;
        int32_t d = dir < 2 ? iabs(dx) + 3 * iabs(dy) : iabs(dy) + 3 * iabs(dx);
        if (d < bd)
            bd = d, best = i;
    }
    return best;
}

/* A key (HK_*; c the character it types). True when a widget took it. */
bool mq_wg_key(mq_t *q, int k, int c, mq_wev_t *ev)
{
    mq_wgs_t *s = &q->wg;
    ev_set(ev, -1, NULL, WHY_NONE, 0, 0, 0);
    if (c) /* a key of a pad with this character */
        for (int i = 0; i < s->n; i++) {
            mq_wg_t *g = &s->w[i];
            if (g->kind == WG_KEY && g->label[0] == c && !g->label[1] && !(g->flags & WF_DISABLED)) {
                s->flash = (uint8_t)i, s->flash_t = mq_mark(q);
                ev_set(ev, i, g, WHY_KEY, g->value, g->x + g->w / 2, g->y + g->h / 2);
                return true;
            }
        }
    int f = s->focus;
    mq_wg_t *g = f >= 0 && f < s->n ? &s->w[f] : NULL;
    int dir = k == HK_LEFT ? 0 : k == HK_RIGHT ? 1 : k == HK_UP ? 2 : k == HK_DOWN ? 3 : -1;
    if (g) {
        bool vert = g->flags & WF_VERT;
        switch (g->kind) {
        case WG_WHEEL:
            if (k == HK_UP || k == HK_DOWN) {
                ev_set(ev, f, g, WHY_KEY, wrapv(g, g->value + (k == HK_UP ? 1 : -1)), 0, 0);
                g->value = ev->value;
                return true;
            }
            if (c >= '0' && c <= '9') { /* the digit, and on to the next wheel */
                ev_set(ev, f, g, WHY_KEY, c - '0', 0, 0);
                g->value = ev->value;
                int n = focus_step(q, 1);
                if (n >= 0 && s->w[n].kind == WG_WHEEL)
                    s->focus = (int8_t)n;
                s->kbd = true;
                return true;
            }
            if (k == HK_DEL) {
                ev_set(ev, f, g, WHY_KEY, g->lo, 0, 0);
                return true;
            }
            break;
        case WG_SLIDER: case WG_NUMLINE: case WG_STEER: case WG_CLOCKHAND:
            if ((!vert && (k == HK_LEFT || k == HK_RIGHT)) || (vert && (k == HK_UP || k == HK_DOWN)) ||
                ((g->flags & WF_DIAL) && dir >= 0)) {
                int d = k == HK_RIGHT || k == HK_DOWN ? 1 : -1;
                if (g->flags & WF_DIAL)
                    d = k == HK_LEFT || k == HK_UP ? 1 : -1;
                int v = g->value + d * g->step;
                ev_set(ev, f, g, WHY_KEY, g->kind == WG_CLOCKHAND ? wrapv(g, v) : clampv(g, v), 0, 0);
                s->kbd = true;
                return true;
            }
            break;
        case WG_AREA:
            if (dir >= 0 || k == HK_ENTER) {
                ev_set(ev, f, g, WHY_KEY, k, 0, 0);
                return true;
            }
            break;
        default: break;
        }
    }
    if (dir >= 0) {
        if (s->picked >= 0 || s->carry >= 0) { /* carrying: the arrows choose where it goes */
            int best = -1;
            for (int i = 0; i < s->n; i++)
                if (s->w[i].kind == WG_DROP && (best < 0 || i > s->hover))
                    if ((dir == 1 || dir == 3) ? i > s->hover : i < s->hover || s->hover < 0)
                        if (best < 0 || ((dir == 1 || dir == 3) ? i < best : i > best))
                            best = i;
            if (best >= 0)
                s->hover = (int8_t)best;
            s->kbd = true;
            return true;
        }
        int n = focus_step(q, dir);
        if (n >= 0)
            s->focus = (int8_t)n, s->kbd = true;
        return n >= 0;
    }
    if (k == HK_ENTER) {
        if (s->picked >= 0 && s->picked < s->n && s->hover >= 0) { /* drop it there */
            ev_set(ev, s->picked, &s->w[s->picked], WHY_DROP, s->w[s->hover].arg, 0, 0);
            ev->target = s->hover;
            s->picked = -1, s->hover = -1;
            return true;
        }
        if (!g)
            return false;
        s->flash = (uint8_t)f, s->flash_t = mq_mark(q);
        if (g->kind == WG_DRAG) {
            s->picked = (int8_t)f, s->hover = -1;
            ev_set(ev, f, g, WHY_PRESS, 0, 0, 0);
            return true;
        }
        if (g->kind == WG_BUTTON || g->kind == WG_CHOICE || g->kind == WG_KEY || g->kind == WG_DROP) {
            ev_set(ev, f, g, WHY_KEY, g->kind == WG_DROP ? g->arg : g->value, g->x + g->w / 2, g->y + g->h / 2);
            return true;
        }
    }
    return false;
}

int mq_wg_focused(mq_t *q) { return q->wg.focus; }
void mq_wg_focus(mq_t *q, int i) { q->wg.focus = (int8_t)(i >= 0 && i < q->wg.n ? i : -1); }
bool mq_wg_pressed(mq_t *q, int i)
{
    const mq_wgs_t *s = &q->wg;
    return (s->press == i && s->press >= 0) || (s->flash == i && s->flash_t && q->anim_t - s->flash_t < 160);
}
/* a thing carried now: where to draw it (lifted, under the finger) */
bool mq_wg_carrying(mq_t *q, int *idx, int *x, int *y)
{
    const mq_wgs_t *s = &q->wg;
    if (s->carry < 0 || s->carry >= s->n || !s->moved)
        return false;
    *idx = s->carry, *x = s->px, *y = s->py;
    return true;
}
int mq_wg_hover(mq_t *q) { return q->wg.carry >= 0 || q->wg.picked >= 0 ? q->wg.hover : -1; }
int mq_wg_picked(mq_t *q) { return q->wg.picked; }

/* ------------------------------------------------------------------ drawing */
static void arrow(mq_t *q, int cx, int cy, int s, bool up, uint16_t c)
{
    int16_t px[3] = {(int16_t)(cx - s), (int16_t)(cx + s), (int16_t)cx};
    int16_t py[3] = {(int16_t)(up ? cy + s / 2 : cy - s / 2), (int16_t)(up ? cy + s / 2 : cy - s / 2),
                     (int16_t)(up ? cy - s / 2 : cy + s / 2)};
    mq_poly(q, px, py, 3, c);
}

/* the widgets the game does not draw itself, then everyone's feedback: pressed, the focus ring, the glow
 * of a drop target under a carried thing */
void mq_wg_draw(mq_t *q)
{
    mq_wgs_t *s = &q->wg;
    const modx_api_t *api = q->api;
    for (int i = 0; i < s->n; i++) {
        const mq_wg_t *g = &s->w[i];
        bool down = mq_wg_pressed(q, i), dis = g->flags & WF_DISABLED;
        if (!(g->flags & WF_CUSTOM)) {
            uint16_t col = g->col ? g->col : Q_MINT;
            switch (g->kind) {
            case WG_BUTTON: case WG_KEY: case WG_CHOICE: {
                int dy = down ? 2 : 0;
                mq_rrect(q, g->x, g->y + 2, g->w, g->h, 8, Q_DIM);
                mq_rrect(q, g->x, g->y + dy, g->w, g->h - (down ? 1 : 0), 8, dis ? Q_GREY : down ? Q_YELLOW : col);
                const font_t *f = g->h >= 30 && api->text_width(api->font_medium, g->label) < g->w - 6 ? api->font_medium
                                                                                                           : api->font_small;
                mq_text_c(q, f, g->x + g->w / 2, g->y + dy + (g->h - (f == api->font_medium ? 16 : 12)) / 2, g->label,
                          dis ? Q_DIM : Q_INK);
                break;
            }
            case WG_WHEEL: { /* a drum: the arrows above and below, the digit in the middle, the ones beside */
                int cx = g->x + g->w / 2, my = g->y + g->h / 2;
                mq_rrect(q, g->x, g->y, g->w, g->h, 6, down ? Q_YELLOW : Q_CREAM);
                mq_rect(q, g->x, g->y, g->w, g->h, Q_DIM);
                arrow(q, cx, g->y + 8, 6, true, Q_HOTPINK);
                arrow(q, cx, g->y + g->h - 8, 6, false, Q_HOTPINK);
                char up[2] = {(char)('0' + wrapv(g, g->value + 1) % 10), 0}, dn[2] = {(char)('0' + wrapv(g, g->value - 1) % 10), 0};
                char v[2] = {(char)('0' + g->value % 10), 0};
                mq_text_c(q, api->font_small, cx, my - 22, up, Q_GREY);
                mq_text_c(q, api->font_small, cx, my + 10, dn, Q_GREY);
                mq_fill(q, g->x + 2, my - 11, g->w - 4, 22, Q_WHITE);
                mq_text_c(q, api->font_large, cx, my - 10, v, Q_INK);
                break;
            }
            case WG_SLIDER: case WG_NUMLINE: case WG_STEER: {
                if (g->flags & WF_DIAL)
                    break;
                bool vert = g->flags & WF_VERT;
                int span = imax(1, g->hi - g->lo), t = (g->value - g->lo) * (vert ? g->h : g->w) / span;
                int kx = vert ? g->x + g->w / 2 : g->x + t, ky = vert ? g->y + t : g->y + g->h / 2;
                if (g->kind == WG_STEER)
                    kx = vert ? kx : g->value;
                if (vert)
                    mq_fill(q, g->x + g->w / 2 - 2, g->y, 4, g->h, Q_GREY);
                else
                    mq_fill(q, g->x, g->y + g->h / 2 - 2, g->w, 4, Q_GREY);
                mq_circle(q, kx, ky, 10, down ? Q_YELLOW : col);
                mq_ring(q, kx, ky, 10, Q_INK);
                break;
            }
            case WG_DROP: mq_rrect(q, g->x, g->y, g->w, g->h, 8, col); break;
            default: break;
            }
        } else if (down && g->kind != WG_AREA && g->kind != WG_STEER && g->kind != WG_CLOCKHAND) /* the game's own: a pressed rim */
            mq_rect(q, g->x - 1, g->y - 1, g->w + 2, g->h + 2, Q_YELLOW);
        if (s->hover == i && (s->carry >= 0 || s->picked >= 0)) /* where the thing would go */
            for (int t = 0; t < 3; t++)
                mq_rect(q, g->x - 2 - t, g->y - 2 - t, g->w + 4 + 2 * t, g->h + 4 + 2 * t, Q_YELLOW);
        if (s->kbd && s->focus == i && !(g->flags & WF_NOFOCUS)) /* the focus ring */
            for (int t = 0; t < 2; t++)
                mq_rect(q, g->x - 3 - t, g->y - 3 - t, g->w + 6 + 2 * t, g->h + 6 + 2 * t, Q_PURPLE);
        if (s->picked == i) /* picked to be put somewhere: it bobs */
            mq_rect(q, g->x - 2, g->y - 2 - (int)(q->anim_t / 200 % 2), g->w + 4, g->h + 4, Q_HOTPINK);
    }
}

/* ------------------------------------------------------------------ the first-time demo */
/* The ghost hand doing widget i's gesture, el ms into it (a 1.6 s loop) */
void mq_wg_demo(mq_t *q, int i, int el)
{
    const mq_wg_t *g = mq_wg(q, i);
    if (!g)
        return;
    int ph = el % 1600, cx = g->x + g->w / 2, cy = g->y + g->h / 2, x0 = cx, y0 = cy, x1 = cx, y1 = cy;
    bool drag = true;
    switch (g->kind) {
    case WG_WHEEL: x0 = x1 = g->x + g->w - 5, y0 = cy + 14, y1 = cy - 18; break; /* push it up (at its edge: the digits show) */
    case WG_SLIDER: case WG_NUMLINE:
        if (g->flags & WF_DIAL) { /* round the outside of the arc, 45 to 105 degrees: the degrees inside stay in sight */
            int ro = g->h + 4, by = g->y + g->h;
            x0 = cx + ro * 7 / 10, y0 = by - ro * 7 / 10, x1 = cx - ro * 26 / 100, y1 = by - ro * 97 / 100;
        } else if (g->flags & WF_VERT)
            y0 = g->y + 4, y1 = g->y + g->h * 2 / 3;
        else
            x0 = g->x + 6, x1 = g->x + g->w * 2 / 3;
        break;
    case WG_STEER:
        if (g->flags & WF_VERT)
            y0 = g->y + g->h / 4, y1 = g->y + g->h * 3 / 4;
        else
            x0 = g->x + g->w / 4, x1 = g->x + g->w * 3 / 4, y0 = y1 = g->y + g->h - 16;
        break;
    case WG_DRAG: { /* to the first place it can go, held at its lower corner (its words and numbers stay in sight: the
                     * render audit's 'the hand covers 279') */
        x0 = g->x + g->w - 14, y0 = g->y + g->h - 10;
        for (int k = 0; k < q->wg.n; k++)
            if (q->wg.w[k].kind == WG_DROP) {
                x1 = q->wg.w[k].x + q->wg.w[k].w - 12, y1 = q->wg.w[k].y - 10; /* (at its lid: its label shows) */
                break;
            }
        break;
    }
    case WG_CLOCKHAND: x0 = cx, y0 = g->y + 8, x1 = g->x + g->w - 8, y1 = cy; break; /* round to 3 */
    case WG_AREA: x0 = g->x + g->w / 3, y0 = g->y + g->h / 3, x1 = g->x + g->w * 2 / 3, y1 = g->y + g->h * 2 / 3; break;
    default: drag = false, x0 = g->x + g->w - 7, y0 = g->y + g->h - 5; break; /* a tap (low at its right edge: a centred label or sign shows) */
    }
    int x = x0, y = y0, press = 0;
    if (drag) {
        int t = ph < 300 ? 0 : ph > 1300 ? 1000 : ph - 300;
        x = x0 + (x1 - x0) * t / 1000, y = y0 + (y1 - y0) * t / 1000;
        press = ph > 200 && ph < 1400 ? 16 : 0;
    } else {
        press = ph > 500 && ph < 800 ? 16 : 0;
        if (ph > 600 && ph < 1200)
            mq_ring(q, x, y, 4 + (ph - 600) / 75, Q_WHITE);
    }
    mq_hand(q, x, y, press);
}

/* the console and the tests: the set as text */
int mq_wg_console(mq_t *q)
{
    const mq_wgs_t *s = &q->wg;
    static const char kn[WG_COUNT][10] = {"button", "choice", "wheel", "slider", "numline", "drag", "drop", "key", "clockhand",
                                          "steer", "area"};
    for (int i = 0; i < s->n; i++) {
        const mq_wg_t *g = &s->w[i];
        int hx, hy, hw, hh;
        mq_wg_hitbox(g, &hx, &hy, &hw, &hh);
        int x = g->x, y = g->y;
        if (q->scene == SC_PLAY) /* (where it is on the screen: a twisted field is drawn mirrored or flipped) */
            mq_game_screen_box(q, &x, &y, g->w, g->h);
        q->api->con_printf("%d %s id %d arg %d at %d,%d %dx%d (hit %dx%d) value %d %s%s\n", i, kn[g->kind % WG_COUNT], g->id,
                           g->arg, x, y, g->w, g->h, hw, hh, g->value, g->label, s->focus == i ? " [focus]" : "");
    }
    return 0;
}
