/*
 * Unit tests of core/fat.c details that need hand-built directory entries or
 * specific failure points, on the simulator's RAM microSD card
 * (hal_sd_sim_insert; sim_sd_writes_left >= 0 makes block writes fail after
 * that many). The general FAT tests are in test_stat.c.
 *   - long names: an entry with a VFAT long name spanning a sector boundary
 *     is listed by its long name; fs_remove deletes the long-name entries
 *     too; orphaned long-name entries are not attached to an unrelated
 *     short entry (checksum);
 *   - fs_open on an already open handle returns FS_EBUSY and leaves it intact;
 *   - a failed directory-sector write does not leave stale data in the cache;
 *   - fs_mkdir frees its cluster when the directory entry cannot be added.
 * Built and run by `make test`.
 */
#include <stdio.h>
#include <string.h>
#include "../../platform/sim/sim.h"
#include "fat.h"
#include "hal.h"

static int fails, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if (!(c)) {                                                               \
            fails++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

static int write_file(const char *path, const char *data, size_t n)
{
    fs_file_t f;
    int r = fs_open(&f, path, FS_WRITE);
    if (r)
        return r;
    if (n && (r = fs_write(&f, data, n)) < 0)
        return r;
    return fs_close(&f);
}

/* fs_list() callback: remembers the last name and counts entries. */
typedef struct {
    int n;
    char name[64];
} seen_t;

static int seen_cb(const fs_dirent_t *e, void *user)
{
    seen_t *s = user;
    s->n++;
    strcpy(s->name, e->name);
    return 0;
}

static uint8_t sfn_sum(const uint8_t *e)
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + e[i]);
    return sum;
}

/* VFAT long-name entry `seq` (1-based, `last` adds 0x40) of `name`. */
static void lfn_entry(uint8_t *e, const char *name, int seq, bool last, uint8_t sum)
{
    static const uint8_t pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    memset(e, 0, 32);
    e[0] = (uint8_t)(seq | (last ? 0x40 : 0));
    e[11] = 0x0F;
    e[13] = sum;
    int len = (int)strlen(name);
    for (int k = 0; k < 13; k++) {
        int at = (seq - 1) * 13 + k;
        uint16_t ch = at < len ? (uint8_t)name[at] : at == len ? 0 : 0xFFFF;
        e[pos[k]] = (uint8_t)ch;
        e[pos[k] + 1] = (uint8_t)(ch >> 8);
    }
}

static void test_long_names(void)
{
    CHECK(hal_sd_sim_insert(8) == 0); /* FAT16: fixed root directory */
    CHECK(fs_mkfs() == 0);
    uint8_t b[512], s0[512], s1[512];
    CHECK(hal_sd_read(0, b, 1) == 0);
    uint32_t root = (uint32_t)(b[14] | b[15] << 8) + b[16] * (uint32_t)(b[22] | b[23] << 8);
    uint32_t free0, free1, blocks;
    int type;
    CHECK(fs_info(&blocks, &free0, &type) == 0);

    /* The short entry is created by fat.c (slot 0), then moved to slot 0 of
     * the next sector behind two long-name entries in slots 14 and 15. */
    static char data[3000];
    memset(data, 'x', sizeof data);
    CHECK(write_file("/LONGNA~1.TXT", data, sizeof data) == 0);
    fs_unmount();
    CHECK(hal_sd_read(root, b, 1) == 0);
    const char *lname = "Long Name File.txt"; /* 18 characters: 2 entries */
    memset(s0, 0, sizeof s0);
    memset(s1, 0, sizeof s1);
    for (int i = 0; i < 14; i++)
        s0[i * 32] = 0xE5;
    lfn_entry(s0 + 14 * 32, lname, 2, true, sfn_sum(b));
    lfn_entry(s0 + 15 * 32, lname, 1, false, sfn_sum(b));
    memcpy(s1, b, 32);
    CHECK(hal_sd_write(root, s0, 1) == 0 && hal_sd_write(root + 1, s1, 1) == 0);

    seen_t s = {0};
    CHECK(fs_list("/", seen_cb, &s) == 0);
    CHECK(s.n == 1 && !strcmp(s.name, lname));
    CHECK(fs_remove("/long name file.TXT") == 0);
    fs_unmount();
    CHECK(hal_sd_read(root, b, 1) == 0);
    CHECK(b[14 * 32] == 0xE5 && b[15 * 32] == 0xE5); /* long-name entries deleted */
    CHECK(hal_sd_read(root + 1, b, 1) == 0);
    CHECK(b[0] == 0xE5);
    CHECK(fs_info(&blocks, &free1, &type) == 0);
    CHECK(free1 == free0);

    /* Orphans (long-name entries left by another system) followed by an
     * unrelated short entry: the long name must not be attached to it. */
    CHECK(write_file("/NEW.TXT", "n", 1) == 0);
    fs_unmount();
    CHECK(hal_sd_read(root, b, 1) == 0); /* NEW.TXT took the first free slot */
    memcpy(s1, b, 32);
    CHECK(hal_sd_write(root, s0, 1) == 0 && hal_sd_write(root + 1, s1, 1) == 0);
    memset(&s, 0, sizeof s);
    CHECK(fs_list("/", seen_cb, &s) == 0);
    CHECK(s.n == 1 && !strcmp(s.name, "NEW.TXT"));
    fs_unmount();
    hal_sd_sim_insert(0);
}

