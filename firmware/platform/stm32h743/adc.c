/*
 * Rail monitors: ADC1 for the board's divider inputs, ADC3 for VREFINT and the
 * die temperature (on the H743 the internal channels exist only on ADC3).
 * Kernel clock: PLL2P = 24 MHz, CCR prescaler /2 -> 12 MHz (rev Y) or 6 MHz
 * (rev V has an extra internal /2). BOOST = 0 is correct for both.
 * Sampling time is the maximum (810.5 cycles) because the dividers present
 * 33-50 kohm source impedance.
 *
 * Inputs (ADC1, single-ended, 16-bit): VBAT_SENSE PA3 = INP15 (switched
 * 100k/47k divider), MON_3V3_BUS PA6 = INP3 and MON_3V3_SD PC4 = INP4
 * (100k/100k), BL_SENSE PC5 = INP8 (backlight current sense resistor).
 * Every reading is a software-triggered single conversion, polled, averaged
 * over 4, and scaled by VDDA measured through VREFINT (VDDA is the 3V3 rail,
 * so its tolerance would otherwise go straight into every reading).
 * One conversion takes ~(810.5 + 8.5) cycles: ~68 us at 12 MHz (rev Y),
 * ~137 us at 6 MHz (rev V). Factory
 * calibration constants and their addresses: DS12110 "Embedded reference
 * voltage" and "Temperature sensor characteristics" (taken at VDDA = 3.3 V).
 */
#include <math.h>
#include "platform.h"

#define VREFINT_CAL (*(const volatile uint16_t *)0x1FF1E860u)
#define TS_CAL1     (*(const volatile uint16_t *)0x1FF1E820u) /* 30 C */
#define TS_CAL2     (*(const volatile uint16_t *)0x1FF1E840u) /* 110 C */
#define ADC3_CH_TEMP 18
#define ADC3_CH_VREF 19

static bool g_adc_ok;

/* Power-up sequence for one ADC (RM0433 "ADC on-off control"): leave deep
 * power-down, enable the internal regulator and wait its start-up time,
 * single-ended linearity + offset calibration (ADCALLIN), then ADEN and wait
 * for ADRDY. BOOST is cleared (correct for <= 6.25 MHz on rev V and for the
 * rev Y clock range used here). Then RES = 0 (16-bit), single conversion,
 * software trigger, OVRMOD = 1 (DR is overwritten, no overrun lock-up).
 * Returns 0, -1 calibration timeout, -2 ADRDY timeout. */
static int adc_enable(ADC_TypeDef *adc)
{
    adc->CR &= ~ADC_CR_DEEPPWD;
    adc->CR |= ADC_CR_ADVREGEN;
    delay_us(50); /* tADCVREG_STUP 10 us; rev V also flags LDORDY */
    adc->CR &= ~(ADC_CR_ADCALDIF | (3u << ADC_CR_BOOST_Pos));
    adc->CR |= ADC_CR_ADCALLIN;
    adc->CR |= ADC_CR_ADCAL;
    for (uint32_t i = 0; adc->CR & ADC_CR_ADCAL; i++) {
        if (i > 10000000u) return -1;
    }
    adc->ISR = ADC_ISR_ADRDY;
    adc->CR |= ADC_CR_ADEN;
    for (uint32_t i = 0; !(adc->ISR & ADC_ISR_ADRDY); i++) {
        if (i > 10000000u) return -2;
    }
    adc->CFGR = (adc->CFGR & ~(ADC_CFGR_RES | ADC_CFGR_CONT | ADC_CFGR_EXTEN)) | ADC_CFGR_OVRMOD; /* 16-bit */
    return 0;
}

/* Clocks on, common prescaler /2 on the asynchronous kernel clock (CKMODE =
 * 0 -> adc_ker_ck = PLL2P), VREFINT and temperature sensor enabled on ADC3,
 * then calibrate and enable ADC1 and ADC3. If either fails every reading
 * returns 0 / NAN. */
