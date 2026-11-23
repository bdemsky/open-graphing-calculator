/*
 * ST7789 (COG) of the EastRising ER-TFT032IPS-3.2 (240x320 IPS) on FMC bank 1,
 * 8080 interface II, 16-bit (IM2..0 = 000 on the board), RGB565. TE (FMARK,
 * FPC pin 8) on PG9 (EXTI9).
 * Command register at 0x60000000 (DCX = FMC_A16 = 0), data at 0x60020000.
 * Backlight: DAC1 channel 1 (PA4, unbuffered) sets a constant-current sink.
 *
 * The panel is a plain memory-mapped peripheral: each 16-bit store to
 * LCD_DATA is one WRX pulse on the shared FMC data bus (the same D0-D15 the
 * SDRAM uses), with DCX taken from address bit A16. The bus timing (FMC
 * BTR/BWTR, ST7789 "8080 interface" AC characteristics) is set up in fmc.c,
 * and the MPU maps the bank as Device memory (system.c) so the CPU never
 * reads it speculatively or merges/reorders the stores. The framebuffer is
 * kept in SDRAM (hal_stm32.c) and copied to GRAM by lcd_flush() with CPU
 * stores; there is no DMA, so a flush blocks the caller.
 *
 * ST7789 command codes used below (Sitronix ST7789V2 datasheet v1.0, section 9
 * "Command"; the panel's controller per EastRising's document set): 0x04 RDDID, 0x10/0x11 SLPIN/SLPOUT, 0x21 INVON, 0x28/0x29
 * DISPOFF/DISPON, 0x2A/0x2B CASET/RASET, 0x2C RAMWR, 0x2E RAMRD, 0x35 TEON,
 * 0x36 MADCTL, 0x3A COLMOD, 0xB2.. panel/power control.
 *
 * The second half of the file drives the backlight constant-current sink and
 * its LED anode supply selector (see the comment block above dac_for()).
 */
#include "hal.h"
#include "platform.h"

#define LCD_CMD  (*(volatile uint16_t *)LCD_CMD_ADDR)
#define LCD_DATA (*(volatile uint16_t *)LCD_DATA_ADDR)

#ifndef LCD_PANEL_TUNING
#define LCD_PANEL_TUNING 1 /* 0: VCOM/VRH/gamma left at the ST7789 power-on defaults */
#endif

/* MADCTL values for LCD_ORIENTATION 0-3 (board.h explains each). */
#define MADCTL_FOR(o) ((o) == 0 ? 0x20 : (o) == 1 ? 0xE0 : (o) == 2 ? 0x60 : 0xA0)
static uint8_t g_madctl = MADCTL_FOR(LCD_ORIENTATION & 3); /* kept across lcd_init() */
static bool g_lcd_ready; /* lcd_init() done and 3V3_BUS still on; cleared by lcd_pins_bus_off() */

/* DCX (FMC_A16) changes only when switching between command and data writes, and the
 * FMC holds it for one kernel clock after WRX rises (the ST7789 wants 10 ns). The barrier
 * makes each access complete before the next is issued, and the delay (10 NOPs: >= 20 ns
 * even at 480 MHz, ~25 ns at our 400 MHz SYSCLK) keeps DCX stable past the hold window on
 * both sides of a command write.
 * Pixel streams are all data writes, so they are not slowed. */
static inline void dcx_guard(void)
{
    __DSB();
    for (int i = 0; i < 10; i++) __asm volatile("nop");
}

/* One RDX read (DCX high) followed by a recovery gap. The FMC read timing
 * (fmc.c) keeps RDX high only for ADDSET = 15 kernel clocks (75 ns at 200 MHz)
 * between back-to-back reads, and ADDSET is already at its 4-bit maximum, but
 * the ST7789V2 wants RDX high >= 90 ns between reads (TRDH / TRDHFM, AC
 * characteristics table 4). The barrier waits for the read to finish and the
 * NOPs add >= 25 ns before the next one is issued. Reads are diagnostics only
 * (RDDID, GRAM readback), so the extra time does not matter. */
static uint16_t rd(void)
{
    uint16_t v = LCD_DATA;
    __DSB();
    for (int i = 0; i < 10; i++) __asm volatile("nop");
    return v;
}

/* Command byte (DCX low). Commands and parameters are 8 bits wide; on the
 * 16-bit bus the ST7789 takes them from D7..D0. */
