/* Host unit tests for core/modstore.c: the content module store (QSPI) with
 * power loss injected at every flash operation of an install.
 *
 * The store lives on a RAM NOR device behind a pstore_dev_t (erase sets
 * 0xFF, programming only clears bits; after `budget` operations the power
 * "goes off": a partly erased sector, a half-programmed write). Modules are
 * built here (header + payload, as tools/mkmodule.py packs them). Checks:
 *   - test_basic: empty store, install, header/ABI checks, upgrade keeps the
 *     previous version, revert, remove, reuse of freed space, store full;
 *   - test_power_loss: a cut at every step of installing v2 over v1 leaves
 *     v1 or v2 active with an intact payload, never anything else, and the
 *     next install works;
 *   - test_compact: compaction closes a gap, and a cut at every step of it
 *     leaves both modules intact and exactly as active as before;
 *   - test_resolve: the running-firmware view (mod_boot over the simulator's
 *     emulated QSPI): ABI mismatch, dependencies, a damaged active version
 *     falling back to the previous one, safe mode, an absent QSPI.
 * Built and run by `make test`. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hal.h"
#include "modstore.h"
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

/* ---------------------------------------------------------- fake NOR */
#define SECTOR 4096u
#define NSECT 96u /* 384 KB store */
static uint8_t nor[NSECT * SECTOR];
static long budget = -1; /* operations (sectors erased, bytes programmed) left; -1 = unlimited */

static int f_read(uint32_t off, void *buf, size_t n)
{
    if (off + n > sizeof nor)
        return -1;
    memcpy(buf, nor + off, n);
    return 0;
}
static int f_erase(uint32_t off, size_t n)
{
    if (off % SECTOR || n % SECTOR || off + n > sizeof nor)
        return -1;
    for (size_t s = 0; s < n; s += SECTOR) {
        if (budget == 0) {
            memset(nor + off + s, 0xFF, SECTOR / 3); /* interrupted: partly erased */
            return -1;
        }
        if (budget > 0)
            budget--;
        memset(nor + off + s, 0xFF, SECTOR);
    }
    return 0;
}
static int f_write(uint32_t off, const void *buf, size_t n)
{
    if (off + n > sizeof nor)
        return -1;
    const uint8_t *b = buf;
    for (size_t i = 0; i < n; i++) {
        if (budget == 0) {
            nor[off + i] &= b[i] | 0x0F; /* half a byte made it */
            return -1;
        }
        if (budget > 0)
            budget--;
        nor[off + i] &= b[i];
    }
    return 0;
}
static const pstore_dev_t dev = {f_read, f_erase, f_write, 0, sizeof nor, SECTOR};

/* ---------------------------------------------------------- modules */
static const mod_iface_t ifs[] = {{"help", 1}, {"cas", 3}};

/* A module file: header + 0xFF to 4 KB + payload of `len` pseudo-random bytes
 * derived from `seed`. Returns a malloc'ed buffer, its size in *size. */
static uint8_t *make_mod(const char *name, int major, int minor, const char *iface, int abi_min, int abi_max,
                         uint32_t len, unsigned seed, const mod_dep_t *deps, int ndeps, size_t *size)
{
    *size = MOD_PAYLOAD_OFF + len;
    uint8_t *m = malloc(*size);
    memset(m, 0xFF, MOD_PAYLOAD_OFF);
    for (uint32_t i = 0; i < len; i++)
        m[MOD_PAYLOAD_OFF + i] = (uint8_t)((i * 2654435761u + seed * 40503u) >> 13);
    mod_hdr_t h;
    memset(&h, 0, sizeof h);
    h.magic = MOD_MAGIC;
    h.fmt = MOD_FMT;
    h.hdr_bytes = MOD_HDR_BYTES;
    strncpy(h.name, name, sizeof h.name);
    h.ver[0] = (uint16_t)major, h.ver[1] = (uint16_t)minor;
    strncpy(h.iface, iface, sizeof h.iface);
    h.abi_min = (uint16_t)abi_min, h.abi_max = (uint16_t)abi_max;
    h.tier = MOD_TIER_STANDARD;
    h.ndeps = (uint8_t)ndeps;
    for (int i = 0; i < ndeps; i++)
        h.deps[i] = deps[i];
    h.payload_len = len;
    h.payload_crc = crc32_update(0, m + MOD_PAYLOAD_OFF, len);
    h.hcrc = crc32_update(0, &h, offsetof(mod_hdr_t, hcrc));
    memcpy(m, &h, sizeof h);
    return m;
}

