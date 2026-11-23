/*
 * FMC: SDRAM (W9825G6KH-6 on bank 2) and the LCD (ST7789 8080-16 on NOR/SRAM bank 1).
 * Both hang off the switched 3V3_BUS rail and share D0-D15.
 *
 * SDCLK = FMC kernel / 2: 100 MHz (10 ns) on silicon rev V, 90 MHz (11.1 ns) on
 * rev Y or an unknown revision (system.c, fmc_kernel_hz(): the routed board has
 * ~0 ns write-data setup margin at 100 MHz on rev Y). The SDRAM timing fields
 * are computed from the actual SDCLK below. W9825G6KH-6 values from the Winbond
 * datasheet rev A04 (2017-03-20), table 9.5 (-6 column; the -6I/-6J/-6L grades
 * need tRCD/tRP 18 ns, which the computed values also meet):
 *   tRC 60 ns, tRAS 42 ns, tRP 15 ns (18), tRCD 15 ns (18), tRRD 2 tCK (FMC: implied),
 *   tXSR 72 ns, tMRD/tRSC 2 tCK, tWR 2 tCK, raised to satisfy the FMC rules
 *   TWR >= TRAS - TRCD and TWR >= TRC - TRCD - TRP.
 *     100 MHz: TRC 6, TRAS 5, TRP 2, TRCD 2, TXSR 8, TWR 3, TMRD 2
 *      90 MHz: TRC 6, TRAS 4, TRP 2, TRCD 2, TXSR 7, TWR 2, TMRD 2
 *   CAS latency 3 (CL2 is also legal at <= 133 MHz for -6: tCK(CL2) >= 7.5 ns;
 *   the board's timing budget was done for CL3).
 *   Refresh: 8192 rows / 64 ms = 7.8125 us x SDCLK - 20 margin
 *     -> 761 at 100 MHz, 683 at 90 MHz.
 *   Power-up: 200 us of NOP with CKE high after the clock starts, PALL, 8 auto
 *   refresh, then MRS (the datasheet allows the refreshes before or after MRS).
 * FMC pins use GPIO speed "high" (OSPEEDR = 2), not "very high": the layout
 * validator's timing budget assumes it, and it means less ringing and EMI on
 * the 13-73 mm unmatched traces.
 *
 * Also here: the 3V3_BUS load switch (SY6280, BUS_EN on PG3), because the
 * FMC pins must be released before that rail drops and configured only after
 * it is up. Boot order (hal_init): bus_power(true) -> lcd_bank_init() ->
 * sdram_init() -> lcd_init() (lcd.c). Register names follow RM0433 "Flexible
 * memory controller (FMC)": BTCR[0]/[1] = BCR1/BTR1, BWTR[0] = BWTR1,
 * SDCR[0..1]/SDTR[0..1] = SDRAM bank 1/2 control/timing, SDCMR, SDRTR, SDSR.
 */
#include "platform.h"

#ifndef SDRAM_CAS_LATENCY
#define SDRAM_CAS_LATENCY 3
#endif
#ifndef SDRAM_RPIPE
#define SDRAM_RPIPE 0 /* extra HCLK read pipe delay; try 1 if reads fail at 100 MHz */
#endif

#define FMC_GPIO_SPEED 2 /* OSPEEDR "high" */
/* 3V3_BUS minimum off time. The FT6236 touch controller needs VDD < 0.3 V for
 * >= 5 ms before it is powered again (FT6x36 DS, power cycle requirement); the rail (33 uF + loads, 100k pull-down on BUS_EN only)
 * takes ~12-17 ms to fall below 0.3 V after the SY6280 opens. */
#define BUS_MIN_OFF_MS 20u
/* FMC_SDCMR.MODE command codes (RM0433 SDRAM command mode register). */
#define SDCMR_NORMAL     0u
#define SDCMR_CLK_ENABLE 1u
#define SDCMR_PALL       2u
#define SDCMR_AUTOREFRESH 3u
#define SDCMR_LOAD_MODE  4u
#define SDCMR_SELF_REFRESH 5u

