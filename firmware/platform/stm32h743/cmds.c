/*
 * cmds.c — hardware-specific debug console commands (see README "Console
 * protocol").
 *
 * The portable console (core/console.c) owns line editing, parsing and the
 * OK/ERR reply protocol; it looks commands up in its own table, then the
 * app's, then hal_console_cmds[] at the end of this file. Each handler gets argc/argv
 * (argv[0] is the command name), prints with con_printf() and returns 0 or
 * the value of con_error(), which emits the ERR line.
 *
 * These commands are bring-up and diagnostic tools: they poke registers and
 * memory directly, may block for hundreds of milliseconds (busy-waits,
 * flash erases, memory tests) and some are destructive (memtest overwrites
 * the app/CAS arena; `qspi erase` and `qspi test` program flash; `rail bus
 * off force` kills the SDRAM). They run in main-loop context. Everything here
 * is also available in the bring-up image (bringup_main.c).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "console.h"
#include "hal.h"
#include "memtest.h"
#include "platform.h"
#include "rle.h" /* crc32_update: qspi stress */

extern uint8_t __sdram_arena_start[], __sdram_arena_end[];
void heartbeat_enable(bool on);

/* NULL-safe string equality for optional arguments. */
static bool argeq(const char *a, const char *b) { return a && strcmp(a, b) == 0; }

/* argv[i] exists and parses as a number (con_parse_u32: decimal or 0x hex). */
static bool arg_u32(int argc, char **argv, int i, unsigned long *v)
{
    return i < argc && con_parse_u32(argv[i], v);
}

/* ---------------------------------------------------------------- clocks */
/* Reconstruct a PLL output frequency (Hz) from the live RCC registers:
 * f = f_src / DIVMx * (DIVNx + 1) / (DIVy + 1) (RM0433 "PLL" section; the
 * fractional part FRACN is ignored). pll 0..2 = PLL1..3; *vco_out gets the
 * VCO frequency. Returns 0 if the PLL's pre-divider is 0 (PLL disabled).
 * HSI is assumed undivided (64 MHz); any source other than HSE/HSI is
 * treated as CSI (4 MHz). */
static uint32_t pll_out(int pll, int which /* 0 P, 1 Q, 2 R */, uint32_t *vco_out)
{
    uint32_t sel = RCC->PLLCKSELR;
    uint32_t src = sel & RCC_PLLCKSELR_PLLSRC;
    uint32_t in = src == RCC_PLLCKSELR_PLLSRC_HSE ? HSE_HZ : src == RCC_PLLCKSELR_PLLSRC_HSI ? 64000000u : 4000000u;
    uint32_t m = (sel >> (4 + 8 * pll)) & 0x3F;
    volatile uint32_t *divr = pll == 0 ? &RCC->PLL1DIVR : pll == 1 ? &RCC->PLL2DIVR : &RCC->PLL3DIVR;
    uint32_t d = *divr;
    if (!m) return 0;
    uint32_t vco = in / m * ((d & 0x1FF) + 1);
    if (vco_out) *vco_out = vco;
    static const uint8_t pos[] = {9, 16, 24}; /* DIVP [15:9], DIVQ [22:16], DIVR [30:24] */
    uint32_t div = ((d >> pos[which]) & 0x7F) + 1;
    return vco / div;
}

/* `clocks`: clock tree as configured (PLLs, bus clocks, VOS, flash wait
 * states), oscillator health, silicon revision and the SDRAM controller
 * registers. hclk/pclk are printed as SYSCLK/2 and /4, the dividers
 * clock_init() programs, not read back from RCC. */
static int cmd_clocks(int argc, char **argv)
{
    uint32_t vco1 = 0, vco2 = 0, vco3 = 0;
    uint32_t p1 = pll_out(0, 0, &vco1), q1 = pll_out(0, 1, NULL);
    uint32_t p2 = pll_out(1, 0, &vco2), q3 = pll_out(2, 1, &vco3);
    uint32_t sws = (RCC->CFGR & RCC_CFGR_SWS) >> RCC_CFGR_SWS_Pos;
    static const char *const swsn[] = {"HSI", "CSI", "HSE", "PLL1"};
    con_printf("sysclk_src=%s sysclk=%lu hclk=%lu pclk=%lu\n", swsn[sws & 3],
               (unsigned long)SystemCoreClock, (unsigned long)(SystemCoreClock / 2), (unsigned long)(SystemCoreClock / 4));
    uint32_t r2 = pll_out(1, 2, NULL);
    con_printf("pll1 vco=%lu p=%lu q=%lu  pll2 vco=%lu p=%lu (adc) r=%lu (fmc rev Y)  pll3 vco=%lu q=%lu (usb)\n",
               (unsigned long)vco1, (unsigned long)p1, (unsigned long)q1, (unsigned long)vco2,
               (unsigned long)p2, (unsigned long)r2, (unsigned long)vco3, (unsigned long)q3);
    con_printf("hse=%s%s lse=%s rev=%s(0x%04lx) devid=0x%03lx vos=%lu flash_ws=%lu\n", hse_ready() ? "ok" : "FAIL",
               clock_hse_failed_at_runtime() ? "(CSS)" : "", lse_ready() ? "ok" : "not-ready", silicon_rev_name(), (unsigned long)silicon_rev(),
               (unsigned long)(DBGMCU->IDCODE & 0xFFF), (unsigned long)((PWR->D3CR & PWR_D3CR_VOS) >> PWR_D3CR_VOS_Pos),
               (unsigned long)(FLASH->ACR & FLASH_ACR_LATENCY));
    uint32_t fmcsel = (RCC->D1CCIPR & RCC_D1CCIPR_FMCSEL) >> RCC_D1CCIPR_FMCSEL_Pos;
    static const char *const fmcn[] = {"hclk3", "pll1q", "pll2r", "per_ck"};
    con_printf("fmc_ker=%lu (%s) sdram=%s sdclk=%lu refresh=%lu cr1=%08lx cr2=%08lx tr1=%08lx tr2=%08lx rtr=%08lx\n",
               (unsigned long)fmc_kernel_hz(), fmcn[fmcsel & 3], sdram_ok() ? "ok" : "FAIL",
               (unsigned long)sdram_sdclk_hz(), (unsigned long)sdram_refresh_count(), (unsigned long)FMC_Bank5_6_R->SDCR[0],
               (unsigned long)FMC_Bank5_6_R->SDCR[1], (unsigned long)FMC_Bank5_6_R->SDTR[0],
               (unsigned long)FMC_Bank5_6_R->SDTR[1], (unsigned long)FMC_Bank5_6_R->SDRTR);
    con_printf("boot=%s\n", power_boot_reason());
    return 0;
}

