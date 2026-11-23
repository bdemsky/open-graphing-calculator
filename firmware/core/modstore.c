/* Module store in the QSPI flash: scan, compatibility, read, install, revert,
 * remove (format and guarantees in modstore.h; design in docs/QSPI_CONTENT.md).
 *
 * Users: app/help.c reads the manual module through mod_read();
 * app/modules_ui.c installs from the microSD card and the console and backs
 * Memory > Modules; app/app.c calls mod_boot() at start-up (disabled in safe
 * mode). tests/unit/test_modstore.c checks every state and cuts the power at
 * every flash operation of an install. */
#include "modstore.h"
#include <string.h>
#include "hal.h"
#include "rle.h" /* crc32_update */
#include "console.h"
#include "evlog.h"
#include "sections.h"

#define SECTOR 4096u
static uint32_t round_up(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* Name fields are NUL-padded, not necessarily NUL-terminated. */
static bool name_eq(const char *field, size_t cap, const char *name)
{
    size_t n = strlen(name);
    return n <= cap && !memcmp(field, name, n) && (n == cap || field[n] == 0);
}

const char *mod_state_name(mod_state_t s)
{
    switch (s) {
    case MOD_OK: return "ok";
    case MOD_NODEV: return "nodev";
    case MOD_ABSENT: return "absent";
    case MOD_ABI: return "abi";
    case MOD_DEPS: return "deps";
    case MOD_CORRUPT: return "corrupt";
    case MOD_DISABLED: return "disabled";
    case MOD_BUILTIN: return "builtin";
    }
    return "?";
}

const char *mod_state_text(mod_state_t s)
{
    switch (s) {
    case MOD_OK: return "installed";
    case MOD_NODEV: return "the QSPI flash is not responding";
    case MOD_ABSENT: return "not installed";
    case MOD_ABI: return "installed, but made for an incompatible firmware";
    case MOD_DEPS: return "installed, but a module it needs is missing";
    case MOD_CORRUPT: return "installed, but damaged";
    case MOD_DISABLED: return "skipped in safe mode";
    case MOD_BUILTIN: return "built into the firmware";
    }
    return "?";
}

const char *mod_strerror(int err)
{
    switch (err) {
    case 0: return "ok";
    case MOD_E_IO: return "flash write failed";
    case MOD_E_FORMAT: return "not a module file for this calculator";
    case MOD_E_ABI: return "module needs a different firmware version";
    case MOD_E_SIZE: return "module file has the wrong size";
    case MOD_E_VERIFY: return "verification after writing failed";
    case MOD_E_NODEV: return "the QSPI flash is not available";
    case MOD_E_FULL: return "not enough free space in the QSPI flash";
    case MOD_E_STATE: return "installer used out of order";
    case MOD_E_NOTFOUND: return "no such module version installed";
    }
    return "error";
}

int mod_vercmp(const uint16_t a[3], const uint16_t b[3])
{
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}

bool mod_header_ok(const mod_hdr_t *h, uint32_t max_payload)
{
    if (h->magic != MOD_MAGIC || h->fmt != MOD_FMT || h->hdr_bytes != MOD_HDR_BYTES)
        return false;
    if (crc32_update(0, h, offsetof(mod_hdr_t, hcrc)) != h->hcrc)
        return false;
    if (!h->name[0] || h->ndeps > MOD_MAX_DEPS || h->payload_len > max_payload)
        return false;
    return true;
}

bool mod_abi_ok(const mod_hdr_t *h, const mod_iface_t *ifs, int nifs)
{
    for (int i = 0; i < nifs; i++)
        if (name_eq(h->iface, sizeof h->iface, ifs[i].iface))
            return ifs[i].abi >= h->abi_min && ifs[i].abi <= h->abi_max;
    return false;
}

int modstore_header(const modstore_t *st, int e, mod_hdr_t *out)
{
    if (!st->dev || e < 0 || e >= st->n)
        return -1;
    return st->dev->read(st->dev->base + st->ext[e].off, out, sizeof *out) ? -1 : 0;
}

/* ------------------------------------------------------------ scan */
/* Walk the region sector by sector. A valid, committed, not removed extent is
 * recorded and skipped as a whole; anything else (erased flash, a damaged or
 * uncommitted or removed extent) only advances one sector, because a newer
 * extent may have been allocated inside the space of a removed one. Every
 * activation seen counts for max_act, so sequence numbers never repeat. */
void modstore_mount(modstore_t *st, const pstore_dev_t *dev, bool readonly)
{
    memset(st, 0, sizeof *st);
    st->dev = dev;
    st->readonly = readonly || !dev || !dev->erase || !dev->write;
    if (!dev)
        return;
    for (uint32_t off = 0; off + SECTOR <= dev->size;) {
        uint32_t magic;
        if (dev->read(dev->base + off, &magic, 4)) {
            st->io_error = true;
            return;
        }
        if (magic != MOD_MAGIC) {
            off += SECTOR;
            continue;
        }
        mod_hdr_t h;
        uint8_t state[MOD_COMMIT_OFF + 8 - MOD_ACT_OFF];
        if (dev->read(dev->base + off, &h, sizeof h) ||
            dev->read(dev->base + off + MOD_ACT_OFF, state, sizeof state)) {
            st->io_error = true;
            return;
        }
        if (!mod_header_ok(&h, dev->size - off - MOD_PAYLOAD_OFF)) {
            off += SECTOR;
            continue;
        }
        uint32_t act = 0;
        int nact = 0;
        for (int k = 0; k < MOD_NACT; k++) {
            uint32_t v = rd32(state + 4 * k);
            if (v == 0xFFFFFFFFu)
                break;
            act = v;
            nact++;
        }
        if (act > st->max_act)
            st->max_act = act;
        bool committed = rd32(state + (MOD_COMMIT_OFF - MOD_ACT_OFF)) == MOD_COMMIT;
        bool removed = rd32(state + (MOD_REMOVE_OFF - MOD_ACT_OFF)) != 0xFFFFFFFFu;
        uint32_t span = round_up(MOD_PAYLOAD_OFF + h.payload_len, SECTOR);
        if (!committed || removed || st->n >= MOD_MAX_EXTENTS) {
            off += SECTOR;
            continue;
        }
        mod_extent_t *e = &st->ext[st->n++];
        memset(e, 0, sizeof *e);
        memcpy(e->name, h.name, sizeof e->name);
        memcpy(e->ver, h.ver, sizeof e->ver);
        e->kind = h.kind;
        e->tier = h.tier;
        e->off = off;
        e->span = span;
        e->payload_len = h.payload_len;
        e->payload_crc = h.payload_crc;
        e->act = act;
        e->nact = (uint8_t)nact;
        st->used += span;
        off += span;
    }
}

int modstore_versions(const modstore_t *st, const char *name, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < st->n; i++)
        if (name_eq(st->ext[i].name, MOD_NAME_LEN, name) && n < max)
            out[n++] = i;
    /* newest activation first (insertion sort; a handful of entries) */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && st->ext[out[j]].act > st->ext[out[j - 1]].act; j--) {
            int t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    return n;
}

