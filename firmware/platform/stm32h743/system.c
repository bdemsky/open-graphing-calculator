/*
 * Clock tree, MPU/caches, SysTick and timing for the STM32H743.
 *
 *   HSE 25 MHz -> /M=5 -> 5 MHz reference (PLLxRGE = 4..8 MHz, wide VCO)
 *   PLL1: N=160 -> VCO 800 MHz, P=2 -> SYSCLK 400 MHz, Q=8 -> 100 MHz (SDMMC kernel)
 *   PLL2: N=144 -> VCO 720 MHz, P=30 -> 24 MHz (ADC kernel), R=4 -> 180 MHz (FMC on rev Y)
 *   PLL3: N=96  -> VCO 480 MHz, Q=10 -> 48 MHz (USB)
 *   D1CPRE /1 = 400 MHz CPU, HPRE /2 = 200 MHz AXI/AHB (QSPI kernel = HCLK3)
 *   APB1..4 /2 = 100 MHz (USART1 kernel = PCLK2, I2C2 kernel = PCLK1)
 * VOS1 (max 400 MHz on rev Y and V), flash 2 wait states, WRHIGHFREQ = 2.
 *
 * FMC kernel clock (SDCLK = kernel / 2) depends on the silicon revision
 * (DBGMCU_IDCODE REV_ID): the layout timing budget has positive write-data
 * margin at 100 MHz SDCLK on rev V, but ~0 ns on rev Y (DS12110
 * td(SDCLKL_Data) 3.0 ns on rev Y). So:
 *   rev V (0x2003)          : FMC = HCLK3 200 MHz -> SDCLK 100 MHz
 *   rev Y (0x1003) / unknown: FMC = PLL2R 180 MHz -> SDCLK  90 MHz
 * Override at build time with -DSDRAM_SDCLK_MHZ=90 or =100. fmc.c derives the
 * SDRAM refresh/timing values and the LCD bank timing from fmc_kernel_hz().
 *
 * HSE: if it is not ready within HSE_TIMEOUT_US, the same frequencies are made
 * from HSI 64 MHz (M=8 -> 8 MHz reference) and the boot log says so. Once the
 * HSE runs, its clock security system (CSS) is enabled: a crystal that stops
 * later raises an NMI, which rebuilds the clock tree on HSI (clock_css_nmi()).
 *
 * Boot order (hal_init() in hal_stm32.c): power_early_init() -> clock_init()
 * -> mpu_cache_init() -> systick_init(). Everything here runs before the UART
 * is up, so nothing in this file may log; failures are recorded in flags
 * (hse_ready(), io_compensation_ready()) and reported by hal_init() afterwards.
 *
 * Also here: the main-stack high-water mark (stack_paint/stack_used), the
 * MPU region map (mpu_cache_init), the 1 kHz SysTick that drives the keypad
 * scan, and the DWT-cycle-counter busy-wait delay_us().
 */
#include "platform.h"

/* Current core clock in Hz. Starts at the HSI reset value and is updated by
 * pll_start(); delay_us() and SysTick_Config() depend on it being right. */
uint32_t SystemCoreClock = 64000000u;
/* Milliseconds since systick_init(), incremented by SysTick_Handler. Wraps
 * after ~49.7 days; compare with unsigned subtraction (now - t0). */
volatile uint32_t g_ticks_ms;
static bool g_hse_ok;
static bool g_hse_failed;   /* CSS fired: never try the HSE again until reset */
static bool g_lse_ok;
static uint32_t g_fmc_ker_hz = 200000000u;

#ifndef HSE_TIMEOUT_US
#define HSE_TIMEOUT_US 100000u /* ST HAL uses 100 ms; a good 25 MHz crystal starts in ~2 ms */
#endif
#ifndef SDRAM_SDCLK_MHZ
#define SDRAM_SDCLK_MHZ 0 /* 0 = by silicon revision (V: 100, anything else: 90) */
#endif
#define FMC_KER_PLL2R_HZ 180000000u

/* Spin until (*reg & mask) == want, at most `loops` polls. The loop count is a
 * crude bound (it is not time-calibrated: usable before any timer runs).
 * Returns true if the condition was met. */
