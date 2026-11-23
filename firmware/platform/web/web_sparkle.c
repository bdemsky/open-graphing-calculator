/*
 * The standalone Sparkle Town page (web/page/sparkle_*, `make sparkle-web`): a high-resolution
 * display for the Math Quest module, and the lock that keeps the page inside the game.
 *
 * Built only with -DWEB_SPARKLE=1 (the sparkle build of the web platform; the calculator page
 * does not have it).
 *
 * Display (modx_set_display): the module draws in physical pixels at the display scale S the
 * page chose (web_sparkle_set_scale, before the module binds). Its calls become drawing commands
 * in a buffer that the page runs on its high-resolution canvas once a frame (web_sparkle_take):
 *   - rectangles and lines as they are;
 *   - what the module brackets with modx_api_t.hr (app/modx.h; modules/mathquest/mq_hr.c):
 *     shapes (anti-aliased circles, rings, rounded rectangles, polygons, thick lines), text by
 *     style (a web font) and pictures of indexed art, found by the hash of their content in the
 *     page's high-resolution art (web/assets/sparkle_art.*, tools/gen_mq_art_hires.py): the
 *     rectangles inside such a bracket are kept out of the canvas;
 *   - whatever the core draws itself (the status and soft-key bars, toasts, messages) is copied
 *     from the 1x framebuffer as pixels (gfx_dirty_hook), in drawing order.
 * Every module call is also drawn into the 1x framebuffer (divided by S), so the console, the
 * text log, the tests and screenshots see the usual 320 x 240 screen.
 *
 * Command words (int32): FILL x y w h c | LINE x0 y0 x1 y1 c | CLIP x0 y0 x1 y1 (physical) |
 * SHAPE n a[n] | TEXT n a[8] bytes (n bytes, padded to words; a[0]: the style, bits 4-5 the alignment) |
 * IMAGE hash a[12] (a[11]: the tint) |
 * COPY x y w h (1x) then w*h RGB565 (two a word) | SCENE a[4] |
 * SPRITE n hash a[1..n) (MODX_HR_SPRITE: the FNV-1a of its id, then a[1..] as the module gave them) |
 * SOFTBAR x y w h mask (1x: a part of a game's soft-key bar, drawn by the page as vector pills; mask:
 * the buttons there are).
 *
 * Sprites (MODX_HR_SPRITE): the module names a thing it draws from many shapes (a building, a
 * background, a bubble, a squishy); if the page has vector art for that id (web_sparkle_vec) the
 * shapes are kept out and the page draws its art; ids without art are counted and listed
 * (web_sparkle_sprite_misses: the page's coverage report).
 */
#if defined(WEB_SPARKLE)
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "gfx.h"
#include "modx.h"
#include "ui.h"
#include "web_api.h"

enum { C_FILL = 1, C_LINE, C_CLIP, C_SHAPE, C_TEXT, C_IMAGE, C_COPY, C_SCENE, C_SPRITE, C_SOFTBAR, C_KINDS };

static int S = 1;              /* the display scale */
static int32_t *cmd;           /* the command buffer */
static size_t ncmd, capcmd;
static int depth;              /* open hr brackets (drawn by the page: rectangles 1x only) */
static int text_style = -1;    /* the style of the open text bracket */
static int quiet;              /* our own 1x drawing: not a core rectangle */
static int clip_sent[4] = {-1, -1, -1, -1};
static uint32_t *art;          /* hashes of the pictures the page has (sorted) */
static size_t nart;
static bool locked;
static uint32_t stats[C_KINDS]; /* per command kind, since web_sparkle_stats */
static uint32_t img_miss;      /* pictures the page had no high-resolution art for */
static uint32_t *vec;          /* FNV-1a of the sprite ids the page has art for (sorted) */
static size_t nvec;
static uint32_t spr_miss;      /* sprites without art */
#define NMISS 64
static char miss_id[NMISS][32]; /* the ids of sprites without art (the first NMISS) */
static int nmiss;

static int32_t *reserve(size_t n) /* room for n more words (NULL: out of memory) */
{
    if (ncmd + n > capcmd) {
        size_t c = capcmd ? capcmd : 65536;
        while (c < ncmd + n)
            c *= 2;
        int32_t *p = realloc(cmd, c * sizeof *cmd);
        if (!p)
            return NULL;
        cmd = p, capcmd = c;
    }
    return cmd + ncmd;
}

