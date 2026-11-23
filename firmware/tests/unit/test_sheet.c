/*
 * Unit tests of the spreadsheet model (app/sheet_core.c) and the PNG
 * screenshot writer (app/sdbk.c):
 *   - reference parsing and moving (relative / absolute parts, #REF);
 *   - formulas through the CAS: ranges, recalculation along a dependency
 *     chain, cycle detection and error propagation, fill-down, copy;
 *   - uncertainty flowing between cells with its correlation (=A1-A1 is
 *     0±0) and with units;
 *   - links to the statistics lists and the saved blob round trip;
 *   - the PNG file on a RAM card: chunk CRCs, then an independent inflate of
 *     the fixed-Huffman stream (RFC 1951 3.2.6), the Adler-32, the filters
 *     undone, and every pixel compared with the framebuffer.
 * The CAS-dependent parts are skipped with CAS=stub. Built and run by
 * `make test` (tests/unit/test_*.c).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../platform/sim/sim.h"
#include "fat.h"
#include "gfx.h"
#include "hal.h"
#include "rle.h"
#include "sdbk.h"
#include "session.h"
#include "sheet.h"
#include "ui.h"

static int fails, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if (!(c)) {                                                               \
            fails++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

static bool real_cas;

static const char *res(const char *ref)
{
    int c, r;
    sheet_parse_ref(ref, &c, &r);
    const scell_t *x = sheet_get(c, r);
    return x ? x->res : "";
}
static double val(const char *ref)
{
    int c, r;
    sheet_parse_ref(ref, &c, &r);
    const scell_t *x = sheet_get(c, r);
    return x ? x->val : NAN;
}
static void set(const char *ref, const char *text)
{
    int c, r;
    CHECK(sheet_parse_ref(ref, &c, &r) > 0);
    CHECK(sheet_set(c, r, text) == 0);
}
#define RES(ref, want)                                                                         \
    do {                                                                                       \
        checks++;                                                                              \
        if (strcmp(res(ref), want)) {                                                          \
            fails++;                                                                           \
            fprintf(stderr, "%s:%d: %s = '%s', want '%s'\n", __FILE__, __LINE__, ref, res(ref), want); \
        }                                                                                      \
    } while (0)

static void test_refs(void)
{
    int c, r;
    CHECK(sheet_parse_ref("B12", &c, &r) == 3 && c == 1 && r == 11);
    CHECK(sheet_parse_ref("$a$3", &c, &r) == 4 && c == 0 && r == 2);
    CHECK(sheet_parse_ref("A0", &c, &r) == 0);
    CHECK(sheet_parse_ref("A1000", &c, &r) == 0);
    CHECK(sheet_parse_ref("A1(", &c, &r) == 0); /* Y1( is a call, not a cell */
    srect_t rc[8];
    int n = sheet_refs("=sum(A1:B3)+log2(8)+_m2+\"C4\"+$D$5", rc, 8);
    CHECK(n == 2);
    CHECK(rc[0].c0 == 0 && rc[0].r0 == 0 && rc[0].c1 == 1 && rc[0].r1 == 2);
    CHECK(rc[1].c0 == 3 && rc[1].r0 == 4);
    char out[64];
    sheet_shift_refs("=A1*$B$1+A$2+$c3", 1, 2, out, sizeof out);
    CHECK(!strcmp(out, "=B3*$B$1+B$2+$c5"));
    sheet_shift_refs("=A1", -1, 0, out, sizeof out);
    CHECK(!strcmp(out, "=#REF"));
}

static void test_formulas(void)
{
    sheet_clear_all();
    sheet_recalc_all();
    set("A1", "2");
    set("A2", "3");
    set("A3", "\"total\"");
    set("B1", "=sum(A1:A3)");
    RES("B1", "5");
    RES("A3", "total");
    set("C1", "=B1*A$1");
    CHECK(val("C1") == 10);
    set("A1", "4"); /* A1 -> B1 -> C1 */
    CHECK(val("B1") == 7 && val("C1") == 28);
    set("A2", "1/3");
    RES("B1", "13/3");
    CHECK(fabs(val("B1") - 13.0 / 3) < 1e-9);
    /* cycle and propagation; breaking it recovers */
    set("D1", "=D2+1");
    set("D2", "=D3+1");
    set("D3", "=D1+1");
    RES("D1", "ERR:CYCLE");
    RES("D3", "ERR:CYCLE");
    set("E1", "=D1*2");
    RES("E1", "ERR:REF");
    set("D3", "1");
    CHECK(val("D1") == 3 && val("E1") == 6);
    set("F1", "=F1");
    RES("F1", "ERR:CYCLE");
    /* empty references are 0; deleting a cell recalculates its users */
    set("G1", "=Z99+1");
    RES("G1", "1");
    set("D3", "");
    CHECK(sheet_get(3, 2) == NULL);
    CHECK(val("D1") == 2 && val("E1") == 4);
    /* fill down */
    set("H1", "=A1*10");
    CHECK(sheet_fill_down(7, 0, 2) == 0);
    CHECK(!strcmp(sheet_get(7, 2)->text, "=A3*10"));
    RES("H3", "10*\"total\""); /* a label is a string in the CAS */
    CHECK(isnan(val("H3")));
    CHECK(sheet_copy(7, 0, 8, 1) == 0); /* H1 -> I2: =B2*10 */
    CHECK(!strcmp(sheet_get(8, 1)->text, "=B2*10"));
    /* uncertainty between cells: one source, so A-A cancels */
    set("J1", "9.81±0.02");
    set("J2", "=J1-J1");
    set("J3", "=J1+J1");
    RES("J2", "0.0±0");
    RES("J3", "19.62±0.04");
    CHECK(fabs(val("J3") - 19.62) < 1e-9);
    set("K1", "(2.0±0.1)_m/(0.50±0.02)_s");
    RES("K1", "(4.0±0.3)_m/s");
    /* the CAS lost the cell variables (MEM > Clear): sheet_ensure rebuilds */
    cas_reset(g.cas);
    sheet_ensure();
    CHECK(fabs(val("C1") - 4 * (4 + 1.0 / 3)) < 1e-9);
    RES("J2", "0.0±0");
}

