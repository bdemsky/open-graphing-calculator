/* Host unit tests for core/ and app/ building blocks.
 *
 * Linked against the native simulator library (platform/sim: hal_init()
 * gives a RAM framebuffer and a simulated LCD panel) and the configured CAS,
 * built and run by `make test` (tests/unit/test_*.c, see Makefile). Covers:
 *   - CRC-32 and the PackBits-style RLE used for screen dumps (core/rle.c),
 *   - the generated key map (core/gen/keymap_gen.c from keys/layout.py),
 *   - the token editor behind every entry line (app/editor.c),
 *   - the graphics layer: dirty-rectangle flushing and the text log that
 *     the UI tests' `expect text` reads (core/gfx.c).
 * CHECK / CHECK_STR count failures and keep going; main() returns nonzero if
 * any check failed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "console.h"
#include "editor.h"
#include "gfx.h"
#include "hal.h"
#include "keyq.h"
#include "rle.h"

static int fails, checks;
#define CHECK(c)                                                              \
    do {                                                                      \
        checks++;                                                             \
        if (!(c)) {                                                           \
            fails++;                                                          \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)
#define CHECK_STR(a, b)                                                                    \
    do {                                                                                   \
        checks++;                                                                          \
        if (strcmp((a), (b))) {                                                            \
            fails++;                                                                       \
            fprintf(stderr, "%s:%d: '%s' != '%s'\n", __FILE__, __LINE__, (a), (b));        \
        }                                                                                  \
    } while (0)

/* rle_encode() output sink: appends the emitted chunks to buf. */
typedef struct {
    uint8_t *buf;
    size_t pos;
} sink_t;

static void emit(const uint8_t *d, size_t len, void *u)
{
    sink_t *k = u;
    memcpy(k->buf + k->pos, d, len);
    k->pos += len;
}

/* CRC-32 against the standard check value, then an RLE round trip on 5000
 * pixels mixing runs (every third 37-pixel block is a slowly changing
 * constant) and literals (LCG noise), checking that the size-only pass
 * (emit = NULL) and the emitting pass agree. Finally a uniform full screen
 * must compress well: runs of up to 128 pixels cost 3 bytes, so 76800
 * pixels take 600 runs = 1800 bytes. */
static void test_crc_rle(void)
{
    CHECK(crc32_update(0, "123456789", 9) == 0xCBF43926u); /* CRC-32/IEEE check value */
    static uint16_t px[5000], out[5000];
    uint32_t s = 1;
    for (int i = 0; i < 5000; i++) {
        s = s * 1103515245u + 12345u;
        px[i] = (i / 37) % 3 ? (uint16_t)(s >> 16) : (uint16_t)(i / 300);
    }
    static uint8_t enc[20000];
    size_t n = rle_encode(px, 5000, NULL, NULL);
    CHECK(n < sizeof enc);
    sink_t k = {enc, 0};
    rle_encode(px, 5000, emit, &k);
    CHECK(k.pos == n);
    CHECK(rle_decode(enc, k.pos, out, 5000) == 5000);
    CHECK(!memcmp(px, out, sizeof px));
    static uint16_t flat[LCD_WIDTH * LCD_HEIGHT];
    CHECK(rle_encode(flat, LCD_WIDTH * LCD_HEIGHT, NULL, NULL) < 2000);
}

/* The generated key map: every key except ON sits at exactly one cell of the
 * 7x7 switch matrix and key_matrix[][] maps that cell back to it (ON is
 * wired separately: key_pos 0xFF); console key names, including aliases
 * such as "bksp", "+" and "var", resolve; and a few spot checks of the
 * matrix positions and of what keys type / do from keys/layout.py. */
