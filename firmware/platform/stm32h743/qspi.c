/*
 * W25Q128JV on QUADSPI bank 1 (CLK PB2, NCS PG6, IO0-3 PF8/PF9/PF7/PF6).
 * Kernel clock HCLK3 = 200 MHz, prescaler 3 -> 66.7 MHz (chip supports 133 MHz;
 * CONFIG_QSPI_100MHZ or console `qspi clock 100` -> 100 MHz, see below).
 * Reads: Fast Read Quad I/O (EBh), 1-4-4 with M7-0 = 0xFF and 4 dummy clocks.
 * Program: Page Program (02h, 1-1-1, tPP <= 3 ms), Sector Erase (20h, 4 KB, tSE <= 400 ms),
 * Block Erase (D8h, 64 KB, tBE2 <= 2 s; typ. 150 ms vs 16 x 45 ms for sectors).
 * Checked against the W25Q128JV datasheet: EBh = 8 instruction clocks, 24-bit
 * address on 4 lines, M7-0 on 4 lines (0xFF: continuous read off), 4 dummy
 * clocks; QE (SR2 bit 1) must be 1 for quad I/O; nCS high >= 50 ns between
 * commands (CSHT = 4 cycles = 60 ns); B9h/ABh deep power-down/release (3 us).
 * Memory-mapped mode is the normal state; indirect operations abort it first.
 *
 * Users: hal_storage_read/erase/write (hal_stm32.c; reads go through the
 * memory-mapped window), which back the content module store
 * (core/modstore.h, 0x00_0000-0xDF_FFFF), the saved-session log
 * (app/session.h, 0xE0_0000-0xFF_EFFF), and the console `qspi` commands
 * (cmds.c), which use the last 4 KB as a test sector. Everything is polled,
 * no DMA or interrupts.
 *
 * Indirect vs memory-mapped (RM0433 "QUADSPI memory-mapped mode", "QUADSPI
 * indirect mode"): CCR may only be written while BUSY = 0; in memory-mapped
 * mode BUSY stays set (the peripheral may hold nCS low for a prefetch), so
 * every indirect command first sets ABORT and waits for it and BUSY to clear
 * (leave_mmap), and the window is re-enabled afterwards (qspi_memory_mapped).
 * The window is backed by the flash only in memory-mapped mode (FMODE = 11);
 * RM0433 gives no meaning to window accesses while an indirect command owns
 * the peripheral (at best they stall until it is done, at worst they fault or
 * return garbage), so while an indirect operation runs nothing may execute
 * from or read the window. The MPU (system.c mpu_cache_init) already makes
 * 0x9000_0000-0x9FFF_FFFF no-access beyond the 16 MB of the chip, so
 * speculative reads cannot reach unmapped addresses. Hence:
 * these functions, the SysTick/keypad/UART interrupt handlers and everything
 * they call live in internal flash/RAM, and all users of the window
 * (module reads through hal_storage_read) run in the main loop, which is
 * busy in this code during a write. No phase-1 code or data is placed in the
 * QSPI; phase 2 XIP modules must obey the same rule (docs/QSPI_CONTENT.md).
 * After a program or erase the affected cache lines of the mapped view are
 * invalidated so later reads through the window see the new contents.
 * Register reference: RM0433 "Quad-SPI interface (QUADSPI)".
 */
#include <string.h>
#include "console.h"
#include "evlog.h"
#include "platform.h"

/* QUADSPI_CR.PRESCALER: kernel HCLK3 200 MHz / (PRESCALER + 1). The default
 * is /3 = 66.7 MHz; CONFIG_QSPI_100MHZ (Kconfig, "Hardware") selects /2 =
 * 100 MHz, to be enabled only after the board passed the bring-up checks at
 * that clock (README.md checklist: signal quality on CLK/IO0-3, then
 * `qspi clock 100` + `qspi stress`). The W25Q128JV allows 133 MHz for EBh
 * with these 4 + 2 dummy/mode clocks. */
#if defined(CONFIG_QSPI_100MHZ)
#define QSPI_PRESCALER 1u /* divide by 2: 100 MHz */
#else
#define QSPI_PRESCALER 2u /* divide by 3: 66.7 MHz */
#endif
static uint32_t g_prescaler = QSPI_PRESCALER; /* console `qspi clock` changes it until the next boot */
/* QUADSPI_CCR.FMODE: indirect write, indirect read, memory-mapped. */
#define FMODE_WRITE 0u
#define FMODE_READ  1u
#define FMODE_MMAP  3u