static void cmd(uint8_t c)
{
    dcx_guard();
    LCD_CMD = c;
    dcx_guard();
}
/* One parameter byte (DCX high). */
static void dat(uint8_t d) { LCD_DATA = d; }

/* Command followed by n parameter bytes. */
static void cmd_seq(uint8_t c, const uint8_t *d, int n)
{
    cmd(c);
    for (int i = 0; i < n; i++) dat(d[i]);
}

/* Set the memory access control (scan direction / row-column exchange;
 * console `lcd madctl`). Only stored while 3V3_BUS is off; lcd_init()
 * re-applies the stored value. */
void lcd_set_madctl(uint8_t v)
{
    g_madctl = v;
    if (!bus_powered()) return;
    cmd(0x36);
    dat(v);
}

uint8_t lcd_get_madctl(void) { return g_madctl; }

/* Raw command + parameters (console `lcd cmd`), e.g. to tune VCOMS (0xBB) or
 * gamma on a panel whose vendor values are unknown. */
void lcd_write_cmd(uint8_t c, const uint8_t *d, int n)
{
    if (!bus_powered()) return;
    cmd_seq(c, d, n);
}

/* Power up (if needed), hardware-reset and initialise the panel, then enable
 * the TE interrupt. Blocking for ~350 ms in total (reset pulse, the ST7789's
 * mandatory 120 ms waits after reset and after SLPOUT). Leaves the display
 * on with whatever GRAM contains (random after power-up) and the backlight
 * untouched (hal_init() sets it after calibration). */
void lcd_init(void)
{
    if (!bus_powered()) bus_power(true);
    lcd_bank_init();

    /* Init sequence. EastRising's sample code for the ER-TFT032IPS-3.2 could not
     * be obtained (buydisplay.com downloads are behind a bot check, and the
     * datasheet Rev 1.0 has no init code), so the
     * panel-tuned values below are the ones from Newhaven's official example
     * for its ST7789 IPS panel (third_party/newhaven_nhd28, BSD-3, commit
     * f2e2d81), which were in use before the panel change:
     *   - PORCTRL, GCTRL, LCMCTRL, VDVVRHEN, VDVS, FRCTRL2 and PWCTRL1 equal the
     *     ST7789's own reset defaults (checked against the ST7789V2 datasheet's
     *     default tables), so they are safe on any ST7789 panel;
     *   - VCOMS 0x2B, VRHS 0x11 and the two gamma tables are panel tuning. They
     *     are typical IPS values (they only affect contrast, flicker and colour,
     *     never the panel's safety); LCD_PANEL_TUNING=0 leaves them at the
     *     ST7789 power-on defaults / OTP instead, and `lcd cmd` can try others
     *     at runtime.
     * Our changes: 16-bit FMC bus, our MADCTL (orientation config), landscape
     * window, TEON so the TE pin can be used, INVON for IPS. */
    gpio_config(P_LCD_RESET, PIN_OUT, PULL_NONE);
    gpio_write(P_LCD_RESET, 0);
    delay_us(50000);  /* ST7789 minimum 10 us (vendor examples use 100-250 ms) */
    gpio_write(P_LCD_RESET, 1);
    delay_us(150000); /* ST7789: 120 ms after reset before SLPOUT */

    cmd(0x28); /* DISPOFF */
    cmd(0x11); /* SLPOUT */
    delay_us(120000);
    cmd(0x36); /* MADCTL */
    dat(g_madctl);
    cmd(0x3A); /* COLMOD: 65K colours, 16 bpp: one bus write per pixel (IM2..0 = 000) */
    dat(0x55);
    static const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    cmd_seq(0xB2, porch, 5);   /* PORCTRL */
    static const uint8_t gctrl[] = {0x35};
    cmd_seq(0xB7, gctrl, 1);   /* GCTRL */
    static const uint8_t lcmctrl[] = {0x2C};
    cmd_seq(0xC0, lcmctrl, 1); /* LCMCTRL */
    static const uint8_t vdvvrhen[] = {0x01, 0xFF};
    cmd_seq(0xC2, vdvvrhen, 2); /* VDVVRHEN: VDV/VRH from the commands below */
    static const uint8_t vdvs[] = {0x20};
    cmd_seq(0xC4, vdvs, 1);     /* VDVS */
    static const uint8_t frctrl2[] = {0x0F}; /* 60 Hz */
    cmd_seq(0xC6, frctrl2, 1);
    static const uint8_t pwctrl1[] = {0xA4, 0xA1};
    cmd_seq(0xD0, pwctrl1, 2);  /* PWCTRL1: AVDD/AVCL/VDS */
#if LCD_PANEL_TUNING
    static const uint8_t vcoms[] = {0x2B};
    cmd_seq(0xBB, vcoms, 1);   /* VCOMS 1.175 V (reset default 0x20, 0.9 V) */
    static const uint8_t vrhs[] = {0x11};
    cmd_seq(0xC3, vrhs, 1);    /* VRHS (reset default 0x0B) */
    static const uint8_t pgamma[] = {0xD0, 0x00, 0x05, 0x0E, 0x15, 0x0D, 0x37,
                                     0x43, 0x47, 0x09, 0x15, 0x12, 0x16, 0x19};
    cmd_seq(0xE0, pgamma, 14); /* PVGAMCTRL: positive voltage gamma */
    static const uint8_t ngamma[] = {0xD0, 0x00, 0x05, 0x0D, 0x0C, 0x06, 0x2D,
                                     0x44, 0x40, 0x0E, 0x1C, 0x18, 0x16, 0x19};
    cmd_seq(0xE1, ngamma, 14); /* NVGAMCTRL: negative voltage gamma */
#endif
    static const uint8_t caset[] = {0x00, 0x00, (LCD_WIDTH - 1) >> 8, (LCD_WIDTH - 1) & 0xFF};
    cmd_seq(0x2A, caset, 4);   /* landscape window 320 x 240 (MV = 1) */
    static const uint8_t raset[] = {0x00, 0x00, (LCD_HEIGHT - 1) >> 8, (LCD_HEIGHT - 1) & 0xFF};
    cmd_seq(0x2B, raset, 4);
    delay_us(10000);
    cmd(0x21); /* INVON (IPS) */
    cmd(0x35); /* TEON, V-blank only (our addition) */
    dat(0x00);
    cmd(0x29); /* DISPON */
    delay_us(10000);
    g_lcd_ready = true;
    lcd_te_init();
}

