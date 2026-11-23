/* The Animation module's GIF encoder (modules/animation/gif.c), checked by
 * decoding its output with an independent GIF89a decoder written here from
 * the specification (header, global colour table, extensions, image
 * descriptors, LZW with variable code width and clear codes, sub-blocks),
 * compositing the frames (disposal "keep") and comparing every pixel with
 * the source frames:
 *   - flat colours of the first frame are reproduced exactly (the exact
 *     palette slots);
 *   - other colours land on the nearest 6x6x6 cube colour (per channel
 *     within 26 of the RGB888 value);
 *   - frames after the first are bounding boxes of the change; an unchanged
 *     frame is 1 x 1; delays and the loop extension are as written;
 *   - long noisy images force the 12-bit code width and table clears;
 *   - a write error stops the stream and is reported.
 * Built and run by `make test`. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../modules/animation/gif.h"

static int fails, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if (!(c)) {                                                               \
            fails++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

/* ------------------------------------------------------------ output buffer */
typedef struct {
    uint8_t *b;
    size_t n, cap;
    long fail_after; /* -1: never */
} buf_t;

static int wr(void *user, const void *p, size_t n)
{
    buf_t *b = user;
    if (b->fail_after >= 0 && (long)(b->n + n) > b->fail_after)
        return -5;
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2;
        b->b = realloc(b->b, b->cap);
    }
    memcpy(b->b + b->n, p, n);
    b->n += n;
    return 0;
}

/* ------------------------------------------------------------ decoder */
typedef struct {
    int w, h, frames, max_bits, clears, delays[256];
    int box[256][4];
    uint8_t pal[256][3];
    uint8_t *canvas;  /* palette indices, composited */
    uint8_t *shots[64]; /* canvas after each frame (first 64) */
    bool loop;
} dec_t;

static int u16(const uint8_t *p) { return p[0] | p[1] << 8; }

/* LZW-decode one image's sub-blocks at *pp into out (npix indices). Returns
 * 0, or -1 on a malformed stream. */
static int lzw_decode(dec_t *d, const uint8_t **pp, const uint8_t *end, uint8_t *out, int npix)
{
    const uint8_t *p = *pp;
    int min = *p++;
    if (min != 8)
        return -1;
    /* gather the sub-blocks */
    static uint8_t data[1 << 20];
    size_t nd = 0;
    while (p < end && *p) {
        int k = *p++;
        if (p + k > end || nd + (size_t)k > sizeof data)
            return -1;
        memcpy(data + nd, p, (size_t)k);
        nd += (size_t)k, p += k;
    }
    if (p >= end)
        return -1;
    p++; /* terminator */
    *pp = p;
    static uint16_t prefix[4096];
    static uint8_t suffix[4096], stack[4097];
    int clear = 256, eoi = 257, size = 9, next = 258, prev = -1, o = 0;
    uint8_t first = 0;
    size_t bitpos = 0;
    for (;;) {
        if (bitpos + (size_t)size > nd * 8)
            return -1; /* ran out before the end code */
        int code = 0;
        for (int i = 0; i < size; i++, bitpos++)
            code |= ((data[bitpos >> 3] >> (bitpos & 7)) & 1) << i;
        if (size > d->max_bits)
            d->max_bits = size;
        if (code == clear) {
            size = 9, next = 258, prev = -1;
            d->clears++;
            continue;
        }
        if (code == eoi)
            break;
        int c = code, sp = 0;
        if (prev < 0) {
            if (code > 255)
                return -1;
            if (o < npix)
                out[o++] = (uint8_t)code;
            first = (uint8_t)code;
            prev = code;
            continue;
        }
        if (code > next)
            return -1;
        if (code == next) { /* KwKwK */
            stack[sp++] = first;
            c = prev;
        }
        while (c > 255) {
            stack[sp++] = suffix[c];
            c = prefix[c];
        }
        stack[sp++] = (uint8_t)c;
        first = (uint8_t)c;
        while (sp)
            if (o < npix)
                out[o++] = stack[--sp];
            else
                sp--;
        if (next < 4096) {
            prefix[next] = (uint16_t)prev;
            suffix[next] = first;
            next++;
            if (next == (1 << size) && size < 12)
                size++;
        }
        prev = code;
    }
    return o == npix ? 0 : -1;
}

