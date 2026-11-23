/* Screen-dump compression: PackBits-style RLE over 16-bit pixels.
 *   header byte h:
 *     h & 0x80: run     — (h & 0x7F) + 1 copies of the next pixel (2 bytes LE)
 *     else    : literal — h + 1 pixels follow (2 bytes LE each)
 *
 * Used by the console `screen` command (core/console.c), which streams the
 * encoded framebuffer as base64; tools/calcctl.py --shot decodes it on the
 * host. A UI screen (large flat areas) typically shrinks from 150 KB to
 * 3–15 KB, which matters at 115200 baud.
 *
 * This header also exports the CRC-32 used across the firmware (screen dumps,
 * pstore snapshot headers and payloads).
 */
#ifndef RLE_H
#define RLE_H
#include <stddef.h>
#include <stdint.h>

/* Output sink: receives each encoded block (1 header byte + pixel bytes, at
 * most 257 bytes) as soon as it is complete. */
typedef void (*rle_emit_fn)(const uint8_t *data, size_t n, void *user);
/* Encode n pixels; returns the encoded size in bytes. emit may be NULL (size only). */
size_t rle_encode(const uint16_t *px, size_t n, rle_emit_fn emit, void *user);
/* Decode into out (capacity max pixels); returns pixels written or -1 on error. */
long rle_decode(const uint8_t *in, size_t len, uint16_t *out, size_t max);

/* Standard CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320; the same value
 * as zlib's crc32()/Python's zlib.crc32). Start with 0; pass the previous
 * result to continue a CRC over several buffers. */
uint32_t crc32_update(uint32_t crc, const void *data, size_t n); /* start with 0 */

#endif
