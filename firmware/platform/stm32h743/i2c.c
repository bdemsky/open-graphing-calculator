/*
 * I2C masters, polled, main context only:
 *   I2C2 (PB10 SCL / PB11 SDA, AF4, 4.7k pull-ups to 3V3): INA226 at 0x40 + expansion header, 100 kHz.
 *   I2C4 (PD12 SCL / PD13 SDA, AF4, 4.7k pull-ups to 3V3_BUS): FT6236 touch controller (on the LCD FPC),
 *        100 kHz by default, ~340 kHz with TOUCH_I2C_FAST=1 or `tp speed fast`
 *        (driver in touch_focaltech.c; the bus is unpowered while 3V3_BUS is off).
 * Kernel clocks: I2C2 = PCLK1 100 MHz (I2C123SEL = 0), I2C4 = PCLK4 100 MHz (I2C4SEL = 0).
 *
 * Uses the STM32H7 I2C v2 master state machine (RM0433 section 47 "Inter-
 * integrated circuit interface"): software only programs CR2 (address,
 * direction, NBYTES, AUTOEND, START) and feeds/drains TXDR/RXDR on TXIS/RXNE;
 * the peripheral generates START, address, ACK/NACK and (with AUTOEND) STOP
 * itself. No interrupts or DMA: every transfer is a short busy-wait, so these
 * functions must not be called from ISRs (they would race main-context
 * transfers on the same bus).
 *
 * Return convention for all transfer functions: 0 = ok, -1 = NACK or failure,
 * -2 (wait_isr only) = timeout/bus error after which the peripheral was reset,
 * -3 = invalid length.
 *
 * The second half of the file is the INA226 power monitor driver (TI INA226
 * datasheet SBOS547), which measures the VSYS input across a 0.1 ohm shunt.
 */
#include "platform.h"

/* TIMINGR field encoding (RM0433 "I2C timings"): PRESC[31:28], SCLDEL[23:20],
 * SDADEL[19:16], SCLH[15:8], SCLL[7:0]; t_PRESC = (PRESC+1) / f_kernel,
 * t_SCLDEL = (SCLDEL+1) t_PRESC, t_SDADEL = SDADEL t_PRESC,
 * t_SCLH = (SCLH+1) t_PRESC, t_SCLL = (SCLL+1) t_PRESC.
 * I2C2, standard mode, 100 MHz kernel: PRESC=9 (100 ns), SCLDEL=4 (500 ns), SDADEL=2 (200 ns),
 * SCLH=39 (4.0 us), SCLL=49 (5.0 us) -> ~95-100 kHz with the rise time (tLOW >= 4.7, tHIGH >= 4.0 us). */
#define I2C2_TIMINGR 0x90422731u
/* I2C4 (touch). The FT6x36 datasheet (v0.3, table 2-2) allows SCL up to 400 kHz but
 * lists standard-mode bus timing: tHD;STA and tSU;STO >= 4.0 us, tBUF >= 4.7 us,
 * tSU;DAT >= 250 ns. The STM32 I2C v2 derives the START/STOP timing from SCLH/SCLL, so
 * only the standard-mode setting meets all of them: by default I2C4 uses the I2C2
 * value above (100 MHz kernel as well; a 2-point report then takes ~1.8 ms).
 * Fast mode, 100 MHz kernel: PRESC=1 (20 ns), SCLDEL=9 (200 ns >= tr + tSU;DAT 100 ns),
 * SDADEL=2 (40 ns), SCLH=49 (1.0 us >= 0.6), SCLL=79 (1.6 us >= 1.3)
 * -> period 2.6 us + rise/fall/sync ~0.35 us = ~340 kHz; FocalTech parts commonly run
 * at 400 kHz, but it is outside the table above. Select it with TOUCH_I2C_FAST=1 or
 * at run time with `tp speed fast` (bring-up checklist, touch step). */
#define I2C4_TIMINGR_STD  I2C2_TIMINGR
#define I2C4_TIMINGR_FAST 0x1092314Fu
#ifndef TOUCH_I2C_FAST
#define TOUCH_I2C_FAST 0
#endif
static bool g_i2c4_fast = TOUCH_I2C_FAST;

