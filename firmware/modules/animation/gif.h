/*
 * Animated GIF encoder (GIF89a) for the Animation module: RGB565 frames in,
 * a byte stream out through a callback (the module writes it to the microSD
 * card, the unit test into memory). No allocation: all state, tables and the
 * two index buffers live in gif_t, which the module keeps in its RW block in
 * SDRAM (~290 KB for 320 x 204 frames).
 *
 * Palette (one global table of 256 colours, fixed by the first frame):
 *   slots 0..GIF_NEXACT-1  the most frequent colours of the first frame,
 *                          exactly (graphs use a few flat colours, so plots,
 *                          axes and text come out unchanged);
 *   slots GIF_NEXACT..255  a 6x6x6 colour cube (levels 0, 51, ..., 255).
 * Every RGB565 value maps to its exact slot if it has one, else to the
 * nearest cube colour per channel; the mapping is cached in map[] (65536
 * entries). RGB565 -> RGB888 widens by bit replication, as the PNG writer
 * (app/sdbk.c) does.
 *
 * Frames: each frame after the first stores only the bounding box of the
 * pixels whose palette index changed (disposal "do not dispose", so the
 * previous frame shows through), which keeps animations of a moving curve
 * over fixed axes small. A frame with no change is a 1 x 1 box (it still
 * carries its delay). Bounded: at most GIF_MAXW x GIF_MAXH pixels.
 *
 * LZW (GIF89a appendix F): minimum code size 8, clear = 256, end = 257,
 * variable code width 9..12 bits, LSB-first bit packing in sub-blocks of at
 * most 255 bytes. The string table is the hashed open-addressing table of
 * the classic Unix compress / GIFENCOD (HSIZE 5003 entries, key = (pixel <<
 * 12) + prefix code, secondary probe by HSIZE - i); when 4096 codes are in
 * use a clear code starts a new table. The width grows after the code that
 * makes the next free code exceed 2^width - 1, which matches the decoder's
 * "one code behind" table, as every GIF encoder does.
 */
#ifndef GIF_H
#define GIF_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GIF_MAXW 320
#define GIF_MAXH 240
#define GIF_NEXACT 40
#define GIF_HSIZE 5003

typedef int (*gif_write_fn)(void *user, const void *buf, size_t n); /* 0 = ok */

typedef struct {
    gif_write_fn write;
    void *user;
    int err;               /* first write error (then everything stops) */
    int w, h;
    uint32_t bytes;        /* written so far */
    int frames;
    bool have_prev;
    uint8_t pal[256][3];
    uint16_t map[65536];   /* RGB565 -> palette index, 0xFFFF = not computed yet */
    uint8_t cur[GIF_MAXW * GIF_MAXH], prev[GIF_MAXW * GIF_MAXH];
    /* LZW */
    int32_t htab[GIF_HSIZE];
    uint16_t codetab[GIF_HSIZE];
    int n_bits, maxcode, free_ent;
    bool clear_flg;
    uint32_t acc;          /* bit accumulator */
    int nacc;
    uint8_t blk[256];      /* sub-block being filled (blk[0] = length) */
} gif_t;

/* Start a GIF of w x h pixels (<= GIF_MAXW x GIF_MAXH): the palette from the
 * first frame px (row stride `stride` pixels), header, global colour table
 * and the NETSCAPE2.0 loop-forever extension. The frame itself is written by
 * the first gif_frame call. 0 or the callback's error. */
int gif_begin(gif_t *g, int w, int h, const uint16_t *px, int stride, gif_write_fn wr, void *user);
/* Append a frame shown for delay_cs hundredths of a second. */
int gif_frame(gif_t *g, const uint16_t *px, int stride, int delay_cs);
/* Trailer; returns 0 or the first error. */
int gif_end(gif_t *g);
/* RGB565 -> palette index (exposed for the unit test) */
int gif_index(gif_t *g, uint16_t c);

#endif
