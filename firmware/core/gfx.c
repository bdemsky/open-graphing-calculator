/*
 * Framebuffer drawing, dirty rectangles and the on-screen text log (gfx.h).
 *
 * Everything here is plain CPU code writing a uint16_t array; there is no
 * DMA2D/LTDC involvement, so no cache maintenance is needed on this side (the
 * hardware flush reads the framebuffer with the CPU as well). All state is
 * module-static and main-context only.
 */
#include "gfx.h"
#include <stdlib.h>
#include <string.h>

static uint16_t *fb; /* LCD_WIDTH * LCD_HEIGHT RGB565 pixels, row-major, stride LCD_WIDTH */
static int clip_x0, clip_y0, clip_x1 = LCD_WIDTH, clip_y1 = LCD_HEIGHT; /* [x0,x1) */
static int base_y1 = LCD_HEIGHT; /* gfx_reset_clip() stops here (gfx_set_base_clip) */
static gfx_stats_t stats;

/* ------------------------------------------------------------ dirty rects */
/* The flush cost on hardware is roughly a fixed per-rectangle overhead (window
 * commands) plus the pixel count over the 16-bit bus, so the list trades a few
 * extra pixels for fewer rectangles: adjacent or overlapping rectangles merge
 * when the bounding box wastes at most 2048 px (~2 % of the screen), and the
 * list is capped at MAX_DIRTY entries. */
#define MAX_DIRTY 16
typedef struct { int x0, y0, x1, y1; } rect_t; /* half-open: [x0,x1) x [y0,y1) */
static rect_t dirty[MAX_DIRTY];
static int ndirty;

static int area(const rect_t *r) { return (r->x1 - r->x0) * (r->y1 - r->y0); }