int modstore_payload_crc(const modstore_t *st, int e, uint32_t *crc)
{
    uint8_t buf[512];
    const mod_extent_t *x = &st->ext[e];
    uint32_t c = 0;
    for (uint32_t p = 0; p < x->payload_len;) {
        uint32_t m = x->payload_len - p < sizeof buf ? x->payload_len - p : (uint32_t)sizeof buf;
        if (st->dev->read(st->dev->base + x->off + MOD_PAYLOAD_OFF + p, buf, m))
            return -1;
        c = crc32_update(c, buf, m);
        p += m;
    }
    *crc = c;
    return 0;
}

/* ------------------------------------------------------------ state words */
static int program_word(modstore_t *st, uint32_t off, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)}, back[4] = {0};
    const pstore_dev_t *d = st->dev;
    for (int attempt = 0;; attempt++) {
        int w = d->write(d->base + off, b, 4), rd = w ? 0 : d->read(d->base + off, back, 4);
        if (!w && !rd && !memcmp(b, back, 4))
            return 0;
        /* a write that reported success but did not land was seen once on a board, until the
         * next boot: reset the device as at boot and try once more (hal_storage_reset) */
        con_log("modstore: word %06lx=%08lx: write %d read %d back %02x%02x%02x%02x%s", (unsigned long)off,
                (unsigned long)v, w, rd, back[3], back[2], back[1], back[0], attempt ? "" : ": reset and retry");
        evlog_put("modstore: word %06lx: w %d r %d%s", (unsigned long)off, w, rd,
                  attempt ? ": failed" : ": reset and retry");
        if (attempt || hal_storage_reset())
            return MOD_E_IO;
    }
}

