/* Power-loss-safe snapshot log (see pstore.h for the format and guarantees).
 *
 * Users: app/session.c saves the serialized session here through the
 * hal_storage_* calls (QSPI W25Q128 at 0xE00000 on hardware; the simulators'
 * emulated flash, kept in a file with --flash). tests/unit/test_pstore.c cuts
 * the power at every point of a save (after any number of programmed bytes or
 * erased sectors) and checks that a mount finds the old or the new snapshot. */
#include "pstore.h"
#include <string.h>
#include "rle.h" /* crc32_update */

#define MAGIC 0x31545350u /* "PST1" little-endian */

/* On-flash header, little-endian words (the struct is stored as-is; both the
 * Cortex-M7 and the simulator hosts are little-endian). */
typedef struct {
    uint32_t magic, seq, len, crc, flags, rsv0, rsv1, hcrc;
} hdr_t;
_Static_assert(sizeof(hdr_t) == PSTORE_HDR, "pstore header size");

static uint32_t round_up(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }

/* Read the header at region offset `off` and check it: magic, header CRC, and
 * a payload length that is plausible and stays inside the region. A torn
 * header write (power lost while programming it) fails the CRC. Payload
 * integrity is checked separately (flash_crc), because it costs a full read. */
static bool read_hdr(pstore_t *ps, uint32_t off, hdr_t *h)
{
    const pstore_dev_t *d = ps->dev;
    if (d->read(d->base + off, h, sizeof *h))
        return false;
    if (h->magic != MAGIC || crc32_update(0, h, offsetof(hdr_t, hcrc)) != h->hcrc)
        return false;
    return h->len <= pstore_max_payload(ps) && off + PSTORE_HDR + h->len <= d->size;
}

/* CRC of the payload as stored in flash, read back in 256-byte pieces (stack
 * buffer; the payload itself may be far larger than any free RAM block). */
static int flash_crc(pstore_t *ps, uint32_t off, uint32_t len, uint32_t *crc)
{
    const pstore_dev_t *d = ps->dev;
    uint8_t buf[256];
    uint32_t c = 0;
    for (uint32_t p = 0; p < len;) {
        uint32_t n = len - p < sizeof buf ? len - p : (uint32_t)sizeof buf;
        if (d->read(d->base + off + PSTORE_HDR + p, buf, n))
            return -1;
        c = crc32_update(c, buf, n);
        p += n;
    }
    *crc = c;
    return 0;
}

/* A third of the region (rounded down to whole sectors), minus the header.
 * With at most a third per snapshot, a new snapshot always fits either right
 * after the newest one or, after wrapping, at the start of the region without
 * overlapping it (see pstore_save). */
size_t pstore_max_payload(const pstore_t *ps)
{
    const pstore_dev_t *d = ps->dev;
    uint32_t third = d->size / 3 / d->sector * d->sector;
    return third > PSTORE_HDR ? third - PSTORE_HDR : 0;
}

/* Snapshots always start on a sector boundary, so only the first bytes of each
 * sector need to be examined. Cost: one header read per sector per pass
 * (512 reads of 32 bytes for 2 MB of 4 KB sectors), plus one payload read for
 * each candidate. max_seq is the highest sequence number among all
 * well-formed headers, including ones whose payload turned out damaged, so
 * the next save never reuses a sequence number. */
int pstore_mount(pstore_t *ps, const pstore_dev_t *dev)
{
    memset(ps, 0, sizeof *ps);
    ps->dev = dev;
    /* newest valid snapshot: try headers in descending sequence order until a
     * payload CRC checks out (normally the first one does). Each pass finds
     * the highest sequence number below `below`. */
    uint32_t below = UINT32_MAX;
    bool any = false;
    for (;;) {
        bool found = false;
        uint32_t best_off = 0;
        hdr_t best = {0};
        for (uint32_t off = 0; off < dev->size; off += dev->sector) {
            hdr_t h;
            if (!read_hdr(ps, off, &h))
                continue;
            if (!any || h.seq > ps->max_seq)
                ps->max_seq = h.seq;
            any = true;
            if (h.seq < below && (!found || h.seq > best.seq)) {
                best = h;
                best_off = off;
                found = true;
            }
        }
        if (!found)
            break;
        uint32_t crc;
        if (flash_crc(ps, best_off, best.len, &crc))
            return -1;
        if (crc == best.crc) {
            ps->have = true;
            ps->seq = best.seq;
            ps->off = best_off;
            ps->len = best.len;
            ps->crc = best.crc;
            /* continue the ring right after it (the first sector after its
             * payload; % wraps an end-of-region snapshot to offset 0) */
            ps->next = round_up(best_off + PSTORE_HDR + best.len, dev->sector) % dev->size;
            break;
        }
        below = best.seq; /* damaged: try the one before it */
    }
    ps->mounted = true;
    return 0;
}

