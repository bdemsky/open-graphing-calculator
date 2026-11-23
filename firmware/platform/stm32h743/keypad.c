/*
 * 7x7 key matrix (no diodes) + ON key.
 * Rows PG11-14, PB0, PB1, PB4: open-drain outputs. Columns PE2-6, PB12, PB13:
 * inputs with internal pull-ups, each on its own EXTI line (2,3,4,5,6,12,13);
 * lines 1 (TP_INT), 9 (LCD_TE) and 10 (PMON_ALERT) belong to other drivers.
 * Idle: all rows driven low, so any key pulls its column low -> EXTI wake.
 * Scan every 5 ms from SysTick; a key changes state after DEBOUNCE equal samples.
 * ON (PA0) is active high (switch to 3V3, 100k pull-down) and is WKUP1.
 *
 * State word: bit (row * 7 + col) for the 49 matrix keys, bit 49 for ON.
 * Debounced transitions are translated through key_matrix[][] (generated
 * keymap, keyq.h) and pushed into the key queue (core/keyq.c) together with
 * the press/release flag; everything above this file works with key codes.
 *
 * Why open-drain rows: with no diodes, two keys in the same column on
 * different rows would short a high row to a low one through the switches if
 * the rows were push-pull. Open-drain rows can only pull low; "released" rows
 * are high-impedance, so pressing any combination of keys is electrically
 * harmless and only causes ghosting (handled below).
 *
 * The column EXTIs are used only to wake the MCU from Stop mode: during
 * normal operation the lines are masked and the SysTick scan is the sole
 * source of key events.
 *
 * Boot: hal_init() calls keypad_init() right after the pins are configured,
 * before the slow parts of the boot (LCD, SDRAM test, touch, backlight
 * calibration, microSD), so keys held through a reset are debounced ~15 ms
 * after it; every debounced press is timestamped (g_down_ms), and
 * keypad_held_ms() says how long a combination has been held together. The
 * safe-mode key (ON + CLEAR for 0.5 s, core/safemode.h) is decided from that
 * when app_init() runs, so a long boot is not a longer wait for the user.
 */
#include "keyq.h"
#include "platform.h"

#define DEBOUNCE 3 /* x 5 ms: a change must be seen on 3 consecutive scans */
#define ON_BIT 49  /* state-word bit of the ON key (after the 7x7 = 49 matrix bits) */

static const uint8_t g_rows[KEYPAD_ROWS] = {P_KEY_ROW0, P_KEY_ROW1, P_KEY_ROW2, P_KEY_ROW3,
                                            P_KEY_ROW4, P_KEY_ROW5, P_KEY_ROW6};
static const uint8_t g_cols[KEYPAD_COLS] = {P_KEY_COL0, P_KEY_COL1, P_KEY_COL2, P_KEY_COL3,
                                            P_KEY_COL4, P_KEY_COL5, P_KEY_COL6};

static volatile uint64_t g_stable;  /* debounced state word (written from SysTick) */
static volatile uint32_t g_down_ms[50]; /* g_ticks_ms when each key's debounced press began */
static volatile bool g_scanning;     /* keypad_init done: the pins are valid */
static uint8_t g_count[50];         /* per key: consecutive scans that disagree with g_stable */
volatile uint32_t keypad_ghost_events; /* scans rejected as ambiguous (console statistics) */

/* Rows to idle (low), EXTI port routing and edge selection for the column
 * lines and the ON key, NVIC enables. The EXTI interrupt masks stay off
 * here; keypad_prepare_wake() unmasks them just before Stop. */
