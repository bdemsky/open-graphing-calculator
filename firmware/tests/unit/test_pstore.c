/* Host unit tests for core/pstore.c: snapshot log on a simulated NOR flash with
 * power loss injected at every point of a save.
 *
 * pstore keeps the saved session (app/session.c) as a ring of snapshots in
 * the QSPI flash; see core/pstore.h for the format and write order. Here the
 * flash is a RAM array behind a pstore_dev_t whose erase/write callbacks
 * behave like NOR (erase sets 0xFF, programming can only clear bits) and can
 * "lose power" after a given number of operations, leaving a partly erased
 * sector or a half-programmed byte as real hardware might. The tests check:
 *   - test_basic: mount/save/load/remount, size limit, empty snapshots,
 *     fallback to the previous snapshot when the newest is corrupt, format;
 *   - test_power_loss: a power cut at every step of a save leaves exactly
 *     the old or the new snapshot, and the next save works;
 *   - test_wear: many saves wrap the ring and spread erases evenly, and
 *     nothing outside the region is touched.
 * Built and run by `make test` (tests/unit/test_*.c, see Makefile). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pstore.h"

static int fails, checks;
#define CHECK(c)                                                              \
    do {                                                                      \
        checks++;                                                             \
        if (!(c)) {                                                           \
            fails++;                                                          \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)

/* ---------------------------------------------------------- fake NOR flash
 * A 48-sector region placed 8 sectors into the device (so writes below the
 * region can be detected), plus one spare sector after it. Every erased
 * sector and every programmed byte is one "operation"; `budget` counts down
 * the operations still allowed before the simulated power loss. */
#define SECTOR 4096u
#define NSECT 48u
#define BASE (8u * SECTOR) /* region starts inside the device */
static uint8_t nor[BASE + NSECT * SECTOR + SECTOR];
static unsigned erase_count[NSECT];
static long budget = -1; /* bytes/sectors that may still be modified; -1 = unlimited */
static bool cut;         /* the power went off */

/* Device callbacks for pstore (absolute device offsets). Erase and write
 * return -1 and set `cut` when the budget is exhausted. */
static int f_read(uint32_t off, void *buf, size_t n)
{
    memcpy(buf, nor + off, n);
    return 0;
}

static int f_erase(uint32_t off, size_t n)
{
    if (off % SECTOR || n % SECTOR || off < BASE)
        return -1;
    for (size_t s = 0; s < n; s += SECTOR) {
        if (budget == 0) {
            cut = true;
            /* an interrupted erase leaves the sector partly erased */
            memset(nor + off + s, 0xFF, SECTOR / 3);
            return -1;
        }
        if (budget > 0)
            budget--;
        memset(nor + off + s, 0xFF, SECTOR);
        erase_count[(off + s - BASE) / SECTOR]++;
    }
    return 0;
}

static int f_write(uint32_t off, const void *buf, size_t n)
{
    const uint8_t *b = buf;
    for (size_t i = 0; i < n; i++) {
        if (budget == 0) {
            cut = true;
            nor[off + i] &= (uint8_t)(b[i] | 0x0F); /* half-programmed byte */
            return -1;
        }
        if (budget > 0)
            budget--;
        nor[off + i] &= b[i];
    }
    return 0;
}

static const pstore_dev_t dev = {f_read, f_erase, f_write, BASE, NSECT * SECTOR, SECTOR};

/* Deterministic test payload; different seeds give different contents. */
static void fill(uint8_t *p, size_t n, unsigned seed)
{
    for (size_t i = 0; i < n; i++)
        p[i] = (uint8_t)((i * 131u + seed * 7919u) >> 3);
}

static uint8_t data_a[60000], data_b[60000], got[70000];

/* Normal operation on a blank (all 0xFF) device. A snapshot may use at most
 * a third of the region minus the header (16 sectors here), and
 * pstore_save() returns -2 for anything larger. Flipping one payload bit of
 * the newest snapshot (ps.off is its offset in the region) must make mount
 * fall back to the previous one. */
static void test_basic(void)
{
    pstore_t ps;
    memset(nor, 0xFF, sizeof nor);
    CHECK(pstore_mount(&ps, &dev) == 0);
    CHECK(!ps.have);
    CHECK(pstore_load(&ps, got, sizeof got) == 0);
    CHECK(pstore_max_payload(&ps) == NSECT / 3 * SECTOR - PSTORE_HDR);
    fill(data_a, 1000, 1);
    CHECK(pstore_save(&ps, data_a, 1000) == 0);
    CHECK(pstore_load(&ps, got, sizeof got) == 1000 && !memcmp(got, data_a, 1000));
    /* survives a remount */
    CHECK(pstore_mount(&ps, &dev) == 0 && ps.have && ps.len == 1000);
    CHECK(pstore_load(&ps, got, sizeof got) == 1000 && !memcmp(got, data_a, 1000));
    /* too large */
    CHECK(pstore_save(&ps, data_a, pstore_max_payload(&ps) + 1) == -2);
    /* empty payload is a valid snapshot */
    CHECK(pstore_save(&ps, data_a, 0) == 0);
    CHECK(pstore_mount(&ps, &dev) == 0 && ps.have && ps.len == 0);
    /* a corrupted newest payload falls back to the previous snapshot */
    fill(data_b, 5000, 2);
    CHECK(pstore_save(&ps, data_a, 1000) == 0);
    CHECK(pstore_save(&ps, data_b, 5000) == 0);
    nor[BASE + ps.off + PSTORE_HDR + 100] ^= 0x01;
    CHECK(pstore_mount(&ps, &dev) == 0 && ps.have);
    CHECK(pstore_load(&ps, got, sizeof got) == 1000 && !memcmp(got, data_a, 1000));
    /* format forgets everything */
    CHECK(pstore_format(&ps) == 0 && !ps.have);
    CHECK(pstore_mount(&ps, &dev) == 0 && !ps.have);
}