/* Read the 24-bit display ID (RDDID); reads are slow FMC cycles (read timing
 * in fmc.c). Returns 0 with the bus off; all-ones / all-zeros usually mean
 * no panel or a broken FPC. */
uint32_t lcd_read_id(void)
{
    if (!bus_powered()) return 0;
    cmd(0x04); /* RDDID: dummy, ID1, ID2, ID3 (ST7789V2 default 85 85 52) */
    (void)rd();
    uint32_t id1 = rd() & 0xFF;
    uint32_t id2 = rd() & 0xFF;
    uint32_t id3 = rd() & 0xFF;
    return (id1 << 16) | (id2 << 8) | id3;
}

/* ------------------------------------------------------- GRAM test */
/* Write n pixels to the top-left row, read them back (RAMRD) and compare.
 * After every LCD write the CPU stores the inverted pixel to SDRAM (a
 * write-through region), so the shared D0-D15 are driven to the opposite value
 * right after WRX rises: the worst case for the panel's data hold time.
 * The ST7789's readback format in 16-bit mode is not pinned down in the panel
 * documentation, so several decoders are tried (RGB565 words; RGB666 packed
 * 3 bytes per pixel; one RGB666 channel per word, low or high byte; each with
 * R/B swapped); the best one wins. Only the top 5/6/5 bits are compared. */

/* CASET/RASET: GRAM write/read window, inclusive corners. */
static void set_window(int x0, int y0, int x1, int y1)
{
    cmd(0x2A);
    dat((uint8_t)(x0 >> 8)); dat((uint8_t)x0); dat((uint8_t)(x1 >> 8)); dat((uint8_t)x1);
    cmd(0x2B);
    dat((uint8_t)(y0 >> 8)); dat((uint8_t)y0); dat((uint8_t)(y1 >> 8)); dat((uint8_t)y1);
}

/* 1 if the 8-bit-per-channel readback (r, g, b; optionally R/B swapped)
 * differs from RGB565 pixel p in the bits that RGB565 actually holds. */
static int rgb_mismatch(uint16_t p, uint8_t r, uint8_t g, uint8_t b, bool swap)
{
    if (swap) { uint8_t x = r; r = b; b = x; }
    return ((r >> 3) != (p >> 11)) || ((g >> 2) != ((p >> 5) & 0x3F)) || ((b >> 3) != (p & 0x1F));
}