static void put(const int32_t *w, size_t n)
{
    int32_t *d = reserve(n);
    if (d)
        memcpy(d, w, n * sizeof *w), ncmd += n;
}

/* ---- the core's own drawing: rectangles of the 1x framebuffer, copied in order ---- */
#define NPEND 24
static int pend[NPEND][4]; /* x0 y0 x1 y1 */
static int npend;

/* a game's soft-key bar: the page draws it as vector pills (the words come as text) */
static bool softbar(const uint16_t *fb, int x, int y, int w, int h)
{
    const screen_t *c = ui_current();
    if (!c || c->style != SCREEN_GAME || y < SOFT_Y || y + h > LCD_HEIGHT || w <= 0 || h <= 0)
        return false;
    int mask = 0;
    for (int i = 0; i < 5; i++) /* a button: its candy colour, not the bar's, in the middle of its cell */
        if (fb[(SOFT_Y + 6) * LCD_WIDTH + i * (LCD_WIDTH / 5) + 4] != GAME_BAR_BG)
            mask |= 1 << i;
    int32_t *d = reserve(6);
    if (!d)
        return true;
    d[0] = C_SOFTBAR, d[1] = x, d[2] = y, d[3] = w, d[4] = h, d[5] = mask;
    ncmd += 6;
    stats[C_SOFTBAR]++;
    return true;
}

static void pend_flush(void)
{
    const uint16_t *fb = gfx_framebuffer();
    for (int i = 0; i < npend; i++) {
        int x = pend[i][0], y = pend[i][1], w = pend[i][2] - x, h = pend[i][3] - y;
        if (softbar(fb, x, y, w, h))
            continue;
        size_t nw = ((size_t)w * h + 1) / 2;
        int32_t *d = reserve(5 + nw);
        if (!d)
            break;
        d[0] = C_COPY, d[1] = x, d[2] = y, d[3] = w, d[4] = h;
        uint16_t *t16 = (uint16_t *)(d + 5);
        for (int j = 0; j < h; j++)
            memcpy(t16 + (size_t)j * w, fb + (size_t)(y + j) * LCD_WIDTH + x, (size_t)w * 2);
        if ((size_t)w * h % 2)
            t16[(size_t)w * h] = 0;
        ncmd += 5 + nw;
        stats[C_COPY]++;
    }
    npend = 0;
}

static void dirty_hook(int x, int y, int w, int h)
{
    if (quiet)
        return;
    int r[4] = {x, y, x + w, y + h};
    for (int i = 0; i < npend; i++) /* inside or touching one: grow it */
        if (r[0] <= pend[i][2] && pend[i][0] <= r[2] && r[1] <= pend[i][3] && pend[i][1] <= r[3]) {
            pend[i][0] = r[0] < pend[i][0] ? r[0] : pend[i][0], pend[i][1] = r[1] < pend[i][1] ? r[1] : pend[i][1];
            pend[i][2] = r[2] > pend[i][2] ? r[2] : pend[i][2], pend[i][3] = r[3] > pend[i][3] ? r[3] : pend[i][3];
            return;
        }
    if (npend == NPEND) /* full: copy what is there (the order is kept) */
        pend_flush();
    memcpy(pend[npend++], r, sizeof r);
}

/* before every module command: the core's pending rectangles, then the clip if it changed */
static void before(void)
{
    if (npend)
        pend_flush();
    int c[4];
    gfx_get_clip(c);
    if (memcmp(c, clip_sent, sizeof c)) {
        memcpy(clip_sent, c, sizeof c);
        int32_t w[5] = {C_CLIP, c[0] * S, c[1] * S, c[2] * S, c[3] * S};
        put(w, 5);
    }
}

static int div_floor(int v) { return v >= 0 ? v / S : -((-v + S - 1) / S); }

/* ---- the display the module draws on ---- */
static void d_fill(int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0)
        return;
    if (!depth) {
        before();
        int32_t cm[6] = {C_FILL, x, y, w, h, c};
        put(cm, 6);
        stats[C_FILL]++;
    }
    int x0 = div_floor(x), y0 = div_floor(y), x1 = div_floor(x + w + S - 1), y1 = div_floor(y + h + S - 1);
    quiet++;
    gfx_fill_rect(x0, y0, x1 - x0, y1 - y0, c);
    quiet--;
}