/* Program the next activation slot of extent e with a new sequence number. */
static int activate(modstore_t *st, int e)
{
    mod_extent_t *x = &st->ext[e];
    if (x->nact >= MOD_NACT)
        return MOD_E_FULL; /* 60 activations of one copy: reinstall it */
    uint32_t seq = st->max_act + 1;
    int r = program_word(st, x->off + MOD_ACT_OFF + 4u * x->nact, seq);
    if (r)
        return r;
    x->nact++;
    x->act = st->max_act = seq;
    return 0;
}

static int remove_extent(modstore_t *st, int e)
{
    int r = program_word(st, st->ext[e].off + MOD_REMOVE_OFF, 0);
    if (r)
        return r;
    st->used -= st->ext[e].span;
    memmove(&st->ext[e], &st->ext[e + 1], sizeof st->ext[0] * (size_t)(st->n - e - 1));
    st->n--;
    return 0;
}

int modstore_revert(modstore_t *st, const char *name)
{
    if (!st->dev || st->readonly)
        return MOD_E_NODEV;
    int v[MOD_MAX_EXTENTS];
    int n = modstore_versions(st, name, v, MOD_MAX_EXTENTS);
    if (n < 2)
        return MOD_E_NOTFOUND;
    return activate(st, v[1]);
}

int modstore_remove(modstore_t *st, const char *name)
{
    if (!st->dev || st->readonly)
        return MOD_E_NODEV;
    bool any = false;
    for (int i = st->n - 1; i >= 0; i--)
        if (name_eq(st->ext[i].name, MOD_NAME_LEN, name)) {
            int r = remove_extent(st, i);
            if (r)
                return r;
            any = true;
        }
    return any ? 0 : MOD_E_NOTFOUND;
}

int modstore_format(modstore_t *st)
{
    if (!st->dev || st->readonly)
        return MOD_E_NODEV;
    while (st->n) {
        int r = remove_extent(st, st->n - 1);
        if (r)
            return r;
    }
    return 0;
}

/* ------------------------------------------------------------ install */
/* First-fit gap between live extents (they are sorted by offset) that holds
 * `need` bytes; returns its offset or UINT32_MAX. */
static uint32_t find_gap_skip(const modstore_t *st, uint32_t need, int skip)
{
    uint32_t at = 0;
    for (int i = 0; i <= st->n; i++) {
        if (i == skip)
            continue; /* treated as free */
        uint32_t end = i < st->n ? st->ext[i].off : st->dev->size;
        if (end >= at && end - at >= need)
            return at;
        if (i < st->n)
            at = st->ext[i].off + st->ext[i].span;
    }
    return UINT32_MAX;
}
static uint32_t find_gap(const modstore_t *st, uint32_t need) { return find_gap_skip(st, need, -1); }

/* Erase [off, off + span) of the store: 64 KB blocks where aligned. */
static int erase_range(modstore_t *st, uint32_t off, uint32_t span)
{
    const pstore_dev_t *d = st->dev;
    for (uint32_t a = off; a < off + span;) {
        uint32_t step = a % 65536u == 0 && off + span - a >= 65536u ? 65536u : SECTOR;
        if (d->erase(d->base + a, step))
            return MOD_E_IO;
        a += step;
    }
    return 0;
}

/* Move extent e to `to` (a gap that does not overlap it), power-safe: the
 * copy is written and committed with the same activation as the original
 * (so it stays exactly as active as before), then the original is removed.
 * A power loss before the commit leaves an ignored partial copy; after it,
 * two identical copies, of which either may serve and the next install or
 * compaction drops one. */