static rect_t runion(rect_t a, rect_t b)
{
    rect_t r = {a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0,
                a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
    return r;
}

/* Overlapping or sharing an edge (the <= lets abutting rectangles merge). */
static bool touches(const rect_t *a, const rect_t *b)
{
    return a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1;
}

/* Add (x, y, w, h), clamped to the screen, to the dirty list. Empty or
 * off-screen rectangles are ignored. */
void (*gfx_dirty_hook)(int x, int y, int w, int h);
bool (*gfx_text_hook)(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg);
static int dirty_hook_off; /* text a gfx_text_hook took over */

static uint16_t *main_fb; /* the display's framebuffer while gfx_target() draws elsewhere */

void gfx_mark_dirty(int x, int y, int w, int h)
{
    if (main_fb)
        return; /* (drawing offscreen) */
    rect_t r = {x, y, x + w, y + h};
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > LCD_WIDTH) r.x1 = LCD_WIDTH;
    if (r.y1 > LCD_HEIGHT) r.y1 = LCD_HEIGHT;
    if (r.x0 >= r.x1 || r.y0 >= r.y1)
        return;
    if (gfx_dirty_hook && !dirty_hook_off)
        gfx_dirty_hook(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
    /* merge with any rectangle it touches (repeat: merging can cascade). A
     * merged entry is removed (swap with the last) and the grown r is
     * re-checked against the remaining entries. */
    for (;;) {
        int i;
        for (i = 0; i < ndirty; i++) {
            if (touches(&dirty[i], &r)) {
                rect_t u = runion(dirty[i], r);
                /* only merge if it does not waste much */
                if (area(&u) <= area(&dirty[i]) + area(&r) + 2048) {
                    r = u;
                    dirty[i] = dirty[--ndirty];
                    break;
                }
            }
        }
        if (i == ndirty)
            break;
    }
    if (ndirty == MAX_DIRTY) {
        /* merge into the rectangle whose union grows least (no cascade here:
         * the list may then hold overlapping entries, which only costs a few
         * pixels sent twice) */
        int best = 0, best_cost = 1 << 30;
        for (int i = 0; i < ndirty; i++) {
            rect_t u = runion(dirty[i], r);
            int cost = area(&u) - area(&dirty[i]);
            if (cost < best_cost)
                best_cost = cost, best = i;
        }
        dirty[best] = runion(dirty[best], r);
        return;
    }
    dirty[ndirty++] = r;
}

void gfx_invalidate(void)
{
    ndirty = 1;
    dirty[0] = (rect_t){0, 0, LCD_WIDTH, LCD_HEIGHT};
}

int gfx_flush(void)
{
    int px = 0;
    for (int i = 0; i < ndirty; i++) {
        rect_t *r = &dirty[i];
        hal_display_flush(fb, r->x0, r->y0, r->x1 - r->x0, r->y1 - r->y0);
        px += area(r);
        stats.rects++;
    }
    if (ndirty)
        stats.flushes++;
    stats.pixels += px;
    ndirty = 0;
    return px;
}

void gfx_get_stats(gfx_stats_t *s) { *s = stats; }

/* ------------------------------------------------------------ text log */
static gfx_textrun_t runs[GFX_TEXTLOG_MAX]; /* sorted by (y, x) */
static int nruns;
static uint32_t run_seq; /* age stamp for the next run */
static bool textlog_on = true;

void gfx_textlog_enable(bool on) { textlog_on = on; }

/* Remove every run whose rectangle intersects (x, y, w, h), keeping order.
 * Not affected by gfx_textlog_enable: painting over text always forgets it. */
static void textlog_erase(int x, int y, int w, int h)
{
    if (main_fb)
        return; /* (drawing offscreen: the screen's text is still there) */
    for (int i = 0; i < nruns;) {
        gfx_textrun_t *t = &runs[i];
        if (t->x < x + w && x < t->x + t->w && t->y < y + h && y < t->y + t->h) {
            memmove(&runs[i], &runs[i + 1], (size_t)(nruns - i - 1) * sizeof runs[0]);
            nruns--;
        } else {
            i++;
        }
    }
}

/* Record a run, replacing any run it overlaps (new text drawn over old text,
 * even with a transparent background, is taken to supersede it). */
static void textlog_add(int x, int y, int w, int h, const char *s)
{
    if (!textlog_on || w <= 0 || main_fb)
        return;
    textlog_erase(x, y, w, h);
    if (nruns == GFX_TEXTLOG_MAX) { /* full: drop the oldest run */
        int o = 0;
        for (int k = 1; k < nruns; k++)
            if (run_seq - runs[k].seq > run_seq - runs[o].seq) /* wrap-safe age */
                o = k;
        memmove(&runs[o], &runs[o + 1], (size_t)(nruns - o - 1) * sizeof runs[0]);
        nruns--;
    }
    /* keep sorted by (y, x): insertion step, shifting larger entries up */
    int i = nruns;
    while (i > 0 && (runs[i - 1].y > y || (runs[i - 1].y == y && runs[i - 1].x > x))) {
        runs[i] = runs[i - 1];
        i--;
    }
    gfx_textrun_t *t = &runs[i];
    t->x = (int16_t)x;
    t->y = (int16_t)y;
    t->w = (int16_t)w;
    t->h = (int16_t)h;
    t->seq = run_seq++;
    strncpy(t->text, s, GFX_TEXTLOG_LEN - 1);
    t->text[GFX_TEXTLOG_LEN - 1] = 0;
    nruns++;
}

void gfx_textlog_note(int x, int y, int w, int h, const char *s) { textlog_add(x, y, w, h, s); }

int gfx_textlog_count(void) { return nruns; }
const gfx_textrun_t *gfx_textlog_get(int i) { return (i >= 0 && i < nruns) ? &runs[i] : NULL; }

bool gfx_textlog_contains(const char *needle)
{
    for (int i = 0; i < nruns; i++)
        if (strstr(runs[i].text, needle))
            return true;
    return false;
}

/* ------------------------------------------------------------ drawing */
/* (See gfx.h.) Note gfx_reset_clip here uses the current base clip, which a
 * previous gfx_set_base_clip may have lowered. */
void gfx_init(uint16_t *framebuffer)
{
    fb = framebuffer;
    gfx_reset_clip();
    nruns = 0;
    gfx_invalidate();
}

uint16_t *gfx_framebuffer(void) { return fb; }

void gfx_target(uint16_t *buf)
{
    if (buf && !main_fb) {
        main_fb = fb;
        fb = buf;
    } else if (!buf && main_fb) {
        fb = main_fb;
        main_fb = NULL;
    }
}

void gfx_set_clip(int x, int y, int w, int h)
{
    clip_x0 = x < 0 ? 0 : x;
    clip_y0 = y < 0 ? 0 : y;
    clip_x1 = x + w > LCD_WIDTH ? LCD_WIDTH : x + w;
    clip_y1 = y + h > LCD_HEIGHT ? LCD_HEIGHT : y + h;
}

void gfx_reset_clip(void) { gfx_set_clip(0, 0, LCD_WIDTH, base_y1); }

void gfx_get_clip(int r[4]) { r[0] = clip_x0, r[1] = clip_y0, r[2] = clip_x1, r[3] = clip_y1; }

int gfx_base_clip(void) { return base_y1; }

void gfx_set_base_clip(int y1)
{
    base_y1 = y1 > LCD_HEIGHT ? LCD_HEIGHT : y1;
    gfx_reset_clip();
}

gfx_color_map_fn gfx_color_map; /* NULL = light palette as-is */

/* Map colour variable c in place through the theme hook (FILL role). Each
 * primitive applies it exactly once, so wrappers (gfx_rect, gfx_line's
 * horizontal/vertical cases) must not map before delegating. */
#define MAP_FILL(c) do { if (gfx_color_map) (c) = gfx_color_map((c), GFX_ROLE_FILL, 0); } while (0)

uint16_t gfx_map(uint16_t c)
{
    MAP_FILL(c);
    return c;
}

void gfx_pixel(int x, int y, uint16_t c)
{
    MAP_FILL(c);
    if (x < clip_x0 || x >= clip_x1 || y < clip_y0 || y >= clip_y1)
        return;
    fb[y * LCD_WIDTH + x] = c;
    gfx_mark_dirty(x, y, 1, 1);
}

uint16_t gfx_get_pixel(int x, int y)
{
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT)
        return 0;
    return fb[y * LCD_WIDTH + x];
}