static void test_open_busy(void)
{
    CHECK(hal_sd_sim_insert(8) == 0);
    CHECK(fs_mkfs() == 0);
    CHECK(write_file("/a.txt", "abc", 3) == 0);
    CHECK(write_file("/b.txt", "def", 3) == 0);
    fs_file_t f;
    char buf[8];
    CHECK(fs_open(&f, "/a.txt", FS_READ) == 0);
    CHECK(fs_open(&f, "/b.txt", FS_READ) == FS_EBUSY);
    CHECK(fs_read(&f, buf, sizeof buf) == 3 && !memcmp(buf, "abc", 3)); /* handle intact */
    CHECK(fs_close(&f) == 0);
    CHECK(fs_open(&f, "/b.txt", FS_READ) == 0); /* not stuck busy */
    CHECK(fs_close(&f) == 0);
    fs_unmount();
    hal_sd_sim_insert(0);
}

static void test_failed_store(void)
{
    CHECK(hal_sd_sim_insert(8) == 0);
    CHECK(fs_mkfs() == 0);
    CHECK(write_file("/keep.txt", "abc", 3) == 0);
    sim_sd_writes_left = 0; /* the directory-entry write of fs_remove fails */
    CHECK(fs_remove("/keep.txt") == FS_EIO);
    sim_sd_writes_left = -1;
    seen_t s = {0}; /* still mounted: the listing must come from the card, not the cache */
    CHECK(fs_list("/", seen_cb, &s) == 0);
    CHECK(s.n == 1 && !strcmp(s.name, "KEEP.TXT"));
    fs_unmount();
    hal_sd_sim_insert(0);
}

static void test_mkdir_full_root(void)
{
    CHECK(hal_sd_sim_insert(40) == 0); /* FAT16 with 1 KB clusters (fs_info counts KB) */
    CHECK(fs_mkfs() == 0);
    char name[16];
    int r = 0;
    for (int i = 0; i < 512 && !r; i++) { /* fill the 512-entry FAT16 root */
        snprintf(name, sizeof name, "/F%03d", i);
        r = write_file(name, NULL, 0);
    }
    CHECK(r == 0);
    uint32_t free0, free1, blocks;
    int type;
    CHECK(fs_info(&blocks, &free0, &type) == 0);
    CHECK(fs_mkdir("/sub") == FS_EFULL);
    CHECK(fs_info(&blocks, &free1, &type) == 0);
    CHECK(free1 == free0);
    fs_unmount();
    hal_sd_sim_insert(0);
}

/* Long names written by fat.c (the notebooks' CALC/NOTEBOOKS/NAME.ipynb): a directory and files whose
 * names are not 8.3 get VFAT entries before a unique short alias; they are found again by the long
 * name (any case), replaced in place, listed by it, removed with their entries; enough of them fill
 * the directory's first cluster and the runs of entries continue in the next one. */
