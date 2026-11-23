/*
 * gpio.c — GPIO helpers and the board pin table (STM32H743, RM0433 section 11
 * "General-purpose I/Os").
 *
 * The pin identifiers used throughout the platform layer are the P_<net>
 * enumerators generated from BOARD_PINS in board.h: (port index << 4) | pin,
 * port A = 0 ... port H = 7. Every GPIO port of the H743 sits at
 * GPIOA_BASE + 0x400 * index on AHB4, which is what gpio_port() relies on.
 *
 * gpio_config_from_table() is called at boot from hal_stm32.c and configures
 * only the "simple" pins. Bus peripherals (FMC, QSPI, SDMMC) and everything
 * powered from the switched 3V3_BUS rail are configured by their own drivers,
 * so that no pin drives a powered-down rail through the far end's ESD diodes
 * (back-powering).
 */
#include <string.h>
#include "platform.h"

/* One BOARD_PINS row kept at run time (the net name classifies the pin in
 * gpio_config_from_table()). */
typedef struct {
    const char *net;
    uint8_t pin;
    uint8_t mode; /* AF number 0-15 or PIN_* */
} pin_def_t;

#define PIN_DEF(net, port, pin, af) {#net, P_##net, af},
static const pin_def_t g_pins[] = {BOARD_PINS(PIN_DEF)};
#undef PIN_DEF

/* Register block of the port that owns `pin` (a P_<net> identifier). */
GPIO_TypeDef *gpio_port(int pin)
{
    return (GPIO_TypeDef *)(GPIOA_BASE + 0x400u * (uint32_t)(pin >> 4));
}

/* Enable the AHB4 clock of the pin's port (RCC_AHB4ENR bits 0..7 = GPIOA..H).
 * The read-back guarantees the enable has reached the RCC before the first
 * access to the port registers. */
static void gpio_clock(int pin)
{
    RCC->AHB4ENR |= 1u << (pin >> 4);
    (void)RCC->AHB4ENR;
}

/*
 * Full configuration of one pin.
 *   mode_af  0-15: alternate function number (MODER = 10b, AFRL/AFRH set);
 *            PIN_OUT / PIN_OD: general-purpose output (01b);
 *            PIN_IN: input (00b);
 *            anything else (PIN_AN): analog (11b), which also forces no pull.
 *   pull     PULL_NONE / PULL_UP / PULL_DOWN (PUPDR encoding).
 *   od       open-drain output type (forced on for PIN_OD).
 *   speed    OSPEEDR value 0 (low) .. 3 (very high).
 * AF, output type, speed and pull are written before MODER, so the pin only
 * switches to its new mode once everything else is in place. The output level
 * (ODR) is not touched: callers set it beforehand with gpio_write().
 */
static void gpio_setup(int pin, int mode_af, int pull, bool od, int speed)
{
    GPIO_TypeDef *g = gpio_port(pin);
    int n = pin & 15;
    gpio_clock(pin);
    uint32_t moder;
    if (mode_af <= 15) {
        moder = 2;
        volatile uint32_t *afr = &g->AFR[n >> 3];
        *afr = (*afr & ~(0xFu << ((n & 7) * 4))) | ((uint32_t)mode_af << ((n & 7) * 4));
    } else if (mode_af == PIN_OUT || mode_af == PIN_OD) {
        moder = 1;
        od = od || mode_af == PIN_OD;
    } else if (mode_af == PIN_IN) {
        moder = 0;
    } else {
        moder = 3;
        pull = PULL_NONE;
    }
    g->OTYPER = (g->OTYPER & ~(1u << n)) | ((od ? 1u : 0u) << n);
    g->OSPEEDR = (g->OSPEEDR & ~(3u << (2 * n))) | ((uint32_t)speed << (2 * n));
    g->PUPDR = (g->PUPDR & ~(3u << (2 * n))) | ((uint32_t)pull << (2 * n));
    g->MODER = (g->MODER & ~(3u << (2 * n))) | (moder << (2 * n));
}

/* Public configuration entry: push-pull; very-high speed for alternate
 * functions (bus pins), medium speed for plain GPIO. */
void gpio_config(int pin, int mode_af, int pull)
{
    gpio_setup(pin, mode_af, pull, false, mode_af <= 15 ? 3 : 1);
}

