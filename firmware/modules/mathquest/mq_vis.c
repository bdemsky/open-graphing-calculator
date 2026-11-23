/*
 * Math Quest: drawing.
 *
 * Primitives on top of the API table's rectangles and text (filled circles
 * and rings, rounded rectangles, filled polygons by scanline, stars, hearts),
 * integer sine / cosine (a 0..90 degree table, value * 1024), sprites
 * (indexed pixel art, scaled, one rectangle per run of equal pixels in a row;
 * a loaded theme pack replaces the built-in cast), hats, stickers, room decor,
 * and the visual models of the problems (mq_visual): arrays, equal groups,
 * fraction pies and bars, base-ten blocks, number lines, clock faces, unit
 * square grids and area models, L-shapes, angles with a protractor, shapes
 * with mirror lines, coins, bar and picture graphs, line plots, tables, the
 * coordinate grid, boxes of cubes, place-value charts, rulers, scales, jugs
 * and lines / rays / segments.
 *
 * `reveal` (0..1000) is the animation progress of a model that appears in
 * stages (rows of an array, slices of a pie, coins dropping in); the caller
 * advances it with the frame clock. Everything is integer arithmetic.
 */
#include "mq.h"

/* Resolution independence (docs/MATHQUEST.md): everything the module computes -- layout, motion,
 * hit tests -- is in logical pixels of the 320 x 240 screen. These primitives are the only place
 * that turns them into physical pixels: x scale (api->display_scale, 1 on the calculator), plus
 * the shake offset of the moment (q->ox, q->oy; logical). Text goes by style: a font is mapped to
 * the font of the same role at the display's scale (mq_font). */
static int S(const mq_t *q) { return q->scale > 1 ? q->scale : 1; }

/* the zoom (q->zm, x16 around zx, zy; 0: none): a part of the scene drawn bigger, e.g. the story
 * stage acted out in the field. Every primitive goes through these. */
static int zx_(const mq_t *q, int v) { return q->zm ? q->zx + (v - q->zx) * q->zm / 16 : v; }
static int zy_(const mq_t *q, int v) { return q->zm ? q->zy + (v - q->zy) * q->zm / 16 : v; }

void mq_zoom(mq_t *q, int zm16, int cx, int cy)
{
    q->zm = (uint8_t)(zm16 == 16 ? 0 : zm16);
    q->zx = (int16_t)cx, q->zy = (int16_t)cy;
}

void mq_flip(mq_t *q, bool fx, bool fy, int cx, int cy)
{
    q->flipx = fx, q->flipy = fy;
    q->fcx = (int16_t)cx, q->fcy = (int16_t)cy;
}

/* a colour at dusk (warm, a little darker) or at night (dark blue) */
uint16_t mq_tint(const mq_t *q, uint16_t c)
{
    if (!q->tint)
        return c;
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    if (q->tint == TINT_NIGHT)
        r = r * 5 / 16 + 2, g = g * 6 / 16 + 5, b = b * 9 / 16 + 9;
    else
        r = r * 15 / 16 + 1, g = g * 14 / 16 + 3, b = b * 12 / 16 + 2; /* dusk: a golden hour, not brown */
    return (uint16_t)((r > 31 ? 31 : r) << 11 | (g > 63 ? 63 : g) << 5 | (b > 31 ? 31 : b));
}

void mq_fill(mq_t *q, int x, int y, int w, int h, uint16_t c)
{
    if (q->head_watch && w >= 60 && h >= 10) /* (tests: a panel over a friend's head drawn before it) */
        for (int k = 0; k < q->head_n; k++) {
            const int16_t *hb = q->head_box[k];
            if (x < hb[0] + hb[2] && hb[0] < x + w && y < hb[1] + hb[3] && hb[1] < y + h)
                q->head_clip++, q->head_clip_y = (int16_t)hb[1], q->head_clip_slot = -1,
                q->head_cover[0] = (int16_t)x, q->head_cover[1] = (int16_t)y, q->head_cover[2] = (int16_t)w,
                q->head_cover[3] = (int16_t)h;
        }
    if (q->zm && w > 0 && h > 0) {
        int x1 = zx_(q, x + w), y1 = zy_(q, y + h);
        x = zx_(q, x), y = zy_(q, y);
        w = x1 - x, h = y1 - y;
    }
    if (q->flipx)
        x = 2 * q->fcx - x - w;
    if (q->flipy)
        y = 2 * q->fcy - y - h;
    c = mq_tint(q, c);
    if (q->clip_y1 && y < q->clip_y0) /* a clip band (the game's field) */
        h -= q->clip_y0 - y, y = q->clip_y0;
    if (q->clip_y1 && y + h > q->clip_y1)
        h = q->clip_y1 - y;
    if (y + h > MQ_Y0 + MQ_H) /* never over the soft keys' bar (the core's: "the bottom buttons are painted over") */
        h = MQ_Y0 + MQ_H - y;
    if (w > 0 && h > 0) {
        int s = S(q);
        q->px_acc += (uint32_t)(w * h), q->fill_n++;
        if (y < q->dirty_y0)
            q->dirty_y0 = (int16_t)y;
        if (y + h > q->dirty_y1)
            q->dirty_y1 = (int16_t)(y + h);
        q->api->fill_rect((x + q->ox) * s, (y + q->oy) * s, w * s, h * s, c);
    }
}

void mq_line(mq_t *q, int x0, int y0, int x1, int y1, uint16_t c)
{
    int s = S(q);
    if (q->zm || q->flipx || q->flipy) { /* zoomed (as thick as the zoom: offset lines), mirrored */
        int t = q->zm >= 24 ? 2 : 1, z = q->zm;
        bool fx = q->flipx, fy = q->flipy;
        x0 = zx_(q, x0), y0 = zy_(q, y0), x1 = zx_(q, x1), y1 = zy_(q, y1);
        if (fx)
            x0 = 2 * q->fcx - x0 - 1, x1 = 2 * q->fcx - x1 - 1;
        if (fy)
            y0 = 2 * q->fcy - y0 - 1, y1 = 2 * q->fcy - y1 - 1;
        q->zm = 0, q->flipx = q->flipy = false;
        for (int k = 0; k < t; k++)
            mq_line(q, x0 + k, y0, x1 + k, y1, c);
        q->zm = (uint8_t)z, q->flipx = fx, q->flipy = fy;
        return;
    }
    c = mq_tint(q, c);
    if (s == 1)
        q->api->line(x0 + q->ox, y0 + q->oy, x1 + q->ox, y1 + q->oy, c);
    else {
        int16_t v[4] = {(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1};
        int hr = mq_hr_shape(q, MODX_SH_LINE, c, v, 4); /* (mq_hr.c: a high-resolution platform) */
        for (int k = 0; k < s; k++) /* a thick line: s offsets */
            q->api->line((x0 + q->ox) * s + k, (y0 + q->oy) * s, (x1 + q->ox) * s + k, (y1 + q->oy) * s, c);
        if (hr)
            mq_hr_end(q);
    }
}

/* a rectangle outline (logical) */
void mq_rect(mq_t *q, int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0)
        return;
    mq_fill(q, x, y, w, 1, c), mq_fill(q, x, y + h - 1, w, 1, c);
    mq_fill(q, x, y, 1, h, c), mq_fill(q, x + w - 1, y, 1, h, c);
}

/* the font of style `st` at the display's scale */
const font_t *mq_font(mq_t *q, int st)
{
    const modx_api_t *api = q->api;
    const font_t *f[4] = {api->font_small, api->font_medium, api->font_large, api->font_xlarge};
    st = st < 0 ? 0 : st > 3 ? 3 : st;
    if (S(q) > 1) /* the next sizes up (a high-resolution platform maps styles to its own fonts) */
        st = st + 2 > 3 ? 3 : st + 2;
    return f[st];
}
/* a font handed in by role (font_small ... font_xlarge) as the style it stands for */
static const font_t *scaled(mq_t *q, const font_t *f)
{
    if (S(q) == 1)
        return f;
    const modx_api_t *api = q->api;
    return mq_font(q, f == api->font_small ? TS_SMALL : f == api->font_medium ? TS_BODY : f == api->font_large ? TS_TITLE : TS_BIG);
}

int mq_text(mq_t *q, const font_t *f, int x, int y, const char *s, uint16_t c)
{
    int k = S(q);
    if (q->flipx || q->flipy) { /* mirrored: the text where its box goes, still readable */
        bool fx = q->flipx, fy = q->flipy;
        int z = q->zm, X = zx_(q, x), Y = zy_(q, y);
        const font_t *f2 = f;
        if (z) {
            const modx_api_t *api = q->api;
            int up = z >= 28 ? 2 : z >= 20 ? 1 : 0, st = f == api->font_small ? 0 : f == api->font_medium ? 1 : f == api->font_large ? 2 : 3;
            const font_t *fs[4] = {api->font_small, api->font_medium, api->font_large, api->font_xlarge};
            f2 = fs[st + up > 3 ? 3 : st + up];
        }
        if (fx)
            X = 2 * q->fcx - X - q->api->text_width(f2, s);
        if (fy)
            Y = 2 * q->fcy - Y - f2->h;
        q->flipx = q->flipy = false, q->zm = 0;
        int r = mq_text(q, f2, X, Y, s, c);
        q->flipx = fx, q->flipy = fy, q->zm = (uint8_t)z;
        return r;
    }
    c = mq_tint(q, c);
    if (q->zm) { /* zoomed: a bigger font at the zoomed place */
        const modx_api_t *api = q->api;
        int z = q->zm, up = z >= 28 ? 2 : z >= 20 ? 1 : 0;
        int st = f == api->font_small ? 0 : f == api->font_medium ? 1 : f == api->font_large ? 2 : 3;
        const font_t *fs[4] = {api->font_small, api->font_medium, api->font_large, api->font_xlarge};
        const font_t *f2 = fs[st + up > 3 ? 3 : st + up];
        int X = zx_(q, x), Y = zy_(q, y) + (zy_(q, y + f->h) - zy_(q, y) - f2->h) / 2;
        q->zm = 0;
        int r = mq_text(q, f2, X, Y, s, c);
        q->zm = (uint8_t)z;
        return r;
    }
    if (q->clip_y1 && (y < q->clip_y0 || y + f->h - 1 > q->clip_y1)) /* (in its band whole, or not at all: no half a number) */
        return x;
    { /* never half a word at the screen's edge (the bot's 14, the audit's text-offscreen): a little over is held
       * at the edge, mostly over (a label scrolling away) is not drawn */
        int tw = q->api->text_width(f, s);
        if (x < 0 || x + tw > MQ_W) {
            int over = x < 0 ? -x : x + tw - MQ_W;
            if (over * 2 > tw || tw > MQ_W)
                return x + tw;
            x = x < 0 ? 0 : MQ_W - tw;
        }
    }
    int hr = mq_hr_text(q, f, x, y, s, c);
    int r = q->api->text(scaled(q, f), (x + q->ox) * k, (y + q->oy) * k, s, c, -1) / k;
    if (hr)
        mq_hr_end(q);
    return r;
}

/* text width in logical pixels: the layout follows the 1x font, whatever the display draws */
int mq_text_w(mq_t *q, const font_t *f, const char *s) { return q->api->text_width(f, s); }

int mq_text_c(mq_t *q, const font_t *f, int cx, int y, const char *s, uint16_t c)
{
    int w = q->api->text_width(f, s); /* the layout follows the logical (1x) font */
    if (q->zm && !q->flipx && !q->flipy) { /* zoomed: centred with the bigger font's width */
        const modx_api_t *api = q->api;
        int z = q->zm, up = z >= 28 ? 2 : z >= 20 ? 1 : 0;
        int st = f == api->font_small ? 0 : f == api->font_medium ? 1 : f == api->font_large ? 2 : 3;
        const font_t *fs[4] = {api->font_small, api->font_medium, api->font_large, api->font_xlarge};
        const font_t *f2 = fs[st + up > 3 ? 3 : st + up];
        int X = zx_(q, cx) - api->text_width(f2, s) / 2, Y = zy_(q, y) + (zy_(q, y + f->h) - zy_(q, y) - f2->h) / 2;
        q->zm = 0, q->talign = MODX_HR_ALIGN_CENTRE;
        int r = mq_text(q, f2, X, Y, s, c);
        q->zm = (uint8_t)z, q->talign = MODX_HR_ALIGN_LEFT;
        return r;
    }
    q->talign = MODX_HR_ALIGN_CENTRE; /* (a platform with its own font keeps it centred) */
    int r = mq_text(q, f, cx - w / 2, y, s, c);
    q->talign = MODX_HR_ALIGN_LEFT;
    return r;
}

/* s in at most w pixels (a long name: "Mia's town", the grown-ups' title): font f, else the smaller
 * fonts (kept on f's middle line), else the smallest cut at a whole character with "..." */
int mq_text_fit(mq_t *q, const font_t *f, int x, int y, int w, const char *s, uint16_t c, bool centre)
{
    const modx_api_t *api = q->api;
    const font_t *fs[4] = {api->font_small, api->font_medium, api->font_large, api->font_xlarge};
    int st = f == fs[0] ? 0 : f == fs[1] ? 1 : f == fs[2] ? 2 : 3;
    char b[80];
    const char *t = s;
    const font_t *g = f;
    for (int k = st; k >= 0; k--) {
        g = fs[k];
        if (api->text_width(g, s) <= w)
            break;
        if (k == 0) { /* still too wide: whole characters and "..." */
            int n = 0;
            while (s[n] && n < (int)sizeof b - 4)
                b[n] = s[n], n++;
            for (; n > 0; n--) {
                b[n] = '.', b[n + 1] = '.', b[n + 2] = '.', b[n + 3] = 0;
                if (api->text_width(g, b) <= w)
                    break;
            }
            if (!n)
                b[0] = 0;
            t = b;
        }
    }
    int dy = (f->h - g->h) / 2, tw = api->text_width(g, t);
    q->talign = centre ? MODX_HR_ALIGN_CENTRE : MODX_HR_ALIGN_LEFT;
    mq_text(q, g, centre ? x - tw / 2 : x, y + dy, t, c);
    q->talign = MODX_HR_ALIGN_LEFT;
    return tw;
}

static int isqrt(int v)
{
    if (v <= 0)
        return 0;
    int r = 0, b = 1 << 14;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b)
            v -= r + b, r = (r >> 1) + b;
        else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

void mq_circle(mq_t *q, int cx, int cy, int r, uint16_t c)
{
    int16_t v[3] = {(int16_t)cx, (int16_t)cy, (int16_t)r};
    int hr = mq_hr_shape(q, MODX_SH_CIRCLE, c, v, 3);
    for (int dy = -r; dy <= r; dy++) {
        int dx = isqrt(r * r - dy * dy + r);
        mq_fill(q, cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
    if (hr)
        mq_hr_end(q);
}

void mq_ring(mq_t *q, int cx, int cy, int r, uint16_t c)
{
    int16_t v[3] = {(int16_t)cx, (int16_t)cy, (int16_t)r};
    int hr = mq_hr_shape(q, MODX_SH_RING, c, v, 3);
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        static const int8_t sx[8] = {1, 1, -1, -1, 1, 1, -1, -1}, sy[8] = {1, -1, 1, -1, 1, -1, 1, -1};
        for (int k = 0; k < 8; k++) {
            int px = k < 4 ? x : y, py = k < 4 ? y : x;
            mq_fill(q, cx + sx[k] * px, cy + sy[k] * py, 1, 1, c);
        }
        y++;
        if (err < 0)
            err += 2 * y + 1;
        else
            x--, err += 2 * (y - x) + 1;
    }
    if (hr)
        mq_hr_end(q);
}

void mq_rrect(mq_t *q, int x, int y, int w, int h, int r, uint16_t c)
{
    if (r * 2 > h)
        r = h / 2;
    if (r * 2 > w)
        r = w / 2;
    int16_t v[5] = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)r};
    int hr = mq_hr_shape(q, MODX_SH_RRECT, c, v, 5);
    mq_fill(q, x, y + r, w, h - 2 * r, c);
    for (int dy = 0; dy < r; dy++) {
        int dx = r - isqrt(r * r - (r - dy) * (r - dy));
        mq_fill(q, x + dx, y + dy, w - 2 * dx, 1, c);
        mq_fill(q, x + dx, y + h - 1 - dy, w - 2 * dx, 1, c);
    }
    if (hr)
        mq_hr_end(q);
}