/* I2C4 speed for the next i2c_bus_init(I2C4) (touch_focaltech.c re-inits it). */
void i2c4_set_fast(bool fast) { g_i2c4_fast = fast; }
bool i2c4_fast(void) { return g_i2c4_fast; }
#define I2C_TIMEOUT 200000u /* ISR polls per wait (iterations, not time: each poll is
                             * an APB register read, so this is on the order of ms) */

static bool g_ina_ok; /* INA226 identified and configured by ina226_init() */

/* Clock and enable one I2C master (I2C2 or I2C4; any other pointer is treated
 * as I2C4). TIMINGR may only be written while PE = 0. Pins are configured
 * separately (gpio_config_from_table() for I2C2, touch_focaltech.c for I2C4,
 * which is unpowered while 3V3_BUS is off). Analog/digital noise filters are
 * left at their reset defaults (analog filter on, digital filter off). */
void i2c_bus_init(I2C_TypeDef *i2c)
{
    if (i2c == I2C2) {
        RCC->APB1LENR |= RCC_APB1LENR_I2C2EN;
        (void)RCC->APB1LENR;
    } else {
        RCC->APB4ENR |= RCC_APB4ENR_I2C4EN;
        (void)RCC->APB4ENR;
    }
    i2c->CR1 = 0;
    i2c->TIMINGR = i2c == I2C2 ? I2C2_TIMINGR : g_i2c4_fast ? I2C4_TIMINGR_FAST : I2C4_TIMINGR_STD;
    i2c->CR1 = I2C_CR1_PE;
}

void i2c_init(void) { i2c_bus_init(I2C2); }

/*
 * Busy-wait until any bit of `mask` is set in ISR.
 * Returns 0 when it is, -1 on NACK (the slave did not acknowledge; the
 * hardware has already issued STOP, which is waited for and cleared), or -2
 * on bus error / arbitration loss / timeout. In the -2 case the peripheral is
 * reset by toggling PE (RM0433 "I2C software reset"), which releases SCL/SDA
 * and returns the state machine to idle without touching TIMINGR.
 */
static int wait_isr(I2C_TypeDef *i2c, uint32_t mask)
{
    for (uint32_t i = 0; i < I2C_TIMEOUT; i++) {
        uint32_t isr = i2c->ISR;
        if (isr & I2C_ISR_NACKF) {
            i2c->ICR = I2C_ICR_NACKCF;
            /* a STOP is generated automatically after NACK in master mode */
            for (uint32_t j = 0; j < I2C_TIMEOUT && !(i2c->ISR & I2C_ISR_STOPF); j++) {
            }
            i2c->ICR = I2C_ICR_STOPCF;
            return -1;
        }
        if (isr & (I2C_ISR_BERR | I2C_ISR_ARLO)) {
            i2c->ICR = I2C_ICR_BERRCF | I2C_ICR_ARLOCF;
            break;
        }
        if (isr & mask) return 0;
    }
    /* Timeout or bus error: reset the peripheral. PE must stay low for >= 3
     * APB clocks; RM0433's sequence is write PE = 0, check PE = 0, write PE = 1. */
    i2c->CR1 = 0;
    while (i2c->CR1 & I2C_CR1_PE) {
    }
    i2c->CR1 = I2C_CR1_PE;
    return -2;
}

/* Write n (0..255) bytes to 7-bit address `addr` in one transaction:
 * START, address+W, data..., STOP (AUTOEND). For register writes the first
 * data byte is the register address. */
