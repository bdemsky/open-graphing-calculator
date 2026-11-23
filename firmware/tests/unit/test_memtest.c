/* Host test for core/memtest.c: a good block passes, injected faults are found.
 *
 * core/memtest.c is the RAM test run at boot over the external SDRAM (data bus,
 * address bus, address-in-address, pseudo-random fill and byte-lane tests).
 * Here it runs on an ordinary 1 MB malloc() block of host memory, which is
 * assumed to be fault-free: every test must report 0 errors. A real fault is
 * then simulated with the host's virtual memory: one 512 KB file mapped twice,
 * back to back, is a 1 MB block whose top address line is not connected (the
 * second half aliases the first), which the address tests must report.
 *
 * Built and run by `make test` (unit-test target in the Makefile: every
 * tests/unit/test_*.c is linked with the simulator library and run; a nonzero
 * exit status fails the build). CHECK() counts failures instead of aborting so
 * one run reports every broken property. */
#define _DEFAULT_SOURCE /* mmap, ftruncate with -std=c11 on glibc */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "memtest.h"

static int failures;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
            failures++;                                   \
        }                                                 \
    } while (0)

/* A block of `bytes` whose second half is the same memory as its first half
 * (an open top address line): a temporary file of bytes/2 mapped twice at
 * consecutive addresses. NULL if the host cannot do it. */
static uint32_t *aliased_block(size_t bytes)
{
    size_t half = bytes / 2;
    FILE *f = tmpfile();
    if (!f || ftruncate(fileno(f), (off_t)half))
        return NULL;
    uint8_t *base = mmap(NULL, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0); /* reserve the range */
    if (base == MAP_FAILED)
        return NULL;
    for (int k = 0; k < 2; k++)
        if (mmap(base + k * half, half, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fileno(f), 0) == MAP_FAILED)
            return NULL;
    return (uint32_t *)base; /* the file stays open (and mapped) until the process ends */
}

/* Runs every sub-test on known-good host memory, then checks that a fill
 * pattern changed in one bit is seen as exactly one bad word, and that the
 * address tests find an aliased address line. */
int main(void)
{
    size_t bytes = 1u << 20;
    uint32_t *mem = malloc(bytes);
    if (!mem) {
        printf("test_memtest: malloc failed\n");
        return 1;
    }
    /* memtest_all(): every test in sequence, seed 42 for the random pass */
    memtest_result_t r;
    uint32_t errs = memtest_all(mem, bytes, 42, &r);
    CHECK(errs == 0, "good memory reported %u errors (%s)", (unsigned)errs, r.failed_test ? r.failed_test : "");
    CHECK(r.failed_test == NULL, "failed_test set on success");

    /* Each sub-test individually */
    memset(&r, 0, sizeof r);
    CHECK(memtest_databus(mem, &r) == 0, "databus");
    CHECK(memtest_addrbus(mem, bytes, &r) == 0, "addrbus");
    CHECK(memtest_address_in_address(mem, bytes, &r) == 0, "addr-in-addr");
    CHECK(memtest_random(mem, bytes, 7, &r) == 0, "random");
    CHECK(memtest_bytelanes(mem, &r) == 0, "bytelanes");

    /* A bit flipped after the fill: fill with memtest_random(seed 99), flip
     * bit 4 of word 1234, then regenerate the same xorshift32 sequence here
     * (13/17/5 shifts, seeded with 99 and stepped before each word, exactly as
     * xorshift() in core/memtest.c) and count the mismatches. This checks that
     * the fill pattern is reproducible and that a single-bit error shows up as
     * exactly one bad word (memtest's own verify runs inside memtest_random,
     * before the bit can be flipped). */
    memset(&r, 0, sizeof r);
    memtest_random(mem, bytes, 99, &r);
    mem[1234] ^= 0x10; /* corrupt after fill: re-verify must catch it */
    uint32_t s = 99, bad = 0;
    for (size_t i = 0; i < bytes / 4; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        if (mem[i] != s) bad++;
    }
    CHECK(bad == 1, "expected exactly one corrupted word, got %u", (unsigned)bad);

    /* An open top address line: writes to the second half land in the first */
    uint32_t *al = aliased_block(bytes);
    CHECK(al != NULL, "could not map an aliased block");
    if (al) {
        al[0] = 1;
        CHECK(al[bytes / 8] == 1, "the block is not aliased");
        memset(&r, 0, sizeof r);
        CHECK(memtest_addrbus(al, bytes, &r) > 0, "addrbus missed the aliased line");
        memset(&r, 0, sizeof r);
        CHECK(memtest_address_in_address(al, bytes, &r) > 0, "addr-in-addr missed the aliased line");
        memset(&r, 0, sizeof r);
        CHECK(memtest_all(al, bytes, 42, &r) > 0 && r.failed_test != NULL, "memtest_all passed aliased memory");
        CHECK(memtest_databus(al, &r) == 0, "the data bus itself is fine");
    }

    free(mem);
    printf("test_memtest: %s (%d failures)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
