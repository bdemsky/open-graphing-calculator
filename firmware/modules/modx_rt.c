/*
 * The few runtime functions the compiler may call by itself in a code module
 * image (struct copies and clears become memcpy / memset calls even with
 * -ffreestanding). Linked into every module image, never into the core or
 * the simulators (they have the C library). Hidden like everything in the
 * image (-fvisibility=hidden), so the calls stay PC-relative.
 */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d < s)
        while (n--)
            *d++ = *s++;
    else
        while (n--)
            d[n] = s[n];
    return dst;
}

/* word stores for the large clears (the GIF map is 128 KB) */
void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    uint8_t b = (uint8_t)c;
    while (n && ((uintptr_t)d & 3))
        *d++ = b, n--;
    uint32_t w = b * 0x01010101u;
    for (; n >= 4; n -= 4, d += 4)
        *(volatile uint32_t *)d = w; /* volatile: keep GCC from turning this loop into a memset call */
    while (n--)
        *d++ = b;
    return dst;
}
