/* PackBits-style RLE of RGB565 pixels and CRC-32 (format in rle.h). */
#include "rle.h"

/* Store one pixel little-endian (independent of the host's byte order). */
static void put_px(uint8_t *b, uint16_t p)
{
    b[0] = (uint8_t)(p & 0xFF);
    b[1] = (uint8_t)(p >> 8);
}

/* Greedy encoder: at each position, a run of >= 2 equal pixels becomes a run
 * block (3 bytes for up to 128 pixels); otherwise collect literals until the
 * next pair of equal pixels or 128 pixels. A 2-pixel run costs 3 bytes instead
 * of 4 as literals, so starting runs at length 2 never loses. Each block is
 * emitted from a stack buffer as soon as it is complete, so the caller can
 * stream without holding the whole output. */
size_t rle_encode(const uint16_t *px, size_t n, rle_emit_fn emit, void *user)
{
    size_t out = 0, i = 0;
    uint8_t buf[1 + 2 * 128]; /* largest block: header + 128 literal pixels */
    while (i < n) {
        /* length of the run of equal pixels starting at i (capped at 128) */
        size_t r = 1;
        while (i + r < n && r < 128 && px[i + r] == px[i])
            r++;
        if (r >= 2) {
            buf[0] = (uint8_t)(0x80 | (r - 1));
            put_px(buf + 1, px[i]);
            if (emit)
                emit(buf, 3, user);
            out += 3;
            i += r;
            continue;
        }
        /* literal: until a run of >= 2 starts or 128 pixels */
        size_t l = 0;
        while (i + l < n && l < 128) {
            if (i + l + 1 < n && px[i + l] == px[i + l + 1])
                break;
            put_px(buf + 1 + 2 * l, px[i + l]);
            l++;
        }
        buf[0] = (uint8_t)(l - 1);
        if (emit)
            emit(buf, 1 + 2 * l, user);
        out += 1 + 2 * l;
        i += l;
    }
    return out;
}

/* Inverse of rle_encode. Validates every block against both the input length
 * and the output capacity, so a truncated or corrupt stream returns -1 instead
 * of overrunning a buffer. */
long rle_decode(const uint8_t *in, size_t len, uint16_t *out, size_t max)
{
    size_t i = 0, o = 0;
    while (i < len) {
        uint8_t h = in[i++];
        size_t cnt = (size_t)(h & 0x7F) + 1;
        if (o + cnt > max)
            return -1;
        if (h & 0x80) {
            if (i + 2 > len)
                return -1;
            uint16_t p = (uint16_t)(in[i] | (in[i + 1] << 8));
            i += 2;
            for (size_t k = 0; k < cnt; k++)
                out[o++] = p;
        } else {
            if (i + 2 * cnt > len)
                return -1;
            for (size_t k = 0; k < cnt; k++, i += 2)
                out[o++] = (uint16_t)(in[i] | (in[i + 1] << 8));
        }
    }
    return (long)o;
}

/* Reflected CRC-32 computed a nibble at a time with a 16-entry table (64 bytes
 * instead of the usual 1 KB byte table; speed is not critical here). t[i] is
 * the remainder after shifting the low nibble i through four bit-steps of the
 * polynomial division, so two table steps consume one byte. The
 * pre/post inversion makes the function chainable: crc32_update(crc32_update(0,
 * a), b) equals the CRC of a followed by b. */
uint32_t crc32_update(uint32_t crc, const void *data, size_t n)
{
    static const uint32_t t[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
    const uint8_t *p = data;
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ t[crc & 15];
        crc = (crc >> 4) ^ t[crc & 15];
    }
    return ~crc;
}
