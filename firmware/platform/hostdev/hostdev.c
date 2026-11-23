/* Device models shared by the simulators (see hostdev.h). */
#include "hostdev.h"
#include <stdio.h>
#include <string.h>
#include "hal.h"

/* build/gen/content_img.c: the content image of this build (size 0 when the
 * build carries its content internally, CONTENT=internal) */
extern const uint8_t content_image[];
extern const uint32_t content_image_size;

bool hostdev_no_content;

enum { Q_OK, Q_ABSENT, Q_DEAD, Q_CUT };
static int qmode = Q_OK;
static uint8_t qfill = 0xFF; /* what a dead chip reads as */
static long qcut;            /* Q_CUT: modifications left before the "power cut" */
static bool sd_dead;

void hostdev_flash_attach(uint8_t *flash, size_t size, void (*sync)(uint32_t off, size_t n))
{
    if (hostdev_no_content || !content_image_size || content_image_size > size)
        return;
    if (!memcmp(flash, content_image, content_image_size))
        return;
    /* program it like the factory would: erase the sectors, then write */
    size_t span = (content_image_size + HAL_STORAGE_SECTOR - 1) / HAL_STORAGE_SECTOR * HAL_STORAGE_SECTOR;
    memset(flash, 0xFF, span);
    memcpy(flash, content_image, content_image_size);
    if (sync)
        sync(0, span);
}

size_t hostdev_flash_size(size_t real) { return qmode == Q_ABSENT ? 0 : real; }

bool hostdev_flash_read(void *buf, size_t n)
{
    if (qmode != Q_DEAD)
        return false;
    memset(buf, qfill, n);
    return true;
}

int hostdev_flash_modify(void)
{
    if (qmode == Q_DEAD || qmode == Q_ABSENT)
        return -1;
    if (qmode == Q_CUT) {
        if (qcut <= 0)
            return -1;
        qcut--;
    }
    return 0;
}

int hostdev_sd_fail(void) { return sd_dead ? -1 : 0; }
bool hostdev_sd_dead(void) { return sd_dead; }

int hostdev_cmd_fault(int argc, char **argv)
{
    if (argc < 3) {
        con_printf("qspi=%s\nsd=%s\n", qmode == Q_OK ? "ok" : qmode == Q_ABSENT ? "absent" : qmode == Q_DEAD ? "dead" : "cut",
                   sd_dead ? "dead" : "ok");
        if (argc < 2)
            return 0;
        return con_error("usage: fault qspi ok|absent|dead ff|dead 00|cut N, fault sd ok|dead");
    }
    if (!strcmp(argv[1], "qspi")) {
        if (!strcmp(argv[2], "ok"))
            qmode = Q_OK;
        else if (!strcmp(argv[2], "absent"))
            qmode = Q_ABSENT;
        else if (!strcmp(argv[2], "dead") && argc > 3 && (!strcmp(argv[3], "ff") || !strcmp(argv[3], "00"))) {
            qmode = Q_DEAD;
            qfill = !strcmp(argv[3], "ff") ? 0xFF : 0x00;
        } else if (!strcmp(argv[2], "cut") && argc > 3) {
            unsigned long n;
            if (!con_parse_u32(argv[3], &n))
                return con_error("bad count '%s'", argv[3]);
            qmode = Q_CUT;
            qcut = (long)n;
        } else
            return con_error("usage: fault qspi ok|absent|dead ff|dead 00|cut N");
        return 0;
    }
    if (!strcmp(argv[1], "sd")) {
        if (!strcmp(argv[2], "ok"))
            sd_dead = false;
        else if (!strcmp(argv[2], "dead"))
            sd_dead = true;
        else
            return con_error("usage: fault sd ok|dead");
        return 0;
    }
    return con_error("unknown device '%s' (qspi, sd)", argv[1]);
}

/* ------------------------------------------------------------ watchdog */
/* The model: started by calc_setup like the IWDG, expires HAL_WATCHDOG_MS of
 * (virtual) time after the last kick. Only the main loop checks it
 * (calc_step) and the console `wdg hang` loop; the app then restarts as after
 * a reset (app_crash_restart(HAL_RESET_WATCHDOG)). */
bool hostdev_wdg_model = true;
static bool wdg_on;
static uint32_t wdg_last;

void hal_watchdog_start(void)
{
#if defined(CONFIG_WATCHDOG)
    wdg_on = hostdev_wdg_model;
#endif
    wdg_last = hal_millis();
}
void hal_watchdog_kick(void) { wdg_last = hal_millis(); }
bool hal_watchdog_running(void) { return wdg_on; }
bool hal_watchdog_expired(void)
{
    if (!wdg_on || hal_millis() - wdg_last < HAL_WATCHDOG_MS)
        return false;
    wdg_last = hal_millis(); /* the "reset" starts it again */
    return true;
}
void hal_watchdog_status(char *out, size_t n)
{
#if !defined(CONFIG_WATCHDOG)
    snprintf(out, n, "off (disabled in this build: CONFIG_WATCHDOG)");
#else
    snprintf(out, n, "%s", wdg_on ? "on (2 s, simulated on the virtual clock)"
                                  : hostdev_wdg_model ? "off (not started yet)" : "off (simulator in real time)");
#endif
}

int hal_reset_cause(void) { return HAL_RESET_POWERON; }
uint32_t hal_keypad_held_ms(uint64_t mask)
{
    (void)mask;
    return 0; /* console-held keys are timed by safemode_keys_held itself */
}

/* ------------------------------------------------------------ hal.h */
uint32_t *hal_boot_record(void)
{
    static uint32_t rec[2];
    return rec;
}

/* No execute-in-place in the simulators: code modules are linked into the
 * host binary (app/modx.c), the store only says whether they are served. */
const void *hal_storage_xip(uint32_t off, size_t n)
{
    (void)off, (void)n;
    return NULL;
}
void hal_code_sync(void) {}
void hal_code_region(void *base, size_t size) { (void)base, (void)size; }

void hal_device_report(void (*kv)(const char *dev, const char *status, void *user), void *user)
{
    const char *q = "ok (emulated W25Q128, 16 MB)";
    if (qmode == Q_ABSENT)
        q = "not responding (JEDEC 000000)";
    else if (qmode == Q_DEAD)
        q = qfill == 0xFF ? "not responding (reads ff, writes fail)" : "not responding (reads 00, writes fail)";
    else if (qmode == Q_CUT && qcut <= 0)
        q = "writes failing (power cut injected)";
    kv("QSPI", q, user);
    kv("microSD", sd_dead ? "not responding" : hal_sd_present() ? "card present" : "no card", user);
    kv("Touch", hal_touch_available() ? "ok (emulated)" : "not found (keys only)", user);
    kv("Power monitor", "ok (emulated)", user);
    kv("SDRAM", "ok (emulated)", user);
    char w[96];
    hal_watchdog_status(w, sizeof w);
    kv("Watchdog", w, user);
}
