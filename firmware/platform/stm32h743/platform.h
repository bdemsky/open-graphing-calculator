/* Internal interfaces of the STM32H743 platform layer.
 *
 * The portable code (core/, app/, cas/) only sees include/hal.h, which
 * hal_stm32.c implements on top of the drivers declared here. These
 * declarations are shared by the drivers, the hardware console commands
 * (cmds.c) and the bring-up image (bringup/bringup_main.c). Pins are the
 * P_<net> identifiers from board.h; times are in ms/us as the names say;
 * functions returning int use 0 = success, < 0 = error unless noted. */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "board.h"
#include "stm32h7xx.h"

/* ---------------------------------------------------------------- gpio */
GPIO_TypeDef *gpio_port(int pin);
void gpio_config(int pin, int mode_af, int pull); /* mode_af: 0-15 AF, PIN_OUT/OD/IN/AN */
void gpio_config_from_table(void);                /* every pin in BOARD_PINS except FMC/SD/QSPI */
void gpio_set_analog(int pin);
#define PULL_NONE 0
#define PULL_UP   1
#define PULL_DOWN 2
/* gpio_write is an atomic set/reset through BSRR (safe against interrupts
 * touching other pins of the same port). gpio_toggle is a read-modify-write
 * of ODR: if an interrupt changes another pin of the same port between the
 * read and the write, that change is undone. */
static inline void gpio_write(int pin, int v)
{
    gpio_port(pin)->BSRR = v ? (1u << (pin & 15)) : (1u << ((pin & 15) + 16));
}
static inline int gpio_read(int pin) { return (gpio_port(pin)->IDR >> (pin & 15)) & 1; }
static inline void gpio_toggle(int pin) { gpio_port(pin)->ODR ^= 1u << (pin & 15); }
void gpio_set_speed(int pin, int speed); /* 0 low .. 3 very high */

/* -------------------------------------------------------------- system */
extern uint32_t SystemCoreClock;
void clock_init(void);           /* PWR supply/VOS + HSE + PLLs + bus clocks */
int clock_restore_after_stop(void);
void mpu_cache_init(void);
void systick_init(void);
void delay_us(uint32_t us);
uint32_t silicon_rev(void);      /* DBGMCU REV_ID */
const char *silicon_rev_name(void);
bool lse_ready(void);
bool hse_ready(void);
bool clock_css_nmi(void);        /* NMI: HSE CSS event -> rebuild clocks on HSI; false if not CSS */
bool clock_hse_failed_at_runtime(void);
uint32_t fmc_kernel_hz(void);    /* 200 MHz (HCLK3, rev V) or 180 MHz (PLL2R, rev Y/unknown) */
void mco1_select(int src, int prescaler); /* src: -1 off, 0 HSI, 1 LSE, 2 HSE, 3 PLL1Q, 4 HSI48 */
extern volatile uint32_t g_ticks_ms;
bool rtc_ensure(void);
void heartbeat_enable(bool on);

/* ----------------------------------------------------------------- fmc */
void bus_power(bool on);         /* 3V3_BUS load switch + FMC pins */
bool bus_powered(void);
int sdram_init(void);            /* returns 0 on success */
void sdram_self_refresh(bool enter);
void lcd_bank_init(void);
#define LCD_WR_ADDSET 5u            /* production LCD write timing (fmc.c table) */
#define LCD_WR_DATAST 15u
void lcd_bank_write_timing(unsigned addset, unsigned datast);
bool sdram_ok(void);
uint32_t sdram_sdclk_hz(void);   /* fmc_kernel_hz() / 2 */
uint32_t sdram_refresh_count(void);
void bus_power_assume_cold(void); /* boot after Standby: 3V3_BUS has been off long enough */
void bus_power_wait_off_time(void); /* block until 3V3_BUS has been off >= 20 ms */