int lcd_ram_test(const uint16_t *pat, int n, volatile uint16_t *sdram_scratch, const char **fmt, uint16_t *raw)
{
    static uint16_t rb[3 * 256];
    if (!g_lcd_ready || n <= 0 || n > 256) return -1;
    volatile uint16_t *d = &LCD_DATA;
    set_window(0, 0, n - 1, 0);
    cmd(0x2C); /* RAMWR */
    for (int i = 0; i < n; i++) {
        *d = pat[i];
        sdram_scratch[i] = (uint16_t)~pat[i];
    }
    __DSB();
    set_window(0, 0, n - 1, 0);
    cmd(0x2E); /* RAMRD: one dummy read, then data */
    (void)rd();
    /* read enough words for the widest format (3 per pixel); the decoders
     * below interpret the same buffer in each candidate layout */
    for (int i = 0; i < 3 * n; i++) rb[i] = rd();
    for (int i = 0; i < 6; i++) raw[i] = rb[i];

    static const char *const names[] = {"rgb565", "rgb666 packed", "rgb666 packed bgr", "rgb666 lo byte",
                                        "rgb666 lo byte bgr", "rgb666 hi byte", "rgb666 hi byte bgr"};
    int best = n + 1, best_i = 0;
    for (int f = 0; f < 7; f++) {
        int err = 0;
        for (int j = 0; j < n; j++) {
            if (f == 0) {
                err += rb[j] != pat[j];
                continue;
            }
            /* channel k of pixel j: packed = byte stream, high byte first;
             * lo/hi byte = one channel per 16-bit word */
            uint8_t c[3];
            for (int k = 0; k < 3; k++) {
                int idx = 3 * j + k;
                if (f <= 2) c[k] = (idx & 1) ? (uint8_t)rb[idx >> 1] : (uint8_t)(rb[idx >> 1] >> 8);
                else if (f <= 4) c[k] = (uint8_t)rb[idx];
                else c[k] = (uint8_t)(rb[idx] >> 8);
            }
            err += rgb_mismatch(pat[j], c[0], c[1], c[2], f == 2 || f == 4 || f == 6);
        }
        if (err < best) { best = err; best_i = f; }
    }
    *fmt = names[best_i];
    return best;
}

/* Panel sleep (display blanking; GRAM is retained). The ST7789 needs 5 ms
 * after SLPIN before the next command and 120 ms after SLPOUT before it is
 * fully up (datasheet SLPIN/SLPOUT descriptions); waking therefore blocks
 * for 120 ms. */
void lcd_sleep(bool sleep)
{
    if (!bus_powered() || !g_lcd_ready) return;
    if (sleep) {
        cmd(0x28); /* DISPOFF */
        cmd(0x10); /* SLPIN */
        delay_us(5000);
    } else {
        cmd(0x11); /* SLPOUT */
        delay_us(120000);
        cmd(0x29); /* DISPON */
    }
}

/* Current level of the TE pin (console diagnostics). */
bool lcd_te_level(void) { return gpio_read(P_LCD_TE) != 0; }

/* ---------------------------------------------------------------- TE */
/* TE (TEON mode 0: V-blank) rises once per frame (~60 Hz). EXTI9 counts the
 * frames; with vsync on, large flushes start right after a V-blank so the
 * panel's scan does not overtake the write. If TE stops (no panel, broken
 * FPC) vsync turns itself off instead of stalling every flush. */
static volatile uint32_t g_te_count;
static bool g_vsync = true;

/* Route PG9 to EXTI line 9, rising edge, unmasked; the shared EXTI9_5
 * vector (keypad.c) dispatches it to lcd_te_irq(). Only while 3V3_BUS is on:
 * with the rail off the pin is kept analog (no input on an unpowered net). */
void lcd_te_init(void)
{
    if (!bus_powered()) return;
    gpio_config(P_LCD_TE, PIN_IN, PULL_NONE);
    RCC->APB4ENR |= RCC_APB4ENR_SYSCFGEN;
    SYSCFG->EXTICR[EXTI_LINE_LCD_TE >> 2] = (SYSCFG->EXTICR[EXTI_LINE_LCD_TE >> 2] & ~(0xFu << ((EXTI_LINE_LCD_TE & 3) * 4))) |
                                            ((uint32_t)(P_LCD_TE >> 4) << ((EXTI_LINE_LCD_TE & 3) * 4));
    EXTI->RTSR1 |= 1u << EXTI_LINE_LCD_TE;
    EXTI->FTSR1 &= ~(1u << EXTI_LINE_LCD_TE);
    EXTI_D1->PR1 = 1u << EXTI_LINE_LCD_TE;
    EXTI_D1->IMR1 |= 1u << EXTI_LINE_LCD_TE;
    NVIC_SetPriority(EXTI9_5_IRQn, 4);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
}