static void test_keymap(void)
{
    int seen[49] = {0};
    for (int k = 1; k < KEY_COUNT; k++) {
        if (k == KEY_ON) {
            CHECK(key_pos[k] == 0xFF);
            continue;
        }
        int r = key_pos[k] >> 4, c = key_pos[k] & 15;
        CHECK(r < 7 && c < 7);
        CHECK(key_matrix[r][c] == k);
        seen[r * 7 + c]++;
    }
    for (int i = 0; i < 49; i++)
        CHECK(seen[i] == 1);
    CHECK(key_from_name("enter") == KEY_ENTER);
    CHECK(key_from_name("bksp") == KEY_DEL);
    CHECK(key_from_name("+") == KEY_ADD);
    CHECK(key_from_name("(-)") == KEY_NEG);
    CHECK(key_from_name("bogus") == KEY_NONE);
    CHECK(key_from_name("shift") == KEY_SHIFT);
    CHECK(key_from_name("assign") == KEY_ASSIGN);
    CHECK(key_from_name("var") == KEY_X);
    /* design.py LAYOUT row-major: F1 is (0,0), SHIFT (0,5), ENTER (6,6) */
    CHECK(key_pos[KEY_F1] == 0x00 && key_pos[KEY_SHIFT] == 0x05 && key_pos[KEY_ENTER] == 0x66);
    CHECK(KEY_F5 == KEY_F1 + 4 && KEY_IS_SOFT(KEY_F3) && !KEY_IS_SOFT(KEY_SHIFT));
    /* keys/layout.py: what the keys type */
    CHECK(!strcmp(key_info[KEY_SIN].text, "sin(") && !strcmp(key_info[KEY_SIN].shift_text, "sin⁻¹("));
    CHECK(!strcmp(key_info[KEY_HOME].action, "home") && !strcmp(key_info[KEY_MENU].shift_action, "help"));
    CHECK(!strcmp(key_info[KEY_MUL].alpha, "s"));
}

/* The token editor: multi-character tokens (π, sin⁻¹(, √() are single
 * tokens, displayed with their glyph and translated to CAS syntax by
 * ed_cas() (with implicit multiplication made explicit, e.g. 2(pi)x and
 * x*(x+1)); delete / backspace semantics; insertion with a suffix after the
 * cursor (templates such as solve( ... ,x)); pretty_from_cas() mapping CAS
 * names back to glyphs only for whole identifiers ("epic" stays); and
 * overwrite mode (ed_insert_mode = false, restored at the end because it is
 * a global). */
static void test_editor(void)
{
    tok_init();
    editor_t e;
    char b[256];
    ed_clear(&e);
    ed_insert_text(&e, "2", NULL);
    ed_insert(&e, TOK_PI);
    ed_insert_text(&e, "x", NULL);
    ed_display(&e, b, sizeof b);
    CHECK_STR(b, "2πx");
    ed_cas(&e, b, sizeof b);
    CHECK_STR(b, "2(pi)x");
    CHECK(ed_cursor_col(&e) == 3);
    ed_left(&e);
    ed_delete(&e); /* deletes x */
    ed_display(&e, b, sizeof b);
    CHECK_STR(b, "2π");
    ed_insert_text(&e, "x", NULL);
    ed_left(&e);
    ed_backspace(&e); /* ⌫: the token before the cursor (π) */
    ed_display(&e, b, sizeof b);
    CHECK_STR(b, "2x");
    ed_home(&e);
    ed_backspace(&e); /* at the start: the token under the cursor */
    ed_display(&e, b, sizeof b);
    CHECK_STR(b, "x");
    ed_clear(&e);
    ed_insert_text(&e, "solve(", ",x)");
    ed_insert_text(&e, "x^2-2", NULL);
    ed_display(&e, b, sizeof b);
    CHECK_STR(b, "solve(x^2-2,x)");
    ed_set_text(&e, "√(2)+sin⁻¹(1)");
    ed_cas(&e, b, sizeof b);
    CHECK_STR(b, "sqrt(2)+asin(1)");
    CHECK(e.len == 7); /* tokens: √( 2 ) + sin⁻¹( 1 ) */
    ed_set_text(&e, "x(x+1)");
    ed_cas(&e, b, sizeof b);
    CHECK_STR(b, "x*(x+1)");
    /* an ODE's initial conditions: y(0) stays a call when the line has y' */
    ed_set_text(&e, "dsolve([y'=2*y,y(0)=3],y,x)");
    ed_cas(&e, b, sizeof b);
    CHECK_STR(b, "dsolve([y'=2*y,y(0)=3],y,x)");
    ed_set_text(&e, "dsolve([y''+y=0,y(0)=1,y'(0)=0],y,x)");
    ed_cas(&e, b, sizeof b);
    CHECK_STR(b, "dsolve([y''+y=0,y(0)=1,y'(0)=0],y,x)");
    ed_set_text(&e, "y(2)+x'");
    ed_cas(&e, b, sizeof b);
    CHECK_STR(b, "y*(2)+x'");
    pretty_from_cas("sqrt(2)*pi+epic", b, sizeof b);
    CHECK_STR(b, "√(2)*π+epic");
    ed_insert_mode = false;
    ed_set_text(&e, "123");
    ed_home(&e);
    ed_insert_text(&e, "9", NULL);
    ed_display(&e, b, sizeof b);
    CHECK_STR(b, "923");
    ed_insert_mode = true;
}