/* ----------------------------------------------------------------- lcd */
void lcd_init(void);
void lcd_set_madctl(uint8_t v);
uint8_t lcd_get_madctl(void);
void lcd_write_cmd(uint8_t c, const uint8_t *d, int n); /* raw command + parameters */
uint32_t lcd_read_id(void);      /* RDDID: ID1<<16 | ID2<<8 | ID3 */
/* GRAM write/readback over n (<= 256) pixels at the top-left, with an SDRAM store
 * (inverted data) right after every LCD write to stress the shared bus. Returns
 * the number of mismatching pixels, or -1 if the readback format was not
 * recognised (raw words in *raw, 6 of them). *fmt names the decoder used. */
int lcd_ram_test(const uint16_t *pat, int n, volatile uint16_t *sdram_scratch, const char **fmt,
                 uint16_t *raw);
void lcd_sleep(bool sleep);
void lcd_flush(const uint16_t *fb, int x, int y, int w, int h);
bool lcd_te_level(void);
void lcd_te_init(void);          /* EXTI9 on PG9, counts frames */
uint32_t lcd_te_count(void);
void lcd_set_vsync(bool on);     /* wait for TE before large flushes */
bool lcd_get_vsync(void);
void lcd_pins_bus_off(void);
void lcd_te_irq(void);           /* from the EXTI9_5 vector */
void backlight_init(void);
void backlight_set_ma(float ma);
float backlight_get_ma(void);
float backlight_measure_ma(void);    /* BL_SENSE (PC5), averaged */
int backlight_calibrate(void);       /* closes the loop once: 0 ok, -1 no current (panel missing?) */
float backlight_gain(void);          /* measured / nominal, from the last calibration */
bool backlight_fault(void);
/* LED anode supply selector (BL_SRC_3V3 PC13 / BL_SRC_VSYS PD6, board.h) */
enum { BL_ANODE_OFF = 0, BL_ANODE_3V3 = 1, BL_ANODE_VSYS = 2, BL_ANODE_AUTO = 3 };
void backlight_src_bus_on(void);     /* bus_power(true), before BUS_EN high: 3V3 path on, VSYS off */
void backlight_src_bus_off(void);    /* bus_power(false), before BUS_EN low: VSYS off, pins released */
int backlight_src_switch(int src);   /* break-before-make to BL_ANODE_3V3 / _VSYS */
int backlight_src_set_mode(int mode);  /* BL_ANODE_AUTO or a forced path (console) */
int backlight_src_update(bool now, float *sat_out); /* auto policy step: path switched to, or 0 */
void backlight_service(void);        /* from hal_service(): auto policy, rate-limited */
int backlight_src(void);             /* active path (BL_ANODE_OFF while 3V3_BUS is off) */
int backlight_src_mode(void);
float backlight_src_sat_ma(void);    /* learned 3V3 limit (mA), 0 = not seen this boot */
void backlight_src_forget(void);

/* -------------------------------------------------------------- keypad */
void keypad_init(void);
void keypad_scan_tick(void);     /* from SysTick */
uint64_t keypad_state(void);
uint32_t keypad_held_ms(uint64_t mask); /* all keys of mask down together for this long (0: not all down) */
void keypad_prepare_wake(void);  /* all rows low, EXTI armed */
extern volatile uint32_t keypad_ghost_events;

/* ---------------------------------------------------------------- uart */
void uart_init(uint32_t baud);
void uart_set_baud(uint32_t baud);
void uart_set_baud_deferred(uint32_t baud);
uint32_t uart_get_baud(void);
int uart_read(char *buf, int max);
void uart_write(const char *s, size_t n);
void uart_write_raw_polled(const char *s); /* for fault handlers */