/* Intersect (x, y, w, h) with the clip rectangle in place. Returns false if
 * nothing is left to draw. */
static bool clip_rect(int *x, int *y, int *w, int *h)
{
    int x0 = *x, y0 = *y, x1 = *x + *w, y1 = *y + *h;
    if (x0 < clip_x0) x0 = clip_x0;
    if (y0 < clip_y0) y0 = clip_y0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (y1 > clip_y1) y1 = clip_y1;
    if (x0 >= x1 || y0 >= y1)
        return false;
    *x = x0, *y = y0, *w = x1 - x0, *h = y1 - y0;
    return true;
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    MAP_FILL(c);
    if (!clip_rect(&x, &y, &w, &h))
        return;
    for (int j = 0; j < h; j++) {
        uint16_t *p = fb + (y + j) * LCD_WIDTH + x;
        for (int i = 0; i < w; i++)
            p[i] = c;
    }
    textlog_erase(x, y, w, h);
    gfx_mark_dirty(x, y, w, h);
}

void gfx_clear(uint16_t c)
{
    gfx_set_clip(0, 0, LCD_WIDTH, LCD_HEIGHT);
    gfx_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, c);
    nruns = 0;
    gfx_reset_clip();
}

void gfx_hline(int x, int y, int w, uint16_t c)
{
    MAP_FILL(c);
    /* height 1 via a compound literal: clip_rect needs an lvalue to write */
    if (!clip_rect(&x, &y, &w, &(int){1}))
        return;
    uint16_t *p = fb + y * LCD_WIDTH + x;
    for (int i = 0; i < w; i++)
        p[i] = c;
    gfx_mark_dirty(x, y, w, 1);
}

void gfx_vline(int x, int y, int h, uint16_t c)
{
    MAP_FILL(c);
    int w = 1;
    if (!clip_rect(&x, &y, &w, &h))
        return;
    for (int j = 0; j < h; j++)
        fb[(y + j) * LCD_WIDTH + x] = c;
    gfx_mark_dirty(x, y, 1, h);
}