/* Every FMC pin (all AF12): the shared D0-D15, the SDRAM address/control
 * lines, and the LCD's NE1/NOE/NWE/A16 (CS/RD/WR/DC). All of them connect to
 * 3V3_BUS devices, so all are switched together with the rail. */
static const uint8_t g_fmc_pins[] = {
    P_FMC_D0, P_FMC_D1, P_FMC_D2, P_FMC_D3, P_FMC_D4, P_FMC_D5, P_FMC_D6, P_FMC_D7,
    P_FMC_D8, P_FMC_D9, P_FMC_D10, P_FMC_D11, P_FMC_D12, P_FMC_D13, P_FMC_D14, P_FMC_D15,
    P_FMC_A0, P_FMC_A1, P_FMC_A2, P_FMC_A3, P_FMC_A4, P_FMC_A5, P_FMC_A6, P_FMC_A7,
    P_FMC_A8, P_FMC_A9, P_FMC_A10, P_FMC_A11, P_FMC_A12, P_FMC_BA0, P_FMC_BA1,
    P_FMC_NBL0, P_FMC_NBL1, P_FMC_SDCLK, P_FMC_SDNCAS, P_FMC_SDNRAS, P_FMC_SDNWE,
    P_FMC_SDCKE1, P_FMC_SDNE1, P_LCD_CS, P_LCD_RD, P_LCD_WR, P_LCD_DC,
};

static bool g_bus_on;
static bool g_sdram_ok;
/* When BUS_EN last went low (g_ticks_ms). 0 = at reset: a software/watchdog/
 * pin reset may have dropped a powered rail a few ms ago (BUS_EN floats to its
 * 100k pull-down at reset), so the first power-up after boot also waits out
 * BUS_MIN_OFF_MS, unless the boot is a wake from Standby (rail off for >= 20 ms
 * before Standby was entered, see power_enter_standby). */
static uint32_t g_bus_off_ms;
/* True once the rail is known to have been off for >= BUS_MIN_OFF_MS, so the
 * next power-up need not wait. */
static bool g_bus_cold;

bool bus_powered(void) { return g_bus_on; }
/* Result of the last sdram_init() quick check; false while the rail is off. */
bool sdram_ok(void) { return g_sdram_ok; }
/* Boot after a Standby wake (power_early_init): the rail has already been off
 * long enough, skip the off-time wait on the first power-up. */
void bus_power_assume_cold(void)
{
    if (!g_bus_on) g_bus_cold = true;
}

/* Blocks until 3V3_BUS has been off for BUS_MIN_OFF_MS. Returns at once if
 * the rail is on (nothing to wait for) or already known to be cold. At boot
 * g_bus_off_ms is 0, so the wait is measured from reset. */
void bus_power_wait_off_time(void)
{
    if (g_bus_on || g_bus_cold) return;
    uint32_t off = g_ticks_ms - g_bus_off_ms;
    if (off < BUS_MIN_OFF_MS) delay_us((BUS_MIN_OFF_MS - off + 1u) * 1000u); /* +1: tick granularity */
    g_bus_cold = true;
}

/* 3V3_BUS feeds the SDRAM, the LCD logic + backlight driver and the touch
 * controller (with its I2C4/INT/RST pull-ups). Power-up: at least
 * BUS_MIN_OFF_MS after the last power-down (touch controller POR), reset lines
 * low first (LCD /RES, TP_RST: the FT6236 wants reset low before power-on and
 * for >= 1 ms after it, Tprt), rail on,
 * then FMC pins. Power-down: every MCU pin that connects to a 3V3_BUS device is
 * made analog (or driven low) BEFORE the rail drops, or the MCU back-powers
 * the devices through their input clamps.
 * Idempotent (returns at once if already in the requested state). Blocking:
 * up to ~25 ms on power-up (off-time wait + 5.2 ms of settling). Turning the
 * rail off loses the SDRAM contents and the LCD state; callers re-run
 * lcd_bank_init/sdram_init/lcd_init after power-up. */
