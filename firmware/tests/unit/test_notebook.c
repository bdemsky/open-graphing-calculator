/*
 * Unit tests of the Notebook's plain-C parts (modules/python/nb.h):
 *   - the JSON writer: Jupyter's layout, escapes, invalid UTF-8 repaired,
 *     characters cut between pieces, numbers, line arrays;
 *   - the streaming parser: what the writer wrote comes back (paths, strings
 *     in pieces, numbers, literals), \u escapes with surrogate pairs, errors
 *     with their line (a missing ':', unterminated string, nesting limit,
 *     text after the end, not an object, a size limit), a reader that fails;
 *   - the PNG writer: chunks and their CRCs, and the zlib stream inflated
 *     here (fixed Huffman codes) back to the filtered rows, with its Adler-32;
 *   - base64 against RFC 4648's vectors;
 *   - the notebook store on a RAM flash: write, list, read back, versions,
 *     the previous one, case-insensitive names, delete, rename (copy), a full
 *     store reclaiming old versions, a damaged sector found by its CRC, a
 *     module extent's sector kept out, wear (the ring), and a power cut at
 *     every device write of a save (the old or the new version, never
 *     anything else, after the next mount).
 * Built and run by `make test`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../modules/python/nb.h"

static int fails, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if (!(c)) {                                                               \
            fails++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

/* ---------------------------------------------------------------- a growing buffer sink */
typedef struct {
    char *b;
    size_t n, cap;
} buf_t;
static void put(void *u, const void *s, size_t n)
{
    buf_t *b = u;
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->b = realloc(b->b, b->cap);
    }
    memcpy(b->b + b->n, s, n);
    b->n += n;
    b->b[b->n] = 0;
}

/* ---------------------------------------------------------------- JSON writer */
static void test_writer(void)
{
    buf_t b = {0};
    jw_t w;
    jw_init(&w, put, &b);
    jw_obj(&w);
    jw_key(&w, "a");
    jw_arr(&w);
    jw_int(&w, 1);
    jw_int(&w, -42);
    jw_num(&w, 2.5);
    jw_null(&w);
    jw_bool(&w, true);
    jw_arr_end(&w);
    jw_key(&w, "empty");
    jw_obj(&w);
    jw_obj_end(&w);
    jw_key(&w, "none");
    jw_arr(&w);
    jw_arr_end(&w);
    jw_key(&w, "s");
    jw_str(&w, "q\"b\\n\n\t\x01 \xC3\xA9");
    jw_obj_end(&w);
    static const char want[] = "{\n \"a\": [\n  1,\n  -42,\n  2.5,\n  null,\n  true\n ],\n \"empty\": {},\n"
                               " \"none\": [],\n \"s\": \"q\\\"b\\\\n\\n\\t\\u0001 \xC3\xA9\"\n}\n";
    CHECK(!strcmp(b.b, want));
    CHECK(w.len == strlen(want));
    if (strcmp(b.b, want))
        fprintf(stderr, "got:\n%s\n", b.b);

    /* invalid UTF-8 becomes U+FFFD; a character cut between pieces is kept whole */
    b.n = 0;
    jw_init(&w, put, &b);
    jw_str_begin(&w);
    jw_str_piece(&w, "a\xFF" "b\xE2\x89", 5); /* ≤ cut after two bytes */
    jw_str_piece(&w, "\xA4" "c\xC3", 3);       /* a lead byte at the end ... */
    jw_str_piece(&w, "z", 1);                  /* ... that is not continued */
    jw_str_end(&w);
    CHECK(!strcmp(b.b, "\"a\xEF\xBF\xBD" "b\xE2\x89\xA4" "c\xEF\xBF\xBDz\""));
    /* overlong and stray continuation bytes */
    b.n = 0;
    jw_init(&w, put, &b);
    jw_str(&w, "\xC0\xAF\x80");
    CHECK(!strcmp(b.b, "\"\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\""));
    /* lines */
    b.n = 0;
    jw_init(&w, put, &b);
    jw_lines(&w, "one\ntwo\n\nthree");
    CHECK(!strcmp(b.b, "[\n \"one\\n\",\n \"two\\n\",\n \"\\n\",\n \"three\"\n]\n"));
    b.n = 0;
    jw_init(&w, put, &b);
    jw_lines(&w, "");
    CHECK(!strcmp(b.b, "[]\n"));
    /* numbers: no NaN in JSON */
    b.n = 0;
    jw_init(&w, put, &b);
    jw_arr(&w);
    jw_num(&w, 0.0 / 0.0);
    jw_num(&w, -10);
    jw_num(&w, 0.1);
    jw_arr_end(&w);
    CHECK(strstr(b.b, "null") && strstr(b.b, "-10") && strstr(b.b, "0.1"));
    free(b.b);
}