void lcd_te_irq(void) { g_te_count++; }
uint32_t lcd_te_count(void) { return g_te_count; }
void lcd_set_vsync(bool on) { g_vsync = on; }
bool lcd_get_vsync(void) { return g_vsync; }

/* Before 3V3_BUS goes off: mask the TE interrupt and float the pin (analog),
 * and mark the panel uninitialised so flushes become no-ops until the next
 * lcd_init(). */
void lcd_pins_bus_off(void)
{
    EXTI_D1->IMR1 &= ~(1u << EXTI_LINE_LCD_TE);
    gpio_set_analog(P_LCD_TE);
    g_lcd_ready = false;
}

/* Busy-wait for the next TE rising edge (start of vertical blanking). A 25 ms
 * timeout permanently disables vsync, see above. Needs interrupts enabled. */
static void wait_vblank(void)
{
    uint32_t c = g_te_count, t0 = g_ticks_ms;
    while (g_te_count == c) {
        if (g_ticks_ms - t0 > 25) { /* > 1.5 frames: TE is not toggling */
            g_vsync = false;
            return;
        }
    }
}

/* Copy the rectangle (x, y, w, h) of the 320x240 RGB565 framebuffer `fb`
 * (row stride LCD_WIDTH pixels) to the same place in GRAM. The caller keeps
 * the rectangle inside the screen; nothing is clipped here. One FMC write
 * cycle per pixel: ADDSET + DATAST + 1 = 21 FMC kernel clocks (fmc.c), i.e.
 * 105 ns at 200 MHz (117 ns at 180 MHz), so a full frame takes ~8-9 ms, about
 * half a 60 Hz frame. Large updates (>= 1/4 screen) therefore start at
 * V-blank so the write stays ahead of the panel's scan (no tearing); small
 * ones go out at once. DBG_IO0 is high for the duration (scope timing). */
void lcd_flush(const uint16_t *fb, int x, int y, int w, int h)
{
    if (!g_lcd_ready || w <= 0 || h <= 0) return;
    if (g_vsync && w * h >= LCD_WIDTH * LCD_HEIGHT / 4) wait_vblank();
    gpio_write(P_DBG_IO0, 1);
    int x1 = x + w - 1, y1 = y + h - 1;
    cmd(0x2A); /* CASET (columns = landscape x with MV=1) */
    dat((uint8_t)(x >> 8));
    dat((uint8_t)x);
    dat((uint8_t)(x1 >> 8));
    dat((uint8_t)x1);
    cmd(0x2B); /* RASET */
    dat((uint8_t)(y >> 8));
    dat((uint8_t)y);
    dat((uint8_t)(y1 >> 8));
    dat((uint8_t)y1);
    cmd(0x2C); /* RAMWR */
    volatile uint16_t *d = &LCD_DATA;
    for (int row = 0; row < h; row++) {
        const uint16_t *p = fb + (y + row) * LCD_WIDTH + x;
        int n = w;
        while (n >= 8) {
            *d = p[0]; *d = p[1]; *d = p[2]; *d = p[3];
            *d = p[4]; *d = p[5]; *d = p[6]; *d = p[7];
            p += 8;
            n -= 8;
        }
        while (n--) *d = *p++;
    }
    gpio_write(P_DBG_IO0, 0);
}