/* `mco`: route a clock to MCO1 (PA8) to measure it with a scope/counter. */
static int cmd_mco(int argc, char **argv)
{
    unsigned long div = 4;
    if (argc < 2) return con_error("usage: mco off|hsi|lse|hse|pll1q [div 1-15]");
    if (argc > 2 && !con_parse_u32(argv[2], &div)) return con_error("bad divider");
    int src;
    if (argeq(argv[1], "off")) src = -1;
    else if (argeq(argv[1], "hsi")) src = 0;
    else if (argeq(argv[1], "lse")) src = 1;
    else if (argeq(argv[1], "hse")) src = 2;
    else if (argeq(argv[1], "pll1q")) src = 3;
    else return con_error("unknown source");
    mco1_select(src, (int)div);
    if (src >= 0) con_printf("MCO1 on PA8 (expansion header pin 7), divider %lu\n", div);
    return 0;
}

/* --------------------------------------------------------------- memory */
static void print_memtest(const char *what, uint32_t errs, const memtest_result_t *r, uint32_t ms)
{
    if (!errs)
        con_printf("%s: PASS (%lu ms)\n", what, (unsigned long)ms);
    else
        con_printf("%s: FAIL errors=%lu first_test=%s addr=0x%08lx expected=0x%08lx actual=0x%08lx\n", what,
                   (unsigned long)errs, r->failed_test ? r->failed_test : "?", (unsigned long)r->first_bad_addr,
                   (unsigned long)r->expected, (unsigned long)r->actual);
}

/* `memtest [full] [noreset]`: SDRAM test over the app/CAS arena (first 4 MB,
 * or all of it with `full`; core/memtest.c). The framebuffer at the start of
 * SDRAM is not included. The arena contents are destroyed, so by default the
 * board resets afterwards (the OK/ERR line is printed before the reset). */
static int cmd_memtest(int argc, char **argv)
{
    bool full = argc > 1 && argeq(argv[1], "full");
    bool noreset = false;
    for (int i = 1; i < argc; i++) noreset |= argeq(argv[i], "noreset");
    if (!sdram_ok()) con_printf("warning: SDRAM failed its boot check; testing anyway\n");
    volatile uint32_t *base = (volatile uint32_t *)__sdram_arena_start;
    size_t bytes = (size_t)(__sdram_arena_end - __sdram_arena_start);
    if (!full && bytes > (4u << 20)) bytes = 4u << 20;
    con_printf("testing SDRAM 0x%08lx..0x%08lx (%lu KB): destroys the app/CAS arena\n", (unsigned long)(uintptr_t)base,
               (unsigned long)((uintptr_t)base + bytes), (unsigned long)(bytes >> 10));
    memtest_result_t r;
    uint32_t t0 = g_ticks_ms;
    uint32_t errs = memtest_all(base, bytes, g_ticks_ms ^ 0x1234567u, &r);
    /* The test ran through the D-cache (the full-region passes are far larger
     * than its 16 KB, so they did reach the chip). Write back and drop the
     * arena's lines so no test data stays cached. */
    SCB_CleanInvalidateDCache();
    print_memtest("sdram", errs, &r, g_ticks_ms - t0);
    if (!noreset) {
        con_printf("resetting to restore app state (use 'noreset' to skip)\n");
        con_puts(errs ? "ERR memtest failed" : "OK");
        hal_delay_ms(50);
        hal_reset();
    }
    return errs ? con_error("memtest failed") : 0;
}

/* `rd` / `wr`: raw 32-bit memory access (address forced to word alignment,
 * at most 1024 words). No validation: an unmapped or MPU-protected address
 * faults (fault_report() records it; see `crash`). */
static int cmd_rd(int argc, char **argv)
{
    unsigned long addr, n = 1;
    if (!arg_u32(argc, argv, 1, &addr)) return con_error("usage: rd <addr> [words]");
    if (argc > 2 && !con_parse_u32(argv[2], &n)) return con_error("bad count");
    if (n > 1024) n = 1024;
    addr &= ~3ul;
    for (unsigned long i = 0; i < n; i++) {
        if (i % 4 == 0) con_printf("%08lx:", addr + i * 4);
        con_printf(" %08lx", (unsigned long)*(volatile uint32_t *)(addr + i * 4));
        if (i % 4 == 3 || i == n - 1) con_printf("\n");
    }
    return 0;
}

static int cmd_wr(int argc, char **argv)
{
    unsigned long addr, v;
    if (!arg_u32(argc, argv, 1, &addr) || !arg_u32(argc, argv, 2, &v)) return con_error("usage: wr <addr> <value>");
    *(volatile uint32_t *)(addr & ~3ul) = (uint32_t)v;
    __DSB();
    return 0;
}

/* `crash [clear]`: the register snapshot fault_report() leaves in .noinit RAM
 * (it survives the reset that follows a fault). */
static int cmd_crash(int argc, char **argv)
{
    if (argc > 1 && argeq(argv[1], "clear")) {
        memset((void *)&g_crash, 0, sizeof g_crash);
        return 0;
    }
    if (g_crash.magic != CRASH_MAGIC) {
        con_printf("no crash recorded\n");
        return 0;
    }
    con_printf("count=%lu pc=%08lx lr=%08lx psr=%08lx cfsr=%08lx hfsr=%08lx mmfar=%08lx bfar=%08lx exc_return=%08lx\n",
               (unsigned long)g_crash.count, (unsigned long)g_crash.pc, (unsigned long)g_crash.lr,
               (unsigned long)g_crash.xpsr, (unsigned long)g_crash.cfsr, (unsigned long)g_crash.hfsr,
               (unsigned long)g_crash.mmfar, (unsigned long)g_crash.bfar, (unsigned long)g_crash.exc_return);
    con_printf("r0=%08lx r1=%08lx r2=%08lx r3=%08lx r12=%08lx\n", (unsigned long)g_crash.r0,
               (unsigned long)g_crash.r1, (unsigned long)g_crash.r2, (unsigned long)g_crash.r3,
               (unsigned long)g_crash.r12);
    con_printf("hint: arm-none-eabi-addr2line -e build/fw/calc.elf 0x%08lx 0x%08lx\n", (unsigned long)g_crash.pc,
               (unsigned long)g_crash.lr);
    return 0;
}