/* Change only the slew rate (OSPEEDR, 0..3) of an already configured pin. */
void gpio_set_speed(int pin, int speed)
{
    GPIO_TypeDef *g = gpio_port(pin);
    int n = pin & 15;
    g->OSPEEDR = (g->OSPEEDR & ~(3u << (2 * n))) | ((uint32_t)speed << (2 * n));
}

/* Analog mode: input buffer and pulls disconnected. Lowest-leakage state, and
 * the one used for every pin facing a powered-down rail (3V3_BUS, SD), where
 * it neither sources nor sinks current. */
void gpio_set_analog(int pin)
{
    gpio_setup(pin, PIN_AN, PULL_NONE, false, 0);
}

/* true if net name `s` begins with prefix `p` */
static bool starts(const char *s, const char *p)
{
    return strncmp(s, p, strlen(p)) == 0;
}

/* Configure every "simple" pin. Bus pins (FMC, QSPI, SDMMC) are configured by
 * their drivers; oscillator, SWD and USB pins are left at reset state. */
void gpio_config_from_table(void)
{
    /* Safe output levels first (ODR before MODER avoids glitches). */
    RCC->AHB4ENR |= 0xFFu; /* GPIOA..GPIOH */
    (void)RCC->AHB4ENR;
    gpio_write(P_BUS_EN, 0);
    gpio_write(P_SD_EN, 0);
    gpio_write(P_LCD_RESET, 0);
    gpio_write(P_TP_RST, 0);
    gpio_write(P_VBAT_SENSE_EN, 0);
    gpio_write(P_LED_DEBUG, 0);

    /* Reset lines low (not floating) while 3V3_BUS is off: the panel and touch
     * controller then see reset asserted before and during power-up. */
    gpio_config(P_LCD_RESET, PIN_OUT, PULL_NONE);
    gpio_config(P_TP_RST, PIN_OUT, PULL_NONE);
    gpio_set_analog(P_LCD_TE);
    gpio_set_analog(P_TP_INT);
    gpio_set_analog(P_TP_SCL);
    gpio_set_analog(P_TP_SDA);

    for (size_t i = 0; i < sizeof g_pins / sizeof g_pins[0]; i++) {
        const pin_def_t *p = &g_pins[i];
        const char *n = p->net;
        if (p->mode == PIN_OSC || starts(n, "FMC_") || starts(n, "QSPI_") || (starts(n, "SD_D") && strcmp(n, "SD_DETECT")) ||
            starts(n, "SD_C") || starts(n, "SW") || starts(n, "USB_D") || !strcmp(n, "LCD_CS") ||
            !strcmp(n, "LCD_RD") || !strcmp(n, "LCD_WR") || !strcmp(n, "LCD_DC") ||
            starts(n, "DBG_TX2") || starts(n, "DBG_RX2") || starts(n, "DBG_MCO") ||
            /* 3V3_BUS-powered signals: set up by bus_power()/touch/lcd drivers,
             * never driven high (or left as floating inputs) while the rail is off */
            starts(n, "TP_") || !strcmp(n, "LCD_TE") || !strcmp(n, "LCD_RESET") ||
            /* LED anode selector: left at reset (analog, on its pull-downs = both
             * paths off) until bus_power(true) drives it */
            starts(n, "BL_SRC_"))
            continue;
        /* Keypad: open-drain rows driven low at idle and pulled-up columns, so
         * any pressed key pulls its column low (keypad.c arms EXTI on that).
         * SD_DETECT gets the same internal pull-up. */
        if (starts(n, "KEY_ROW")) {
            gpio_write(p->pin, 0); /* idle: rows low so any key pulls its column low */
            gpio_setup(p->pin, PIN_OD, PULL_NONE, true, 0);
        } else if (starts(n, "KEY_COL") || !strcmp(n, "SD_DETECT")) {
            gpio_setup(p->pin, PIN_IN, PULL_UP, false, 0);
        } else if (starts(n, "I2C_")) {
            gpio_setup(p->pin, p->mode, PULL_NONE, true, 1); /* external 4.7k pull-ups */
        } else if (!strcmp(n, "UART_RX")) {
            gpio_setup(p->pin, p->mode, PULL_UP, false, 1);
        } else {
            /* as the table says: AF pins (USART1 TX) at high speed, plain
             * GPIO outputs/inputs at low speed */
            gpio_setup(p->pin, p->mode, PULL_NONE, false, p->mode <= 15 ? 2 : 0);
        }
    }
}
