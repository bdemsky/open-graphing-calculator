/*
 * pstore — power-loss-safe snapshot log on NOR flash (the QSPI W25Q128 on
 * hardware, a file in the simulator).
 *
 * The region is used as a ring of sectors. Each save writes a complete new
 * snapshot after the previous one (wrapping to the start of the region when
 * it does not fit before the end), so erases are spread over the whole
 * region (wear levelling) and the previous snapshot is never touched:
 *
 *   sector-aligned:  [header 32 B][payload ...........][pad to sector]
 *   header: magic "PST1", seq, payload length, payload CRC-32, flags,
 *           2 reserved words, header CRC-32 (over the first 28 bytes)
 *
 * Write order: erase the sectors, program the payload, then the header. A
 * power loss at any point leaves either no valid header (torn write: the
 * snapshot is ignored) or a complete snapshot. Mount scans every sector
 * start for a valid header and takes the highest sequence number whose
 * payload CRC also checks out, so a damaged newest snapshot falls back to the
 * one before it.
 *
 * A snapshot may use at most a third of the region (pstore_max_payload), which
 * guarantees there is always room for a new one that does not overlap the
 * current newest one.
 */
#ifndef PSTORE_H
#define PSTORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Flash access. Offsets are absolute device offsets; erase works on whole
 * sectors; write requires erased bytes (NOR: programming only clears bits).
 * Each callback returns 0 on success, nonzero on error. */
typedef struct {
    int (*read)(uint32_t off, void *buf, size_t n);
    int (*erase)(uint32_t off, size_t n);
    int (*write)(uint32_t off, const void *buf, size_t n);
    uint32_t base;   /* first byte of the region (sector aligned) */
    uint32_t size;   /* region size (multiple of sector) */
    uint32_t sector; /* erase granularity */
} pstore_dev_t;

/* Mounted state (RAM only; rebuilt by pstore_mount). */
typedef struct {
    const pstore_dev_t *dev;
    bool mounted;
    bool have;       /* a valid snapshot exists */
    uint32_t seq;    /* newest valid snapshot: sequence number */
    uint32_t off;    /*   region-relative offset of its header */
    uint32_t len;    /*   payload length */
    uint32_t crc;    /*   payload CRC-32 */
    uint32_t next;   /* region-relative offset for the next snapshot */
    uint32_t max_seq;/* highest sequence number seen in any valid header */
    uint32_t writes; /* snapshots written since mount (statistics) */
    uint32_t erases; /* sectors erased since mount */
} pstore_t;

#define PSTORE_HDR 32u /* header bytes at the start of each snapshot */

/* Scan the region. Returns 0 (also when it is empty), -1 on device error. */
int pstore_mount(pstore_t *ps, const pstore_dev_t *dev);
/* Largest payload a snapshot may have. */
size_t pstore_max_payload(const pstore_t *ps);
/* Copy the newest payload into buf (max bytes). Returns its length, 0 if there
 * is none, -1 on error or if it does not fit. */
long pstore_load(pstore_t *ps, void *buf, size_t max);
/* Write a new snapshot. Returns 0, -1 device error / verify failure, -2 too
 * large. On failure the previous snapshot stays the current one. */
int pstore_save(pstore_t *ps, const void *data, size_t len);
/* Erase the whole region (forget every snapshot). */
int pstore_format(pstore_t *ps);

#endif