static int move_extent(modstore_t *st, int e, uint32_t to)
{
    const pstore_dev_t *d = st->dev;
    mod_extent_t x = st->ext[e];
    int r = erase_range(st, to, x.span);
    uint8_t buf[256];
    /* header region (state words excluded: they stay erased) and payload */
    for (uint32_t p = 0; !r && p < MOD_PAYLOAD_OFF + x.payload_len;) {
        if (p == MOD_ACT_OFF)
            p = MOD_PAYLOAD_OFF;
        uint32_t lim = p < MOD_ACT_OFF ? MOD_ACT_OFF : MOD_PAYLOAD_OFF + x.payload_len;
        uint32_t m = lim - p < sizeof buf ? lim - p : (uint32_t)sizeof buf;
        if (d->read(d->base + x.off + p, buf, m) || d->write(d->base + to + p, buf, m))
            r = MOD_E_IO;
        p += m;
    }
    if (r)
        return r;
    /* verify the copy, then commit and activate it like the original */
    st->ext[e].off = to;
    uint32_t crc;
    if (modstore_payload_crc(st, e, &crc) || crc != x.payload_crc) {
        st->ext[e].off = x.off;
        return MOD_E_VERIFY;
    }
    if (program_word(st, to + MOD_COMMIT_OFF, MOD_COMMIT) || (x.act && program_word(st, to + MOD_ACT_OFF, x.act))) {
        st->ext[e].off = x.off;
        return MOD_E_IO;
    }
    st->ext[e].nact = x.act ? 1 : 0;
    r = program_word(st, x.off + MOD_REMOVE_OFF, 0);
    /* keep the table sorted by offset */
    mod_extent_t moved = st->ext[e];
    memmove(&st->ext[e], &st->ext[e + 1], sizeof st->ext[0] * (size_t)(st->n - e - 1));
    int i = 0;
    while (i < st->n - 1 && st->ext[i].off < to)
        i++;
    memmove(&st->ext[i + 1], &st->ext[i], sizeof st->ext[0] * (size_t)(st->n - 1 - i));
    st->ext[i] = moved;
    return r;
}

int modstore_compact(modstore_t *st)
{
    if (!st->dev || st->readonly)
        return MOD_E_NODEV;
    for (bool again = true; again;) {
        again = false;
        for (int e = 0; e < st->n && !again; e++) {
            uint32_t to = find_gap(st, st->ext[e].span);
            if (to != UINT32_MAX && to < st->ext[e].off) { /* a lower gap holds it whole: no overlap */
                int r = move_extent(st, e, to);
                if (r)
                    return r;
                again = true;
            }
        }
    }
    return 0;
}

int mod_inst_begin(mod_inst_t *in, modstore_t *st, const mod_iface_t *ifs, int nifs, bool force)
{
    memset(in, 0, sizeof *in);
    in->st = st, in->ifs = ifs, in->nifs = nifs, in->force = force;
    if (!st || !st->dev || st->readonly)
        in->err = MOD_E_NODEV;
    return in->err;
}

uint32_t mod_inst_total(const mod_inst_t *in)
{
    return in->pos >= MOD_HDR_BYTES ? MOD_PAYLOAD_OFF + in->hdr.payload_len : 0;
}

/* The header has arrived: check it, apply the retention policy, choose the
 * extent, erase its first sector and program the header. */
