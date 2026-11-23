/* Placement of large read-only data and cold code on the hardware build.
 *
 * Internal flash is two 512 KB banks that are not contiguous (0x08000000 and
 * 0x08100000). Bank 1 holds the vectors and the hot code; the linker script
 * (platform/stm32h743/stm32h743zg.ld, section .flash2) puts the whole CAS
 * library and anything tagged with these macros in bank 2. Both banks run at
 * the same speed. On the simulators the macros are empty.
 *
 * Use the macros for big tables that would otherwise crowd bank 1 (e.g. the
 * generated large fonts in core/gen/fonts_gen.c); untagged code and constants
 * stay in bank 1. `make release` reports how full each bank is.
 *
 *   FLASH2_RODATA const uint8_t big_table[] = {...};
 *   FLASH2_TEXT void rarely_used(void) {...}
 */
#ifndef SECTIONS_H
#define SECTIONS_H

/* DTCM_BSS: zero-initialised statics in DTCM (.dtcm_bss) instead of AXI SRAM,
 * which is nearly full (.bss + the SDRAM-failure fallback arena + heap). Not
 * for DMA buffers (the DMA controllers cannot reach DTCM).
 * NOINIT: survives a reset, never zeroed (.noinit DTCM; lost in Standby and on
 * power loss): validate it with a magic number (core/evlog.c). On the
 * simulators a plain static (zero at start, kept across app restarts). */
#if defined(PLATFORM_STM32)
#define FLASH2_RODATA __attribute__((section(".flash2.rodata")))
#define FLASH2_TEXT __attribute__((section(".flash2.text")))
#define DTCM_BSS __attribute__((section(".dtcm_bss")))
#define NOINIT __attribute__((section(".noinit")))
#else
#define FLASH2_RODATA
#define FLASH2_TEXT
#define DTCM_BSS
#define NOINIT
#endif

#endif
