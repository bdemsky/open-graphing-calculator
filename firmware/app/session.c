/* Session persistence: registered blobs -> one snapshot in the QSPI flash log.
 * See session.h.
 *
 * Save: serialize() runs every persist_registry[] save() into the session
 * buffer (from the arena, app_init), the CRC-32 of the result is compared
 * with what is already in flash, and only a changed session is handed to
 * pstore_save() (core/pstore.c), which writes a new snapshot and keeps the
 * previous one valid until the new one is complete.
 * Load: pstore_load() returns the newest valid snapshot; each blob whose key
 * is registered is passed to its load() with a reader limited to the blob.
 * Triggers: power-off (platform_hibernate_save), the idle autosave
 * (session_tick), MEM > Save session now and the console.
 * microSD fallback (CONFIG_SESSION_SD_FALLBACK, docs/QSPI_CONTENT.md): when
 * the QSPI log is unusable (no chip, or a save to it failed), the payload is
 * written to /CALC/AUTOSAVE.SES (payload + CRC-32; the FAT layer publishes a
 * file only on close, so a power loss keeps the old one); at boot it is read
 * when the log has no snapshot. Reset all deletes it.
 * Safe mode (core/safemode.h): session_disable() stops both loading and
 * saving, so the saved session survives untouched for the next normal boot.
 * All of this runs in the main-loop context. */
#include "anim.h"
#include "modx.h"
#include "session.h"
#include <stdio.h>
#include <string.h>
#include "console.h"
#include "evlog.h"
#include "fat.h"
#include "hal.h"
#include "pstore.h"
#include "registry.h"
#include "rle.h" /* crc32_update */
#include "py_core.h" /* the Notebook's save at power-off */

/* ------------------------------------------------------------ pw / pr
 * Little-endian, bounds-checked writer and reader. Errors are sticky: once a
 * write does not fit (overflow) or a read runs past the end (err), every later
 * call does nothing / returns 0, so save() and load() functions need not check
 * after every field — they test the flag once at the end. */

/* Append n bytes, or set w->overflow (and append nothing) if they do not fit. */
static void put(pw_t *w, const void *p, size_t n)
{
    if (w->overflow || w->len + n > w->cap) {
        w->overflow = true;
        return;
    }
    memcpy(w->buf + w->len, p, n);
    w->len += n;
}

void pw_u8(pw_t *w, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    put(w, &b, 1);
}
void pw_u16(pw_t *w, uint32_t v)
{
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    put(w, b, 2);
}
void pw_u32(pw_t *w, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(w, b, 4);
}
/* A double as its IEEE-754 bit pattern (low word first): exact round trip,
 * including NaN and infinities. */
void pw_f64(pw_t *w, double v)
{
    uint64_t u;
    memcpy(&u, &v, 8);
    pw_u32(w, (uint32_t)u);
    pw_u32(w, (uint32_t)(u >> 32));
}
void pw_bytes(pw_t *w, const void *p, size_t n) { put(w, p, n); }
/* u16 length + bytes; NULL is written as ""; longer strings are cut at 65535
 * bytes. */
void pw_str(pw_t *w, const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n > 0xFFFF)
        n = 0xFFFF;
    pw_u16(w, (uint32_t)n);
    put(w, s, n);
}

/* Consume n bytes: a pointer into the buffer, or NULL (and r->err set) if
 * fewer than n are left. */
static const uint8_t *get(pr_t *r, size_t n)
{
    if (r->err || r->pos + n > r->len) {
        r->err = true;
        return NULL;
    }
    const uint8_t *p = r->buf + r->pos;
    r->pos += n;
    return p;
}

