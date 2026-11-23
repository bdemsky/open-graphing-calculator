/* Functions exported from the WebAssembly build (called from JavaScript as
 * Module._web_xxx, or through the wrapper in web/page/calc_api.js), used by
 * the browser page (web/page/calc_page.js) and the node test runner
 * (web/test/run_web_test.js). Implemented in web_hal.c.
 *
 * Every call that feeds input (web_frame, web_key, web_touch*) runs the main
 * loop (calc_step) until the app is idle before returning, so the panel is
 * current when the call returns. Returned pointers point into wasm memory and
 * stay valid, except web_console_take()'s, which is freed by its next call. */
#ifndef WEB_API_H
#define WEB_API_H
#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WEB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WEB_EXPORT
#endif

int web_init(void);                       /* hal_init + calc_setup (idempotent) */
void web_frame(uint32_t dt_ms);           /* advance virtual time, run until idle */
int web_key_from_name(const char *name);  /* design.py name or console alias -> key id (0 = unknown) */
int web_key_count(void);                  /* KEY_COUNT: valid ids are 1..count-1 */
const char *web_key_name(int key);        /* canonical name (keys/layout.py), "" if invalid */
void web_key(int key, int down);          /* press (1) / release (0); runs until idle */
const uint16_t *web_framebuffer(void);    /* app framebuffer, RGB565, 320x240 */
const uint16_t *web_panel(void);          /* simulated panel GRAM (what flushes delivered) */
const uint8_t *web_lcd_rgba(void);        /* panel as RGBA8888 (black while off) */
uint32_t web_lcd_serial(void);            /* changes whenever the panel may have changed */
int web_display_on(void);                 /* hal_display_enable state */
int web_backlight(void);                  /* 0..100 */
uint32_t web_millis(void);                /* virtual clock, ms */
int web_is_off(void);                     /* calculator powered off (hibernated) */
const char *web_version(void);
int web_console_input(const char *text);  /* queue console input (processed by web_frame); -1 if the 16 KB queue is full */
const char *web_console_take(void);       /* console output since the last call */
int web_console_run(const char *line);    /* run one line to completion, sim time semantics; 0 ok, -1 queue full/runaway */
int web_expect_failures(void);            /* failed console `expect` commands so far */
void web_touch(int x, int y, int state);  /* display touch, x 0..319, y 0..239; 1 down 2 move 0 up */
void web_touch_id(int id, int x, int y, int state); /* same for finger id 0..4 (multi-touch, pinch) */
int web_touch_state(void);                /* -1 if not touching, else y << 16 | x */
/* files and kept sessions (the page's startup presets and localStorage persistence) */
int web_write_file(const char *path, const uint8_t *data, uint32_t len); /* onto the simulated card: 0 or FS_E* */
const uint8_t *web_session_get(uint32_t *len); /* the session now (saved first) */
uint8_t *web_session_buffer(uint32_t *cap);    /* where a kept session goes before web_session_put */
int web_session_put(uint32_t len);             /* install it and restart the app: 0, <0 not a session */
/* the standalone Sparkle Town page (built with -DWEB_SPARKLE only; web_sparkle.c) */
void web_sparkle_set_scale(int s);              /* the high-resolution display, 2..8 (before the module binds) */
int web_sparkle_scale(void);
void web_sparkle_art(const uint32_t *p, int n); /* hashes of the high-resolution pictures the page has */
void web_sparkle_lock(void);                    /* stay in Sparkle Town (it is the home screen) */
const int32_t *web_sparkle_take(uint32_t *n);   /* the frame's drawing commands (*n words) */
void web_sparkle_redraw(void);                  /* draw everything again */
const uint32_t *web_sparkle_stats(void);        /* commands by kind since the last call; [8] pictures without art, [9..11] sprites */
void web_sparkle_vec(const uint32_t *p, int n);  /* FNV-1a of the sprite ids the page has vector art for */
const char *web_sparkle_sprite_misses(void);     /* ids of sprites without art since the last call, a line each */

#endif