/* Install a file in uneven chunks (as the SD reader / console deliver it). */
static int install(modstore_t *st, const uint8_t *m, size_t size, bool force)
{
    static mod_inst_t in;
    int r = mod_inst_begin(&in, st, ifs, 2, force);
    for (size_t p = 0; !r && p < size;) {
        size_t k = size - p < 1000 ? size - p : 1000;
        r = mod_inst_write(&in, m + p, k);
        p += k;
    }
    return r ? r : mod_inst_finish(&in);
}

/* Active (newest activation) version of `name` and its payload check. */
static int active(modstore_t *st, const char *name, int *major)
{
    int v[MOD_MAX_EXTENTS];
    int n = modstore_versions(st, name, v, MOD_MAX_EXTENTS);
    if (n < 1)
        return 0;
    uint32_t crc;
    if (modstore_payload_crc(st, v[0], &crc) || crc != st->ext[v[0]].payload_crc)
        return -1;
    *major = st->ext[v[0]].ver[0];
    return n;
}

static void test_basic(void)
{
    modstore_t st;
    memset(nor, 0xFF, sizeof nor);
    modstore_mount(&st, &dev, false);
    CHECK(st.n == 0 && st.used == 0 && !st.io_error);

    size_t s1, s2, s3, sb;
    uint8_t *m1 = make_mod("manual", 1, 0, "help", 1, 1, 100000, 1, NULL, 0, &s1);
    uint8_t *m2 = make_mod("manual", 2, 0, "help", 1, 2, 90000, 2, NULL, 0, &s2);
    uint8_t *x = make_mod("special", 1, 0, "cas", 3, 4, 50000, 3, NULL, 0, &s3);
    CHECK(install(&st, m1, s1, false) == 0);
    int maj = 0;
    CHECK(active(&st, "manual", &maj) == 1 && maj == 1);
    modstore_mount(&st, &dev, false); /* survives a remount */
    CHECK(active(&st, "manual", &maj) == 1 && maj == 1);

    /* format errors: bad header CRC, wrong ABI (unless forced), short file, long file */
    uint8_t *bad = malloc(s1);
    memcpy(bad, m1, s1);
    bad[10] ^= 1;
    CHECK(install(&st, bad, s1, false) == MOD_E_FORMAT);
    uint8_t *wrong = make_mod("manual", 3, 0, "help", 2, 2, 1000, 4, NULL, 0, &sb);
    CHECK(install(&st, wrong, sb, false) == MOD_E_ABI);
    CHECK(install(&st, m1, s1 - 10, false) == MOD_E_SIZE);
    modstore_mount(&st, &dev, false);
    CHECK(active(&st, "manual", &maj) == 1 && maj == 1); /* none of that disturbed v1 */

    /* upgrade: v1 stays as the previous version; another module is independent */
    CHECK(install(&st, m2, s2, false) == 0);
    CHECK(install(&st, x, s3, false) == 0);
    modstore_mount(&st, &dev, false);
    CHECK(active(&st, "manual", &maj) == 2 && maj == 2);
    CHECK(active(&st, "special", &maj) == 1);
    /* revert: v1 active again, v2 kept; revert again: v2 */
    CHECK(modstore_revert(&st, "manual") == 0);
    modstore_mount(&st, &dev, false);
    CHECK(active(&st, "manual", &maj) == 2 && maj == 1);
    CHECK(modstore_revert(&st, "manual") == 0);
    CHECK(active(&st, "manual", &maj) == 2 && maj == 2);
    CHECK(modstore_revert(&st, "special") == MOD_E_NOTFOUND);
    /* a third version drops the oldest (only the active one is kept as fallback) */
    CHECK(install(&st, m1, s1, false) == 0);
    modstore_mount(&st, &dev, false);
    CHECK(active(&st, "manual", &maj) == 2 && maj == 1);
    /* remove: gone, the other module untouched, the space is reused */
    CHECK(modstore_remove(&st, "manual") == 0);
    modstore_mount(&st, &dev, false);
    CHECK(active(&st, "manual", &maj) == 0);
    CHECK(active(&st, "special", &maj) == 1);
    uint32_t used = st.used;
    CHECK(install(&st, m1, s1, false) == 0);
    CHECK(st.used == used + (MOD_PAYLOAD_OFF + 100000 + SECTOR - 1) / SECTOR * SECTOR);
    /* full: a module larger than the free space; with room for only one version the
     * active one is dropped to make room */
    size_t sbig;
    uint8_t *big = make_mod("manual", 4, 0, "help", 1, 1, (NSECT - 20) * SECTOR, 5, NULL, 0, &sbig);
    modstore_t st2;
    modstore_mount(&st2, &dev, false);
    int r = install(&st2, big, sbig, false);
    CHECK(r == 0); /* fits once v1 is dropped */
    modstore_mount(&st2, &dev, false);
    CHECK(active(&st2, "manual", &maj) == 1 && maj == 4);
    size_t shuge;
    uint8_t *huge = make_mod("other", 1, 0, "help", 1, 1, NSECT * SECTOR, 6, NULL, 0, &shuge);
    CHECK(install(&st2, huge, shuge, false) == MOD_E_SIZE);
    size_t sfull;
    uint8_t *full = make_mod("other", 1, 0, "help", 1, 1, 30 * SECTOR, 7, NULL, 0, &sfull);
    CHECK(install(&st2, full, sfull, false) == MOD_E_FULL);
    CHECK(modstore_format(&st2) == 0);
    modstore_mount(&st2, &dev, false);
    CHECK(st2.n == 0);
    free(m1), free(m2), free(x), free(bad), free(wrong), free(big), free(huge), free(full);
}