uint32_t pr_u8(pr_t *r)
{
    const uint8_t *p = get(r, 1);
    return p ? p[0] : 0;
}
uint32_t pr_u16(pr_t *r)
{
    const uint8_t *p = get(r, 2);
    return p ? (uint32_t)(p[0] | p[1] << 8) : 0;
}
uint32_t pr_u32(pr_t *r)
{
    const uint8_t *p = get(r, 4);
    return p ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : 0;
}
double pr_f64(pr_t *r)
{
    uint64_t lo = pr_u32(r), hi = pr_u32(r);
    uint64_t u = lo | hi << 32;
    double v;
    memcpy(&v, &u, 8);
    return r->err ? 0.0 : v;
}
bool pr_bytes(pr_t *r, void *p, size_t n)
{
    const uint8_t *s = get(r, n);
    if (!s)
        return false;
    memcpy(p, s, n);
    return true;
}
/* Read a pw_str() string into out (n bytes, always NUL-terminated when n > 0).
 * The whole stored string is consumed even if it is truncated in out; on a
 * read error out is "". */
void pr_str(pr_t *r, char *out, size_t n)
{
    size_t l = pr_u16(r);
    const uint8_t *s = get(r, l);
    if (!n)
        return;
    if (!s)
        l = 0;
    size_t c = l < n - 1 ? l : n - 1;
    if (c)
        memcpy(out, s, c);
    out[c] = 0;
}
size_t pr_left(const pr_t *r) { return r->err ? 0 : r->len - r->pos; }

/* ------------------------------------------------------------ flash log */
/* The pstore device: the platform's storage (QSPI flash on hardware, a RAM or
 * file image in the simulators) restricted to the session region. */
static int dev_read(uint32_t off, void *buf, size_t n) { return hal_storage_read(off, buf, n); }
static int dev_erase(uint32_t off, size_t n) { return hal_storage_erase(off, n); }
static int fail_writes; /* test hook (console `session fail N`): the next N writes fail */
static int dev_write(uint32_t off, const void *buf, size_t n)
{
    if (fail_writes > 0) {
        fail_writes--;
        return -1;
    }
    return hal_storage_write(off, buf, n);
}
static const pstore_dev_t dev = {dev_read, dev_erase, dev_write, SESSION_FLASH_BASE, SESSION_FLASH_SIZE,
                                 HAL_STORAGE_SECTOR};

static pstore_t ps;
static bool ps_ok;          /* the flash log is mounted (false: saving is impossible) */
static uint8_t *sbuf;       /* serialization buffer (arena) */
static size_t scap;         /* its usable size: <= SESSION_MAX and <= the log's max payload */
static size_t scap_full;    /* scap as set by session_init (session_test_limit restores it) */
static uint32_t last_crc, last_len; /* what is in flash now (skip identical saves) */
static bool last_valid;
static bool dirty;          /* input since the last save */
static uint32_t last_input, save_ms, load_ms; /* ms: last key/touch; durations for `state` */
static bool soon;            /* a save asked for (session_save_soon) */
static uint32_t last_write;  /* when the session was last written (ms; 0: not yet) */
static int nloaded, nsaved, last_rc;          /* statistics for `session info` */
static char last_dropped[192];                /* keys of the blobs that did not fit, comma-separated */
static const char *disabled_why;              /* session_disable(): nothing is loaded or saved */
static int sd_state;                          /* fallback: 0 unused, 1 saved on SD, 2 loaded from SD, -1 SD failed */

#define SD_AUTOSAVE "/CALC/AUTOSAVE.SES"

/* microSD fallback: write sbuf[0..len) + CRC-32 as SD_AUTOSAVE (0 / <0). */
static int sd_save(size_t len)
{
#if defined(CONFIG_SESSION_SD_FALLBACK)
    if (!hal_sd_present())
        return sd_state = -1;
    fs_file_t f;
    uint32_t crc = crc32_update(0, sbuf, len);
    int r = fs_mkdir("/CALC");
    if (!r)
        r = fs_open(&f, SD_AUTOSAVE, FS_WRITE);
    if (!r && (fs_write(&f, sbuf, len) != (int)len || fs_write(&f, &crc, 4) != 4))
        r = FS_EIO; /* fs_write dropped the file */
    else if (!r)
        r = fs_close(&f);
    fs_unmount();
    sd_state = r ? -1 : 1;
    return r ? -1 : 0;
#else
    (void)len;
    return -1;
#endif
}