static int decode(const uint8_t *b, size_t n, dec_t *d)
{
    memset(d, 0, sizeof *d);
    const uint8_t *p = b, *end = b + n;
    if (n < 13 + 768 || memcmp(p, "GIF89a", 6))
        return -1;
    d->w = u16(p + 6), d->h = u16(p + 8);
    if (p[10] != 0xF7)
        return -2;
    p += 13;
    memcpy(d->pal, p, 768);
    p += 768;
    d->canvas = calloc((size_t)d->w * (size_t)d->h, 1);
    int delay = 0;
    while (p < end) {
        if (*p == 0x3B)
            return 0;
        if (*p == 0x21) { /* extension */
            int label = p[1];
            p += 2;
            if (label == 0xF9) {
                if (p[0] != 4 || (p[1] & 0x1C) != 0x04)
                    return -3; /* disposal: keep */
                delay = u16(p + 2);
            }
            if (label == 0xFF && p[0] == 11 && !memcmp(p + 1, "NETSCAPE2.0", 11) && p[12] == 3 && p[13] == 1)
                d->loop = true;
            while (p < end && *p)
                p += *p + 1;
            p++;
            continue;
        }
        if (*p != 0x2C)
            return -4;
        int x = u16(p + 1), y = u16(p + 3), w = u16(p + 5), h = u16(p + 7);
        if (p[9] != 0 || x + w > d->w || y + h > d->h || w < 1 || h < 1)
            return -5;
        p += 10;
        uint8_t *img = malloc((size_t)w * (size_t)h);
        if (lzw_decode(d, &p, end, img, w * h)) {
            free(img);
            return -6;
        }
        for (int r = 0; r < h; r++)
            memcpy(d->canvas + (y + r) * d->w + x, img + r * w, (size_t)w);
        free(img);
        if (d->frames < 256) {
            d->delays[d->frames] = delay;
            d->box[d->frames][0] = x, d->box[d->frames][1] = y, d->box[d->frames][2] = w, d->box[d->frames][3] = h;
        }
        if (d->frames < 64) {
            d->shots[d->frames] = malloc((size_t)d->w * (size_t)d->h);
            memcpy(d->shots[d->frames], d->canvas, (size_t)d->w * (size_t)d->h);
        }
        d->frames++;
    }
    return -7; /* no trailer */
}

static void dec_free(dec_t *d)
{
    free(d->canvas);
    for (int i = 0; i < 64; i++)
        free(d->shots[i]);
}

/* ------------------------------------------------------------ helpers */
static uint16_t c565(int r, int g, int b) { return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)); }
static void to888(uint16_t c, int o[3])
{
    int r = c >> 11, g = c >> 5 & 63, b = c & 31;
    o[0] = r << 3 | r >> 2, o[1] = g << 2 | g >> 4, o[2] = b << 3 | b >> 2;
}

/* Every pixel of shot k equals the source frame: exactly for the exact
 * colours, within the cube's half step for the rest. */
static int compare(const dec_t *d, int k, const uint16_t *src, int stride, bool exact_only)
{
    int bad = 0;
    for (int y = 0; y < d->h; y++)
        for (int x = 0; x < d->w; x++) {
            int idx = d->shots[k][y * d->w + x], o[3];
            to888(src[y * stride + x], o);
            for (int ch = 0; ch < 3; ch++) {
                int diff = abs(d->pal[idx][ch] - o[ch]);
                if (exact_only ? diff != 0 : diff > 26)
                    bad++;
            }
        }
    return bad;
}

static gif_t G; /* ~290 KB: static, as the module keeps it in its RW block */

/* A graph-like animation: white background, grey axes, a moving blue dot
 * and a red bar; few colours, so everything is exact. */
static void test_flat(void)
{
    enum { W = 160, H = 100, N = 6 };
    static uint16_t fr[N][H][W];
    uint16_t white = 0xFFFF, grey = c565(128, 128, 128), blue = c565(0, 0, 255), red = c565(255, 0, 0);
    for (int k = 0; k < N; k++) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                fr[k][y][x] = x == W / 2 || y == H / 2 ? grey : white;
        for (int y = 10 + 5 * k; y < 20 + 5 * k; y++)
            for (int x = 20 + 10 * k; x < 30 + 10 * k; x++)
                fr[k][y][x] = blue;
        for (int x = 0; x < 3 * k; x++)
            fr[k][H - 3][x] = red;
    }
    buf_t b = {NULL, 0, 0, -1};
    CHECK(gif_begin(&G, W, H, &fr[0][0][0], W, wr, &b) == 0);
    for (int k = 0; k < N; k++)
        CHECK(gif_frame(&G, &fr[k][0][0], W, 10 + k) == 0);
    CHECK(gif_frame(&G, &fr[N - 1][0][0], W, 50) == 0); /* unchanged */
    CHECK(gif_end(&G) == 0);
    CHECK(G.frames == N + 1 && G.bytes == b.n);
    dec_t d;
    int r = decode(b.b, b.n, &d);
    CHECK(r == 0);
    if (r == 0) {
        CHECK(d.w == W && d.h == H && d.frames == N + 1 && d.loop);
        for (int k = 0; k < N; k++) {
            CHECK(compare(&d, k, &fr[k][0][0], W, true) == 0);
            CHECK(d.delays[k] == 10 + k);
        }
        CHECK(compare(&d, N, &fr[N - 1][0][0], W, true) == 0);
        CHECK(d.box[N][2] == 1 && d.box[N][3] == 1 && d.delays[N] == 50); /* the unchanged frame */
        /* frame 1 is a box around what moved, not the whole image */
        CHECK(d.box[1][2] < W && d.box[1][3] < H);
        CHECK(d.box[0][2] == W && d.box[0][3] == H);
    }
    printf("flat: %d frames %dx%d -> %zu bytes (raw %d)\n", N + 1, W, H, b.n, (N + 1) * W * H);
    dec_free(&d);
    free(b.b);
}