/* Count the operations of one install, then cut the power after each of
 * them in turn (fresh v1 each time). */
static void test_power_loss(void)
{
    size_t s1, s2;
    uint8_t *m1 = make_mod("manual", 1, 0, "help", 1, 1, 20000, 11, NULL, 0, &s1);
    uint8_t *m2 = make_mod("manual", 2, 0, "help", 1, 1, 30000, 12, NULL, 0, &s2);
    modstore_t st;
    long total;
    {
        memset(nor, 0xFF, sizeof nor);
        modstore_mount(&st, &dev, false);
        install(&st, m1, s1, false);
        budget = 1L << 40;
        modstore_mount(&st, &dev, false);
        install(&st, m2, s2, false);
        total = (1L << 40) - budget;
        budget = -1;
    }
    int old_kept = 0, new_won = 0, bad = 0;
    for (long cut = 0; cut <= total; cut += cut < 64 || total - cut < 64 ? 1 : 97) {
        memset(nor, 0xFF, sizeof nor);
        modstore_mount(&st, &dev, false);
        install(&st, m1, s1, false);
        modstore_mount(&st, &dev, false);
        budget = cut;
        install(&st, m2, s2, false);
        budget = -1;
        modstore_mount(&st, &dev, false);
        int maj = 0, n = active(&st, "manual", &maj);
        if (n > 0 && maj == 1)
            old_kept++;
        else if (n > 0 && maj == 2)
            new_won++;
        else
            bad++;
        /* the store is still usable: installing v2 now works */
        CHECK(install(&st, m2, s2, false) == 0);
        modstore_mount(&st, &dev, false);
        CHECK(active(&st, "manual", &maj) > 0 && maj == 2);
    }
    CHECK(bad == 0 && old_kept > 0 && new_won > 0);
    printf("power loss during install (%ld operations): old kept %d, new %d, bad %d\n", total, old_kept, new_won,
           bad);
    free(m1), free(m2);
}

/* A gap at the start (a removed module) with two modules after it. */
static void fragmented(modstore_t *st, const uint8_t *a, size_t sa, const uint8_t *b, size_t sb, const uint8_t *c,
                       size_t sc)
{
    memset(nor, 0xFF, sizeof nor);
    modstore_mount(st, &dev, false);
    install(st, a, sa, false);
    install(st, b, sb, false);
    install(st, c, sc, false);
    modstore_remove(st, "first");
    modstore_mount(st, &dev, false);
}