void bus_power(bool on)
{
    if (on) {
        if (g_bus_on) return;
        bus_power_wait_off_time();
        gpio_write(P_LCD_RESET, 0);
        gpio_config(P_LCD_RESET, PIN_OUT, PULL_NONE);
        touch_reset_assert();
        backlight_src_bus_on(); /* PD6 driven low, then PC13 high: LED anode from 3V3_BUS */
        delay_us(200);
        gpio_write(P_BUS_EN, 1);
        /* SY6280 turn-on (TON) is ~120 us typ; the 0.52 A limit (Rset 13k)
         * charges the rail's ~33 uF in ~0.2 ms. 5 ms leaves a wide margin
         * before the SDRAM clock starts. */
        delay_us(5000);
        RCC->AHB3ENR |= RCC_AHB3ENR_FMCEN;
        (void)RCC->AHB3ENR; /* read back: the clock is on before the FMC is touched */
        for (unsigned i = 0; i < sizeof g_fmc_pins; i++) {
            gpio_config(g_fmc_pins[i], 12, PULL_NONE);
            gpio_set_speed(g_fmc_pins[i], FMC_GPIO_SPEED);
        }
        gpio_config(P_LCD_TE, PIN_IN, PULL_NONE);
        g_bus_on = true;
        g_bus_cold = false;
    } else {
        if (!g_bus_on) return;
        lcd_pins_bus_off();   /* TE EXTI off, PG9 analog */
        if (RCC->APB1LENR & RCC_APB1LENR_DAC12EN)
            backlight_set_ma(0); /* DAC -> 10k -> op-amp input on 3V3_BUS: no back-powering */
        backlight_src_bus_off(); /* VSYS -> LCD_A off (1 ms), selector pins analog = both off */
        touch_pins_bus_off(); /* I2C4 + INT analog, TP_RST low */
        for (unsigned i = 0; i < sizeof g_fmc_pins; i++) gpio_set_analog(g_fmc_pins[i]);
        gpio_write(P_LCD_RESET, 0); /* low (100k pull-down on the board too) */
        gpio_write(P_BUS_EN, 0);
        g_bus_off_ms = g_ticks_ms;
        g_bus_on = false;
        g_bus_cold = false;
        g_sdram_ok = false;
    }
}

/* ---------------------------------------------------------- SDRAM timing */
/* SDCLK in Hz: SDCR1.SDCLK = 2 (below) divides the FMC kernel clock by 2. */
uint32_t sdram_sdclk_hz(void) { return fmc_kernel_hz() / 2u; }

/* ns -> SDCLK cycles, rounded up, at least min. Uses whole MHz (90 or 100),
 * which is exact for both supported clocks. */
static uint32_t cyc(uint32_t ns, uint32_t min)
{
    uint32_t mhz = sdram_sdclk_hz() / 1000000u;
    uint32_t c = (ns * mhz + 999u) / 1000u;
    return c < min ? min : c;
}

/* Value for FMC_SDRTR.COUNT, in SDCLK cycles. RM0433: refresh rate =
 * (SDRAM refresh period / rows) x SDCLK - 20, the 20 cycles covering the
 * latency of a refresh request that arrives during a read/write burst. */
uint32_t sdram_refresh_count(void)
{
    /* 64 ms / 8192 rows = 7.8125 us per row, minus 20 cycles of margin.
     * 64-bit intermediate: SDCLK (1e8) x 78125 overflows 32 bits. */
    return (uint32_t)((uint64_t)sdram_sdclk_hz() * 78125u / 10000000000ull) - 20u;
}

/* Issue one SDRAM command to bank 2 only (CTB2: the chip is on SDNE1/SDCKE1).
 * nrfs = number of auto-refresh cycles (field holds n - 1), mrd = mode
 * register value for LOAD_MODE. Always returns 0 (no completion status). */