/* ----------------------------------------------------------------- qspi */
#define QSPI_TEST_SECTOR (QSPI_SIZE - 4096u) /* last 4 KB, reserved for self-test */
static uint8_t g_qbuf[4096];

/* CRC-32 of the whole chip, 4 KB at a time: through the memory-mapped window
 * (D-cache lines dropped first, so every byte comes from the chip) or by
 * indirect quad reads. */
static int chip_crc(bool mmap, uint32_t *crc)
{
    uint32_t c = 0;
    for (uint32_t off = 0; off < QSPI_SIZE; off += sizeof g_qbuf) {
        if (!(off & 0xFFFFu)) iwdg_kick();
        if (mmap) {
            SCB_InvalidateDCache_by_Addr((void *)(QSPI_MMAP_BASE + off), (int32_t)sizeof g_qbuf);
            memcpy(g_qbuf, (const void *)(QSPI_MMAP_BASE + off), sizeof g_qbuf);
        } else if (qspi_read(off, g_qbuf, sizeof g_qbuf)) {
            return -1;
        }
        c = crc32_update(c, g_qbuf, sizeof g_qbuf);
    }
    *crc = c;
    return 0;
}

/* `qspi stress [passes]` (default 10): the check before raising the clock
 * (README.md bring-up checklist). Reference: CRC of all 16 MB read at the safe
 * 66.7 MHz with indirect reads; then at the clock selected now (`qspi clock
 * 100` first), `passes` times: the whole chip through the memory-mapped window
 * (the way modules are read) and by indirect reads, compared with the
 * reference, plus an erase / program / verify of the test sector with a new
 * pattern. Any mismatch fails. Leaves the selected clock. ~2 s per pass. */
static int qspi_stress(int argc, char **argv)
{
    unsigned long passes = 10;
    if (argc > 2 && (!con_parse_u32(argv[2], &passes) || !passes)) return con_error("usage: qspi stress [passes]");
    if (!qspi_is_ok()) return con_error("qspi not initialised (bad id?)");
    unsigned mhz = qspi_clock_hz() > 80000000u ? 100u : 66u;
    uint32_t ref, c;
    qspi_set_clock_mhz(66);
    if (chip_crc(false, &ref)) return con_error("reference read failed");
    qspi_set_clock_mhz(mhz);
    unsigned long bad = 0;
    uint32_t t0 = g_ticks_ms;
    for (unsigned long p = 0; p < passes; p++) {
        if (chip_crc(true, &c) || c != ref) {
            con_printf("pass %lu: memory-mapped read of the chip differs (crc %08lx, reference %08lx)\n", p,
                       (unsigned long)c, (unsigned long)ref);
            bad++;
        }
        if (chip_crc(false, &c) || c != ref) {
            con_printf("pass %lu: indirect read of the chip differs\n", p);
            bad++;
        }
        uint32_t seed = (g_ticks_ms + (uint32_t)p) * 2654435761u | 1u;
        for (unsigned i = 0; i < sizeof g_qbuf; i++) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            g_qbuf[i] = (uint8_t)seed;
        }
        static uint8_t back[4096];
        if (qspi_erase_sector(QSPI_TEST_SECTOR) || qspi_write(QSPI_TEST_SECTOR, g_qbuf, sizeof g_qbuf) ||
            qspi_read(QSPI_TEST_SECTOR, back, sizeof back) || memcmp(back, g_qbuf, sizeof back) ||
            memcmp((const void *)(QSPI_MMAP_BASE + QSPI_TEST_SECTOR), g_qbuf, sizeof g_qbuf)) {
            con_printf("pass %lu: test sector program/verify failed\n", p);
            bad++;
        }
        /* the test sector changed: the chip's reference changes with it */
        qspi_set_clock_mhz(66);
        if (chip_crc(false, &ref)) return con_error("reference read failed");
        qspi_set_clock_mhz(mhz);
    }
    con_printf("qspi stress %s: %lu passes at %u MHz, %lu errors, %lu ms\n", bad ? "FAIL" : "PASS", passes, mhz, bad,
               (unsigned long)(g_ticks_ms - t0));
    return bad ? con_error("qspi stress: %lu errors at %u MHz", bad, mhz) : 0;
}

/* `qspi ...`: W25Q128 diagnostics. Commands that use indirect mode return the
 * controller to memory-mapped mode afterwards (the storage layer reads
 * through the mapped window). `test` erases, programs and verifies the last
 * sector (both indirect and memory-mapped read-back) and measures the
 * mapped read throughput with the D-cache invalidated, i.e. from the chip. */