/* W25Q128JV instruction codes (datasheet "Instructions" tables). */
#define W25_WRITE_ENABLE 0x06
#define W25_READ_SR1 0x05
#define W25_READ_SR2 0x35
#define W25_READ_SR3 0x15
#define W25_WRITE_SR2 0x31
#define W25_JEDEC_ID 0x9F
#define W25_PAGE_PROGRAM 0x02
#define W25_SECTOR_ERASE 0x20
#define W25_BLOCK_ERASE64 0xD8
#define W25_FAST_READ_QIO 0xEB
#define W25_POWER_DOWN 0xB9
#define W25_RELEASE_PD 0xAB
#define W25_ENABLE_RESET 0x66
#define W25_RESET 0x99

/* Set by qspi_init() when the JEDEC ID matched (Winbond, 16 MB); the data
 * operations refuse to run otherwise. */
static bool g_qspi_ok;
static uint32_t g_qspi_id;
bool qspi_is_ok(void) { return g_qspi_ok; }
uint32_t qspi_boot_id(void) { return g_qspi_id; }

/* Poll QUADSPI_SR until any bit of mask is set; timeout counts polls, not
 * time. Returns 0, or -1 on timeout. */
static int wait_flag(uint32_t mask, uint32_t timeout)
{
    while (!(QUADSPI->SR & mask)) {
        if (!timeout--) return -1;
    }
    return 0;
}

/* CCR may only be written while the peripheral is not busy. In memory-mapped
 * mode it stays busy (nCS may even be held low for a prefetch), so abort the
 * current mode and wait for ABORT to self-clear and BUSY to drop. */
/* XIP guard (phase-2 code modules, docs/QSPI_CONTENT.md 7.2): MPU region 4
 * (the chip's 16 MB, set up executable by mpu_cache_init) is made
 * execute-never while the peripheral is out of memory-mapped mode and
 * executable again by qspi_memory_mapped(). Nothing runs from the window
 * during a write by construction (writes run in the main loop, modules are
 * idle: app/modx.c, session.c), so this only stops the core from fetching
 * instructions there speculatively (Cortex-M7 TRM: no speculative
 * instruction fetches from XN regions) while an indirect command owns the
 * bus. RASR of an enabled region may be rewritten in place; DSB + ISB make
 * the new attributes apply to the next fetch (Armv7-M ARM B3.5). */
static void xip_allow(bool on)
{
    MPU->RNR = 4u;
    uint32_t rasr = MPU->RASR;
    MPU->RASR = on ? rasr & ~MPU_RASR_XN_Msk : rasr | MPU_RASR_XN_Msk;
    __DSB();
    __ISB();
}

/* Times the abort did not leave the peripheral idle and it was disabled and re-enabled
 * (console `qspi status`). */
static uint32_t g_forced_idle;

static bool idle_wait(void)
{
    for (uint32_t n = 1000000u; (QUADSPI->CR & QUADSPI_CR_ABORT) && n; n--) {
    }
    for (uint32_t n = 1000000u; (QUADSPI->SR & QUADSPI_SR_BUSY) && n; n--) {
    }
    return !(QUADSPI->SR & QUADSPI_SR_BUSY) && !(QUADSPI->CR & QUADSPI_CR_ABORT);
}

/* Leave memory-mapped mode (or a stuck command) and make sure the peripheral is idle: a
 * command written to CCR while BUSY is still set is silently ignored (RM0433), which on a
 * board looked like a page program that "succeeded" but never reached the flash. With the
 * timeout counter off (qspi_init) only an abort ends a memory-mapped prefetch, so if the
 * abort does not leave BUSY clear the peripheral is disabled and enabled again, which
 * stops any transfer (logged). Returns 0, or -1 if it is still busy (the command fails
 * instead of being lost). The waits are bounded so a wedged peripheral cannot hang. */