static int sdram_cmd(uint32_t mode, uint32_t nrfs, uint32_t mrd)
{
    FMC_Bank5_6_R->SDCMR = (mode << FMC_SDCMR_MODE_Pos) | FMC_SDCMR_CTB2 |
                           ((nrfs ? nrfs - 1 : 0) << FMC_SDCMR_NRFS_Pos) | (mrd << FMC_SDCMR_MRD_Pos);
    delay_us(2); /* the H7 FMC has no busy flag; commands take a few SDCLKs */
    return 0;
}

/* Program the SDRAM controller for the W9825G6KH, run the JEDEC power-up
 * sequence, start auto-refresh, and do a two-word write/readback check at the
 * first and last word. Returns 0 if the check passed (also in sdram_ok()),
 * -1 otherwise; the caller then falls back to the AXI SRAM arena. The check
 * restores both words, so it can be re-run without corrupting live data. */
int sdram_init(void)
{
    if (!g_bus_on) bus_power(true);

    /* SDCR1 carries SDCLK/RBURST/RPIPE for both banks; SDCR2 the bank-2 geometry.
     * SDCLK = 2: SDCLK period = 2 x kernel clock. RBURST: burst read mode (the
     * controller anticipates the following reads during the CAS latency and
     * buffers them in its read FIFO). The geometry fields
     * are written to SDCR1 too (bank 1 is unused; the values are harmless). */
    FMC_Bank5_6_R->SDCR[0] = (2u << FMC_SDCRx_SDCLK_Pos) | FMC_SDCRx_RBURST |
                             ((uint32_t)SDRAM_RPIPE << FMC_SDCRx_RPIPE_Pos) |
                             (1u << FMC_SDCRx_NC_Pos) | (2u << FMC_SDCRx_NR_Pos) | (1u << FMC_SDCRx_MWID_Pos) |
                             FMC_SDCRx_NB | ((uint32_t)SDRAM_CAS_LATENCY << FMC_SDCRx_CAS_Pos);
    FMC_Bank5_6_R->SDCR[1] = (1u << FMC_SDCRx_NC_Pos)       /* 9 column bits */
                             | (2u << FMC_SDCRx_NR_Pos)     /* 13 row bits */
                             | (1u << FMC_SDCRx_MWID_Pos)   /* 16-bit */
                             | FMC_SDCRx_NB                 /* 4 internal banks */
                             | ((uint32_t)SDRAM_CAS_LATENCY << FMC_SDCRx_CAS_Pos);
    /* SDTR1 carries TRC/TRP for both banks. Fields are value-1. */
    uint32_t trcd = cyc(18, 2), trp = cyc(18, 2), tras = cyc(42, 1), trc = cyc(60, 1), txsr = cyc(72, 1);
    uint32_t tmrd = 2, twr = 2;
    if (twr < tras - trcd) twr = tras - trcd;
    if (twr < trc - trcd - trp) twr = trc - trcd - trp;
    uint32_t sdtr = ((trc - 1) << FMC_SDTRx_TRC_Pos) | ((trp - 1) << FMC_SDTRx_TRP_Pos) |
                    ((tmrd - 1) << FMC_SDTRx_TMRD_Pos) | ((txsr - 1) << FMC_SDTRx_TXSR_Pos) |
                    ((tras - 1) << FMC_SDTRx_TRAS_Pos) | ((twr - 1) << FMC_SDTRx_TWR_Pos) |
                    ((trcd - 1) << FMC_SDTRx_TRCD_Pos);
    FMC_Bank5_6_R->SDTR[0] = sdtr;
    FMC_Bank5_6_R->SDTR[1] = sdtr;
    /* BCR1.FMCEN is the global enable for the whole FMC on the H7 (all banks). */
    FMC_Bank1_R->BTCR[0] |= FMC_BCR1_FMCEN;

    sdram_cmd(SDCMR_CLK_ENABLE, 1, 0);
    delay_us(250); /* W9825G6KH: 200 us with stable clock before PALL */
    sdram_cmd(SDCMR_PALL, 1, 0);
    sdram_cmd(SDCMR_AUTOREFRESH, 8, 0);
    /* Mode register: burst length 1, sequential, CAS latency, single-location write burst
     * (A2-A0 = 000 BL1, A3 = 0 sequential, A6-A4 = CL, A9 = 1 single write: 0x230 for CL3). */
    sdram_cmd(SDCMR_LOAD_MODE, 1, 0x200u | ((uint32_t)SDRAM_CAS_LATENCY << 4));
    FMC_Bank5_6_R->SDRTR = sdram_refresh_count() << FMC_SDRTR_COUNT_Pos;

    /* Quick sanity check (full tests via console `memtest`). The MPU and
     * D-cache are already on (mpu_cache_init runs first) and both regions are
     * cacheable: the save reads allocate the two lines, so after the writes
     * both lines are cleaned and invalidated and the readbacks come from the
     * chip, not the D-cache. */
    volatile uint32_t *p = (volatile uint32_t *)SDRAM_BASE;
    volatile uint32_t *q = (volatile uint32_t *)(SDRAM_BASE + SDRAM_SIZE - 4);
    uint32_t save_p = p[0], save_q = *q;
    p[0] = 0x5AA55AA5u;
    *q = 0xA55AA55Au;
    SCB_CleanInvalidateDCache_by_Addr((void *)p, 4); /* includes the DSB/ISB */
    SCB_CleanInvalidateDCache_by_Addr((void *)q, 4);
    g_sdram_ok = (p[0] == 0x5AA55AA5u) && (*q == 0xA55AA55Au);
    p[0] = save_p;
    *q = save_q;
    return g_sdram_ok ? 0 : -1;
}