static bool wait_flag(volatile uint32_t *reg, uint32_t mask, uint32_t want, uint32_t loops)
{
    while (loops--) {
        if ((*reg & mask) == want) return true;
    }
    return false;
}

/* Supply configuration and voltage scaling (RM0433 "Power control (PWR)").
 * Must run first after reset: until ACTVOSRDY is set the core may not raise
 * its clock, and VOS1 must be reached (VOSRDY) before SYSCLK goes to 400 MHz. */
static void power_supply_init(void)
{
    /* LDO supply (no SMPS on the H743). The supply configuration is
     * write-once after POR; this mirrors HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY). */
    PWR->CR3 = (PWR->CR3 & ~(PWR_CR3_SCUEN | PWR_CR3_LDOEN | PWR_CR3_BYPASS)) | PWR_CR3_LDOEN;
    wait_flag(&PWR->CSR1, PWR_CSR1_ACTVOSRDY, PWR_CSR1_ACTVOSRDY, 10000000u);
    /* VDD33_USB is fed from 3V3 directly: regulator off, level detector on. */
    PWR->CR3 = (PWR->CR3 & ~PWR_CR3_USBREGEN) | PWR_CR3_USB33DEN;

    /* VOS1: enough for 400 MHz on both silicon revisions (VOS0 only needed for 480).
     * D3CR.VOS encoding: 3 = VOS1 (highest of the non-overdrive scales). The
     * read-back makes sure the write has reached PWR before VOSRDY is polled. */
    PWR->D3CR = (PWR->D3CR & ~PWR_D3CR_VOS) | (3u << PWR_D3CR_VOS_Pos);
    (void)PWR->D3CR;
    wait_flag(&PWR->D3CR, PWR_D3CR_VOSRDY, PWR_D3CR_VOSRDY, 10000000u);
}

/* Wait for a flag with a time limit, using the DWT cycle counter (works before
 * SysTick runs; SystemCoreClock must be the current core clock). Enables the
 * cycle counter itself (TRCENA + the DWT software lock) because it is used
 * before systick_init(). `us` is in microseconds; the product us * MHz must
 * fit in 32 bits (~10 s at 400 MHz). Returns true if the flag matched. */
static bool wait_flag_us(volatile uint32_t *reg, uint32_t mask, uint32_t want, uint32_t us)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55u; /* unlock (Cortex-M7 DWT lock access register) */
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t start = DWT->CYCCNT;
    uint32_t n = us * (SystemCoreClock / 1000000u);
    for (;;) {
        if ((*reg & mask) == want) return true;
        if (DWT->CYCCNT - start > n) return (*reg & mask) == want;
    }
}

/* Start HSE (or fall back to HSI), the three PLLs, and switch SYSCLK to PLL1.
 *
 * Sequence (RM0433 "Reset and clock control (RCC)", PLL programming):
 *   1. try the HSE for HSE_TIMEOUT_US (skipped for good once CSS has fired);
 *   2. move SYSCLK to HSI and stop all three PLLs (DIVM/DIVN/PLLCFGR may only
 *      be written while the PLLs are off);
 *   3. program the common source + /M, the input range and dividers, start;
 *   4. raise flash wait states BEFORE the faster clock, then the bus
 *      prescalers, then switch SYSCLK to PLL1P.
 * The HSI path uses M=8 (64/8 = 8 MHz reference, PLLxRGE "8-16 MHz") and
 * N chosen so every VCO lands on the same frequency as with the HSE, so all
 * derived clocks (UART baud, SDRAM refresh, SDMMC, ADC) are unchanged.
 * Register fields hold value - 1 for N, P, Q and R. */