static void d_rect(int x, int y, int w, int h, uint16_t c)
{
    d_fill(x, y, w, 1, c), d_fill(x, y + h - 1, w, 1, c), d_fill(x, y, 1, h, c), d_fill(x + w - 1, y, 1, h, c);
}

static void d_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    if (!depth) {
        before();
        int32_t cm[6] = {C_LINE, x0, y0, x1, y1, c};
        put(cm, 6);
        stats[C_LINE]++;
    }
    quiet++;
    gfx_line(div_floor(x0), div_floor(y0), div_floor(x1), div_floor(y1), c);
    quiet--;
}

static const font_t *style_font(int st)
{
    return st <= 0 ? &font_small : st == 1 ? &font_medium : st == 2 ? &font_large : &font_xlarge;
}

static void text_cmd(int st, int c, int clip0, int clip1, int x16, int y16, int w16, int h16, const char *s)
{
    int32_t n = (int32_t)strlen(s);
    int32_t head[10] = {C_TEXT, n, st, c, clip0, clip1, x16, y16, w16, h16};
    put(head, 10);
    size_t nw = (size_t)(n + 4) / 4;
    int32_t *b = calloc(nw, sizeof *b);
    if (b) {
        memcpy(b, s, (size_t)n);
        put(b, nw);
        free(b);
    } else {
        int32_t z[64] = {0};
        put(z, nw < 64 ? nw : 64);
    }
    stats[C_TEXT]++;
}

static int d_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    /* bracketed: the style the module named; else the style a font of that size stands for here */
    int st = text_style >= 0 ? text_style : (f == &font_xlarge || f == &font_medium) ? 1 : 0;
    const font_t *f1 = style_font(st);
    if (!depth) {
        before();
        int w16 = gfx_text_width(f1, s) * S * 16, h16 = f1->h * S * 16;
        if (bg >= 0) {
            int32_t cm[6] = {C_FILL, x, y, w16 / 16, h16 / 16, bg};
            put(cm, 6);
        }
        text_cmd(st | MODX_HR_ALIGN_CENTRE << 4, fg, 0, 0, x * 16, y * 16, w16, h16, s);
    }
    quiet++;
    int w = gfx_text(f1, div_floor(x), div_floor(y), s, fg, bg);
    quiet--;
    return w * S;
}

/* the core's own text (the soft-key bar, toasts, messages): the web font too, in drawing order */
static bool core_text(const font_t *f, int x, int y, const char *s, uint16_t fg, int bg)
{
    if (quiet || !*s)
        return false;
    int st = f == &font_small ? 0 : f == &font_medium ? 1 : f == &font_large ? 2 : f == &font_xlarge ? 3 : -1;
    if (st < 0)
        return false;
    before();
    int w = gfx_text_width(f, s) * S, h = f->h * S;
    if (bg >= 0) {
        int32_t cm[6] = {C_FILL, x * S, y * S, w, h, bg};
        put(cm, 6);
    }
    text_cmd(st | MODX_HR_ALIGN_CENTRE << 4, fg, 0, 0, x * S * 16, y * S * 16, w * 16, h * 16, s);
    return true;
}

/* FNV-1a of a picture's colours (RGB565 per pixel, 1 for transparent; w and h first): the key of
 * its high-resolution art (tools/gen_mq_art_hires.py computes the same) */
static uint32_t img_hash(const modx_hr_img_t *im, int w, int h)
{
    uint32_t hv = 2166136261u;
#define MIX(b) (hv = (hv ^ (uint8_t)(b)) * 16777619u)
    MIX(w), MIX(w >> 8), MIX(h), MIX(h >> 8);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int v = im->bpp == 4 ? (im->px[y * im->stride + x / 2] >> ((x & 1) * 4)) & 15 : im->px[y * im->stride + x];
            uint16_t c = v ? im->pal[v] : 1;
            MIX(c), MIX(c >> 8);
        }
#undef MIX
    return hv;
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* the page's font: the advance of each printable character (32..126) and of the cent sign, per style, at
 * 100 px x 16 (web_sparkle_metrics); 0: not given yet */
