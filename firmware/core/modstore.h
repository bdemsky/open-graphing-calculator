/*
 * modstore — installed content modules in the QSPI flash (design:
 * docs/QSPI_CONTENT.md).
 *
 * Content that does not fit the internal flash is packaged as modules: the
 * help manual is the first one ("manual"); CAS extension modules and data
 * tables follow in phase 2. A module is a unit with a name, a semantic
 * version, the core interface ("iface") and ABI range it needs,
 * dependencies on other modules (version ranges), a tier and the Kconfig
 * symbol that selects it. The firmware boots and works without any module;
 * each installed and compatible module enables its feature.
 *
 * Module file (.MOD on the microSD card, /CALC/MODULES) = installed extent,
 * byte for byte, except the state words:
 *
 *   0x0000  module header, 512 bytes (mod_hdr_t), CRC-protected
 *   0x0200  0xFF
 *   0x0F00  activation slots: MOD_NACT words, programmed one after the other
 *           with a store-wide sequence number; the extent's activation is the
 *           last programmed slot (0 = never activated)
 *   0x0FF0  commit word "CMIT" (programmed after the payload has been read
 *           back and verified; 0xFFFFFFFF = install not finished)
 *   0x0FF4  remove word (0xFFFFFFFF = present; 0 = removed, space reusable)
 *   0x1000  payload (payload_len bytes, CRC-32 + SHA-256 in the header)
 *
 * Store: the QSPI region 0x000000..0xDFFFFF (the session log and the test
 * sector above it are never touched) is a sequence of extents, each starting
 * on a 4 KB sector boundary and covering 4 KB + payload rounded up to 4 KB.
 * There is no central directory: mounting walks the region, reading 16 bytes
 * at each candidate sector and skipping over every valid extent, so a
 * damaged or half-written extent can only lose itself. Per-module state is
 * changed only by programming words of that module's own header sector
 * (commit, activation, remove), so installing, upgrading, reverting or
 * removing one module never touches another one.
 *
 * Which version is used: per module name, among the committed, not removed
 * extents, the one with the highest activation; it must also be compatible
 * (iface known to this core with its ABI inside [abi_min, abi_max], every
 * dependency active within its version range), else the module is
 * DISABLED with a reason. Its payload CRC is checked the first time it is
 * used after boot; a damaged active version falls back to the previous
 * installed one.
 *
 * Install keeps the previous version (for "revert") when space allows: it
 * first drops versions older than the active one, then allocates first-fit;
 * only if that fails is the active version removed to make room. Compaction
 * (copy an extent to a lower free gap as a new extent, then remove the old
 * one: each step power-safe) is designed (docs) but not needed yet.
 *
 * Portable C11, no malloc, main context only. Storage through pstore_dev_t
 * (core/pstore.h). A store may be read-only (erase/write NULL).
 */
#ifndef MODSTORE_H
#define MODSTORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pstore.h"

#define MOD_MAGIC 0x444F4D43u    /* "CMOD" */
#define MOD_FMT 1u
#define MOD_HDR_BYTES 512u
#define MOD_ACT_OFF 0xF00u       /* activation slots */
#define MOD_NACT 60
#define MOD_COMMIT_OFF 0xFF0u
#define MOD_REMOVE_OFF 0xFF4u
#define MOD_COMMIT 0x54494D43u   /* "CMIT" */
#define MOD_PAYLOAD_OFF 0x1000u
#define MOD_MAX_DEPS 8
#define MOD_MAX_EXTENTS 32       /* live extents tracked per store (versions of all modules) */
#define MOD_NAME_LEN 16

/* QSPI region of the store (the notebook store from 0xA00000, app/py_core.h NB_STORE_*; the
 * session log from 0xE00000, app/session.h). Until the notebook store the region was 14 MB. */
#define MODSTORE_QSPI_BASE 0x000000u
#define MODSTORE_QSPI_SIZE 0xA00000u
#define MODSTORE_SD_DIR "/CALC/MODULES"

enum { MOD_KIND_DATA = 0, MOD_KIND_CODE_XIP = 1, MOD_KIND_CODE_LOAD = 2 };
enum { MOD_TIER_CORE = 0, MOD_TIER_STANDARD = 1, MOD_TIER_ADVANCED = 2, MOD_TIER_DATA = 3 };