/* sin * 1024 for whole degrees */
static const int16_t SIN90[91] = {
    0,    18,   36,   54,   71,   89,   107,  125,  143,  160,  178,  195,  213,  230,  248,  265,  282,  299,  316,
    333,  350,  367,  384,  400,  416,  433,  449,  465,  481,  496,  512,  527,  543,  558,  573,  587,  602,  616,
    630,  644,  658,  672,  685,  698,  711,  724,  737,  749,  761,  773,  784,  796,  807,  818,  828,  839,  849,
    859,  868,  878,  887,  896,  904,  912,  920,  928,  935,  943,  949,  956,  962,  968,  974,  979,  984,  989,
    994,  998,  1002, 1005, 1008, 1011, 1014, 1016, 1018, 1020, 1022, 1023, 1023, 1024, 1024};
int mq_isin(int deg)
{
    deg %= 360;
    if (deg < 0)
        deg += 360;
    if (deg <= 90)
        return SIN90[deg];
    if (deg <= 180)
        return SIN90[180 - deg];
    if (deg <= 270)
        return -SIN90[deg - 180];
    return -SIN90[360 - deg];
}
int mq_icos(int deg) { return mq_isin(deg + 90); }

static void poly(mq_t *q, const int16_t *px, const int16_t *py, int n, uint16_t c);
void mq_poly(mq_t *q, const int16_t *px, const int16_t *py, int n, uint16_t c) { poly(q, px, py, n, c); }

/* scanline fill of a polygon (n <= 48 points, even-odd) */
static void poly(mq_t *q, const int16_t *px, const int16_t *py, int n, uint16_t c)
{
    int16_t v[96];
    for (int i = 0; i < n && n <= 48; i++)
        v[i] = px[i], v[n + i] = py[i];
    int hr = n <= 48 ? mq_hr_shape(q, MODX_SH_POLY, c, v, n) : 0;
    int y0 = py[0], y1 = py[0];
    for (int i = 1; i < n; i++)
        y0 = py[i] < y0 ? py[i] : y0, y1 = py[i] > y1 ? py[i] : y1;
    for (int y = y0; y <= y1; y++) {
        int xs[48], k = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            int ay = py[i], by = py[j];
            if ((ay <= y && by > y) || (by <= y && ay > y)) {
                if (k < 48)
                    xs[k++] = px[i] + (y - ay) * (px[j] - px[i]) / (by - ay);
            }
        }
        for (int i = 1; i < k; i++) /* insertion sort */
            for (int j = i; j > 0 && xs[j] < xs[j - 1]; j--) {
                int t = xs[j];
                xs[j] = xs[j - 1], xs[j - 1] = t;
            }
        for (int i = 0; i + 1 < k; i += 2)
            mq_fill(q, xs[i], y, xs[i + 1] - xs[i] + 1, 1, c);
    }
    if (hr)
        mq_hr_end(q);
}

static void tri(mq_t *q, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c)
{
    int16_t px[3] = {(int16_t)x0, (int16_t)x1, (int16_t)x2}, py[3] = {(int16_t)y0, (int16_t)y1, (int16_t)y2};
    poly(q, px, py, 3, c);
}

void mq_star(mq_t *q, int cx, int cy, int r, uint16_t c)
{
    int16_t px[10], py[10];
    for (int i = 0; i < 10; i++) {
        int rr = i % 2 ? r * 2 / 5 : r, a = -90 + i * 36;
        px[i] = (int16_t)(cx + mq_icos(a) * rr / 1024), py[i] = (int16_t)(cy + mq_isin(a) * rr / 1024);
    }
    poly(q, px, py, 10, c);
}

void mq_heart(mq_t *q, int cx, int cy, int r, uint16_t c)
{
    mq_circle(q, cx - r / 2, cy - r / 4, r / 2 + 1, c);
    mq_circle(q, cx + r / 2, cy - r / 4, r / 2 + 1, c);
    tri(q, cx - r - 1, cy - r / 8, cx + r + 1, cy - r / 8, cx, cy + r, c);
}

/* pie sector from angle a0 to a1 (degrees, clockwise from 12 o'clock) */
static void sector(mq_t *q, int cx, int cy, int r, int a0, int a1, uint16_t c)
{
    int16_t px[48], py[48];
    int n = 0;
    px[n] = (int16_t)cx, py[n++] = (int16_t)cy;
    int step = (a1 - a0) / 40 + 1;
    for (int a = a0; n < 47; a += step) {
        if (a > a1)
            a = a1;
        px[n] = (int16_t)(cx + mq_isin(a) * r / 1024), py[n++] = (int16_t)(cy - mq_icos(a) * r / 1024);
        if (a == a1)
            break;
    }
    poly(q, px, py, n, c);
}

/* a slice of a cake (the teaching moments' shared cake) */
void mq_cake_slice(mq_t *q, int cx, int cy, int r, int a0, int a1, uint16_t c) { sector(q, cx, cy, r, a0, a1, c); }

/* ------------------------------------------------------------------ sprites */
static void sprite_px(mq_t *q, const uint8_t *px, int cw, int ch, const uint16_t *pal, int x, int y, int s)
{
    int hr = mq_hr_image(q, px, cw, ch, cw, 8, pal, x, y, s * 16, 0, 0, 16, -1);
    for (int r = 0; r < ch; r++) {
        int c = 0;
        while (c < cw) {
            uint8_t v = px[r * cw + c];
            int c1 = c + 1;
            while (c1 < cw && px[r * cw + c1] == v)
                c1++;
            if (v)
                mq_fill(q, x + c * s, y + r * s, (c1 - c) * s, s, pal[v]);
            c = c1;
        }
    }
    if (hr)
        mq_hr_end(q);
}

/* A picture of cw x ch art pixels drawn into a dw x dh box (any size: nearest pixels), its height
 * scaled by sy/16 (squash < 16 < stretch, standing on the box's bottom), the rows sheared by
 * `shear` pixels at the top (a tilt), mirrored. Rows that come from the same art row are one
 * rectangle tall when nothing shears them. */
static void blit(mq_t *q, const uint8_t *px, int cw, int ch, const uint16_t *pal, int x, int y, int dw, int dh, int shear,
                 int sy, bool mir)
{
    int hh = dh * sy / 16;
    if (hh < 1)
        hh = 1;
    int hr = mq_hr_image(q, px, cw, ch, cw, 8, pal, x, y + dh - ch * dw / cw, dw * 16 / cw, mir ? 4 : 0, shear, sy, -1);
    int top = y + dh - hh;
    for (int r = 0; r < hh;) {
        int sr = r * ch / hh, r1 = r + 1;
        while (r1 < hh && r1 * ch / hh == sr && !shear)
            r1++;
        int ox = shear * (hh - r) / hh;
        const uint8_t *row = px + sr * cw;
        int c = 0;
        while (c < cw) {
            uint8_t v = row[mir ? cw - 1 - c : c];
            int c1 = c + 1;
            while (c1 < cw && row[mir ? cw - 1 - c1 : c1] == v)
                c1++;
            if (v) {
                int x0 = c * dw / cw, x1 = c1 * dw / cw;
                if (x1 > x0)
                    mq_fill(q, x + ox + x0, top + r, x1 - x0, r1 - r, pal[v]);
            }
            c = c1;
        }
        r = r1;
    }
    if (hr)
        mq_hr_end(q);
}

/* x, y, w, h of a rectangle, then (run, value) pairs filling it row by row, into dst (stride) */
void mq_rle_decode(const uint8_t *src, uint8_t *dst, int stride)
{
    int x0 = src[0], y0 = src[1], w = src[2], h = src[3], n = w * h, i = 0;
    /* (bounded: a picture that would not fit the cell buffer, or a run of 0, is drawn no further -- CR-05) */
    if (!w || x0 + w > stride || (y0 + h) * stride > THM_MAXCELL * THM_MAXCELL)
        return;
    src += 4;
    while (i < n) {
        int run = *src++, v = *src++;
        if (!run)
            break;
        for (int k = 0; k < run && i < n; k++, i++)
            dst[(y0 + i / w) * stride + x0 + i % w] = (uint8_t)v;
    }
}

/* the decoded picture of `key` (a few are kept); *fresh: it must be decoded now */
static uint8_t *art_slot(mq_t *q, uint16_t key, bool *fresh)
{
    for (int i = 0; i < 4; i++)
        if (q->art_key[i] == key) {
            *fresh = false;
            return q->art_buf[i];
        }
    int i = q->art_rr++ % 4;
    q->art_key[i] = key;
    *fresh = true;
    return q->art_buf[i];
}

/* a built-in face of the cast (EX_*): 40 x 40, or 20 x 20 (small); the idle face whole, the others
 * as the rectangle where they differ from it */
const uint8_t *mq_cast_px(mq_t *q, int slot, int ex, bool small)
{
    if (slot < 0 || slot >= MQ_NCAST)
        slot = 0;
    if (ex < 0 || ex >= EX_COUNT)
        ex = EX_IDLE;
    bool fresh;
    uint8_t *b = art_slot(q, (uint16_t)(1 + (small ? 0x400 : 0) + slot * 16 + ex), &fresh);
    if (fresh) {
        int n = small ? ART_SMALL : ART_BIG;
        mq_rle_decode(mq_art_rle + mq_art_face[small][slot][EX_IDLE], b, n);
        if (ex != EX_IDLE)
            mq_rle_decode(mq_art_rle + mq_art_face[small][slot][ex], b, n);
    }
    return b;
}

/* sprite i of the theme pack, decoded (cw x ch) */
const uint8_t *mq_theme_px(mq_t *q, int i)
{
    bool fresh;
    uint8_t *b = art_slot(q, (uint16_t)(0x8000 | i), &fresh);
    if (fresh)
        mq_rle_decode(q->theme.px + q->theme.off[i], b, q->theme.cw);
    return b;
}

/* the theme's sprite for slot: its idle frame 0 (or any of the slot); -1 none */
static int theme_base(const mq_theme_t *t, int slot)
{
    int any = -1;
    if (!t->loaded)
        return -1;
    for (int i = 0; i < t->nspr; i++)
        if (t->role[i] == slot) {
            if (t->anim[i] == AN_IDLE && t->frame[i] == 0)
                return i;
            if (any < 0)
                any = i;
        }
    return any;
}

/* The theme sprite showing `anim` of `slot` at time t (frames in turn: idle slowly, the
 * others quickly), or -1 when the pack has no frames for it. */
int mq_theme_frame(const mq_t *q, int slot, int anim, uint32_t t)
{
    const mq_theme_t *th = &q->theme;
    if (!th->loaded)
        return -1;
    int n = 0;
    for (int i = 0; i < th->nspr; i++)
        if (th->role[i] == slot && th->anim[i] == anim && th->frame[i] + 1 > n)
            n = th->frame[i] + 1;
    if (!n)
        return -1;
    int f = (int)(t / (anim == AN_IDLE ? 420u : anim == AN_DANCE ? 250u : 160u) % (uint32_t)n), best = -1;
    for (int i = 0; i < th->nspr; i++)
        if (th->role[i] == slot && th->anim[i] == anim && (best < 0 || th->frame[i] == f))
            best = i;
    return best;
}

/* a theme sprite of slot tagged anim, frame f exactly (-1 none) */
static int theme_exact(const mq_theme_t *th, int slot, int anim, int f)
{
    if (!th->loaded)
        return -1;
    for (int i = 0; i < th->nspr; i++)
        if (th->role[i] == slot && th->anim[i] == anim && th->frame[i] == f)
            return i;
    return -1;
}

/* a little sparkle (four points) */
static void twinkle(mq_t *q, int cx, int cy, int r, uint16_t c)
{
    int16_t px[8], py[8];
    for (int i = 0; i < 8; i++) {
        int rr = i % 2 ? r / 3 : r;
        px[i] = (int16_t)(cx + mq_icos(i * 45 - 90) * rr / 1024), py[i] = (int16_t)(cy + mq_isin(i * 45 - 90) * rr / 1024);
    }
    mq_poly(q, px, py, 8, c);
}

/* Each friend's idle quirk, now and then (about every 11 s for 1.4 s; qt: ms into it): what it does
 * with its face and body, and what appears beside it. Box x, y of size z. */
static void quirk(mq_t *q, int slot, int qt, int x, int y, int z, int *ex, int *shear, int *sy, int *up, bool *mir, bool draw)
{
#define U(v) ((v) * z / 40)
    int ph = qt * 4 / 1400; /* 0..3 */
    switch (slot) {
    case 0: /* Pip: a butterfly visits its leaves */
        *ex = ph < 2 ? EX_LOOKR : EX_HAPPY;
        if (draw) {
            int bx = x + U(30) - qt * z / 3000, by = y + U(2) + (mq_isin(qt / 3 % 360) * U(4)) / 1024, f = qt / 90 % 2;
            mq_oval(q, bx - U(2), by, U(2) + 1, f ? U(3) : 1, Q_PINK), mq_oval(q, bx + U(2), by, U(2) + 1, f ? U(3) : 1, Q_LAV);
            mq_fill(q, bx, by - U(1), 1, U(3), Q_INK);
        }
        break;
    case 1: /* Dot: a letter arrives; it reads it and beams */
        *ex = ph < 2 ? EX_WOW : EX_HAPPY;
        if (draw && ph < 3) {
            int lx = x + U(28), ly = y + U(20) - (ph == 0 ? U(10) - qt * z / 1400 : 0);
            mq_fill(q, lx, ly, U(9), U(6), Q_WHITE), mq_rect(q, lx, ly, U(9), U(6), Q_DIM);
            mq_line(q, lx, ly, lx + U(9) / 2, ly + U(3), Q_DIM), mq_line(q, lx + U(9) - 1, ly, lx + U(9) / 2, ly + U(3), Q_DIM);
            mq_heart(q, lx + U(9) / 2, ly + U(3), U(1) + 1, Q_HOTPINK);
        }
        break;
    case 2: /* Momo: blows a bubble from its gills */
        *ex = ph < 2 ? EX_TALK : EX_HAPPY;
        if (draw) {
            int r = U(2) + qt * z / 9000;
            mq_ring(q, x + U(32) + qt * z / 7000, y + U(18) - qt * z / 3200, r, Q_SKY);
            mq_fill(q, x + U(31) + qt * z / 7000, y + U(17) - qt * z / 3200, 2, 2, Q_WHITE);
        }
        break;
    case 3: /* Olli: looks about and hoots a little song */
        *ex = ph % 2 ? EX_LOOKR : EX_LOOKL;
        if (draw) {
            int nx = x + U(34) + (qt / 200 % 2) * 2, ny = y + U(14) - qt * z / 5600;
            mq_circle(q, nx, ny, U(2) + 1, Q_PURPLE), mq_fill(q, nx + U(2), ny - U(7), 1, U(7), Q_PURPLE);
        }
        break;
    case 4: /* Tango: turns round with a wag */
        *ex = EX_HAPPY;
        *mir = ph == 1 || ph == 2;
        *shear = (qt / 120 % 2 ? 1 : -1) * U(2);
        break;
    case 5: /* Dabble: a splash of paint, then a proud smile */
        *ex = ph < 2 ? EX_THINK : EX_HAPPY;
        if (draw && ph >= 1) {
            int sx = x + U(36), sy0 = y + U(30), r = ph == 1 ? U(2) : U(3);
            mq_circle(q, sx, sy0, r + 1, Q_HOTPINK), mq_circle(q, sx + r + 1, sy0 - r, r / 2 + 1, Q_SKY);
            mq_circle(q, sx - r - 1, sy0 - r / 2, r / 2 + 1, Q_YELLOW);
        }
        break;
    case 6: /* Bramble: curls up in a ball and pops out */
        *ex = ph < 2 ? EX_BLINK : EX_WOW;
        *sy = ph == 0 ? 12 : ph == 1 ? 11 : ph == 2 ? 18 : 16;
        *up = ph == 2 ? U(4) : 0;
        if (draw && ph >= 2)
            mq_text(q, q->api->font_medium, x + U(34), y - U(2), "!", Q_ORANGE);
        break;
    case 7: /* Shelly: a zoom on the spot */
        *ex = EX_HAPPY;
        *shear = U(5);
        if (draw)
            for (int k = 0; k < 3; k++)
                mq_fill(q, x - U(6) - (qt / 60 + k * 5) % U(10), y + U(22) + k * U(5), U(6), 1, Q_DIM);
        break;
    default: /* Nova: twinkles */
        *ex = ph % 2 ? EX_HAPPY : EX_IDLE;
        if (draw)
            for (int k = 0; k < 3; k++) {
                int a = qt / 4 + k * 120, r = U(22);
                twinkle(q, x + U(20) + mq_icos(a) * r / 1024, y + U(22) + mq_isin(a) * r / 1024, U(3) + 1,
                        (qt / 100 + k) % 2 ? Q_YELLOW : Q_WHITE);
            }
        break;
    }
#undef U
}

