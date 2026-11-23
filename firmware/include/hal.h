/*
 * hal.h — the platform layer. Implemented twice:
 *   platform/stm32h743/  real hardware
 *   platform/sim/        macOS simulator
 *   platform/web/        WebAssembly simulator
 * Everything above this line (core/, app/, cas) is portable C11 and must only
 * talk to hardware through these functions.
 *
 * Calling context: unless stated otherwise, call these from the main loop
 * (core/calc.c), not from interrupt handlers. Status returns follow one
 * convention: 0 = success, negative = failure.
 */
#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LCD_WIDTH  320
#define LCD_HEIGHT 240

/* ------------------------------------------------------------- system --- */
void hal_init(void);                 /* clocks, memories, display, keypad, console */
const char *hal_platform_name(void); /* "stm32h743", "sim" or "web" */
void hal_reset(void);                /* software reset; does not return on hardware */
void hal_enter_bootloader(void);     /* ROM USB-DFU / UART bootloader; does not return on hardware */

/* Big memories supplied by the platform (SDRAM on hardware). */
uint16_t *hal_framebuffer(void);     /* LCD_WIDTH*LCD_HEIGHT RGB565, row-major */
void *hal_arena(size_t *size);       /* scratch arena for the app + CAS (tens of MB on hw) */

/* Interrupt masking for code shared with ISRs (no-ops in the simulators).
 * hal_irq_save() disables interrupts and returns the previous state, which
 * must be passed back to hal_irq_restore(); pairs may nest. */
uint32_t hal_irq_save(void);
void hal_irq_restore(uint32_t state);

/* -------------------------------------------------------------- timer --- */
uint32_t hal_millis(void);           /* monotonic ms since boot (wraps after 49 days) */
/* Block for `ms` milliseconds (hardware: WFI on the 1 ms tick, so the delay is
 * between ms-1 and ms; never with interrupts masked). The simulators advance
 * their virtual clock instead of sleeping when time is virtual. */
void hal_delay_ms(uint32_t ms);
uint32_t hal_cycles(void);           /* free-running CPU cycle counter (profiling); wraps */
uint32_t hal_cycles_per_us(void);    /* hal_cycles() ticks per microsecond (400 at 400 MHz) */

/* ------------------------------------------------------------ display --- */
/* Copy a rectangle of `fb` (stride LCD_WIDTH) to the panel. */
void hal_display_flush(const uint16_t *fb, int x, int y, int w, int h);
void hal_display_enable(bool on);    /* panel sleep out/in + backlight on/off */
void hal_backlight_set(int percent); /* 0..100 % of the backlight current limit */
/* Boot/app default brightness. Hardware: 45 % of 90 mA = ~40 mA, ~125 cd/m2 on
 * the ER-TFT032IPS-3.2 (280 cd/m2 at ~90 mA), comfortable indoors. */
#define HAL_BACKLIGHT_DEFAULT_PCT 45
int hal_backlight_get(void);

/* ------------------------------------------------------------- keypad --- */
/* Key events are pushed into core/keyq.h by the platform (scanner ISR or sim).
 * This returns the raw, debounced state: bit (row*7+col) set = pressed;
 * bit 49 = ON key. Used by the console `keys` command. */
uint64_t hal_keypad_raw(void);
/* How long (ms) every key in `mask` (hal_keypad_raw() bits) has been held
 * down together, as of now, by the debounced state; 0 if any of them is up.
 * Hardware: the scanner timestamps each debounced press, and it runs from the
 * first SysTick after the pins are configured, so keys held through the reset
 * count from ~15 ms after it. Simulators: 0 (console-held keys are timed by
 * the caller). Used by the safe-mode key check (core/safemode.h). */
uint32_t hal_keypad_held_ms(uint64_t mask);

/* -------------------------------------------------------------- power --- */
/* Sleep until something happens (interrupt on hw; input or ~10 ms in the sim). */
void hal_idle(void);
/* Power off = hibernate: save the session, switch rails off, MCU Standby,
 * ON key wakes (= reset). In the simulators it saves the session, blanks the
 * panel and returns; core/calc.c then ignores everything but ON. */
void hal_power_off(void);
/* Implemented by the application (app/session.c): save the session to the
 * storage before the power goes off. Every platform calls it at the start of
 * hal_power_off() / Standby entry (the hardware also has a weak no-op for the
 * bring-up image, which has no app). Returns 0 on success. */
int platform_hibernate_save(void);

typedef struct {
    bool valid_pmon;        /* INA226 present */
    float vsys_v;           /* INA226 bus voltage (VSYS) */
    float isys_ma;          /* INA226 current through the 0.1 ohm shunt */
    float psys_mw;          /* INA226 power register (VSYS * ISYS) */
    float vbat_v;           /* battery pack (switched divider), NAN if unknown */
    float v3v3_v;           /* 3V3 via VREFINT */
    float v3v3_bus_v;       /* 3V3_BUS rail monitor */
    float v3v3_sd_v;        /* 3V3_SD rail monitor */
    float bl_ma;            /* backlight sink current */
    float temp_c;           /* MCU die temperature, NAN if unknown */
    bool usb_present;       /* VBUS sense */
} hal_telemetry_t;
/* Snapshot of the power and rail monitors (console `power`, the app's battery
 * indicator). Fills every field; voltages in V, currents in mA. */