static int leave_mmap(void)
{
    if (((QUADSPI->CCR >> QUADSPI_CCR_FMODE_Pos) & 3u) == FMODE_MMAP)
        xip_allow(false);
    if (((QUADSPI->CCR >> QUADSPI_CCR_FMODE_Pos) & 3u) != FMODE_MMAP && !(QUADSPI->SR & QUADSPI_SR_BUSY))
        return 0;
    QUADSPI->CR |= QUADSPI_CR_ABORT;
    if (idle_wait())
        return 0;
    g_forced_idle++;
    con_log("qspi: busy after abort (sr=%08lx cr=%08lx): peripheral disabled and enabled",
            (unsigned long)QUADSPI->SR, (unsigned long)QUADSPI->CR);
    evlog_put("qspi: busy after abort: forced idle (sr %08lx)", (unsigned long)QUADSPI->SR);
    QUADSPI->CR &= ~QUADSPI_CR_EN;
    for (uint32_t n = 1000000u; (QUADSPI->SR & QUADSPI_SR_BUSY) && n; n--) {
    }
    QUADSPI->CR |= QUADSPI_CR_EN;
    if (idle_wait())
        return 0;
    evlog_add("qspi: still busy (sr=%08lx)", (unsigned long)QUADSPI->SR);
    return -1;
}

uint32_t qspi_forced_idle_count(void) { return g_forced_idle; }

/* Generic indirect command. addr < 0: no address phase. quad: use the EBh layout.
 *   instr : instruction byte, always sent on one line (IMODE = 1);
 *   addr  : 24-bit address (ADSIZE = 2), on 1 line, or 4 lines when quad;
 *   fmode : FMODE_READ or FMODE_WRITE;
 *   buf/len: data phase (none if len == 0), 1 line or 4 lines when quad;
 *   quad  : 1-4-4 Fast Read Quad I/O framing: 8-bit alternate byte M7-0
 *           on 4 lines + 4 dummy cycles before the data.
 * The transfer starts on the CCR write, or on the AR write when there is an
 * address phase (an indirect write with data waits for the first DR write as
 * well). Data moves byte-wise through DR, gated by FTF (FIFO
 * threshold, FTHRES = 3 -> 4 bytes); TCF marks the end of the transfer.
 * Returns 0, -1 (FIFO timeout) or -2 (no transfer-complete). Leaves the
 * peripheral in indirect mode: callers restore memory-mapped mode. */
static int cmd(uint8_t instr, long addr, uint32_t fmode, void *buf, size_t len, bool quad)
{
    if (leave_mmap())
        return -1;
    QUADSPI->FCR = QUADSPI_FCR_CTCF | QUADSPI_FCR_CTEF;
    if (len) QUADSPI->DLR = (uint32_t)len - 1;
    uint32_t ccr = (fmode << QUADSPI_CCR_FMODE_Pos) | (1u << QUADSPI_CCR_IMODE_Pos) | instr;
    if (addr >= 0) ccr |= ((quad ? 3u : 1u) << QUADSPI_CCR_ADMODE_Pos) | (2u << QUADSPI_CCR_ADSIZE_Pos);
    if (len) ccr |= (quad ? 3u : 1u) << QUADSPI_CCR_DMODE_Pos;
    if (quad) {
        ccr |= (3u << QUADSPI_CCR_ABMODE_Pos) | (0u << QUADSPI_CCR_ABSIZE_Pos) | (4u << QUADSPI_CCR_DCYC_Pos);
        QUADSPI->ABR = 0xFF; /* M7-0: no continuous-read mode */
    }
    QUADSPI->CCR = ccr;
    if (addr >= 0) QUADSPI->AR = (uint32_t)addr;

    volatile uint8_t *dr = (volatile uint8_t *)&QUADSPI->DR;
    uint8_t *p = buf;
    for (size_t i = 0; i < len; i++) {
        if (wait_flag(QUADSPI_SR_FTF | QUADSPI_SR_TCF, 10000000u)) return -1;
        if (fmode == FMODE_READ)
            p[i] = *dr;
        else
            *dr = p[i];
    }
    if (wait_flag(QUADSPI_SR_TCF, 10000000u)) return -2;
    QUADSPI->FCR = QUADSPI_FCR_CTCF;
    return 0;
}

/* Drop stale cache lines of the memory-mapped view (region is write-through,
 * so there is never dirty data to lose). Never invalidate the whole D-cache:
 * that would discard dirty SDRAM lines. */