static void pll_start(void)
{
    /* Every caller (boot, Stop wake, CSS NMI) runs on HSI 64 MHz here. */
    SystemCoreClock = 64000000u;
    if (!g_hse_failed) {
        RCC->CR |= RCC_CR_HSEON;
        g_hse_ok = wait_flag_us(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY, HSE_TIMEOUT_US);
    } else {
        g_hse_ok = false;
    }

    /* Switch to HSI while reprogramming the PLLs (they cannot change while on). */
    RCC->CR |= RCC_CR_HSION;
    wait_flag(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY, 1000000u);
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
    wait_flag(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSI, 1000000u);
    RCC->CR &= ~(RCC_CR_PLL1ON | RCC_CR_PLL2ON | RCC_CR_PLL3ON);
    wait_flag(&RCC->CR, RCC_CR_PLL1RDY | RCC_CR_PLL2RDY | RCC_CR_PLL3RDY, 0, 1000000u);
    if (!g_hse_ok) RCC->CR &= ~RCC_CR_HSEON; /* don't leave a dead oscillator enabled */

    uint32_t m, n1, n2, n3, rge;
    uint32_t src;
    if (g_hse_ok) {
        src = RCC_PLLCKSELR_PLLSRC_HSE;
        m = 5;      /* 5 MHz */
        n1 = 160;   /* 800 MHz */
        n2 = 144;   /* 720 MHz */
        n3 = 96;    /* 480 MHz */
        rge = 2;    /* 4-8 MHz */
    } else {
        src = RCC_PLLCKSELR_PLLSRC_HSI; /* HSI 64 MHz (HSIDIV = 1 at reset) */
        m = 8;      /* 8 MHz */
        n1 = 100;   /* 800 MHz */
        n2 = 90;    /* 720 MHz */
        n3 = 60;    /* 480 MHz */
        rge = 3;    /* 8-16 MHz */
    }
    RCC->PLLCKSELR = src | (m << RCC_PLLCKSELR_DIVM1_Pos) | (m << RCC_PLLCKSELR_DIVM2_Pos) |
                     (m << RCC_PLLCKSELR_DIVM3_Pos);
    RCC->PLLCFGR = (rge << RCC_PLLCFGR_PLL1RGE_Pos) | (rge << RCC_PLLCFGR_PLL2RGE_Pos) |
                   (rge << RCC_PLLCFGR_PLL3RGE_Pos) | /* VCOSEL = 0: wide VCO 192-836/960 MHz */
                   RCC_PLLCFGR_DIVP1EN | RCC_PLLCFGR_DIVQ1EN | RCC_PLLCFGR_DIVR1EN |
                   RCC_PLLCFGR_DIVP2EN | RCC_PLLCFGR_DIVQ2EN | RCC_PLLCFGR_DIVR2EN |
                   RCC_PLLCFGR_DIVP3EN | RCC_PLLCFGR_DIVQ3EN | RCC_PLLCFGR_DIVR3EN;
    RCC->PLL1DIVR = ((n1 - 1) << RCC_PLL1DIVR_N1_Pos) | ((2 - 1) << RCC_PLL1DIVR_P1_Pos) |
                    ((8 - 1) << RCC_PLL1DIVR_Q1_Pos) | ((2 - 1) << RCC_PLL1DIVR_R1_Pos);
    /* PLL1: P 400 MHz (SYSCLK), Q 100 MHz (SDMMC kernel), R 400 MHz (unused) */
    /* PLL2: P 24 MHz (ADC), Q 180 MHz (unused), R 180 MHz (FMC kernel on rev Y) */
    RCC->PLL2DIVR = ((n2 - 1) << RCC_PLL2DIVR_N2_Pos) | ((30 - 1) << RCC_PLL2DIVR_P2_Pos) |
                    ((4 - 1) << RCC_PLL2DIVR_Q2_Pos) | ((4 - 1) << RCC_PLL2DIVR_R2_Pos);
    /* PLL3: Q 48 MHz (USB kernel); P and R 120 MHz are unused */
    RCC->PLL3DIVR = ((n3 - 1) << RCC_PLL3DIVR_N3_Pos) | ((4 - 1) << RCC_PLL3DIVR_P3_Pos) |
                    ((10 - 1) << RCC_PLL3DIVR_Q3_Pos) | ((4 - 1) << RCC_PLL3DIVR_R3_Pos);
    RCC->PLL1FRACR = 0;
    RCC->PLL2FRACR = 0;
    RCC->PLL3FRACR = 0;
    RCC->CR |= RCC_CR_PLL1ON | RCC_CR_PLL2ON | RCC_CR_PLL3ON;
    wait_flag(&RCC->CR, RCC_CR_PLL1RDY | RCC_CR_PLL2RDY | RCC_CR_PLL3RDY,
              RCC_CR_PLL1RDY | RCC_CR_PLL2RDY | RCC_CR_PLL3RDY, 4000000u);

    /* Flash wait states before raising the clock: VOS1, ACLK 200 MHz -> 2 WS. */
    FLASH->ACR = (2u << FLASH_ACR_LATENCY_Pos) | (2u << FLASH_ACR_WRHIGHFREQ_Pos);
    wait_flag(&FLASH->ACR, FLASH_ACR_LATENCY, 2u << FLASH_ACR_LATENCY_Pos, 100000u);

    /* Bus prescalers: CPU /1, AXI/AHB /2, all APB /2. Field encodings: HPRE
     * 8 (0b1000) = /2, D1PPRE/D2PPRE1/D2PPRE2/D3PPRE 4 (0b100) = /2. */
    RCC->D1CFGR = (0u << RCC_D1CFGR_D1CPRE_Pos) | (8u << RCC_D1CFGR_HPRE_Pos) |
                  (4u << RCC_D1CFGR_D1PPRE_Pos);
    RCC->D2CFGR = (4u << RCC_D2CFGR_D2PPRE1_Pos) | (4u << RCC_D2CFGR_D2PPRE2_Pos);
    RCC->D3CFGR = (4u << RCC_D3CFGR_D3PPRE_Pos);

    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL1;
    wait_flag(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL1, 1000000u);
    SystemCoreClock = SYSCLK_HZ;

    /* Clock security system on the HSE (cannot be switched off again until
     * reset; the detector pauses by itself while the HSE is stopped in Stop). */
    if (g_hse_ok) RCC->CR |= RCC_CR_CSSHSEON;
}