/* ----------------------------------------------------------- i2c/pmon */
void i2c_init(void);                                    /* I2C2 */
void i2c_bus_init(I2C_TypeDef *i2c);                    /* I2C2 or I2C4 */
void i2c4_set_fast(bool fast);                          /* I2C4: ~340 kHz instead of 100 kHz at next init */
bool i2c4_fast(void);
int i2c_bus_write(I2C_TypeDef *i2c, uint8_t addr, const uint8_t *data, int n);
int i2c_bus_read(I2C_TypeDef *i2c, uint8_t addr, uint8_t reg, uint8_t *buf, int n);
int i2c_bus_probe(I2C_TypeDef *i2c, uint8_t addr);
int i2c_write(uint8_t addr, const uint8_t *data, int n);
int i2c_read_reg16(uint8_t addr, uint8_t reg, uint16_t *val);
int i2c_write_reg16(uint8_t addr, uint8_t reg, uint16_t val);
int i2c_probe(uint8_t addr);
int ina226_init(void);
int ina226_read(float *vbus_v, float *i_ma, float *p_mw, float *vshunt_mv);
int ina226_shutdown(void);
int ina226_alert_bus_under(uint32_t mv); /* 0 = off */
int ina226_alert_ack(uint16_t *mask);
void pmon_alert_init(void);
void pmon_alert_irq(void);        /* from the EXTI15_10 vector */
void pmon_service(void);
extern volatile uint32_t g_pmon_alerts;

/* --------------------------------------------------------------- touch */
int touch_init(void);             /* probe the FT6236 (FocalTech) on I2C4, arm EXTI1; 0 = present */
const char *touch_chip_name(void); /* from ID_G_CIPHER: "FT6236", ..., "none" */
void touch_service(void);         /* main context: read points after INT */
void touch_pins_bus_off(void);    /* I2C4/INT analog, TP_RST low (before/after 3V3_BUS off) */
void touch_reset_assert(void);
void touch_reset_release(void);
void touch_sleep(void);
void touch_wake(void);
bool touch_hw_present(void);
void touch_map_point(int rx, int ry, int *sx, int *sy);
int touch_get_map(void);
void touch_set_map(int m);
int touch_console(int argc, char **argv);
bool ina226_present(void);

/* ----------------------------------------------------------------- adc */
void adc_init(void);
float adc_vdda(void);                 /* from VREFINT */
float adc_read_volts(int channel);    /* ADC1 channel, scaled by VDDA */
float adc_temp_c(void);
float adc_vbat(void);                 /* switched divider, ~16 ms */
#define ADC_CH_VBAT    15
#define ADC_CH_3V3_BUS 3
#define ADC_CH_3V3_SD  4
#define ADC_CH_BL      8

/* ---------------------------------------------------------------- qspi */
int qspi_init(void);
uint32_t qspi_read_id(void);          /* JEDEC 9F: manuf<<16 | type<<8 | cap */
int qspi_read(uint32_t addr, void *buf, size_t n);
int qspi_write(uint32_t addr, const void *buf, size_t n);
uint32_t qspi_forced_idle_count(void); /* aborts that left the peripheral busy (qspi.c leave_mmap) */
int qspi_erase_sector(uint32_t addr); /* 4 KB */
int qspi_erase_block64(uint32_t addr); /* 64 KB, addr 64 KB aligned */
uint32_t qspi_boot_id(void);          /* JEDEC ID read by qspi_init() (0 before) */
int qspi_read_status(uint8_t *sr1, uint8_t *sr2, uint8_t *sr3);
int qspi_memory_mapped(void);
uint32_t qspi_clock_hz(void);           /* 66.7 MHz default, 100 MHz with CONFIG_QSPI_100MHZ */
int qspi_set_clock_mhz(unsigned mhz);    /* 66 or 100 (console `qspi clock`), until the next boot */
void qspi_deep_power_down(void);
void qspi_release_power_down(void);
bool qspi_is_ok(void);

/* ------------------------------------------------------------------ sd */
void sd_init(void);
bool sd_card_present(void);
void sd_power(bool on);
int sd_identify(uint32_t *ocr, uint32_t cid[4], uint32_t *rca);