static int cmd_qspi(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "id";
    if (argeq(sub, "id")) {
        uint32_t id = qspi_read_id();
        qspi_memory_mapped();
        con_printf("jedec=%06lx (%s) expected ef4018 (Winbond W25Q128JV)\n", (unsigned long)id,
                   id == 0xEF4018 ? "match" : "MISMATCH");
        return id == 0xEF4018 ? 0 : con_error("unexpected JEDEC id");
    }
    if (argeq(sub, "status")) {
        uint8_t s1, s2, s3;
        qspi_read_status(&s1, &s2, &s3);
        qspi_memory_mapped();
        con_printf("sr1=%02x sr2=%02x (QE=%d) sr3=%02x\nforced_idle=%lu\n", s1, s2, (s2 >> 1) & 1, s3,
                   (unsigned long)qspi_forced_idle_count());
        return 0;
    }
    if (argeq(sub, "init")) return qspi_init() ? con_error("qspi init failed") : 0;
    if (argeq(sub, "dpd")) {
        qspi_deep_power_down();
        return 0;
    }
    if (argeq(sub, "wake")) {
        qspi_release_power_down();
        qspi_memory_mapped();
        return 0;
    }
    if (argeq(sub, "mmap")) return qspi_memory_mapped();
    if (argeq(sub, "read")) {
        unsigned long off, n = 64;
        if (!arg_u32(argc, argv, 2, &off)) return con_error("usage: qspi read <off> [n<=4096]");
        if (argc > 3 && !con_parse_u32(argv[3], &n)) return con_error("bad length");
        if (n > sizeof g_qbuf) n = sizeof g_qbuf;
        if (qspi_read((uint32_t)off, g_qbuf, n)) return con_error("read failed");
        for (unsigned long i = 0; i < n; i++)
            con_printf("%02x%s", g_qbuf[i], (i % 32 == 31 || i == n - 1) ? "\n" : " ");
        return 0;
    }
    if (argeq(sub, "erase")) {
        unsigned long off;
        if (!arg_u32(argc, argv, 2, &off)) return con_error("usage: qspi erase <off>");
        return qspi_erase_sector((uint32_t)off) ? con_error("erase failed") : 0;
    }
    if (argeq(sub, "test")) {
        if (!qspi_is_ok()) return con_error("qspi not initialised (bad id?)");
        uint32_t t0 = g_ticks_ms;
        if (qspi_erase_sector(QSPI_TEST_SECTOR)) return con_error("erase failed");
        uint32_t t_erase = g_ticks_ms - t0;
        if (qspi_read(QSPI_TEST_SECTOR, g_qbuf, sizeof g_qbuf)) return con_error("read failed");
        for (unsigned i = 0; i < sizeof g_qbuf; i++)
            if (g_qbuf[i] != 0xFF) return con_error("not blank after erase at +%u (%02x)", i, g_qbuf[i]);
        /* xorshift32 pseudo-random pattern, seeded from the tick count */
        uint32_t seed = g_ticks_ms * 2654435761u;
        for (unsigned i = 0; i < sizeof g_qbuf; i++) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            g_qbuf[i] = (uint8_t)seed;
        }
        t0 = g_ticks_ms;
        if (qspi_write(QSPI_TEST_SECTOR, g_qbuf, sizeof g_qbuf)) return con_error("program failed");
        uint32_t t_prog = g_ticks_ms - t0;
        static uint8_t back[4096];
        if (qspi_read(QSPI_TEST_SECTOR, back, sizeof back)) return con_error("read-back failed");
        if (memcmp(back, g_qbuf, sizeof back)) return con_error("indirect read-back mismatch");
        if (memcmp((const void *)(QSPI_MMAP_BASE + QSPI_TEST_SECTOR), g_qbuf, sizeof g_qbuf))
            return con_error("memory-mapped read mismatch");
        t0 = DWT->CYCCNT;
        volatile uint32_t sum = 0;
        SCB_InvalidateDCache_by_Addr((void *)(QSPI_MMAP_BASE + QSPI_TEST_SECTOR), 4096);
        for (unsigned i = 0; i < 1024; i++) sum += ((const volatile uint32_t *)(QSPI_MMAP_BASE + QSPI_TEST_SECTOR))[i];
        uint32_t cyc = DWT->CYCCNT - t0;
        con_printf("qspi test PASS: erase %lu ms, program 4 KB %lu ms, mmap read %lu KB/s (sector 0x%06lx)\n",
                   (unsigned long)t_erase, (unsigned long)t_prog,
                   (unsigned long)(4096ull * SystemCoreClock / (cyc ? cyc : 1) / 1024u),
                   (unsigned long)QSPI_TEST_SECTOR);
        return 0;
    }
    if (argeq(sub, "clock")) {
        unsigned long mhz;
        if (argc > 2) {
            if (!con_parse_u32(argv[2], &mhz) || qspi_set_clock_mhz((unsigned)mhz))
                return con_error("usage: qspi clock [66|100]");
        }
        con_printf("qspi clock %lu kHz (build default %s; `qspi stress` checks it)\n",
                   (unsigned long)(qspi_clock_hz() / 1000u),
#if defined(CONFIG_QSPI_100MHZ)
                   "100 MHz"
#else
                   "66.7 MHz"
#endif
        );
        return 0;
    }
    if (argeq(sub, "stress")) return qspi_stress(argc, argv);
    return con_error("usage: qspi id|status|read|erase|test|stress [passes]|clock [66|100]|mmap|dpd|wake|init");
}

/* ------------------------------------------------------------------- sd */
/* `sd info|test|power on|off`: card detect, then the SD identification
 * sequence (sd_identify: OCR, CID, RCA; card powered off again afterwards),
 * then a full open through the block driver and a read of block 0 (the MBR /
 * boot sector; 55 AA expected at offset 510). Decoded CID fields: MID =
 * CID[127:120], PNM = CID[103:64] (SD Physical Layer spec, CID register). */
static int cmd_sd(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "info";
    if (argeq(sub, "power")) {
        bool on = argc > 2 && argeq(argv[2], "on");
        sd_power(on);
        return 0;
    }
    if (argeq(sub, "info") || argeq(sub, "test")) {
        bool present = sd_card_present();
        con_printf("detect=%s (PG7=%d, active-low assumed)\n", present ? "present" : "absent",
                   present == SD_DETECT_ACTIVE_LOW ? 0 : 1);
        if (!present) return argeq(sub, "test") ? con_error("no card") : 0;
        uint32_t ocr = 0, cid[4] = {0}, rca = 0;
        int r = sd_identify(&ocr, cid, &rca);
        if (r) {
            sd_power(false);
            return con_error("identify failed (%d)", r);
        }
        con_printf("ocr=%08lx %s rca=%04lx cid=%08lx%08lx%08lx%08lx mid=%02lx pnm=%c%c%c%c%c\n",
                   (unsigned long)ocr, (ocr & 0x40000000u) ? "SDHC/SDXC" : "SDSC", (unsigned long)rca,
                   (unsigned long)cid[0], (unsigned long)cid[1], (unsigned long)cid[2], (unsigned long)cid[3],
                   (unsigned long)(cid[0] >> 24), (char)(cid[0] & 0xFF), (char)(cid[1] >> 24),
                   (char)(cid[1] >> 16), (char)(cid[1] >> 8), (char)cid[1]);
        sd_power(false);
        uint32_t blocks;
        if (hal_sd_open(&blocks) == 0) {
            uint8_t b[512];
            int rr = hal_sd_read(0, b, 1);
            con_printf("capacity=%lu blocks (%lu MB), block 0 read %s, signature %02x%02x\n", (unsigned long)blocks,
                       (unsigned long)(blocks / 2048u), rr ? "FAILED" : "ok", b[510], b[511]);
            hal_sd_close();
            if (rr) return con_error("block read failed");
        } else {
            return con_error("card init (CSD/select/4-bit) failed");
        }
        return 0;
    }
    return con_error("usage: sd info|test|power on|off");
}