static int start_module(mod_inst_t *in)
{
    modstore_t *st = in->st;
    const pstore_dev_t *d = st->dev;
    memcpy(&in->hdr, in->hbuf, sizeof in->hdr);
    if (in->hdr.magic != MOD_MAGIC || !mod_header_ok(&in->hdr, 0xFFFFFFFFu))
        return MOD_E_FORMAT;
    if (in->hdr.payload_len > d->size - MOD_PAYLOAD_OFF)
        return MOD_E_SIZE;
    if (!in->force && !mod_abi_ok(&in->hdr, in->ifs, in->nifs))
        return MOD_E_ABI;
    uint32_t need = round_up(MOD_PAYLOAD_OFF + in->hdr.payload_len, SECTOR);
    /* keep only the active version of this module as the fallback */
    char name[MOD_NAME_LEN + 1] = {0};
    memcpy(name, in->hdr.name, MOD_NAME_LEN);
    int v[MOD_MAX_EXTENTS];
    int n = modstore_versions(st, name, v, MOD_MAX_EXTENTS);
    uint32_t keep = n ? st->ext[v[0]].off : UINT32_MAX; /* by offset: indices shift on removal */
    for (int i = st->n - 1; i >= 0; i--)
        if (name_eq(st->ext[i].name, MOD_NAME_LEN, name) && st->ext[i].off != keep)
            if (remove_extent(st, i))
                return MOD_E_IO;
    if (st->n >= MOD_MAX_EXTENTS)
        return MOD_E_FULL;
    uint32_t off = find_gap(st, need);
    if (off == UINT32_MAX) { /* fragmented? move extents down into the gaps */
        if (modstore_compact(st))
            return MOD_E_IO;
        off = find_gap(st, need);
    }
    if (off == UINT32_MAX) {
        /* still no room: drop the active version too, but only if the free
         * space with it gone is enough (then compaction makes it contiguous,
         * at worst after moving every extent down) */
        n = modstore_versions(st, name, v, MOD_MAX_EXTENTS);
        if (n < 1 || d->size - (st->used - st->ext[v[0]].span) < need)
            return MOD_E_FULL;
        if (modstore_remove(st, name))
            return MOD_E_IO;
        in->dropped_active = true;
        off = find_gap(st, need);
        if (off == UINT32_MAX && modstore_compact(st) == 0)
            off = find_gap(st, need);
        if (off == UINT32_MAX)
            return MOD_E_FULL;
    }
    in->off = off;
    if (d->erase(d->base + off, SECTOR))
        return MOD_E_IO;
    in->erased_to = off + SECTOR;
    mod_hdr_t back;
    if (d->write(d->base + off, &in->hdr, sizeof in->hdr) || d->read(d->base + off, &back, sizeof back) ||
        memcmp(&back, &in->hdr, sizeof back))
        return MOD_E_IO;
    return 0;
}

/* Erase the target up to `end` (store offset): 64 KB blocks where aligned and
 * wholly inside the extent, else sectors. */
static int erase_until(mod_inst_t *in, uint32_t end)
{
    const pstore_dev_t *d = in->st->dev;
    uint32_t ext_end = in->off + round_up(MOD_PAYLOAD_OFF + in->hdr.payload_len, SECTOR);
    while (in->erased_to < end) {
        uint32_t step = SECTOR;
        if (in->erased_to % 65536u == 0 && ext_end - in->erased_to >= 65536u)
            step = 65536u;
        if (d->erase(d->base + in->erased_to, step))
            return MOD_E_IO;
        in->erased_to += step;
    }
    return 0;
}

int mod_inst_write(mod_inst_t *in, const void *data, size_t n)
{
    const uint8_t *p = data;
    while (n && !in->err) {
        if (in->pos < MOD_HDR_BYTES) {
            size_t m = MOD_HDR_BYTES - in->pos < n ? MOD_HDR_BYTES - in->pos : n;
            memcpy(in->hbuf + in->pos, p, m);
            in->pos += (uint32_t)m, p += m, n -= m;
            if (in->pos == MOD_HDR_BYTES)
                in->err = start_module(in);
            continue;
        }
        uint32_t total = mod_inst_total(in);
        if (in->pos >= total) {
            in->err = MOD_E_SIZE; /* longer than the header says */
            break;
        }
        if (in->pos < MOD_PAYLOAD_OFF) { /* rest of the header sector: ignored */
            size_t m = MOD_PAYLOAD_OFF - in->pos < n ? MOD_PAYLOAD_OFF - in->pos : n;
            in->pos += (uint32_t)m, p += m, n -= m;
            continue;
        }
        uint32_t at = in->off + in->pos; /* store offset */
        uint32_t sec_end = (at / SECTOR + 1) * SECTOR;
        size_t m = total - in->pos;
        if (m > n)
            m = n;
        if (m > sec_end - at)
            m = sec_end - at;
        if ((in->err = erase_until(in, sec_end)) != 0)
            break;
        if (in->st->dev->write(in->st->dev->base + at, p, m)) {
            in->err = MOD_E_IO;
            break;
        }
        in->pos += (uint32_t)m, p += m, n -= m;
    }
    return in->err;
}

