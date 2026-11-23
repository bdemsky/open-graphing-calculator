/*
 * Unit tests of the virtual USB drive (core/vdrive.c) with the simulators'
 * model of a computer (platform/hostdev/usbhost.c): the generated volume is a
 * valid FAT16 file system whose files read back as generated; files written
 * the way macOS writes them (AppleDouble "._" file, .fseventsd, the entry
 * first and its size last) and the way Windows does (System Volume
 * Information, data first, the entry last), contiguous or fragmented, are
 * found complete only once everything is there and the writes have been
 * quiet, and read back byte for byte along their chains; metadata and
 * directories are never candidates; a file is handed out once per
 * generation; a rebuild forgets everything the host wrote; module files are
 * recognised by name. Built and run by `make test`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../platform/hostdev/usbhost.h"
#include "vdrive.h"

static int fails, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if (!(c)) {                                                               \
            fails++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

#define STAGE (256u << 10) /* a smaller staging buffer than the calculator's 2 MB */
static vdrive_t vd;
static uint8_t *mem, *stage;
static uint32_t now = 1000; /* the clock vd_write and vd_poll see */

/* the computer's view: every sector through vd_read / vd_write */
static int dev_read(void *u, uint32_t lba, uint8_t *buf)
{
    (void)u;
    if (lba >= vd_sectors(&vd))
        return -1;
    vd_read(&vd, lba, buf);
    return 0;
}
static int dev_write(void *u, uint32_t lba, const uint8_t *buf)
{
    (void)u;
    if (lba >= vd_sectors(&vd))
        return -1;
    vd_write(&vd, lba, buf, now);
    now += 1; /* a millisecond per sector */
    return 0;
}
static const uh_dev_t dev = {dev_read, dev_write, NULL};

/* generated files */
static const char hello[] = "Drop .MOD files here to install them.\r\n";
static void rd_text(void *user, uint32_t off, uint8_t *buf, uint32_t n) { memcpy(buf, (const char *)user + off, n); }
static void rd_pattern(void *user, uint32_t off, uint8_t *buf, uint32_t n)
{
    (void)user;
    for (uint32_t i = 0; i < n; i++)
        buf[i] = (uint8_t)((off + i) * 7 + 3);
}

static void build(void)
{
    vd_begin(&vd, "CALC", 0x1234ABCDu, (uint16_t)(46u << 9 | 1u << 5 | 1u), 0);
    CHECK(vd_add_file(&vd, "README.TXT", VD_ATTR_RO, sizeof hello - 1, rd_text, (void *)hello) == 0);
    CHECK(vd_add_file(&vd, "SCREEN.BMP", VD_ATTR_RO, 230454, rd_pattern, NULL) == 0);
    CHECK(vd_add_file(&vd, "EMPTY.TXT", VD_ATTR_RO, 0, rd_text, (void *)"") == 0);
    vd_build(&vd);
}

/* a module-like file of n bytes */
static uint8_t *make(uint32_t n, uint8_t seed)
{
    uint8_t *p = malloc(n);
    for (uint32_t i = 0; i < n; i++)
        p[i] = (uint8_t)(i * 13 + seed + (i >> 9));
    return p;
}

