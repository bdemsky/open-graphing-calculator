/*
 * PNG for the notebook's pictures (png.h): 8-bit indexed colour, one zlib stream
 * in a single deflate block with the fixed Huffman codes (RFC 1951 3.2.6).
 * The only matches searched are the two a plot is made of: a run of the same
 * colour (distance 1) and the row above (distance = the row's bytes), which
 * makes a plot of 288 x 132 pixels a couple of kilobytes. Base64 for the
 * notebook's JSON follows.
 */
#include "png.h"
#include <string.h>
#include "rle.h"

typedef struct {
    png_sink_fn put;
    void *u;
    uint32_t bits, nbits; /* deflate's bit buffer (LSB first) */
    uint8_t z[2048];      /* zlib bytes waiting for their IDAT chunk */
    size_t zn;
    uint32_t a1, a2;      /* Adler-32 */
} png_t;

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v; }

static void chunk(png_t *g, const char *type, const uint8_t *d, size_t n)
{
    uint8_t h[8];
    be32(h, (uint32_t)n);
    memcpy(h + 4, type, 4);
    g->put(g->u, h, 8);
    if (n)
        g->put(g->u, d, n);
    uint32_t crc = crc32_update(crc32_update(0, type, 4), d, n);
    be32(h, crc);
    g->put(g->u, h, 4);
}