/* Graphics: 300 random rectangles, lines, texts and pixels (partly off
 * screen, to exercise clipping) with flushes in between; afterwards the
 * simulated panel must equal the framebuffer (`lcdcheck`), i.e. the
 * dirty-rectangle tracking never misses a changed pixel. Then the text log:
 * drawn text is recorded, and a fill that covers part of a run removes it. */
static void test_gfx(void)
{
    gfx_init(hal_framebuffer());
    gfx_clear(C_WHITE);
    gfx_flush();
    uint32_t s = 7;
    for (int i = 0; i < 300; i++) {
        s = s * 1664525u + 1013904223u;
        int x = (int)(s >> 8) % 340 - 10, y = (int)(s >> 16) % 260 - 10;
        switch (i % 4) {
        case 0: gfx_fill_rect(x, y, (int)(s % 50), (int)(s % 30), (uint16_t)s); break;
        case 1: gfx_line(x, y, x + (int)(s % 90) - 45, y + (int)(s % 70) - 35, (uint16_t)(s >> 3)); break;
        case 2: gfx_text(&font_medium, x, y, "Hello π²", (uint16_t)s, i & 8 ? GFX_TRANSPARENT : 0); break;
        case 3: gfx_pixel(x, y, (uint16_t)s); break;
        }
        if (i % 37 == 0)
            gfx_flush();
    }
    gfx_flush();
    CHECK(console_exec("lcdcheck") == 0); /* simulated panel == framebuffer */
    gfx_clear(C_WHITE);
    gfx_text(&font_large, 10, 10, "abc", C_BLACK, GFX_TRANSPARENT);
    gfx_text(&font_large, 10, 40, "xyz", C_BLACK, C_WHITE);
    CHECK(gfx_textlog_contains("abc") && gfx_textlog_contains("xyz"));
    gfx_fill_rect(0, 35, 100, 10, C_WHITE); /* overwrites part of "xyz" */
    CHECK(gfx_textlog_contains("abc") && !gfx_textlog_contains("xyz"));

    /* A full log drops its oldest run (here the bottom one), not the topmost. */
    gfx_clear(C_WHITE);
    gfx_textlog_note(0, 230, 10, 5, "oldest");
    char nm[8];
    for (int k = 0; k < GFX_TEXTLOG_MAX; k++) {
        snprintf(nm, sizeof nm, "r%d", k);
        gfx_textlog_note((k % 16) * 20, (k / 16) * 10, 10, 5, nm);
    }
    CHECK(gfx_textlog_count() == GFX_TEXTLOG_MAX && !gfx_textlog_contains("oldest"));
    CHECK(!strcmp(gfx_textlog_get(0)->text, "r0"));

    /* Lines with far off-screen endpoints are drawn where they cross the screen. */
    uint16_t *fb = gfx_framebuffer();
    gfx_clear(C_WHITE);
    uint16_t white = fb[0];
    gfx_line(-10000, -10000, 10000, 10000, C_BLACK);
    CHECK(fb[100 * LCD_WIDTH + 100] != white && fb[239 * LCD_WIDTH + 239] != white);
    gfx_line(100, 50, 20100, 10050, C_BLACK); /* slope 1/2 from an on-screen start */
    CHECK(fb[50 * LCD_WIDTH + 100] != white && fb[51 * LCD_WIDTH + 102] != white &&
          fb[150 * LCD_WIDTH + 300] != white);

    /* gfx_restore_rect never writes below the base clip (gfx_save_rect does
     * not read there), even with a full-screen current clip. */
    static uint16_t cache[LCD_WIDTH * LCD_HEIGHT];
    for (size_t i = 0; i < sizeof cache / sizeof cache[0]; i++)
        cache[i] = (uint16_t)~white;
    gfx_set_base_clip(200);
    gfx_save_rect(cache, 0, 0, LCD_WIDTH, LCD_HEIGHT);
    gfx_set_clip(0, 0, LCD_WIDTH, LCD_HEIGHT);
    gfx_restore_rect(cache, 0, 0, LCD_WIDTH, LCD_HEIGHT);
    CHECK(fb[220 * LCD_WIDTH + 10] == white);
    gfx_set_base_clip(LCD_HEIGHT);
}