static void test_volume(void)
{
    build();
    uint8_t b[512];
    vd_read(&vd, 0, b);
    CHECK(b[510] == 0x55 && b[511] == 0xAA);
    CHECK(b[11] == 0 && b[12] == 2 && b[13] == 1 && b[16] == 2); /* 512 B sectors, 1 per cluster, 2 FATs */
    CHECK(!memcmp(b + 54, "FAT16   ", 8) && !memcmp(b + 43, "CALC       ", 11));
    /* FAT16 needs at least 4085 clusters: the small staging is padded with bad clusters */
    CHECK(vd.nclusters >= 4085 && vd.nclusters < 65525);
    uint32_t total = (uint32_t)b[19] | (uint32_t)b[20] << 8;
    CHECK(total == vd_sectors(&vd));
    CHECK(vd_free_bytes(&vd) == STAGE && vd_writable(&vd));
    /* the computer reads the generated files back */
    uint8_t out[1024];
    int n = uh_read_file(&dev, "readme.txt", out, sizeof out);
    CHECK(n == (int)sizeof hello - 1 && !memcmp(out, hello, sizeof hello - 1));
    uint8_t *bmp = malloc(230454);
    n = uh_read_file(&dev, "SCREEN.BMP", bmp, 230454);
    bool same = n == 230454;
    for (int i = 0; same && i < n; i++)
        same = bmp[i] == (uint8_t)(i * 7 + 3);
    CHECK(same);
    free(bmp);
    CHECK(uh_read_file(&dev, "EMPTY.TXT", out, sizeof out) == 0);
    CHECK(uh_read_file(&dev, "NOPE.TXT", out, sizeof out) == -2);
    /* writes to the generated files' clusters are dropped (read-only) */
    uint32_t lba = vd.data_start + vd.files[0].first - 2; /* (generated files come after the staging region) */
    memset(b, 'X', sizeof b);
    vd_write(&vd, lba, b, now);
    n = uh_read_file(&dev, "README.TXT", out, sizeof out);
    CHECK(n == (int)sizeof hello - 1 && !memcmp(out, hello, sizeof hello - 1));
}

/* drop a file, wait until quiet, poll: the found file must equal data */
static int drop_and_find(const char *name, const uint8_t *data, uint32_t n, int style, vd_found_t *f)
{
    build();
    if (uh_write_file(&dev, name, data, n, style))
        return -1;
    /* not before the writes went quiet */
    if (vd_poll(&vd, now + VD_QUIET_MS / 2, f))
        return -2;
    now += VD_QUIET_MS + 1;
    if (!vd_poll(&vd, now, f))
        return -3;
    uint8_t *back = malloc(n + 1);
    int k = vd_file_read(&vd, f, 0, back, n);
    int r = k == (int)n && !memcmp(back, data, n) ? 0 : -4;
    /* reads in odd pieces along the chain give the same bytes */
    for (uint32_t off = 0; !r && off < n; off += 333) {
        uint32_t m = n - off < 333 ? n - off : 333;
        if (vd_file_read(&vd, f, off, back, m) != (int)m || memcmp(back, data + off, m))
            r = -5;
    }
    free(back);
    return r;
}

static void test_mac_win(void)
{
    vd_found_t f;
    uint32_t n = 36628; /* a module file's size */
    uint8_t *d = make(n, 1);
    CHECK(drop_and_find("GEOME010.MOD", d, n, UH_MAC, &f) == 0);
    CHECK(f.module && !strcmp(f.name, "GEOME010.MOD") && f.size == n);
    /* the AppleDouble file and .fseventsd are no candidates: nothing else is found */
    vd_done(&vd, &f);
    vd_found_t g;
    CHECK(vd_poll(&vd, now + 2000, &g) == 0);

    CHECK(drop_and_find("GEOME010.MOD", d, n, UH_WIN, &f) == 0);
    CHECK(f.module && f.size == n);
    vd_done(&vd, &f);
    CHECK(vd_poll(&vd, now + 2000, &g) == 0);

    /* fragmented: every other free cluster */
    CHECK(drop_and_find("CASX010.MOD", d, n, UH_WIN | UH_FRAG, &f) == 0);
    CHECK(drop_and_find("CASX010.MOD", d, n, UH_MAC | UH_FRAG, &f) == 0);

    /* a long name in lower case, with spaces: matched by its long name */
    CHECK(drop_and_find("my geometry v2.mod", d, n, UH_MAC, &f) == 0);
    CHECK(f.module && !strcmp(f.name, "my geometry v2.mod"));
    /* other extensions are found but not modules */
    CHECK(drop_and_find("notes.txt", (const uint8_t *)"hi\n", 3, UH_WIN, &f) == 0);
    CHECK(!f.module);
    free(d);
}