/* Called from the NMI handler when RCC_CIFR.HSECSSF is set: the crystal stopped
 * while in use. The hardware has already switched SYSCLK to HSI and stopped the
 * HSE; rebuild the PLLs from HSI (same frequencies) so the UART, SDRAM refresh
 * and timers keep their rates. Returns true if this was a CSS event. */
bool clock_css_nmi(void)
{
    if (!(RCC->CIFR & RCC_CIFR_HSECSSF)) return false;
    RCC->CICR = RCC_CICR_HSECSSC;
    g_hse_failed = true;
    pll_start();
    /* SysTick reload is in core cycles: reprogram it for the new SYSCLK
     * (pll_start ends at the same 400 MHz, but SysTick ran at 64 MHz meanwhile). */
    SysTick_Config(SystemCoreClock / 1000u);
    NVIC_SetPriority(SysTick_IRQn, 3);
    return true;
}
/* True once the CSS has fired since reset (console `clocks` shows hse=FAIL(CSS)). */
bool clock_hse_failed_at_runtime(void) { return g_hse_failed; }

/* FMC kernel clock by silicon revision (see the header comment). The FMC is
 * not enabled yet when this runs: FMCSEL must not change while the FMC is
 * clocked and in use. Records the chosen kernel frequency in g_fmc_ker_hz,
 * from which fmc.c computes SDRAM timing, refresh, and the LCD bank timing. */
static void fmc_clock_select(void)
{
    uint32_t mhz = SDRAM_SDCLK_MHZ;
    if (mhz != 90 && mhz != 100) mhz = silicon_rev() == 0x2003u ? 100 : 90;
    RCC->D1CCIPR &= ~RCC_D1CCIPR_FMCSEL;
    if (mhz == 100) {
        g_fmc_ker_hz = HCLK_HZ; /* FMCSEL = 0: HCLK3 */
    } else {
        RCC->D1CCIPR |= 2u << RCC_D1CCIPR_FMCSEL_Pos; /* PLL2R */
        g_fmc_ker_hz = FMC_KER_PLL2R_HZ;
    }
}
/* FMC kernel clock in Hz as selected by fmc_clock_select() (SDCLK = half of it). */
uint32_t fmc_kernel_hz(void) { return g_fmc_ker_hz; }

/* Start the 32.768 kHz LSE crystal (RTC clock, used for the Stop wakeup timer)
 * without waiting for it. The LSE lives in the backup domain, whose registers
 * (RCC_BDCR) are write-protected until PWR_CR1.DBP is set. If the LSE is
 * already running (the backup domain survives a system reset) it is left
 * alone. */
