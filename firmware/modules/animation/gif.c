/*
 * Animated GIF encoder: see gif.h for the format decisions (palette, frame
 * differences, LZW). Part of the Animation code module: no static data, no
 * library calls (the module image may not have any: modules/modx_image.ld),
 * everything in the caller's gif_t.
 */
#include "gif.h"
#include <string.h>

/* ------------------------------------------------------------ output */
/* Write n bytes through the callback, counting them; after an error nothing
 * more is written. */
static void put(gif_t *g, const void *b, size_t n)
{
    if (g->err)
        return;
    g->err = g->write(g->user, b, n);
    g->bytes += (uint32_t)n;
}

static void put16(gif_t *g, int v)
{
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    put(g, b, 2);
}

/* one byte of LZW data into the current sub-block (flushed when full) */
static void blk_byte(gif_t *g, uint8_t b)
{
    g->blk[++g->blk[0]] = b;
    if (g->blk[0] == 255) {
        put(g, g->blk, 256);
        g->blk[0] = 0;
    }
}

/* ------------------------------------------------------------ palette */
static uint8_t widen5(int v) { return (uint8_t)(v << 3 | v >> 2); }
static uint8_t widen6(int v) { return (uint8_t)(v << 2 | v >> 4); }
static void rgb(uint16_t c, uint8_t out[3])
{
    out[0] = widen5(c >> 11), out[1] = widen6(c >> 5 & 63), out[2] = widen5(c & 31);
}

/* nearest cube level (0..5) of an 8-bit channel */
static int level6(int v) { return (v * 5 + 127) / 255; }

int gif_index(gif_t *g, uint16_t c)
{
    uint16_t m = g->map[c];
    if (m != 0xFFFF)
        return m;
    uint8_t p[3];
    rgb(c, p);
    m = (uint16_t)(GIF_NEXACT + level6(p[0]) * 36 + level6(p[1]) * 6 + level6(p[2]));
    g->map[c] = m;
    return m;
}

/* Palette from the first frame: count its distinct colours (up to
 * GIF_HSIZE of them, list in codetab, counts in htab, list index cached in
 * map), take the GIF_NEXACT most frequent as exact entries, fill the cube,
 * then reset map to "exact entries only". */
static void build_palette(gif_t *g, const uint16_t *px, int stride)
{
    memset(g->map, 0xFF, sizeof g->map);
    int nd = 0;
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            uint16_t c = px[y * stride + x];
            uint16_t k = g->map[c];
            if (k == 0xFFFF) {
                if (nd >= GIF_HSIZE)
                    continue;
                k = (uint16_t)nd++;
                g->map[c] = k;
                g->codetab[k] = c;
                g->htab[k] = 0;
            }
            g->htab[k]++;
        }
    memset(g->map, 0xFF, sizeof g->map);
    memset(g->pal, 0, sizeof g->pal);
    for (int e = 0; e < GIF_NEXACT && e < nd; e++) {
        int best = -1;
        for (int k = 0; k < nd; k++)
            if (g->htab[k] > 0 && (best < 0 || g->htab[k] > g->htab[best]))
                best = k;
        if (best < 0)
            break;
        g->htab[best] = -1; /* taken */
        rgb(g->codetab[best], g->pal[e]);
        g->map[g->codetab[best]] = (uint16_t)e;
    }
    for (int i = 0; i < 216; i++) {
        g->pal[GIF_NEXACT + i][0] = (uint8_t)(i / 36 * 51);
        g->pal[GIF_NEXACT + i][1] = (uint8_t)(i / 6 % 6 * 51);
        g->pal[GIF_NEXACT + i][2] = (uint8_t)(i % 6 * 51);
    }
}

/* ------------------------------------------------------------ LZW */
#define CLEAR_CODE 256
#define EOI_CODE 257
#define MAXBITS 12
#define MAXMAX (1 << MAXBITS)

static void cl_hash(gif_t *g)
{
    for (int i = 0; i < GIF_HSIZE; i++)
        g->htab[i] = -1;
}

/* Emit one code at the current width; then widen the code (or reset it
 * after a clear) as described in gif.h. */
static void output(gif_t *g, int code)
{
    g->acc |= (uint32_t)code << g->nacc;
    g->nacc += g->n_bits;
    while (g->nacc >= 8) {
        blk_byte(g, (uint8_t)g->acc);
        g->acc >>= 8;
        g->nacc -= 8;
    }
    if (g->clear_flg) {
        g->n_bits = 9, g->maxcode = (1 << 9) - 1;
        g->clear_flg = false;
    } else if (g->free_ent > g->maxcode) {
        g->n_bits++;
        g->maxcode = g->n_bits == MAXBITS ? MAXMAX : (1 << g->n_bits) - 1;
    }
}