static void test_incomplete(void)
{
    vd_found_t f;
    uint32_t n = 20000;
    uint8_t *d = make(n, 9);
    build();
    /* write the file, then undo the last FAT link: the chain is incomplete */
    CHECK(uh_write_file(&dev, "PART.MOD", d, n, UH_WIN) == 0);
    now += 2000;
    CHECK(vd_poll(&vd, now, &f) == 1); /* complete */
    uint32_t c = f.first, need = (n + 511) / 512;
    for (uint32_t k = 1; k + 1 < need; k++)
        c = (uint32_t)vd.fat[2 * c] | (uint32_t)vd.fat[2 * c + 1] << 8;
    uint8_t save0 = vd.fat[2 * c], save1 = vd.fat[2 * c + 1];
    vd.fat[2 * c] = 0, vd.fat[2 * c + 1] = 0; /* the last cluster not linked yet */
    vd.dirty = true;
    CHECK(vd_poll(&vd, now, &f) == 0);
    vd.fat[2 * c] = save0, vd.fat[2 * c + 1] = save1;
    vd.dirty = true;
    CHECK(vd_poll(&vd, now, &f) == 1);
    /* a data cluster not written (the host's write still in its cache) */
    uint32_t i = f.first - vd.free_first; /* (the staging region starts at free_first) */
    vd.written[i / 8] &= (uint8_t)~(1u << (i % 8));
    vd.dirty = true;
    CHECK(vd_poll(&vd, now, &f) == 0);
    vd.written[i / 8] |= (uint8_t)(1u << (i % 8));
    vd.dirty = true;
    CHECK(vd_poll(&vd, now, &f) == 1);
    /* handed back: not found again; a rebuild forgets the host's writes */
    vd_done(&vd, &f);
    CHECK(vd_poll(&vd, now, &f) == 0);
    build();
    uint8_t out[64];
    CHECK(uh_read_file(&dev, "PART.MOD", out, sizeof out) == -2);
    CHECK(vd.writes == 0 && vd_poll(&vd, now + 5000, &f) == 0);
    free(d);
}

static void test_full(void)
{
    /* a file larger than the staging region does not fit: the computer is told the drive is full */
    uint32_t n = STAGE + 4096;
    uint8_t *d = make(n, 3);
    build();
    CHECK(uh_write_file(&dev, "BIG.MOD", d, n, UH_WIN) < 0);
    free(d);
    /* a read-only drive (no staging): the host cannot write */
    vdrive_t keep = vd;
    uint8_t *m2 = calloc(1, vd_mem_need(0));
    vd_init(&vd, m2, NULL, 0);
    build();
    CHECK(!vd_writable(&vd) && vd.nclusters >= 4085);
    CHECK(uh_write_file(&dev, "X.MOD", (const uint8_t *)"abc", 3, UH_WIN) < 0);
    free(m2);
    vd = keep;
}

/* ---- the subdirectory (NOTEBOOKS on the CALC drive) and long names ---- */
static char lnames[VD_MAX_FILES][64];

/* the root files plus a subdirectory with nsub generated notebook files of nlen-character names */
static void build_sub(int nsub, int nlen)
{
    vd_begin(&vd, "CALC", 0x1234ABCDu, (uint16_t)(46u << 9 | 1u << 5 | 1u), 0);
    CHECK(vd_add_file(&vd, "README.TXT", VD_ATTR_RO, sizeof hello - 1, rd_text, (void *)hello) == 0);
    CHECK(vd_add_dir(&vd, "NOTEBOOKS") == 0);
    for (int i = 0; i < nsub; i++) {
        char base[64];
        snprintf(base, sizeof base, "notebook %02d with a long name %s", i, "................................");
        snprintf(lnames[i], sizeof lnames[i], "%.*s.ipynb", nlen - 6, base);
        CHECK(vd_add_long(&vd, 1, lnames[i], VD_ATTR_RO, 1000u + 700u * (uint32_t)i, rd_pattern, NULL) == 0);
    }
    vd_build(&vd);
}

static int count_cb_n;
static void count_cb(const char *name, uint32_t size, uint8_t attr, void *u)
{
    (void)name, (void)size, (void)attr, (void)u;
    count_cb_n++;
}