static void lse_start(void)
{
    RCC->APB4ENR |= RCC_APB4ENR_RTCAPBEN;
    PWR->CR1 |= PWR_CR1_DBP; /* backup domain write access */
    wait_flag(&PWR->CR1, PWR_CR1_DBP, PWR_CR1_DBP, 100000u);
    if (!(RCC->BDCR & RCC_BDCR_LSERDY)) {
        /* Medium-high drive (2): margin for the CL 7 pF crystal. */
        RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_LSEDRV) | (2u << RCC_BDCR_LSEDRV_Pos);
        RCC->BDCR |= RCC_BDCR_LSEON;
    }
    /* A cold crystal takes up to ~2 s: don't block boot. rtc_ensure() finishes
     * the job when the RTC is first needed. */
}

/* Select LSE for the RTC and enable it. Returns false if the LSE is not running.
 * RTCSEL = 1 is LSE. RTCSEL can only be set once per backup-domain reset, so it
 * is written only while the RTC is not yet enabled. Called lazily (power.c
 * rtc_wakeup_arm()) because the crystal may still be starting at boot. */
bool rtc_ensure(void);
bool rtc_ensure(void)
{
    g_lse_ok = lse_ready();
    if (!g_lse_ok) return false;
    if (!(RCC->BDCR & RCC_BDCR_RTCEN)) {
        RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | (1u << RCC_BDCR_RTCSEL_Pos) | RCC_BDCR_RTCEN;
    }
    return true;
}

/* I/O compensation cell: RM0433 recommends it for I/Os toggling above 50 MHz
 * (FMC SDCLK 100 MHz, QSPI 66 MHz). It needs the CSI oscillator running.
 * READY not appearing is not fatal (I/Os still work, with less slew control);
 * hal_init() logs "io-comp NOT READY". */
static bool g_cc_ready;
static void io_compensation_init(void)
{
    RCC->CR |= RCC_CR_CSION;
    wait_flag(&RCC->CR, RCC_CR_CSIRDY, RCC_CR_CSIRDY, 100000u);
    RCC->APB4ENR |= RCC_APB4ENR_SYSCFGEN;
    (void)RCC->APB4ENR;
    SYSCFG->CCCSR |= SYSCFG_CCCSR_EN;
    g_cc_ready = wait_flag(&SYSCFG->CCCSR, SYSCFG_CCCSR_READY, SYSCFG_CCCSR_READY, 1000000u);
}
bool io_compensation_ready(void) { return g_cc_ready; }

/* ---------------------------------------------------------------- stack */
/* The main stack (DTCM, see the linker script) is painted at boot so the
 * high-water mark can be read later (`stack` console command). */
extern uint32_t _sstack, _estack;
#define STACK_PAINT 0x57AC57ACu

/* Fill the unused part of the main stack with STACK_PAINT. Called from
 * Reset_Handler while the stack is nearly empty; the 64 words (256 bytes)
 * just below the current SP are skipped so the caller's own frame is not
 * overwritten. */
void stack_paint(void)
{
    uint32_t *p = &_sstack;
    uint32_t *sp = (uint32_t *)__get_MSP();
    while (p < sp - 64) *p++ = STACK_PAINT; /* leave the live frames alone */
}

/* Size of the main stack region in bytes (linker symbols _sstack.._estack). */
uint32_t stack_size(void) { return (uint32_t)((uintptr_t)&_estack - (uintptr_t)&_sstack); }

/* High-water mark in bytes: the stack grows down from _estack, so the first
 * word above _sstack that no longer holds the paint pattern marks the deepest
 * point reached since boot. (A value that happens to equal STACK_PAINT would
 * under-report by at most that word's distance; practically never matters.) */
uint32_t stack_used(void)
{
    const uint32_t *p = &_sstack;
    while (p < &_estack && *p == STACK_PAINT) p++;
    return (uint32_t)((uintptr_t)&_estack - (uintptr_t)p);
}

