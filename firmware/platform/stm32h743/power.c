/*
 * Power states.
 *   Run    : 400 MHz, everything on.
 *   Sleep  : WFI between SysTick interrupts (hal_idle).
 *   Stop   : SDRAM in self-refresh, all clocks stopped, LCD keeps its image.
 *            Wake: any key (column EXTI), ON key (EXTI0) or RTC wakeup timer.
 *   Standby: "off"/hibernate. Session save (platform_hibernate_save), LCD + SDRAM rail off,
 *            QSPI deep power-down, INA226 shutdown. Wake = ON key on WKUP1,
 *            which restarts the firmware from reset.
 * The watchdog (iwdg.c) keeps running in Sleep, where the 1 kHz SysTick keeps
 * the main loop and its kicks going; the option bytes freeze it in Stop and
 * Standby (without them it is not started at all, see iwdg.c).
 *
 * Also here: the boot-reason string (RCC_RSR + PWR standby flag), the RTC
 * wakeup timer used to bound a Stop, and the two low-battery paths: the
 * periodic VBAT_SENSE check (battery_service) and the INA226 bus
 * under-voltage alert (pmon_*). battery_service() and pmon_service() run
 * from hal_service() in the main loop; Stop is entered from the console
 * (`pwr stop`), Standby via hal_power_off() (SHIFT ON, `power off`, low
 * battery). Register reference: RM0433 "Power control (PWR)" (low-power
 * modes, wakeup pins) and "Real-time clock (RTC)" (wakeup timer).
 */
#include <math.h>
#include <stdio.h>
#include "calc.h"
#include "console.h"
#include "evlog.h"
#include "hal.h"
#include "platform.h"

bool rtc_ensure(void);

static char g_boot_reason[96];
static int g_reset_cause; /* HAL_RESET_* */
static volatile bool g_rtc_woke;

/* The app (app/session.c) overrides this to save the session before Standby;
 * the bring-up image keeps this no-op. */
__attribute__((weak)) int platform_hibernate_save(void) { return 0; }

/* First thing in hal_init(): record why we booted (reset flags in RCC_RSR,
 * Standby flag PWR_CPUCR.SBF), then clear both so the next boot reports
 * fresh values (RMVF clears RSR; CSSF clears SBF/STOPF). Runs before the
 * clocks and UART are set up; the string is printed later. */
void power_early_init(void)
{
    uint32_t rsr = RCC->RSR;
    bool standby = (PWR->CPUCR & PWR_CPUCR_SBF) != 0;
    snprintf(g_boot_reason, sizeof g_boot_reason, "%s%s%s%s%s%s%s(RSR=%08lx)",
             standby ? "standby-wake " : "", (rsr & RCC_RSR_PORRSTF) ? "por " : "",
             (rsr & RCC_RSR_BORRSTF) ? "bor " : "", (rsr & RCC_RSR_PINRSTF) ? "pin " : "",
             (rsr & RCC_RSR_SFTRSTF) ? "software " : "",
             (rsr & (RCC_RSR_IWDG1RSTF | RCC_RSR_WWDG1RSTF)) ? "watchdog " : "",
             (rsr & RCC_RSR_LPWRRSTF) ? "lowpower " : "", (unsigned long)rsr);
    /* One cause for MEM > About and safe mode, most specific first: an
     * internal reset also pulls NRST, so PINRSTF accompanies all of them, and
     * a power-on sets POR, BOR and PIN together. A fault handler's reset is a
     * software reset with its note in .noinit (startup.c). */
    bool fault = g_reset_note == RESET_NOTE_FAULT;
    g_reset_note = 0;
    if (rsr & (RCC_RSR_IWDG1RSTF | RCC_RSR_WWDG1RSTF))
        g_reset_cause = HAL_RESET_WATCHDOG;
    else if (standby)
        g_reset_cause = HAL_RESET_WAKE;
    else if (rsr & RCC_RSR_PORRSTF)
        g_reset_cause = HAL_RESET_POWERON;
    else if (rsr & RCC_RSR_BORRSTF)
        g_reset_cause = HAL_RESET_BROWNOUT;
    else if (rsr & RCC_RSR_SFTRSTF)
        g_reset_cause = fault ? HAL_RESET_FAULT : HAL_RESET_SOFTWARE;
    else if (rsr & RCC_RSR_PINRSTF)
        g_reset_cause = HAL_RESET_PIN;
    else
        g_reset_cause = HAL_RESET_OTHER;
    RCC->RSR |= RCC_RSR_RMVF;
    PWR->CPUCR |= PWR_CPUCR_CSSF;
    /* 3V3_BUS was switched off >= 20 ms before Standby was entered: the touch
     * controller has had its power-off time, no extra wait at boot. */
    if (standby) bus_power_assume_cold();
}