int mod_inst_finish(mod_inst_t *in)
{
    if (in->err)
        return in->err;
    if (in->pos < MOD_HDR_BYTES || in->pos != mod_inst_total(in))
        return in->err = MOD_E_SIZE;
    modstore_t *st = in->st;
    if (st->n >= MOD_MAX_EXTENTS)
        return in->err = MOD_E_FULL;
    /* record the new extent (sorted by offset) and verify its payload */
    int i = 0;
    while (i < st->n && st->ext[i].off < in->off)
        i++;
    memmove(&st->ext[i + 1], &st->ext[i], sizeof st->ext[0] * (size_t)(st->n - i));
    st->n++;
    mod_extent_t *e = &st->ext[i];
    memset(e, 0, sizeof *e);
    memcpy(e->name, in->hdr.name, sizeof e->name);
    memcpy(e->ver, in->hdr.ver, sizeof e->ver);
    e->kind = in->hdr.kind;
    e->tier = in->hdr.tier;
    e->off = in->off;
    e->span = round_up(MOD_PAYLOAD_OFF + in->hdr.payload_len, SECTOR);
    e->payload_len = in->hdr.payload_len;
    e->payload_crc = in->hdr.payload_crc;
    uint32_t crc;
    int r = modstore_payload_crc(st, i, &crc);
    if (!r && crc != in->hdr.payload_crc)
        r = MOD_E_VERIFY;
    if (!r)
        r = program_word(st, in->off + MOD_COMMIT_OFF, MOD_COMMIT);
    if (r) { /* not committed: forget it again (the space stays reusable) */
        memmove(&st->ext[i], &st->ext[i + 1], sizeof st->ext[0] * (size_t)(st->n - i - 1));
        st->n--;
        return in->err = r == MOD_E_VERIFY ? r : MOD_E_IO;
    }
    st->used += e->span;
    e->crc_checked = true;
    /* the new version becomes active (a power loss here leaves it installed
     * but inactive; the previous version stays active) */
    if (activate(st, i))
        return in->err = MOD_E_IO;
    return 0;
}

/* ------------------------------------------------------------ singleton */
static long fail_after = -1;
void mod_test_fail_after(long n) { fail_after = n; }
static int fail_now(void)
{
    if (fail_after < 0)
        return 0;
    if (fail_after == 0)
        return 1;
    fail_after--;
    return 0;
}
static int q_read(uint32_t off, void *buf, size_t n) { return hal_storage_read(off, buf, n); }
static int q_erase(uint32_t off, size_t n) { return fail_now() ? -1 : hal_storage_erase(off, n); }
static int q_write(uint32_t off, const void *buf, size_t n) { return fail_now() ? -1 : hal_storage_write(off, buf, n); }
static const pstore_dev_t qdev = {q_read, q_erase, q_write, MODSTORE_QSPI_BASE, MODSTORE_QSPI_SIZE, SECTOR};

static DTCM_BSS modstore_t g_st[MODSTORE_N];
static const mod_iface_t *g_ifs;
static int g_nifs;
static bool g_disabled;

/* Who serves each module name: store/extent, or the reason none does. */
#define MAX_NAMES 24
static struct {
    char name[MOD_NAME_LEN + 1];
    int store, ext;     /* -1: none */
    mod_state_t state;
} g_srv[MAX_NAMES] DTCM_BSS;
static int g_nsrv;

modstore_t *mod_store(int s) { return s >= 0 && s < MODSTORE_N ? &g_st[s] : NULL; }
const mod_iface_t *mod_ifaces(int *n)
{
    *n = g_nifs;
    return g_ifs;
}

static int srv_find(const char *name)
{
    for (int i = 0; i < g_nsrv; i++)
        if (!strcmp(g_srv[i].name, name))
            return i;
    return -1;
}