/* ---------------------------------------------------------------- JSON parser */
typedef struct {
    const char *s;
    size_t n, pos, chunk;
    int fail_at; /* the read that fails (-1 none) */
    int reads;
} src_t;
static int src_read(void *u, uint8_t *buf, size_t n)
{
    src_t *s = u;
    if (s->fail_at >= 0 && s->reads++ == s->fail_at)
        return -1;
    size_t k = s->n - s->pos < n ? s->n - s->pos : n;
    if (s->chunk && k > s->chunk)
        k = s->chunk;
    memcpy(buf, s->s + s->pos, k);
    s->pos += k;
    return (int)k;
}

/* the events as text: one line each, "path EV value" */
typedef struct {
    buf_t out;
    char str[4096];
    size_t sn;
    int pieces;
} rec_t;
static void rec(jp_t *p, int ev, const char *s, size_t n)
{
    rec_t *r = p->u;
    static const char *const names[] = {"{", "}", "[", "]", "key", "str", "piece", "strend", "num", "true", "false",
                                        "null"};
    char line[600], path[300] = "";
    size_t k = 0;
    for (int d = 0; d < p->depth && k < sizeof path - 40; d++)
        k += (size_t)snprintf(path + k, sizeof path - k, "/%s", p->key[d][0] ? p->key[d] : "#");
    if (ev == JE_STR) {
        r->sn = 0;
        return;
    }
    if (ev == JE_STR_PIECE) {
        memcpy(r->str + r->sn, s, n);
        r->sn += n;
        r->pieces++;
        return;
    }
    if (ev == JE_STR_END) {
        r->str[r->sn] = 0;
        snprintf(line, sizeof line, "%s str %s\n", path, r->str);
    } else {
        snprintf(line, sizeof line, "%s %s %.*s\n", path, names[ev], (int)n, s ? s : "");
    }
    put(&r->out, line, strlen(line));
}

static int parse(const char *text, size_t chunk, rec_t *r, char *err, uint32_t max)
{
    static jp_t p;
    memset(&p, 0, sizeof p);
    memset(r, 0, sizeof *r);
    src_t s = {text, strlen(text), 0, chunk, -1, 0};
    p.read = src_read, p.ru = &s, p.ev = rec, p.u = r, p.max = max;
    int e = jp_parse(&p);
    if (err)
        strcpy(err, p.err);
    return e;
}