const char *power_boot_reason(void) { return g_boot_reason; }
int power_reset_cause(void) { return g_reset_cause; }

/* RTC write-protection key sequence (RTC_WPR: 0xCA then 0x53); any other
 * value (0xFF below) locks it again. Needs backup-domain access (DBP, set in
 * system.c lse_start). */
static void rtc_unlock(void)
{
    RTC->WPR = 0xCA;
    RTC->WPR = 0x53;
}

/* Arm the RTC wakeup timer to fire once after `ms` milliseconds (resolution
 * 1/2048 s, max 32 s) and route it to the CPU as EXTI line 19 (rising edge,
 * interrupt unmasked) so it can wake the core from Stop. WUTR may only be
 * written after WUTE is cleared and WUTWF confirms the write access.
 * Returns false if the LSE is not running (no RTC clock). */
static bool rtc_wakeup_arm(uint32_t ms)
{
    if (!rtc_ensure()) return false;
    rtc_unlock();
    RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
    for (int i = 0; i < 100000 && !(RTC->ISR & RTC_ISR_WUTWF); i++) {
    }
    if (ms > 32000u) ms = 32000u; /* max 32 s (clamped first: ms * 2048 overflows above ~35 min) */
    uint32_t ticks = ms * 2048u / 1000u; /* WUCKSEL = 000: RTCCLK/16 = 2048 Hz */
    if (ticks < 1) ticks = 1;
    if (ticks > 0xFFFF) ticks = 0xFFFF;
    RTC->WUTR = ticks - 1;
    RTC->CR = (RTC->CR & ~RTC_CR_WUCKSEL) | RTC_CR_WUTE | RTC_CR_WUTIE;
    RTC->ISR &= ~RTC_ISR_WUTF;
    RTC->WPR = 0xFF;
    EXTI->RTSR1 |= 1u << 19; /* RTC wakeup = EXTI line 19 */
    EXTI_D1->IMR1 |= 1u << 19;
    NVIC_EnableIRQ(RTC_WKUP_IRQn);
    return true;
}

/* Stop the wakeup timer and mask its EXTI line again. */
static void rtc_wakeup_disarm(void)
{
    rtc_unlock();
    RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
    RTC->WPR = 0xFF;
    EXTI_D1->IMR1 &= ~(1u << 19);
}

/* Wakeup-timer interrupt: clear WUTF in the RTC and the EXTI pending bit
 * (both, or it re-enters at once) and tell power_enter_stop() the RTC woke us. */
void RTC_WKUP_IRQHandler(void)
{
    rtc_unlock();
    RTC->ISR &= ~RTC_ISR_WUTF;
    RTC->WPR = 0xFF;
    EXTI_D1->PR1 = 1u << 19;
    g_rtc_woke = true;
}

/* ------------------------------------------------------- INA226 alert */
/* PMON_ALERT (PF10, EXTI10, open drain, 10k to 3V3): the INA226 flags VSYS
 * below PMON_LOW_MV (latched). VSYS is the cells after the reverse-protection
 * FET, the battery switch and the 0.1 ohm shunt, or USB minus the M7 diode
 * (~4.3 V). Since the TLV3012 VBUS comparator was added, the cells take over
 * within ~20 us of a USB unplug, so an unplug is not a brown-out and does not
 * reach this threshold. The buck (100 % duty near the end) needs VSYS >= ~3.5 V
 * at load, the SDRAM VDD >= 3.0 V, so this is the fast backstop under load;
 * the normal low-battery path is battery_service() below, on VBAT_SENSE
 * (HAL_BATT_WARN_V 4.0 V warning, HAL_BATT_EMPTY_V 3.6 V save + power off).
 * The ISR only flags; pmon_service() confirms with a reading. */
#define PMON_LOW_MV 3550u
#define PMON_GRACE_MS 10000u /* no action in the first 10 s after boot */
#define PMON_RETRY_MS 1000u  /* retry interval after a failed I2C read */
volatile uint32_t g_pmon_alerts; /* total alerts since boot (console `ina226`) */
static volatile bool g_pmon_flag;
static volatile uint32_t g_pmon_retry; /* g_ticks_ms of the next attempt after a failure */

/* EXTI10 (PF10 falling) from EXTI15_10_IRQHandler (keypad.c). The alert is
 * latched in the INA226, so the line stays low until it is acknowledged over
 * I2C; mask it here (no I2C in interrupt context) and let pmon_service() do
 * the reading and re-arm. */