static void invalidate_view(uint32_t addr, size_t n)
{
    uint32_t a = (QSPI_MMAP_BASE + addr) & ~31u;
    uint32_t e = (QSPI_MMAP_BASE + addr + (uint32_t)n + 31u) & ~31u;
    SCB_InvalidateDCache_by_Addr((void *)a, (int32_t)(e - a));
}

/* Read one status register (05h SR1, 35h SR2, 15h SR3). 0xFF if the read
 * failed (looks "busy", so wait_ready() keeps polling until its timeout). */
static uint8_t read_sr(uint8_t which)
{
    uint8_t v = 0xFF;
    cmd(which, -1, FMODE_READ, &v, 1, false);
    return v;
}

/* Poll SR1.BUSY (bit 0) until the program/erase/status write finishes.
 * Needs SysTick running (g_ticks_ms). Returns 0, or -1 after timeout_ms. */
static int wait_ready(uint32_t timeout_ms)
{
    uint32_t t0 = g_ticks_ms;
    while (read_sr(W25_READ_SR1) & 1u) {
        iwdg_kick(); /* a 64 KB block erase may take up to 2 s, the watchdog's period */
        if (g_ticks_ms - t0 > timeout_ms) return -1;
    }
    return 0;
}

/* 06h Write Enable, then verify SR1.WEL (bit 1). Every program, erase and
 * non-volatile status write needs it; the chip clears WEL when they finish. */
static int write_enable(void)
{
    cmd(W25_WRITE_ENABLE, -1, FMODE_WRITE, NULL, 0, false);
    return (read_sr(W25_READ_SR1) & 2u) ? 0 : -1;
}

/* All three status registers (console `qspi status`). Leaves the peripheral
 * in indirect mode; the caller restores memory-mapped mode if it needs it. */
int qspi_read_status(uint8_t *sr1, uint8_t *sr2, uint8_t *sr3)
{
    *sr1 = read_sr(W25_READ_SR1);
    *sr2 = read_sr(W25_READ_SR2);
    *sr3 = read_sr(W25_READ_SR3);
    return 0;
}

/* 9Fh JEDEC ID: manufacturer (EFh Winbond) << 16 | memory type (40h for the
 * -IQ/-JQ parts in SPI mode) << 8 | capacity (18h = 2^24 bytes). Like
 * qspi_read_status, leaves the peripheral in indirect mode. */
uint32_t qspi_read_id(void)
{
    uint8_t id[3] = {0};
    cmd(W25_JEDEC_ID, -1, FMODE_READ, id, 3, false);
    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
}

/* ABh Release Power-down. Harmless if the chip was not powered down. */
void qspi_release_power_down(void)
{
    cmd(W25_RELEASE_PD, -1, FMODE_WRITE, NULL, 0, false);
    delay_us(30); /* tRES1 3 us */
}

/* B9h Power-down (~1 uA) before Standby. The chip then ignores everything
 * except ABh, so the memory-mapped window must not be read afterwards; the
 * next boot's qspi_init() releases it. */
void qspi_deep_power_down(void)
{
    cmd(W25_POWER_DOWN, -1, FMODE_WRITE, NULL, 0, false);
    delay_us(5); /* tDP 3 us */
}

/* Reset and configure the QUADSPI peripheral, wake and software-reset the
 * flash (66h + 99h, so a chip left mid-operation by an MCU reset starts
 * clean), check the JEDEC ID, make sure QE is set, and enter memory-mapped
 * mode. Returns 0 on success, -1 if no W25Q128 answered. IO2/IO3 (/WP and
 * /HOLD in single-line mode) get pull-ups so they stay inactive while not
 * driven. */
