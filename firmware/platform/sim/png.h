#ifndef SIM_PNG_H
#define SIM_PNG_H
#include <stdint.h>
/* Write an RGB565 image as a 24-bit PNG (stored deflate blocks). 0 = ok.
 * `px` is row-major with `stride` pixels per row (LCD_WIDTH for the
 * framebuffer); `w` x `h` pixels starting at px[0] are written. Used by the
 * simulator's `screenshot` command and the --live panel image. */
int png_write_rgb565(const char *path, const uint16_t *px, int w, int h, int stride);
#endif