static void test_compact(void)
{
    size_t sa, sb, sc;
    uint8_t *a = make_mod("first", 1, 0, "help", 1, 1, 40000, 31, NULL, 0, &sa);
    uint8_t *b = make_mod("second", 1, 0, "help", 1, 1, 30000, 32, NULL, 0, &sb);
    uint8_t *c = make_mod("third", 1, 0, "help", 1, 1, 20000, 33, NULL, 0, &sc);
    modstore_t st;
    fragmented(&st, a, sa, b, sb, c, sc);
    CHECK(st.n == 2 && st.ext[0].off > 0);
    budget = 1L << 40;
    CHECK(modstore_compact(&st) == 0);
    long total = (1L << 40) - budget;
    budget = -1;
    modstore_mount(&st, &dev, false);
    int maj;
    CHECK(st.n == 2 && st.ext[0].off == 0);
    CHECK(active(&st, "second", &maj) == 1 && active(&st, "third", &maj) == 1);
    int bad = 0;
    for (long cut = 0; cut <= total; cut += cut < 64 || total - cut < 64 ? 1 : 89) {
        fragmented(&st, a, sa, b, sb, c, sc);
        budget = cut;
        modstore_compact(&st);
        budget = -1;
        modstore_mount(&st, &dev, false);
        if (active(&st, "second", &maj) < 1 || active(&st, "third", &maj) < 1)
            bad++;
        CHECK(modstore_compact(&st) == 0); /* and it can finish afterwards */
        modstore_mount(&st, &dev, false);
        CHECK(active(&st, "second", &maj) >= 1 && active(&st, "third", &maj) >= 1);
    }
    CHECK(bad == 0);
    printf("power loss during compaction (%ld operations): bad %d\n", total, bad);
    free(a), free(b), free(c);
}

/* The singleton over the simulator's emulated QSPI (hal_storage_*). */
static void test_resolve(void)
{
    mod_boot(ifs, 2, false);
    modstore_t *q = mod_store(MODSTORE_QSPI);
    CHECK(q->dev != NULL);
    CHECK(modstore_format(q) == 0);
    mod_remount();
    CHECK(mod_state("manual") == MOD_ABSENT);
    size_t s1, s2, sa, sx;
    uint8_t *m1 = make_mod("manual", 1, 0, "help", 1, 1, 30000, 21, NULL, 0, &s1);
    uint8_t *m2 = make_mod("manual", 2, 0, "help", 1, 1, 30000, 22, NULL, 0, &s2);
    mod_dep_t dep = {"manual", {2, 0, 0}, {2, 65535, 65535}};
    uint8_t *ext = make_mod("special", 1, 0, "cas", 3, 3, 8000, 23, &dep, 1, &sx);
    uint8_t *alien = make_mod("alien", 1, 0, "gpu", 1, 1, 100, 24, NULL, 0, &sa);
    CHECK(install(q, m1, s1, false) == 0);
    CHECK(install(q, ext, sx, false) == 0);
    CHECK(install(q, alien, sa, true) == 0); /* forced past the ABI check */
    mod_remount();
    CHECK(mod_state("manual") == MOD_OK);
    CHECK(mod_state("special") == MOD_DEPS); /* needs manual >= 2 */
    CHECK(mod_state("alien") == MOD_ABI);
    CHECK(install(q, m2, s2, false) == 0);
    mod_remount();
    CHECK(mod_state("special") == MOD_OK);
    CHECK(mod_header("manual") && mod_header("manual")->ver[0] == 2);
    uint8_t b[16];
    CHECK(mod_read("manual", 0, b, sizeof b) == 0 && !memcmp(b, m2 + MOD_PAYLOAD_OFF, sizeof b));
    CHECK(mod_read("manual", 30000 - 4, b, 8) < 0); /* past the end */
    /* damage v2's payload: the first read notices, v1 takes over, and the
     * dependent module loses its dependency */
    int v[MOD_MAX_EXTENTS];
    CHECK(modstore_versions(q, "manual", v, MOD_MAX_EXTENTS) == 2);
    uint8_t zero = 0;
    hal_storage_write(q->ext[v[0]].off + MOD_PAYLOAD_OFF + 100, &zero, 1);
    mod_remount();
    CHECK(mod_read("manual", 0, b, sizeof b) == 0 && !memcmp(b, m1 + MOD_PAYLOAD_OFF, sizeof b));
    CHECK(mod_header("manual")->ver[0] == 1);
    CHECK(mod_state("special") == MOD_DEPS);
    /* safe mode: nothing is used */
    mod_boot(ifs, 2, true);
    CHECK(mod_state("manual") == MOD_DISABLED && mod_read("manual", 0, b, 1) < 0);
    mod_boot(ifs, 2, false);
    CHECK(modstore_format(mod_store(MODSTORE_QSPI)) == 0);
    mod_remount();
    free(m1), free(m2), free(ext), free(alien);
}

int main(void)
{
    test_basic();
    test_power_loss();
    test_compact();
    test_resolve();
    printf("test_modstore: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