static void test_links_persist(void)
{
    sheet_clear_all();
    set("A1", "\"x\"");
    set("A2", "1.5");
    set("A3", "=A2*2");
    set("A5", "7");
    CHECK(sheet_link(0, "L4") == 0);
    slist_t *l = sl_find("L4");
    CHECK(l && l->len == 3 && l->v[0] == 1.5 && l->v[1] == 3 && l->v[2] == 7);
    set("A2", "2");
    CHECK(l->len == 3 && l->v[1] == 4);
    CHECK(sheet_link(1, "1bad") != 0);
    CHECK(sheet_from_list(2, "L4") == 0);
    CHECK(val("C3") == 7);
    /* blob round trip */
    extern const persist_def_t persist_sheet;
    static uint8_t buf[16384];
    pw_t w = {buf, sizeof buf, 0, false};
    persist_sheet.save(&w);
    CHECK(!w.overflow);
    int n = sheet_count();
    persist_sheet.reset();
    CHECK(sheet_count() == 0 && !sheet_link_name(0)[0]);
    pr_t r = {buf, w.len, 0, false};
    persist_sheet.load(&r, persist_sheet.version);
    CHECK(sheet_count() == n);
    CHECK(!strcmp(sheet_link_name(0), "L4"));
    sheet_ensure();
    CHECK(val("A3") == 4);
    /* a truncated blob keeps what it could read and does not crash */
    pr_t t = {buf, w.len / 2, 0, false};
    persist_sheet.load(&t, persist_sheet.version);
    CHECK(sheet_count() <= n);
}