/* A cast member in a size x size box at (x, y), animated: AN_IDLE (a bob, blinks, a look about now
 * and then, its own little quirk), AN_HAPPY (a jump with a squash), AN_THINK (a tilt and a "?"),
 * AN_DANCE, AN_POUT (sad, a frown cloud), AN_FLY, AN_TALK (the mouth going), AN_WOW; AN_IDLE |
 * AN_STILL: blinks only. The built-in cast has a face for each moment (40 x 40 art, 20 x 20 for a
 * small box); a theme pack's frames are used when it has them, else the moves are made from its
 * idle frame (squash, stretch, tilt). */
void mq_cast(mq_t *q, int slot, int x, int y, int size, int anim)
{
    if (slot < 0 || slot >= MQ_NCAST)
        slot = 0;
    if (size < 8)
        size = 8;
    bool still = (anim & AN_STILL) != 0;
    anim &= ~AN_STILL;
    int z = size;
#define U(v) ((v) * z / 40)
    uint32_t t = q->anim_t + (uint32_t)slot * 137u;
    int ex = EX_IDLE, shear = 0, sy = 16, up = 0;
    bool mir = false;
    int bob = (mq_isin((int)((q->anim_t / 4 + (uint32_t)slot * 70) % 360)) + 1024) * z / 20480;
    int qt = -1;
    switch (anim) {
    case AN_HAPPY: {
        static const int8_t ph_sy[4] = {13, 18, 16, 17}, ph_up[4] = {0, 6, 10, 4};
        int ph = (int)(t / 120 % 4);
        sy = ph_sy[ph], up = U(ph_up[ph]);
        ex = EX_HAPPY;
        break;
    }
    case AN_THINK:
        shear = (int)((t / 500) % 2 ? 2 : 1) * z / 20;
        ex = EX_THINK;
        mq_text(q, q->api->font_medium, x + U(36), y - U(4), "?", Q_PURPLE);
        break;
    case AN_DANCE: {
        int ph = (int)(t / 250 % 4);
        shear = ph == 0 ? -U(6) : ph == 2 ? U(6) : 0;
        sy = ph == 1 ? 13 : ph == 3 ? 18 : 16;
        up = ph == 3 ? U(4) : 0;
        ex = ph % 2 ? EX_HAPPY : EX_WOW;
        mir = ph >= 2;
        break;
    }
    case AN_POUT: { /* a little pout: sad, shrunk down, a frown cloud puffing beside */
        sy = 14 + (int)(t / 400 % 2), up = -z / 20;
        ex = EX_SAD;
        int px0 = x + U(38), py0 = y + U(2) + (int)(t / 300 % 2) * z / 20;
        mq_circle(q, px0, py0, U(6), Q_GREY), mq_circle(q, px0 + U(6), py0 - U(2), U(4), Q_GREY);
        mq_fill(q, px0 - U(2), py0, U(6), z > 40 ? z / 40 : 1, Q_DIM);
        break;
    }
    case AN_FLY: { /* aloft: a high bob and flapping wings */
        up = U(12) + (mq_isin((int)(t / 3 % 360)) * U(6)) / 1024;
        ex = (t / 900) % 2 ? EX_HAPPY : EX_WOW;
        int fl = (int)(t / 120 % 2);
        mq_circle(q, x + U(2), y + U(18) - up - fl * U(4), U(6), Q_WHITE);
        mq_circle(q, x + U(38), y + U(18) - up - fl * U(4), U(6), Q_WHITE);
        mq_fill(q, x + U(12), y + U(42), U(16), z > 40 ? z / 40 : 1, RGB(220, 220, 235)); /* its shadow */
        break;
    }
    case AN_TALK: /* telling something: the mouth going, a little bounce on the words */
        ex = (t / 140) % 3 == 2 ? EX_IDLE : EX_TALK;
        up = (t / 140) % 3 == 1 ? U(1) : 0;
        if (t % 2900 < 110)
            ex = EX_BLINK;
        break;
    case AN_WOW: {
        ex = EX_WOW;
        int ph = (int)(t / 110 % 6);
        up = ph == 1 ? U(5) : ph == 2 ? U(3) : 0;
        sy = ph == 0 ? 14 : ph == 1 ? 18 : 16;
        break;
    }
    case AN_WAVE: { /* a wave: the body sways with the hand, a little hand going to and fro */
        ex = (t / 600) % 4 == 3 ? EX_TALK : EX_HAPPY;
        int a = (int)(t * 360 / 520 % 360), w = mq_isin(a) * U(3) / 1024;
        shear = w / 2;
        int hx = x + U(37) + w, hy = y + U(12) - bob;
        mq_circle(q, hx, hy, U(3) + 1, Q_INK), mq_circle(q, hx, hy, U(3), Q_WHITE);
        up = bob;
        break;
    }
    case AN_HOP: { /* hopping along: up and down with a squash on each landing, the ears lagging */
        int ph = (int)(t % 420);
        up = mq_isin(ph * 180 / 420) * U(7) / 1024;
        sy = ph < 50 || ph > 380 ? 14 : ph < 120 ? 18 : 16;
        shear = ph < 210 ? U(1) : -U(1);
        ex = EX_HAPPY;
        break;
    }
    case AN_POKED: { /* tapped: a surprised hop, then a giggle with a heart */
        ex = EX_WOW;
        int ph = (int)(t / 90 % 8);
        up = ph == 1 ? U(7) : ph == 2 ? U(5) : ph == 3 ? U(2) : 0;
        sy = ph == 0 ? 13 : ph == 1 ? 19 : ph == 4 ? 15 : 16;
        if (ph >= 4) {
            ex = EX_HAPPY;
            mq_heart(q, x + U(34), y - U(2) - (ph - 4) * U(2), U(3) + 1, Q_HOTPINK);
        }
        break;
    }
    default: /* idle: a gentle bob (the ears and bows a beat behind: a soft squash), a blink now and
              * then, and a long loop of little things -- a look about, a stretch, a happy hop, a sway,
              * its own quirk -- so a friend left on screen keeps living (22 s before it repeats) */
        if (!still) {
            up = bob;
            int lag = mq_isin((int)((q->anim_t / 4 + (uint32_t)slot * 70 + 270) % 360));
            sy = 16 + (lag > 700 ? 1 : 0); /* the lagging follow-through */
            uint32_t p = (q->anim_t + (uint32_t)slot * 2300u) % 22000u;
            if (p >= 5200 && p < 6000)
                ex = EX_LOOKL;
            else if (p >= 6000 && p < 6800)
                ex = EX_LOOKR;
            else if (p >= 7600 && p < 8400) { /* a big stretch */
                int k = (int)(p - 7600);
                sy = 16 + (k < 400 ? k : 800 - k) / 130, ex = k > 200 && k < 600 ? EX_BLINK : EX_IDLE;
            } else if (p >= 9600 && p < 11000 && z >= 32)
                qt = (int)(p - 9600);
            else if (p >= 13000 && p < 13500) { /* a happy little hop */
                int k = (int)(p - 13000);
                up = bob + mq_isin(k * 180 / 500) * U(6) / 1024, ex = EX_HAPPY, sy = k < 60 ? 14 : 16;
            } else if (p >= 15500 && p < 17100) { /* a sway, humming */
                shear = mq_isin((int)((p - 15500) * 360 / 800 % 360)) * U(2) / 1024;
                ex = (p / 400) % 2 ? EX_TALK : EX_HAPPY;
            } else if (p >= 19000 && p < 20400 && z >= 32)
                qt = (int)(p - 19000); /* the quirk again */
        }
        if (t % 3100 < 130)
            ex = EX_BLINK;
        break;
    }
    if (qt >= 0)
        quirk(q, slot, qt, x, y, z, &ex, &shear, &sy, &up, &mir, true);
    { /* (the head's top: the art's top, a bow or an ear on its first row) */
        int top = y - up + z - z * sy / 16;
        if (top < MQ_Y0)
            q->head_clip++, q->head_clip_y = (int16_t)top, q->head_clip_slot = (int16_t)slot;
        if (q->head_watch && q->head_n < 8 && z >= 20) { /* (tests: a panel drawn over it later) */
            int16_t *hb = q->head_box[q->head_n++];
            hb[0] = (int16_t)(x + z / 4), hb[1] = (int16_t)(top + z / 8), hb[2] = (int16_t)(z / 2), hb[3] = (int16_t)(z / 6);
        }
    }
    /* the picture */
    mq_theme_t *th = &q->theme;
    int b = theme_base(th, slot);
    if (b < 0 || !th->cw || !th->ch) { /* (no picture of hers in the pack, or a pack without cells: the built-in) */
        bool small = z <= 24;
        int n = small ? ART_SMALL : ART_BIG;
        blit(q, mq_cast_px(q, slot, ex, small), n, n, mq_art_pal, x, y - up, z, z, shear, sy, mir);
        return;
    }
    /* a theme pack: its frames for the moment, else its idle frame made to move */
    int fi = -1;
    if (anim != AN_IDLE && anim != AN_DANCE)
        fi = mq_theme_frame(q, slot, anim, t);
    if (fi < 0 && anim == AN_DANCE)
        fi = mq_theme_frame(q, slot, AN_DANCE, t);
    if (fi >= 0) { /* the pack's own animation: its frames as drawn */
        shear = 0, sy = 16, up = anim == AN_FLY ? up : 0;
    } else {
        if (ex == EX_BLINK) { /* a blink: its blink frame, else a quick squash */
            fi = theme_exact(th, slot, AN_BLINK, 0);
            if (fi < 0 && anim == AN_IDLE)
                sy = 15;
        } else if (ex == EX_LOOKL || ex == EX_LOOKR)
            fi = theme_exact(th, slot, AN_LOOK, ex - EX_LOOKL);
        else if (ex == EX_TALK)
            fi = theme_exact(th, slot, AN_TALK, (int)(t / 140 % 2));
        else if (ex == EX_WOW)
            fi = theme_exact(th, slot, AN_WOW, 0);
        if (fi < 0 && anim == AN_IDLE && theme_exact(th, slot, AN_BLINK, 0) < 0)
            fi = mq_theme_frame(q, slot, AN_IDLE, t); /* an older pack's idle loop (its blink in it) */
        if (fi < 0)
            fi = b;
    }
    int cw = th->cw, ch = th->ch, m = cw > ch ? cw : ch;
    blit(q, mq_theme_px(q, fi), cw, ch, th->pal, x + (z - cw * z / m) / 2, y - up, cw * z / m, ch * z / m, shear, sy, mir);
#undef U
}

/* cast member `slot` at (x, y) in a box of 20 s (the nominal cell at scale s); bob: alive (a bob,
 * blinks, glances, its quirk), else a still picture that only blinks */
void mq_sprite(mq_t *q, int slot, int x, int y, int s, int bob)
{
    mq_cast(q, slot, x, y, ART_CELL * s, bob ? AN_IDLE : AN_IDLE | AN_STILL);
}

/* a cast member animated (AN_*) in a box of 20 s */
void mq_sprite_anim(mq_t *q, int slot, int x, int y, int s, int anim) { mq_cast(q, slot, x, y, ART_CELL * s, anim); }

void mq_sticker(mq_t *q, int k, int x, int y, int s)
{
    if (k >= 0 && k < MQ_NSTICKER)
        sprite_px(q, mq_art_sticker[k], ART_STK, ART_STK, mq_art_pal, x, y, s);
}

/* hats 1..MQ_NHAT on a sprite drawn at (x, y) with scale s */
void mq_hat(mq_t *q, int hat, int x, int y, int s)
{
    int cx = x + 10 * s, top = y + 2 * s;
    switch (hat) {
    case 1: /* crown */
        mq_fill(q, cx - 5 * s, top - s, 10 * s, 3 * s, Q_YELLOW);
        tri(q, cx - 5 * s, top, cx - 5 * s, top - 5 * s, cx - 2 * s, top, Q_YELLOW);
        tri(q, cx - 2 * s, top, cx, top - 5 * s, cx + 2 * s, top, Q_YELLOW);
        tri(q, cx + 2 * s, top, cx + 5 * s, top - 5 * s, cx + 5 * s, top, Q_YELLOW);
        mq_fill(q, cx - s / 2, top, s, s, Q_HOTPINK);
        break;
    case 2: /* flower crown */
        for (int i = 0; i < 5; i++)
            mq_circle(q, cx - 6 * s + i * 3 * s, top, s + 1, i % 2 ? Q_PINK : Q_LAV);
        break;
    case 3: /* party hat */
        tri(q, cx - 3 * s, top + s, cx + 3 * s, top + s, cx, top - 7 * s, Q_SKY);
        mq_circle(q, cx, top - 7 * s, s, Q_YELLOW);
        break;
    case 4: /* chef hat */
        mq_fill(q, cx - 4 * s, top - 2 * s, 8 * s, 3 * s, Q_WHITE);
        mq_circle(q, cx - 2 * s, top - 3 * s, 2 * s, Q_WHITE);
        mq_circle(q, cx + 2 * s, top - 3 * s, 2 * s, Q_WHITE);
        break;
    case 5: /* star headband */
        mq_fill(q, cx - 7 * s, top, 14 * s, s, Q_HOTPINK);
        mq_star(q, cx, top - s, 3 * s, Q_YELLOW);
        break;
    case 6: /* wizard hat */
        tri(q, cx - 5 * s, top + s, cx + 5 * s, top + s, cx + s, top - 8 * s, Q_PURPLE);
        mq_star(q, cx, top - 2 * s, s + 1, Q_YELLOW);
        break;
    default:
        break;
    }
}

/* room decor k (0..MQ_NDECOR-1) in a 40 x 40 box at (x, y) */
void mq_decor(mq_t *q, int k, int x, int y)
{
    switch (k) {
    case 0: /* rug */
        mq_rrect(q, x + 2, y + 26, 36, 12, 6, Q_LAV);
        mq_rrect(q, x + 8, y + 29, 24, 6, 3, Q_PINK);
        break;
    case 1: /* lamp */
        mq_fill(q, x + 19, y + 14, 2, 22, Q_BROWN);
        tri(q, x + 10, y + 16, x + 30, y + 16, x + 20, y + 2, Q_YELLOW);
        mq_fill(q, x + 13, y + 36, 14, 3, Q_BROWN);
        break;
    case 2: /* plant */
        mq_rrect(q, x + 12, y + 26, 16, 12, 3, Q_ORANGE);
        mq_circle(q, x + 20, y + 16, 8, Q_GREEN);
        mq_circle(q, x + 13, y + 20, 5, Q_MINT);
        mq_circle(q, x + 27, y + 20, 5, Q_MINT);
        break;
    case 3: /* cake stand */
        mq_fill(q, x + 8, y + 30, 24, 3, Q_GREY);
        mq_fill(q, x + 19, y + 30, 2, 8, Q_GREY);
        mq_rrect(q, x + 11, y + 16, 18, 14, 4, Q_PINK);
        mq_fill(q, x + 11, y + 20, 18, 3, Q_WHITE);
        mq_circle(q, x + 20, y + 14, 3, Q_HOTPINK);
        break;
    case 4: /* window */
        mq_rrect(q, x + 4, y + 2, 32, 28, 4, Q_WHITE);
        mq_fill(q, x + 7, y + 5, 26, 22, Q_SKY);
        mq_fill(q, x + 19, y + 5, 2, 22, Q_WHITE);
        mq_circle(q, x + 12, y + 11, 3, Q_WHITE);
        break;
    case 5: /* bookshelf */
        mq_fill(q, x + 6, y + 4, 28, 34, Q_BROWN);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 5; j++)
                mq_fill(q, x + 8 + j * 5, y + 6 + i * 11, 4, 9, (i + j) % 3 == 0 ? Q_PINK : (i + j) % 3 ? Q_SKY : Q_MINT);
        break;
    case 6: /* teapot */
        mq_circle(q, x + 20, y + 26, 10, Q_MINT);
        mq_fill(q, x + 30, y + 22, 6, 3, Q_MINT);
        mq_circle(q, x + 20, y + 15, 3, Q_PINK);
        break;
    default: /* fairy lights */
        mq_line(q, x + 2, y + 6, x + 38, y + 10, Q_DIM);
        for (int i = 0; i < 6; i++)
            mq_circle(q, x + 4 + i * 6, y + 8 + i % 2, 2, i % 3 == 0 ? Q_YELLOW : i % 3 == 1 ? Q_PINK : Q_SKY);
        break;
    }
}

/* ------------------------------------------------------------------ visual models */
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static void numtxt(mq_t *q, int cx, int y, int v, uint16_t c)
{
    char s[12];
    q->api->snprintf(s, sizeof s, "%d", v);
    mq_text_c(q, q->api->font_small, cx, y, s, c);
}

static void flower(mq_t *q, int cx, int cy, int r, uint16_t c)
{
    for (int k = 0; k < 5; k++)
        mq_circle(q, cx + mq_icos(k * 72) * r / 1600, cy + mq_isin(k * 72) * r / 1600, r / 2, c);
    mq_circle(q, cx, cy, r / 3, Q_YELLOW);
}