/* A full-screen gradient plus noise: hundreds of colours (cube mapping) and
 * long LZW runs that need 12-bit codes and table clears. */
static void test_many_colours(void)
{
    enum { W = 320, H = 204 };
    static uint16_t fr[2][H][W];
    uint32_t s = 12345;
    for (int k = 0; k < 2; k++)
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                s = s * 1103515245u + 12345u;
                int noise = (int)(s >> 16) % 7;
                fr[k][y][x] = (x < 40 && y < 20) ? 0xFFFF
                                                 : c565((x * 255 / W + noise + 13 * k) % 256, y * 255 / H, (x + y) % 256);
            }
    buf_t b = {NULL, 0, 0, -1};
    CHECK(gif_begin(&G, W, H, &fr[0][0][0], W, wr, &b) == 0);
    CHECK(gif_frame(&G, &fr[0][0][0], W, 10) == 0);
    CHECK(gif_frame(&G, &fr[1][0][0], W, 10) == 0);
    CHECK(gif_end(&G) == 0);
    dec_t d;
    int r = decode(b.b, b.n, &d);
    CHECK(r == 0);
    if (r == 0) {
        CHECK(d.frames == 2);
        CHECK(compare(&d, 0, &fr[0][0][0], W, false) == 0);
        CHECK(compare(&d, 1, &fr[1][0][0], W, false) == 0);
        CHECK(d.max_bits == 12 && d.clears > 2); /* code width grew, the table was cleared */
        /* the most frequent colour (white corner) is exact */
        CHECK(d.pal[gif_index(&G, 0xFFFF)][0] == 255 && gif_index(&G, 0xFFFF) < GIF_NEXACT);
    }
    printf("many colours: 2 frames %dx%d -> %zu bytes, max code %d bits, %d clears\n", W, H, b.n, d.max_bits,
           d.clears);
    dec_free(&d);
    free(b.b);
}

/* The palette: exact slots first, then the cube; an unknown colour maps to
 * the nearest cube level per channel. */
static void test_palette(void)
{
    uint16_t px[4] = {c565(10, 200, 30), c565(10, 200, 30), 0x0000, 0xFFFF};
    buf_t b = {NULL, 0, 0, -1};
    CHECK(gif_begin(&G, 4, 1, px, 4, wr, &b) == 0);
    CHECK(gif_index(&G, px[0]) == 0); /* most frequent first */
    int idx = gif_index(&G, c565(250, 100, 0));
    CHECK(idx >= GIF_NEXACT);
    CHECK(G.pal[idx][0] == 255 && G.pal[idx][1] == 102 && G.pal[idx][2] == 0);
    CHECK(gif_begin(&G, GIF_MAXW + 1, 1, px, 4, wr, &b) != 0); /* too wide */
    free(b.b);
}

/* A failing write (card full) stops the stream with the error. */
static void test_write_error(void)
{
    static uint16_t px[64 * 64];
    for (int i = 0; i < 64 * 64; i++)
        px[i] = (uint16_t)(i * 37);
    buf_t b = {NULL, 0, 0, 900};
    gif_begin(&G, 64, 64, px, 64, wr, &b);
    int r = gif_frame(&G, px, 64, 10);
    CHECK(r == -5 && gif_end(&G) == -5);
    CHECK(b.n <= 900);
    free(b.b);
}

int main(void)
{
    test_flat();
    test_many_colours();
    test_palette();
    test_write_error();
    printf("test_gif: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
