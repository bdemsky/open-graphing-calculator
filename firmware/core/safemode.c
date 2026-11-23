/* Boot recovery: safe mode on a held key or after a crash loop (see safemode.h). */
#include "safemode.h"
#include "hal.h"

#define BOOT_MAGIC 0x53414645u /* "SAFE": the record is valid (power-on leaves random data) */

static int g_reason;
static bool g_counted_ok; /* this boot already reported as successful */
static uint32_t g_boot_ms;
static int g_force = -1;  /* test hook: -1 none, else SAFEMODE_OFF / SAFEMODE_FORCED for the next boot */
static int g_reset_cause; /* HAL_RESET_* of this boot */

/* The record: {magic, attempts}. Garbage (power-on) reads as 0 attempts. */
static uint32_t *record(void)
{
    uint32_t *r = hal_boot_record();
    if (r && r[0] != BOOT_MAGIC) {
        r[0] = BOOT_MAGIC;
        r[1] = 0;
    }
    return r;
}

bool safemode_keys_held(uint64_t (*held)(void), uint64_t mask)
{
    uint32_t before = hal_keypad_held_ms(mask); /* already held this long (hardware) */
    uint32_t t0 = hal_millis();
    for (;;) {
        if ((held() & mask) != mask)
            return false; /* not both, or one released before the time was up */
        if (before + (hal_millis() - t0) >= SAFEMODE_KEY_MS)
            return true;
        hal_watchdog_kick();
        hal_delay_ms(10);
    }
}

void safemode_boot(bool keys, int reset_cause)
{
    g_reset_cause = reset_cause;
    uint32_t *r = record();
    uint32_t prev = r ? r[1] : 0; /* boots that did not finish before this one */
    if (r)
        r[1] = prev + 1;
    g_counted_ok = false;
    g_boot_ms = hal_millis();
    g_reason = SAFEMODE_OFF;
    if (keys)
        g_reason = SAFEMODE_KEY;
    else if (prev >= SAFEMODE_CRASHES)
        g_reason = SAFEMODE_CRASHLOOP;
    if (g_force >= 0) {
        g_reason = g_force;
        g_force = -1;
    }
}

bool safemode_active(void) { return g_reason != SAFEMODE_OFF; }
int safemode_reason(void) { return g_reason; }

const char *safemode_reason_text(void)
{
    switch (g_reason) {
    case SAFEMODE_KEY: return "ON and CLEAR were held at power-on";
    case SAFEMODE_CRASHLOOP: return "the last starts did not finish (crash loop)";
    case SAFEMODE_FORCED: return "requested from the console";
    }
    return "off";
}

int safemode_reset_cause(void) { return g_reset_cause; }

const char *safemode_reset_text(void)
{
    switch (g_reset_cause) {
    case HAL_RESET_POWERON: return "power-on";
    case HAL_RESET_WAKE: return "switched on";
    case HAL_RESET_SOFTWARE: return "restart";
    case HAL_RESET_FAULT: return "crash (fault handler)";
    case HAL_RESET_WATCHDOG: return "watchdog (the firmware hung)";
    case HAL_RESET_PIN: return "reset pin";
    case HAL_RESET_BROWNOUT: return "brown-out (supply too low)";
    }
    return "other";
}

void safemode_tick(uint32_t now_ms)
{
    if (g_counted_ok || now_ms - g_boot_ms < SAFEMODE_OK_MS)
        return;
    g_counted_ok = true;
    uint32_t *r = record();
    if (r)
        r[1] = 0;
}

void safemode_clean_shutdown(void)
{
    uint32_t *r = record();
    if (r)
        r[1] = 0;
}

uint32_t safemode_attempts(void)
{
    uint32_t *r = record();
    return r ? r[1] : 0;
}

void safemode_test_force(int mode) { g_force = mode; }