/* Enter/leave self-refresh around Stop mode (power.c power_enter_stop): in
 * self-refresh the SDRAM keeps its contents with SDCLK stopped. Entry cleans
 * the whole D-cache first so no dirty SDRAM line is lost while the memory is
 * unreachable. Each direction polls SDSR.MODES2 for the new state (RM0433
 * FMC_SDSR: 00 normal, 01 self-refresh, 10 power-down) with a bounded loop
 * and returns either way (no error report). */
void sdram_self_refresh(bool enter)
{
    if (!g_bus_on) return;
    if (enter) {
        SCB_CleanDCache(); /* write back everything destined for SDRAM first */
        sdram_cmd(SDCMR_SELF_REFRESH, 1, 0);
        for (int i = 0; i < 10000 && ((FMC_Bank5_6_R->SDSR >> FMC_SDSR_MODES2_Pos) & 3u) != 1u; i++) {
        }
    } else {
        sdram_cmd(SDCMR_NORMAL, 1, 0);
        for (int i = 0; i < 10000 && ((FMC_Bank5_6_R->SDSR >> FMC_SDSR_MODES2_Pos) & 3u) != 0u; i++) {
        }
    }
}

/* NOR/SRAM bank 1, NE1: ST7789 8080 16-bit, mode A (EXTMOD: separate read and
 * write timings). The FMC kernel clock is 200 MHz (5 ns, rev V) or 180 MHz
 * (5.56 ns, rev Y); the same register values meet the ST7789 8080-II write
 * timing (ST7789V2 datasheet v1.0 table 4, same values as the ST7789V) at both. Mode A write = ADDSET + DATAST + 1 kernel
 * clocks; NWE low for DATAST, high for ADDSET + 1; BUSTURN is NOT inserted
 * between consecutive writes to the same bank (RM0433), so it does not count.
 *                                    200 MHz   180 MHz   spec
 *   write cycle  (5 + 15 + 1) clk    105 ns    117 ns    twc  >= 66 ns
 *   WRX low      DATAST 15            75 ns     83 ns    twrl >= 15 ns
 *   WRX high     ADDSET 5 + 1         30 ns     33 ns    twrh >= 15 ns
 *   data setup   DATAST 15 - RC      >= 43 ns  >= 51 ns  tdst >= 10 ns  (RC to VIH <= 24 ns)
 *   DCX setup    ADDSET 5             25 ns     28 ns    tast >= 0
 *   data hold    1 clk + RC          ~11 ns    ~12 ns    tdht >= 10 ns
 *   DCX hold     see lcd.c cmd()     >20 ns              taht >= 10 ns
 *   read: ADDSET 15, DATAST 90 -> RDX low 450/500 ns (trdlfm >= 355 ns), cycle
 *         >= 530 ns (trcfm >= 450 ns); BUSTURN 3 between a read and the next access.
 *         RDX high between reads is only ADDSET (75 ns; trdh/trdhfm >= 90 ns in the
 *         ST7789V2 datasheet) and ADDSET is at its maximum, so lcd.c rd() adds a gap
 *         after every read.
 * Hold time: the H743 FMC has no DATAHLD field (newer FMCs do), so mode A
 * drives data and A16 (DCX) for exactly one kernel clock after NWE rises, and
 * the next transaction (often SDRAM, on the shared D0-D15) can follow at once.
 * Hardware fix: the LCD data lines go through 1k series resistors (R26-R41),
 * which with the panel's ~8-12 pF delay every data edge at the panel by ~5-7 ns
 * (rise ~20 ns), so data hold is ~11 ns whatever drives the bus next. DATAST 15
 * leaves >= 43 ns of setup after that slower edge even at a pessimistic 20 pF
 * (a full frame takes ~8 ms). The bring-up `lcdbus` command measures the real
 * margin: it sweeps DATAST with GRAM write/readback while SDRAM traffic hits the
 * shared bus after every LCD write. DCX (A16, no resistor) only
 * changes between a command and its parameters: cmd() in lcd.c brackets every
 * command write with a barrier + delay. If pixels still corrupt on hardware,
 * lower the FMC clock (SDRAM_SDCLK_MHZ=90) or raise DATAST further. */