/* microSD fallback: read SD_AUTOSAVE into sbuf; its payload length, or -1. */
static long sd_load(void)
{
#if defined(CONFIG_SESSION_SD_FALLBACK)
    if (!hal_sd_present())
        return -1;
    fs_file_t f;
    long len = -1;
    if (fs_open(&f, SD_AUTOSAVE, FS_READ) == 0) {
        size_t got = 0;
        int k;
        while (got < scap && (k = fs_read(&f, sbuf + got, scap - got)) > 0)
            got += (size_t)k;
        fs_close(&f);
        uint32_t crc;
        if (got >= 12) {
            memcpy(&crc, sbuf + got - 4, 4);
            if (crc == crc32_update(0, sbuf, got - 4))
                len = (long)(got - 4);
        }
    }
    fs_unmount();
    if (len > 0)
        sd_state = 2;
    return len;
#else
    return -1;
#endif
}

void session_disable(const char *why)
{
    disabled_why = why;
    ps_ok = false;
}

/* Mount the flash log if the platform's storage is large enough to hold the
 * session region and scanning it succeeds (an empty region is fine). Without
 * it the calculator works normally but nothing is saved. Re-run on every
 * app_init(), so last_valid/dirty start afresh. */
void session_init(void *buf, size_t cap)
{
    sbuf = buf;
    scap = cap < SESSION_MAX ? cap : SESSION_MAX;
    ps_ok = false;
    disabled_why = NULL;
    sd_state = 0;
    last_valid = false;
    dirty = false;
    if (hal_storage_size() >= SESSION_FLASH_BASE + SESSION_FLASH_SIZE && pstore_mount(&ps, &dev) == 0)
        ps_ok = true;
    if (ps_ok && pstore_max_payload(&ps) < scap)
        scap = pstore_max_payload(&ps);
    scap_full = scap;
}

#define KEYLEN 16 /* blob key field: up to 15 characters + NUL padding */

/* Write the payload (layout in session.h) into sbuf and return its length.
 * Each blob is framed (key, version, reserved u16, u32 length placeholder),
 * its save() appends the data, and the blob is padded to a multiple of 4
 * bytes. A blob that overflows the buffer is rolled back to its start and
 * skipped (its name is appended to last_dropped), so the blobs after it still
 * get a chance; that blob keeps its defaults after the next boot. The length field
 * and the blob count in the header are patched in place afterwards. */
static size_t serialize(void)
{
    pw_t w = {sbuf, scap, 0, false};
    pw_bytes(&w, "SES1", 4);
    pw_u16(&w, 0); /* blob count, patched below */
    pw_u16(&w, 0);
    int n = 0;
    last_dropped[0] = 0;
    for (int i = 0; i < persist_registry_count; i++) {
        const persist_def_t *d = persist_registry[i];
        size_t start = w.len;
        char key[KEYLEN] = {0};
        strncpy(key, d->key, KEYLEN - 1);
        pw_bytes(&w, key, KEYLEN);
        pw_u16(&w, d->version);
        pw_u16(&w, 0);
        size_t lenpos = w.len;
        pw_u32(&w, 0);
        size_t data = w.len;
        if (!w.overflow)
            d->save(&w);
        while (!w.overflow && (w.len & 3))
            pw_u8(&w, 0);
        if (w.overflow) { /* drop this blob, keep the others */
            w.len = start;
            w.overflow = false;
            size_t l = strlen(last_dropped);
            snprintf(last_dropped + l, sizeof last_dropped - l, "%s%s", l ? "," : "", d->key);
            continue;
        }
        uint32_t len = (uint32_t)(w.len - data);
        sbuf[lenpos] = (uint8_t)len, sbuf[lenpos + 1] = (uint8_t)(len >> 8);
        sbuf[lenpos + 2] = (uint8_t)(len >> 16), sbuf[lenpos + 3] = (uint8_t)(len >> 24);
        n++;
    }
    sbuf[4] = (uint8_t)n, sbuf[5] = (uint8_t)(n >> 8);
    nsaved = n;
    return w.len;
}

/* Serialize and, if the payload differs from the snapshot in flash (same CRC
 * and length = unchanged) or `force` is set, write it. Clears the autosave
 * flag when the session is in flash (or there is no flash to save to); after
 * a failed write it stays set and the idle timer restarts, so the autosave
 * retries SESSION_IDLE_MS later. Takes ~50-100 ms per erased 4 KB sector on
 * hardware. */