void pmon_alert_irq(void)
{
    g_pmon_retry = g_ticks_ms;
    g_pmon_flag = true;
    g_pmon_alerts++;
    EXTI_D1->IMR1 &= ~(1u << EXTI_LINE_PMON_ALERT); /* re-armed by pmon_service */
}

/* Route PF10 to EXTI line 10 (SYSCFG_EXTICR selects the port: 5 = F),
 * falling edge; program the INA226 bus under-voltage limit, clear any old
 * alert, then unmask the line and enable the shared EXTI15_10 vector at
 * priority 4 (below SysTick's 3). No-op without an INA226. */
void pmon_alert_init(void)
{
    if (!ina226_present()) return;
    gpio_config(P_PMON_ALERT, PIN_IN, PULL_NONE);
    RCC->APB4ENR |= RCC_APB4ENR_SYSCFGEN;
    SYSCFG->EXTICR[EXTI_LINE_PMON_ALERT >> 2] =
        (SYSCFG->EXTICR[EXTI_LINE_PMON_ALERT >> 2] & ~(0xFu << ((EXTI_LINE_PMON_ALERT & 3) * 4))) |
        ((uint32_t)(P_PMON_ALERT >> 4) << ((EXTI_LINE_PMON_ALERT & 3) * 4));
    EXTI->FTSR1 |= 1u << EXTI_LINE_PMON_ALERT;
    EXTI->RTSR1 &= ~(1u << EXTI_LINE_PMON_ALERT);
    ina226_alert_bus_under(PMON_LOW_MV);
    ina226_alert_ack(NULL);
    EXTI_D1->PR1 = 1u << EXTI_LINE_PMON_ALERT;
    EXTI_D1->IMR1 |= 1u << EXTI_LINE_PMON_ALERT;
    NVIC_SetPriority(EXTI15_10_IRQn, 4);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/* Main-loop half of the alert: acknowledge it (reading the Mask/Enable
 * register clears the latch), read VSYS and the current, log, and request
 * a save + power-off if the reading is plausible and low (> 1 V and below
 * the threshold + 50 mV, allowing for the reading taken a little after the
 * alert), we are on battery (no USB VBUS), and the boot grace period is over. Otherwise re-arm the EXTI line. If the I2C
 * reads fail the flag stays set and the whole sequence is retried every
 * PMON_RETRY_MS (the line stays masked meanwhile: an unacknowledged alert
 * holds it low, so no new edge could arrive anyway). */
void pmon_service(void)
{
    if (!g_pmon_flag || (int32_t)(g_ticks_ms - g_pmon_retry) < 0) return;
    uint16_t mask = 0;
    float v = 0, i = 0;
    if (ina226_alert_ack(&mask) || ina226_read(&v, &i, NULL, NULL)) {
        g_pmon_retry = g_ticks_ms + PMON_RETRY_MS;
        return;
    }
    g_pmon_flag = false;
    bool usb = gpio_read(P_USB_VBUS_SENSE) != 0;
    bool low = v > 1.0f && v * 1000.0f < (float)PMON_LOW_MV + 50.0f; /* sanity: a real, low reading */
    con_log("pmon: alert mask=%04x vsys=%.3f V isys=%.1f mA usb=%d%s", mask, (double)v, (double)i, usb,
            low && !usb ? " -> low battery, powering off" : "");
    if (low && !usb && g_ticks_ms > PMON_GRACE_MS) {
        evlog_put("pmon: low battery %.2f V: powering off", (double)v);
        calc_request_power_off();
        return;
    }
    EXTI_D1->PR1 = 1u << EXTI_LINE_PMON_ALERT;
    EXTI_D1->IMR1 |= 1u << EXTI_LINE_PMON_ALERT;
}

/* ------------------------------------------------------- battery gauge */
/* Every BATT_CHECK_MS on battery (VBUS absent), read VBAT_SENSE (the switched
 * divider costs ~16 ms of blocking time per reading).
 *   < HAL_BATT_WARN_V  (4.0 V): log once (status bar battery turns red).
 *   < HAL_BATT_EMPTY_V (3.6 V) twice in a row (1 s apart, so a load transient
 *     doesn't count): save the session and power off (Standby). The bring-up
 *     image only logs.
 * Below 1 V there are no cells (bench supply on VSYS_IN): nothing happens. On
 * USB the battery switch is open, so the reading is the unloaded pack and is
 * only informational. */
#define BATT_FIRST_MS 2000u
#define BATT_CHECK_MS 10000u
#define BATT_CONFIRM_MS 1000u
static uint32_t g_batt_next = BATT_FIRST_MS;
static int g_batt_low;
static bool g_batt_warned;
static float g_batt_v = NAN;

/* Last VBAT_SENSE reading in volts (NAN before the first one); drives the
 * status-bar battery gauge. */
float battery_last_v(void) { return g_batt_v; }

/* Main-loop poll (hal_service). Cheap when not due; when due it blocks for
 * the ~16 ms adc_vbat() settling time. The (int32_t) difference makes the
 * deadline check wrap-safe. g_batt_low counts consecutive "empty" readings;
 * after the first one the next check is pulled in to BATT_CONFIRM_MS. */
void battery_service(void)
{
    uint32_t now = g_ticks_ms;
    if ((int32_t)(now - g_batt_next) < 0) return;
    g_batt_next = now + BATT_CHECK_MS;
    if (gpio_read(P_USB_VBUS_SENSE)) {
        g_batt_low = 0;
        return;
    }
    float v = adc_vbat();
    g_batt_v = v;
    if (!(v > 1.0f)) {
        g_batt_low = 0;
        return;
    }
    if (v < HAL_BATT_WARN_V && !g_batt_warned) {
        g_batt_warned = true;
        con_log("battery: LOW %.2f V (warning below %.1f V, power-off below %.1f V)", (double)v,
                (double)HAL_BATT_WARN_V, (double)HAL_BATT_EMPTY_V);
    } else if (v > HAL_BATT_WARN_V + 0.2f) {
        g_batt_warned = false; /* fresh cells */
    }
    if (v < HAL_BATT_EMPTY_V) {
        if (++g_batt_low >= 2) {
#ifdef BRINGUP
            con_log("battery: EMPTY %.2f V (bring-up image: not powering off)", (double)v);
            g_batt_low = 0;
#else
            evlog_add("battery: empty %.2f V: saving and powering off", (double)v);
            calc_request_power_off();
#endif
            return;
        }
        g_batt_next = now + BATT_CONFIRM_MS;
    } else {
        g_batt_low = 0;
    }
}

/* Returns 1 if woken by the RTC, 0 if by a key/other interrupt, <0 on error.
 * Enter Stop (all D1/D2/D3 domains in Stop, not Standby: PDDS_Dx = 0; LPDS
 * selects the low-power regulator), optionally bounded by the RTC wakeup
 * timer (max_ms = 0: no timeout; -1 if a timeout was asked for but the LSE
 * is not running). Before: key rows low with column EXTIs armed, TE masked,
 * UART drained (TC) so no character is cut off, SDRAM in self-refresh (the
 * FMC stops clocking it), SysTick interrupt off (a tick arriving just
 * before WFI would otherwise end the Stop immediately). After WFI returns:
 * PLLs rebuilt (Stop drops back to HSI), SysTick restarted, SDRAM back to
 * normal, TE restored (its pending bit cleared first), Stop flags cleared.
 * Peripheral registers and RAM are retained in Stop; console `pwr stop`. */
static int stop_once(uint32_t max_ms);

/* Stop as below. With a running watchdog that the option bytes do not freeze
 * in Stop (iwdg.c), the Stop is cut into RTC wake-ups of at most
 * iwdg_stop_slice_ms(), each followed by a kick, until a key wakes us or
 * max_ms is used up (max_ms = 0: until a key). Needs the LSE for the RTC. */
int power_enter_stop(uint32_t max_ms)
{
    uint32_t slice = iwdg_stop_slice_ms();
    if (!slice)
        return stop_once(max_ms);
    for (uint32_t left = max_ms;;) {
        uint32_t ms = max_ms && left < slice ? left : slice;
        iwdg_kick();
        int r = stop_once(ms);
        iwdg_kick();
        if (r != 1)
            return r; /* a key / other interrupt, or no RTC (-1) */
        if (max_ms && (left -= ms) == 0)
            return 1;
    }
}

static int stop_once(uint32_t max_ms)
{
    g_rtc_woke = false;
    if (max_ms && !rtc_wakeup_arm(max_ms)) return -1;
    keypad_prepare_wake();
    /* TE would wake us 60 times a second: mask it. Touch (EXTI1) and the
     * power-monitor alert (EXTI10) stay armed as wake sources. */
    uint32_t te_mask = EXTI_D1->IMR1 & (1u << EXTI_LINE_LCD_TE);
    EXTI_D1->IMR1 &= ~(1u << EXTI_LINE_LCD_TE);
    while (!(USART1->ISR & USART_ISR_TC)) {
    }
    sdram_self_refresh(true);
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    PWR->CPUCR &= ~(PWR_CPUCR_PDDS_D1 | PWR_CPUCR_PDDS_D2 | PWR_CPUCR_PDDS_D3);
    PWR->CR1 |= PWR_CR1_LPDS; /* low-power regulator in Stop */
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    __WFI();
    __ISB();
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    /* Wakes on HSI 64 MHz: rebuild the PLL clock tree, then SDRAM. */
    clock_restore_after_stop();
    SysTick_Config(SystemCoreClock / 1000u);
    sdram_self_refresh(false);
    EXTI_D1->PR1 = 1u << EXTI_LINE_LCD_TE;
    EXTI_D1->IMR1 |= te_mask;
    if (max_ms) rtc_wakeup_disarm();
    PWR->CPUCR |= PWR_CPUCR_CSSF;
    return g_rtc_woke ? 1 : 0;
}

/* Put every switched rail's pins in their unpowered state and drop the rails:
 * SD (pins analog, SD_EN low), 3V3_BUS (see bus_power in fmc.c), backlight
 * DAC off and its pin analog, VBAT divider switch off, debug LED off. Used
 * before Standby; in Standby the GPIOs float and the 100k pull-downs keep
 * BUS_EN/SD_EN low. */
void power_set_pins_analog_for_off(void)
{
    sd_power(false);
    bus_power(false); /* FMC, LCD_TE, I2C4/TP_INT analog, TP_RST + LCD_RESET low, then BUS_EN low */
    backlight_set_ma(0);
    DAC1->CR &= ~DAC_CR_EN1;
    gpio_set_analog(P_LCD_BL_DAC);
    gpio_write(P_VBAT_SENSE_EN, 0);
    gpio_write(P_LED_DEBUG, 0);
}

/* "Off": save the session, put every device in its lowest-power state, drop
 * the rails, arm WKUP1 (ON key, PA0) and enter Standby (all domains
 * powered down: PDDS_D1/D2/D3 = 1, SLEEPDEEP, WFI). Never returns; wake is
 * a reset, and power_early_init() then reports "standby-wake". The order
 * matters: the session save needs SDRAM and QSPI, so it comes first; the
 * devices get their sleep commands while their rails are still up; pins go
 * analog before the rails drop (no back-powering). */
void power_enter_standby(void)
{
    /* The session lives in SDRAM (CAS arena) and goes to the QSPI flash: only
     * possible while 3V3_BUS is on (it is, unless the console switched it off). */
    if (bus_powered()) {
        int rc = platform_hibernate_save();
        if (rc) uart_write_raw_polled("# standby: session save FAILED\r\n");
    } else {
        uart_write_raw_polled("# standby: 3V3_BUS off, session not saved\r\n");
    }
    backlight_set_ma(0);
    touch_sleep();
    lcd_sleep(true);
    qspi_deep_power_down();
    EXTI_D1->IMR1 &= ~(1u << EXTI_LINE_PMON_ALERT);
    if (ina226_present()) {
        ina226_alert_bus_under(0);
        ina226_shutdown();
    }
    power_set_pins_analog_for_off();

    /* Wait for ON to be released, or WKUP1 (level-sensitive) fires at once. */
    for (uint32_t t0 = g_ticks_ms; gpio_read(P_KEY_ON) && g_ticks_ms - t0 < 5000;) {
        iwdg_kick(); /* up to 5 s; the IWDG is frozen once Standby is reached (iwdg.c) */
    }
    /* 3V3_BUS stays off >= 20 ms before Standby (touch controller power-off time, >= 5 ms), so
     * the boot after a Standby wake may power it up at once. */
    bus_power_wait_off_time();
    delay_us(20000);
    uart_write_raw_polled("# standby: press ON to wake\r\n");

    __disable_irq();
    /* WKUP1 = PA0, rising edge/high level (WKUPP1 = 0), no internal pull (100k on board) */
    PWR->WKUPEPR = (PWR->WKUPEPR & ~(PWR_WKUPEPR_WKUPP1 | (3u << PWR_WKUPEPR_WKUPPUPD1_Pos))) |
                   PWR_WKUPEPR_WKUPEN1;
    PWR->WKUPCR = 0x3Fu; /* clear all wakeup-pin flags */
    PWR->CPUCR |= PWR_CPUCR_PDDS_D1 | PWR_CPUCR_PDDS_D2 | PWR_CPUCR_PDDS_D3;
    PWR->CPUCR |= PWR_CPUCR_CSSF;
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    /* Interrupts are disabled but WFI still wakes on a pending one; the loop
     * just re-enters. Standby itself ends only with a reset (WKUP1, NRST or
     * a power cycle). */
    for (;;) __WFI();
}