typedef struct {
    char name[MOD_NAME_LEN];
    uint16_t min[3], max[3]; /* inclusive version range */
} mod_dep_t;

typedef struct {
    uint32_t magic;
    uint16_t fmt, hdr_bytes;     /* MOD_FMT, MOD_HDR_BYTES */
    char name[MOD_NAME_LEN];     /* "manual" */
    uint16_t ver[3];             /* major, minor, patch */
    uint16_t kind;               /* MOD_KIND_* */
    char iface[8];               /* core interface it plugs into: "help", "cas" */
    uint16_t abi_min, abi_max;   /* interface ABI versions it works with */
    uint8_t tier, ndeps;
    uint16_t flags;              /* 0 (reserved) */
    char kconfig[24];            /* the Kconfig symbol that selects it, "MANUAL_FULL" */
    uint32_t payload_len, payload_crc;
    uint8_t sha256[32];          /* of the payload */
    mod_dep_t deps[MOD_MAX_DEPS];
    char built_with[24];         /* firmware version + rev of the build that made it */
    uint8_t pad[512 - 4 - 4 - 16 - 6 - 2 - 8 - 4 - 4 - 24 - 8 - 32 - 28 * MOD_MAX_DEPS - 24 - 4];
    uint32_t hcrc;               /* CRC-32 of the first 508 bytes */
} mod_hdr_t;
_Static_assert(sizeof(mod_dep_t) == 28, "mod_dep_t");
_Static_assert(sizeof(mod_hdr_t) == MOD_HDR_BYTES, "mod_hdr_t");

/* One interface the running core offers to modules. */
typedef struct {
    const char *iface;
    uint16_t abi;
} mod_iface_t;

/* State of a module (by name) as the firmware sees it. */
typedef enum {
    MOD_OK = 0,         /* active, compatible (payload CRC checked on first use) */
    MOD_NODEV,          /* the QSPI is not responding */
    MOD_ABSENT,         /* not installed */
    MOD_ABI,            /* installed, but needs a core interface/ABI this firmware lacks */
    MOD_DEPS,           /* installed, but a dependency is missing or out of range */
    MOD_CORRUPT,        /* installed, but its payload failed the CRC (and no older version works) */
    MOD_DISABLED,       /* safe mode */
    MOD_BUILTIN,        /* this build carries it in internal flash (CONTENT=internal) */
} mod_state_t;
const char *mod_state_name(mod_state_t s);   /* "ok", "absent", ... */
const char *mod_state_text(mod_state_t s);   /* sentence part: "not installed", ... */

/* A live (committed, not removed) extent found by the mount scan: a summary
 * of its header (the full header is re-read with modstore_header). */
typedef struct {
    char name[MOD_NAME_LEN];
    uint16_t ver[3], kind;
    uint32_t off;      /* store offset of its header sector */
    uint32_t span;     /* bytes it occupies (multiple of 4096) */
    uint32_t payload_len, payload_crc;
    uint32_t act;      /* activation (0 = never activated) */
    uint8_t nact;      /* activation slots used */
    uint8_t tier;
    bool abi_ok;       /* interface/ABI accepted by this core (mod_boot) */
    bool crc_checked, crc_bad;
} mod_extent_t;

typedef struct {
    const pstore_dev_t *dev;  /* NULL: no device */
    bool readonly;
    bool io_error;            /* a read failed during the scan */
    int n;                    /* live extents, in increasing offset order */
    mod_extent_t ext[MOD_MAX_EXTENTS];
    uint32_t max_act;         /* highest activation seen (next = max_act + 1) */
    uint32_t used;            /* bytes in live extents */
} modstore_t;

/* Scan a store. Never fails: an absent device gives an empty store with dev NULL. */
void modstore_mount(modstore_t *st, const pstore_dev_t *dev, bool readonly);
bool mod_header_ok(const mod_hdr_t *h, uint32_t max_payload);
int mod_vercmp(const uint16_t a[3], const uint16_t b[3]);
/* Full header of extent e (0 / -1). */
int modstore_header(const modstore_t *st, int e, mod_hdr_t *out);
/* Live extents of module `name`, newest activation first (out: indices). */
int modstore_versions(const modstore_t *st, const char *name, int *out, int max);
/* Compatibility of header h with the core's interfaces (deps ignored). */
bool mod_abi_ok(const mod_hdr_t *h, const mod_iface_t *ifs, int nifs);
int modstore_payload_crc(const modstore_t *st, int e, uint32_t *crc);