/* Power loss at every step of a save: afterwards the store holds either the
 * old or the new snapshot, never anything else; a following save works. */
static void test_power_loss(size_t len_old, size_t len_new)
{
    pstore_t ps;
    fill(data_a, len_old, 3);
    fill(data_b, len_new, 4);
    long steps = 0;
    /* count the operations of an uninterrupted save: a huge budget is
     * decremented once per operation, the difference is the step count */
    memset(nor, 0xFF, sizeof nor);
    pstore_mount(&ps, &dev);
    pstore_save(&ps, data_a, len_old);
    budget = 1L << 30;
    pstore_save(&ps, data_b, len_new);
    steps = (1L << 30) - budget;
    budget = -1;
    int olds = 0, news = 0, bad = 0;
    /* Cut at every step for small saves; for large ones (a 60 KB save is
     * ~60000 steps, each iteration re-runs whole saves) every step near the
     * start and the end, where the erases and the header write happen, and
     * about 1500 evenly spaced cut points in between. */
    long stride = steps > 4000 ? steps / 1500 : 1;
    for (long cutat = 0; cutat <= steps; cutat += (cutat < 64 || steps - cutat < 64) ? 1 : stride) {
        memset(nor, 0xFF, sizeof nor);
        pstore_mount(&ps, &dev);
        CHECK(pstore_save(&ps, data_a, len_old) == 0);
        budget = cutat;
        cut = false;
        int rc = pstore_save(&ps, data_b, len_new);
        budget = -1;
        CHECK(rc == 0 || cut);
        pstore_t ps2;
        CHECK(pstore_mount(&ps2, &dev) == 0 && ps2.have);
        long n = pstore_load(&ps2, got, sizeof got);
        /* classify the survivor: the new snapshot is acceptable after a
         * successful or an interrupted save, the old one only if the save
         * did not report success; anything else (garbage, wrong length) is
         * a failure */
        if (n == (long)len_new && !memcmp(got, data_b, len_new) && (rc == 0 || cut))
            news++;
        else if (n == (long)len_old && !memcmp(got, data_a, len_old) && rc != 0)
            olds++;
        else
            bad++;
        /* the next save after the reboot succeeds and is the newest */
        CHECK(pstore_save(&ps2, data_b, len_new) == 0);
        pstore_t ps3;
        pstore_mount(&ps3, &dev);
        CHECK(pstore_load(&ps3, got, sizeof got) == (long)len_new && !memcmp(got, data_b, len_new));
    }
    CHECK(bad == 0);
    CHECK(olds > 0 && news > 0);
    printf("power loss %zu -> %zu bytes: %ld steps, old kept %d, new %d, bad %d\n", len_old, len_new, steps, olds,
           news, bad);
}

/* Many saves wrap around the region and spread the erases evenly. 600 saves
 * of 500..20500 bytes with a remount every 97 saves; afterwards every
 * sector must have been erased at least once and no sector more than three
 * times as often as the least used one. */
static void test_wear(void)
{
    pstore_t ps;
    memset(nor, 0xFF, sizeof nor);
    memset(erase_count, 0, sizeof erase_count);
    pstore_mount(&ps, &dev);
    for (int i = 0; i < 600; i++) {
        size_t len = 500 + (size_t)(i * 977) % 20000;
        fill(data_a, len, (unsigned)i);
        CHECK(pstore_save(&ps, data_a, len) == 0);
        if (i % 97 == 0) { /* reboot now and then */
            pstore_mount(&ps, &dev);
            CHECK(pstore_load(&ps, got, sizeof got) == (long)len && !memcmp(got, data_a, len));
        }
    }
    unsigned lo = ~0u, hi = 0;
    for (unsigned s = 0; s < NSECT; s++) {
        if (erase_count[s] < lo)
            lo = erase_count[s];
        if (erase_count[s] > hi)
            hi = erase_count[s];
    }
    printf("wear: 600 saves, erases per sector %u..%u\n", lo, hi);
    CHECK(lo > 0 && hi <= lo * 3);
    /* nothing outside the region was touched: the sectors below it (f_erase
     * also refuses offsets below BASE) and the spare sector after it */
    for (unsigned i = 0; i < sizeof nor; i++)
        if ((i < BASE || i >= BASE + NSECT * SECTOR) && nor[i] != 0xFF) {
            CHECK(!"write outside region");
            break;
        }
}

/* Power-loss cases: growing snapshot, shrinking snapshot, and the largest
 * size used here (60000 bytes, close to the 65504-byte maximum). */
int main(void)
{
    test_basic();
    test_power_loss(3000, 9000);
    test_power_loss(20000, 700);
    test_power_loss(60000, 60000);
    test_wear();
    printf("test_pstore: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