/* ---------------------------------------------------------- i2c / power */
static int cmd_i2c(int argc, char **argv)
{
    /* i2c [scan] [2|4]: I2C2 = INA226 + expansion header, I2C4 = touch (3V3_BUS).
     * Probes every non-reserved 7-bit address (0x08-0x77) with an address-only
     * write. */
    bool bus4 = argc > 1 && argeq(argv[argc - 1], "4");
    if (bus4 && !bus_powered()) return con_error("I2C4 is unpowered while 3V3_BUS is off");
    int found = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c_bus_probe(bus4 ? I2C4 : I2C2, a) == 0) {
            con_printf("0x%02x%s\n", a, !bus4 && a == INA226_ADDR ? " (INA226)" : bus4 && a == TOUCH_I2C_ADDR ? " (touch, FT6236)" : "");
            found++;
        }
    }
    con_printf("%d device(s) on I2C%d\n", found, bus4 ? 4 : 2);
    return 0;
}

/* `ina226 [init|off]`: VSYS power-monitor readings, or (re)start / shut down. */
static int cmd_ina226(int argc, char **argv)
{
    if (argc > 1 && argeq(argv[1], "init")) return ina226_init() ? con_error("INA226 not responding") : 0;
    if (argc > 1 && argeq(argv[1], "off")) return ina226_shutdown() ? con_error("i2c error") : 0;
    float v, i, p, sh;
    if (!ina226_present() || ina226_read(&v, &i, &p, &sh)) return con_error("INA226 not present (try 'ina226 init')");
    con_printf("vsys=%.3f V  isys=%.2f mA  psys=%.1f mW  vshunt=%.4f mV\n", (double)v, (double)i, (double)p,
               (double)sh);
    con_printf("alert_pin=%d (PF10, low = alert) alerts=%lu current_scale=%.3f\n", gpio_read(P_PMON_ALERT),
               (unsigned long)g_pmon_alerts, (double)INA226_CURRENT_SCALE);
    return 0;
}

/* `adc`: every ADC-monitored quantity, scaled to rail volts / mA. */
static int cmd_adc(int argc, char **argv)
{
    float vdda = adc_vdda();
    con_printf("vdda(3V3)=%.3f V  temp=%.1f C\n", (double)vdda, (double)adc_temp_c());
    con_printf("3V3_BUS=%.3f V (PA6)  3V3_SD=%.3f V (PC4)  BL=%.1f mA (PC5, setpoint %.1f mA)\n",
               (double)(adc_read_volts(ADC_CH_3V3_BUS) * RAIL_DIV_RATIO),
               (double)(adc_read_volts(ADC_CH_3V3_SD) * RAIL_DIV_RATIO),
               (double)(adc_read_volts(ADC_CH_BL) / BL_SENSE_OHMS * 1000.0f), (double)backlight_get_ma());
    con_printf("vbat=%.3f V (PA3, divider switched by PA1)  vbus=%s (PA2)  warn<%.1f V off<%.1f V\n",
               (double)adc_vbat(), gpio_read(P_USB_VBUS_SENSE) ? "present" : "absent", (double)HAL_BATT_WARN_V,
               (double)HAL_BATT_EMPTY_V);
    return 0;
}

/* `rail [bus|sd on|off [force]]`: switch the 3V3_BUS / 3V3_SD load switches.
 * Turning 3V3_BUS on repeats the hal_init() bring-up of everything on it
 * (touch reset release first so its ~300 ms start-up overlaps the rest,
 * FMC banks, SDRAM, LCD, backlight calibration, touch probe). Turning it
 * off requires `force` because the framebuffer and arena live in SDRAM. */
static int cmd_rail(int argc, char **argv)
{
    if (argc < 3) {
        con_printf("3V3_BUS=%s 3V3_SD=%s\n", bus_powered() ? "on" : "off", gpio_read(P_SD_EN) ? "on" : "off");
        return 0;
    }
    bool on = argeq(argv[2], "on");
    if (argeq(argv[1], "sd")) {
        sd_power(on);
        return 0;
    }
    if (argeq(argv[1], "bus")) {
        if (!on && !(argc > 3 && argeq(argv[3], "force")))
            return con_error("turning 3V3_BUS off kills SDRAM (framebuffer, arena): add 'force'");
        bus_power(on);
        if (on) {
            touch_reset_release();
            lcd_bank_init();
            sdram_init();
            lcd_init();
            backlight_calibrate();
            hal_backlight_set(hal_backlight_get());
            touch_init();
            con_printf("sdram=%s touch=%s\n", sdram_ok() ? "ok" : "FAIL", touch_hw_present() ? "ok" : "absent");
        }
        return 0;
    }
    return con_error("usage: rail [bus|sd on|off [force]]");
}

/* ------------------------------------------------------------------ lcd */
/* `lcd ...`: ST7789 diagnostics: ID, orientation (MADCTL), re-init, TE
 * frame rate, vsync on/off, sleep/wake, full-flush timing, raw commands. */
