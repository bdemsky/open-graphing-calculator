/*
 * board.h — calculator rev A pin assignment and board constants
 * (final hardware: EastRising ER-TFT032IPS-3.2 panel (ST7789, 240x320 IPS) with
 * the ER-TPC032-3.1 capacitive touch panel (FocalTech FT6236 on the LCD's own
 * 40-pin FPC, J3 pins 1-4), shared 3V3_BUS rail for SDRAM + LCD + touch,
 * TE on PG9, PMON_ALERT on PF10).
 *
 * The PIN() table below is the single source of truth for the firmware's
 * GPIO configuration. tools/check_pins.py parses it and checks every entry
 * against hardware/pinmap/pinmap.py and ST's open pin data (AF numbers).
 *
 *   PIN(net, port, pin, af)   af: 0-15 alternate function, or one of
 *     PIN_OUT  push-pull output     PIN_OD   open-drain output
 *     PIN_IN   input                PIN_AN   analog
 *     PIN_OSC  oscillator pin (no GPIO config)
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

/* Non-AF pin modes, numbered above the AF range 0-15 so one `af` argument
 * (gpio_config's mode_af) can carry either. */
#define PIN_OUT 16
#define PIN_OD  17
#define PIN_IN  18
#define PIN_AN  19
#define PIN_OSC 20

/* X-macro: each user passes its own PIN(net, port, pin, af) expansion
 * (the enum below, gpio.c's run-time pin table); tools/check_pins.py reads
 * the rows as text.
 * AF numbers per DS12110 "Alternate function" tables: 4 I2C2/I2C4, 7 USART1/
 * USART6, 9/10 QUADSPI, 10 OTG_FS, 12 FMC and SDMMC1. */