#define MET_N 96
static int32_t met[4][MET_N];
static bool met_on;
WEB_EXPORT void web_sparkle_metrics(const int32_t *p, int n)
{
    met_on = n >= 4 * MET_N;
    if (met_on)
        memcpy(met, p, sizeof met);
}
/* MODX_HR_MEASURE: the width of s in style st, in a box h logical pixels high, logical pixels x 16 (the page
 * draws a text at 0.78 of its box's height: sparkle_page.js _text); 0: a character the table lacks */
static int measure(int st, int h, const char *s)
{
    if (!met_on || h <= 0 || !s)
        return 0;
    st = st < 0 ? 0 : st > 3 ? 3 : st;
    int64_t sum = 0;
    for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
        if (*c >= 32 && *c < 127)
            sum += met[st][*c - 32];
        else if (c[0] == 0xC2 && c[1] == 0xA2) /* the cent sign */
            sum += met[st][MET_N - 1], c++;
        else
            return 0;
    }
    return (int)(sum * h * 78 / 10000);
}

static int d_hr(int op, const int32_t *a, int n, const void *p)
{
    switch (op) {
    case MODX_HR_MEASURE:
        return n >= 2 ? measure(MODX_HR_TEXT_STYLE(a[0]), a[1], p) : 0;

    case MODX_HR_END:
        if (depth > 0 && --depth == 0)
            text_style = -1;
        return 0;
    case MODX_HR_SHAPE:
        if (depth || n < 5)
            return depth ? (depth++, 1) : 0;
        before();
        {
            int32_t h[2] = {C_SHAPE, n};
            put(h, 2), put(a, (size_t)n);
        }
        stats[C_SHAPE]++;
        depth++;
        return 1;
    case MODX_HR_TEXT:
        if (depth || n < 8 || !p)
            return depth ? (depth++, 1) : 0;
        before();
        text_cmd(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], p); /* (the style word keeps its alignment bits) */
        text_style = MODX_HR_TEXT_STYLE(a[0]) > 3 ? 3 : MODX_HR_TEXT_STYLE(a[0]);
        depth++;
        return 1;
    case MODX_HR_IMAGE: {
        if (depth || n < 12 || !p)
            return depth ? (depth++, 1) : 0;
        const modx_hr_img_t *im = p;
        uint32_t hv = img_hash(im, a[7], a[8]);
        if (!art || !bsearch(&hv, art, nart, sizeof *art, cmp_u32)) {
            img_miss++;
            return 0; /* the pixels, as rectangles */
        }
        before();
        int32_t w[14] = {C_IMAGE, (int32_t)hv};
        memcpy(w + 2, a + 1, 11 * sizeof *w);
        w[13] = n >= 13 ? a[12] : 0; /* the tint */
        put(w, 14);
        stats[C_IMAGE]++;
        depth++;
        return 1;
    }
    case MODX_HR_SPRITE: {
        if (depth || n < 12 || !p)
            return depth ? (depth++, 1) : 0;
        const char *id = p;
        uint32_t hv = 2166136261u;
        for (const char *c = id; *c; c++)
            hv = (hv ^ (uint8_t)*c) * 16777619u;
        if (!vec || !bsearch(&hv, vec, nvec, sizeof *vec, cmp_u32)) {
            spr_miss++;
            bool known = false;
            for (int i = 0; i < nmiss && !known; i++)
                known = !strcmp(miss_id[i], id);
            if (!known && nmiss < NMISS) {
                strncpy(miss_id[nmiss], id, sizeof miss_id[0] - 1);
                miss_id[nmiss++][sizeof miss_id[0] - 1] = 0;
            }
            return 0; /* its shapes, as usual */
        }
        before();
        int32_t h[3] = {C_SPRITE, n, (int32_t)hv};
        put(h, 3), put(a + 1, (size_t)n - 1);
        stats[C_SPRITE]++;
        depth++;
        return 1;
    }
    case MODX_HR_SCENE:
        if (n >= 4) {
            int32_t w[5] = {C_SCENE, a[0], a[1], a[2], a[3]};
            put(w, 5);
        }
        return 0;
    default:
        return 0;
    }
}

static modx_display_t display = {2, d_fill, d_rect, d_text, d_line, d_hr};