int qspi_init(void)
{
    RCC->AHB3ENR |= RCC_AHB3ENR_QSPIEN;
    (void)RCC->AHB3ENR;
    RCC->AHB3RSTR |= RCC_AHB3RSTR_QSPIRST;
    RCC->AHB3RSTR &= ~RCC_AHB3RSTR_QSPIRST;
    gpio_config(P_QSPI_CLK, 9, PULL_NONE);
    gpio_config(P_QSPI_NCS, 10, PULL_UP);
    gpio_config(P_QSPI_IO0, 10, PULL_NONE);
    gpio_config(P_QSPI_IO1, 10, PULL_NONE);
    gpio_config(P_QSPI_IO2, 9, PULL_UP);
    gpio_config(P_QSPI_IO3, 9, PULL_UP);

    /* No timeout counter (TCEN = 0): in memory-mapped mode nCS stays low after
     * a prefetch until the next access or abort. With TCEN + LPTR releasing nCS
     * after idle, a read that races the timeout can return wrong data (errata
     * "memory-mapped read operations may fail when timeout counter is enabled",
     * same QUADSPI block on F4/F7/L4); on the board this showed up as rare
     * crashes inside XIP module code (pc in the module, impossible register
     * values). The flash idling selected costs well under a mA while running;
     * Standby puts it in deep power-down anyway. */
    /* PRESCALER 2 -> 200/3 MHz (1 -> 100 MHz); FTHRES 3 -> FTF at 4 bytes; SSHIFT samples
     * read data half a clock later (margin for the flash's output delay). */
    QUADSPI->CR = (g_prescaler << QUADSPI_CR_PRESCALER_Pos) | (3u << QUADSPI_CR_FTHRES_Pos) |
                  QUADSPI_CR_SSHIFT;
    /* FSIZE 24 (32 MB) for the 16 MB chip: with the exact size (23), a read
     * reaching the chip's last byte fails (ES0392 erratum "memory-mapped read
     * of last memory byte fails"; on rev A board 1 an indirect 4 KB read of the
     * last sector set TEF at byte 0 after a program). MPU region 3 keeps the
     * core out of the upper 16 MB. CS high >= 4 cycles (60 ns > tSHSL 50 ns). */
    QUADSPI->DCR = (24u << QUADSPI_DCR_FSIZE_Pos) | (3u << QUADSPI_DCR_CSHT_Pos);
    QUADSPI->CR |= QUADSPI_CR_EN;

    qspi_release_power_down(); /* in case we come back from Standby */
    cmd(W25_ENABLE_RESET, -1, FMODE_WRITE, NULL, 0, false);
    cmd(W25_RESET, -1, FMODE_WRITE, NULL, 0, false);
    delay_us(50); /* tRST 30 us */

    uint32_t id = qspi_read_id();
    g_qspi_id = id;
    g_qspi_ok = (id >> 16) == 0xEF && (id & 0xFF) == 0x18;
    if (!g_qspi_ok) return -1;

    /* Quad Enable (SR2 bit 1). Factory-set on -IQ parts; set it if not. */
    uint8_t sr2 = read_sr(W25_READ_SR2);
    if (!(sr2 & 2u)) {
        write_enable(); /* non-volatile write of SR2 */
        uint8_t v = sr2 | 2u;
        cmd(W25_WRITE_SR2, -1, FMODE_WRITE, &v, 1, false);
        wait_ready(50);
    }
    return qspi_memory_mapped();
}

/* QSPI clock in Hz, and a change of it (console `qspi clock 66|100`, for the
 * bring-up measurement; the build option sets the boot value). The prescaler
 * may only change while the peripheral is idle, so memory-mapped mode is left
 * first and re-entered afterwards. */
uint32_t qspi_clock_hz(void) { return 200000000u / (g_prescaler + 1u); }

int qspi_set_clock_mhz(unsigned mhz)
{
    uint32_t pre = mhz == 100 ? 1u : mhz == 66 || mhz == 67 ? 2u : 0xFFu;
    if (pre == 0xFFu) return -1;
    (void)leave_mmap(); /* (a failure is logged; the next command reports it) */
    QUADSPI->CR &= ~QUADSPI_CR_EN;
    QUADSPI->CR = (QUADSPI->CR & ~QUADSPI_CR_PRESCALER) | (pre << QUADSPI_CR_PRESCALER_Pos);
    QUADSPI->CR |= QUADSPI_CR_EN;
    g_prescaler = pre;
    SCB_InvalidateDCache_by_Addr((void *)QSPI_MMAP_BASE, (int32_t)QSPI_SIZE); /* nothing dirty: write-through */
    return qspi_memory_mapped();
}

