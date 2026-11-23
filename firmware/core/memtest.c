/* Portable RAM tests (Michael Barr's classic data-bus / address-bus /
 * device tests, plus a pseudo-random pass and a byte-lane test). */
#include "memtest.h"
#include "hal.h" /* hal_watchdog_kick: a 32 MB pass takes ~0.3 s, a full run several seconds */

/* Feed the watchdog every 64 K words of a pass. */
#define KICK(i) do { if (!((i) & 0xFFFFu)) hal_watchdog_kick(); } while (0)

/* Count a mismatch; the first one is recorded in *r. Returns 1 so callers can
 * write `bad += fail(...)`. */
static int fail(memtest_result_t *r, const char *test, volatile void *addr, uint32_t exp, uint32_t act)
{
    if (r->errors++ == 0) {
        r->failed_test = test;
        r->first_bad_addr = (uintptr_t)addr;
        r->expected = exp;
        r->actual = act;
    }
    return 1;
}

int memtest_databus(volatile uint32_t *addr, memtest_result_t *r)
{
    int bad = 0;
    for (uint32_t pat = 1; pat; pat <<= 1) {
        *addr = pat;
        uint32_t v = *addr;
        if (v != pat) bad += fail(r, "databus-walking1", addr, pat, v);
        *addr = ~pat;
        v = *addr;
        if (v != ~pat) bad += fail(r, "databus-walking0", addr, ~pat, v);
    }
    return bad;
}

int memtest_addrbus(volatile uint32_t *base, size_t bytes, memtest_result_t *r)
{
    size_t words = bytes / 4;
    const uint32_t pat = 0xAAAAAAAAu, anti = 0x55555555u;
    int bad = 0;
    /* Only the words at offsets 0 and 2^k are used: each differs from word 0
     * in exactly one address line. Fill them all with `pat`, then write `anti`
     * to offset 0. */
    for (size_t off = 1; off < words; off <<= 1) base[off] = pat;
    base[0] = anti;
    /* stuck-high address bits: a line stuck at 1 would make a write to
     * offset 0 land on offset 2^k */
    for (size_t off = 1; off < words; off <<= 1) {
        uint32_t v = base[off];
        if (v != pat) bad += fail(r, "addrbus-stuck-high", &base[off], pat, v);
    }
    base[0] = pat;
    /* stuck-low / shorted address bits: write `anti` to one offset 2^test at
     * a time; any other probed word (including word 0) that changes shares an
     * address line with it */
    for (size_t test = 1; test < words; test <<= 1) {
        hal_watchdog_kick();
        base[test] = anti;
        uint32_t v0 = base[0];
        if (v0 != pat) bad += fail(r, "addrbus-short", &base[test], pat, v0);
        for (size_t off = 1; off < words; off <<= 1) {
            uint32_t v = base[off];
            if (off != test && v != pat) bad += fail(r, "addrbus-short", &base[off], pat, v);
        }
        base[test] = pat;
    }
    return bad;
}

/* Three passes: write each word's own (32-bit truncated) address; verify and
 * replace with its complement; verify the complement. Because every value is
 * unique within 4 GB, any aliasing between cells shows up. */
int memtest_address_in_address(volatile uint32_t *base, size_t bytes, memtest_result_t *r)
{
    size_t words = bytes / 4;
    int bad = 0;
    for (size_t i = 0; i < words; i++) {
        KICK(i);
        base[i] = (uint32_t)(uintptr_t)&base[i];
    }
    for (size_t i = 0; i < words; i++) {
        KICK(i);
        uint32_t exp = (uint32_t)(uintptr_t)&base[i], v = base[i];
        if (v != exp) bad += fail(r, "addr-in-addr", &base[i], exp, v);
        base[i] = ~exp;
    }
    for (size_t i = 0; i < words; i++) {
        KICK(i);
        uint32_t exp = ~(uint32_t)(uintptr_t)&base[i], v = base[i];
        if (v != exp) bad += fail(r, "addr-in-addr-inv", &base[i], exp, v);
    }
    return bad;
}

/* Marsaglia xorshift32 (13, 17, 5): period 2^32 - 1, never yields 0 from a
 * nonzero state, so the sequence can be regenerated for verification. */
static uint32_t xorshift(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

int memtest_random(volatile uint32_t *base, size_t bytes, uint32_t seed, memtest_result_t *r)
{
    size_t words = bytes / 4;
    int bad = 0;
    if (!seed) seed = 0x12345678u; /* xorshift state must be nonzero */
    uint32_t s = seed;
    for (size_t i = 0; i < words; i++) {
        KICK(i);
        base[i] = xorshift(&s);
    }
    s = seed;
    for (size_t i = 0; i < words; i++) {
        KICK(i);
        uint32_t exp = xorshift(&s), v = base[i];
        if (v != exp) bad += fail(r, "random", &base[i], exp, v);
    }
    return bad;
}

int memtest_bytelanes(volatile uint32_t *base, memtest_result_t *r)
{
    int bad = 0;
    volatile uint8_t *b = (volatile uint8_t *)base;
    volatile uint16_t *h = (volatile uint16_t *)base;
    /* On the 16-bit FMC bus a byte write must assert only one of NBL0/NBL1;
     * a broken mask line would corrupt the neighbouring byte. Expected values
     * assume a little-endian CPU (Cortex-M7, and the simulator hosts). */
    base[0] = 0;
    base[1] = 0;
    /* bytes 0..3 = 11 22 33 44 -> word 0x44332211 */
    for (int i = 0; i < 4; i++) b[i] = (uint8_t)(0x11 * (i + 1));
    if (base[0] != 0x44332211u) bad += fail(r, "bytelanes-8", base, 0x44332211u, base[0]);
    h[2] = 0xBEEF;
    h[3] = 0xDEAD;
    if (base[1] != 0xDEADBEEFu) bad += fail(r, "bytelanes-16", &base[1], 0xDEADBEEFu, base[1]);
    b[5] = 0x00; /* clear one high byte; its neighbours must survive */
    if (base[1] != 0xDEAD00EFu) bad += fail(r, "bytelanes-mask", &base[1], 0xDEAD00EFu, base[1]);
    return bad;
}

uint32_t memtest_all(volatile uint32_t *base, size_t bytes, uint32_t seed, memtest_result_t *r)
{
    r->errors = 0;
    r->first_bad_addr = 0;
    r->expected = r->actual = 0;
    r->failed_test = 0;
    memtest_databus(base, r);
    memtest_bytelanes(base, r);
    memtest_addrbus(base, bytes, r);
    memtest_address_in_address(base, bytes, r);
    memtest_random(base, bytes, seed, r);
    return r->errors;
}