/* clang-format off */
#define BOARD_PINS(PIN) \
    PIN(HSE_IN,        H,  0, PIN_OSC) \
    PIN(HSE_OUT,       H,  1, PIN_OSC) \
    PIN(LSE_IN,        C, 14, PIN_OSC) \
    PIN(LSE_OUT,       C, 15, PIN_OSC) \
    PIN(SWDIO,         A, 13, 0) \
    PIN(SWCLK,         A, 14, 0) \
    PIN(SWO,           B,  3, 0) \
    PIN(UART_TX,       A,  9, 7) \
    PIN(UART_RX,       A, 10, 7) \
    PIN(KEY_ON,        A,  0, PIN_IN) \
    PIN(FMC_D0,        D, 14, 12) \
    PIN(FMC_D1,        D, 15, 12) \
    PIN(FMC_D2,        D,  0, 12) \
    PIN(FMC_D3,        D,  1, 12) \
    PIN(FMC_D4,        E,  7, 12) \
    PIN(FMC_D5,        E,  8, 12) \
    PIN(FMC_D6,        E,  9, 12) \
    PIN(FMC_D7,        E, 10, 12) \
    PIN(FMC_D8,        E, 11, 12) \
    PIN(FMC_D9,        E, 12, 12) \
    PIN(FMC_D10,       E, 13, 12) \
    PIN(FMC_D11,       E, 14, 12) \
    PIN(FMC_D12,       E, 15, 12) \
    PIN(FMC_D13,       D,  8, 12) \
    PIN(FMC_D14,       D,  9, 12) \
    PIN(FMC_D15,       D, 10, 12) \
    PIN(FMC_A0,        F,  0, 12) \
    PIN(FMC_A1,        F,  1, 12) \
    PIN(FMC_A2,        F,  2, 12) \
    PIN(FMC_A3,        F,  3, 12) \
    PIN(FMC_A4,        F,  4, 12) \
    PIN(FMC_A5,        F,  5, 12) \
    PIN(FMC_A6,        F, 12, 12) \
    PIN(FMC_A7,        F, 13, 12) \
    PIN(FMC_A8,        F, 14, 12) \
    PIN(FMC_A9,        F, 15, 12) \
    PIN(FMC_A10,       G,  0, 12) \
    PIN(FMC_A11,       G,  1, 12) \
    PIN(FMC_A12,       G,  2, 12) \
    PIN(FMC_BA0,       G,  4, 12) \
    PIN(FMC_BA1,       G,  5, 12) \
    PIN(FMC_NBL0,      E,  0, 12) \
    PIN(FMC_NBL1,      E,  1, 12) \
    PIN(FMC_SDCLK,     G,  8, 12) \
    PIN(FMC_SDNCAS,    G, 15, 12) \
    PIN(FMC_SDNRAS,    F, 11, 12) \
    PIN(FMC_SDNWE,     C,  0, 12) \
    PIN(FMC_SDCKE1,    B,  5, 12) \
    PIN(FMC_SDNE1,     B,  6, 12) \
    PIN(LCD_CS,        D,  7, 12) \
    PIN(LCD_RD,        D,  4, 12) \
    PIN(LCD_WR,        D,  5, 12) \
    PIN(LCD_DC,        D, 11, 12) \
    PIN(LCD_RESET,     D,  3, PIN_OUT) \
    PIN(LCD_TE,        G,  9, PIN_IN) \
    PIN(LCD_BL_DAC,    A,  4, PIN_AN) \
    PIN(QSPI_CLK,      B,  2, 9) \
    PIN(QSPI_NCS,      G,  6, 10) \
    PIN(QSPI_IO0,      F,  8, 10) \
    PIN(QSPI_IO1,      F,  9, 10) \
    PIN(QSPI_IO2,      F,  7, 9) \
    PIN(QSPI_IO3,      F,  6, 9) \
    PIN(SD_D0,         C,  8, 12) \
    PIN(SD_D1,         C,  9, 12) \
    PIN(SD_D2,         C, 10, 12) \
    PIN(SD_D3,         C, 11, 12) \
    PIN(SD_CK,         C, 12, 12) \
    PIN(SD_CMD,        D,  2, 12) \
    PIN(SD_DETECT,     G,  7, PIN_IN) \
    PIN(USB_DM,        A, 11, 10) \
    PIN(USB_DP,        A, 12, 10) \
    PIN(USB_VBUS_SENSE,A,  2, PIN_IN) \
    PIN(BUS_EN,        G,  3, PIN_OUT) \
    PIN(SD_EN,         G, 10, PIN_OUT) \
    PIN(VBAT_SENSE,    A,  3, PIN_AN) \
    PIN(VBAT_SENSE_EN, A,  1, PIN_OUT) \
    PIN(LED_DEBUG,     B,  8, PIN_OUT) \
    PIN(I2C_SCL,       B, 10, 4) \
    PIN(I2C_SDA,       B, 11, 4) \
    PIN(PMON_ALERT,    F, 10, PIN_IN) \
    PIN(MON_3V3_BUS,   A,  6, PIN_AN) \
    PIN(MON_3V3_SD,    C,  4, PIN_AN) \
    PIN(BL_SENSE,      C,  5, PIN_AN) \
    PIN(BL_SRC_3V3,    C, 13, PIN_OUT) \
    PIN(BL_SRC_VSYS,   D,  6, PIN_OUT) \
    PIN(DBG_TX2,       C,  6, 7) \
    PIN(DBG_RX2,       C,  7, 7) \
    PIN(DBG_MCO,       A,  8, 0) \
    PIN(DBG_IO0,       A, 15, PIN_OUT) \
    PIN(DBG_IO1,       B,  9, PIN_OUT) \
    PIN(DBG_IO2,       B, 15, PIN_OUT) \
    PIN(DBG_IO3,       A,  5, PIN_OUT) \
    PIN(DBG_IO4,       B,  7, PIN_OUT) \
    PIN(TP_SCL,        D, 12, 4) \
    PIN(TP_SDA,        D, 13, 4) \
    PIN(TP_INT,        C,  1, PIN_IN) \
    PIN(TP_RST,        A,  7, PIN_OUT) \
    PIN(KEY_ROW0,      G, 11, PIN_OD) \
    PIN(KEY_ROW1,      G, 12, PIN_OD) \
    PIN(KEY_ROW2,      G, 13, PIN_OD) \
    PIN(KEY_ROW3,      G, 14, PIN_OD) \
    PIN(KEY_ROW4,      B,  0, PIN_OD) \
    PIN(KEY_ROW5,      B,  1, PIN_OD) \
    PIN(KEY_ROW6,      B,  4, PIN_OD) \
    PIN(KEY_COL0,      E,  2, PIN_IN) \
    PIN(KEY_COL1,      E,  3, PIN_IN) \
    PIN(KEY_COL2,      E,  4, PIN_IN) \
    PIN(KEY_COL3,      E,  5, PIN_IN) \
    PIN(KEY_COL4,      E,  6, PIN_IN) \
    PIN(KEY_COL5,      B, 12, PIN_IN) \
    PIN(KEY_COL6,      B, 13, PIN_IN)