/* Key queue: when it is flooded, presses are dropped together with their
 * releases, and the release of every queued press still gets in. */
static void test_keyq(void)
{
    keyq_clear();
    for (int i = 0; i < 200; i++) /* main loop stuck: far more presses than fit */
        keyq_push(KEY_ENTER, true), keyq_push(KEY_ENTER, false);
    keyq_push(KEY_ADD, true);  /* queue nearly full: dropped */
    keyq_push(KEY_ADD, false); /* its release goes too */
    key_event_t ev;
    int n = 0, last_down = -1;
    bool add = false;
    while (keyq_pop(&ev)) {
        n++;
        last_down = ev.pressed;
        add |= ev.key == KEY_ADD;
    }
    CHECK(n > 0 && n % 2 == 0 && last_down == 0 && !add); /* every press has its release */
    /* releases of queued presses fit even when presses no longer do */
    for (int i = 0; i < 59; i++) /* 118 of the 120 slots open to presses */
        keyq_push(KEY_ENTER, true), keyq_push(KEY_ENTER, false);
    keyq_push(KEY_F1, true);
    keyq_push(KEY_F2, true);
    keyq_push(KEY_F3, true); /* 120 queued: dropped */
    keyq_push(KEY_F1, false);
    keyq_push(KEY_F2, false);
    keyq_push(KEY_F3, false);
    int down = 0, up = 0;
    while (keyq_pop(&ev)) {
        if (ev.key != KEY_ENTER)
            ev.pressed ? down++ : up++;
        CHECK(ev.key != KEY_F3);
    }
    CHECK(down == 2 && up == 2);
}

/* Console: number parsing is decimal or 0x hex (no octal), with range checks;
 * `key` with an unknown name queues nothing; too many arguments are refused. */
static void test_console(void)
{
    long v;
    unsigned long u;
    CHECK(con_parse_int("010", &v) && v == 10);
    CHECK(con_parse_int("08", &v) && v == 8);
    CHECK(con_parse_int("-0x10", &v) && v == -16);
    CHECK(!con_parse_int("99999999999999999999999", &v));
    CHECK(con_parse_u32("0xFFFFFFFF", &u) && u == 0xFFFFFFFFul);
    CHECK(!con_parse_u32("0x100000000", &u) && !con_parse_u32("-1", &u));
    keyq_clear();
    CHECK(console_exec("key ENTER bogus") != 0);
    CHECK(keyq_empty());
    CHECK(console_exec("key 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4") != 0); /* 25 tokens */
    CHECK(keyq_empty());
    CHECK(console_exec("key 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3") == 0);
    keyq_clear();
}

int main(void)
{
    hal_init();
    test_crc_rle();
    test_keymap();
    test_editor();
    test_gfx();
    test_keyq();
    test_console();
    printf("test_core: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