static int cmd_lcd(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "id";
    if (argeq(sub, "id")) {
        uint32_t id = lcd_read_id();
        con_printf("rddid=%06lx madctl=%02x orientation=%d te=%d\n", (unsigned long)id, lcd_get_madctl(),
                   LCD_ORIENTATION, lcd_te_level());
        return 0;
    }
    if (argeq(sub, "madctl")) {
        unsigned long v;
        if (!arg_u32(argc, argv, 2, &v)) return con_error("usage: lcd madctl <hex>, e.g. 0x20");
        lcd_set_madctl((uint8_t)v);
        hal_display_flush(hal_framebuffer(), 0, 0, LCD_WIDTH, LCD_HEIGHT);
        return 0;
    }
    if (argeq(sub, "reinit")) {
        lcd_init();
        hal_display_flush(hal_framebuffer(), 0, 0, LCD_WIDTH, LCD_HEIGHT);
        return 0;
    }
    if (argeq(sub, "te")) {
        /* TE frame rate from the EXTI9 interrupt count over 500 ms */
        uint32_t c0 = lcd_te_count(), t0 = g_ticks_ms;
        while (g_ticks_ms - t0 < 500) {
        }
        uint32_t n = lcd_te_count() - c0;
        con_printf("te interrupts in 500 ms: %lu (~%lu Hz, expect ~60) level=%d vsync=%d\n", (unsigned long)n,
                   (unsigned long)(n * 2), lcd_te_level(), lcd_get_vsync());
        return n ? 0 : con_error("no TE edges: check PG9 / FPC pin 8 (FMARK)");
    }
    if (argeq(sub, "vsync")) {
        if (argc > 2) lcd_set_vsync(argeq(argv[2], "on"));
        con_printf("vsync=%d\n", lcd_get_vsync());
        return 0;
    }
    if (argeq(sub, "sleep")) {
        lcd_sleep(true);
        return 0;
    }
    if (argeq(sub, "wake")) {
        lcd_sleep(false);
        return 0;
    }
    if (argeq(sub, "flush")) {
        uint32_t t0 = DWT->CYCCNT;
        hal_display_flush(hal_framebuffer(), 0, 0, LCD_WIDTH, LCD_HEIGHT);
        con_printf("full flush %lu us\n", (unsigned long)((DWT->CYCCNT - t0) / (SystemCoreClock / 1000000u)));
        return 0;
    }
    if (argeq(sub, "cmd")) {
        /* raw command + up to 16 parameter bytes, e.g. `lcd cmd bb 20` (VCOMS) */
        unsigned long c, v;
        uint8_t d[16];
        int n = 0;
        if (!arg_u32(argc, argv, 2, &c) || c > 0xFF) return con_error("usage: lcd cmd <hex> [bytes...]");
        for (int i = 3; i < argc && n < (int)sizeof d; i++) {
            if (!arg_u32(argc, argv, i, &v) || v > 0xFF) return con_error("parameters are bytes");
            d[n++] = (uint8_t)v;
        }
        if (!bus_powered()) return con_error("3V3_BUS is off");
        lcd_write_cmd((uint8_t)c, d, n);
        return 0;
    }
    return con_error("usage: lcd id|madctl <hex>|reinit|te|vsync [on|off]|sleep|wake|flush|cmd <hex> [bytes]");
}

/* ------------------------------------------------------------------ misc */
/* `led on|off|blink`: debug LED; blink = 1 Hz heartbeat from SysTick. */
static int cmd_led(int argc, char **argv)
{
    if (argc < 2) return con_error("usage: led on|off|blink");
    heartbeat_enable(argeq(argv[1], "blink"));
    if (!argeq(argv[1], "blink")) gpio_write(P_LED_DEBUG, argeq(argv[1], "on"));
    return 0;
}

/* `dbgio N 0|1`: drive one of the DBG_IO0-4 test points (scope markers;
 * DBG_IO0 is also toggled by lcd_flush()). */
static int cmd_dbgio(int argc, char **argv)
{
    static const uint8_t pins[] = {P_DBG_IO0, P_DBG_IO1, P_DBG_IO2, P_DBG_IO3, P_DBG_IO4};
    unsigned long n, v;
    if (!arg_u32(argc, argv, 1, &n) || !arg_u32(argc, argv, 2, &v) || n > 4) return con_error("usage: dbgio <0-4> <0|1>");
    gpio_write(pins[n], (int)v);
    return 0;
}

/* `baud [rate]`: show or change the console bit rate. The change is deferred
 * (uart_set_baud_deferred) so this command's reply still goes out at the old
 * rate; the host switches after reading it. */
static int cmd_baud(int argc, char **argv)
{
    unsigned long b;
    if (!arg_u32(argc, argv, 1, &b) || b < 9600 || b > 6000000) {
        con_printf("baud=%lu\n", (unsigned long)uart_get_baud());
        return argc > 1 ? con_error("usage: baud <9600..6000000>") : 0;
    }
    con_printf("switching to %lu baud after this reply\n", b);
    uart_set_baud_deferred((uint32_t)b); /* applied once the OK line has been sent */
    return 0;
}

/* `kpd`: debounced matrix state (7x7 grid, row-major) and ON key. */
static int cmd_kpd(int argc, char **argv)
{
    uint64_t s = keypad_state();
    for (int r = 0; r < 7; r++) {
        con_printf("row%d:", r);
        for (int c = 0; c < 7; c++) con_printf(" %d", (int)((s >> (r * 7 + c)) & 1));
        con_printf("\n");
    }
    con_printf("on=%d ghost_events=%lu\n", (int)((s >> 49) & 1), (unsigned long)keypad_ghost_events);
    return 0;
}

/* `pwr info|stop [ms]|standby|sleep [ms]`: exercise the low-power modes.
 * stop: Stop mode with an RTC wakeup (needs LSE) or any key; standby: prints
 * OK first, since Standby only ends in a reset; sleep: WFI loop (Sleep mode). */