void gfx_rect(int x, int y, int w, int h, uint16_t c)
{
    gfx_hline(x, y, w, c);
    gfx_hline(x, y + h - 1, w, c);
    gfx_vline(x, y, h, c);
    gfx_vline(x + w - 1, y, h, c);
}

void gfx_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    if (x0 == x1) { /* (hline/vline map the colour) */
        gfx_vline(x0, y0 < y1 ? y0 : y1, abs(y1 - y0) + 1, c);
        return;
    }
    if (y0 == y1) {
        gfx_hline(x0 < x1 ? x0 : x1, y0, abs(x1 - x0) + 1, c);
        return;
    }
    /* Reject lines entirely outside the clip box (cheap; avoids long loops). */
    if ((x0 < clip_x0 && x1 < clip_x0) || (x0 >= clip_x1 && x1 >= clip_x1) ||
        (y0 < clip_y0 && y1 < clip_y0) || (y0 >= clip_y1 && y1 >= clip_y1))
        return;
    MAP_FILL(c);
    /* A line of 4096 steps or more (far off-screen endpoints) is first cut to
     * the clip box plus a 1-pixel margin (Liang-Barsky, endpoints rounded), so
     * the loop below stays short; shorter lines are drawn exactly as given. */
    if (abs(x1 - x0) >= 4096 || abs(y1 - y0) >= 4096) {
        double t0 = 0, t1 = 1, fx = x0, fy = y0, ddx = (double)x1 - x0, ddy = (double)y1 - y0;
        double p[4] = {-ddx, ddx, -ddy, ddy};
        double q[4] = {fx - (clip_x0 - 1), clip_x1 - fx, fy - (clip_y0 - 1), clip_y1 - fy};
        for (int k = 0; k < 4; k++) {
            if (p[k] == 0) {
                if (q[k] < 0)
                    return;
            } else {
                double t = q[k] / p[k];
                if (p[k] < 0 && t > t0)
                    t0 = t;
                else if (p[k] > 0 && t < t1)
                    t1 = t;
            }
        }
        if (t0 > t1)
            return;
        /* round half up; a cut point in the -1 margin may land on 0 instead,
         * which only moves an end pixel by one */
        x1 = (int)(fx + t1 * ddx + 0.5), y1 = (int)(fy + t1 * ddy + 0.5);
        x0 = (int)(fx + t0 * ddx + 0.5), y0 = (int)(fy + t0 * ddy + 0.5);
    }
    /* Integer Bresenham (all-octant form with err = dx - |dy|): each step moves
     * x, y or both, and pixels are clipped individually. */
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int minx = x0 < x1 ? x0 : x1, miny = y0 < y1 ? y0 : y1;
    for (;;) {
        if (x0 >= clip_x0 && x0 < clip_x1 && y0 >= clip_y0 && y0 < clip_y1)
            fb[y0 * LCD_WIDTH + x0] = c;
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
    /* bounding box of the whole line (gfx_mark_dirty clamps it to the screen) */
    gfx_mark_dirty(minx, miny, dx + 1, -dy + 1);
}

void gfx_save_rect(uint16_t *dst, int x, int y, int w, int h)
{
    /* reading is clipped to the base clip, not the caller's current clip;
     * the caller's clip is restored afterwards */
    int cx0 = clip_x0, cy0 = clip_y0, cx1 = clip_x1, cy1 = clip_y1;
    gfx_reset_clip();
    if (clip_rect(&x, &y, &w, &h))
        for (int j = 0; j < h; j++)
            memcpy(dst + (y + j) * LCD_WIDTH + x, fb + (y + j) * LCD_WIDTH + x, (size_t)w * 2);
    clip_x0 = cx0, clip_y0 = cy0, clip_x1 = cx1, clip_y1 = cy1;
}

void gfx_restore_rect(const uint16_t *src, int x, int y, int w, int h)
{
    if (!clip_rect(&x, &y, &w, &h))
        return;
    if (y + h > base_y1) /* never below the base clip: gfx_save_rect didn't read there */
        h = base_y1 - y;
    if (h <= 0)
        return;
    for (int j = 0; j < h; j++)
        memcpy(fb + (y + j) * LCD_WIDTH + x, src + (y + j) * LCD_WIDTH + x, (size_t)w * 2);
    gfx_mark_dirty(x, y, w, h);
}