int session_save(bool force)
{
    if (modx_busy()) {
        /* reached from code-module code (app/modx.h): a QSPI write would take
         * the memory-mapped window away from the code that is running; the
         * idle autosave or the next power-off saves instead */
        dirty = true;
        return -2;
    }
    if (!sbuf)
        return -1;
    uint32_t t0 = hal_millis();
    hal_watchdog_kick(); /* a save can block for seconds; the storage drivers kick while they wait */
    size_t len = serialize();
    uint32_t crc = crc32_update(0, sbuf, len);
    if (!force && last_valid && crc == last_crc && len == last_len) {
        dirty = false;
        last_rc = 0;
        return 0;
    }
    if (!ps_ok) { /* no usable flash log: the microSD card, unless disabled (safe mode) */
        dirty = false;
        last_rc = disabled_why ? -1 : sd_save(len);
        if (last_rc && !disabled_why) {
            evlog_add("session: not saved (no flash, no microSD card)");
            evlog_saved(); /* not a reason to try again: that would only fail again */
        }
        if (last_rc == 0) {
            last_write = hal_millis() ? hal_millis() : 1;
            last_crc = crc;
            last_len = (uint32_t)len;
            last_valid = true;
        }
        return last_rc;
    }
    int rc = pstore_save(&ps, sbuf, len);
    hal_watchdog_kick();
    if (rc && !disabled_why && sd_save(len) == 0) /* the flash failed: keep the work on the card */
        rc = 0;
    last_rc = rc;
    save_ms = hal_millis() - t0;
    if (rc == 0) {
        last_write = hal_millis() ? hal_millis() : 1;
        last_crc = crc;
        last_len = (uint32_t)len;
        last_valid = true;
        dirty = false;
    } else {
        evlog_add("session: save failed (%d)", rc);
        evlog_saved(); /* the retry below is the autosave's own */
        last_input = hal_millis(); /* retry after another idle period */
    }
    return rc;
}

/* The registered blob with this key (compared over the 16-byte key field). */
static const persist_def_t *find_def(const char *key)
{
    for (int i = 0; i < persist_registry_count; i++)
        if (!strncmp(persist_registry[i]->key, key, KEYLEN))
            return persist_registry[i];
    return NULL;
}

/* Restore the newest snapshot: walk the blobs and give each registered one
 * its own reader (a corrupt blob can only fail its own load; it cannot read
 * into the next blob). Unknown keys are skipped by length; a truncated frame
 * ends the walk, keeping what was loaded so far. The snapshot's CRC is
 * remembered so an unchanged session is not written back. Returns the number
 * of blobs loaded (0: nothing saved, no flash, or not a "SES1" payload).
 * Every registered reset() runs first, so state that has no blob in the
 * snapshot gets its defaults instead of what was in RAM: the simulators'
 * power cycles and reboots, and a restore from the microSD card on every
 * platform, restart without clearing statics (app_resume). */
int session_load(void)
{
    nloaded = 0;
    for (int i = 0; i < persist_registry_count; i++)
        if (persist_registry[i]->reset)
            persist_registry[i]->reset();
    if (!sbuf || disabled_why)
        return 0;
    uint32_t t0 = hal_millis();
    long len = ps_ok ? pstore_load(&ps, sbuf, scap) : 0;
    if (len <= 0) /* nothing in the flash log: a fallback copy on the microSD card? */
        len = sd_load();
    if (len < 8 || memcmp(sbuf, "SES1", 4))
        return 0;
    last_crc = crc32_update(0, sbuf, (size_t)len);
    last_len = (uint32_t)len;
    last_valid = true;
    pr_t r = {sbuf, (size_t)len, 8, false};
    unsigned n = sbuf[4] | sbuf[5] << 8;
    for (unsigned i = 0; i < n && !r.err; i++) {
        char key[KEYLEN + 1] = {0};
        pr_bytes(&r, key, KEYLEN);
        unsigned ver = pr_u16(&r);
        pr_u16(&r);
        uint32_t blen = pr_u32(&r);
        if (r.err || blen > pr_left(&r))
            break;
        const persist_def_t *d = find_def(key);
        if (d) {
            pr_t br = {sbuf + r.pos, blen, 0, false};
            d->load(&br, ver);
            nloaded++;
        }
        r.pos += blen;
    }
    load_ms = hal_millis() - t0;
    return nloaded;
}

