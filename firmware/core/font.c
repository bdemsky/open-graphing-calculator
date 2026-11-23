/* Glyph lookup and UTF-8 decoding for the fixed-cell bitmap fonts (font.h). */
#include "font.h"
#include <stddef.h>

/* Binary search of the sorted codepoint table. Pass 0 looks for cp; if that
 * misses, pass 1 looks for '?' instead. */
const uint8_t *font_glyph(const font_t *f, uint32_t cp)
{
    int lo = 0, hi = f->nglyphs - 1;
    for (int pass = 0; pass < 2; pass++) {
        lo = 0;
        hi = f->nglyphs - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (f->cp[mid] == cp)
                return f->bits + (size_t)mid * f->h * f->bytes_per_row;
            if (f->cp[mid] < cp)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
        cp = '?';
    }
    return f->bits; /* glyph 0 = space (lowest codepoint in every font) */
}

uint32_t utf8_next(const char **sp)
{
    const unsigned char *s = (const unsigned char *)*sp;
    uint32_t c = s[0];
    if (c == 0)
        return 0;
    /* Lead byte: number of continuation bytes n and the payload bits it carries
     * (0xxxxxxx, 110xxxxx, 1110xxxx, 11110xxx). Anything else (a stray
     * continuation byte 10xxxxxx, or 0xF8..0xFF) is consumed as one '?'. */
    int n = 0;
    if (c < 0x80) {
        n = 0;
    } else if ((c & 0xE0) == 0xC0) {
        c &= 0x1F;
        n = 1;
    } else if ((c & 0xF0) == 0xE0) {
        c &= 0x0F;
        n = 2;
    } else if ((c & 0xF8) == 0xF0) {
        c &= 0x07;
        n = 3;
    } else {
        *sp += 1;
        return '?';
    }
    /* Each continuation byte (10xxxxxx) adds 6 payload bits. Stop at the first
     * byte that is not one (this includes the NUL terminator, so we never read
     * past the end of the string). */
    int i;
    for (i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80)
            break;
        c = (c << 6) | (s[i] & 0x3F);
    }
    if (i <= n) { /* truncated sequence: skip the lead and the valid continuations only */
        *sp += i;
        return '?';
    }
    *sp += n + 1;
    return c;
}

int utf8_len(const char *s)
{
    int n = 0;
    while (utf8_next(&s))
        n++;
    return n;
}