/* a number as text, or "?" when it is the answer of the question (question mode) */
static void numq(mq_t *q, const mq_prob_t *p, char *s, size_t n, const char *fmt, int v)
{
    if (q->vis_q && (v == p->ans || (p->kind == K_REM && v == p->ans / 1000))) /* (a leftover order's answer is
                                                                                * q * 1000 + r: its quotient, CR2-15) */
        q->api->snprintf(s, n, "?");
    else
        q->api->snprintf(s, n, fmt, v);
}

/* question mode: the array as a labelled box ("7 rows of 6"), nothing to count */
/* Which label of an array is the order's unknown (0 the rows, 1 the columns, 2 the total)? A
 * generator says so with v[6] = 0x80 | bits when a given number may equal the answer (a split
 * flower bed: 3 x 8 = 3 x 5 + 3 x ?, whose 3 rows are not the unknown 3); else the answer's value
 * picks the one label (the total first, then the rows, then the columns: never two "?"). */
bool mq_array_hides(const mq_prob_t *p, int which)
{
    if (p->v[6] & 0x80)
        return (p->v[6] >> which) & 1;
    int rows = p->v[0], cols = p->v[1], total = rows * cols;
    bool tot = total == p->ans || p->skill == S_TWOSTEP || p->skill == S_FACTS;
    if (which == 2)
        return tot;
    if (tot)
        return false;
    return which == 0 ? rows == p->ans : cols == p->ans && rows != p->ans;
}

/* (s within the order's words: its lines or the friend's caption) */
static bool order_says(const mq_prob_t *p, const char *s)
{
    for (int l = 0; l < 4; l++) {
        const char *t = l < 3 ? p->text[l] : p->cap;
        for (; *t; t++) {
            int k = 0;
            while (s[k] && t[k] == s[k])
                k++;
            if (!s[k])
                return true;
        }
    }
    return false;
}

static void array_box(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h)
{
    int rows = p->v[0], cols = p->v[1];
    char a[12], b[12], s[40];
    if (p->kind == K_CHOICE && !(p->v[6] & 0x80)) { /* odd or even, which pile packs, can they make rows: the total and
                                                     * its rows are what the order is about -- never printed (an odd
                                                     * one left over is not in "rows of": CR2-11); the rows only as the
                                                     * order says them */
        mq_rrect(q, x + w / 2 - 50, y + 8, 100, h - 34, 8, Q_PINK);
        mq_rect(q, x + w / 2 - 50, y + 8, 100, h - 34, Q_HOTPINK);
        mq_text_c(q, q->api->font_medium, x + w / 2, y + h / 2 - 16, "?", Q_INK);
        q->api->snprintf(s, sizeof s, "%d rows of %d", rows, cols);
        if (!p->v[5] && order_says(p, s))
            mq_text_c(q, q->api->font_small, x + w / 2, y + h - 20, s, Q_INK);
        return;
    }
    q->api->snprintf(a, sizeof a, q->vis_q && mq_array_hides(p, 0) ? "?" : "%d", rows);
    q->api->snprintf(b, sizeof b, q->vis_q && mq_array_hides(p, 1) ? "?" : "%d", cols);
    mq_rrect(q, x + w / 2 - 50, y + 8, 100, h - 34, 8, Q_PINK);
    mq_rect(q, x + w / 2 - 50, y + 8, 100, h - 34, Q_HOTPINK);
    int total = rows * cols;
    if (!mq_array_hides(p, 2))
        q->api->snprintf(s, sizeof s, "%d in all", total);
    else
        q->api->snprintf(s, sizeof s, "?");
    mq_text_c(q, q->api->font_medium, x + w / 2, y + h / 2 - 16, s, Q_INK);
    q->api->snprintf(s, sizeof s, "%s rows of %s", a, b);
    mq_text_c(q, q->api->font_small, x + w / 2, y + h - 20, s, Q_INK);
}

static void v_array(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h, int reveal)
{
    int rows = imax(1, p->v[0]), cols = imax(1, p->v[1]), split = p->v[2];
    if (q->vis_q) {
        array_box(q, p, x, y, w, h);
        return;
    }
    if (cols * 4 > w || rows * 4 > h) { /* too big to draw dot by dot */
        char s[24];
        q->api->snprintf(s, sizeof s, "%d rows of %d", rows, cols);
        mq_text_c(q, q->api->font_medium, x + w / 2, y + h / 2 - 7, s, Q_INK);
        return;
    }
    int cell = imin(imin(w / (cols + (split ? 1 : 0)), h / rows), 18);
    if (cell < 4)
        cell = 4;
    int shown = (rows * reveal + 999) / 1000;
    int ox = x + (w - cell * cols - (split ? cell / 2 : 0)) / 2, oy = y + (h - cell * rows) / 2;
    for (int r = 0; r < rows && r < shown; r++)
        for (int c = 0; c < cols; c++) {
            int cx = ox + c * cell + cell / 2 + (split && c >= split ? cell / 2 : 0), cy = oy + r * cell + cell / 2;
            uint16_t col = split && c >= split ? Q_SKY : Q_HOTPINK;
            if (p->v[3] == 1 && cell >= 10)
                flower(q, cx, cy, cell * 3 / 4, col == Q_SKY ? Q_LAV : Q_PINK);
            else
                mq_circle(q, cx, cy, cell / 3, col);
            q->vis_units++;
        }
    for (int k = 0; k < p->v[5] && k < 9 && rows * cell + cell <= h + cell; k++) { /* the ones left without a pair */
        int cx = ox + (cols + (split ? 1 : 0)) * cell + cell / 2 + k * cell, cy = oy + cell / 2;
        if (cx + cell / 2 <= x + w)
            mq_circle(q, cx, cy, cell / 3, Q_ORANGE), q->vis_units++;
    }
    if (cell >= 10) {
        char s[16];
        q->api->snprintf(s, sizeof s, "%d rows", rows);
        mq_text(q, q->api->font_small, x, y + h - 12, s, Q_DIM);
    }
}

/* question mode: the total as one pile (a sack with its number) and the groups as empty
 * pots labelled with what is known; a group count that is the answer is not drawn */
static void groups_q(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h)
{
    int g = p->v[0], each = p->v[1] * (p->v[3] == 10 ? 10 : 1), rem = p->v[2];
    int32_t total = p->big ? p->big : (int32_t)g * each + rem;
    char s[16], e[12];
    /* the pile */
    mq_circle(q, x + 34, y + h / 2 + 8, 26, Q_PEACH);
    mq_rrect(q, x + 24, y + h / 2 - 22, 20, 10, 4, Q_PEACH);
    if (total == p->ans)
        q->api->snprintf(s, sizeof s, "?");
    else
        q->api->snprintf(s, sizeof s, "%ld", (long)total);
    mq_text_c(q, q->api->font_medium, x + 34, y + h / 2 + 2, s, Q_INK);
    numq(q, p, e, sizeof e, "%d", each);
    if (!each && total > 0) /* (a quotient too big for v[1]: unknown, never "0" -- CR2-15) */
        q->api->snprintf(e, sizeof e, "?");
    int gx = x + 72, gw = w - 76;
    if (g == p->ans || g > 12) { /* how many groups is the question: one pot, "6 in each" */
        mq_circle(q, gx + 24, y + h / 2, 18, Q_CREAM);
        mq_ring(q, gx + 24, y + h / 2, 18, Q_INK);
        mq_text_c(q, q->api->font_medium, gx + 24, y + h / 2 - 7, e, Q_INK);
        if (gx + 46 + q->api->text_width(q->api->font_small, "in each") <= x + w)
            mq_text(q, q->api->font_small, gx + 46, y + h / 2 - 6, "in each", Q_DIM);
        else { /* (a narrow card: under the pot) */
            int tw = q->api->text_width(q->api->font_small, "in each");
            mq_text_c(q, q->api->font_small, imin(gx + 24, x + w - tw / 2 - 1), imin(y + h / 2 + 19, y + h - q->api->font_small->h),
                      "in each", Q_DIM);
        }
        return;
    }
    int per = g <= 4 ? g : (g + 1) / 2, rows = (g + per - 1) / per;
    int cw = gw / per, ch = h / rows, r = imin(cw, ch) / 2 - 2;
    for (int i = 0; i < g; i++) {
        int cx = gx + (i % per) * cw + cw / 2, cy = y + (i / per) * ch + ch / 2;
        mq_circle(q, cx, cy, r, Q_CREAM);
        mq_ring(q, cx, cy, r, Q_INK);
        if (r >= 8)
            mq_text_c(q, q->api->font_small, cx, cy - 6, e, Q_INK);
    }
}

static void v_groups(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h, int reveal)
{
    if (q->vis_q) {
        groups_q(q, p, x, y, w, h);
        return;
    }
    int g = imax(1, imin(p->v[0], 12)), each = p->v[1], rem = p->v[2];
    int per_row = g <= 4 ? g : (g + 1) / 2, rows = (g + per_row - 1) / per_row;
    int cw = w / (per_row + (rem ? 1 : 0)), ch = h / rows, r = imin(cw, ch) / 2 - 2;
    int shown = (g * reveal + 999) / 1000;
    for (int i = 0; i < g; i++) {
        int cx = x + (i % per_row) * cw + cw / 2, cy = y + (i / per_row) * ch + ch / 2;
        mq_circle(q, cx, cy, r, Q_CREAM);
        mq_ring(q, cx, cy, r, Q_INK);
        if (i >= shown)
            continue;
        if (each > 12 || p->v[3] == 10 || r < 8) {
            char s[12];
            q->api->snprintf(s, sizeof s, p->v[3] == 10 ? "%d0" : "%d", each);
            mq_text_c(q, q->api->font_medium, cx, cy - 7, s, Q_INK);
        } else
            for (int k = 0; k < each; k++) {
                int a = k * 360 / imax(each, 1);
                int rr = each == 1 ? 0 : r * 3 / 5;
                mq_circle(q, cx + mq_isin(a) * rr / 1024, cy - mq_icos(a) * rr / 1024, imax(2, r / 5), Q_HOTPINK);
                q->vis_units++;
            }
    }
    for (int k = 0; k < rem && k < 9; k++)
        mq_circle(q, x + per_row * cw + 8 + (k % 3) * 8, y + 10 + (k / 3) * 8, 3, Q_ORANGE);
}

static void v_pie(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h, int reveal)
{
    int d = imax(1, p->v[1]), n = p->v[0], pies = imax(1, imin(p->v[2], 4));
    int r = imin(h / 2 - 2, (w / pies) / 2 - 3);
    int shade = (n * reveal + 999) / 1000;
    for (int k = 0; k < pies; k++) {
        int cx = x + (w / pies) * k + (w / pies) / 2, cy = y + h / 2;
        mq_circle(q, cx, cy, r + 2, Q_BROWN);
        mq_circle(q, cx, cy, r, Q_CREAM);
        for (int s = 0; s < d; s++) {
            int idx = k * d + s;
            bool on = p->kind == K_SHADE && !k ? (p->v[5] >> s) & 1 : idx < shade;
            if (q->vis_q && p->kind != K_SHADE) /* question: the pies cut, nothing to count */
                on = false;
            if (on)
                sector(q, cx, cy, r, s * 360 / d, (s + 1) * 360 / d, Q_PINK), q->vis_units++, q->vis_shaded++;
            q->vis_parts++;
        }
        if (d > 1)
            for (int s = 0; s < d; s++)
                mq_line(q, cx, cy, cx + mq_isin(s * 360 / d) * r / 1024, cy - mq_icos(s * 360 / d) * r / 1024, Q_BROWN);
        if (p->kind == K_SHADE && p->v[4] >= 0 && p->v[4] < d && !k) { /* key focus */
            int a = p->v[4] * 360 / d + 180 / d;
            mq_ring(q, cx + mq_isin(a) * r * 2 / 3 / 1024, cy - mq_icos(a) * r * 2 / 3 / 1024, 4, Q_PURPLE);
        }
    }
}

static void bar(mq_t *q, int x, int y, int w, int h, int n, int d, int n2, int reveal)
{
    d = imax(1, d);
    mq_fill(q, x, y, w, h, Q_CREAM);
    int shade = ((n + n2) * reveal + 999) / 1000;
    for (int i = 0; i < d && i < 100; i++) {
        int x0 = x + w * i / d, x1 = x + w * (i + 1) / d;
        q->vis_parts++;
        if (i < shade)
            mq_fill(q, x0, y, x1 - x0, h, i < n ? Q_PINK : Q_SKY), q->vis_units++, q->vis_shaded += i < n;
        mq_fill(q, x0, y, 1, h, Q_BROWN);
    }
    mq_rect(q, x, y, w, h, Q_BROWN);
}

static void v_clock(mq_t *q, int cx, int cy, int r, int hh, int mm)
{
    int32_t cv[4] = {r, 0, 0, 0}; /* (a high-resolution platform may draw it from its own art: MODX_HR_SPRITE) */
    int hr = mq_hr_sprite(q, "game/clock_small", cx - r - 4, cy - r - 4, 2 * r + 8, 2 * r + 8, cv, 1);
    mq_circle(q, cx, cy, r + 3, Q_LAV);
    mq_circle(q, cx, cy, r, Q_WHITE);
    for (int i = 0; i < 60; i++) {
        int a = i * 6, l = i % 5 ? 2 : 5;
        mq_line(q, cx + mq_isin(a) * (r - l) / 1024, cy - mq_icos(a) * (r - l) / 1024, cx + mq_isin(a) * r / 1024,
                cy - mq_icos(a) * r / 1024, i % 5 ? Q_GREY : Q_INK);
    }
    if (hr)
        mq_hr_end(q);
    if (r >= 40)
        for (int i = 1; i <= 12; i++)
            numtxt(q, cx + mq_isin(i * 30) * (r - 13) / 1024, cy - mq_icos(i * 30) * (r - 13) / 1024 - 6, i, Q_INK);
    int ha = (hh % 12) * 30 + mm / 2, ma = mm * 6;
    cv[1] = ha, cv[2] = ma, cv[3] = 0;
    hr = mq_hr_sprite(q, "game/clock_hands", cx - r, cy - r, 2 * r, 2 * r, cv, 4);
    for (int t = -1; t <= 1; t++) {
        mq_line(q, cx + t, cy, cx + t + mq_isin(ha) * r / 2 / 1024, cy - mq_icos(ha) * r / 2 / 1024, Q_INK);
        mq_line(q, cx, cy + t, cx + mq_isin(ma) * (r - 6) / 1024, cy + t - mq_icos(ma) * (r - 6) / 1024, Q_HOTPINK);
    }
    mq_circle(q, cx, cy, 3, Q_INK);
    if (hr)
        mq_hr_end(q);
}

/* the coins of a value in cents, as drawn circles (q d n p + dollar bills) */
static void v_coins(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h, int reveal)
{
    static const uint8_t rad[4] = {12, 9, 10, 9};
    static const char lab[4][4] = {"25", "10", "5", "1"};
    int cx = x + 4, cy = y + 4, shown = 0, total = p->v[0] + p->v[1] + p->v[2] + p->v[3] + p->v[4];
    if (q->vis_q) { /* a question about change or cents: the till, not the coins of the answer */
        mq_rrect(q, x + w / 2 - 40, y + 20, 80, 50, 8, Q_LAV);
        mq_rrect(q, x + w / 2 - 30, y + 8, 60, 16, 4, Q_SKY);
        mq_fill(q, x + w / 2 - 34, y + 50, 68, 3, Q_PURPLE);
        mq_text_c(q, q->api->font_large, x + w / 2, y + 26, "$", Q_INK);
        return;
    }
    int lim = (total * reveal + 999) / 1000;
    for (int k = 0; k < p->v[4] && shown < lim; k++, shown++) {
        mq_fill(q, cx, cy, 44, 22, Q_MINT);
        mq_rect(q, cx, cy, 44, 22, Q_GREEN);
        mq_text_c(q, q->api->font_small, cx + 22, cy + 5, "$1", Q_INK);
        cx += 48;
        if (cx > x + w - 44)
            cx = x + 4, cy += 26;
    }
    for (int t = 0; t < 4; t++)
        for (int k = 0; k < p->v[t] && shown < lim; k++, shown++) {
            int r = rad[t];
            q->vis_units++;
            if (cx + 2 * r > x + w)
                cx = x + 4, cy += 28;
            if (cy + 2 * r > y + h)
                return;
            mq_circle(q, cx + r, cy + r, r, t == 3 ? Q_ORANGE : Q_GREY);
            mq_ring(q, cx + r, cy + r, r, Q_DIM);
            mq_text_c(q, q->api->font_small, cx + r, cy + r - 6, lab[t], Q_INK);
            cx += 2 * r + 4;
        }
}