/* Erase the whole log region (no snapshot left: the next boot starts with
 * defaults unless something is saved again before that). */
int session_clear(void)
{
    last_valid = false;
#if defined(CONFIG_SESSION_SD_FALLBACK)
    if (hal_sd_present()) { /* the fallback copy would otherwise come back at the next boot */
        fs_remove(SD_AUTOSAVE);
        fs_unmount();
    }
#endif
    return ps_ok ? pstore_format(&ps) : -1;
}

void session_activity(void)
{
    dirty = true;
    last_input = hal_millis();
}

/* Autosave once SESSION_IDLE_MS have passed since the last input, so a save
 * (and its flash erase) never delays typing. */
void session_save_soon(void) { soon = true; }
void session_tick(uint32_t now)
{
    if (evlog_unsaved() && !dirty && !disabled_why) { /* a new event: saved with the next autosave */
        dirty = true;
        last_input = now;
    }
    if (dirty && now - last_input >= SESSION_IDLE_MS)
        session_save(false);
    else if (soon && dirty && now - last_input >= SESSION_SOON_QUIET_MS &&
             (!last_write || now - last_write >= SESSION_SOON_GAP_MS))
        session_save(false);
    if (!dirty)
        soon = false;
}

/* Called by the platform before power-off (hardware: Standby entry; simulators:
 * hal_power_off). */
int platform_hibernate_save(void)
{
    anim_sleep(); /* a playing animation pauses (and stores its value) first */
    py_nb_flush(); /* the open notebook's unsaved changes (app/py_core.h) */
    return session_save(false);
}

/* Backup / restore (session.h). The snapshot is the save payload; install
 * replaces the flash snapshot with a payload read from a file, after a
 * structural check (header and blob frames within the length), so a
 * corrupt file cannot leave an unreadable session behind. */
const uint8_t *session_snapshot(size_t *len)
{
    if (!sbuf)
        return NULL;
    *len = serialize();
    return sbuf;
}

uint8_t *session_buffer(size_t *cap)
{
    *cap = scap;
    return sbuf;
}

int session_install(size_t len)
{
    if (!sbuf || len < 8 || len > scap || memcmp(sbuf, "SES1", 4))
        return -2;
    size_t pos = 8;
    unsigned n = sbuf[4] | sbuf[5] << 8;
    for (unsigned i = 0; i < n; i++) { /* every frame must fit */
        if (pos + KEYLEN + 8 > len)
            return -2;
        size_t blen = sbuf[pos + KEYLEN + 4] | sbuf[pos + KEYLEN + 5] << 8 | (size_t)sbuf[pos + KEYLEN + 6] << 16 |
                      (size_t)sbuf[pos + KEYLEN + 7] << 24;
        pos += KEYLEN + 8;
        if (blen > len - pos)
            return -2;
        pos += blen;
    }
    if (!ps_ok)
        return -1;
    int rc = pstore_save(&ps, sbuf, len);
    last_rc = rc;
    if (rc == 0) {
        last_crc = crc32_update(0, sbuf, len);
        last_len = (uint32_t)len;
        last_valid = true;
        dirty = false;
    }
    return rc;
}

/* Test hooks (console `session fail N` / `session limit BYTES`): make the
 * next n flash writes fail; limit the serialization buffer to cap bytes (0:
 * back to the full buffer; app_init's session_init also restores it). */
void session_test_fail_writes(int n) { fail_writes = n; }
void session_test_limit(size_t cap) { scap = cap && cap < scap_full ? cap : scap_full; }

/* `state` keys: flash present, snapshot sequence number and size, blobs saved
 * and loaded, last save result, the blobs dropped by the last save, whether an
 * autosave is pending, timings, pstore write/erase counters and the usable
 * buffer size. */