/* clang-format on */

/* Pin identifiers: P_<net> = (port index << 4) | pin, port A = 0 */
#define PORT_IDX_A 0
#define PORT_IDX_B 1
#define PORT_IDX_C 2
#define PORT_IDX_D 3
#define PORT_IDX_E 4
#define PORT_IDX_F 5
#define PORT_IDX_G 6
#define PORT_IDX_H 7
#define PIN_ENUM(net, port, pin, af) P_##net = (PORT_IDX_##port << 4) | (pin),
enum board_pin { BOARD_PINS(PIN_ENUM) };
#undef PIN_ENUM

/* ---- EXTI lines of the interrupt inputs ----
 * EXTI lines are shared by pin NUMBER across ports, and the vector depends on
 * the line (EXTI1, EXTI9_5, EXTI15_10). If the pin map moves one of these, the
 * asserts fail and point here: update the line, the vector/handler, and make
 * sure no key column (lines 2-6, 12, 13) uses the same number. */
#define EXTI_LINE_TP_INT     1  /* PC1  -> EXTI1_IRQHandler (touch_focaltech.c) */
#define EXTI_LINE_LCD_TE     9  /* PG9  -> EXTI9_5_IRQHandler (keypad.c -> lcd_te_irq) */
#define EXTI_LINE_PMON_ALERT 10 /* PF10 -> EXTI15_10_IRQHandler (keypad.c -> pmon_alert_irq) */
#define EXTI_LINE_KEY_ON     0  /* PA0  -> EXTI0 (wake from Stop) + WKUP1 (Standby) */
_Static_assert((P_TP_INT & 15) == EXTI_LINE_TP_INT, "TP_INT moved: update EXTI_LINE_TP_INT and its vector");
_Static_assert((P_LCD_TE & 15) == EXTI_LINE_LCD_TE, "LCD_TE moved: update EXTI_LINE_LCD_TE and its vector");
_Static_assert((P_PMON_ALERT & 15) == EXTI_LINE_PMON_ALERT, "PMON_ALERT moved: update EXTI_LINE_PMON_ALERT");
_Static_assert((P_KEY_ON & 15) == EXTI_LINE_KEY_ON && (P_KEY_ON >> 4) == PORT_IDX_A, "KEY_ON must stay PA0 (WKUP1)");
#define KEYCOL_LINE_CLASH(l) ((P_KEY_COL0 & 15) == (l) || (P_KEY_COL1 & 15) == (l) || (P_KEY_COL2 & 15) == (l) || \
                              (P_KEY_COL3 & 15) == (l) || (P_KEY_COL4 & 15) == (l) || (P_KEY_COL5 & 15) == (l) || \
                              (P_KEY_COL6 & 15) == (l))
_Static_assert(!KEYCOL_LINE_CLASH(EXTI_LINE_TP_INT) && !KEYCOL_LINE_CLASH(EXTI_LINE_LCD_TE) &&
                   !KEYCOL_LINE_CLASH(EXTI_LINE_PMON_ALERT) && !KEYCOL_LINE_CLASH(EXTI_LINE_KEY_ON),
               "a key column shares an EXTI line with TP_INT/LCD_TE/PMON_ALERT/KEY_ON");

/* ---- board constants ---- */
/* Clock tree targets (system.c): SYSCLK = CPU, HCLK = AXI/AHB (also the QSPI
 * and, on rev V, FMC kernel clock), PCLK = every APB bus. */
#define HSE_HZ          25000000u
#define SYSCLK_HZ       400000000u
#define HCLK_HZ         200000000u
#define PCLK_HZ         100000000u

/* Memory windows: FMC SDRAM bank 2 (SDNE1), QUADSPI memory-mapped region,
 * FMC NOR/SRAM bank 1 (NE1) for the LCD. A write to LCD_CMD_ADDR drives
 * DCX (A16) low = command, to LCD_DATA_ADDR high = data/parameter. */
#define SDRAM_BASE      0xD0000000u
#define SDRAM_SIZE      (32u * 1024u * 1024u)
#define QSPI_MMAP_BASE       0x90000000u
#define QSPI_SIZE       (16u * 1024u * 1024u)
#define LCD_CMD_ADDR    0x60000000u
#define LCD_DATA_ADDR   0x60020000u /* FMC_A16 = HADDR[17] in 16-bit mode */

