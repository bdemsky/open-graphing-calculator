/*
 * Session persistence: everything the user set up survives power-off
 * (Standby), battery changes and firmware updates.
 *
 * The session is a list of named blobs (persist_def_t, registered in
 * app/registry.c), serialized into one buffer and written as a snapshot to
 * the QSPI flash log (core/pstore.c: wear-levelled ring with CRCs, a power
 * loss mid-write keeps the previous snapshot). Saved on power-off
 * (platform_hibernate_save), automatically after a minute of inactivity
 * following a change, and with the console `session save`. Restored at boot.
 *
 * Payload layout (little-endian):
 *   "SES1" u16 nblobs u16 0
 *   per blob: char key[16] (NUL-padded), u16 version, u16 0, u32 len, data[len], pad to 4
 * Unknown keys are ignored on load (older/newer firmware), missing blobs keep
 * their defaults.
 */
#ifndef SESSION_H
#define SESSION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* QSPI layout (16 MB W25Q128, offsets from 0x90000000):
 *   0x000000-0x9FFFFF  the module store (core/modstore.h)
 *   0xA00000-0xDFFFFF  the notebook store (app/py_core.h NB_STORE_*, modules/python/nb_store.c)
 *   0xE00000-0xFFEFFF  session log (this module), 2 MB - 4 KB
 *   0xFFF000-0xFFFFFF  reserved for the hardware `qspi test` command */
#define SESSION_FLASH_BASE 0x00E00000u
#define SESSION_FLASH_SIZE 0x001FF000u
#define SESSION_MAX (256u << 10) /* largest session payload */

/* ---- writer / reader for blob contents (bounds-checked, little-endian) ----
 * The error flags are sticky: after the first failure every call is a no-op
 * (reads return 0 / ""), so check the flag once at the end. */
typedef struct {
    uint8_t *buf;
    size_t cap, len;
    bool overflow; /* set when something did not fit; the blob is then dropped */
} pw_t;
void pw_u8(pw_t *w, uint32_t v);
void pw_u16(pw_t *w, uint32_t v);
void pw_u32(pw_t *w, uint32_t v);
void pw_f64(pw_t *w, double v);
void pw_bytes(pw_t *w, const void *p, size_t n);
void pw_str(pw_t *w, const char *s); /* u16 length + bytes (no NUL) */

typedef struct {
    const uint8_t *buf;
    size_t len, pos;
    bool err; /* set on reading past the end; reads then return 0 / "" */
} pr_t;
uint32_t pr_u8(pr_t *r);
uint32_t pr_u16(pr_t *r);
uint32_t pr_u32(pr_t *r);
double pr_f64(pr_t *r);
bool pr_bytes(pr_t *r, void *p, size_t n);
void pr_str(pr_t *r, char *out, size_t n); /* always NUL-terminates (truncates) */
size_t pr_left(const pr_t *r);

/* ---- registration (see registry.h: persist_registry) ----
 * save() appends the feature's state with pw_*; load() reads it back with
 * pr_* from a reader that covers exactly this blob, receiving the version the
 * blob was written with, and must keep its defaults on r->err (the reader
 * covers the blob's padding to 4 bytes too: a field appended later is told
 * apart by a new version, never by r->err at the end); reset()
 * restores factory defaults. All three run in the main loop; save() runs on
 * every save attempt (also the autosave), so it must be fast. */
typedef struct persist_def {
    const char *key;   /* unique, <= 15 chars, e.g. "stat.lists" */
    uint16_t version;  /* stored with the blob; bump when the layout changes */
    void (*save)(pw_t *w);
    void (*load)(pr_t *r, unsigned version); /* only called when the blob exists */
    void (*reset)(void); /* optional: factory defaults (MEM > Reset all) */
} persist_def_t;

/* ---- API ---- */
/* Mount the flash log; buf/cap is the serialization buffer (from the arena). */
void session_init(void *buf, size_t cap);
/* Restore the newest snapshot (boot). Returns the number of blobs loaded. */
int session_load(void);
/* Serialize and write a snapshot unless it equals the last one (force: always).
 * 0 = ok or unchanged, <0 = error (the previous snapshot stays valid). */
int session_save(bool force);
/* Safe mode: neither load nor save (the saved session stays for the next
 * normal start); `why` (a literal) is shown on the About page. */
void session_disable(const char *why);
/* Forget the saved session (MEM > Reset all). */
int session_clear(void);
/* Activity hook (keys, touch): arms the idle autosave. */
void session_activity(void);
/* Called from app_tick: autosave after SESSION_IDLE_MS without input. */
void session_tick(uint32_t now_ms);
#define SESSION_IDLE_MS 60000u
/* A program asks for a save soon (a round won, a purchase, a placement: progress worth keeping before a
 * battery pull): the session is written at the first quiet moment (no input for SESSION_SOON_QUIET_MS)
 * once SESSION_SOON_GAP_MS have passed since the last write (debounced: at most a write every 30 s
 * of play from these; about 100 KB a write into the 2 MB wear-levelled log at 100 000 erases a
 * sector is decades of daily play -- docs/QSPI_CONTENT.md). */
void session_save_soon(void);
#define SESSION_SOON_GAP_MS 30000u
#define SESSION_SOON_QUIET_MS 1500u
/* Backup / restore of the whole session as a file (app/sdbk.c):
 * session_snapshot() serializes every blob into the session buffer, exactly
 * what a save would write, and returns it (NULL if there is no buffer);
 * session_buffer() is that buffer, for reading a backup file into;
 * session_install(len) checks that buffer[0..len) is a session payload
 * ("SES1") and writes it to the flash log as the newest snapshot (0 = ok,
 * <0 error). The caller then restarts the app (app_resume) so every module
 * loads it as after a power cycle. */
const uint8_t *session_snapshot(size_t *len);
uint8_t *session_buffer(size_t *cap);
int session_install(size_t len);

/* test hooks for the console (`session fail N`, `session limit BYTES`) */
void session_test_fail_writes(int n);
void session_test_limit(size_t cap);
/* one line for the About page */
void session_state_text(char *out, size_t n);
/* key=value lines for `state` / `session info` */
void session_state(void (*kv)(const char *, const char *, void *), void *user);

#endif
