/* Minimal dependency-free PNG writer (uncompressed deflate).
 *
 * The simulator writes screenshots for the UI tests (tests/ui -> sim/screenshots/)
 * and the --live panel image. Compression does not matter there, so the image
 * data goes into a zlib stream made only of "stored" deflate blocks (RFC 1951
 * section 3.2.4, BTYPE=00): no Huffman coding, no dependency on zlib. Files are
 * ~3 bytes/pixel (about 230 KB for 320x240).
 *
 * Layout: 8-byte signature, IHDR (8-bit RGB truecolour, no interlace), one
 * IDAT with the whole zlib stream, IEND (PNG spec / RFC 2083). */
#include "png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rle.h" /* crc32_update: same CRC-32 as PNG/zlib */

/* Store v big-endian (PNG's byte order for lengths, CRCs and IHDR fields). */
static void be32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v >> 24), b[1] = (uint8_t)(v >> 16), b[2] = (uint8_t)(v >> 8), b[3] = (uint8_t)v;
}

/* Write one PNG chunk: length, 4-char type, data, CRC-32 over type + data
 * (the length is not covered by the CRC). Write errors are left in the
 * stream's error flag and checked once by the caller (ferror). */
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t hdr[8];
    be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len)
        fwrite(data, 1, len, f);
    uint32_t crc = crc32_update(0, type, 4);
    crc = crc32_update(crc, data, len);
    uint8_t c[4];
    be32(c, crc);
    fwrite(c, 1, 4, f);
}

int png_write_rgb565(const char *path, const uint16_t *px, int w, int h, int stride)
{
    /* 1. Raw scanlines: each row is a filter-type byte (0 = None) followed by
     *    w RGB triplets. RGB565 is widened to 8 bits per channel by bit
     *    replication (x << 3 | x >> 2 for 5 bits, x << 2 | x >> 4 for 6 bits),
     *    so 0x1F -> 0xFF and 0 -> 0: full white and black are exact. */
    size_t rowlen = (size_t)w * 3 + 1;
    size_t rawlen = rowlen * (size_t)h;
    uint8_t *raw = malloc(rawlen);
    if (!raw)
        return -1;
    for (int y = 0; y < h; y++) {
        uint8_t *r = raw + (size_t)y * rowlen;
        *r++ = 0; /* filter: none */
        for (int x = 0; x < w; x++) {
            uint16_t p = px[y * stride + x];
            uint8_t R = (uint8_t)((p >> 11) & 31), G = (uint8_t)((p >> 5) & 63), B = (uint8_t)(p & 31);
            *r++ = (uint8_t)((R << 3) | (R >> 2));
            *r++ = (uint8_t)((G << 2) | (G >> 4));
            *r++ = (uint8_t)((B << 3) | (B >> 2));
        }
    }
    /* 2. zlib stream with stored blocks of <= 65535 bytes (RFC 1950/1951):
     *    2-byte header, per block a 5-byte header (BFINAL/BTYPE byte, LEN,
     *    NLEN = ~LEN, little-endian) + the bytes, then the Adler-32 checksum
     *    of the uncompressed data, big-endian. */
    size_t nblocks = (rawlen + 65534) / 65535;
    size_t zlen = 2 + rawlen + nblocks * 5 + 4;
    uint8_t *z = malloc(zlen);
    if (!z) {
        free(raw);
        return -1;
    }
    size_t o = 0;
    /* CMF 0x78 = deflate with a 32 KB window; FLG 0x01 makes the 16-bit
     * header a multiple of 31 (FCHECK) and selects "fastest" compression. */
    z[o++] = 0x78;
    z[o++] = 0x01;
    /* Adler-32: a = 1 + sum of bytes, b = sum of the running a, both mod 65521. */
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < rawlen; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t off = 0; off < rawlen; off += 65535) {
        size_t n = rawlen - off > 65535 ? 65535 : rawlen - off;
        z[o++] = off + n == rawlen ? 1 : 0; /* BFINAL on the last block; BTYPE 00 = stored */
        z[o++] = (uint8_t)(n & 0xFF);
        z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)(~n & 0xFF);
        z[o++] = (uint8_t)((~n >> 8) & 0xFF);
        memcpy(z + o, raw + off, n);
        o += n;
    }
    be32(z + o, (b << 16) | a);
    o += 4;

    /* 3. The PNG file around it. */
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(raw);
        free(z);
        return -1;
    }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* truecolour */
    ihdr[10] = 0, ihdr[11] = 0, ihdr[12] = 0; /* deflate, adaptive filtering, no interlace */
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)o);
    chunk(f, "IEND", NULL, 0);
    int rc = ferror(f) ? -1 : 0;
    fclose(f);
    free(raw);
    free(z);
    return rc;
}