static void v_grid(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h, int reveal)
{
    int gw = imax(1, p->v[0]), gh = imax(1, p->v[1]), mode = p->v[2];
    char s[16];
    if (q->vis_q && mode >= 1 && mode <= 3) { /* question: the rectangle and its sides, no unit squares */
        int cell = imin(imin((w - 24) / gw, (h - 16) / gh), 18), ox = x + 20 + (w - 24 - cell * gw) / 2, oy = y + 2;
        if (mode == 3) {
            mq_fill(q, ox, oy, cell * p->v[3], cell * gh, Q_PINK);
            mq_fill(q, ox + cell * p->v[3], oy, cell * (gw - p->v[3]), cell * gh, Q_SKY);
            numq(q, p, s, sizeof s, "%d", p->v[3]);
            mq_text_c(q, q->api->font_small, ox + cell * p->v[3] / 2, oy + cell * gh / 2 - 6, s, Q_INK);
            numq(q, p, s, sizeof s, "%d", gw - p->v[3]);
            mq_text_c(q, q->api->font_small, ox + cell * p->v[3] + cell * (gw - p->v[3]) / 2, oy + cell * gh / 2 - 6, s,
                      Q_INK);
        } else
            mq_fill(q, ox, oy, cell * gw, cell * gh, mode == 2 ? Q_CREAM : Q_MINT);
        for (int t = 0; t < (mode == 2 ? 3 : 1); t++)
            mq_rect(q, ox - t, oy - t, cell * gw + 1 + 2 * t, cell * gh + 1 + 2 * t, mode == 2 ? Q_HOTPINK : Q_INK);
        numq(q, p, s, sizeof s, "%d", gw);
        mq_text_c(q, q->api->font_small, ox + cell * gw / 2, oy + cell * gh + 2, s, Q_INK);
        numq(q, p, s, sizeof s, "%d", gh);
        mq_text(q, q->api->font_small, ox - 16, oy + cell * gh / 2 - 6, s, Q_INK);
        return;
    }
    if (q->vis_q && mode == 5 && p->skill == S_TENTHS) { /* tenths as ten strips: no hundredths to count */
        int sw = (w - 24) / 10, ox = x + 12;
        for (int c = 0; c < 10; c++) {
            mq_fill(q, ox + c * sw, y + 4, sw, h - 10, c < p->v[3] / 10 ? Q_PINK : Q_CREAM); /* (the amount in hundredths, as
                                                                                         * tenths: CR2-13) */
            mq_rect(q, ox + c * sw, y + 4, sw + 1, h - 10, Q_DIM);
        }
        return;
    }
    if (mode == 6) { /* two hundredths grids side by side, each shaded (comparing decimals) */
        int gw2 = (w - 12) / 2, cell = imin(gw2 / 10, (h - 4) / 10);
        if (cell < 2)
            cell = 2;
        for (int g = 0; g < 2; g++) {
            int ox = x + 4 + g * (gw2 + 4), oy = y + 2, sh = g ? p->v[4] : p->v[3];
            for (int c = 0; c < 100; c++) {
                int cx = c / 10, cy = c % 10; /* (filled column by column: tenths are columns) */
                mq_fill(q, ox + cx * cell, oy + cy * cell, cell, cell, c < sh ? (g ? Q_SKY : Q_PINK) : Q_CREAM);
                q->vis_units = (uint16_t)(q->vis_units + (c < sh));
            }
            mq_rect(q, ox, oy, cell * 10 + 1, cell * 10 + 1, Q_INK);
            for (int k = 1; k < 10 && cell >= 4; k++)
                mq_fill(q, ox + k * cell, oy, 1, cell * 10, Q_DIM);
        }
        return;
    }
    if (mode == 4) { /* area model of a multiplication: place-value parts */
        int a = p->a[0], b = p->a[1];
        int at = a / 10 * 10, ao = a % 10, bt = b > 9 ? b / 10 * 10 : b, bo = b > 9 ? b % 10 : 0;
        int W = w - 30, H = h - 20, wx = at ? W * 3 / 4 : 0, hy = bo ? H * 3 / 4 : H;
        mq_fill(q, x + 30, y + 14, wx, hy, Q_PINK);
        mq_fill(q, x + 30 + wx, y + 14, W - wx, hy, Q_SKY);
        if (bo) {
            mq_fill(q, x + 30, y + 14 + hy, wx, H - hy, Q_LAV);
            mq_fill(q, x + 30 + wx, y + 14 + hy, W - wx, H - hy, Q_MINT);
        }
        mq_rect(q, x + 30, y + 14, W, H, Q_INK);
        q->api->snprintf(s, sizeof s, "%d", at ? at : a);
        mq_text_c(q, q->api->font_small, x + 30 + (wx ? wx : W) / 2, y, s, Q_INK);
        if (at) {
            q->api->snprintf(s, sizeof s, "%d", ao);
            mq_text_c(q, q->api->font_small, x + 30 + wx + (W - wx) / 2, y, s, Q_INK);
        }
        q->api->snprintf(s, sizeof s, "%d", bt);
        mq_text(q, q->api->font_small, x, y + 14 + hy / 2 - 6, s, Q_INK);
        if (bo) {
            q->api->snprintf(s, sizeof s, "%d", bo);
            mq_text(q, q->api->font_small, x, y + 14 + hy + (H - hy) / 2 - 6, s, Q_INK);
        }
        return;
    }
    int cell = imin((w - 24) / gw, (h - 16) / gh);
    if (cell > 18)
        cell = 18;
    if (cell < 3)
        cell = 3;
    int ox = x + 20 + (w - 24 - cell * gw) / 2, oy = y + 2;
    int fill = (gw * gh * reveal + 999) / 1000;
    for (int r = 0; r < gh; r++)
        for (int c = 0; c < gw; c++) {
            int idx = r * gw + c;
            uint16_t col = Q_CREAM;
            if (mode == 0 || mode == 1)
                col = idx < fill ? Q_MINT : Q_CREAM;
            if (mode == 3)
                col = c < p->v[3] ? Q_PINK : Q_SKY;
            if (mode == 5 && q->vis_asked && p->skill == S_FRACMUL && p->v[4]) /* a fraction of a fraction, asked: the first
                                                * fraction's band only (the tulips' overlap is the answer: CR2-17) */
                col = c < p->v[4] ? Q_MINT : Q_CREAM;
            else if (mode == 5) {
                col = idx < (p->v[3] * reveal + 999) / 1000 ? Q_PINK : Q_CREAM;
                q->vis_parts++, q->vis_shaded += col == Q_PINK;
            }
            mq_fill(q, ox + c * cell, oy + r * cell, cell, cell, col);
            if (mode != 2 && cell >= 5)
                mq_rect(q, ox + c * cell, oy + r * cell, cell + 1, cell + 1, mode == 1 ? Q_GREY : Q_DIM);
            if (mode == 5 ? col == Q_PINK : mode != 2)
                q->vis_units++;
        }
    uint16_t edge = mode == 2 ? Q_HOTPINK : Q_INK;
    for (int t = 0; t < (mode == 2 ? 3 : 1); t++)
        mq_rect(q, ox - t, oy - t, cell * gw + 1 + 2 * t, cell * gh + 1 + 2 * t, edge);
    if (mode >= 1 && mode <= 3) {
        q->api->snprintf(s, sizeof s, "%d", gw);
        mq_text_c(q, q->api->font_small, ox + cell * gw / 2, oy + cell * gh + 2, s, Q_INK);
        q->api->snprintf(s, sizeof s, "%d", gh);
        mq_text(q, q->api->font_small, ox - 16, oy + cell * gh / 2 - 6, s, Q_INK);
    }
}

static void v_lshape(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h, int reveal)
{
    int w1 = p->v[0], h1 = p->v[1], w2 = p->v[2], h2 = p->v[3];
    int cell = imin((w - 30) / imax(w1, 1), (h - 20) / imax(h1 + h2, 1));
    if (cell > 16)
        cell = 16;
    int ox = x + 24, oy = y + 4;
    char s[8];
    bool lines = !q->vis_q; /* a question shows the shape and its sides, no squares to count */
    for (int r = 0; r < h2; r++)
        for (int c = 0; c < w2; c++) {
            mq_fill(q, ox + c * cell, oy + r * cell, cell, cell, reveal > 500 && lines ? Q_SKY : Q_MINT);
            if (lines)
                mq_rect(q, ox + c * cell, oy + r * cell, cell + 1, cell + 1, Q_DIM), q->vis_units++;
        }
    for (int r = 0; r < h1; r++)
        for (int c = 0; c < w1; c++) {
            mq_fill(q, ox + c * cell, oy + (h2 + r) * cell, cell, cell, Q_MINT);
            if (lines)
                mq_rect(q, ox + c * cell, oy + (h2 + r) * cell, cell + 1, cell + 1, Q_DIM), q->vis_units++;
        }
    if (!lines) {
        mq_rect(q, ox, oy, w2 * cell + 1, h2 * cell + 1, Q_INK);
        mq_rect(q, ox, oy + h2 * cell, w1 * cell + 1, h1 * cell + 1, Q_INK);
    }
    q->api->snprintf(s, sizeof s, "%d", w1);
    mq_text_c(q, q->api->font_small, ox + w1 * cell / 2, oy + (h1 + h2) * cell + 2, s, Q_INK);
    q->api->snprintf(s, sizeof s, "%d", h1);
    mq_text(q, q->api->font_small, ox - 16, oy + h2 * cell + h1 * cell / 2 - 6, s, Q_INK);
    q->api->snprintf(s, sizeof s, "%d", h2);
    mq_text(q, q->api->font_small, ox - 16, oy + h2 * cell / 2 - 6, s, Q_INK);
    q->api->snprintf(s, sizeof s, "%d", w2);
    mq_text_c(q, q->api->font_small, ox + w2 * cell / 2, oy - 12 < y ? oy + 2 : oy - 12, s, Q_INK);
}

static void v_angle(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h, int reveal)
{
    if (p->v[3] == 1) { /* it opens from the left: the picture mirrored (the protractor's other scale) */
        bool f = q->flipx;
        int fc = q->fcx;
        mq_prob_t m = *p;
        m.v[3] = 2;
        q->flipx = !f, q->fcx = (int16_t)(x + w / 2);
        v_angle(q, &m, x, y, w, h, reveal);
        q->flipx = f, q->fcx = (int16_t)fc;
        return;
    }
    int cx = x + w / 2, cy = y + h - 12, r = imin(w / 2 - 8, h - 20);
    if (p->v[2])
        r = imin(w / 2 - 26, h - 34); /* room for the outer scale */
    int deg = (p->v[0] * reveal + 999) / 1000; /* (a question's 999: the whole angle, without the corner mark) */
    if (p->v[2]) { /* protractor */
        sector(q, cx, cy, r + 8, 270, 360, Q_CREAM);
        sector(q, cx, cy, r + 8, 0, 90, Q_CREAM);
        for (int a = 0; a <= 180; a += 10) {
            int l = a % 30 ? 4 : 8;
            mq_line(q, cx - mq_icos(a) * (r + 8) / 1024, cy - mq_isin(a) * (r + 8) / 1024,
                    cx - mq_icos(a) * (r + 8 - l) / 1024, cy - mq_isin(a) * (r + 8 - l) / 1024, Q_DIM);
            if (a % 30 == 0 && r > 40) /* one scale, from the angle's own side (audit C4) */
                numtxt(q, cx - mq_icos(a) * (r + 2) / 1024, cy - mq_isin(a) * (r + 2) / 1024 - 6, a, Q_INK);
        }
    }
    if (p->v[1] > 0 && p->v[1] < p->v[0]) { /* two parts */
        sector(q, cx, cy, r / 3, 270, 270 + imin(p->v[1], deg), Q_PINK);
        if (deg > p->v[1])
            sector(q, cx, cy, r / 3, 270 + p->v[1], 270 + deg, Q_SKY);
        mq_line(q, cx, cy, cx - mq_icos(p->v[1]) * r / 1024, cy - mq_isin(p->v[1]) * r / 1024, Q_PURPLE);
    } else if (deg > 0)
        sector(q, cx, cy, r / 3, 270, 270 + deg, Q_PINK);
    for (int t = 0; t < 2; t++) {
        mq_line(q, cx, cy + t, cx + r, cy + t, Q_INK);
        mq_line(q, cx, cy + t, cx - mq_icos(deg) * r / 1024, cy + t - mq_isin(deg) * r / 1024, Q_INK);
    }
    if (p->v[0] == 90 && reveal >= 1000)
        mq_rect(q, cx - 10, cy - 10, 10, 10, Q_INK);
}

/* shapes: 0 square, 1 rectangle, 2 rhombus, 3 trapezoid, 4 parallelogram, 5 right triangle,
 * 6 acute triangle, 7 obtuse triangle, 8 kite, 9 pentagon, 10 hexagon */
static void v_shape(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h)
{
    int cx = x + w / 2, cy = y + h / 2, s = imin(w, h) / 2 - 6;
    int16_t px[8], py[8];
    int n = 4, k = p->v[0];
    switch (k) {
    case 0:
        px[0] = (int16_t)(cx - s), py[0] = (int16_t)(cy - s), px[1] = (int16_t)(cx + s), py[1] = (int16_t)(cy - s);
        px[2] = (int16_t)(cx + s), py[2] = (int16_t)(cy + s), px[3] = (int16_t)(cx - s), py[3] = (int16_t)(cy + s);
        break;
    case 1:
        px[0] = (int16_t)(cx - s * 3 / 2), py[0] = (int16_t)(cy - s * 2 / 3), px[1] = (int16_t)(cx + s * 3 / 2),
        py[1] = py[0];
        px[2] = px[1], py[2] = (int16_t)(cy + s * 2 / 3), px[3] = px[0], py[3] = py[2];
        break;
    case 2:
    case 8:
        px[0] = (int16_t)cx, py[0] = (int16_t)(cy - s), px[1] = (int16_t)(cx + s * 2 / 3), py[1] = (int16_t)(k == 8 ? cy - s / 3 : cy);
        px[2] = (int16_t)cx, py[2] = (int16_t)(cy + s), px[3] = (int16_t)(cx - s * 2 / 3), py[3] = py[1];
        break;
    case 3:
        px[0] = (int16_t)(cx - s / 2), py[0] = (int16_t)(cy - s / 2), px[1] = (int16_t)(cx + s / 2), py[1] = py[0];
        px[2] = (int16_t)(cx + s * 3 / 2), py[2] = (int16_t)(cy + s / 2), px[3] = (int16_t)(cx - s * 3 / 2), py[3] = py[2];
        break;
    case 4:
        px[0] = (int16_t)(cx - s / 2), py[0] = (int16_t)(cy - s / 2), px[1] = (int16_t)(cx + s * 3 / 2), py[1] = py[0];
        px[2] = (int16_t)(cx + s / 2), py[2] = (int16_t)(cy + s / 2), px[3] = (int16_t)(cx - s * 3 / 2), py[3] = py[2];
        break;
    case 5:
    case 6:
    case 7:
        n = 3;
        px[0] = (int16_t)(cx - s), py[0] = (int16_t)(cy + s * 2 / 3), px[1] = (int16_t)(cx + s), py[1] = py[0];
        px[2] = (int16_t)(k == 5 ? cx - s : k == 6 ? cx + s / 4 : cx + s * 2), py[2] = (int16_t)(cy - s * 2 / 3);
        if (k == 7)
            px[1] = (int16_t)(cx), px[0] = (int16_t)(cx - s * 3 / 2);
        break;
    default:
        n = k == 9 ? 5 : 6;
        for (int i = 0; i < n; i++)
            px[i] = (int16_t)(cx + mq_isin(i * 360 / n) * s / 1024), py[i] = (int16_t)(cy - mq_icos(i * 360 / n) * s / 1024);
        break;
    }
    poly(q, px, py, n, k >= 5 && k <= 7 ? Q_SKY : Q_LAV);
    for (int i = 0; i < n; i++)
        mq_line(q, px[i], py[i], px[(i + 1) % n], py[(i + 1) % n], Q_INK);
    if (k == 5)
        mq_rect(q, px[0], py[0] - 8, 8, 8, Q_INK);
    int ln = p->v[2];
    if (ln >= 1 && ln <= 3) { /* candidate mirror line, dashed */
        int x0 = ln == 1 ? cx : ln == 2 ? cx - s * 2 : cx - s, y0 = ln == 1 ? cy - s - 6 : ln == 2 ? cy : cy - s;
        int x1 = ln == 1 ? cx : ln == 2 ? cx + s * 2 : cx + s, y1 = ln == 1 ? cy + s + 6 : ln == 2 ? cy : cy + s;
        if (k == 1 && ln == 3)
            x0 = px[0], y0 = py[0], x1 = px[2], y1 = py[2];
        for (int t = 0; t < 12; t += 2)
            mq_line(q, x0 + (x1 - x0) * t / 12, y0 + (y1 - y0) * t / 12, x0 + (x1 - x0) * (t + 1) / 12,
                    y0 + (y1 - y0) * (t + 1) / 12, Q_HOTPINK);
    }
}