void hal_telemetry(hal_telemetry_t *t);
/* VBUS sense alone (a GPIO read, cheap enough to poll on every app tick). */
bool hal_usb_present(void);

/* USB device (hardware: platform/stm32h743/usb.c; a model in the simulators).
 * It starts by itself when VBUS appears and stops when it goes; its mass
 * storage is core/usbdisk.h, its serial port a second console link.
 *   hal_usb_configured: a computer has configured the device and the bus is
 *     not suspended (a charger alone never configures it). While true the
 *     calculator does not power itself off.
 *   hal_usb_poll: service the device from inside a long computation (the CAS
 *     poll hook); cheap, at most once per millisecond.
 *   hal_usb_detach: leave the bus now (before a reset into the ROM
 *     bootloader, so the computer sees the device go).
 *   hal_usb_reconnect: leave the bus for `ms` and come back (the main loop
 *     reconnects), so the computer enumerates the device again and mounts
 *     its drives afresh; false if no computer has it configured. */
bool hal_usb_configured(void);
void hal_usb_poll(void);
void hal_usb_detach(void);
bool hal_usb_reconnect(uint32_t ms);
/* The diagnostics as (name, value) lines: VBUS, device state, the last
 * error, clocks, interrupt and event counts, the last SETUP request, the
 * console port, the drives (MEM > USB status, console `usb`). Cheap. */
void hal_usb_status(void (*kv)(const char *k, const char *v, void *u), void *u);

/* Battery thresholds: 4x AAA in series, measured at VBAT_SENSE (pack voltage
 * after the reverse-protection FET, switched divider). The 3.3 V buck
 * (TPS62162, 100 % duty near the end) needs VSYS >= ~3.5 V at load, and the
 * cells -> VSYS path (two P-FETs + 0.1 ohm shunt, ~0.3 ohm) drops ~0.1 V at
 * 0.3 A, hence: the status bar shows empty at 3.6 V and turns red below 4.0 V;
 * the hardware logs a warning below 4.0 V and saves + powers off below 3.6 V
 * (on battery only; platform/stm32h743/power.c). The INA226 alert on VSYS
 * (3.55 V) is the fast backstop under load. */
#define HAL_BATT_FULL_V  6.0f /* 4 x 1.5 V fresh alkaline */
#define HAL_BATT_WARN_V  4.0f /* 1.0 V per cell */
#define HAL_BATT_EMPTY_V 3.6f /* 0.9 V per cell: save the session and power off */

/* ------------------------------------------------------------ storage --- */
/* Non-volatile storage (QSPI NOR on hw, a file in the sim). Erase granularity
 * is HAL_STORAGE_SECTOR bytes; write requires erased (0xFF) bytes. 0 = ok.
 * Offsets are byte addresses from the start of the storage area; requests
 * beyond hal_storage_size() fail. Used by app/session.c through core/pstore.c. */
#define HAL_STORAGE_SECTOR 4096u
size_t hal_storage_size(void);                                   /* bytes */
int hal_storage_read(uint32_t off, void *buf, size_t n);
int hal_storage_erase(uint32_t off, size_t n);                   /* off, n: whole sectors */
int hal_storage_write(uint32_t off, const void *buf, size_t n);  /* NOR program: only clears bits */
/* Reset the storage device as at boot (hardware: the QSPI controller and the flash chip,
 * then memory-mapped mode again); after a failed or unverified write. 0 = ok. On hardware
 * erase and write already reset and retry once by themselves (hal_stm32.c). */
int hal_storage_reset(void);
/* Execute-in-place view of [off, off+n) of the storage: hardware, a pointer
 * into the memory-mapped QSPI window (0x90000000 + off; MPU region 4:
 * read-only, executable while memory-mapped); NULL if the chip is not
 * working, and always NULL in the simulators (they link code modules into
 * the host binary instead: app/modx.c). Valid until the next erase/write. */
const void *hal_storage_xip(uint32_t off, size_t n);
/* Make code that was just installed or moved in the QSPI visible to
 * instruction fetches: hardware, I-cache invalidate + DSB + ISB (QSPI writes
 * bypass the caches); nothing in the simulators. */
void hal_code_sync(void);
/* Make [base, base + size) of the SDRAM executable for code-load modules
 * (app/modx.c): hardware, MPU region 5 (Normal, write-back, executable; size a
 * power of two, base aligned to it); nothing in the simulators. hal_code_sync
 * also writes the D-cache back, so code copied there reaches the fetches. */
void hal_code_region(void *base, size_t size);


/* ---------------------------------------------------------- recovery --- */
/* Two words that survive a reset but not a power loss (hardware: .noinit
 * DTCM, lost in Standby; simulators: a static, kept across `reboot`), used by
 * core/safemode.c to count boots that never finished. NULL if none. */