void adc_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_ADC12EN;
    RCC->AHB4ENR |= RCC_AHB4ENR_ADC3EN;
    (void)RCC->AHB4ENR;
    ADC12_COMMON->CCR = (ADC12_COMMON->CCR & ~(ADC_CCR_CKMODE | ADC_CCR_PRESC)) | (1u << ADC_CCR_PRESC_Pos);
    ADC3_COMMON->CCR = (ADC3_COMMON->CCR & ~(ADC_CCR_CKMODE | ADC_CCR_PRESC)) | (1u << ADC_CCR_PRESC_Pos) |
                       ADC_CCR_VREFEN | ADC_CCR_TSEN;
    g_adc_ok = adc_enable(ADC1) == 0 && adc_enable(ADC3) == 0;
}

/* Sampling time of channel ch = 7 (810.5 ADC clocks). Channels 0-9 are in
 * SMPR1, 10-19 in SMPR2, 3 bits each. */
static void set_smp(ADC_TypeDef *adc, int ch)
{
    if (ch < 10) {
        adc->SMPR1 = (adc->SMPR1 & ~(7u << (3 * ch))) | (7u << (3 * ch));
    } else {
        adc->SMPR2 = (adc->SMPR2 & ~(7u << (3 * (ch - 10)))) | (7u << (3 * (ch - 10)));
    }
}

/* One conversion of channel ch: preselect it (PCSEL, H7-specific: the
 * channel's input switch must be enabled), make it the only entry in the
 * regular sequence, start, poll EOC. Returns the 16-bit result, or 0 if the
 * ADC is not initialised or the conversion timed out (callers treat 0 as
 * "no reading" where it matters). */
static uint32_t convert(ADC_TypeDef *adc, int ch)
{
    if (!g_adc_ok) return 0;
    adc->PCSEL |= 1u << ch;
    set_smp(adc, ch);
    adc->SQR1 = ((uint32_t)ch << ADC_SQR1_SQ1_Pos); /* L = 0: one conversion */
    adc->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
    adc->CR |= ADC_CR_ADSTART;
    for (uint32_t i = 0; !(adc->ISR & ADC_ISR_EOC); i++) {
        if (i > 1000000u) return 0;
    }
    return adc->DR & 0xFFFFu;
}

static uint32_t convert_avg(ADC_TypeDef *adc, int ch, int n)
{
    uint32_t sum = 0;
    for (int i = 0; i < n; i++) sum += convert(adc, ch);
    return sum / (uint32_t)n;
}

/* VDDA in volts: VREFINT_CAL was measured at VDDA = 3.3 V, so
 * VDDA = 3.3 x VREFINT_CAL / VREFINT_raw (both 16-bit). NAN if no reading. */
float adc_vdda(void)
{
    uint32_t raw = convert_avg(ADC3, ADC3_CH_VREF, 4);
    if (!raw) return NAN;
    return 3.3f * (float)VREFINT_CAL / (float)raw;
}

/* Voltage at an ADC1 pin in volts (before any divider ratio; callers apply
 * VBAT_DIV_RATIO / RAIL_DIV_RATIO / BL_SENSE_OHMS from board.h). */
float adc_read_volts(int channel)
{
    float vdda = adc_vdda();
    if (isnan(vdda)) return NAN;
    return vdda * (float)convert_avg(ADC1, channel, 4) / 65535.0f;
}

/* Die temperature in deg C, by linear interpolation between the two factory
 * points (TS_CAL1 at 30 C, TS_CAL2 at 110 C). The raw value is first rescaled
 * to what it would read at VDDA = 3.3 V, the calibration condition. */
float adc_temp_c(void)
{
    float vdda = adc_vdda();
    uint32_t raw = convert_avg(ADC3, ADC3_CH_TEMP, 4);
    if (isnan(vdda) || !raw || TS_CAL2 == TS_CAL1) return NAN;
    float scaled = (float)raw * vdda / 3.3f; /* calibration values were taken at 3.3 V */
    return 30.0f + (110.0f - 30.0f) * (scaled - (float)TS_CAL1) / (float)(TS_CAL2 - TS_CAL1);
}

/* Battery pack voltage in volts. The divider is only switched on for the
 * reading so it does not drain the cells; blocks ~16 ms. Main context only. */
float adc_vbat(void)
{
    /* High-side P-FET switch on the divider; ~16 ms to settle (100k into 100 nF). */
    gpio_write(P_VBAT_SENSE_EN, 1);
    delay_us(16000);
    float v = adc_read_volts(ADC_CH_VBAT) * VBAT_DIV_RATIO;
    gpio_write(P_VBAT_SENSE_EN, 0);
    return v;
}
