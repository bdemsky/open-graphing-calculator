/*
 * PNG writer and base64 (app/png.c): the notebook's pictures, both for NOTEBOOK.HTML (app/nbhtml.c)
 * and for the .ipynb the Python module writes (modules/python/nb_doc.c). One source: the core links
 * it, and the Python module's image compiles the same file (with core/rle.c for the CRC), since a
 * code-load module calls no core code but through its API tables.
 */
#ifndef PNG_H
#define PNG_H
#include <stddef.h>
#include <stdint.h>

typedef void (*png_sink_fn)(void *u, const void *s, size_t n);

/* 8-bit indexed PNG of w x h pixels (pal: RGB565, npal <= 256) into put(). */
void png_indexed(const uint8_t *pix, int w, int h, const uint16_t *pal, int npal, png_sink_fn put, void *u);

/* base64 in pieces: b64_put is a png_sink_fn, so a PNG can be written straight into it */
typedef struct {
    png_sink_fn put;
    void *u;
    uint8_t q[3];
    int n;
} b64_t;
void b64_init(b64_t *b, png_sink_fn put, void *u);
void b64_put(void *b64, const void *p, size_t n);
void b64_end(b64_t *b);

#endif