/* Polarities (unverified items marked) */
#define SD_DETECT_ACTIVE_LOW 1 /* ASSUMPTION: CD switch closes to GND with a card inserted */
#define KEY_ON_ACTIVE_HIGH   1 /* ON switch to 3V3, 100k pull-down */

/* Analog front ends: multiply the ADC pin voltage by these to get the rail
 * (V) or divide by BL_SENSE_OHMS to get the backlight current (A). */
#define VBAT_DIV_RATIO  (147.0f / 47.0f) /* 100k over 47k */
#define RAIL_DIV_RATIO  2.0f             /* 100k / 100k */
#define BL_SENSE_OHMS   1.0f
/* Backlight sink setpoint: DAC1 (PA4, unbuffered, Ro 10-16 kohm) -> 10k -> BL_SET,
 * 1k to GND, 1 ohm sense: I_fs = 3.3 V * 1k / (10k + Ro + 1k) / 1 ohm. */
#define BL_DAC_FS_MA     (3300.0f * 1.0f / (10.0f + 13.0f + 1.0f)) /* ~138 mA nominal (Ro ~13k) */
#define BL_DAC_FS_MAX_MA (3300.0f * 1.0f / (10.0f + 10.0f + 1.0f)) /* ~157 mA worst case (Ro 10k): used
                                                                    * until a calibration succeeds */
#define BL_LIMIT_MA      90.0f  /* 100 % backlight = this current: the panel's normal LED current
                                 * (ER-TFT032IPS-3.2 datasheet 4.4: 5 LEDs in parallel, 90 mA
                                 * normal, 100 mA max, Vf 3.2 typ / 3.4 max at 100 mA) */
#define BL_ABS_MAX_MA    100.0f /* hard clamp for any setpoint (console included): datasheet max */
/* default brightness: HAL_BACKLIGHT_DEFAULT_PCT (hal.h), 45 % = ~40 mA */
#define BL_MIN_DETECT_MA 2.0f   /* below this at the 20 mA probe: no panel / open LED string */
/* LED anode (LCD_A) supply selector (replaces solder jumper JP1), two P-FETs switched by N-FETs:
 *   BL_SRC_3V3  (PC13) high = 3V3_BUS -> LCD_A ON; low / released (100k pull-down) = off (reset default)
 *   BL_SRC_VSYS (PD6)  high = VSYS -> LCD_A ON; low / released (100k pull-down) = off (reset default)
 * Both paths are OFF at reset, in Standby and whenever the pins are analog (a pull-up on PC13 let a
 * reset while on VSYS overlap the two paths), so the firmware always drives PC13 high to use 3V3.
 * Never both on (VSYS would be tied to 3V3_BUS): backlight_src_switch() sets the LED current to 0,
 * turns the active path off, waits BL_SRC_DEAD_US (gate RC ~65 us), turns the other on, waits
 * BL_SRC_ON_US, restores the setpoint. PC13 is in the backup domain (RM0433 "PC13, PC14, PC15":
 * fed through the power switch, sinks <= 3 mA, <= 2 MHz / 30 pF, never a current source, RTC
 * tamper/OUT must not be routed to it): it only drives an N-FET gate at the lowest GPIO speed. */
#define BL_SRC_DEAD_US     1000u   /* both paths off */
#define BL_SRC_ON_US       200u    /* new path on, before the setpoint returns */
#define BL_SRC_SAT_FRAC    0.90f   /* on 3V3: measured < 90 % of the setpoint = saturated -> VSYS */
#define BL_SRC_BACK_FRAC   0.80f   /* on VSYS: setpoint < 80 % of the learned 3V3 limit -> 3V3 */
#define BL_SRC_MIN_MA      10.0f   /* below this setpoint 3V3_BUS always regulates: never checked */
#define BL_SRC_SETTLE_MS   5u      /* setpoint change -> BL_SENSE check */
#define BL_SRC_RECHECK_MS  2000u   /* periodic re-check on 3V3 (Vf drifts with temperature) */
#define BL_SRC_MIN_GAP_MS  500u    /* auto mode switches at most this often */
/* Touch controller: FocalTech FT6236 (FT6x36 family) on the panel FPC, powered
 * from VCI = 3V3_BUS together with its I2C4/INT/RST pull-ups. */
#define TOUCH_I2C_ADDR  0x38
#define TOUCH_RAW_W     240              /* panel-native (portrait) coordinate ranges */
#define TOUCH_RAW_H     320