/* The payload CRC is checked again after the copy: mount verified the flash
 * contents, this also catches a read error during the load itself. */
long pstore_load(pstore_t *ps, void *buf, size_t max)
{
    if (!ps->mounted)
        return -1;
    if (!ps->have)
        return 0;
    if (ps->len > max)
        return -1;
    const pstore_dev_t *d = ps->dev;
    if (d->read(d->base + ps->off + PSTORE_HDR, buf, ps->len))
        return -1;
    if (crc32_update(0, buf, ps->len) != ps->crc)
        return -1;
    return (long)ps->len;
}

/* Power-fail ordering (each step leaves the previous snapshot current):
 *   1. erase the target sectors: they never overlap the current snapshot, so
 *      only older (superseded) snapshots are destroyed;
 *   2. program the payload and read it back for a CRC check: still no
 *      header, so a torn payload is invisible to mount;
 *   3. program the 32-byte header, whose CRC makes a torn header invalid;
 *      once it reads back intact the new snapshot (seq = max_seq + 1) wins.
 * On failure `next` moves past the attempted area so a retry does not keep
 * hitting the same (possibly worn) sectors. Takes one erase per sector of the
 * snapshot (W25Q128: 4 KB sector erase, typ. 45 ms) plus programming time;
 * blocking, main context only. */
int pstore_save(pstore_t *ps, const void *data, size_t len)
{
    if (!ps->mounted)
        return -1;
    const pstore_dev_t *d = ps->dev;
    if (len > pstore_max_payload(ps))
        return -2;
    uint32_t total = round_up(PSTORE_HDR + (uint32_t)len, d->sector); /* header + payload, whole sectors */
    /* choose the target: normally `next`, else wrap to 0, and in either case
     * step past the current snapshot if the range would overlap it (only
     * possible after a failed save moved `next`) */
    uint32_t off = ps->next;
    if (off + total > d->size)
        off = 0; /* wrap; the 1/3 size limit keeps it clear of the newest snapshot */
    if (ps->have) {
        uint32_t cur_end = round_up(ps->off + PSTORE_HDR + ps->len, d->sector);
        if (off < cur_end && ps->off < off + total)
            off = cur_end + total <= d->size ? cur_end : 0; /* never overlap the current one */
    }
    if (d->erase(d->base + off, total))
        return -1;
    ps->erases += total / d->sector;
    hdr_t h = {MAGIC, ps->max_seq + 1, (uint32_t)len, crc32_update(0, data, len), 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0};
    h.hcrc = crc32_update(0, &h, offsetof(hdr_t, hcrc));
    uint32_t crc;
    if ((len && d->write(d->base + off + PSTORE_HDR, data, len)) || flash_crc(ps, off, (uint32_t)len, &crc) ||
        crc != h.crc) {
        ps->next = (off + total) % d->size; /* skip the bad spot next time */
        return -1;
    }
    /* commit: the header goes last */
    hdr_t back;
    if (d->write(d->base + off, &h, sizeof h) || d->read(d->base + off, &back, sizeof back) ||
        memcmp(&back, &h, sizeof h)) {
        ps->next = (off + total) % d->size;
        return -1;
    }
    ps->have = true;
    ps->seq = ps->max_seq = h.seq;
    ps->off = off;
    ps->len = (uint32_t)len;
    ps->crc = h.crc;
    ps->next = (off + total) % d->size;
    ps->writes++;
    return 0;
}

/* Forget every snapshot (app/session.c session_clear). Returns 0, or -1 on a
 * device error. Not atomic: after a power loss part-way some headers are
 * still valid and the newest of those becomes current again. */
int pstore_format(pstore_t *ps)
{
    /* Invalidate every valid header by clearing its magic word (NOR can clear
     * bits without an erase), which is much faster than erasing the region. */
    const pstore_dev_t *d = ps->dev;
    static const uint32_t zero = 0;
    for (uint32_t off = 0; off < d->size; off += d->sector) {
        hdr_t h;
        if (read_hdr(ps, off, &h) && d->write(d->base + off, &zero, sizeof zero))
            return -1;
    }
    return pstore_mount(ps, d);
}
