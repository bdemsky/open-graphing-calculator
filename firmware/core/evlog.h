/*
 * evlog — a small persistent log of important events (README.md "Diagnostics").
 *
 * The last EVLOG_N events (a QSPI erase/write that needed a reset and retry, a
 * forced QSPI idle, a module-store word that did not land, an unexpected start:
 * watchdog, crash, brown-out, safe mode, a USB start that failed, a session save
 * that failed, the low-battery power-off), each with the boot it happened in,
 * the time since that boot and a repeat count, in a fixed-size ring:
 *
 *   - in RAM that survives a reset (hardware: .noinit DTCM, like the crash
 *     record; simulators: a static), valid while magic and CRC-32 match. Standby
 *     (off) and a power loss lose it (the simulators model the Standby loss:
 *     evlog_power_lost() after a power-off);
 *   - in the saved session (blob "evlog", app/session.c), written at power-off
 *     and with the autosave; at boot the saved copy is merged into the RAM ring
 *     (evlog_merge: the same event, same boot + time + text, is kept once, with
 *     the larger repeat count; the newest EVLOG_N stay), so events survive a
 *     power-off and a battery change.
 *
 * The boot counter counts app starts (every reset and every switch-on); it is
 * kept in the RAM record and in the session, so after a power loss it goes on
 * from the saved value (the merge renumbers this boot's events).
 *
 * Shown on MEM > About ("Last events"), in INFO.TXT on the CALC drive and by
 * the console `evlog`. Main-loop context only (not from interrupts).
 */
#ifndef EVLOG_H
#define EVLOG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EVLOG_N 16
#define EVLOG_TEXT 54 /* text bytes per event, NUL included: 53 characters */

typedef struct {
    uint32_t boot;  /* boot number (evlog_boots() when it happened) */
    uint32_t ms;    /* ms since that boot, first occurrence */
    uint16_t count; /* times in a row (same boot, same text) */
    char text[EVLOG_TEXT];
} evlog_rec_t;      /* 64 bytes */

/* Record an event (printf format; cut at EVLOG_TEXT - 1 characters) and print
 * it as a console log line too ("# " + the full text). The same text as the
 * newest event of this boot only counts up its repeat count. */
void evlog_add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Record only (no console line): for call sites that already print a longer
 * con_log with the details. */
void evlog_put(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* A new app start (app_init): counts the boot. Events recorded before it in
 * the same run (hardware: from hal_init) already started this boot. */
void evlog_boot(void);
/* Simulators: switching off loses the RAM record as Standby does. */
void evlog_power_lost(void);

/* The events, oldest first; their number. Valid until the next add/merge/clear. */
int evlog_count(void);
const evlog_rec_t *evlog_get(int i);
uint32_t evlog_boots(void); /* this boot's number */
/* Forget every event (the boot counter stays). */
void evlog_clear(void);
/* Events added or merged since the last evlog_saved(): the session saves them. */
bool evlog_unsaved(void);
void evlog_saved(void);

/* Merge a saved copy (oldest first, `boots`: the boot counter it was saved
 * with): if it is not older than this boot's number, this boot is renumbered
 * after it (a power loss reset the RAM record); events present in both are
 * kept once; the newest EVLOG_N stay. */
void evlog_merge(const evlog_rec_t *recs, int n, uint32_t boots);

/* One event as a line: "boot N +12.3 s: TEXT" (+ " (3 times)"). */
void evlog_format(const evlog_rec_t *r, char *out, size_t n);

/* Test hook: forget the RAM record entirely (power-on state, boot counter 0). */
void evlog_test_reset(void);

#endif