void keypad_init(void)
{
    for (int r = 0; r < KEYPAD_ROWS; r++) gpio_write(g_rows[r], 0);
    /* After a Standby wake the WKUP1 function may still own PA0 (WKUPEN1 is
     * set again just before the next Standby): give the pin back to the GPIO
     * so the scanner reads ON. */
    PWR->WKUPEPR &= ~PWR_WKUPEPR_WKUPEN1;
    /* Pins themselves are configured by gpio_config_from_table(). EXTI port
     * selection for the column lines (used only to wake from Stop). */
    RCC->APB4ENR |= RCC_APB4ENR_SYSCFGEN;
    for (int c = 0; c < KEYPAD_COLS; c++) {
        int pin = g_cols[c];
        int line = pin & 15;
        int port = pin >> 4;
        /* SYSCFG_EXTICRn: four 4-bit port selectors per register (RM0433
         * "SYSCFG external interrupt configuration register") */
        SYSCFG->EXTICR[line >> 2] = (SYSCFG->EXTICR[line >> 2] & ~(0xFu << ((line & 3) * 4))) |
                                    ((uint32_t)port << ((line & 3) * 4));
        EXTI->FTSR1 |= 1u << line;
    }
    /* ON key: EXTI0 from PA0 rising edge (also WKUP1 for Standby). */
    SYSCFG->EXTICR[0] &= ~0xFu;
    EXTI->RTSR1 |= 1u; /* armed (IMR) only before Stop; the scanner reports ON normally */
    NVIC_SetPriority(EXTI0_IRQn, 4);
    NVIC_EnableIRQ(EXTI0_IRQn);
    static const IRQn_Type irqs[] = {EXTI2_IRQn, EXTI3_IRQn, EXTI4_IRQn, EXTI9_5_IRQn, EXTI15_10_IRQn};
    for (unsigned i = 0; i < sizeof irqs / sizeof irqs[0]; i++) {
        NVIC_SetPriority(irqs[i], 4);
        NVIC_EnableIRQ(irqs[i]);
    }
    g_scanning = true;
}

/* Bit c set = column c is pulled low (a key on the currently driven row). */
static uint32_t col_mask(void)
{
    uint32_t m = 0;
    for (int c = 0; c < KEYPAD_COLS; c++)
        if (!gpio_read(g_cols[c])) m |= 1u << c;
    return m;
}

/* Row pull-ups, on only while scanning (PUPDR 01 = pull-up, 00 = none). At
 * idle every row is driven low, so a pull-up there would draw ~80 uA per row
 * in Stop; during a scan the released rows need one. */
static void rows_pull_up(bool on)
{
    for (int r = 0; r < KEYPAD_ROWS; r++) {
        GPIO_TypeDef *g = gpio_port(g_rows[r]);
        unsigned sh = (unsigned)(g_rows[r] & 15) * 2u;
        g->PUPDR = (g->PUPDR & ~(3u << sh)) | ((on ? 1u : 0u) << sh);
    }
}

/* One full matrix scan: drive one row low at a time (others released), read
 * the columns, then return all rows to low so a key press can still wake the
 * MCU via EXTI. A released row is high-impedance and starts from 0 V (idle,
 * or its own scan step); a pressed key ties it to its column, so until the
 * row has charged back up it pulls that column low and the key also shows on
 * the rows scanned next. With only the column's ~40k pull-up and 2 us of
 * settling, rev A board 1 saw every key on two or three rows (F1 as F1 + DEL).
 * The rows get their own pull-up during the scan, and each step waits 10 us.
 * Takes ~7 x 10 us plus GPIO overhead, inside the SysTick ISR. */
static uint64_t scan_raw(void)
{
    uint64_t raw = 0;
    rows_pull_up(true);
    /* release all rows */
    for (int r = 0; r < KEYPAD_ROWS; r++) gpio_write(g_rows[r], 1);
    for (int r = 0; r < KEYPAD_ROWS; r++) {
        gpio_write(g_rows[r], 0);
        delay_us(10); /* the previously released row recharges through ~20k (row + column pull-ups) */
        uint32_t m = col_mask();
        gpio_write(g_rows[r], 1);
        raw |= (uint64_t)m << (r * KEYPAD_COLS);
    }
    for (int r = 0; r < KEYPAD_ROWS; r++) gpio_write(g_rows[r], 0); /* idle: wake-capable */
    rows_pull_up(false);
    if (gpio_read(P_KEY_ON) == KEY_ON_ACTIVE_HIGH) raw |= 1ull << ON_BIT;
    return raw;
}

/* Without diodes, three keys at three corners of a rectangle make the fourth
 * corner read as pressed. Return true if the raw state is ambiguous.
 * A rectangle in the raw state is exactly two rows that share at least two
 * pressed columns; in that case there is no way to tell which corner (if any)
 * is a ghost, so the scan is flagged instead of being guessed at. */
static bool ghosting(uint64_t raw)
{
    uint64_t m = raw & ((1ull << 49) - 1);
    if (__builtin_popcountll(m) < 3) return false;
    for (int r1 = 0; r1 < KEYPAD_ROWS; r1++) {
        uint32_t a = (uint32_t)(m >> (r1 * 7)) & 0x7F;
        if (__builtin_popcount(a) < 1) continue;
        for (int r2 = r1 + 1; r2 < KEYPAD_ROWS; r2++) {
            uint32_t b = (uint32_t)(m >> (r2 * 7)) & 0x7F;
            if (__builtin_popcount(a & b) >= 2) return true;
        }
    }
    return false;
}