/* BWTR1 value: write-phase ADDSET/DATAST in FMC kernel clocks (ACCMOD = 0,
 * mode A), BUSTURN 1. */
static uint32_t lcd_bwtr(unsigned addset, unsigned datast)
{
    return (addset << FMC_BWTRx_ADDSET_Pos) | (datast << FMC_BWTRx_DATAST_Pos) | (1u << FMC_BWTRx_BUSTURN_Pos);
}

/* Change the LCD write timing at run time (bring-up margin sweep). ADDSET is
 * a 4-bit and DATAST an 8-bit field, hence the masks. The DSBs make sure no
 * buffered LCD write is still in flight when the timing changes. */
void lcd_bank_write_timing(unsigned addset, unsigned datast)
{
    __DSB();
    FMC_Bank1E_R->BWTR[0] = lcd_bwtr(addset & 0xFu, datast & 0xFFu);
    __DSB();
}

/* Configure NOR/SRAM bank 1 for the ST7789: SRAM type, 16-bit, write enable,
 * EXTMOD (separate read timing in BTR1 and write timing in BWTR1, see the
 * table above). Sets FMCEN too, so the bank works even before sdram_init(). */
void lcd_bank_init(void)
{
    if (!g_bus_on) bus_power(true);
    FMC_Bank1_R->BTCR[1] = (15u << FMC_BTRx_ADDSET_Pos) | (90u << FMC_BTRx_DATAST_Pos) |
                           (3u << FMC_BTRx_BUSTURN_Pos); /* ACCMOD = A */
    FMC_Bank1E_R->BWTR[0] = lcd_bwtr(LCD_WR_ADDSET, LCD_WR_DATAST);
    FMC_Bank1_R->BTCR[0] = FMC_BCRx_MBKEN | (0u << FMC_BCRx_MTYP_Pos) | (1u << FMC_BCRx_MWID_Pos) |
                           (1u << 7) /* reserved, reset value 1 */ | FMC_BCRx_WREN | FMC_BCRx_EXTMOD |
                           FMC_BCR1_FMCEN;
}