static void test_parser(void)
{
    rec_t r;
    char err[100];
    CHECK(parse("{\"a\": [1, -2.5e3, true, false, null, \"x\"], \"b\": {\"c\": \"d\"}, \"e\": {}, \"f\": []}", 0, &r, err,
                0) == 0);
    static const char want[] = " { \n/a key a\n/a [ \n/a/# num 1\n/a/# num -2.5e3\n/a/# true true\n/a/# false false\n"
                               "/a/# null null\n/a/# str x\n/a ] \n/b key b\n/b { \n/b/c key c\n/b/c str d\n/b } \n"
                               "/e key e\n/e { \n/e } \n/f key f\n/f [ \n/f ] \n } \n";
    CHECK(r.out.b && !strcmp(r.out.b, want));
    if (r.out.b && strcmp(r.out.b, want))
        fprintf(stderr, "events:\n%s", r.out.b);
    free(r.out.b);
    /* escapes: \uXXXX, a surrogate pair, a lone surrogate (U+FFFD), and long strings in pieces */
    CHECK(parse("{\"k\": \"\\u00e9\\ud83d\\ude00\\udc00 \\\"\\\\\\/\\n\"}", 3, &r, err, 0) == 0);
    CHECK(r.out.b && strstr(r.out.b, "/k str \xC3\xA9\xF0\x9F\x98\x80\xEF\xBF\xBD \"\\/\n"));
    free(r.out.b);
    static char big[5000];
    memset(big, 0, sizeof big);
    strcpy(big, "{\"long\": \"");
    memset(big + 10, 'y', 3000);
    strcpy(big + 3010, "\"}");
    CHECK(parse(big, 7, &r, err, 0) == 0 && r.pieces >= 11 && r.sn == 3000);
    free(r.out.b);
    /* what the writer writes, read back */
    buf_t b = {0};
    jw_t w;
    jw_init(&w, put, &b);
    jw_obj(&w);
    jw_key(&w, "cells");
    jw_arr(&w);
    jw_obj(&w);
    jw_key(&w, "source");
    jw_lines(&w, "print('hi')\nx = \"\xE2\x88\x9A\"\n");
    jw_obj_end(&w);
    jw_arr_end(&w);
    jw_obj_end(&w);
    CHECK(parse(b.b, 0, &r, err, 0) == 0);
    CHECK(r.out.b && strstr(r.out.b, "/cells/#/source/# str print('hi')\n") &&
          strstr(r.out.b, "/cells/#/source/# str x = \"\xE2\x88\x9A\"\n"));
    free(r.out.b);
    free(b.b);
    /* errors, with the line */
    CHECK(parse("{\n\"a\" 1}", 0, &r, err, 0) < 0 && strstr(err, "line 2") && strstr(err, "':'"));
    free(r.out.b);
    CHECK(parse("{\"a\": \"open", 0, &r, err, 0) < 0 && strstr(err, "unterminated"));
    free(r.out.b);
    CHECK(parse("[1, 2]", 0, &r, err, 0) < 0 && strstr(err, "not a JSON object"));
    free(r.out.b);
    CHECK(parse("{} x", 0, &r, err, 0) < 0 && strstr(err, "after the end"));
    free(r.out.b);
    CHECK(parse("{\"a\": tru}", 0, &r, err, 0) < 0 && strstr(err, "literal"));
    free(r.out.b);
    CHECK(parse("{\"a\": [1 2]}", 0, &r, err, 0) < 0 && strstr(err, "expected ','"));
    free(r.out.b);
    CHECK(parse("{\"a\": \"tab\there\"}", 0, &r, err, 0) < 0 && strstr(err, "control character"));
    free(r.out.b);
    CHECK(parse("", 0, &r, err, 0) < 0);
    free(r.out.b);
    char deep[200] = "{\"a\": ";
    for (int i = 0; i < 40; i++)
        strcat(deep, "[");
    CHECK(parse(deep, 0, &r, err, 0) < 0 && strstr(err, "nested"));
    free(r.out.b);
    CHECK(parse("{\"a\": \"0123456789\"}", 0, &r, err, 10) < 0 && strstr(err, "too large"));
    free(r.out.b);
    /* a reader that fails */
    static jp_t p;
    memset(&p, 0, sizeof p);
    memset(&r, 0, sizeof r);
    src_t s = {"{\"a\": [1, 2, 3]}", 16, 0, 4, 2, 0};
    p.read = src_read, p.ru = &s, p.ev = rec, p.u = &r;
    CHECK(jp_parse(&p) < 0 && strstr(p.err, "could not be read"));
    free(r.out.b);
    /* jp_path */
    memset(&p, 0, sizeof p);
    p.depth = 3;
    strcpy(p.key[0], "cells"), strcpy(p.key[1], ""), strcpy(p.key[2], "source");
    static const char *const k1[] = {"cells", "*", "source"}, *const k2[] = {"cells", "*", "outputs"};
    CHECK(jp_path(&p, 3, k1) && !jp_path(&p, 3, k2) && !jp_path(&p, 2, k1));
    /* nb_strtod */
    const char *e;
    CHECK(nb_strtod("-1.5e2", &e) == -150 && !*e);
    CHECK(nb_strtod(" 7", &e) == 7 && !*e);
    CHECK(nb_strtod("x", &e) == 0 && *e == 'x');
    CHECK(nb_strtod("0.25 ", &e) == 0.25 && *e == ' ');
}