static void v_bargraph(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h, int reveal)
{
    static const uint16_t col[4] = {Q_HOTPINK, Q_GREEN, Q_PURPLE, Q_ORANGE};
    static const char lab[4][7] = {"apple", "pear", "plum", "grape"};
    int n = imin(p->v[0], 4), scale = imax(1, p->v[1]), maxv = 0;
    for (int i = 0; i < n; i++)
        maxv = imax(maxv, p->v[2 + i]);
    int top = (maxv / scale + 1) * scale, gx = x + 26, gw = w - 30, gh = h - 26;
    char s[16];
    int fh = q->api->font_small->h, bw = gw / imax(n, 1), nlw = 0;
    for (int i = 0; i < n; i++)
        nlw = imax(nlw, q->api->text_width(q->api->font_small, lab[i]));
    bool two = nlw > bw - 2; /* the names side by side would run together: two rows, every other one a line lower */
    if (two)
        gh = h - 2 * fh - 3;
    if (p->v[6]) { /* picture graph: one star per `scale` (a half star for half of it), the key always in its
                    * own line under the rows, a star and "= 2" in a pill (the playtest: "only 5 stars when
                    * the answer is 10": the key was small, under the last row) */
        int keyh = 15, rh = imax(8, (h - keyh - 2) / imax(n, 1)), lw = 0, maxk = 1;
        for (int i = 0; i < n; i++)
            lw = imax(lw, q->api->text_width(q->api->font_small, lab[i])), maxk = imax(maxk, (p->v[2 + i] + scale - 1) / scale);
        int sx0 = x + lw + 6, step = imin(16, (x + w - sx0 - 2) / maxk), r = imax(2, imin(7, imin(rh / 2 - 1, step / 2)));
        for (int i = 0; i < n; i++) {
            int cy = y + i * rh + rh / 2;
            mq_text(q, q->api->font_small, x, cy - 6, lab[i], Q_INK);
            int whole = p->v[2 + i] / scale, half = p->v[2 + i] % scale ? 1 : 0;
            int k = ((whole + half) * reveal + 999) / 1000; /* (ceiling: a question's frame is never one short, CR-01) */
            for (int j = 0; j < k; j++) {
                int sx = sx0 + r + j * step;
                mq_star(q, sx, cy, r, col[i]), q->vis_units++;
                if (j == whole) /* the half: the star's right half covered */
                    mq_fill(q, sx, cy - r - 1, r + 2, 2 * r + 3, Q_WHITE);
            }
        }
        int ky = y + h - keyh; /* the key */
        q->api->snprintf(s, sizeof s, "= %d", scale);
        int kw = 26 + q->api->text_width(q->api->font_small, s) + 8;
        mq_rrect(q, x, ky, kw, keyh - 1, 6, scale > 1 ? Q_YELLOW : Q_CREAM);
        mq_rect(q, x, ky, kw, keyh - 1, Q_DIM);
        mq_text(q, q->api->font_small, x + 4, ky + 1, "key", Q_DIM);
        mq_star(q, x + 30 - 4 + 2, ky + keyh / 2, 5, col[0]);
        mq_text(q, q->api->font_small, x + 36, ky + 1, s, Q_INK);
        q->graph_key = 1;
        return;
    }
    int last = 1 << 14; /* the last label's top: none closer than a line above it */
    for (int t = 0; t <= top; t += scale) {
        int yy = y + gh - gh * t / top, ly = imax(y, yy - 6); /* (the top label: inside the box) */
        mq_fill(q, gx, yy, gw, 1, Q_GREY);
        if ((t / scale) % (top / scale > 8 ? 2 : 1) == 0 && last - ly >= q->api->font_small->h) {
            q->api->snprintf(s, sizeof s, "%d", t);
            mq_text(q, q->api->font_small, x, ly, s, Q_DIM);
            last = ly;
        }
    }
    for (int i = 0; i < n; i++) {
        int bh = (gh * p->v[2 + i] / top * reveal + 999) / 1000;
        mq_fill(q, gx + i * bw + 4, y + gh - bh, bw - 8, bh, col[i]), q->vis_units++;
        int lx = gx + i * bw + bw / 2, hw = q->api->text_width(q->api->font_small, lab[i]) / 2;
        lx = imin(imax(lx, x + hw + 1), x + w - hw - 1); /* (inside the panel) */
        mq_text_c(q, q->api->font_small, lx, y + gh + 2 + (two && i % 2 ? fh : 0), lab[i], Q_INK);
    }
}

/* a line plot of ribbon lengths 4/4 .. 9/4: the tick labels fitted to the room (in two staggered rows when
 * side by side they would run together: the render audit's H5), the axis title fitted to the panel */
static void v_lineplot(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h)
{
    const font_t *fs = q->api->font_small;
    int step = (w - 20) / 6, lw = q->api->text_width(fs, "9/4") + 3, fh = fs->h;
    bool two = lw > step; /* labels alternate between two rows */
    int ax = y + h - (two ? 3 * fh + 10 : 2 * fh + 10); /* (the labels, then the title under them) */
    mq_fill(q, x + 6, ax, w - 12, 2, Q_INK);
    int top = 0; /* the tallest column's crosses stay inside the panel */
    for (int i = 1; i <= 6; i++)
        top = top > p->v[i] ? top : p->v[i];
    int dk = top > 0 && ax - 16 - (top - 1) * 13 < y + 2 ? (ax - 18 - y) / (top > 1 ? top - 1 : 1) : 13;
    for (int i = 1; i <= 6; i++) {
        int cx = x + 10 + (i - 1) * step + step / 2;
        mq_fill(q, cx, ax - 3, 1, 8, Q_INK);
        char s[8];
        q->api->snprintf(s, sizeof s, "%d/4", i + 3);
        mq_text_c(q, fs, cx, ax + 6 + (two && i % 2 == 0 ? fh - 1 : 0), s, Q_INK);
        for (int k = 0; k < p->v[i]; k++) { /* a drawn cross: stacked closer when the column is tall, never one on another */
            int r = imax(2, imin(4, dk / 2 - 1)), yy = ax - 10 - k * dk;
            for (int t = 0; t < 2; t++) {
                mq_line(q, cx - r + t, yy - r, cx + r + t, yy + r, Q_HOTPINK);
                mq_line(q, cx - r + t, yy + r, cx + r + t, yy - r, Q_HOTPINK);
            }
            q->vis_units++;
        }
    }
    mq_text_fit(q, fs, x + w / 2, y + h - fh - 1, w - 8, "length (inches)", Q_DIM, true);
}

static void v_table(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h)
{
    int rh = imin(18, h / 5);
    mq_fill(q, x, y, w, rh, Q_LAV);
    mq_text(q, q->api->font_small, x + 6, y + 3, "friend", Q_INK);
    mq_text(q, q->api->font_small, x + w / 2 + 6, y + 3, "tickets", Q_INK);
    for (int i = 0; i < 4; i++) {
        int yy = y + rh * (i + 1);
        mq_fill(q, x, yy, w, rh, i % 2 ? Q_CREAM : Q_WHITE);
        mq_text_fit(q, q->api->font_small, x + 6, yy + 3, w / 2 - 10, mq_cast_name(q, i), Q_INK, false); /* (a long name: fitted to its column, never over the number: the render audit's H6) */
        char s[8];
        q->api->snprintf(s, sizeof s, "%d", p->v[1 + i]);
        mq_text(q, q->api->font_small, x + w / 2 + 6, yy + 3, s, Q_INK);
    }
    mq_rect(q, x, y, w, rh * 5, Q_DIM);
    mq_fill(q, x + w / 2, y, 1, rh * 5, Q_DIM);
}

static void v_coord(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h)
{
    int cell = imin((w - 20) / 9, (h - 18) / 7), ox = x + 16, oy = y + 4 + cell * 6;
    for (int i = 0; i <= 8; i++) {
        mq_fill(q, ox + i * cell, oy - cell * 6, 1, cell * 6, Q_GREY);
        numtxt(q, ox + i * cell, oy + 2, i, Q_DIM);
    }
    for (int j = 0; j <= 6; j++) {
        mq_fill(q, ox, oy - j * cell, cell * 8, 1, Q_GREY);
        if (j && (j % 2 == 0 || cell >= 13))
            numtxt(q, ox - 8, oy - j * cell - 6, j, Q_DIM);
    }
    mq_fill(q, ox, oy, cell * 8, 2, Q_INK);
    mq_fill(q, ox, oy - cell * 6, 2, cell * 6, Q_INK);
    mq_star(q, ox + p->v[0] * cell, oy - p->v[1] * cell, 7, Q_HOTPINK);
}

static void poly4(mq_t *q, int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint16_t c)
{
    int16_t px[4] = {(int16_t)x0, (int16_t)x1, (int16_t)x2, (int16_t)x3}, py[4] = {(int16_t)y0, (int16_t)y1, (int16_t)y2, (int16_t)y3};
    poly(q, px, py, 4, c);
    for (int i = 0; i < 4; i++)
        mq_line(q, px[i], py[i], px[(i + 1) % 4], py[(i + 1) % 4], Q_INK);
}

static void v_prism(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h, int reveal)
{
    if (p->v[3] > 0) { /* two boxes, side by side */
        mq_prob_t b1 = *p, b2 = *p;
        b1.v[3] = 0, b2.v[0] = p->v[3], b2.v[1] = p->v[4], b2.v[2] = p->v[5], b2.v[3] = 0;
        v_prism(q, &b1, x, y, w / 2 - 2, h, reveal);
        int u = q->vis_units;
        v_prism(q, &b2, x + w / 2 + 2, y, w / 2 - 2, h, reveal);
        q->vis_units = (uint16_t)(q->vis_units + u);
        return;
    }
    int l = p->v[0], wd = p->v[1], ht = p->v[2];
    int c = imin(w / (l + wd / 2 + 2), h / (ht + wd / 2 + 2));
    if (c > 14)
        c = 14;
    int dx = c / 2, dy = c / 2, ox = x + 8, oy = y + h - 8 - c;
    int layers = (ht * reveal + 999) / 1000;
    if (q->vis_q) { /* question: the box and its edges labelled, no cubes to count */
        int fh = q->api->font_small->h;
        c = imin(c, imin((w - 22) / (l + wd / 2 + 1), (h - fh - 4) / (ht + wd / 2 + 1))); /* (the labels inside the card) */
        c = imax(c, 2), dx = c / 2, dy = c / 2, oy = y + h - fh - 2 - c;
        char s[12];
        int bw = l * c, bh = ht * c, dd = wd * dx;
        mq_fill(q, ox, oy + c - bh, bw, bh, Q_LAV);
        poly4(q, ox, oy + c - bh, ox + bw, oy + c - bh, ox + bw + dd, oy + c - bh - dd, ox + dd, oy + c - bh - dd, Q_SKY);
        poly4(q, ox + bw, oy + c, ox + bw, oy + c - bh, ox + bw + dd, oy + c - bh - dd, ox + bw + dd, oy + c - dd, Q_PINK);
        q->api->snprintf(s, sizeof s, "%d", l);
        mq_text_c(q, q->api->font_small, ox + bw / 2, oy + c + 1, s, Q_INK);
        q->api->snprintf(s, sizeof s, "%d", ht);
        mq_text(q, q->api->font_small, ox + bw + dd + 3, oy + c - dd - bh / 2 - 6, s, Q_INK);
        q->api->snprintf(s, sizeof s, "%d", wd);
        int wy = oy + c - dd / 2 - 2, hy = oy + c - dd - bh / 2 - 6;
        if (wy - hy < fh) /* (a small box: the depth beside the length, under it) */
            wy = oy + c + 1;
        mq_text(q, q->api->font_small, ox + bw + dd / 2 + 4, wy, s, Q_DIM);
        return;
    }
    for (int k = 0; k < layers; k++)
        for (int j = wd - 1; j >= 0; j--)
            for (int i = 0; i < l; i++) {
                int px0 = ox + i * c + j * dx, py0 = oy - k * c - j * dy;
                mq_fill(q, px0, py0, c, c, (i + j + k) % 2 ? Q_SKY : Q_LAV);
                mq_rect(q, px0, py0, c + 1, c + 1, Q_INK);
                q->vis_units++;
            }
}

static void v_pv(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h)
{
    static const char nm[6][8] = {"1s", "10s", "100s", "1Ks", "10Ks", "100Ks"};
    int32_t n = p->big;
    int places = 1;
    for (int32_t t = n; t >= 10; t /= 10)
        places++;
    if (p->v[0] == -2) { /* decimals: ones . tenths hundredths thousandths */
        static const char dn[4][8] = {"ones", "10ths", "100ths", "1000s"};
        int cw = w / 4;
        for (int i = 0; i < 4; i++) {
            mq_fill(q, x + i * cw + 2, y + 4, cw - 4, 22, i ? Q_SKY : Q_PINK);
            mq_text_fit(q, q->api->font_small, x + i * cw + cw / 2, y + 8, cw - 5, dn[i], Q_INK, true);
        }
        mq_circle(q, x + cw, y + 50, 3, Q_INK);
        return;
    }
    if (places < 3)
        places = 3;
    int cw = w / places;
    for (int i = 0; i < places; i++) {
        int pl = places - 1 - i;
        bool hi = p->v[0] > 0 && pl == p->v[0];
        mq_fill(q, x + i * cw + 1, y + 4, cw - 2, 18, hi ? Q_YELLOW : pl % 3 == 0 ? Q_PINK : pl % 3 == 1 ? Q_MINT : Q_SKY);
        mq_text_fit(q, q->api->font_small, x + i * cw + cw / 2, y + 6, cw - 3, nm[pl < 6 ? pl : 5], Q_INK, true); /* (fitted: six places in a card) */
        int32_t dv = n;
        for (int k = 0; k < pl; k++)
            dv /= 10;
        char s[4] = {(char)('0' + dv % 10), 0};
        mq_text_c(q, q->api->font_xlarge, x + i * cw + cw / 2, y + 30, s, Q_INK);
    }
    (void)h;
}

/* question mode for adding and subtracting: the numbers lined up by place, the answer "?" */
static void column_sum(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h)
{
    int k = h < 76 ? 4 : 0; /* (a short card: the lines closer) */
    int32_t a = p->a[0], b = p->a[1];
    if (p->big > 30000)
        a = p->big;
    if (p->op[1] > 30000 && p->op[1] % 30000 == b) /* (a[] keeps them mod 30000: the true numbers -- CR2-15) */
        b = p->op[1];
    if (p->op[0] > 30000 && p->op[0] % 30000 == p->a[0])
        a = p->op[0];
    char s[24];
    int rx = x + w / 2 + 40;
    q->api->snprintf(s, sizeof s, "%ld", (long)a);
    mq_text(q, q->api->font_large, rx - q->api->text_width(q->api->font_large, s), y + 6 - k - 1, s, Q_INK);
    q->api->snprintf(s, sizeof s, "%s %ld", p->a[2] ? "\xE2\x88\x92" : "+", (long)b);
    mq_text(q, q->api->font_large, rx - q->api->text_width(q->api->font_large, s), y + 26 - k, s, Q_INK);
    mq_fill(q, rx - 80, y + 48 - 5 * k / 4, 82, 2, Q_INK);
    mq_text(q, q->api->font_large, rx - 12, y + 52 - 7 * k / 4, "?", Q_HOTPINK);
}

/* the base-ten blocks of n laid out in w x h at u pixels a small cube: thousands (cubes), hundreds
 * (flats, up to three a row), tens (rods), ones (in threes); the size it needs (draw: false) */