/* ------------------------------------------------------------ backlight */
/* Constant-current sink: TLV9001 + AO3400A + 1 ohm. Setpoint from DAC1 (PA4) in
 * unbuffered mode through 10k into 1k: I = Vdac * 1k / (10k + Ro + 1k) / 1 ohm,
 * Ro = 10-16 kohm (unbuffered DAC output impedance) -> 122-157 mA full scale
 * (nominal ~138 mA, BL_DAC_FS_MA). The panel (5 LEDs in parallel) wants 90 mA
 * normal, 100 mA max: setpoints are clamped to BL_ABS_MAX_MA, 100 % is
 * BL_LIMIT_MA. BL_SENSE (PC5) reads the actual current (1 V/A), so the
 * open-loop estimate is corrected by a measured gain (backlight_calibrate), and
 * a missing panel or an open LED string shows up as ~0 mA. Until a calibration
 * has succeeded the DAC code is computed for the worst-case (lowest) Ro, so an
 * uncalibrated setpoint never gives more current than asked for. The op-amp is
 * on 3V3_BUS: nothing to measure while the rail is off.
 *
 * LED anode (LCD_A) supply: 3V3_BUS (default, efficient) or VSYS, chosen by
 * BL_SRC_3V3 (PC13) / BL_SRC_VSYS (PD6), see board.h; both off at reset / analog. With Vf 3.2 V typ / 3.4 V
 * max at 100 mA, a 3.3 V anode leaves almost no headroom near full current, so
 * in auto mode backlight_service() checks BL_SENSE after each setpoint change
 * (and every BL_SRC_RECHECK_MS) while on 3V3: below BL_SRC_SAT_FRAC of the
 * setpoint the 3V3 path is saturated, the measured current is remembered as the
 * 3V3 limit (until reset: it depends on the panel's Vf) and the anode moves to
 * VSYS. Setpoints above limit / BL_SRC_SAT_FRAC then go straight to VSYS; below
 * BL_SRC_BACK_FRAC x limit they return to 3V3. Switches are rate-limited
 * (BL_SRC_MIN_GAP_MS) and always break-before-make (backlight_src_switch). */
static float g_bl_ma, g_bl_gain = 1.0f;
static bool g_bl_fault, g_bl_cal;

static int g_bl_src = BL_ANODE_OFF;       /* active anode path (OFF while 3V3_BUS is off) */
static int g_bl_src_mode = BL_ANODE_AUTO; /* AUTO or a forced path (console) */
static float g_bl_sat_ma;               /* learned 3V3 limit, 0 = not seen yet */
static bool g_bl_check;                 /* BL_SENSE check pending */
static uint32_t g_bl_set_ms, g_bl_switch_ms;
static bool g_bl_switched_once;

/* Program the DAC for `ma` milliamps of LED current (12-bit right-aligned,
 * code = ma / full_scale * 4095, saturating). Full scale is the calibrated
 * nominal value, or the worst-case maximum before a calibration so the
 * result can only err low. No clamping to BL_ABS_MAX_MA here: callers do. */
static void dac_for(float ma)
{
    float fs = g_bl_cal ? BL_DAC_FS_MA * g_bl_gain : BL_DAC_FS_MAX_MA;
    uint32_t code = (uint32_t)(ma / fs * 4095.0f + 0.5f);
    if (code > 4095) code = 4095;
    DAC1->DHR12R1 = code;
}

/* DAC clocked and channel 1 enabled, i.e. backlight_init() has run and the
 * DAC has not been shut down for Standby. */
static bool dac_on(void) { return (RCC->APB1LENR & RCC_APB1LENR_DAC12EN) && (DAC1->CR & DAC_CR_EN1); }

/* Enable DAC1 channel 1 on PA4 at code 0 (no LED current). MODE1 = 010:
 * normal mode, output to the external pin, buffer disabled (RM0433 DAC_MCR);
 * the unbuffered output's ~10-16 kohm impedance is part of the setpoint
 * divider, which is why the full scale needs calibrating. MODE may only be
 * changed while EN1 = 0. */
void backlight_init(void)
{
    gpio_set_analog(P_LCD_BL_DAC);
    RCC->APB1LENR |= RCC_APB1LENR_DAC12EN;
    (void)RCC->APB1LENR;
    DAC1->CR &= ~DAC_CR_EN1;
    DAC1->MCR = (DAC1->MCR & ~DAC_MCR_MODE1) | (2u << DAC_MCR_MODE1_Pos); /* normal, pin, no buffer */
    DAC1->DHR12R1 = 0;
    DAC1->CR |= DAC_CR_EN1;
    g_bl_ma = 0;
}

/* 3V3_BUS about to come on: selector driven to the 3V3 path (3V3 on, VSYS off).
 * Both pins sit low on their 100k pull-downs (both paths off) until here. ODR
 * before MODER, and PD6 is made a (low) output first, so the only edge is PC13
 * rising after VSYS is already actively held off: never both on. */
void backlight_src_bus_on(void)
{
    gpio_write(P_BL_SRC_VSYS, 0);
    gpio_write(P_BL_SRC_3V3, 1);
    gpio_config(P_BL_SRC_VSYS, PIN_OUT, PULL_NONE);
    gpio_config(P_BL_SRC_3V3, PIN_OUT, PULL_NONE);
    gpio_set_speed(P_BL_SRC_VSYS, 0);
    gpio_set_speed(P_BL_SRC_3V3, 0); /* PC13: backup-domain pin, low speed only */
    g_bl_src = BL_ANODE_3V3;
    g_bl_check = true;
    g_bl_set_ms = g_ticks_ms;
}