/* ---- PNG check: an independent decoder for what sdbk.c writes ---- */
static const uint8_t *bp;
static size_t bpos, blen; /* bit position */
static unsigned getbit(void)
{
    if (bpos / 8 >= blen)
        return 0;
    unsigned b = (bp[bpos / 8] >> (bpos % 8)) & 1;
    bpos++;
    return b;
}
static unsigned getbits(int n)
{
    unsigned v = 0;
    for (int i = 0; i < n; i++)
        v |= getbit() << i;
    return v;
}
static unsigned gethuff(int n) /* MSB first */
{
    unsigned v = 0;
    for (int i = 0; i < n; i++)
        v = v << 1 | getbit();
    return v;
}
/* Inflate one fixed-Huffman block; returns the output length or -1. */
static long inflate_fixed(const uint8_t *in, size_t n, uint8_t *out, size_t cap)
{
    static const uint16_t lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                       31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t dbase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                       193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    bp = in, bpos = 0, blen = n;
    if (getbits(1) != 1 || getbits(2) != 1)
        return -1;
    size_t o = 0;
    for (;;) {
        unsigned s = gethuff(7);
        if (s <= 23)
            s += 256;
        else {
            s = s << 1 | getbit();
            if (s >= 0x30 && s <= 0xBF)
                s -= 0x30;
            else if (s >= 0xC0 && s <= 0xC7)
                s = s - 0xC0 + 280;
            else {
                s = s << 1 | getbit();
                if (s < 0x190)
                    return -1;
                s = s - 0x190 + 144;
            }
        }
        if (s < 256) {
            if (o >= cap)
                return -1;
            out[o++] = (uint8_t)s;
        } else if (s == 256) {
            return (long)o;
        } else {
            unsigned i = s - 257;
            if (i > 28)
                return -1;
            unsigned len = lbase[i] + getbits(lext[i]);
            unsigned dc = gethuff(5);
            if (dc > 29)
                return -1;
            unsigned dist = dbase[dc] + getbits(dc < 4 ? 0 : (int)(dc / 2 - 1));
            if (dist > o || o + len > cap)
                return -1;
            for (unsigned k = 0; k < len; k++, o++)
                out[o] = out[o - dist];
        }
    }
}
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static void test_png(void)
{
    hal_sd_sim_insert(8);
    CHECK(fs_mkfs() == 0);
    /* something to look at: bands, text, a gradient */
    gfx_clear(C_WHITE);
    gfx_fill_rect(0, 0, LCD_WIDTH, 18, C_STATUS_BG);
    gfx_text(&font_large, 10, 40, "Hello 9.81±0.02", C_BLACK, GFX_TRANSPARENT);
    for (int x = 0; x < LCD_WIDTH; x++)
        gfx_vline(x, 120, 60, RGB565(x * 255 / 319, 255 - x * 255 / 319, 128));
    char msg[64];
    CHECK(sdbk_screenshot(msg, sizeof msg) == 0);
    CHECK(!strcmp(msg, "Saved /CALC/SHOT001.PNG."));
    CHECK(sdbk_screenshot(msg, sizeof msg) == 0 && strstr(msg, "SHOT002"));
    static uint8_t file[300000], z[300000], raw[(LCD_WIDTH * 3 + 1) * LCD_HEIGHT + 16];
    fs_file_t f;
    CHECK(fs_open(&f, "/CALC/SHOT001.PNG", FS_READ) == 0);
    long n = 0, k;
    while ((k = fs_read(&f, file + n, 4096)) > 0)
        n += k;
    fs_close(&f);
    fs_unmount();
    CHECK(n > 100 && n < 60000); /* compressed */
    CHECK(!memcmp(file, "\x89PNG\r\n\x1a\n", 8));
    /* walk the chunks: CRC each, collect IDAT */
    long pos = 8, zn = 0;
    bool iend = false;
    int w = 0, h = 0;
    while (pos + 12 <= n && !iend) {
        uint32_t len = rd32(file + pos);
        const uint8_t *type = file + pos + 4, *data = file + pos + 8;
        CHECK(crc32_update(0, type, len + 4) == rd32(data + len));
        { if (!memcmp(type, "IHDR", 4)) { w = (int)rd32(data); h = (int)rd32(data + 4); CHECK(data[8] == 8 && data[9] == 2); } }
        if (!memcmp(type, "IDAT", 4))
            memcpy(z + zn, data, len), zn += len;
        iend = !memcmp(type, "IEND", 4);
        pos += 12 + len;
    }
    CHECK(iend && pos == n && w == LCD_WIDTH && h == LCD_HEIGHT);
    CHECK(z[0] == 0x78 && ((z[0] << 8) | z[1]) % 31 == 0);
    long rn = inflate_fixed(z + 2, (size_t)zn - 6, raw, sizeof raw);
    CHECK(rn == (LCD_WIDTH * 3 + 1) * LCD_HEIGHT);
    uint32_t a1 = 1, a2 = 0;
    for (long i = 0; i < rn; i++)
        a1 = (a1 + raw[i]) % 65521, a2 = (a2 + a1) % 65521;
    CHECK((a2 << 16 | a1) == rd32(z + zn - 4));
    /* undo the filters and compare with the framebuffer */
    const uint16_t *fb = gfx_framebuffer();
    static uint8_t prev[LCD_WIDTH * 3], cur[LCD_WIDTH * 3];
    memset(prev, 0, sizeof prev);
    int bad = 0;
    for (int y = 0; y < LCD_HEIGHT && rn > 0; y++) {
        const uint8_t *row = raw + (size_t)y * (LCD_WIDTH * 3 + 1);
        for (int i = 0; i < LCD_WIDTH * 3; i++) {
            uint8_t a = row[0] == 1 ? (i >= 3 ? cur[i - 3] : 0) : row[0] == 2 ? prev[i] : 0;
            cur[i] = (uint8_t)(row[1 + i] + a);
        }
        for (int x = 0; x < LCD_WIDTH; x++) {
            uint16_t c = fb[y * LCD_WIDTH + x];
            bad += cur[3 * x] != (uint8_t)(((c >> 11) & 31) << 3 | ((c >> 11) & 31) >> 2);
            bad += cur[3 * x + 1] != (uint8_t)(((c >> 5) & 63) << 2 | ((c >> 5) & 63) >> 4);
        }
        memcpy(prev, cur, sizeof prev);
    }
    CHECK(bad == 0);
    FILE *o = fopen("build/test/sheet_shot.png", "wb"); /* for a look / external check */
    if (o) {
        fwrite(file, 1, (size_t)n, o);
        fclose(o);
    }
}

int main(void)
{
    hal_init();
    real_cas = strcmp(CAS_IMPL, "stub") != 0;
    size_t n = 8u << 20;
    g.cas = cas_init(malloc(n), n);
    cas_get_settings(g.cas, &g.cs);
    g.fix = -1;
    tok_init();
    gfx_init(hal_framebuffer());
    static double spool[200000];
    stat_mem_init(spool, true);
    static uint8_t shpool[1u << 20];
    CHECK(sheet_mem_init(shpool, true) <= sizeof shpool);
    test_refs();
    if (real_cas) {
        test_formulas();
        test_links_persist();
    }
    test_png();
    printf("test_sheet: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