/* --------------------------------------------------------------- power */
void power_early_init(void);       /* boot reason, standby flag */
const char *power_boot_reason(void);
int power_reset_cause(void);       /* HAL_RESET_* (hal.h) decoded by power_early_init */
int power_enter_stop(uint32_t max_ms);
void power_enter_standby(void);
void power_set_pins_analog_for_off(void);
void battery_service(void);        /* main loop: VBAT_SENSE warning / power-off thresholds */
float battery_last_v(void);

/* -------------------------------------------------------------- faults */
/* Crash record written by fault_report() (startup.c) into .noinit DTCM; it
 * survives the reset that follows, valid while magic == CRASH_MAGIC. The
 * registers are the hardware-stacked frame plus the SCB fault status
 * registers. count is incremented on every fault, but nothing initialises
 * it, so after power-on it starts from whatever the RAM held. */
typedef struct {
    uint32_t magic;
    uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;
    uint32_t cfsr, hfsr, mmfar, bfar, exc_return;
    uint32_t count;
    uint32_t sp;         /* stack pointer before the fault (above the stacked frame) */
    uint32_t stack_used; /* main stack high-water mark at the fault, bytes */
} crash_record_t;
extern crash_record_t g_crash;
#define CRASH_MAGIC 0xC0FFEE02u
/* Set to BOOT_MAGIC_DFU before a software reset to make Reset_Handler jump
 * to the ST ROM bootloader (USB DFU) on the next boot. */
extern volatile uint32_t g_boot_magic;
#define BOOT_MAGIC_DFU 0xDF00B007u
/* Set by fault_report() just before its reset, so the next boot can tell a
 * crash from a deliberate software reset (both are SFTRSTF in RCC_RSR). */
extern volatile uint32_t g_reset_note;
#define RESET_NOTE_FAULT 0xFA017EDu

/* ---------------------------------------------------------------- iwdg */
/* IWDG1, ~2 s (iwdg.c). iwdg_early(): first thing in hal_init (only acts when
 * the option bytes select the hardware watchdog, which runs from reset with a
 * ~0.5 s default). iwdg_start(): at the end of the boot, if the option bytes
 * freeze it in Standby. */
void iwdg_early(void);
void iwdg_start(void);
void iwdg_kick(void);
bool iwdg_running(void);
bool iwdg_frozen_in_stop(void);
bool iwdg_frozen_in_standby(void);
uint32_t iwdg_stop_slice_ms(void); /* >0: Stop must wake this often to refresh it */
void iwdg_status(char *out, size_t n);
int iwdg_console(int argc, char **argv); /* console `optbytes` */

/* ----------------------------------------------------------------- usb */
/* usb.c: the USB device (TinyUSB). Not in the bring-up image. */
#if defined(CONFIG_USB) && !defined(BRINGUP)
#define HAVE_USB 1
#else
#define HAVE_USB 0
#endif
void usb_service(void);                          /* main loop: VBUS, start/stop, the stack */
int usb_console_read(char *buf, int max);        /* CDC console, non-blocking */
void usb_console_write(const char *s, size_t n); /* CDC console, while a terminal has it open */
void usb_status(int argc, char **argv);          /* console `usb [start]` */
void OTG_FS_IRQHandler(void);

/* Interrupt/exception handlers (vector table in startup.c) */
void SysTick_Handler(void);
void USART1_IRQHandler(void);
void EXTI0_IRQHandler(void);
void EXTI1_IRQHandler(void);
void EXTI2_IRQHandler(void);
void EXTI3_IRQHandler(void);
void EXTI4_IRQHandler(void);
void EXTI9_5_IRQHandler(void);
void EXTI15_10_IRQHandler(void);
void RTC_WKUP_IRQHandler(void);
void fault_report(uint32_t *frame, uint32_t exc_return, uint32_t kind);
int platform_hibernate_save(void);
void stack_paint(void);
uint32_t stack_used(void);           /* high-water mark of the main stack, bytes */
uint32_t stack_size(void);
bool io_compensation_ready(void);

#endif