static int cmd_pwr(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "info";
    if (argeq(sub, "stop")) {
        unsigned long ms = 5000;
        if (argc > 2 && !con_parse_u32(argv[2], &ms)) return con_error("usage: pwr stop [ms]");
        hal_telemetry_t before;
        hal_telemetry(&before);
        con_printf("entering Stop for up to %lu ms (any key wakes); run-mode isys=%.1f mA\n", ms,
                   (double)before.isys_ma);
        int r = power_enter_stop((uint32_t)ms);
        if (r < 0) return con_error("RTC/LSE not running: cannot time the wakeup (a key still works)");
        con_printf("woke by %s; sdram %s\n", r == 1 ? "rtc" : "key/irq", sdram_ok() ? "ok" : "?");
        return 0;
    }
    if (argeq(sub, "standby")) {
        con_puts("OK");
        hal_delay_ms(20);
        power_enter_standby();
        return 0;
    }
    if (argeq(sub, "sleep")) {
        unsigned long ms = 1000;
        if (argc > 2) con_parse_u32(argv[2], &ms);
        for (uint32_t t0 = g_ticks_ms; g_ticks_ms - t0 < ms;) { /* WFI loop, the watchdog fed */
            iwdg_kick();
            __WFI();
        }
        return 0;
    }
    char wd[96];
    iwdg_status(wd, sizeof wd);
    con_printf("boot=%s\nwatchdog=%s\n", power_boot_reason(), wd);
    return 0;
}

static const char *bl_src_name(int s)
{
    return s == BL_ANODE_3V3 ? "3v3" : s == BL_ANODE_VSYS ? "vsys" : s == BL_ANODE_AUTO ? "auto" : "off";
}

/* One sweep point: set, settle, let the auto policy act (no rate limit), settle again if it switched. */
static float bl_sweep_point(float ma)
{
    backlight_set_ma(ma);
    delay_us(5000);
    float sat = 0;
    int sw = backlight_src_update(true, &sat);
    if (sw) {
        delay_us(5000);
        if (sw == BL_ANODE_VSYS)
            con_printf("  -> vsys at set=%.0f mA (3v3 gave %.1f mA)\n", (double)ma, (double)sat);
        else
            con_printf("  -> 3v3 at set=%.0f mA (< %.0f %% of the 3v3 limit %.1f mA)\n", (double)ma,
                       (double)(BL_SRC_BACK_FRAC * 100.0f), (double)backlight_src_sat_ma());
    }
    float m = backlight_measure_ma();
    con_printf("set=%.0f mA measured=%.1f mA src=%s%s\n", (double)ma, (double)m, bl_src_name(backlight_src()),
               m >= ma * 0.85f ? "" : "  <- not regulating (LED Vf + sense > anode supply)");
    return m;
}

/* `backlight`: status, calibration, anode-supply mode, or a setpoint sweep
 * (lcd.c has the current-sink and anode-selector design). */
static int cmd_backlight(int argc, char **argv)
{
    /* backlight [cal|sweep|src auto|3v3|vsys]: closed-loop check of the constant-current sink */
    const char *sub = argc > 1 ? argv[1] : "info";
    if (!bus_powered()) return con_error("3V3_BUS is off");
    if (argeq(sub, "cal")) {
        int r = backlight_calibrate();
        con_printf("gain=%.3f fault=%d (on the 3v3 anode)\n", (double)backlight_gain(), backlight_fault());
        return r ? con_error("no backlight current: panel missing, LED string open, or op-amp/FET fault") : 0;
    }
    if (argeq(sub, "src")) {
        if (argc < 3) {
            con_printf("src=%s mode=%s\n", bl_src_name(backlight_src()), bl_src_name(backlight_src_mode()));
            return 0;
        }
        int mode = argeq(argv[2], "auto") ? BL_ANODE_AUTO : argeq(argv[2], "3v3") ? BL_ANODE_3V3
                 : argeq(argv[2], "vsys") ? BL_ANODE_VSYS : -1;
        if (mode < 0) return con_error("usage: backlight src auto|3v3|vsys");
        if (backlight_src_set_mode(mode)) return con_error("switch failed");
        con_printf("src=%s mode=%s\n", bl_src_name(backlight_src()), bl_src_name(backlight_src_mode()));
        return 0;
    }
    if (argeq(sub, "sweep")) {
        /* Setpoint vs measured, up to 100 % (BL_LIMIT_MA) and back down. The panel's LEDs
         * need Vf 3.2 V typ / 3.4 V max at 100 mA plus ~0.1 V across the 1 ohm sense
         * resistor and the FET, so on the 3V3_BUS anode the current saturates somewhere
         * around 40-90 mA depending on the panel's Vf. In auto mode the sweep starts on
         * 3V3 with the learned limit forgotten, so it re-learns it and shows where the
         * anode moves to VSYS (and back, with hysteresis). A forced mode sweeps that path. */
        float keep = backlight_get_ma();
        static const float pts[] = {5, 10, 20, 30, 40, 50, 60, 70, 80, 90};
        const int n = (int)(sizeof pts / sizeof pts[0]);
        bool aut = backlight_src_mode() == BL_ANODE_AUTO;
        if (aut) {
            backlight_set_ma(0);
            backlight_src_switch(BL_ANODE_3V3);
            backlight_src_forget();
        }
        con_printf("mode=%s\n", bl_src_name(backlight_src_mode()));
        float best = 0;
        for (int i = 0; i < n && pts[i] <= BL_LIMIT_MA; i++)
            if (bl_sweep_point(pts[i]) >= pts[i] * 0.85f) best = pts[i];
        if (aut)
            for (int i = n - 2; i >= 0; i--) bl_sweep_point(pts[i]);
        backlight_set_ma(keep);
        backlight_src_update(true, 0);
        if (aut) {
            float sat = backlight_src_sat_ma();
            if (sat > 0)
                con_printf("3v3 limit %.1f mA: vsys above %.0f mA set, back to 3v3 below %.0f mA\n", (double)sat,
                           (double)(sat / BL_SRC_SAT_FRAC), (double)(sat * BL_SRC_BACK_FRAC));
            else
                con_printf("3v3 regulated the whole sweep: vsys never needed\n");
        }
        con_printf("regulates up to %.0f mA of %.0f mA (100 %%)%s\n", (double)best, (double)BL_LIMIT_MA,
                   best < BL_LIMIT_MA ? (backlight_src_mode() == BL_ANODE_3V3 ? ": normal on the 3v3 anode"
                                                                               : ": check the vsys path")
                                      : "");
        return 0;
    }
    float sat = backlight_src_sat_ma();
    con_printf("setpoint=%.1f mA measured=%.1f mA gain=%.3f fault=%d dac=%lu\n", (double)backlight_get_ma(),
               (double)backlight_measure_ma(), (double)backlight_gain(), backlight_fault(),
               (unsigned long)DAC1->DHR12R1);
    if (sat > 0)
        con_printf("src=%s mode=%s 3v3_limit=%.1f mA (vsys above %.0f mA, 3v3 below %.0f mA)\n",
                   bl_src_name(backlight_src()), bl_src_name(backlight_src_mode()), (double)sat,
                   (double)(sat / BL_SRC_SAT_FRAC), (double)(sat * BL_SRC_BACK_FRAC));
    else
        con_printf("src=%s mode=%s 3v3_limit=unknown (not saturated this boot)\n", bl_src_name(backlight_src()),
                   bl_src_name(backlight_src_mode()));
    return 0;
}

