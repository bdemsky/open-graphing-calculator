/*
 * Math Quest: the high-resolution hooks (modx_api_t.hr, API size 6; docs/MATHQUEST.md
 * "Resolution independence").
 *
 * At display_scale() > 1 the drawing primitives of mq_vis.c tell the platform what the rectangles
 * they are about to draw stand for -- a circle, a ring, a rounded rectangle, a polygon, a thick
 * line, a text in a style, a picture of indexed art -- so a high-resolution platform (the
 * standalone Sparkle Town web page) can draw it smoothly instead: anti-aliased shapes, a web font,
 * the high-resolution picture of the same art (found by its content). The module still draws the
 * rectangles (they make the 1x framebuffer the tests and screenshots read); the platform keeps them
 * out of its high-resolution image while the bracket is open. On the calculator (scale 1, or no
 * hr entry) every hook is a no-op returning 0, and nothing else changes.
 *
 * Coordinates handed over are physical pixels x 16 of the logical ones the module computed (the
 * shake offset included). Stateless: no RW (the module image has no .data / .bss).
 */
#include "mq.h"

static const modx_api_t *hr_api(const mq_t *q)
{
    const modx_api_t *a = q->api;
    if (q->scale < 2 || a->size < offsetof(modx_api_t, hr) + sizeof a->hr || !a->hr)
        return 0;
    return a;
}

/* a[0..3]: kind, colour, the clip band (physical rows) */
static void head(const mq_t *q, int32_t *a, int kind, int c)
{
    a[0] = kind, a[1] = c >= 0 && kind != 0 ? mq_tint(q, (uint16_t)c) : c;
    a[2] = q->clip_y1 ? (q->clip_y0 + q->oy) * q->scale : 0;
    a[3] = q->clip_y1 ? (q->clip_y1 + q->oy) * q->scale : 0;
}

/* logical x / y (+ half a pixel: the centre of the pixel) -> physical x 16; zoomed (mq_zoom) */
static int32_t px16(const mq_t *q, int v, int o, int half, int z0, bool flip, int f0)
{
    int32_t v16 = (int32_t)v * 16 + half;
    if (q->zm)
        v16 = z0 * 16 + (v16 - z0 * 16) * q->zm / 16;
    if (flip) /* mirrored (a point: the centre of the mirrored pixel) */
        v16 = 2 * f0 * 16 - v16;
    return (v16 + o * 16) * q->scale;
}
#define PX(v, half) px16(q, v, q->ox, half, q->zx, q->flipx, q->fcx)
#define PY(v, half) px16(q, v, q->oy, half, q->zy, q->flipy, q->fcy)
/* a length x 16, physical (zoomed) */
static int32_t len16(const mq_t *q, int32_t v16) { return (q->zm ? v16 * q->zm / 16 : v16) * q->scale; }

int mq_hr_shape(mq_t *q, int kind, uint16_t c, const int16_t *v, int nv)
{
    const modx_api_t *api = hr_api(q);
    if (!api)
        return 0;
    int32_t a[4 + 1 + 2 * 48];
    int n = 4;
    head(q, a, kind, c);
    switch (kind) {
    case MODX_SH_CIRCLE: /* cx cy r: rows cy-r..cy+r of half-width ~sqrt(r^2 + r) */
    case MODX_SH_RING:   /* cx cy r: a one-pixel ring through the pixel centres */
        a[n++] = PX(v[0], 8), a[n++] = PY(v[1], 8);
        a[n++] = len16(q, v[2] * 16 + (kind == MODX_SH_CIRCLE ? 8 : 0));
        if (kind == MODX_SH_RING)
            a[n++] = len16(q, 16);
        break;
    case MODX_SH_RRECT: /* x y w h r (mirrored: from its other edge) */
        a[n++] = q->flipx ? PX(v[0] + v[2], 0) : PX(v[0], 0), a[n++] = q->flipy ? PY(v[1] + v[3], 0) : PY(v[1], 0);
        a[n++] = len16(q, v[2] * 16), a[n++] = len16(q, v[3] * 16);
        a[n++] = len16(q, v[4] * 16);
        break;
    case MODX_SH_LINE: /* x0 y0 x1 y1, as thick as a logical pixel */
        a[n++] = PX(v[0], 8), a[n++] = PY(v[1], 8);
        a[n++] = PX(v[2], 8), a[n++] = PY(v[3], 8);
        a[n++] = len16(q, 16);
        break;
    case MODX_SH_POLY: /* v: x[nv] then y[nv]; scanlines through the pixel centres */
        if (nv < 3 || nv > 48)
            return 0;
        a[n++] = nv;
        for (int i = 0; i < nv; i++)
            a[n++] = PX(v[i], 8), a[n++] = PY(v[nv + i], 8);
        break;
    default:
        return 0;
    }
    return api->hr(MODX_HR_SHAPE, a, n, 0);
}

int mq_hr_text(mq_t *q, const font_t *f, int x, int y, const char *s, uint16_t c)
{
    const modx_api_t *api = hr_api(q);
    if (!api)
        return 0;
    int st = f == api->font_small ? TS_SMALL : f == api->font_medium ? TS_BODY : f == api->font_large ? TS_TITLE : TS_BIG;
    int32_t a[8];
    head(q, a, st, c);
    int al = q->talign > MODX_HR_ALIGN_RIGHT ? MODX_HR_ALIGN_LEFT : q->talign;
    a[0] |= al << 4; /* where it sits in its 1x box (the render audit's M2: left columns stay left) */
    a[4] = PX(x, 0), a[5] = PY(y, 0);
    a[6] = len16(q, (int32_t)api->text_width(f, s) * 16), a[7] = len16(q, (int32_t)f->h * 16);
    return api->hr(MODX_HR_TEXT, a, 8, s);
}