int i2c_bus_write(I2C_TypeDef *i2c, uint8_t addr, const uint8_t *data, int n)
{
    i2c->CR2 = ((uint32_t)addr << 1) | ((uint32_t)n << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND | I2C_CR2_START;
    for (int i = 0; i < n; i++) {
        if (wait_isr(i2c, I2C_ISR_TXIS)) return -1;
        i2c->TXDR = data[i];
    }
    if (wait_isr(i2c, I2C_ISR_STOPF)) return -1;
    i2c->ICR = I2C_ICR_STOPCF;
    return 0;
}

/* Write register address `reg`, repeated START, read n (1..255) bytes. */
int i2c_bus_read(I2C_TypeDef *i2c, uint8_t addr, uint8_t reg, uint8_t *buf, int n)
{
    if (n < 1 || n > 255) return -3;
    /* phase 1: one byte, no AUTOEND, so the peripheral stops at TC (SCL held
     * low) instead of sending STOP; writing CR2 again then emits a repeated
     * START for the read phase */
    i2c->CR2 = ((uint32_t)addr << 1) | (1u << I2C_CR2_NBYTES_Pos) | I2C_CR2_START;
    if (wait_isr(i2c, I2C_ISR_TXIS)) return -1;
    i2c->TXDR = reg;
    if (wait_isr(i2c, I2C_ISR_TC)) return -1;
    i2c->CR2 = ((uint32_t)addr << 1) | I2C_CR2_RD_WRN | ((uint32_t)n << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND |
               I2C_CR2_START;
    for (int i = 0; i < n; i++) {
        if (wait_isr(i2c, I2C_ISR_RXNE)) return -1;
        buf[i] = (uint8_t)i2c->RXDR;
    }
    if (wait_isr(i2c, I2C_ISR_STOPF)) return -1;
    i2c->ICR = I2C_ICR_STOPCF;
    return 0;
}

/* Address-only transaction (NBYTES = 0): START, address+W, STOP. Returns 0 if
 * a device acknowledged `addr`, -1 on NACK. */
int i2c_bus_probe(I2C_TypeDef *i2c, uint8_t addr)
{
    i2c->CR2 = ((uint32_t)addr << 1) | (0u << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND | I2C_CR2_START;
    int r = wait_isr(i2c, I2C_ISR_STOPF);
    i2c->ICR = I2C_ICR_STOPCF;
    return r;
}

/* Shorthands for the main bus, I2C2 (INA226, expansion header). */
int i2c_write(uint8_t addr, const uint8_t *data, int n) { return i2c_bus_write(I2C2, addr, data, n); }
int i2c_probe(uint8_t addr) { return i2c_bus_probe(I2C2, addr); }

/* 16-bit register access, big-endian on the wire (INA226 convention). */
int i2c_read_reg16(uint8_t addr, uint8_t reg, uint16_t *val)
{
    uint8_t b[2];
    if (i2c_bus_read(I2C2, addr, reg, b, 2)) return -1;
    *val = (uint16_t)((b[0] << 8) | b[1]);
    return 0;
}

int i2c_write_reg16(uint8_t addr, uint8_t reg, uint16_t val)
{
    uint8_t b[3] = {reg, (uint8_t)(val >> 8), (uint8_t)val};
    return i2c_write(addr, b, 3);
}

/* ------------------------------------------------------------- INA226 */
#define INA_CONFIG 0x00
#define INA_SHUNT  0x01
#define INA_BUS    0x02
#define INA_POWER  0x03
#define INA_CURRENT 0x04
#define INA_CAL    0x05
#define INA_MASK   0x06
#define INA_ALERT  0x07
#define INA_MFG_ID 0xFE
#define INA_DIE_ID 0xFF
/* Calibration (INA226 datasheet eq. 1-3), shunt 0.1 ohm 1 % (2512, Kelvin-routed,
 * sense inputs through matched 10 ohm filter resistors; CAL is unchanged by them;
 * INA226_CURRENT_SCALE in board.h is the one-point gain correction):
 *   max shunt voltage 81.92 mV -> 819 mA full scale (the board draws < 450 mA worst case),
 *   Current_LSB = 25 uA, CAL = 0.00512 / (25e-6 * 0.1) = 2048, Power_LSB = 25 * Current_LSB = 625 uW.
 *   Shunt LSB 2.5 uV (= 25 uA here), bus LSB 1.25 mV. Standby (~35 uA) is about 1 LSB: use the
 *   averaged readings, and expect a few LSB of offset (datasheet: +-10 uV max = +-100 uA).
 * AVG=16 (010), VBUSCT=1.1 ms (100), VSHCT=1.1 ms (100), MODE=shunt+bus continuous (111)
 * -> a new averaged result every 16 x (1.1 + 1.1) ms = ~35 ms. Bit 14 is the
 * datasheet's fixed '1'. SHUTDOWN is the same word with MODE = 000. */
#define INA_CONFIG_RUN 0x4527u
#define INA_CONFIG_SHUTDOWN 0x4520u

bool ina226_present(void) { return g_ina_ok; }

/* Identify the INA226 (manufacturer ID 0x5449 "TI", die ID 0x226x) and start
 * continuous conversion with the calibration above. Returns 0 on success,
 * -1 no answer, -2 wrong IDs, -3/-4 configuration write failed. */
int ina226_init(void)
{
    uint16_t mfg = 0, die = 0;
    g_ina_ok = false;
    if (i2c_read_reg16(INA226_ADDR, INA_MFG_ID, &mfg) || i2c_read_reg16(INA226_ADDR, INA_DIE_ID, &die))
        return -1;
    if (mfg != 0x5449 || (die & 0xFFF0) != 0x2260) return -2;
    if (i2c_write_reg16(INA226_ADDR, INA_CONFIG, INA_CONFIG_RUN)) return -3;
    if (i2c_write_reg16(INA226_ADDR, INA_CAL, INA226_CAL)) return -4;
    g_ina_ok = true;
    return 0;
}

/* Read the latest conversion: VSYS bus voltage (V), current (mA, signed),
 * power (mW) and shunt voltage (mV, signed). Any output pointer may be NULL.
 * The four registers are read in separate transactions, so they may straddle
 * a conversion boundary (~35 ms). */
int ina226_read(float *vbus_v, float *i_ma, float *p_mw, float *vshunt_mv)
{
    uint16_t sh, bus, pw, cur;
    if (i2c_read_reg16(INA226_ADDR, INA_SHUNT, &sh) || i2c_read_reg16(INA226_ADDR, INA_BUS, &bus) ||
        i2c_read_reg16(INA226_ADDR, INA_POWER, &pw) || i2c_read_reg16(INA226_ADDR, INA_CURRENT, &cur))
        return -1;
    if (vshunt_mv) *vshunt_mv = (float)(int16_t)sh * 2.5e-3f;     /* 2.5 uV/LSB */
    if (vbus_v) *vbus_v = (float)bus * 1.25e-3f;                    /* 1.25 mV/LSB */
    if (i_ma) *i_ma = (float)(int16_t)cur * INA226_I_LSB_A * 1000.0f * INA226_CURRENT_SCALE;
    if (p_mw) *p_mw = (float)pw * 25.0f * INA226_I_LSB_A * 1000.0f * INA226_CURRENT_SCALE; /* 25 x current LSB */
    return 0;
}

/* Alert pin (PF10, open drain, EXTI10): bus under-voltage on VSYS, latched.
 * mv = 0 disables the alert. Reading Mask/Enable clears the latch. */
int ina226_alert_bus_under(uint32_t mv)
{
    if (!g_ina_ok) return -1;
    if (!mv) return i2c_write_reg16(INA226_ADDR, INA_MASK, 0);
    uint32_t lsb = mv * 1000u / 1250u; /* 1.25 mV/LSB */
    if (i2c_write_reg16(INA226_ADDR, INA_ALERT, (uint16_t)(lsb > 0x7FFF ? 0x7FFF : lsb))) return -1;
    return i2c_write_reg16(INA226_ADDR, INA_MASK, (1u << 12) /* BUL */ | 1u /* LEN: latch */);
}

/* Read Mask/Enable (clears a latched alert and releases the ALERT pin);
 * optionally returns the register so the caller can see which flag fired. */
int ina226_alert_ack(uint16_t *mask)
{
    uint16_t m = 0;
    int r = i2c_read_reg16(INA226_ADDR, INA_MASK, &m);
    if (mask) *mask = m;
    return r;
}

/* Power-down mode (before Standby): conversions stop, supply current drops to
 * the datasheet's shutdown level. ina226_init() restarts it. */
int ina226_shutdown(void)
{
    return i2c_write_reg16(INA226_ADDR, INA_CONFIG, INA_CONFIG_SHUTDOWN);
}
