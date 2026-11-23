/* Persistent event log: a ring of the last events in reset-proof RAM (see evlog.h). */
#include "evlog.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "console.h"
#include "hal.h"
#include "rle.h" /* crc32_update */
#include "sections.h"

#define EVLOG_MAGIC 0x45564C31u /* "EVL1": the record is valid (power-on leaves random data) */

/* Oldest event first: rec[0 .. n). A full ring drops rec[0] (a memmove of 1 KB,
 * only when an event is added). The CRC covers everything before it. */
typedef struct {
    uint32_t magic;
    uint32_t boots; /* this boot's number */
    uint32_t n;
    evlog_rec_t rec[EVLOG_N];
    uint32_t crc;
} evlog_ram_t;

static evlog_ram_t g_ram NOINIT;
static bool g_started;     /* this run has its boot number (.bss: false after every reset) */
static bool g_app_started; /* evlog_boot() ran in this run (simulators: app restarts) */
static uint32_t g_t0;      /* hal_millis() at the start of this boot */
static bool g_unsaved;

static uint32_t ram_crc(void) { return crc32_update(0, &g_ram, offsetof(evlog_ram_t, crc)); }
static void seal(void) { g_ram.crc = ram_crc(); }

/* Garbage (power-on) or a damaged record: start empty, boot counter 0. */
static void valid(void)
{
    if (g_ram.magic == EVLOG_MAGIC && g_ram.n <= EVLOG_N && g_ram.crc == ram_crc())
        return;
    memset(&g_ram, 0, sizeof g_ram);
    g_ram.magic = EVLOG_MAGIC;
    seal();
}

static void start(void)
{
    valid();
    g_ram.boots++;
    seal();
    g_started = true;
#if defined(PLATFORM_STM32)
    g_t0 = 0; /* the millisecond counter starts at the reset */
#else
    g_t0 = hal_millis(); /* the simulators' clock runs on across app restarts */
#endif
}

void evlog_boot(void)
{
    if (!g_started || g_app_started)
        start();
    g_app_started = true;
}

void evlog_power_lost(void)
{
    g_ram.magic = 0;
    g_started = false;
    g_app_started = false;
}

static void put(const char *text)
{
    if (!g_started)
        start();
    else
        valid();
    evlog_rec_t *last = g_ram.n ? &g_ram.rec[g_ram.n - 1] : NULL;
    if (last && last->boot == g_ram.boots && !strncmp(last->text, text, EVLOG_TEXT - 1)) {
        if (last->count < 0xFFFFu)
            last->count++;
    } else {
        if (g_ram.n == EVLOG_N) {
            memmove(&g_ram.rec[0], &g_ram.rec[1], (EVLOG_N - 1) * sizeof g_ram.rec[0]);
            g_ram.n--;
        }
        evlog_rec_t *r = &g_ram.rec[g_ram.n++];
        memset(r, 0, sizeof *r);
        r->boot = g_ram.boots;
        r->ms = hal_millis() - g_t0;
        r->count = 1;
        strncpy(r->text, text, EVLOG_TEXT - 1);
    }
    g_unsaved = true;
    seal();
}

void evlog_add(const char *fmt, ...)
{
    char b[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    con_log("%s", b);
    put(b);
}

void evlog_put(const char *fmt, ...)
{
    char b[EVLOG_TEXT];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    put(b);
}

int evlog_count(void)
{
    valid();
    return (int)g_ram.n;
}

const evlog_rec_t *evlog_get(int i)
{
    valid();
    return i >= 0 && i < (int)g_ram.n ? &g_ram.rec[i] : NULL;
}

uint32_t evlog_boots(void)
{
    valid();
    return g_ram.boots;
}

void evlog_clear(void)
{
    valid();
    g_ram.n = 0;
    memset(g_ram.rec, 0, sizeof g_ram.rec);
    seal();
    g_unsaved = true;
}

bool evlog_unsaved(void) { return g_unsaved; }
void evlog_saved(void) { g_unsaved = false; }

FLASH2_TEXT static bool same(const evlog_rec_t *a, const evlog_rec_t *b)
{
    return a->boot == b->boot && a->ms == b->ms && !strncmp(a->text, b->text, EVLOG_TEXT);
}

FLASH2_TEXT void evlog_merge(const evlog_rec_t *recs, int n, uint32_t boots)
{
    if (!g_started)
        start();
    else
        valid();
    if (boots >= g_ram.boots) { /* the RAM record was lost (power): this boot comes after the saved ones */
        uint32_t nb = boots + 1;
        for (uint32_t i = 0; i < g_ram.n; i++)
            if (g_ram.rec[i].boot == g_ram.boots)
                g_ram.rec[i].boot = nb;
        g_ram.boots = nb; /* saved at the next power-off or autosave (not a reason for a save of its own) */
    }
    evlog_rec_t all[2 * EVLOG_N];
    int m = (int)g_ram.n, ram_n = m, matched = 0;
    memcpy(all, g_ram.rec, (size_t)m * sizeof all[0]);
    for (int i = 0; i < n && i < EVLOG_N; i++) {
        evlog_rec_t r = recs[i];
        r.text[EVLOG_TEXT - 1] = 0;
        int k = 0;
        while (k < ram_n && !same(&all[k], &r))
            k++;
        if (k < ram_n) { /* in both: RAM is newer, unless the saved copy counted more */
            if (r.count > all[k].count)
                all[k].count = r.count;
            matched++;
        } else {
            all[m++] = r;
        }
    }
    if (matched < ram_n)
        g_unsaved = true; /* events the saved copy does not have yet */
    /* oldest first by (boot, time); stable, so a boot's events keep their order */
    for (int i = 1; i < m; i++) {
        evlog_rec_t t = all[i];
        int j = i;
        while (j > 0 && (all[j - 1].boot > t.boot || (all[j - 1].boot == t.boot && all[j - 1].ms > t.ms))) {
            all[j] = all[j - 1];
            j--;
        }
        all[j] = t;
    }
    int keep = m < EVLOG_N ? m : EVLOG_N;
    memset(g_ram.rec, 0, sizeof g_ram.rec);
    memcpy(g_ram.rec, all + (m - keep), (size_t)keep * sizeof all[0]);
    g_ram.n = (uint32_t)keep;
    seal();
}

FLASH2_TEXT void evlog_format(const evlog_rec_t *r, char *out, size_t n)
{
    char t[32];
    unsigned long s = r->ms / 1000u;
    if (s < 100)
        snprintf(t, sizeof t, "%lu.%lu s", s, (unsigned long)(r->ms % 1000u) / 100u);
    else if (s < 3600)
        snprintf(t, sizeof t, "%lu min %lu s", s / 60u, s % 60u);
    else
        snprintf(t, sizeof t, "%lu h %lu min", s / 3600u, s / 60u % 60u);
    char c[24] = "";
    if (r->count > 1)
        snprintf(c, sizeof c, " (%u times)", (unsigned)r->count);
    snprintf(out, n, "boot %lu +%s: %.*s%s", (unsigned long)r->boot, t, EVLOG_TEXT - 1, r->text, c);
}

void evlog_test_reset(void)
{
    memset(&g_ram, 0, sizeof g_ram);
    g_started = g_app_started = g_unsaved = false;
}