/* Full clock bring-up at boot: supply/VOS, PLLs and SYSCLK, peripheral kernel
 * clock muxes, D2 SRAM clocks, I/O compensation cell, LSE start. Must run
 * before any peripheral driver is initialised (they assume the frequencies in
 * the header comment). */
void clock_init(void)
{
    power_supply_init();
    pll_start();

    /* Kernel clock selection (most are reset defaults, written for clarity). */
    RCC->D1CCIPR = (RCC->D1CCIPR & ~(RCC_D1CCIPR_FMCSEL | RCC_D1CCIPR_QSPISEL | RCC_D1CCIPR_SDMMCSEL));
    /* QSPI = HCLK3, SDMMC = PLL1Q (100 MHz); FMC = HCLK3 or PLL2R by silicon rev */
    fmc_clock_select();
    RCC->D2CCIP2R = (RCC->D2CCIP2R & ~(RCC_D2CCIP2R_USART16SEL | RCC_D2CCIP2R_I2C123SEL |
                                       RCC_D2CCIP2R_USBSEL)) |
                    (2u << RCC_D2CCIP2R_USBSEL_Pos); /* USB = PLL3Q 48 MHz */
    RCC->D3CCIPR = (RCC->D3CCIPR & ~RCC_D3CCIPR_ADCSEL); /* ADC = PLL2P 24 MHz */

    /* D2 SRAMs are clocked off at reset on the H743 */
    RCC->AHB2ENR |= RCC_AHB2ENR_SRAM1EN | RCC_AHB2ENR_SRAM2EN | RCC_AHB2ENR_SRAM3EN;
    RCC->APB4ENR |= RCC_APB4ENR_SYSCFGEN;

    io_compensation_init();
    lse_start();
}

/* After Stop the RCC has switched SYSCLK back to HSI and stopped the PLLs
 * (the kernel clock muxes and prescalers are retained). Rebuild the PLL tree;
 * the caller (power_enter_stop) then reprograms SysTick. Returns 0 if the HSE
 * came back, -1 if the clocks now run from HSI. */
int clock_restore_after_stop(void)
{
    pll_start();
    return g_hse_ok ? 0 : -1;
}

bool hse_ready(void) { return g_hse_ok; }
bool lse_ready(void) { return (RCC->BDCR & RCC_BDCR_LSERDY) != 0; }

/* Route a clock to MCO1 (PA8, test pad) for measurement (console `mco`).
 * src is the RCC_CFGR.MCO1 selector (0 HSI, 1 LSE, 2 HSE, 3 PLL1Q, 4 HSI48),
 * or < 0 to switch the output off (pin back to analog). prescaler is clamped
 * to 1..15 and written to MCO1PRE, which divides by that value. */
void mco1_select(int src, int prescaler)
{
    if (src < 0) {
        gpio_set_analog(P_DBG_MCO);
        return;
    }
    if (prescaler < 1) prescaler = 1;
    if (prescaler > 15) prescaler = 15;
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_MCO1 | RCC_CFGR_MCO1PRE)) |
                ((uint32_t)src << RCC_CFGR_MCO1_Pos) | ((uint32_t)prescaler << RCC_CFGR_MCO1PRE_Pos);
    gpio_config(P_DBG_MCO, 0, PULL_NONE);
}

/* DBGMCU_IDCODE.REV_ID: 0x1001 Z, 0x1003 Y, 0x2001 X, 0x2003 V (ES0392
 * errata sheet / RM0433). Readable without a debugger attached. */
uint32_t silicon_rev(void)
{
    return (DBGMCU->IDCODE & DBGMCU_IDCODE_REV_ID) >> DBGMCU_IDCODE_REV_ID_Pos;
}

const char *silicon_rev_name(void)
{
    switch (silicon_rev()) {
    case 0x1001: return "Z";
    case 0x1003: return "Y";
    case 0x2001: return "X";
    case 0x2003: return "V";
    default: return "?";
    }
}