static void test_subdir(void)
{
    build_sub(3, 20);
    /* the generated files read back by their long names, in the subdirectory */
    uint8_t *out = malloc(8192);
    int n = uh_read_file(&dev, "NOTEBOOKS/notebook 01 wi.ipynb", out, 8192);
    bool same = n == 1700;
    for (int i = 0; same && i < n; i++)
        same = out[i] == (uint8_t)(i * 7 + 3);
    CHECK(same);
    CHECK(uh_read_file(&dev, "notebooks/NOTEBOOK 02 WI.IPYNB", out, 8192) == 2400); /* case-insensitive */
    CHECK(uh_read_file(&dev, "NOTEBOOKS/missing.ipynb", out, 8192) == -2);
    count_cb_n = 0;
    CHECK(uh_list(&dev, "NOTEBOOKS", count_cb, NULL) == 0 && count_cb_n == 5); /* . .. and the three */
    CHECK(uh_list(&dev, "NOPE", count_cb, NULL) == -2);

    /* a notebook dropped into the subdirectory (macOS: AppleDouble next to it; Windows), and in the root */
    static const char nb[] = "{\"cells\": [], \"metadata\": {}, \"nbformat\": 4, \"nbformat_minor\": 5}\n";
    vd_found_t f, g;
    for (int style = 0; style < 2; style++) {
        build_sub(3, 20);
        CHECK(uh_write_file(&dev, "NOTEBOOKS/My Plan.ipynb", (const uint8_t *)nb, sizeof nb - 1, style) == 0);
        now += VD_QUIET_MS + 1;
        CHECK(vd_poll(&vd, now, &f) == 1);
        CHECK(f.notebook && !f.module && f.dir == 1 && !strcmp(f.name, "My Plan.ipynb") && f.size == sizeof nb - 1);
        CHECK(vd_file_read(&vd, &f, 0, out, f.size) == (int)f.size && !memcmp(out, nb, f.size));
        vd_done(&vd, &f);
        CHECK(vd_poll(&vd, now, &g) == 0); /* the AppleDouble file is no candidate */
    }
    CHECK(drop_and_find("plan.IPYNB", (const uint8_t *)nb, sizeof nb - 1, UH_WIN, &f) == 0);
    CHECK(f.notebook && f.dir == 0);

    /* replacing a generated file: the computer deletes it (its clusters are freed) and writes the new
     * copy into the free clusters, which come first: the staging region */
    build_sub(3, 20);
    uint8_t *big = make(30000, 5);
    CHECK(uh_write_file(&dev, "NOTEBOOKS/notebook 00 wi.ipynb", big, 30000, UH_MAC | UH_REPLACE) == 0);
    now += VD_QUIET_MS + 1;
    CHECK(vd_poll(&vd, now, &f) == 1 && f.dir == 1 && f.size == 30000 && f.first < vd.gen_first);
    CHECK(vd_file_read(&vd, &f, 0, out, 8192) == 8192 && !memcmp(out, big, 8192));
    CHECK(uh_read_file(&dev, "NOTEBOOKS/notebook 00 wi.ipynb", out, 8192) == 8192 && !memcmp(out, big, 8192));
    free(big);

    /* a nearly full subdirectory: the computer extends its chain into the staging region, and files
     * whose entries are there are found too */
    build_sub(76, 63); /* 76 x 6 entries + . and .. = 458 of 512 */
    char nm[64];
    int found = 0;
    for (int i = 0; i < 25; i++) { /* 25 x (3 + 3 AppleDouble) entries: past the end of the chain */
        snprintf(nm, sizeof nm, "NOTEBOOKS/dropped notebook %02d.ipynb", i);
        CHECK(uh_write_file(&dev, nm, (const uint8_t *)nb, sizeof nb - 1, UH_MAC) == 0);
    }
    uint32_t last = vd.sub_first + VD_SUB_SECTORS - 1, nx = (uint32_t)vd.fat[2 * last] | (uint32_t)vd.fat[2 * last + 1] << 8;
    CHECK(nx >= 2 && nx < vd.gen_first); /* the chain went on in the staging region */
    now += VD_QUIET_MS + 1;
    while (vd_poll(&vd, now, &f) == 1 && found < 40) {
        CHECK(f.notebook && f.dir == 1);
        vd_done(&vd, &f);
        found++;
        if (found == VD_MAX_DONE) /* (the done list is per generation: keep the rest for later) */
            break;
    }
    CHECK(found == VD_MAX_DONE);
    CHECK(uh_read_file(&dev, "NOTEBOOKS/dropped notebook 24.ipynb", out, 8192) == (int)sizeof nb - 1);
    /* the table is full at VD_MAX_FILES; generated clusters are limited */
    vd_begin(&vd, "CALC", 1, 0, 0);
    CHECK(vd_add_dir(&vd, "NOTEBOOKS") == 0);
    CHECK(vd_add_long(&vd, 1, "huge.ipynb", VD_ATTR_RO, VD_GEN_CLUSTERS * VD_SECTOR, rd_pattern, NULL) == -1);
    CHECK(vd_add_long(&vd, 1, "ok.ipynb", VD_ATTR_RO, 1u << 20, rd_pattern, NULL) == 0);
    vd_begin(&vd, "CALC", 1, 0, 0);
    CHECK(vd_add_long(&vd, 1, "x.ipynb", VD_ATTR_RO, 10, rd_pattern, NULL) == -1); /* no subdirectory */
    free(out);
}