/* ---------------------------------------------------------------- PNG */
/* a minimal inflater for one block of fixed Huffman codes (what png_indexed writes) */
typedef struct {
    const uint8_t *p;
    size_t n, pos;
    uint32_t bit;
} bits_t;
static int getbit(bits_t *b)
{
    if (b->pos >= b->n)
        return -1;
    int v = (b->p[b->pos] >> b->bit) & 1;
    if (++b->bit == 8)
        b->bit = 0, b->pos++;
    return v;
}
static uint32_t getbits(bits_t *b, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++)
        v |= (uint32_t)getbit(b) << i;
    return v;
}
static int fixed_sym(bits_t *b)
{
    int code = 0;
    for (int len = 1; len <= 9; len++) {
        code = code << 1 | getbit(b);
        if (len == 7 && code <= 0x17)
            return 256 + code;
        if (len == 8 && code >= 0x30 && code <= 0xBF)
            return code - 0x30;
        if (len == 8 && code >= 0xC0 && code <= 0xC7)
            return 280 + code - 0xC0;
        if (len == 9 && code >= 0x190)
            return 144 + code - 0x190;
    }
    return -1;
}
static long inflate_fixed(const uint8_t *z, size_t n, uint8_t *out, size_t max, uint32_t *adler)
{
    static const uint16_t lb[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99,
                                    115, 131, 163, 195, 227, 258};
    static const uint8_t le[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t db[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025,
                                    1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t de[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12,
                                   12, 13, 13};
    if (n < 6 || z[0] != 0x78 || (z[0] * 256 + z[1]) % 31)
        return -1;
    bits_t b = {z + 2, n - 2, 0, 0};
    if (getbits(&b, 1) != 1 || getbits(&b, 2) != 1) /* final, fixed */
        return -2;
    size_t o = 0;
    for (;;) {
        int s = fixed_sym(&b);
        if (s < 0)
            return -3;
        if (s < 256) {
            if (o >= max)
                return -4;
            out[o++] = (uint8_t)s;
            continue;
        }
        if (s == 256)
            break;
        int len = lb[s - 257] + (int)getbits(&b, le[s - 257]);
        int dc = 0;
        for (int i = 0; i < 5; i++)
            dc = dc << 1 | getbit(&b);
        size_t dist = db[dc] + getbits(&b, de[dc]);
        if (dist > o || o + (size_t)len > max)
            return -5;
        for (int i = 0; i < len; i++, o++)
            out[o] = out[o - dist];
    }
    if (b.bit)
        b.pos++, b.bit = 0;
    const uint8_t *a = b.p + b.pos;
    *adler = (uint32_t)a[0] << 24 | (uint32_t)a[1] << 16 | (uint32_t)a[2] << 8 | a[3];
    return (long)o;
}
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void test_png(void)
{
    enum { W = 61, H = 37 };
    static uint8_t pix[W * H];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            pix[y * W + x] = (uint8_t)(x == y || y == 18 ? 3 : (x * 7 + y * 13) % 11 == 0 ? 2 : 0);
    static const uint16_t pal[4] = {0xFFFF, 0xE73C, 0x8410, 0x22BB};
    buf_t b = {0};
    png_indexed(pix, W, H, pal, 4, put, &b);
    const uint8_t *p = (const uint8_t *)b.b;
    CHECK(b.n > 8 && !memcmp(p, "\x89PNG\r\n\x1A\n", 8));
    /* chunks: IHDR, PLTE, IDAT..., IEND, each CRC right; the zlib stream gathered */
    static uint8_t z[1 << 16];
    size_t zn = 0, at = 8;
    int nchunks = 0;
    bool ihdr = false, plte = false, iend = false;
    while (at + 12 <= b.n) {
        uint32_t len = be32(p + at);
        const uint8_t *type = p + at + 4, *data = p + at + 8;
        CHECK(at + 12 + len <= b.n);
        CHECK(crc32_update(crc32_update(0, type, 4), data, len) == be32(data + len));
        if (!memcmp(type, "IHDR", 4))
            ihdr = be32(data) == W && be32(data + 4) == H && data[8] == 8 && data[9] == 3;
        if (!memcmp(type, "PLTE", 4))
            plte = len == 12 && data[9] == 0x21 && data[10] == 0x55 && data[11] == 0xDE; /* 0x22BB widened */
        if (!memcmp(type, "IDAT", 4)) {
            memcpy(z + zn, data, len);
            zn += len;
        }
        iend = !memcmp(type, "IEND", 4);
        at += 12 + len;
        nchunks++;
    }
    CHECK(ihdr && plte && iend && at == b.n && nchunks >= 4);
    static uint8_t raw[(W + 1) * H + 16];
    uint32_t adler;
    long n = inflate_fixed(z, zn, raw, sizeof raw, &adler);
    CHECK(n == (W + 1) * H);
    bool same = n == (W + 1) * H;
    uint32_t a1 = 1, a2 = 0;
    for (int y = 0; y < H && same; y++) {
        same = raw[y * (W + 1)] == 0 && !memcmp(raw + y * (W + 1) + 1, pix + y * W, W);
    }
    for (long i = 0; i < n; i++)
        a1 = (a1 + raw[i]) % 65521, a2 = (a2 + a1) % 65521;
    CHECK(same);
    CHECK(adler == (a2 << 16 | a1));
    /* a plain image compresses well (runs and the row above) */
    static uint8_t flat[288 * 132];
    memset(flat, 0, sizeof flat);
    for (int x = 0; x < 288; x++)
        flat[66 * 288 + x] = 2;
    b.n = 0;
    png_indexed(flat, 288, 132, pal, 4, put, &b);
    CHECK(b.n < 1500);
    free(b.b);
}

static void test_base64(void)
{
    static const char *const in[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    static const char *const out[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    for (int i = 0; i < 7; i++) {
        buf_t b = {0};
        put(&b, "", 0);
        b64_t e;
        b64_init(&e, put, &b);
        for (const char *s = in[i]; *s; s++) /* a byte at a time */
            b64_put(&e, s, 1);
        b64_end(&e);
        CHECK(!strcmp(b.b, out[i]));
        free(b.b);
    }
}

/* ---------------------------------------------------------------- the store */
#define FLASH (64 * NBS_SECTOR)
static uint8_t flash[FLASH];
static long erase_count[64];
static int f_read(void *u, uint32_t off, void *buf, size_t n)
{
    (void)u;
    if (off + n > FLASH)
        return -1;
    memcpy(buf, flash + off, n);
    return 0;
}
static int f_erase(void *u, uint32_t off, size_t n)
{
    (void)u;
    if (off % NBS_SECTOR || off + n > FLASH)
        return -1;
    memset(flash + off, 0xFF, n);
    erase_count[off / NBS_SECTOR]++;
    return 0;
}
static int f_write(void *u, uint32_t off, const void *buf, size_t n)
{
    (void)u;
    if (off + n > FLASH)
        return -1;
    const uint8_t *s = buf;
    for (size_t i = 0; i < n; i++)
        flash[off + i] &= s[i]; /* NOR: programming only clears bits */
    return 0;
}
static const nbs_dev_t dev = {f_read, f_erase, f_write, NULL, FLASH};
static nbs_t st;
static nbs_wr_t wr;

static int save(const char *name, const char *data, size_t n)
{
    int e = nbs_create(&st, name, (uint32_t)n, &wr);
    if (e) {
        nbs_close(&wr);
        return e;
    }
    nbs_put(&wr, data, n);
    return nbs_close(&wr);
}
static int load(const char *name, int which, char *out, size_t max)
{
    nbs_rd_t r;
    int e = nbs_open(&st, name, which, &r);
    if (e)
        return e;
    size_t got = 0;
    for (int k; (k = nbs_read(&r, (uint8_t *)out + got, max - got)) > 0;)
        got += (size_t)k;
    return r.err ? r.err : (int)got;
}
static char *make(size_t n, int seed)
{
    char *p = malloc(n + 1);
    for (size_t i = 0; i < n; i++)
        p[i] = (char)('a' + (i * 7 + (size_t)seed + i / 4032) % 26);
    p[n] = 0;
    return p;
}

static void test_store(void)
{
    memset(flash, 0xFF, sizeof flash);
    CHECK(nbs_mount(&st, &dev) == 0 && st.mounted && st.nsec == 64);
    nbs_entry_t e[8];
    CHECK(nbs_list(&st, e, 8) == 0);
    CHECK(nbs_mount(&st, NULL) == NBS_E_NODEV);
    CHECK(nbs_mount(&st, &dev) == 0);
    /* names */
    CHECK(nbs_name_ok("Plan for May") && nbs_name_ok("a") && !nbs_name_ok("") && !nbs_name_ok(".x") &&
          !nbs_name_ok("a/b") && !nbs_name_ok("trailing ") && !nbs_name_ok("0123456789012345678901234567890123456"));
    CHECK(save("bad/name", "x", 1) == NBS_E_NAME);
    /* write, list, read back (3 sectors) */
    char *a = make(10000, 1), *b2 = make(5000, 2), *out = malloc(300000);
    CHECK(save("Algebra", a, 10000) == 0);
    CHECK(save("Notes", "{}", 2) == 0);
    CHECK(nbs_list(&st, e, 8) == 2 && !strcmp(e[0].name, "Algebra") && e[0].size == 10000 && e[0].versions == 1 &&
          !strcmp(e[1].name, "Notes"));
    CHECK(load("algebra", 0, out, 300000) == 10000 && !memcmp(out, a, 10000)); /* any case */
    /* a new version; the previous one stays */
    CHECK(save("ALGEBRA", b2, 5000) == 0);
    CHECK(nbs_list(&st, e, 8) == 2 && e[0].versions == 2 && e[0].size == 5000);
    CHECK(load("Algebra", 0, out, 300000) == 5000 && !memcmp(out, b2, 5000));
    CHECK(load("Algebra", 1, out, 300000) == 10000 && !memcmp(out, a, 10000));
    CHECK(load("Algebra", 2, out, 300000) == NBS_E_NOTFOUND);
    /* the same after a mount */
    CHECK(nbs_mount(&st, &dev) == 0);
    CHECK(nbs_list(&st, e, 8) == 2 && e[0].versions == 2);
    CHECK(load("Algebra", 0, out, 300000) == 5000 && !memcmp(out, b2, 5000));
    /* revert: the previous version copied as the newest */
    CHECK(nbs_copy(&st, "Algebra", 1, "Algebra", &wr) == 0);
    CHECK(load("Algebra", 0, out, 300000) == 10000 && !memcmp(out, a, 10000));
    /* rename: copy, then delete */
    CHECK(nbs_copy(&st, "Notes", 0, "Diary", &wr) == 0 && nbs_delete(&st, "Notes") == 0);
    CHECK(nbs_mount(&st, &dev) == 0);
    CHECK(nbs_list(&st, e, 8) == 2 && !strcmp(e[1].name, "Diary") && load("Notes", 0, out, 10) == NBS_E_NOTFOUND);
    CHECK(nbs_delete(&st, "nothing") == NBS_E_NOTFOUND);
    /* sectors for the USB drive */
    uint32_t off[80], size;
    CHECK(nbs_sectors(&st, "Algebra", off, 80, &size) == 3 && size == 10000);
    CHECK(!memcmp(flash + off[1] + NBS_HDR, a + NBS_DATA, 100));
    /* empty notebook, exactly one sector, two sectors */
    CHECK(save("Empty", "", 0) == 0 && load("Empty", 0, out, 10) == 0);
    char *one = make(NBS_DATA, 5);
    CHECK(save("One", one, NBS_DATA) == 0 && load("One", 0, out, 300000) == (int)NBS_DATA);
    CHECK(save("Two", one, NBS_DATA - 1) == 0 && nbs_sectors(&st, "Two", off, 80, &size) == 1);
    /* too large; announced size not met */
    CHECK(nbs_create(&st, "Big", NBS_MAX_BYTES + 1, &wr) == NBS_E_SIZE);
    nbs_close(&wr);
    CHECK(nbs_create(&st, "Short", 100, &wr) == 0);
    nbs_put(&wr, "abc", 3);
    CHECK(nbs_close(&wr) == NBS_E_SIZE && load("Short", 0, out, 10) == NBS_E_NOTFOUND);
    /* full: old versions are reclaimed, the current ones never */
    memset(flash, 0xFF, sizeof flash);
    CHECK(nbs_mount(&st, &dev) == 0);
    char *big = make(40 * NBS_DATA, 7);
    CHECK(save("Big", big, 30 * NBS_DATA) == 0);  /* 30 of 64 sectors */
    CHECK(save("Big", big, 30 * NBS_DATA) == 0);  /* 60: two versions */
    CHECK(save("Big", big, 30 * NBS_DATA) == 0);  /* the oldest reclaimed */
    CHECK(nbs_list(&st, e, 8) == 1 && e[0].versions == 2);
    CHECK(save("Other", big, 40 * NBS_DATA) == NBS_E_FULL); /* 34 free + 30 old, but "Big" stays */
    CHECK(load("Big", 0, out, 300000) == 30 * NBS_DATA && !memcmp(out, big, 30 * NBS_DATA));
    CHECK(save("Other", big, 34 * NBS_DATA) == 0); /* the old version of Big goes */
    CHECK(nbs_list(&st, e, 8) == 2 && e[0].versions == 1);
    CHECK(nbs_free(&st) == 0);
    /* a damaged sector: its CRC */
    CHECK(nbs_sectors(&st, "Other", off, 80, &size) == 34);
    flash[off[5] + NBS_HDR + 7] ^= 0x10;
    CHECK(nbs_mount(&st, &dev) == 0);
    CHECK(load("Other", 0, out, 300000) == NBS_E_CRC);
    /* a module extent's header ("CMOD") in the region: never used */
    memset(flash, 0xFF, sizeof flash);
    memcpy(flash + 3 * NBS_SECTOR, "CMOD", 4);
    CHECK(nbs_mount(&st, &dev) == 0 && st.owner[3] == -2);
    for (int i = 0; i < 20; i++)
        CHECK(save("Fill", big, 20 * NBS_DATA) == 0);
    CHECK(!memcmp(flash + 3 * NBS_SECTOR, "CMOD", 4));
    /* wear: saving one small notebook again and again erases every sector in turn */
    memset(flash, 0xFF, sizeof flash);
    memset(erase_count, 0, sizeof erase_count);
    CHECK(nbs_mount(&st, &dev) == 0);
    for (int i = 0; i < 640; i++)
        save("Small", a, 5000);
    long lo = erase_count[0], hi = erase_count[0];
    for (int i = 0; i < 64; i++)
        lo = erase_count[i] < lo ? erase_count[i] : lo, hi = erase_count[i] > hi ? erase_count[i] : hi;
    CHECK(lo >= 19 && hi <= 21);
    free(a), free(b2), free(one), free(big), free(out);
}

/* A power cut at every device write and erase of a save: after the next mount the notebook is the
 * old version or the new one, complete and undamaged. */
static void test_power_cuts(void)
{
    static uint8_t before[FLASH];
    char *v1 = make(9000, 11), *v2 = make(12000, 12), *out = malloc(20000);
    int cuts = 0, news = 0;
    for (long k = 0;; k++) {
        memset(flash, 0xFF, sizeof flash);
        CHECK(nbs_mount(&st, &dev) == 0);
        CHECK(save("Doc", v1, 9000) == 0);
        CHECK(save("Pad", "x", 1) == 0);
        memcpy(before, flash, sizeof flash);
        st.fail_after = k; /* k device operations succeed, then all fail (the power is gone) */
        int e = save("Doc", v2, 12000);
        st.fail_after = -1;
        CHECK(nbs_mount(&st, &dev) == 0);
        int n = load("Doc", 0, out, 20000);
        bool old = n == 9000 && !memcmp(out, v1, 9000), neu = n == 12000 && !memcmp(out, v2, 12000);
        CHECK(old || neu);
        CHECK(load("Pad", 0, out, 10) == 1);
        if (e == 0) {
            CHECK(neu);
            break;
        }
        cuts++, news += neu;
        if (k > 200)
            break;
    }
    CHECK(cuts >= 6 && news == 0); /* (the new version counts only once its last header is written) */
    free(v1), free(v2), free(out);
}

int main(void)
{
    test_writer();
    test_parser();
    test_png();
    test_base64();
    test_store();
    test_power_cuts();
    printf("test_notebook: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