/* Is dependency d satisfied by a currently serving module? */
static bool dep_ok(const mod_dep_t *d)
{
    char name[MOD_NAME_LEN + 1] = {0};
    memcpy(name, d->name, MOD_NAME_LEN);
    int i = srv_find(name);
    if (i < 0 || g_srv[i].ext < 0)
        return false;
    const uint16_t *v = g_st[g_srv[i].store].ext[g_srv[i].ext].ver;
    return mod_vercmp(v, d->min) >= 0 && mod_vercmp(v, d->max) <= 0;
}

/* Choose per name the first (store order, then newest activation) version
 * that is ABI-compatible and not known damaged; then drop choices whose
 * dependencies are not served, until nothing changes (a dependency chain
 * resolves in as many passes as it is long). */
static void resolve(void)
{
    g_nsrv = 0;
    for (int s = 0; s < MODSTORE_N; s++)
        for (int e = 0; e < g_st[s].n; e++) {
            char name[MOD_NAME_LEN + 1] = {0};
            memcpy(name, g_st[s].ext[e].name, MOD_NAME_LEN);
            if (srv_find(name) < 0 && g_nsrv < MAX_NAMES) {
                memcpy(g_srv[g_nsrv].name, name, sizeof name);
                g_srv[g_nsrv].store = g_srv[g_nsrv].ext = -1;
                g_srv[g_nsrv].state = MOD_ABSENT;
                g_nsrv++;
            }
        }
    uint8_t excluded_dep[MAX_NAMES][(MODSTORE_N * MOD_MAX_EXTENTS + 7) / 8]; /* versions ruled out by deps */
    memset(excluded_dep, 0, sizeof excluded_dep);
    for (int pass = 0; pass <= MAX_NAMES; pass++) {
        for (int i = 0; i < g_nsrv; i++) {
            g_srv[i].store = g_srv[i].ext = -1;
            g_srv[i].state = MOD_ABSENT;
            bool any_abi = false, any_crc = false, any_dep = false;
            for (int s = 0; s < MODSTORE_N && g_srv[i].ext < 0; s++) {
                int v[MOD_MAX_EXTENTS];
                int n = modstore_versions(&g_st[s], g_srv[i].name, v, MOD_MAX_EXTENTS);
                for (int k = 0; k < n; k++) {
                    const mod_extent_t *x = &g_st[s].ext[v[k]];
                    if (!x->abi_ok) {
                        any_abi = true;
                        continue;
                    }
                    if (x->crc_bad) {
                        any_crc = true;
                        continue;
                    }
                    if (excluded_dep[i][(s * MOD_MAX_EXTENTS + v[k]) / 8] & (1 << ((s * MOD_MAX_EXTENTS + v[k]) % 8))) {
                        any_dep = true;
                        continue;
                    }
                    g_srv[i].store = s, g_srv[i].ext = v[k], g_srv[i].state = MOD_OK;
                    break;
                }
            }
            if (g_srv[i].ext < 0)
                g_srv[i].state = any_crc ? MOD_CORRUPT : any_dep ? MOD_DEPS : any_abi ? MOD_ABI : MOD_ABSENT;
        }
        /* dependencies */
        bool changed = false;
        for (int i = 0; i < g_nsrv; i++) {
            if (g_srv[i].ext < 0)
                continue;
            mod_hdr_t h;
            int s = g_srv[i].store;
            if (modstore_header(&g_st[s], g_srv[i].ext, &h))
                continue;
            for (int d = 0; d < h.ndeps; d++)
                if (!dep_ok(&h.deps[d])) {
                    int bit = s * MOD_MAX_EXTENTS + g_srv[i].ext;
                    excluded_dep[i][bit / 8] |= (uint8_t)(1u << (bit % 8));
                    changed = true;
                    break;
                }
        }
        if (!changed)
            break;
    }
}