/* A large generated file with a long name in the root (CALCULATOR.HTML, the browser notebook,
 * app/usb_app.c) next to the NOTEBOOKS folder: read back byte for byte, and a module dropped
 * afterwards is still found. */
static void test_root_long(void)
{
    const uint32_t big = 700000;
    vd_begin(&vd, "CALC", 1, 0, 0);
    CHECK(vd_add_file(&vd, "README.TXT", VD_ATTR_RO, sizeof hello - 1, rd_text, (void *)hello) == 0);
    CHECK(vd_add_long(&vd, 0, "CALCULATOR.HTML", VD_ATTR_RO, big, rd_pattern, NULL) == 0);
    CHECK(vd_add_dir(&vd, "NOTEBOOKS") == 0);
    CHECK(vd_add_long(&vd, 1, "a.ipynb", VD_ATTR_RO, 100, rd_pattern, NULL) == 0);
    vd_build(&vd);
    uint8_t *back = malloc(big);
    int n = uh_read_file(&dev, "Calculator.html", back, big);
    bool same = n == (int)big;
    for (uint32_t i = 0; same && i < big; i++)
        same = back[i] == (uint8_t)(i * 7 + 3);
    CHECK(same);
    CHECK(uh_read_file(&dev, "NOTEBOOKS/a.ipynb", back, big) == 100);
    free(back);
    uint8_t *m = make(5000, 9);
    CHECK(uh_write_file(&dev, "LATE.MOD", m, 5000, UH_WIN) == 0);
    now += VD_QUIET_MS + 1;
    vd_found_t f;
    CHECK(vd_poll(&vd, now, &f) == 1 && f.module && f.size == 5000);
    free(m);
}

static void test_names(void)
{
    char n[11];
    CHECK(vd_name83("INFO.TXT", n) && !memcmp(n, "INFO    TXT", 11));
    CHECK(vd_name83("readme.txt", n) && !memcmp(n, "README  TXT", 11));
    CHECK(vd_name83("SCREEN", n) && !memcmp(n, "SCREEN     ", 11));
    CHECK(!vd_name83("TOOLONGNAME.TXT", n));
    CHECK(!vd_name83("A.TOOL", n));
}

int main(void)
{
    mem = calloc(1, vd_mem_need(STAGE));
    stage = calloc(1, STAGE);
    vd_init(&vd, mem, stage, STAGE);
    test_volume();
    test_mac_win();
    test_incomplete();
    test_full();
    test_names();
    test_subdir();
    test_root_long();
    free(mem);
    free(stage);
    printf("test_vdrive: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