/* ================================================================ page API */
/* The display scale (2..8): before the module binds (the page calls it first). */
WEB_EXPORT void web_sparkle_set_scale(int s)
{
    S = s < 2 ? 2 : s > 8 ? 8 : s;
    display.scale = S;
    modx_set_display(&display);
    gfx_dirty_hook = dirty_hook;
    gfx_text_hook = core_text;
}
WEB_EXPORT int web_sparkle_scale(void) { return S; }

/* A scroll wheel or a trackpad's two-finger scroll over the game: dy notches (+ down) at logical (x, y) */
void mquest_scroll(int dy, int x, int y);
WEB_EXPORT void web_sparkle_scroll(int dy, int x, int y) { mquest_scroll(dy, x, y); }
#include <stdbool.h>
bool mquest_text(int c);
/* a typed character (a computer keyboard's printable key): 1 if the game's text field took it */
WEB_EXPORT int web_sparkle_text(int c) { return mquest_text(c) ? 1 : 0; }

/* The hashes of the high-resolution pictures the page has (n words at p). */
WEB_EXPORT void web_sparkle_art(const uint32_t *p, int n)
{
    free(art);
    art = n > 0 ? malloc((size_t)n * sizeof *art) : NULL;
    nart = art ? (size_t)n : 0;
    if (art) {
        memcpy(art, p, nart * sizeof *art);
        qsort(art, nart, sizeof *art, cmp_u32);
    }
}

/* The FNV-1a hashes of the sprite ids the page has vector art for (n words at p). */
WEB_EXPORT void web_sparkle_vec(const uint32_t *p, int n)
{
    free(vec);
    vec = n > 0 ? malloc((size_t)n * sizeof *vec) : NULL;
    nvec = vec ? (size_t)n : 0;
    if (vec) {
        memcpy(vec, p, nvec * sizeof *vec);
        qsort(vec, nvec, sizeof *vec, cmp_u32);
    }
}

/* The ids of sprites drawn without art since the last call, one a line (the coverage report). */
WEB_EXPORT const char *web_sparkle_sprite_misses(void)
{
    static char out[NMISS * 33 + 1];
    size_t k = 0;
    for (int i = 0; i < nmiss; i++) {
        size_t l = strlen(miss_id[i]);
        memcpy(out + k, miss_id[i], l);
        out[k + l] = '\n';
        k += l + 1;
    }
    out[k] = 0;
    nmiss = 0;
    return out;
}

/* Keep the page inside Sparkle Town: the game is the home screen, and any other screen (but a
 * message of the game's) switches back to it. */
extern const screen_t scr_mquest;
static void guard(void)
{
    const screen_t *c = ui_current();
    if (locked && c != &scr_mquest && (!c || strcmp(c->name, "message")))
        ui_switch(&scr_mquest);
}
WEB_EXPORT void web_sparkle_lock(void)
{
    locked = true;
    ui_set_home(&scr_mquest);
    guard();
}

/* The commands of the frame (*n words); the buffer is reused by the next call. */
WEB_EXPORT const int32_t *web_sparkle_take(uint32_t *n)
{
    guard();
    if (npend)
        pend_flush();
    static int32_t *out;
    static size_t outcap;
    if (ncmd > outcap) {
        free(out);
        outcap = ncmd + 1024;
        out = malloc(outcap * sizeof *out);
        if (!out)
            outcap = 0;
    }
    size_t k = out ? ncmd : 0;
    if (k)
        memcpy(out, cmd, k * sizeof *cmd);
    *n = (uint32_t)k;
    ncmd = 0;
    return out;
}

/* Everything again (the page's canvas changed size): the whole screen as the core would draw it. */
WEB_EXPORT void web_sparkle_redraw(void)
{
    clip_sent[0] = -1;
    app_redraw();
}

/* Counts of the commands since the last call (index C_* up to COPY), [8] the pictures without art,
 * [9] sprites drawn by the page, [10] sprites without art, [11] soft-key bar pieces. */
WEB_EXPORT const uint32_t *web_sparkle_stats(void)
{
    static uint32_t out[12];
    memcpy(out, stats, 8 * sizeof *stats); /* [1..7]: FILL .. COPY */
    out[8] = img_miss;
    out[9] = stats[C_SPRITE], out[10] = spr_miss, out[11] = stats[C_SOFTBAR];
    memset(stats, 0, sizeof stats);
    img_miss = spr_miss = 0;
    return out;
}
#endif