void session_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[64];
    kv("session_flash", ps_ok ? "1" : "0", user);
    snprintf(b, sizeof b, "%lu", ps_ok && ps.have ? (unsigned long)ps.seq : 0ul);
    kv("session_seq", b, user);
    snprintf(b, sizeof b, "%lu", ps_ok && ps.have ? (unsigned long)ps.len : 0ul);
    kv("session_bytes", b, user);
    snprintf(b, sizeof b, "%d", nsaved);
    kv("session_blobs", b, user);
    snprintf(b, sizeof b, "%d", nloaded);
    kv("session_loaded", b, user);
    snprintf(b, sizeof b, "%d", last_rc);
    kv("session_rc", b, user);
    kv("session_dropped", last_dropped, user);
    kv("session_dirty", dirty ? "1" : "0", user);
    snprintf(b, sizeof b, "%lu", (unsigned long)save_ms);
    kv("session_save_ms", b, user);
    snprintf(b, sizeof b, "%lu", (unsigned long)load_ms);
    kv("session_load_ms", b, user);
    snprintf(b, sizeof b, "%lu/%lu", ps_ok ? (unsigned long)ps.writes : 0ul, ps_ok ? (unsigned long)ps.erases : 0ul);
    kv("session_writes_erases", b, user);
    snprintf(b, sizeof b, "%lu", (unsigned long)scap);
    kv("session_max", b, user);
}

void session_state_text(char *out, size_t n)
{
    if (disabled_why)
        snprintf(out, n, "Session: not loaded or saved (%s); the saved one is kept for the next start", disabled_why);
    else if (sd_state == 1 || (sd_state == 2 && !ps_ok))
        snprintf(out, n, "Session: kept on the microSD card (%s), the QSPI flash is not usable", SD_AUTOSAVE);
    else if (sd_state == 2)
        snprintf(out, n, "Session: restored from the microSD card copy (%s); saved to flash again from now on",
                 SD_AUTOSAVE);
    else if (!ps_ok)
        snprintf(out, n, "Session: NOT saved: the QSPI flash is not responding%s",
                 sd_state < 0 ? " and the microSD card failed" : "; insert a microSD card to keep it");
    else if (last_rc < 0)
        snprintf(out, n, "Session: the last save FAILED (flash error)");
    else if (!ps.have)
        snprintf(out, n, "Session: not saved yet");
    else
        snprintf(out, n, "Session: %lu bytes saved (snapshot %lu)", (unsigned long)ps.len, (unsigned long)ps.seq);
}

/* ------------------------------------------------------------ event log */
/* Blob "evlog" (core/evlog.h), version 1: u32 boot counter, u8 count, then per
 * event (oldest first) u32 boot, u32 ms, u16 repeat count, str text. Loading
 * merges it into the RAM ring (which may hold newer events of a reset); there
 * is no reset(): Reset all and the reset before every load keep the log. */
static void evlog_save(pw_t *w)
{
    int n = evlog_count();
    pw_u32(w, evlog_boots());
    pw_u8(w, (uint32_t)n);
    for (int i = 0; i < n; i++) {
        const evlog_rec_t *r = evlog_get(i);
        pw_u32(w, r->boot);
        pw_u32(w, r->ms);
        pw_u16(w, r->count);
        pw_str(w, r->text);
    }
    evlog_saved();
}

static void evlog_load(pr_t *r, unsigned version)
{
    if (version != 1)
        return;
    evlog_rec_t recs[EVLOG_N];
    memset(recs, 0, sizeof recs);
    uint32_t boots = pr_u32(r);
    unsigned n = pr_u8(r);
    if (n > EVLOG_N)
        return;
    for (unsigned i = 0; i < n; i++) {
        recs[i].boot = pr_u32(r);
        recs[i].ms = pr_u32(r);
        recs[i].count = (uint16_t)pr_u16(r);
        pr_str(r, recs[i].text, sizeof recs[i].text);
    }
    if (!r->err)
        evlog_merge(recs, (int)n, boots);
}

const persist_def_t persist_evlog = {"evlog", 1, evlog_save, evlog_load, NULL};