/* INA226 (I2C2, A0/A1 grounded -> 0x40). Current LSB 25 uA gives an
 * 819 mA full scale (32767 LSB); the calibration register follows the
 * datasheet formula CAL = 0.00512 / (Current_LSB x R_shunt). */
#define INA226_ADDR     0x40
#define INA226_SHUNT    0.1f
#define INA226_I_LSB_A  25e-6f
#define INA226_CAL      2048u            /* 0.00512 / (25e-6 * 0.1) */
/* One-point current calibration: true current / reported current. The sense
 * inputs go through matched 10 ohm filter resistors at the shunt pads (allowed
 * by the INA226 datasheet; their bias-current gain error is ~0.1 %), but the
 * Kelvin taps and copper around the 0.1 ohm 1 % shunt can still leave a few %
 * of gain error. Measure a known load (bench meter in series with VSYS_IN)
 * and set this; it scales current and power, not the bus voltage. */
#ifndef INA226_CURRENT_SCALE
#define INA226_CURRENT_SCALE 1.0f
#endif

/* LCD orientation (MADCTL). The panel (native 240x320 portrait, FPC at the
 * portrait bottom) is mounted landscape, rotated 90 deg clockwise, with the
 * FPC on the left, viewed from the front. All four options are landscape
 * (MV=1). The default was derived for the previous panel with the same
 * physical arrangement (portrait upright = MADCTL 0x80 there); the ER-TFT032's
 * native scan direction is not documented (datasheet Rev 1.0 has no init code),
 * so it is NOT verified. ST7789V2 datasheet fig. 33: 0x20/0xE0 are 180 deg
 * apart, and so are 0x60/0xA0; one pair is mirror images of the other. If
 * this panel is upright at MADCTL 0x00 (the usual ST7789 module convention),
 * the right value is 0xA0 (option 3) and 0x20 shows mirrored text:
 *   0: FPC left  (default)          MADCTL 0x20 (MV)
 *   1: FPC right (rotated 180)      MADCTL 0xE0 (MV|MX|MY)
 *   2: FPC left, mirrored X         MADCTL 0x60 (MV|MX)   <- if text is mirrored
 *   3: FPC left, mirrored Y         MADCTL 0xA0 (MV|MY)   <- if upside-down mirrored
 * Use console `lcd madctl <hex>` + `pattern orient` to find the right one. */
#ifndef LCD_ORIENTATION
#define LCD_ORIENTATION 3 /* rev A board 1 with the ER-TFT032: 0x20 mirrored, 0x60 upside down, 0xA0 upright */
#endif

/* Touch coordinate transform (FT6236 panel coordinates -> app screen
 * coordinates). The CTP reports in the panel's native portrait frame, ranges
 * TOUCH_RAW_W x TOUCH_RAW_H = 240 x 320. The ER-TPC032-3.1 drawing (front
 * view, FPC tail at the BOTTOM) marks "0,0" at the top-left and "240,320" at
 * the bottom-right: x 0..239 left->right, y 0..319 top->bottom. (Documented,
 * still to be confirmed with `tp watch` on the first board.) We mount the panel landscape with the FPC on the LEFT, i.e.
 * rotated 90 deg clockwise, so
 *     screen_x = 319 - raw_y,   screen_y = raw_x.
 * Steps, in order: SWAP_XY exchanges raw x/y (ranges become 320 x 240), then
 * FLIP_X mirrors x (319 - x) and FLIP_Y mirrors y (239 - y).
 * Check on hardware: `tp map <0-7>` changes it at runtime, `tp watch`
 * shows raw and mapped points. Touch the top-left corner of the screen: the
 * mapped point must be near 0,0. Expected candidates:
 *   5 = SWAP|FLIP_X  (default: CTP origin at the portrait top-left, away from the FPC,
 *                     as the ER-TPC032-3.1 drawing shows)
 *   3 = SWAP|FLIP_Y  (CTP origin at the portrait bottom-right, at the FPC end; common
 *                     on FT6x36 modules, e.g. Adafruit's FT6206 shield flips both axes)
 *   1 = SWAP / 7 = SWAP|FLIP_X|FLIP_Y if only one raw axis is reversed. */
#define TOUCH_SWAP_XY 1
#define TOUCH_FLIP_Y  2
#define TOUCH_FLIP_X  4
#ifndef TOUCH_ORIENTATION
#define TOUCH_ORIENTATION (TOUCH_SWAP_XY | TOUCH_FLIP_X)
#endif

#endif