static void blocks(mq_t *q, int32_t n, int x, int y, int u, int lim, bool draw, int *bw, int *bh)
{
    int th = (int)(n / 1000 % 10), hu = (int)(n / 100 % 10), te = (int)(n / 10 % 10), on = (int)(n % 10);
    int f = 10 * u, cx = x, k = 0, rows = hu ? (hu + 2) / 3 : 0, hcols = hu > 3 ? 3 : hu;
    for (int i = 0; i < th; i++, k++) {
        if (draw && k < lim)
            mq_fill(q, cx, y, f, f, Q_PURPLE), mq_rect(q, cx, y, f, f, Q_INK), mq_fill(q, cx + u, y + u, f - 2 * u, 2, Q_LAV);
        cx += f + 3;
    }
    for (int i = 0; i < hu; i++, k++)
        if (draw && k < lim) {
            int fx = cx + (i % 3) * (f + 2), fy = y + (i / 3) * (f + 2);
            mq_fill(q, fx, fy, f, f, Q_PINK);
            for (int g = 1; g < 10 && u >= 2; g++)
                mq_fill(q, fx + g * u, fy, 1, f, Q_HOTPINK), mq_fill(q, fx, fy + g * u, f, 1, Q_HOTPINK);
            mq_rect(q, fx, fy, f, f, Q_HOTPINK);
        }
    if (hu)
        cx += hcols * (f + 2) + 3;
    for (int i = 0; i < te; i++, k++) {
        if (draw && k < lim)
            mq_fill(q, cx, y, u, f, Q_SKY), mq_rect(q, cx, y, u + 1, f, Q_BLUE);
        cx += u + 2;
    }
    if (te)
        cx += 3;
    for (int i = 0; i < on; i++, k++)
        if (draw && k < lim)
            mq_fill(q, cx + (i % 3) * (u + 1), y + f - u - (i / 3) * (u + 1), u, u, Q_ORANGE);
    if (on)
        cx += 3 * (u + 1);
    *bw = cx - x, *bh = (rows > 1 ? rows : 1) * (f + 2);
}
/* the largest cube size (3..1 px) at which the blocks of n fit; 0: they do not (the place chart then) */
int mq_base10_unit(mq_t *q, int32_t n, int w, int h)
{
    if (n >= 10000 || n < 0)
        return 0;
    for (int u = 3; u >= 1; u--) {
        int bw, bh;
        blocks(q, n, 0, 0, u, 0, false, &bw, &bh);
        if (bw <= w && bh <= h && (u >= 2 || n < 1000))
            return u;
    }
    return 0;
}

static void v_base10(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h, int reveal)
{
    int32_t n = p->big;
    if (q->vis_q && (p->skill == S_ADDSUB || p->skill == S_ADDSUBBIG)) {
        column_sum(q, p, x, y, w, h);
        return;
    }
    bool two = p->skill == S_CMP3 && p->v[2] > 0; /* a comparison: both numbers, one above the other */
    int bh = two ? (h - 4) / 2 : h - 4;
    int u = mq_base10_unit(q, n, w - (two ? 30 : 0), bh - (two ? 0 : 14));
    if (two)
        u = u && mq_base10_unit(q, p->v[2], w - 30, bh) ? (u < mq_base10_unit(q, p->v[2], w - 30, bh) ? u : mq_base10_unit(q, p->v[2], w - 30, bh)) : 0;
    if (!u && two) { /* (two that would not fit: the numbers lined up by place) */
        char s[12];
        int rx = x + w / 2 + 30;
        q->api->snprintf(s, sizeof s, "%ld", (long)n);
        mq_text(q, q->api->font_large, rx - q->api->text_width(q->api->font_large, s), y + 4, s, Q_INK);
        q->api->snprintf(s, sizeof s, "%ld", (long)p->v[2]);
        mq_text(q, q->api->font_large, rx - q->api->text_width(q->api->font_large, s), y + 4 + bh, s, Q_INK);
        return;
    }
    if (!u) { /* they would not fit: never a partial model -- the places with their digits */
        v_pv(q, p, x, y, w, h);
        return;
    }
    int th = (int)(n / 1000 % 10), hu = (int)(n / 100 % 10), te = (int)(n / 10 % 10), on = (int)(n % 10);
    int lim = (th + hu + te + on) * reveal / 1000 + 1, bw, bh2;
    q->vis_units = (uint16_t)(th + hu + te + on);
    if (two) {
        char s[12];
        int32_t m = p->v[2];
        q->api->snprintf(s, sizeof s, "%ld", (long)n);
        mq_text(q, q->api->font_small, x, y + 2, s, Q_INK);
        blocks(q, n, x + 30, y + 2, u, 99, true, &bw, &bh2);
        q->api->snprintf(s, sizeof s, "%ld", (long)m);
        mq_text(q, q->api->font_small, x, y + 4 + bh, s, Q_INK);
        blocks(q, m, x + 30, y + 4 + bh, u, 99, true, &bw, &bh2);
        q->vis_units = (uint16_t)(q->vis_units + m / 1000 % 10 + m / 100 % 10 + m / 10 % 10 + m % 10);
        return;
    }
    blocks(q, n, x + 2, y + 4, u, lim, true, &bw, &bh2);
    if (p->v[1] > 0) {
        char s[24];
        q->api->snprintf(s, sizeof s, "and %d", p->v[1]);
        mq_text(q, q->api->font_medium, x, y + 6 + bh2, s, Q_INK);
    }
}

static void v_numline(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h)
{
    int ly = y + h / 2, lx = x + 10, lw = w - 20;
    mq_fill(q, lx, ly, lw, 2, Q_INK);
    if (p->v[4] == 1) { /* fraction line: 0..v1 wholes, v2 parts per whole */
        int units = imax(1, p->v[1]), den = imax(1, p->v[2]), ticks = units * den;
        for (int t = 0; t <= ticks; t++) {
            int tx = lx + lw * t / ticks;
            bool whole = t % den == 0;
            mq_fill(q, tx, ly - (whole ? 8 : 5), 1, whole ? 18 : 12, Q_INK);
            if (whole)
                numtxt(q, tx, ly + 12, t / den, Q_INK);
            if (p->kind == K_LINE && p->v[5] == t)
                mq_circle(q, tx, ly - 14, 6, Q_HOTPINK);
        }
        if (p->v[3] >= 0)
            mq_circle(q, lx + lw * p->v[3] / ticks, ly - 14, 6, Q_GREEN);
        return;
    }
    /* rounding line: v0 .. v1 with 10 ticks; the number at v3 */
    int span = imax(1, p->v[1] - p->v[0]);
    for (int t = 0; t <= 10; t++) {
        int tx = lx + lw * t / 10;
        mq_fill(q, tx, ly - (t % 5 ? 4 : 8), 1, t % 5 ? 10 : 18, Q_INK);
    }
    numtxt(q, lx, ly + 12, p->v[0], Q_INK);
    numtxt(q, lx + lw, ly + 12, p->v[1], Q_INK);
    numtxt(q, lx + lw / 2, ly + 12, p->v[0] + span / 2, Q_DIM);
    int mx = lx + lw * p->v[3] / span;
    mq_circle(q, mx, ly - 14, 6, Q_HOTPINK);
    numtxt(q, mx, ly - 36, p->v[0] + p->v[3], Q_HOTPINK);
}

static void v_ruler(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h, int reveal)
{
    int den = imax(1, p->v[1]), inch = (w - 20) / 5, rx = x + 10, ry = y + h / 2;
    mq_fill(q, rx, ry, inch * 5, 26, Q_YELLOW);
    for (int t = 0; t <= 5 * 4; t++) {
        int tx = rx + inch * t / 4, l = t % 4 == 0 ? 14 : t % 2 == 0 ? 9 : 5;
        mq_fill(q, tx, ry, 1, l, Q_INK);
        if (t % 4 == 0)
            numtxt(q, tx, ry + 13, t / 4, Q_INK);
    }
    if (p->v[2]) { /* ribbon */
        int len = (p->v[0] * inch / den * reveal + 999) / 1000;
        mq_fill(q, rx, ry - 14, len, 8, Q_HOTPINK);
    }
}

static void v_lines(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h)
{
    int k = p->v[0], cy = y + h / 2;
    int x0 = x + 20, x1 = x + w - 20;
    switch (k) {
    case 0: /* segment */
        mq_fill(q, x0, cy, x1 - x0, 2, Q_INK);
        mq_circle(q, x0, cy, 4, Q_HOTPINK), mq_circle(q, x1, cy, 4, Q_HOTPINK);
        break;
    case 1: /* ray */
        mq_fill(q, x0, cy, x1 - x0, 2, Q_INK);
        mq_circle(q, x0, cy, 4, Q_HOTPINK);
        tri(q, x1, cy - 6, x1, cy + 7, x1 + 10, cy, Q_INK);
        break;
    case 2: /* line */
        mq_fill(q, x0, cy, x1 - x0, 2, Q_INK);
        tri(q, x1, cy - 6, x1, cy + 7, x1 + 10, cy, Q_INK);
        tri(q, x0, cy - 6, x0, cy + 7, x0 - 10, cy, Q_INK);
        break;
    case 3: /* parallel */
        mq_fill(q, x0, cy - 16, x1 - x0, 2, Q_INK);
        mq_fill(q, x0, cy + 16, x1 - x0, 2, Q_INK);
        break;
    default: /* perpendicular */
        mq_fill(q, x0, cy, x1 - x0, 2, Q_INK);
        mq_fill(q, (x0 + x1) / 2, y + 6, 2, h - 12, Q_INK);
        mq_rect(q, (x0 + x1) / 2 + 2, cy - 10, 10, 10, Q_HOTPINK);
        break;
    }
}

/* Does the question of p hide its model's give-aways? (VR_* of the skill; q->vis_q is set
 * while a question is drawn: play screen; feedback and worked examples show everything) */
bool mq_visual_hides(const mq_t *q, const mq_prob_t *p)
{
    int sk = p->skill < MQ_NSKILL ? p->skill : 0, vr = mq_skill[sk].visual_may_reveal;
    return q->vis_q && !(vr == VR_MAY || mq_rung_concrete(sk, p->diff));
}

/* The concrete stage of a skill's ladder (rungs 0 and 1: single digits, the first steps of a new
 * idea): a question's picture may be counted there, as children start. Not where counting would
 * replace the skill at any rung: the fact games (recall), area by multiplying, the multi-digit
 * operations, the formulas. */
bool mq_rung_concrete(int skill, int rung)
{
    switch (skill) {
    case S_FACTS: case S_DIVFACT: case S_AREAMUL: case S_FORMULA: case S_ADDSUB: case S_ADDSUBBIG: case S_MULTI:
    case S_DIVIDE: case S_MULBIG: case S_DIV2: case S_MUL10: return false;
    default: return rung <= 1;
    }
}

/* ---- real measuring things (backlog (c)): a kitchen scale with a dial, a balance with two pans, a
 * measuring jug with its liter marks. Every number stays on the picture in every frame (the playtest:
 * "a number disappears on the scales"): only the needle, the beam and the juice move. */

/* a kitchen scale: a sack of flour on the plate, the dial's needle at kg (0 .. a round top), "N kg" under it */
static void v_kitchen_scale(mq_t *q, int kg, int x, int y, int w, int h, int reveal)
{
    const font_t *fs = q->api->font_small;
    int top = kg <= 10 ? 10 : kg <= 20 ? 20 : kg <= 50 ? 50 : 100;
    int r = imax(12, imin((h - fs->h - 3 - 8 - 16 - 14) / 2, w / 3 - 8)), cx = x + w / 2, by = y + h - fs->h - 3; /* (room
                                                                  * for the plate and the sack above the dial) */
    int dy = by - 8 - r;                                                                  /* the dial's centre */
    /* the body: the plate on a post, the base with the dial */
    mq_rrect(q, cx - r - 6, dy - r - 6, 2 * r + 12, by - (dy - r - 6), 8, Q_LAV);
    mq_circle(q, cx, dy, r, Q_WHITE);
    mq_ring(q, cx, dy, r, Q_PURPLE);
    mq_fill(q, cx - 3, dy - r - 12, 6, 6, Q_GREY);
    mq_rrect(q, cx - r - 2, dy - r - 16, 2 * r + 4, 5, 2, Q_GREY); /* the plate */
    /* the sack on the plate */
    int sw = imin(2 * r, 34), sh = imin(18, dy - r - 16 - y - 1);
    if (sh >= 8) {
        mq_rrect(q, cx - sw / 2, dy - r - 16 - sh, sw, sh, 5, Q_CREAM);
        mq_rect(q, cx - sw / 2, dy - r - 16 - sh, sw, sh, Q_BROWN);
        mq_fill(q, cx - 4, dy - r - 16 - sh - 2, 8, 3, Q_BROWN); /* the tie */
    }
    /* the dial: ticks from -135 to +135 degrees, 0 and the top labelled */
    for (int i = 0; i <= 10; i++) {
        int a = -135 + 27 * i, l = i % 5 ? 3 : 6;
        mq_line(q, cx + mq_isin(a) * (r - l) / 1024, dy - mq_icos(a) * (r - l) / 1024, cx + mq_isin(a) * (r - 1) / 1024,
                dy - mq_icos(a) * (r - 1) / 1024, Q_INK);
    }
    if (r >= 22) {
        char t[8];
        mq_text_c(q, fs, cx - r * 2 / 5, dy + r / 3, "0", Q_DIM);
        q->api->snprintf(t, sizeof t, "%d", top);
        mq_text_c(q, fs, cx + r * 2 / 5, dy + r / 3, t, Q_DIM);
    }
    int a = -135 + 270 * kg / top * reveal / 1000; /* the needle swings to the weight */
    for (int t = 0; t < 2; t++)
        mq_line(q, cx + t, dy, cx + t + mq_isin(a) * (r - 4) / 1024, dy - mq_icos(a) * (r - 4) / 1024, Q_HOTPINK);
    mq_circle(q, cx, dy, 2, Q_INK);
    char s[12];
    q->api->snprintf(s, sizeof s, "%d kg", kg);
    mq_text_c(q, fs, cx, by + 2, s, Q_INK); /* (always: the operand) */
}

/* a balance: the thing on the left pan (0 an apple, 1 a paper clip, 2 a puppy, 3 a backpack), the right pan empty:
 * the beam tips toward the heavier side as it settles */
static void v_balance(mq_t *q, int obj, int x, int y, int w, int h, int reveal)
{
    static const int8_t tilt[4] = {5, 1, 9, 8}; /* how far the beam tips (px), by the thing's weight */
    int cx = x + w / 2, base = y + h - 6, arm = imin(w / 2 - 16, 56), py = y + h / 2;
    int d = tilt[obj & 3] * reveal / 1000;
    mq_rrect(q, cx - 22, base - 6, 44, 6, 3, Q_LAV);                /* the foot */
    mq_fill(q, cx - 2, py, 4, base - 6 - py, Q_PURPLE);              /* the post */
    for (int t = 0; t < 3; t++)                                      /* the beam */
        mq_line(q, cx - arm, py + d + t - 1, cx + arm, py - d + t - 1, Q_INK);
    mq_circle(q, cx, py, 3, Q_YELLOW);
    for (int sd = -1; sd <= 1; sd += 2) {                            /* the pans on their strings */
        int px = cx + sd * arm, pyy = py - sd * d + 18;
        mq_line(q, px, py - sd * d, px - 12, pyy, Q_DIM);
        mq_line(q, px, py - sd * d, px + 12, pyy, Q_DIM);
        mq_rrect(q, px - 16, pyy, 32, 5, 2, Q_GREY);
        if (sd < 0) { /* the thing */
            int ox = px, oy = pyy - 1;
            switch (obj & 3) {
            case 0: mq_circle(q, ox, oy - 7, 7, Q_HOTPINK), mq_fill(q, ox, oy - 17, 2, 4, Q_BROWN); break; /* an apple */
            case 1: mq_rect(q, ox - 5, oy - 5, 10, 4, Q_DIM); break;                                     /* a paper clip */
            case 2: /* a puppy */
                mq_rrect(q, ox - 11, oy - 12, 22, 12, 5, Q_PEACH), mq_circle(q, ox + 9, oy - 14, 6, Q_PEACH);
                mq_circle(q, ox + 11, oy - 15, 1, Q_INK);
                break;
            default: /* a backpack */
                mq_rrect(q, ox - 9, oy - 18, 18, 18, 4, Q_SKY), mq_rect(q, ox - 6, oy - 10, 12, 6, Q_BLUE);
                break;
            }
        }
    }
}

/* a measuring jug: the handle and spout, a mark at every liter (numbered as often as they fit), the juice to its level
 * (a question: the jug full, what is poured is the question; only the full mark numbered) */
static void v_jug(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h, int reveal, bool asked)
{
    const font_t *fs = q->api->font_small;
    int cap = imax(1, p->v[0]);
    int jw = imin(56, w / 3), jx = x + w / 2 - jw / 2, jy = y + 10, jh = h - 16;
    int lv = q->vis_q ? jh - 4 : (jh - 4) * p->v[1] / cap * reveal / 1000;
    mq_rrect(q, jx, jy, jw, jh, 6, Q_WHITE);                                         /* the glass */
    mq_fill(q, jx + 2, jy + jh - 2 - lv, jw - 4, lv, Q_YELLOW);                       /* the juice */
    if (lv > 2)
        mq_fill(q, jx + 2, jy + jh - 2 - lv, jw - 4, 2, Q_ORANGE);                    /* its top */
    mq_rect(q, jx, jy, jw, jh, Q_DIM);
    tri(q, jx - 6, jy - 4, jx + 6, jy, jx, jy + 8, Q_WHITE);                          /* the spout */
    mq_line(q, jx - 6, jy - 4, jx, jy + 8, Q_DIM);
    for (int t = 0; t < 3; t++)                                                       /* the handle */
        mq_ring(q, jx + jw + 4, jy + jh / 3, imin(10, jh / 5) - t, Q_DIM);
    int step = 1; /* the numbers as often as they fit, never one on another */
    while ((jh - 4) * step / cap < fs->h + 1 && step < cap)
        step = step == 1 ? 2 : step == 2 ? 5 : step * 2;
    for (int i = 1; i <= cap; i++) {
        int my = jy + jh - 2 - (jh - 4) * i / cap;
        mq_fill(q, jx + jw - 12, my, 10, 1, Q_INK);
        if (i == cap || (i % step == 0 && !asked)) { /* (a question: only the jug's own size, never the answer's mark) */
            if (i != cap && cap - i < step && (jh - 4) * (cap - i) / cap < fs->h + 1)
                continue; /* (too near the top mark's number) */
            char s[8];
            q->api->snprintf(s, sizeof s, "%d", i);
            mq_text(q, fs, jx - 4 - q->api->text_width(fs, s), my - fs->h / 2, s, Q_DIM);
        }
    }
    if (jx + jw + 18 + q->api->text_width(fs, "liters") <= x + w)
        mq_text(q, fs, jx + jw + 18, jy + jh - fs->h, "liters", Q_DIM);
}