uint32_t *hal_boot_record(void);

/* Why the MCU last started (hardware: RCC_RSR, PWR standby flag and the
 * fault handler's note, read once at boot; simulators: power-on). */
enum {
    HAL_RESET_POWERON = 0, /* power applied (batteries in, USB plugged) */
    HAL_RESET_WAKE,        /* switched on with ON (wake from Standby) */
    HAL_RESET_SOFTWARE,    /* deliberate restart (console reset, update) */
    HAL_RESET_FAULT,       /* a fault handler reset after a crash */
    HAL_RESET_WATCHDOG,    /* the independent watchdog: the firmware hung */
    HAL_RESET_PIN,         /* the NRST pin (debugger, reset button) */
    HAL_RESET_BROWNOUT,    /* supply dipped below the brown-out level */
    HAL_RESET_OTHER,
};
int hal_reset_cause(void);
/* The fault handler's record of the last crash since power-on, as one line of
 * text (count, pc, lr, fault status); false if there is none (simulators:
 * always). Shown on the About page so a crash can be read off the screen. */
bool hal_crash_text(char *out, size_t n);

/* Watchdog: a hang (no hal_watchdog_kick() for HAL_WATCHDOG_MS) resets the
 * MCU, which counts as a failed boot for safe mode (core/safemode.h).
 * Hardware: IWDG1 on the LSI (~2 s, LSI tolerance 1.9-2.2 s), started after
 * hal_init() by calc_setup(); not started when the option bytes let it run in
 * Standby (it would wake the switched-off calculator), see hal_stm32.c.
 * Simulators: a model on the virtual clock (hal_watchdog_expired()).
 * The main loop kicks it every iteration, the CAS through its poll hook
 * (app_cas_poll), and long blocking operations (QSPI erase, microSD install,
 * session save, Standby entry) in their loops. */
#define HAL_WATCHDOG_MS 2000u
void hal_watchdog_start(void);
void hal_watchdog_kick(void);
bool hal_watchdog_running(void);
/* Simulators: true once HAL_WATCHDOG_MS passed without a kick (the caller then
 * restarts the app as the reset would); hardware: always false (the IWDG
 * resets by itself). */
bool hal_watchdog_expired(void);
/* One line for MEM > About / console `wdg`: "on (2 s, frozen in Stop and
 * Standby)", "off: ..." with the reason. */
void hal_watchdog_status(char *out, size_t n);
/* The optional devices and how they answered at boot, as (device, status)
 * pairs, e.g. ("QSPI", "ok (W25Q128, JEDEC ef4018)"), ("Touch", "not found
 * (keys only)"): MEM > About and the console `devices`. */
void hal_device_report(void (*kv)(const char *dev, const char *status, void *user), void *user);

/* ------------------------------------------------------------ microSD --- */
/* Block device of 512-byte blocks (file system: core/fat.h). Hardware:
 * SDMMC1, 4-bit, polled; the card is powered only between open and close.
 * Simulators: a disk image (calcsim --sd FILE) or a RAM card made by the
 * console command `sd insert MB` (tests). 0 = ok. */
#define HAL_SD_BLOCK 512u
bool hal_sd_present(void);                 /* card in the slot */
int hal_sd_open(uint32_t *nblocks);        /* power up + initialise (no-op if open) */
/* Transfer `count` 512-byte blocks starting at block `lba`; requires a
 * successful hal_sd_open(). */
int hal_sd_read(uint32_t lba, void *buf, uint32_t count);
int hal_sd_write(uint32_t lba, const void *buf, uint32_t count);
void hal_sd_close(void);                   /* power down */
/* simulators: insert a blank RAM card of `mb` MB (0 = eject); -1 on hardware */
int hal_sd_sim_insert(unsigned mb);

/* ------------------------------------------------------------ console --- */
/* Non-blocking; returns bytes read (0 if none). Returns -1 at end of input
 * (simulator stdin EOF only), which makes the main loop exit. */
int hal_console_read(char *buf, int max);
void hal_console_write(const char *s, size_t n); /* blocking */

/* -------------------------------------------------------------- touch --- */
/* Touch events reach core/touch.h (touch_report) from the platform. This says
 * whether a touch controller is present and working (hardware: the FT6236 on
 * I2C4 answered; simulators: always true). Touch is optional: the app works
 * entirely by keys. */
bool hal_touch_available(void);

/* ----------------------------------------------------------- services --- */
/* Called by the main loop on every iteration (main context, never from an
 * ISR): deferred driver work such as reading the touch controller after its
 * interrupt, or acting on a power-monitor alert. No-op in the simulators. */
void hal_service(void);
/* C stack the CAS may use (cas_set_stack_limit), in bytes; 0 = CAS default. */
size_t hal_cas_stack_limit(void);

/* --------------------------------------------- platform console hooks --- */
/* Each platform provides extra debug-console commands (memtest, clocks, ...). */
/* Declared in core/console.h as: extern const console_cmd hal_console_cmds[];
 * (terminated by {0}). */

#endif