/* Called every 5 ms from SysTick_Handler (interrupt context). Scans the
 * matrix, debounces each key independently (DEBOUNCE consecutive disagreeing
 * samples flip the stable state) and queues press/release events. While the
 * raw state is ambiguous (ghosting) new presses are held off, but releases
 * are still accepted so keys never get stuck. */
void keypad_scan_tick(void)
{
    if (!g_scanning)
        return; /* SysTick runs before the pins are set up */
    uint64_t raw = scan_raw();
    bool ghost = ghosting(raw);
    if (ghost) keypad_ghost_events++;
    uint64_t st = g_stable;
    for (int k = 0; k < 50; k++) {
        bool now = (raw >> k) & 1;
        bool was = (st >> k) & 1;
        if (now == was) {
            g_count[k] = 0;
            continue;
        }
        if (now && ghost) { /* don't accept new presses while ambiguous */
            g_count[k] = 0;
            continue;
        }
        if (++g_count[k] >= DEBOUNCE) {
            g_count[k] = 0;
            st ^= 1ull << k;
            if (now)
                g_down_ms[k] = g_ticks_ms;
            uint8_t key = (k == ON_BIT) ? KEY_ON : key_matrix[k / 7][k % 7];
            keyq_push(key, now);
        }
    }
    g_stable = st;
}

/* Debounced state word (bit row*7+col, bit 49 = ON); for the console. */
uint64_t keypad_state(void) { return g_stable; }

/* How long all keys of `mask` have been down together: since the latest of
 * their debounced presses. 0 if one of them is up. */
uint32_t keypad_held_ms(uint64_t mask)
{
    if (!mask || (g_stable & mask) != mask)
        return 0;
    uint32_t now = g_ticks_ms, held = UINT32_MAX;
    for (int k = 0; k < 50; k++)
        if ((mask >> k) & 1) {
            uint32_t h = now - g_down_ms[k];
            if (h < held)
                held = h;
        }
    return held;
}

/* Before Stop: rows low, column EXTI interrupts armed (falling edge).
 * Stale pending bits are cleared first (PR1 is write-1-to-clear) so only a
 * new edge wakes the core. */
void keypad_prepare_wake(void)
{
    for (int r = 0; r < KEYPAD_ROWS; r++) gpio_write(g_rows[r], 0);
    uint32_t lines = 0;
    for (int c = 0; c < KEYPAD_COLS; c++) lines |= 1u << (g_cols[c] & 15);
    lines |= 1u; /* ON key, EXTI0 rising */
    EXTI_D1->PR1 = lines;
    EXTI_D1->IMR1 |= lines;
}

/* Wake interrupt from a key column or ON: acknowledge and mask all of them
 * again. The interrupt itself carries no key information; it only ends Stop
 * mode, and the next SysTick scans pick up the key. */
static void exti_columns_irq(void)
{
    uint32_t lines = 0;
    for (int c = 0; c < KEYPAD_COLS; c++) lines |= 1u << (g_cols[c] & 15);
    lines |= 1u;
    uint32_t pend = EXTI_D1->PR1 & lines;
    EXTI_D1->PR1 = pend;
    EXTI_D1->IMR1 &= ~lines; /* one-shot: the SysTick scanner takes over */
}

void EXTI0_IRQHandler(void) { exti_columns_irq(); }

void EXTI2_IRQHandler(void) { exti_columns_irq(); }
void EXTI3_IRQHandler(void) { exti_columns_irq(); }
void EXTI4_IRQHandler(void) { exti_columns_irq(); }
/* Shared vectors: EXTI9_5 also carries LCD_TE (PG9, line 9); EXTI15_10 also
 * carries PMON_ALERT (PF10, line 10). Those lines are their own (no key column
 * uses pin number 9 or 10), so the handlers just dispatch by pending bit. */

void EXTI9_5_IRQHandler(void)
{
    if (EXTI_D1->PR1 & (1u << EXTI_LINE_LCD_TE)) {
        EXTI_D1->PR1 = 1u << EXTI_LINE_LCD_TE;
        lcd_te_irq();
    }
    exti_columns_irq();
}

void EXTI15_10_IRQHandler(void)
{
    if (EXTI_D1->PR1 & (1u << EXTI_LINE_PMON_ALERT)) {
        EXTI_D1->PR1 = 1u << EXTI_LINE_PMON_ALERT;
        pmon_alert_irq();
    }
    exti_columns_irq();
}