/* Draw the visual model of p in the box (x, y, w, h); reveal 0..1000. While a question is
 * drawn (q->vis_q) and the skill's model would give the answer away, it is drawn so that
 * nothing can be counted for the answer: arrays as labelled boxes, divisions as one pile and
 * empty pots, areas as sides without unit squares, sums lined up by place, cut pies without
 * shading; the full model appears in the feedback and the worked example. q->vis_units
 * counts the countable things drawn (dots, slices, squares, coins, cubes, stars, marks). */
void mq_visual(mq_t *q, const mq_prob_t *p, int x, int y, int w, int h, int reveal)
{
    bool asked = q->vis_q;
    q->vis_asked = asked;
    q->vis_q = mq_visual_hides(q, p);
    q->vis_units = 0, q->vis_shaded = 0, q->vis_parts = 0;
    if (p == &q->prob)
        q->prob_pic_t = mq_mark(q);
    if (asked && reveal > 999 && (p->vis == V_PATTERN || p->vis == V_ANGLE)) /* a question never shows its last stage (the
                                * pattern's answer, the right angle's mark); every other model is whole (CR-01: a cap for
                                * all drew "n of d" one short wherever a model rounded down) */
        reveal = 999;
    if (asked && !q->vis_q && mq_vis_data(p) && p->vis != V_ANGLE) /* the picture IS the question (coloured parts to name, a graph, a clock):
                                                * all of it from the first frame, never animated in (the pink cake) */
        reveal = 1000;
    if (reveal < 0)
        reveal = 0;
    if (reveal > 1000)
        reveal = 1000;
    switch (p->vis) {
    case V_ARRAY: v_array(q, p, x, y, w, h, reveal); break;
    case V_GROUPS: v_groups(q, p, x, y, w, h, reveal); break;
    case V_PIE: v_pie(q, p, x, y, w, h, reveal); break;
    case V_BAR: bar(q, x + 4, y + h / 2 - 14, w - 8, 28, q->vis_q ? 0 : p->v[0], p->v[1], q->vis_q ? 0 : p->v[2], reveal); break;
    case V_BAR2: { /* "1/3 = ?/9": the first fraction, pink; the second bar with its unknown a "?" -- never the first
                    * fraction's label again, never the answer, and an unknown denominator not cut into countable
                    * parts (CR2-13) */
        char s[12];
        bool num = p->kind != K_CHOICE; /* (a choice's answer is an index: "which is bigger?" asks no part) */
        bool qn = asked && num && p->v[2] == p->ans, qd = asked && num && !qn && p->v[3] == p->ans;
        bar(q, x + 40, y + 8, w - 44, 26, q->vis_q ? 0 : p->v[0], p->v[1], 0, reveal);
        bar(q, x + 40, y + 50, w - 44, 26, q->vis_q || qn || qd ? 0 : p->v[2], qd ? 1 : p->v[3], 0, reveal);
        q->api->snprintf(s, sizeof s, "%d/%d", p->v[0], p->v[1]);
        mq_text(q, q->api->font_small, x, y + 14, s, Q_INK);
        if (qn)
            q->api->snprintf(s, sizeof s, "?/%d", p->v[3]);
        else if (qd)
            q->api->snprintf(s, sizeof s, "%d/?", p->v[2]);
        else
            q->api->snprintf(s, sizeof s, "%d/%d", p->v[2], p->v[3]);
        mq_text(q, q->api->font_small, x, y + 56, s, Q_INK);
        break;
    }
    case V_BASE10: v_base10(q, p, x, y, w, h, reveal); break;
    case V_NUMLINE: v_numline(q, p, x, y, w, h); break;
    case V_CLOCK: v_clock(q, x + w / 2, y + h / 2, imin(w, h) / 2 - 4, p->v[0], p->v[1]); break;
    case V_GRID: v_grid(q, p, x, y, w, h, reveal); break;
    case V_LSHAPE: v_lshape(q, p, x, y, w, h, reveal); break;
    case V_ANGLE: v_angle(q, p, x, y, w, h, reveal); break;
    case V_SHAPE: v_shape(q, p, x, y, w, h); break;
    case V_COINS: v_coins(q, p, x, y, w, h, reveal); break;
    case V_BARGRAPH: v_bargraph(q, p, x, y, w, h, reveal); break;
    case V_LINEPLOT: v_lineplot(q, p, x, y, w, h); break;
    case V_TABLE: v_table(q, p, x, y, w, h); break;
    case V_COORD: v_coord(q, p, x, y, w, h); break;
    case V_PRISM: v_prism(q, p, x, y, w, h, reveal); break;
    case V_PV: v_pv(q, p, x, y, w, h); break;
    case V_RULER: v_ruler(q, p, x, y, w, h, reveal); break;
    case V_LINES: v_lines(q, p, x, y, w, h); break;
    case V_SCALE:
        if (p->v[1])
            v_kitchen_scale(q, p->v[1], x, y, w, h, reveal);
        else
            v_balance(q, p->v[0], x, y, w, h, reveal);
        break;
    case V_JUG: v_jug(q, p, x, y, w, h, reveal, asked); break;
    case V_PATTERN: { /* a run of terms; v[3]: the box the order asks for (1-based; 0: the next after four), v[4]: a
                       * second pattern's step (two rows: the first pattern up to the box asked, the second with its
                       * "?" there) -- the "?" where the text asks, never the next term of the wrong pattern (CR2-14) */
        int a = p->v[0], b = p->v[1], b2 = p->v[4], ask = imax(0, imin(12, p->v[3]));
        int top = ask ? a + (ask - 1) * imax(b, b2) : 0; /* (the biggest number drawn: three digits want wider boxes) */
        int nb = top >= 100 ? 4 : 5, first = ask > nb ? ask - nb : 0; /* (a window of boxes, up to the one asked) */
        int rows = b2 ? 2 : 1, rh = h / rows, bh = imin(32, rh - 6), bw = (w - 8) / nb;
        for (int r = 0; r < rows; r++) {
            int step = r == 0 ? b : b2, cy = y + r * rh + rh / 2;
            bool last = r == rows - 1;
            for (int j = 0; j < nb; j++) {
                int i = first + j, bx = x + 4 + j * bw, val = step ? a + i * step : a << i;
                if (!last && ask && i >= ask) /* (the first of two patterns: up to the box asked) */
                    break;
                bool unknown = asked && last && (ask ? i == ask - 1 : i >= 3 && val == p->ans);
                if (unknown) { /* a question: its unknown term a "?", never the answer */
                    mq_rrect(q, bx, cy - bh / 2, bw - 6, bh, 6, Q_YELLOW);
                    mq_text_c(q, q->api->font_medium, bx + (bw - 6) / 2, cy - 7, "?", Q_INK);
                    break;
                }
                bool tail = !ask && j == nb - 1; /* (the next term: shown as the model completes) */
                mq_rrect(q, bx, cy - bh / 2, bw - 6, bh, 6, tail || (ask && i == ask - 1) ? Q_YELLOW : Q_MINT);
                if (!tail || reveal >= 1000)
                    numtxt(q, bx + (bw - 6) / 2, cy - 6, val, Q_INK);
                else
                    mq_text_c(q, q->api->font_medium, bx + (bw - 6) / 2, cy - 7, "?", Q_INK);
            }
        }
        break;
    }
    default:
        break;
    }
    q->vis_q = asked;
    q->frame_units = (uint16_t)(q->frame_units + q->vis_units);
}

/* ------------------------------------------------------------------ does the picture fit the numbers? */
/* the numbers of the problem's text: what a picture may show */
static int text_nums(const mq_prob_t *p, int32_t *out, int max)
{
    int n = 0;
    for (int l = 0; l < 3; l++)
        for (const char *s = p->text[l]; *s && n < max; s++)
            if (*s >= '0' && *s <= '9' && (s == p->text[l] || s[-1] < '0' || s[-1] > '9')) {
                int32_t v = 0;
                while (*s >= '0' && *s <= '9')
                    v = v * 10 + (*s++ - '0');
                out[n++] = v;
                s--;
            }
    return n;
}
static bool told(const int32_t *t, int n, int32_t v, const mq_prob_t *p)
{
    if ((p->kind == K_NUM || p->kind == K_SHADE) && v == p->ans)
        return true;
    for (int i = 0; i < n; i++)
        if (t[i] == v)
            return true;
    for (int i = 0; i < n; i++) /* a product or a sum of two of them (groups of, sides of) */
        for (int j = 0; j < n; j++)
            if (i != j && (t[i] * t[j] == v || t[i] + t[j] == v))
                return true;
    return false;
}
static int vfail(char *why, int n, const char *msg)
{
    if (why && n > 0) {
        int k = 0;
        for (; msg[k] && k < n - 1; k++)
            why[k] = msg[k];
        why[k] = 0;
    }
    return -1;
}
/* 0: the picture of p shows the numbers of its story (and is drawn whole); else -1 and why */
int mq_vis_check(mq_t *q, const mq_prob_t *p, char *why, int n)
{
    int32_t t[24];
    int nt = text_nums(p, t, 24);
    const int16_t *v = p->v;
    (void)q;
    switch (p->vis) {
    case V_ARRAY: {
        if (v[0] < 1 || v[1] < 1) /* (zero groups, or groups of zero: the facts' x0 rule, rightly empty) */
            return p->skill == S_FACTS && v[0] >= 0 && v[1] >= 0 ? 0 : vfail(why, n, "an empty array");
        int32_t tot = v[0] * v[1] + v[5];
        if (!told(t, nt, tot, p) && !told(t, nt, v[0], p))
            return vfail(why, n, "the array is not the story's");
        return 0;
    }
    case V_GROUPS: {
        int32_t each = v[1] * (v[3] == 10 ? 10 : 1), tot = (int32_t)v[0] * each + v[2];
        if (v[0] < 1 || v[2] < 0)
            return vfail(why, n, "groups out of range");
        if (v[1] && !told(t, nt, tot, p) && !told(t, nt, v[0], p) && !(p->big && tot == p->big))
            return vfail(why, n, "the groups are not the story's");
        return 0;
    }
    case V_PIE:
        if (v[1] < 1 || v[0] < 0 || v[0] > v[1] * (v[2] > 0 ? v[2] : 1) || v[2] > 4)
            return vfail(why, n, "more slices shaded than the pies have");
        return 0;
    case V_BAR:
        if (v[1] < 1 || v[0] + v[2] > v[1] || v[0] < 0)
            return vfail(why, n, "the bar holds more than its parts");
        return 0;
    case V_BAR2:
        if (v[1] < 1 || v[3] < 1 || v[0] > v[1] || v[2] > v[3])
            return vfail(why, n, "a bar holds more than its parts");
        return 0;
    case V_BASE10:
        if (p->big <= 0 || !told(t, nt, p->big, p))
            return vfail(why, n, "the blocks are not the story's number");
        if (p->skill == S_CMP3 && (v[2] <= 0 || !told(t, nt, v[2], p)))
            return vfail(why, n, "a comparison shows only one number");
        return 0; /* (blocks that would not fit become the place chart: never partial) */
    case V_NUMLINE:
        if (v[4] == 1 ? (v[3] > v[1] * v[2] || v[2] < 1) : (v[3] < 0 || v[3] > v[1] - v[0]))
            return vfail(why, n, "the mark is off the line");
        return 0;
    case V_CLOCK:
        if (v[0] < 1 || v[0] > 12 || v[1] < 0 || v[1] > 59)
            return vfail(why, n, "no such time");
        return 0;
    case V_GRID:
        if (v[2] == 5 && v[3] > v[0] * v[1])
            return vfail(why, n, "more squares shaded than the grid has");
        if (v[2] == 6 && (v[3] > 100 || v[4] > 100))
            return vfail(why, n, "more than a hundredths grid");
        if (v[2] <= 3 && v[0] && v[1] && !told(t, nt, v[0] * v[1], p) && !told(t, nt, v[0], p))
            return vfail(why, n, "the rectangle is not the story's");
        return 0;
    case V_LSHAPE:
        if (v[2] >= v[0] || v[0] < 2 || v[1] < 1 || v[2] < 1 || v[3] < 1)
            return vfail(why, n, "not an L");
        if (p->kind == K_NUM && p->ans != v[0] * v[1] + v[2] * v[3])
            return vfail(why, n, "the L's squares are not the answer");
        return 0;
    case V_SHAPE: { /* a shape the text names is the shape drawn */
        static const char nm[11][10] = {"square", "rectangle", "rhombus", "trapezoid", "parallelo", "", "", "", "kite",
                                        "pentagon", "hexagon"};
        if (p->skill == S_SHAPES5 || p->skill == S_SHAPES)
            return 0; /* (riddles name families; the family is what is asked) */
        for (int k = 0; k < 11; k++)
            if (nm[k][0])
                for (int l = 0; l < 3; l++)
                    for (const char *s = p->text[l]; *s; s++) {
                        int i = 0;
                        while (nm[k][i] && s[i] == nm[k][i])
                            i++;
                        if (!nm[k][i] && k != v[0])
                            return vfail(why, n, "the text names another shape");
                    }
        return 0;
    }
    case V_COINS:
        if ((p->skill == S_COINS || p->skill == S_CHANGE) && v[4] * 100 + v[0] * 25 + v[1] * 10 + v[2] * 5 + v[3] != p->ans)
            return vfail(why, n, "the coins are not the amount");
        return 0;
    case V_PRISM:
        if (p->kind == K_NUM && p->ans != v[0] * v[1] * v[2] + v[3] * v[4] * v[5])
            return vfail(why, n, "the cubes are not the answer");
        return 0;
    case V_COORD:
        if (p->kind == K_COORD && p->ans != v[0] * 100 + v[1])
            return vfail(why, n, "the star is not at the point");
        return 0;
    case V_LINEPLOT:
        for (int i = 1; i <= 6; i++)
            if (v[i] < 0 || v[i] > 9)
                return vfail(why, n, "a column of the plot out of range");
        return 0;
    case V_BARGRAPH:
        for (int i = 0; i < v[0] && i < 4; i++)
            if (v[1] < 1 || v[2 + i] % v[1])
                return vfail(why, n, "a bar is not a whole number of keys");
        return 0;
    default: return 0;
    }
}

/* the picture holds the question's data: what it needs (below), and the coloured parts of a fraction picture
 * (a pie, a bar: what is coloured is part of the question, never animated in) */
bool mq_vis_data(const mq_prob_t *p)
{
    return mq_vis_needed(p) || (!p->vq && (p->vis == V_PIE || p->vis == V_BAR || p->vis == V_BAR2));
}

/* the question cannot be done without its picture (the data is in it: a graph, a plot, a clock to read,
 * coins to count, a shape, squares to count) */
bool mq_vis_needed(const mq_prob_t *p)
{
    if (p->vq || p->vis == V_NONE)
        return false;
    switch (p->vis) {
    case V_LSHAPE: case V_LINEPLOT: case V_BARGRAPH: case V_TABLE: case V_SHAPE: case V_LINES: case V_COORD:
        return true;
    case V_RULER: return p->skill == S_RULER;
    case V_ANGLE: return p->skill == S_PROTRACTOR || (p->skill == S_ANGLES && p->kind == K_CHOICE);
    case V_CLOCK: return p->skill == S_TIME5 || p->skill == S_TIME1;
    case V_COINS: return p->skill == S_COINS;
    case V_GRID: return p->v[2] == 0 || p->v[2] == 6;
    case V_NUMLINE: return p->skill == S_DECIMAL;
    case V_PIE: case V_BAR: return p->skill == S_UNITFRAC;
    case V_PRISM: return p->skill == S_VOLUME && p->diff == 0;
    default: return false;
    }
}