static void zbyte(png_t *g, uint8_t b)
{
    g->z[g->zn++] = b;
    if (g->zn == sizeof g->z) {
        chunk(g, "IDAT", g->z, g->zn);
        g->zn = 0;
    }
}
static void bits(png_t *g, uint32_t v, int n)
{
    g->bits |= v << g->nbits;
    g->nbits += (uint32_t)n;
    while (g->nbits >= 8) {
        zbyte(g, (uint8_t)g->bits);
        g->bits >>= 8, g->nbits -= 8;
    }
}
/* a Huffman code: most significant bit first */
static void code(png_t *g, uint32_t c, int n)
{
    uint32_t r = 0;
    for (int i = 0; i < n; i++)
        r |= ((c >> i) & 1) << (n - 1 - i);
    bits(g, r, n);
}
static void lit(png_t *g, int v) /* a literal/length symbol of the fixed code */
{
    if (v < 144)
        code(g, 0x30u + (uint32_t)v, 8);
    else if (v < 256)
        code(g, 0x190u + (uint32_t)(v - 144), 9);
    else if (v < 280)
        code(g, (uint32_t)(v - 256), 7);
    else
        code(g, 0xC0u + (uint32_t)(v - 280), 8);
}
static void match(png_t *g, int len, int dist)
{
    static const uint16_t lb[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99,
                                    115, 131, 163, 195, 227, 258};
    static const uint8_t le[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t db[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025,
                                    1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t de[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12,
                                   13, 13};
    int i = 28;
    while (lb[i] > len)
        i--;
    lit(g, 257 + i);
    bits(g, (uint32_t)(len - lb[i]), le[i]);
    int j = 29;
    while (db[j] > dist)
        j--;
    code(g, (uint32_t)j, 5);
    bits(g, (uint32_t)(dist - db[j]), de[j]);
}

/* the filtered image (filter 0: a 0 byte before each row) as one byte stream */
typedef struct {
    const uint8_t *pix;
    int w;
    uint32_t stride;
} raw_t;
static uint8_t raw(const raw_t *r, uint32_t i)
{
    uint32_t x = i % r->stride;
    return x ? r->pix[(i / r->stride) * (uint32_t)r->w + x - 1] : 0;
}

void png_indexed(const uint8_t *pix, int w, int h, const uint16_t *pal, int npal, png_sink_fn put, void *u)
{
    png_t g;
    memset(&g, 0, sizeof g);
    g.put = put, g.u = u;
    g.a1 = 1;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    put(u, sig, 8);
    uint8_t ih[13] = {0};
    be32(ih, (uint32_t)w), be32(ih + 4, (uint32_t)h);
    ih[8] = 8, ih[9] = 3; /* 8 bits, indexed colour; deflate, adaptive filtering, no interlace */
    chunk(&g, "IHDR", ih, 13);
    uint8_t pl[256 * 3];
    for (int i = 0; i < npal && i < 256; i++) {
        unsigned c = pal[i], r5 = c >> 11, g6 = (c >> 5) & 63, b5 = c & 31;
        pl[3 * i] = (uint8_t)(r5 << 3 | r5 >> 2), pl[3 * i + 1] = (uint8_t)(g6 << 2 | g6 >> 4),
        pl[3 * i + 2] = (uint8_t)(b5 << 3 | b5 >> 2);
    }
    chunk(&g, "PLTE", pl, (size_t)npal * 3);

    zbyte(&g, 0x78), zbyte(&g, 0x01); /* zlib: deflate, 32 KB window, no dictionary */
    bits(&g, 1, 1), bits(&g, 1, 2);    /* the final block, fixed codes */
    raw_t r = {pix, w, (uint32_t)w + 1};
    uint32_t total = r.stride * (uint32_t)h;
    for (uint32_t i = 0; i < total;) {
        int best = 0, bd = 0;
        static const int dists[2] = {1, 0};
        for (int k = 0; k < 2; k++) {
            uint32_t d = dists[k] ? (uint32_t)dists[k] : r.stride;
            if (d > i)
                continue;
            int len = 0;
            while (len < 258 && i + (uint32_t)len < total && raw(&r, i + (uint32_t)len) == raw(&r, i + (uint32_t)len - d))
                len++;
            if (len > best)
                best = len, bd = (int)d;
        }
        if (best >= 3) {
            match(&g, best, bd);
        } else {
            best = 1;
            lit(&g, raw(&r, i));
        }
        for (int k = 0; k < best; k++, i++) {
            g.a1 = (g.a1 + raw(&r, i)) % 65521u;
            g.a2 = (g.a2 + g.a1) % 65521u;
        }
    }
    lit(&g, 256); /* the end of the block */
    if (g.nbits)
        bits(&g, 0, 8 - (int)g.nbits);
    uint8_t ad[4];
    be32(ad, g.a2 << 16 | g.a1);
    for (int k = 0; k < 4; k++)
        zbyte(&g, ad[k]);
    if (g.zn)
        chunk(&g, "IDAT", g.z, g.zn);
    chunk(&g, "IEND", NULL, 0);
}

/* ---------------------------------------------------------------- base64 */
static const char b64c[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
void b64_init(b64_t *b, png_sink_fn put, void *u)
{
    b->put = put, b->u = u, b->n = 0;
}
void b64_put(void *bp, const void *p, size_t n)
{
    b64_t *b = bp;
    const uint8_t *s = p;
    char o[64];
    size_t k = 0;
    while (n--) {
        b->q[b->n++] = *s++;
        if (b->n == 3) {
            o[k++] = b64c[b->q[0] >> 2];
            o[k++] = b64c[(b->q[0] & 3) << 4 | b->q[1] >> 4];
            o[k++] = b64c[(b->q[1] & 15) << 2 | b->q[2] >> 6];
            o[k++] = b64c[b->q[2] & 63];
            b->n = 0;
            if (k == sizeof o) {
                b->put(b->u, o, k);
                k = 0;
            }
        }
    }
    if (k)
        b->put(b->u, o, k);
}
void b64_end(b64_t *b)
{
    if (!b->n)
        return;
    char o[4];
    uint8_t q1 = b->n > 1 ? b->q[1] : 0;
    o[0] = b64c[b->q[0] >> 2];
    o[1] = b64c[(b->q[0] & 3) << 4 | q1 >> 4];
    o[2] = b->n > 1 ? b64c[(q1 & 15) << 2] : '=';
    o[3] = '=';
    b->put(b->u, o, 4);
    b->n = 0;
}