typedef struct {
    int n, found;
    const char *want;
} find_t;
static int find_cb(const fs_dirent_t *e, void *user)
{
    find_t *f = user;
    f->n++;
    f->found += !strcmp(e->name, f->want);
    return 0;
}

static void test_create_long_names(int mb)
{
    CHECK(hal_sd_sim_insert(mb) == 0);
    CHECK(fs_mkfs() == 0);
    CHECK(fs_mkdir("/CALC") == 0);
    CHECK(fs_mkdir("/CALC/NOTEBOOKS") == 0);
    CHECK(fs_mkdir("/CALC/NOTEBOOKS") == 0); /* exists: fine */
    static const char a[] = "{\"cells\": []}\n";
    CHECK(write_file("/CALC/NOTEBOOKS/My Plan for May.ipynb", a, sizeof a - 1) == 0);
    CHECK(write_file("/CALC/NOTEBOOKS/My Plan for June.ipynb", "June", 4) == 0);
    find_t f = {0, 0, "My Plan for May.ipynb"};
    CHECK(fs_list("/CALC/NOTEBOOKS", find_cb, &f) == 0 && f.n == 2 && f.found == 1);
    f = (find_t){0, 0, "NOTEBOOKS"};
    CHECK(fs_list("/CALC", find_cb, &f) == 0 && f.found == 1);
    fs_file_t h;
    char buf[64];
    CHECK(fs_open(&h, "/calc/notebooks/my plan for may.IPYNB", FS_READ) == 0);
    CHECK(fs_read(&h, buf, sizeof buf) == (int)sizeof a - 1 && !memcmp(buf, a, sizeof a - 1));
    fs_close(&h);
    CHECK(fs_open(&h, "/CALC/NOTEBOOKS/My Plan for June.ipynb", FS_READ) == 0);
    CHECK(fs_read(&h, buf, sizeof buf) == 4 && !memcmp(buf, "June", 4));
    fs_close(&h);
    /* replaced in place: still one entry under that name */
    CHECK(write_file("/CALC/NOTEBOOKS/My Plan for May.ipynb", "new", 3) == 0);
    f = (find_t){0, 0, "My Plan for May.ipynb"};
    CHECK(fs_list("/CALC/NOTEBOOKS", find_cb, &f) == 0 && f.n == 2 && f.found == 1);
    CHECK(fs_open(&h, "/CALC/NOTEBOOKS/My Plan for May.ipynb", FS_READ) == 0);
    CHECK(fs_read(&h, buf, sizeof buf) == 3);
    fs_close(&h);
    /* many: the directory's chain grows, every name is found */
    char nm[80];
    for (int i = 0; i < 40; i++) {
        snprintf(nm, sizeof nm, "/CALC/NOTEBOOKS/Notebook number %02d.ipynb", i);
        CHECK(write_file(nm, nm, strlen(nm)) == 0);
    }
    for (int i = 0; i < 40; i += 13) {
        snprintf(nm, sizeof nm, "Notebook number %02d.ipynb", i);
        f = (find_t){0, 0, nm};
        CHECK(fs_list("/CALC/NOTEBOOKS", find_cb, &f) == 0 && f.found == 1 && f.n == 42);
    }
    CHECK(fs_remove("/CALC/NOTEBOOKS/My Plan for June.ipynb") == 0);
    f = (find_t){0, 0, "My Plan for June.ipynb"};
    CHECK(fs_list("/CALC/NOTEBOOKS", find_cb, &f) == 0 && f.found == 0 && f.n == 41);
    CHECK(write_file("/CALC/NOTEBOOKS/.hidden", "x", 1) == FS_ENAME);
    fs_unmount();
    hal_sd_sim_insert(0);
}

int main(void)
{
    test_long_names();
    test_create_long_names(8);   /* FAT16 */
    test_create_long_names(128); /* FAT32 */
    test_open_busy();
    test_failed_store();
    test_mkdir_full_root();
    printf("test_fat: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