/* ------------------------------------------------------------------ MPU */
/* MPU regions, then enable I- and D-cache. Region attributes use the CMSIS
 * ARM_MPU_RASR(XN, AP, TEX, S, C, B, SRD, SIZE) encoding (Armv7-M ARM, "MPU
 * memory attributes"):
 *   TEX0 C0 B1 S1 = Shared Device; TEX1 C1 B1 = Normal write-back
 *   read/write-allocate; TEX0 C1 B0 = Normal write-through, no write-allocate.
 * Higher-numbered regions win where they overlap, so region 2 (framebuffer
 * write-through) overrides region 1 for its first 256 KB, and region 4 (the
 * real 16 MB of flash) overrides the no-access region 3. PRIVDEFENA keeps the
 * default memory map for everything not covered (flash, TCMs, SRAMs,
 * peripherals). The caches are enabled only after the map is in place, so no
 * line is ever allocated under the wrong attributes. */
void mpu_cache_init(void)
{
    ARM_MPU_Disable();
    /* 0: FMC bank 1 (LCD at 0x60000000) — shared Device, never speculatively read
     *    (a speculative read would pulse RDX on the panel). */
    ARM_MPU_SetRegion(ARM_MPU_RBAR(0, 0x60000000u),
                      ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 0, 1, 0, 1, 0, ARM_MPU_REGION_SIZE_256MB));
    /* 1: SDRAM 32 MB, Normal write-back write-allocate. */
    ARM_MPU_SetRegion(ARM_MPU_RBAR(1, SDRAM_BASE),
                      ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 1, 0, 1, 1, 0, ARM_MPU_REGION_SIZE_32MB));
    /* 2: first 256 KB of SDRAM (framebuffer) write-through, so a debugger
     *    reading it over SWD (which bypasses the D-cache) sees current pixels. */
    ARM_MPU_SetRegion(ARM_MPU_RBAR(2, SDRAM_BASE),
                      ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 0, 0, 1, 0, 0, ARM_MPU_REGION_SIZE_256KB));
    /* 3: whole QSPI window no-access (blocks speculative fetches outside the chip) */
    ARM_MPU_SetRegion(ARM_MPU_RBAR(3, QSPI_MMAP_BASE),
                      ARM_MPU_RASR(1, ARM_MPU_AP_NONE, 0, 0, 0, 0, 0, ARM_MPU_REGION_SIZE_256MB));
    /* 4: the 16 MB W25Q128: Normal write-through, read-only, executable */
    ARM_MPU_SetRegion(ARM_MPU_RBAR(4, QSPI_MMAP_BASE),
                      ARM_MPU_RASR(0, ARM_MPU_AP_RO, 0, 0, 1, 0, 0, ARM_MPU_REGION_SIZE_16MB));
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);

    SCB_EnableICache();
    SCB_EnableDCache();
}

/* ------------------------------------------------------------- SysTick */
/* Start the DWT cycle counter (delay_us, hal_cycles) and a 1 kHz SysTick at
 * priority 3. Must run after clock_init() so the reload matches 400 MHz. */
void systick_init(void)
{
    /* Cycle counter for delay_us / hal_cycles */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55u;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    SysTick_Config(SystemCoreClock / 1000u);
    NVIC_SetPriority(SysTick_IRQn, 3);
}

/* Debug LED blink (1 Hz): on in the bring-up image (its "alive" sign), off in
 * the calculator; the console `led blink|off` switches it. */
static bool g_heartbeat;
void heartbeat_enable(bool on);
void heartbeat_enable(bool on) { g_heartbeat = on; }

/* 1 ms tick: advance g_ticks_ms, run the keypad matrix scan every 5 ms (the
 * debounce logic in keypad.c counts in these 5 ms steps), toggle the
 * heartbeat LED every 500 ms. Runs in interrupt context; keep it short. */
void SysTick_Handler(void)
{
    uint32_t t = ++g_ticks_ms;
    if (t % 5u == 0) keypad_scan_tick();
    if (g_heartbeat && t % 500u == 0) gpio_toggle(P_LED_DEBUG);
}

/* Busy-wait `us` microseconds on the DWT cycle counter. Works with interrupts
 * disabled and independently of SysTick; the unsigned difference handles
 * counter wrap. us * MHz must fit in 32 bits: at most ~10.7 s at 400 MHz.
 * Interrupts taken during the wait only lengthen it. */
void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t n = us * (SystemCoreClock / 1000000u);
    while (DWT->CYCCNT - start < n) {
    }
}