/* ---- install (push API: the module file arrives in order, any chunking) ----
 * 1. header bytes 0..511: format/CRC check, interface/ABI check (unless
 *    force), then the target extent is chosen (retention policy above), its
 *    sectors are erased and the header is programmed (state words erased);
 * 2. file bytes 512..4095 are ignored (padding, the file's own state words);
 * 3. payload: programmed as it arrives;
 * 4. finish: payload read back against its CRC, then the commit word, then an
 *    activation slot (the new version becomes the active one).
 * A power loss before 4 leaves an uncommitted extent (ignored, reusable);
 * the previously active version stays active throughout. */
enum {
    MOD_E_IO = -1, MOD_E_FORMAT = -2, MOD_E_ABI = -3, MOD_E_SIZE = -4, MOD_E_VERIFY = -5,
    MOD_E_NODEV = -6, MOD_E_FULL = -7, MOD_E_STATE = -8, MOD_E_NOTFOUND = -9,
};
const char *mod_strerror(int err);

typedef struct {
    modstore_t *st;
    const mod_iface_t *ifs;
    int nifs;
    bool force;
    int err;
    uint32_t pos;            /* file bytes received */
    uint32_t off;            /* target extent offset */
    uint32_t erased_to;      /* store offset up to which the target is erased */
    bool dropped_active;     /* the previous active version had to go to make room */
    mod_hdr_t hdr;
    uint8_t hbuf[MOD_HDR_BYTES];
} mod_inst_t;
int mod_inst_begin(mod_inst_t *in, modstore_t *st, const mod_iface_t *ifs, int nifs, bool force);
int mod_inst_write(mod_inst_t *in, const void *data, size_t n);
int mod_inst_finish(mod_inst_t *in);
uint32_t mod_inst_total(const mod_inst_t *in); /* file bytes expected (0 before the header) */

/* Make the newest live version older than the active one active again
 * (MOD_E_NOTFOUND if there is none). */
int modstore_revert(modstore_t *st, const char *name);
/* Mark every version of `name` removed. */
int modstore_remove(modstore_t *st, const char *name);
/* Move extents down into lower gaps that hold them whole (each move
 * power-safe: copy, verify, commit with the same activation, then remove the
 * original). Run by an install that finds no gap; console `module compact`. */
int modstore_compact(modstore_t *st);
/* Remove every module (console `module format`, Memory > Remove content). */
int modstore_format(modstore_t *st);

/* ---- the running firmware's modules (singleton used by the app) ----
 * The stores in priority order (one so far, the QSPI); a module name is served
 * by the first store in which it is active and compatible. */
enum { MODSTORE_QSPI, MODSTORE_N };
void mod_boot(const mod_iface_t *ifs, int nifs, bool disabled);
/* Re-scan after an install/remove (keeps the interfaces and the safe-mode flag). */
void mod_remount(void);
mod_state_t mod_state(const char *name);
/* Header of the serving version (a static copy, valid until the next call), NULL if none. */
const mod_hdr_t *mod_header(const char *name);
int mod_serving_store(const char *name);         /* MODSTORE_*, -1 */
/* Read n payload bytes at off of the serving version of `name`. The first
 * read after mount verifies the payload CRC (a mismatch falls back to the
 * previous version if one works, else the module turns CORRUPT). */
int mod_read(const char *name, uint32_t off, void *buf, size_t n);
int mod_verify(const char *name);
/* Pointer to the serving version's payload in mapped memory (the QSPI window;
 * NULL in the simulators, or if not served / damaged); *len = its
 * length. The XIP base of a code module (app/modx.c). Valid until the next
 * store write. */
const void *mod_map(const char *name, uint32_t *len);
modstore_t *mod_store(int s);
/* Names of all modules found in any store (for listings); returns the count. */
int mod_names(const char **out, int max);
/* Store index and extent index serving `name` (-1 if none). */
int mod_serving_extent(const char *name, int *store);
const mod_iface_t *mod_ifaces(int *n);
/* Test hook (console `module fail N`): N more QSPI erases/writes succeed, then
 * all fail until `module fail -1` (a power cut in the middle of an install). */
void mod_test_fail_after(long n);

#endif
