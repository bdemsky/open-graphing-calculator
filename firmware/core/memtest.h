/* Portable RAM tests (used for SDRAM on hardware, a heap block in the sim).
 *
 * On the board the target is the W9825G6KH 32 MB SDRAM on FMC SDRAM bank 2
 * (16-bit bus, NBL0/NBL1 byte masks; platform/stm32h743/fmc.c): the bring-up
 * image runs memtest_all over 1 MB of the arena at boot, the calculator's
 * `memtest` console command (platform/stm32h743/cmds.c) over the arena after
 * the framebuffer. All tests are destructive. Accesses go through volatile
 * pointers so the compiler keeps every read and write; the SDRAM arena is
 * write-back cached by the MPU setup, so tests that touch only a few words
 * (data bus, byte lanes, address bus) can be satisfied by the D-cache, while
 * the full-region passes (address-in-address, random) are much larger than
 * the cache and do reach the chips.
 *
 * Every test returns its number of mismatches and records the first one in
 * *r (r->errors accumulates across tests; the caller zeroes *r first unless
 * it uses memtest_all). `bytes` is the region size; it is used in 32-bit
 * words, any remainder is ignored. */
#ifndef MEMTEST_H
#define MEMTEST_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t errors;          /* total mismatches */
    uintptr_t first_bad_addr; /* address of the first mismatch (0 if none) */
    uint32_t expected, actual;/* values at the first mismatch */
    const char *failed_test;  /* name of the first failing test, NULL if all passed */
} memtest_result_t;

/* Data-bus walking 1s/0s at one address (finds stuck or shorted data lines,
 * independent of the address bus). */
int memtest_databus(volatile uint32_t *addr, memtest_result_t *r);
/* Address-bus test: power-of-two word offsets (finds stuck/shorted address lines). */
int memtest_addrbus(volatile uint32_t *base, size_t bytes, memtest_result_t *r);
/* Every word = its own address, then complement (each cell holds both 0 and 1
 * in every bit; finds aliasing and bad cells). */
int memtest_address_in_address(volatile uint32_t *base, size_t bytes, memtest_result_t *r);
/* Pseudo-random fill + verify (xorshift32 seeded by `seed`). */
int memtest_random(volatile uint32_t *base, size_t bytes, uint32_t seed, memtest_result_t *r);
/* Byte/halfword lane test (checks NBL0/NBL1 byte masks). Uses the first two
 * words at base. */
int memtest_bytelanes(volatile uint32_t *base, memtest_result_t *r);
/* Clear *r, run all of the above in order (data bus, byte lanes, address bus,
 * address-in-address, random); returns total errors. */
uint32_t memtest_all(volatile uint32_t *base, size_t bytes, uint32_t seed, memtest_result_t *r);

#endif