/* Switch to memory-mapped mode: every read of 0x9000_0000 + a becomes an EBh
 * Fast Read Quad I/O of address a with the same framing as cmd(quad). The
 * peripheral prefetches and holds nCS low until the next access (no timeout
 * counter, see qspi_init). */
int qspi_memory_mapped(void)
{
    (void)leave_mmap(); /* (a failure is logged; the next command reports it) */
    QUADSPI->FCR = QUADSPI_FCR_CTCF;
    QUADSPI->ABR = 0xFF;
    QUADSPI->CCR = (FMODE_MMAP << QUADSPI_CCR_FMODE_Pos) | (3u << QUADSPI_CCR_DMODE_Pos) |
                   (4u << QUADSPI_CCR_DCYC_Pos) | (3u << QUADSPI_CCR_ABMODE_Pos) |
                   (0u << QUADSPI_CCR_ABSIZE_Pos) | (2u << QUADSPI_CCR_ADSIZE_Pos) |
                   (3u << QUADSPI_CCR_ADMODE_Pos) | (1u << QUADSPI_CCR_IMODE_Pos) | W25_FAST_READ_QIO;
    xip_allow(true);
    return 0;
}

/* Indirect quad read of n bytes from flash address addr into buf, then back
 * to memory-mapped mode. Bypasses the D-cache (useful to check what is
 * really in the chip). Returns 0, -1 if the chip is absent, or a cmd() error. */
int qspi_read(uint32_t addr, void *buf, size_t n)
{
    if (!g_qspi_ok) return -1;
    int r = 0;
    uint8_t *p = buf;
    while (n && !r) { /* DLR is 32-bit but keep chunks modest */
        size_t chunk = n > 65536 ? 65536 : n;
        r = cmd(W25_FAST_READ_QIO, (long)addr, FMODE_READ, p, chunk, true);
        addr += chunk;
        p += chunk;
        n -= chunk;
    }
    qspi_memory_mapped();
    return r;
}

/* Program n bytes at addr (the target must be erased: programming only
 * clears bits). Split at 256-byte page boundaries, because a Page Program
 * that crosses one wraps around inside the page. Each page: WREN, 02h,
 * wait for BUSY to clear (tPP max 3 ms; 10 ms timeout). Returns 0, -1 if
 * the chip is absent, -2 if WEL did not set, or a cmd()/timeout error. */
int qspi_write(uint32_t addr, const void *buf, size_t n)
{
    if (!g_qspi_ok) return -1;
    const uint8_t *p = buf;
    int r = 0;
    uint32_t start = addr;
    size_t total = n;
    while (n && !r) {
        size_t room = 256u - (addr & 255u);
        size_t chunk = n < room ? n : room;
        if (write_enable()) {
            r = -2;
            break;
        }
        r = cmd(W25_PAGE_PROGRAM, (long)addr, FMODE_WRITE, (void *)(uintptr_t)p, chunk, false);
        if (!r) r = wait_ready(10);
        addr += chunk;
        p += chunk;
        n -= chunk;
    }
    qspi_memory_mapped();
    invalidate_view(start, total);
    return r;
}

/* Erase the 64 KB block at addr (64 KB aligned) to 0xFF (D8h). Blocking, up
 * to 2 s (tBE2 max); the 2.5 s timeout covers it. */
int qspi_erase_block64(uint32_t addr)
{
    if (!g_qspi_ok || (addr & 0xFFFFu)) return -1;
    int r = write_enable();
    if (!r) r = cmd(W25_BLOCK_ERASE64, (long)addr, FMODE_WRITE, NULL, 0, false);
    if (!r) r = wait_ready(2500);
    qspi_memory_mapped();
    invalidate_view(addr, 65536);
    return r;
}

/* Erase the 4 KB sector containing addr to 0xFF (20h). Blocking, up to
 * 400 ms (tSE max); the 500 ms timeout covers it. */
int qspi_erase_sector(uint32_t addr)
{
    if (!g_qspi_ok) return -1;
    int r = write_enable();
    if (!r) r = cmd(W25_SECTOR_ERASE, (long)(addr & ~0xFFFu), FMODE_WRITE, NULL, 0, false);
    if (!r) r = wait_ready(500); /* tSE max 400 ms */
    qspi_memory_mapped();
    invalidate_view(addr & ~0xFFFu, 4096);
    return r;
}