/* ------------------------------------------------------------ text */
/* Blit one glyph cell with per-pixel clipping. fg/bg are already mapped; set
 * bits get fg, clear bits get bg unless bg is GFX_TRANSPARENT (< 0). Does not
 * mark dirty (callers mark the whole string at once). */
static void draw_glyph(const font_t *f, int x, int y, uint32_t cp, uint16_t fg, int bg)
{
    const uint8_t *g = font_glyph(f, cp);
    for (int r = 0; r < f->h; r++) {
        int yy = y + r;
        if (yy < clip_y0 || yy >= clip_y1)
            continue;
        const uint8_t *row = g + r * f->bytes_per_row;
        uint16_t *p = fb + yy * LCD_WIDTH;
        for (int c = 0; c < f->w; c++) {
            int xx = x + c;
            if (xx < clip_x0 || xx >= clip_x1)
                continue;
            if (row[c >> 3] & (0x80 >> (c & 7)))
                p[xx] = fg;
            else if (bg >= 0)
                p[xx] = (uint16_t)bg;
        }
    }
}

/* Approximate luma of an RGB565 colour: R5*16 + G6*20 + B5*8, i.e. about
 * 0.25 R + 0.63 G + 0.12 B. White is 2004, so 8 units ~ one step of 8-bit luma
 * and the threshold 140 * 8 below means "luma above ~140/255". */
static int luma(uint16_t c) { return ((c >> 11) & 31) * 2 * 8 + ((c >> 5) & 63) * 5 * 4 + (c & 31) * 8; }

/* theme: map the text colours (ink chosen against the background under it:
 * the majority of the four corners of the first character cell). The corners
 * are read from the framebuffer, i.e. already-mapped colours. Each corner is
 * classed light or dark; light wins with 3 or 4 light corners (a 2-2 tie
 * counts as dark), and `under` is the last corner of the winning class. The
 * same ink is then used for the whole string. */
static void map_text(const font_t *f, int x, int y, uint16_t *fg, int *bg)
{
    if (!gfx_color_map)
        return;
    uint16_t under;
    if (*bg >= 0) {
        under = (uint16_t)(*bg = gfx_color_map((uint16_t)*bg, GFX_ROLE_FILL, 0));
    } else {
        uint16_t c[4] = {gfx_get_pixel(x, y), gfx_get_pixel(x + f->w - 1, y), gfx_get_pixel(x, y + f->h - 1),
                         gfx_get_pixel(x + f->w - 1, y + f->h - 1)};
        int light = 0;
        for (int i = 0; i < 4; i++)
            light += luma(c[i]) >= 140 * 8;
        under = c[0];
        for (int i = 0; i < 4; i++)
            if ((luma(c[i]) >= 140 * 8) == (light >= 3))
                under = c[i];
    }
    *fg = gfx_color_map(*fg, GFX_ROLE_INK, under);
}

int gfx_char(const font_t *f, int x, int y, uint32_t cp, uint16_t fg, int bg)
{
    map_text(f, x, y, &fg, &bg);
    draw_glyph(f, x, y, cp, fg, bg);
    gfx_mark_dirty(x, y, f->w, f->h);
    return f->w;
}

int gfx_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    const char *p = s;
    int x0 = x;
    uint32_t cp;
    map_text(f, x, y, &fg, &bg);
    bool hooked = gfx_text_hook && gfx_text_hook(f, x, y, s, fg, bg);
    while ((cp = utf8_next(&p)) != 0) {
        draw_glyph(f, x, y, cp, fg, bg);
        x += f->w;
    }
    int w = x - x0;
    if (w > 0) {
        dirty_hook_off += hooked;
        gfx_mark_dirty(x0, y, w, f->h);
        dirty_hook_off -= hooked;
        /* the run is logged at its full (unclipped) extent, with the whole
         * string, even if part of it falls outside the clip */
        textlog_add(x0, y, w, f->h, s);
    }
    return w;
}

int gfx_text_width(const font_t *f, const char *s) { return utf8_len(s) * f->w; }