/* `stack`: main stack size, painted high-water mark, CAS recursion limit. */
static int cmd_stack(int argc, char **argv)
{
    con_printf("stack_size=%lu\nstack_used=%lu\ncas_stack_limit=%lu\n", (unsigned long)stack_size(),
               (unsigned long)stack_used(), (unsigned long)hal_cas_stack_limit());
    return 0;
}

static int cmd_reg(int argc, char **argv)
{
    /* Dump key peripheral registers useful at bring-up. */
    con_printf("RCC CR=%08lx CFGR=%08lx D1CFGR=%08lx PLLCKSELR=%08lx PLLCFGR=%08lx PLL1DIVR=%08lx BDCR=%08lx\n",
               (unsigned long)RCC->CR, (unsigned long)RCC->CFGR, (unsigned long)RCC->D1CFGR,
               (unsigned long)RCC->PLLCKSELR, (unsigned long)RCC->PLLCFGR, (unsigned long)RCC->PLL1DIVR,
               (unsigned long)RCC->BDCR);
    con_printf("PWR CR1=%08lx CR3=%08lx CSR1=%08lx D3CR=%08lx CPUCR=%08lx\n", (unsigned long)PWR->CR1,
               (unsigned long)PWR->CR3, (unsigned long)PWR->CSR1, (unsigned long)PWR->D3CR, (unsigned long)PWR->CPUCR);
    con_printf("FMC BCR1=%08lx BTR1=%08lx BWTR1=%08lx SDSR=%08lx\n", (unsigned long)FMC_Bank1_R->BTCR[0],
               (unsigned long)FMC_Bank1_R->BTCR[1], (unsigned long)FMC_Bank1E_R->BWTR[0],
               (unsigned long)FMC_Bank5_6_R->SDSR);
    con_printf("QSPI CR=%08lx DCR=%08lx CCR=%08lx SR=%08lx\n", (unsigned long)QUADSPI->CR,
               (unsigned long)QUADSPI->DCR, (unsigned long)QUADSPI->CCR, (unsigned long)QUADSPI->SR);
    return 0;
}

/* Command table: {name, argument synopsis, help text, handler}, terminated
 * by an all-zero entry. The synopsis/help strings are what `help` prints. */
/* usb: the USB device's state (usb.c) */
static int cmd_usb(int argc, char **argv)
{
    usb_status(argc, argv);
    return 0;
}

const console_cmd hal_console_cmds[] = {
    {"usb", "[start]", "USB diagnostics (as MEM > USB status); start: retry after a failed start", cmd_usb},
    {"clocks", "", "clock tree, silicon revision, SDRAM controller registers", cmd_clocks},
    {"mco", "off|hsi|lse|hse|pll1q [div]", "route a clock to PA8 (MCO1) for measurement", cmd_mco},
    {"memtest", "[quick|full] [noreset]", "SDRAM test (data/address bus, address-in-address, random)", cmd_memtest},
    {"rd", "<addr> [words]", "read 32-bit words from memory", cmd_rd},
    {"wr", "<addr> <value>", "write a 32-bit word to memory", cmd_wr},
    {"crash", "[clear]", "show the crash record kept across reset", cmd_crash},
    {"qspi", "id|status|read|erase|test|stress [N]|clock [66|100]|mmap|dpd|wake|init", "W25Q128 QSPI flash", cmd_qspi},
    {"optbytes", "[iwdg-freeze]", "option bytes: watchdog freeze in Stop/Standby (one-time board set-up)", iwdg_console},
    {"sd", "info|test|power on|off", "microSD detect + card identification", cmd_sd},
    {"i2c", "scan [2|4]", "scan I2C2 (INA226) or I2C4 (touch)", cmd_i2c},
    {"tp", "[init|raw|watch [ms]|map [0-7]|reg R [N]|sleep|wake|speed [std|fast]]", "FT6236 (FocalTech) touch controller", touch_console},
    {"backlight", "[cal|sweep|src auto|3v3|vsys]", "backlight current loop (BL_SENSE), LED anode source", cmd_backlight},
    {"stack", "", "main stack size and high-water mark", cmd_stack},
    {"ina226", "[init|off]", "power monitor readings", cmd_ina226},
    {"adc", "", "rail monitors, VDDA, temperature, battery", cmd_adc},
    {"rail", "[bus|sd on|off [force]]", "switched rails", cmd_rail},
    {"lcd", "id|madctl <hex>|reinit|te|vsync|sleep|wake|flush|cmd <hex> [bytes]", "LCD controller", cmd_lcd},
    {"led", "on|off|blink", "bring-up LED (PB8)", cmd_led},
    {"dbgio", "<0-4> <0|1>", "drive a timing-marker GPIO", cmd_dbgio},
    {"baud", "[rate]", "show/change the console baud rate", cmd_baud},
    {"kpd", "", "raw debounced key matrix state", cmd_kpd},
    {"pwr", "info|stop [ms]|standby|sleep [ms]", "low-power states", cmd_pwr},
    {"reg", "", "dump RCC/PWR/FMC/QSPI registers", cmd_reg},
    {0, 0, 0, 0},
};