/* 3V3_BUS about to go off (and Standby): LED current 0, VSYS path off first
 * (1 ms), then both pins released to their pull-downs = both paths off (analog:
 * no input leakage, and the same state the pins have at reset and in Standby).
 * Nothing needs the 3V3 path while the pins are analog: backlight_calibrate()
 * and bus_power(true) drive PC13 high via backlight_src_bus_on(). */
void backlight_src_bus_off(void)
{
    if (dac_on()) DAC1->DHR12R1 = 0;
    if (g_bl_src == BL_ANODE_VSYS || ((gpio_port(P_BL_SRC_VSYS)->ODR >> (P_BL_SRC_VSYS & 15)) & 1)) {
        gpio_write(P_BL_SRC_VSYS, 0);
        delay_us(BL_SRC_DEAD_US);
    }
    gpio_set_analog(P_BL_SRC_VSYS);
    gpio_set_analog(P_BL_SRC_3V3);
    g_bl_src = BL_ANODE_OFF;
    g_bl_check = false;
}

/* Break-before-make: current 0, active path off, >= 1 ms with both off, new
 * path on, 0.2 ms, setpoint back. Interrupts are not masked: nothing else
 * touches these pins. */
int backlight_src_switch(int src)
{
    if (src != BL_ANODE_3V3 && src != BL_ANODE_VSYS) return -1;
    if (g_bl_src == BL_ANODE_OFF || !bus_powered()) return -2;
    if (src == g_bl_src) return 0;
    if (dac_on()) DAC1->DHR12R1 = 0;
    delay_us(100); /* sink loop follows in ~0.1 ms: switch with no LED current */
    if (src == BL_ANODE_VSYS) gpio_write(P_BL_SRC_3V3, 0);
    else gpio_write(P_BL_SRC_VSYS, 0);
    g_bl_src = BL_ANODE_OFF;
    delay_us(BL_SRC_DEAD_US);
    if (src == BL_ANODE_VSYS) gpio_write(P_BL_SRC_VSYS, 1);
    else gpio_write(P_BL_SRC_3V3, 1);
    delay_us(BL_SRC_ON_US);
    g_bl_src = src;
    if (dac_on()) dac_for(g_bl_ma);
    g_bl_switch_ms = g_ticks_ms;
    g_bl_switched_once = true;
    g_bl_check = true;
    g_bl_set_ms = g_ticks_ms;
    return 0;
}

/* Set the LED current in mA (negative/NaN -> 0, clamped to BL_ABS_MAX_MA).
 * A changed setpoint schedules a BL_SENSE check for the auto anode-supply
 * policy (backlight_service). */
void backlight_set_ma(float ma)
{
    if (!(ma > 0)) ma = 0;
    if (ma > BL_ABS_MAX_MA) ma = BL_ABS_MAX_MA;
    dac_for(ma);
    if (ma != g_bl_ma) {
        g_bl_check = true;
        g_bl_set_ms = g_ticks_ms;
    }
    g_bl_ma = ma;
}

float backlight_get_ma(void) { return g_bl_ma; }
float backlight_gain(void) { return g_bl_gain; }
bool backlight_fault(void) { return g_bl_fault; }
int backlight_src(void) { return g_bl_src; }
int backlight_src_mode(void) { return g_bl_src_mode; }
float backlight_src_sat_ma(void) { return g_bl_sat_ma; }
void backlight_src_forget(void) { g_bl_sat_ma = 0; }

/* Actual LED current in mA: BL_SENSE (1 ohm sense resistor -> 1 mV/mA),
 * average of 4 ADC conversions. */
float backlight_measure_ma(void)
{
    float sum = 0;
    for (int i = 0; i < 4; i++) sum += adc_read_volts(ADC_CH_BL);
    return sum / 4.0f / BL_SENSE_OHMS * 1000.0f;
}

/* Source selection. now: check BL_SENSE at once (caller has waited for the
 * loop to settle) and ignore the rate limit. Returns the path it switched to,
 * or 0. *sat_out (optional) gets the measured current when 3V3 saturated. */