/* the platform's width of s in f's style, logical pixels x 16 (MODX_HR_MEASURE; 0: not known) */
int mq_hr_measure(mq_t *q, const font_t *f, const char *s)
{
    const modx_api_t *api = hr_api(q);
    if (!api || !s || !*s)
        return 0;
    int32_t a[2] = {f == api->font_small ? TS_SMALL : f == api->font_medium ? TS_BODY : f == api->font_large ? TS_TITLE : TS_BIG,
                    f->h};
    int w = api->hr(MODX_HR_MEASURE, a, 2, s);
    return w > 0 && w < 16 * 4 * MQ_W ? w : 0;
}

/* a text whose high-resolution box the module sets itself: left at x16 (logical x 16), w16 wide (a run of
 * a sentence set by the platform's widths, mq_hr_measure); its 1x drawing follows inside the bracket */
int mq_hr_text_at(mq_t *q, const font_t *f, int x16, int y, int w16, const char *s, uint16_t c)
{
    const modx_api_t *api = hr_api(q);
    if (!api || x16 < 0)
        return 0;
    int st = f == api->font_small ? TS_SMALL : f == api->font_medium ? TS_BODY : f == api->font_large ? TS_TITLE : TS_BIG;
    int32_t a[8];
    head(q, a, st, c);
    a[0] |= MODX_HR_ALIGN_LEFT << 4;
    a[4] = PX(x16 >> 4, x16 & 15), a[5] = PY(y, 0);
    a[6] = len16(q, w16), a[7] = len16(q, (int32_t)f->h * 16);
    return api->hr(MODX_HR_TEXT, a, 8, s);
}

int mq_hr_image(mq_t *q, const uint8_t *px, int w, int h, int stride, int bpp, const uint16_t *pal, int x, int y,

                int art16, int flags, int shear, int sy16, int sil)
{
    const modx_api_t *api = hr_api(q);
    if (!api)
        return 0;
    modx_hr_img_t im = {px, pal, (int16_t)stride, (uint8_t)bpp};
    int32_t a[13];
    head(q, a, 0, sil);
    a[4] = PX(x, 0), a[5] = PY(y, 0);
    if (q->flipx) /* mirrored: from the picture's right edge, the picture mirrored */
        a[4] = PX(x + w * art16 / 16, 0), flags ^= 4;
    if (q->flipy) /* upside down: turned half round and mirrored */
        a[5] = PY(y + h * art16 / 16, 0), flags = (flags & ~3) | ((flags + 2) & 3), flags ^= 4;
    a[6] = len16(q, art16), a[7] = w, a[8] = h, a[9] = flags;
    a[10] = len16(q, (int32_t)shear * 16), a[11] = sy16;
    a[12] = q->tint; /* dusk / night: the platform tints the picture as mq_tint does every pixel (render audit H2) */
    return api->hr(MODX_HR_IMAGE, a, 13, &im);
}

void mq_hr_end(mq_t *q)
{
    const modx_api_t *api = hr_api(q);
    if (api)
        api->hr(MODX_HR_END, 0, 0, 0);
}

/* the scene now (an id of the module and its sky, ground and accent colours): a wide window may
 * carry it out beside the 4:3 play area (the standalone page's sides) */
void mq_hr_scene(mq_t *q, int id, uint16_t sky, uint16_t ground, uint16_t accent)
{
    const modx_api_t *api = hr_api(q);
    if (!api)
        return;
    int32_t a[4] = {id, sky, ground, accent};
    api->hr(MODX_HR_SCENE, a, 4, 0);
}

/* a thing drawn from many shapes, by its name (MODX_HR_SPRITE): the platform may have art of its
 * own for it (the Mac page's vector set) */
int mq_hr_sprite(mq_t *q, const char *id, int x, int y, int w, int h, const int32_t *v, int nv)
{
    const modx_api_t *api = hr_api(q);
    if (!api || !id)
        return 0;
    nv = nv < 0 ? 0 : nv > 64 ? 64 : nv;
    int32_t a[14 + 64];
    head(q, a, 0, -1);
    int32_t x0 = PX(x, 0), x1 = PX(x + w, 0), y0 = PY(y, 0), y1 = PY(y + h, 0);
    a[4] = x0 < x1 ? x0 : x1, a[5] = y0 < y1 ? y0 : y1;
    a[6] = x0 < x1 ? x1 - x0 : x0 - x1, a[7] = y0 < y1 ? y1 - y0 : y0 - y1;
    a[8] = (q->flipx ? 1 : 0) | (q->flipy ? 2 : 0);
    a[9] = (int32_t)q->anim_t, a[10] = q->tint, a[11] = nv + 2;
    a[12] = w, a[13] = h; /* the box in logical pixels */
    for (int i = 0; i < nv; i++)
        a[14 + i] = v[i];
    return api->hr(MODX_HR_SPRITE, a, 14 + nv, id);
}