static void mount_all(void)
{
    const pstore_dev_t *q = hal_storage_size() >= MODSTORE_QSPI_BASE + MODSTORE_QSPI_SIZE ? &qdev : NULL;
    modstore_mount(&g_st[MODSTORE_QSPI], g_disabled ? NULL : q, false);
    for (int s = 0; s < MODSTORE_N; s++)
        for (int e = 0; e < g_st[s].n; e++) {
            mod_hdr_t h;
            g_st[s].ext[e].abi_ok = modstore_header(&g_st[s], e, &h) == 0 && mod_abi_ok(&h, g_ifs, g_nifs);
        }
    resolve();
}

void mod_boot(const mod_iface_t *ifs, int nifs, bool disabled)
{
    g_ifs = ifs, g_nifs = nifs, g_disabled = disabled;
    mount_all();
}

void mod_remount(void) { mount_all(); }

mod_state_t mod_state(const char *name)
{
    if (g_disabled)
        return MOD_DISABLED;
    int i = srv_find(name);
    if (i >= 0 && g_srv[i].state != MOD_ABSENT)
        return g_srv[i].state;
    if (!g_st[MODSTORE_QSPI].dev || g_st[MODSTORE_QSPI].io_error)
        return MOD_NODEV;
    return MOD_ABSENT;
}

int mod_serving_extent(const char *name, int *store)
{
    int i = srv_find(name);
    if (g_disabled || i < 0 || g_srv[i].ext < 0)
        return -1;
    *store = g_srv[i].store;
    return g_srv[i].ext;
}

int mod_serving_store(const char *name)
{
    int s;
    return mod_serving_extent(name, &s) < 0 ? -1 : s;
}

const mod_hdr_t *mod_header(const char *name)
{
    static mod_hdr_t h;
    int s, e = mod_serving_extent(name, &s);
    return e >= 0 && modstore_header(&g_st[s], e, &h) == 0 ? &h : NULL;
}

int mod_names(const char **out, int max)
{
    int n = 0;
    for (int i = 0; i < g_nsrv && n < max; i++)
        out[n++] = g_srv[i].name;
    return n;
}

int mod_verify(const char *name)
{
    for (int tries = 0; tries < MOD_MAX_EXTENTS; tries++) {
        int s, e = mod_serving_extent(name, &s);
        if (e < 0)
            return -1;
        mod_extent_t *x = &g_st[s].ext[e];
        uint32_t crc;
        if (modstore_payload_crc(&g_st[s], e, &crc) == 0 && crc == x->payload_crc) {
            x->crc_checked = true;
            return 0;
        }
        x->crc_bad = true; /* fall back to the next version, if any */
        resolve();
    }
    return -1;
}

int mod_read(const char *name, uint32_t off, void *buf, size_t n)
{
    int s, e = mod_serving_extent(name, &s);
    if (e < 0)
        return -1;
    if (!g_st[s].ext[e].crc_checked) {
        if (mod_verify(name))
            return -1;
        e = mod_serving_extent(name, &s);
    }
    const mod_extent_t *x = &g_st[s].ext[e];
    if (off > x->payload_len || n > x->payload_len - off)
        return -1;
    const pstore_dev_t *d = g_st[s].dev;
    return d->read(d->base + x->off + MOD_PAYLOAD_OFF + off, buf, n) ? -1 : 0;
}

/* Zero-copy view of the serving version's payload (docs/QSPI_CONTENT.md 7.3;
 * the XIP base of a code module, app/modx.c): the payload is CRC-checked
 * first (like the first mod_read), then the pointer is into the memory-mapped
 * QSPI window (hal_storage_xip). NULL when the module is not served, fails its check, or the platform
 * has no mapped view (simulators). Valid until the next write to the store:
 * every QSPI write leaves memory-mapped mode for its duration. */
const void *mod_map(const char *name, uint32_t *len)
{
    int s, e = mod_serving_extent(name, &s);
    if (e < 0)
        return NULL;
    if (!g_st[s].ext[e].crc_checked) {
        if (mod_verify(name))
            return NULL;
        e = mod_serving_extent(name, &s);
    }
    const mod_extent_t *x = &g_st[s].ext[e];
    *len = x->payload_len;
    return hal_storage_xip(g_st[s].dev->base + x->off + MOD_PAYLOAD_OFF, x->payload_len);
}