int backlight_src_update(bool now, float *sat_out)
{
    if (g_bl_src == BL_ANODE_OFF || !bus_powered() || !dac_on()) return 0;
    uint32_t t = g_ticks_ms;
    int want = g_bl_src;
    if (g_bl_src_mode != BL_ANODE_AUTO) {
        want = g_bl_src_mode;
    } else if (!g_bl_cal || g_bl_fault) {
        want = BL_ANODE_3V3; /* BL_SENSE not trusted (no panel?): stay on the 3V3 path */
    } else if (g_bl_src == BL_ANODE_VSYS) {
        /* limit unknown (e.g. after a forced vsys): back to 3V3, which re-learns it */
        if (!(g_bl_sat_ma > 0) || g_bl_ma < BL_SRC_BACK_FRAC * g_bl_sat_ma) want = BL_ANODE_3V3;
    } else if (g_bl_sat_ma > 0 && g_bl_ma * BL_SRC_SAT_FRAC > g_bl_sat_ma) {
        want = BL_ANODE_VSYS; /* known not to regulate from 3V3_BUS */
    } else if (g_bl_ma >= BL_SRC_MIN_MA) {
        if (!g_bl_check && t - g_bl_set_ms >= BL_SRC_RECHECK_MS) {
            g_bl_check = true;
            g_bl_set_ms = t - BL_SRC_SETTLE_MS;
        }
        if (g_bl_check && (now || t - g_bl_set_ms >= BL_SRC_SETTLE_MS)) {
            float m = backlight_measure_ma();
            g_bl_check = false;
            g_bl_set_ms = t;
            if (m > BL_MIN_DETECT_MA && m < BL_SRC_SAT_FRAC * g_bl_ma) {
                g_bl_sat_ma = m;
                if (sat_out) *sat_out = m;
                want = BL_ANODE_VSYS;
            } else if (g_bl_sat_ma > 0 && m > g_bl_sat_ma) {
                g_bl_sat_ma = m; /* 3V3 regulated more than the learned limit: raise it */
            }
        }
    }
    if (want == g_bl_src) return 0;
    if (!now && g_bl_switched_once && t - g_bl_switch_ms < BL_SRC_MIN_GAP_MS) return 0;
    return backlight_src_switch(want) == 0 ? want : 0;
}

/* Main-loop hook (hal_service): one rate-limited auto-policy step. */
void backlight_service(void) { (void)backlight_src_update(false, 0); }

/* Console / debug: AUTO, or force one path (still break-before-make). */
int backlight_src_set_mode(int mode)
{
    if (mode != BL_ANODE_AUTO && mode != BL_ANODE_3V3 && mode != BL_ANODE_VSYS) return -1;
    g_bl_src_mode = mode;
    if (mode != BL_ANODE_AUTO) return backlight_src_switch(mode);
    g_bl_check = true;
    (void)backlight_src_update(true, 0);
    return 0;
}

/* Measure at a known setpoint and fold the error into the gain (two passes).
 * Needs the ADC and 3V3_BUS. Returns -1 (fault) if no current flows or the
 * gain is implausible; the setpoints then stay on the conservative worst-case
 * full scale. The probe (20 mA) is low enough to regulate from 3V3_BUS, and
 * the calibration always runs on the 3V3 path (backlight_service() moves to
 * VSYS afterwards if the mode or the setpoint asks for it). */
int backlight_calibrate(void)
{
    if (!bus_powered()) return -2;
    if (g_bl_src == BL_ANODE_OFF) backlight_src_bus_on();
    backlight_src_switch(BL_ANODE_3V3);
    float keep = g_bl_ma;
    const float probe = 20.0f;
    g_bl_gain = 1.0f;
    g_bl_cal = true; /* passes use the nominal full scale times the running gain */
    for (int pass = 0; pass < 2; pass++) {
        dac_for(probe);
        delay_us(3000); /* loop settles in ~100 us; 10 nF-ish on the sense node */
        float m = backlight_measure_ma();
        float gain = g_bl_gain * m / probe;
        /* Ro 10-16k and 1 % resistors: 0.89-1.14 expected; outside 0.7-1.4 the
         * hardware or the measurement is wrong, so do not trust it. */
        if (!(m > BL_MIN_DETECT_MA) || gain < 0.7f || gain > 1.4f) {
            g_bl_fault = true;
            g_bl_cal = false;
            g_bl_gain = 1.0f;
            backlight_set_ma(keep);
            g_bl_check = true;
            return -1;
        }
        g_bl_gain = gain;
    }
    g_bl_fault = false;
    backlight_set_ma(keep);
    g_bl_check = true;
    g_bl_set_ms = g_ticks_ms;
    return 0;
}