/* LZW-compress the w x h box at (x0, y0) of g->cur as one image's data. */
static void lzw_box(gif_t *g, int x0, int y0, int w, int h)
{
    uint8_t init = 8;
    put(g, &init, 1);
    g->n_bits = 9, g->maxcode = (1 << 9) - 1;
    g->free_ent = EOI_CODE + 1;
    g->clear_flg = false;
    g->acc = 0, g->nacc = 0;
    g->blk[0] = 0;
    cl_hash(g);
    output(g, CLEAR_CODE);
    int ent = -1;
    for (int y = y0; y < y0 + h; y++) {
        const uint8_t *row = g->cur + y * g->w;
        for (int x = x0; x < x0 + w; x++) {
            int c = row[x];
            if (ent < 0) {
                ent = c;
                continue;
            }
            int32_t fcode = ((int32_t)c << MAXBITS) + ent;
            int i = (c << 4) ^ ent; /* xor hashing (GIFENCOD, hshift 4 for HSIZE 5003) */
            if (g->htab[i] == fcode) {
                ent = g->codetab[i];
                continue;
            }
            if (g->htab[i] >= 0) { /* secondary probe */
                int disp = i == 0 ? 1 : GIF_HSIZE - i;
                bool found = false;
                do {
                    if ((i -= disp) < 0)
                        i += GIF_HSIZE;
                    if (g->htab[i] == fcode) {
                        ent = g->codetab[i];
                        found = true;
                        break;
                    }
                } while (g->htab[i] >= 0);
                if (found)
                    continue;
            }
            output(g, ent);
            ent = c;
            if (g->free_ent < MAXMAX) {
                g->codetab[i] = (uint16_t)g->free_ent++;
                g->htab[i] = fcode;
            } else { /* table full: clear it */
                cl_hash(g);
                g->free_ent = EOI_CODE + 1;
                g->clear_flg = true;
                output(g, CLEAR_CODE);
            }
        }
    }
    output(g, ent);
    output(g, EOI_CODE);
    if (g->nacc > 0)
        blk_byte(g, (uint8_t)g->acc);
    if (g->blk[0])
        put(g, g->blk, (size_t)g->blk[0] + 1);
    uint8_t term = 0;
    put(g, &term, 1);
}

/* ------------------------------------------------------------ stream */
int gif_begin(gif_t *g, int w, int h, const uint16_t *px, int stride, gif_write_fn wr, void *user)
{
    g->write = wr, g->user = user;
    g->err = 0, g->bytes = 0, g->frames = 0, g->have_prev = false;
    if (w < 1 || h < 1 || w > GIF_MAXW || h > GIF_MAXH)
        return g->err = -1;
    g->w = w, g->h = h;
    build_palette(g, px, stride);
    put(g, "GIF89a", 6);
    put16(g, w);
    put16(g, h);
    uint8_t lsd[3] = {0xF7, 0, 0}; /* global table, 8-bit colour resolution, 256 entries */
    put(g, lsd, 3);
    put(g, g->pal, sizeof g->pal);
    static const uint8_t loop[19] = {0x21, 0xFF, 11, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0',
                                     3, 1, 0, 0, 0}; /* loop forever */
    put(g, loop, sizeof loop);
    return g->err;
}

int gif_frame(gif_t *g, const uint16_t *px, int stride, int delay_cs)
{
    if (g->err)
        return g->err;
    /* palette indices of the frame, and the box of what changed */
    int bx0 = g->w, by0 = g->h, bx1 = -1, by1 = -1;
    for (int y = 0; y < g->h; y++) {
        const uint16_t *src = px + y * stride;
        uint8_t *dst = g->cur + y * g->w, *old = g->prev + y * g->w;
        for (int x = 0; x < g->w; x++) {
            uint16_t c = src[x];
            uint16_t m = g->map[c];
            dst[x] = (uint8_t)(m != 0xFFFF ? m : gif_index(g, c));
            if (!g->have_prev || dst[x] != old[x]) {
                if (x < bx0) bx0 = x;
                if (x > bx1) bx1 = x;
                if (y < by0) by0 = y;
                if (y > by1) by1 = y;
            }
        }
    }
    if (bx1 < 0) /* nothing changed: a 1 x 1 frame keeps the timing */
        bx0 = bx1 = by0 = by1 = 0;
    uint8_t gce[8] = {0x21, 0xF9, 4, 0x04 /* disposal 1: keep */, (uint8_t)delay_cs, (uint8_t)(delay_cs >> 8), 0, 0};
    put(g, gce, sizeof gce);
    uint8_t sep = 0x2C;
    put(g, &sep, 1);
    put16(g, bx0);
    put16(g, by0);
    put16(g, bx1 - bx0 + 1);
    put16(g, by1 - by0 + 1);
    uint8_t packed = 0;
    put(g, &packed, 1);
    lzw_box(g, bx0, by0, bx1 - bx0 + 1, by1 - by0 + 1);
    memcpy(g->prev, g->cur, (size_t)g->w * (size_t)g->h);
    g->have_prev = true;
    g->frames++;
    return g->err;
}

int gif_end(gif_t *g)
{
    uint8_t t = 0x3B;
    put(g, &t, 1);
    return g->err;
}
