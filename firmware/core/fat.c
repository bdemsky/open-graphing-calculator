/*
 * fat.c -- FAT16/FAT32 on the microSD card (see fat.h). Written from the
 * Microsoft FAT specification (fatgen103): BPB fields, cluster arithmetic,
 * FAT entry encoding, 8.3 and long-name directory entries.
 *
 * Caching: one directory/data sector and one FAT sector, both write-through
 * (every modification reaches the card immediately, in the order that keeps
 * the file system consistent: a new file's clusters are allocated in the FAT
 * and its data written first, the directory entry that makes them reachable
 * last; the previous version's clusters are freed only after that).
 *
 * Layout recap (fatgen103): [reserved sectors incl. boot sector/BPB]
 * [FAT copy 1..nfats][FAT16 only: fixed root directory][data area]. Cluster
 * numbers start at 2 in the data area; a FAT entry holds the next cluster of a
 * chain, 0 for free, or an end-of-chain marker (>= 0xFFF8 / 0x0FFFFFF8).
 * The FAT type is decided only by the cluster count (< 4085 FAT12, which is
 * not supported, < 65525 FAT16, else FAT32), as the specification requires.
 * All LBAs held in V are absolute card blocks (partition offset included).
 *
 * Not reentrant (static state and caches): call from the main context only.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: size over speed (not time-critical) */
#endif
#include "fat.h"
#include <string.h>
#include "hal.h"
#include "sections.h"

#define SEC 512u

/* Volume geometry from the BPB (all sector numbers absolute). */
static struct {
    bool mounted;
    int type; /* 16 or 32 */
    uint32_t blocks,    /* card size in 512-byte blocks */
        part,           /* first sector of the volume (0 if no partition table) */
        fat_start,      /* first sector of FAT copy 0 */
        fat_sz,         /* sectors per FAT copy */
        nfats,          /* number of FAT copies (normally 2) */
        root_start,     /* FAT16 fixed root directory */
        root_secs,      /*   and its size in sectors (0 on FAT32) */
        data_start,     /* first sector of cluster 2 */
        spc,            /* sectors per cluster */
        nclus,          /* number of data clusters (valid: 2 .. nclus+1) */
        root_clus;      /* FAT32 root directory cluster */
    uint32_t fsinfo,    /* FAT32 FSInfo sector (0 = none) */
        hint;           /* where the next free-cluster search starts */
    bool fsinfo_done;   /* FSInfo hints already invalidated this mount */
} V;

/* sec[] mirrors card sector sec_lba (0xFFFFFFFF = nothing cached). Code that
 * modifies sec[] must store() it before anything else may load() another
 * sector, since load() simply overwrites the buffer. */
static uint8_t sec[SEC];   /* directory / data sector cache */
static uint32_t sec_lba = 0xFFFFFFFFu;
static uint8_t fsec[SEC];  /* FAT sector cache */
static uint32_t fsec_lba = 0xFFFFFFFFu;
static uint8_t wbuf[SEC];  /* write buffer of the open file */
static bool busy;          /* a file is open */

/* Little-endian field access (on-disk structures are little-endian and
 * unaligned, so no struct casts). */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v), wr16(p + 2, v >> 16); }

static int rd(uint32_t lba, uint8_t *p) { return hal_sd_read(lba, p, 1) ? FS_EIO : FS_OK; }
static int wr(uint32_t lba, const uint8_t *p) { return hal_sd_write(lba, p, 1) ? FS_EIO : FS_OK; }

/* Make sec[] hold sector lba (no card access on a cache hit). On a read
 * error the cache is left empty so stale data is never mistaken for lba. */
static int load(uint32_t lba)
{
    if (sec_lba == lba)
        return FS_OK;
    sec_lba = 0xFFFFFFFFu;
    int e = rd(lba, sec);
    if (!e)
        sec_lba = lba;
    return e;
}

/* Write sec[] to sector lba and mark it as the cached sector (also used to
 * write a freshly built sector to a new location). On a write error the
 * cache is left empty: sec[] no longer matches what the card holds. */
static int store(uint32_t lba) /* write the cached sector back */
{
    int e = wr(lba, sec);
    sec_lba = e ? 0xFFFFFFFFu : lba;
    return e;
}

const char *fs_strerror(int err)
{
    static const char *const m[] = {"ok", "no card", "card read/write error", "no FAT16/FAT32 file system",
                                    "not found", "invalid name (8.3)", "card or directory full", "busy",
                                    "is a directory", "invalid argument", "connected to the computer"};
    return err <= 0 && err >= FS_EUSB ? m[-err] : "error";
}

/* ------------------------------------------------------------ mount */
static int mount_volume(uint32_t blocks);

/* Power up the card and parse the volume (mount_volume). Idempotent while
 * mounted. On failure the card is powered down again. */
/* The card given to a computer over USB (app/usb_app.c): every operation
 * fails with FS_EUSB until it is taken back. */
static bool locked;
void fs_lock(bool on)
{
    if (on)
        fs_unmount();
    locked = on;
}

int fs_mount(void)
{
    if (locked)
        return FS_EUSB;
    if (V.mounted)
        return FS_OK;
    sec_lba = fsec_lba = 0xFFFFFFFFu;
    if (!hal_sd_present())
        return FS_ENOCARD;
    uint32_t blocks;
    if (hal_sd_open(&blocks))
        return FS_ENOCARD;
    int r = mount_volume(blocks);
    if (r)
        hal_sd_close();
    return r;
}

/* Sector 0 is either the volume boot record itself (a "superfloppy" card, as
 * fs_mkfs writes) or an MBR, in which case the first FAT16/FAT32 partition is
 * used. Then the BPB gives the region layout. */
static int mount_volume(uint32_t blocks)
{
    memset(&V, 0, sizeof V);
    V.blocks = blocks;
    if (load(0))
        return FS_EIO;
    if (rd16(sec + 510) != 0xAA55)
        return FS_ENOFS;
    /* a VBR has a jump and 512-byte sectors; else read the first MBR partition */
    if (!((sec[0] == 0xEB || sec[0] == 0xE9) && rd16(sec + 11) == SEC)) {
        for (int i = 0; i < 4; i++) {
            /* MBR partition entry: type at +4, start LBA at +8. Types: 04/06/0E
             * FAT16 (CHS small / CHS / LBA), 0B/0C FAT32 (CHS / LBA). */
            const uint8_t *pe = sec + 446 + 16 * i;
            uint8_t t = pe[4];
            if (t == 0x04 || t == 0x06 || t == 0x0E || t == 0x0B || t == 0x0C) {
                V.part = rd32(pe + 8);
                break;
            }
        }
        if (!V.part || load(V.part) || rd16(sec + 510) != 0xAA55)
            return FS_ENOFS;
    }
    /* BPB (fatgen103): +11 bytes/sector, +13 sectors/cluster, +14 reserved
     * sectors, +16 number of FATs, +17 root entries (FAT16), +19/+32 total
     * sectors (16/32-bit), +22/+36 sectors per FAT (FAT16/FAT32). */
    if (rd16(sec + 11) != SEC || !sec[13] || !sec[16])
        return FS_ENOFS;
    V.spc = sec[13];
    uint32_t rsvd = rd16(sec + 14), rootent = rd16(sec + 17);
    V.nfats = sec[16];
    V.fat_sz = rd16(sec + 22) ? rd16(sec + 22) : rd32(sec + 36);
    uint32_t tot = rd16(sec + 19) ? rd16(sec + 19) : rd32(sec + 32);
    V.root_secs = (rootent * 32 + SEC - 1) / SEC;
    V.fat_start = V.part + rsvd;
    V.root_start = V.fat_start + V.nfats * V.fat_sz;
    V.data_start = V.root_start + V.root_secs;
    if (tot < rsvd + V.nfats * V.fat_sz + V.root_secs)
        return FS_ENOFS;
    V.nclus = (tot - rsvd - V.nfats * V.fat_sz - V.root_secs) / V.spc;
    if (V.nclus < 4085)
        return FS_ENOFS; /* FAT12 */
    V.type = V.nclus < 65525 ? 16 : 32;
    if (V.type == 32) {
        /* FAT32 extended BPB: +44 root cluster, +48 FSInfo sector */
        V.root_clus = rd32(sec + 44);
        V.fsinfo = rd16(sec + 48) ? V.part + rd16(sec + 48) : 0;
    }
    V.hint = 2;
    V.mounted = true;
    return FS_OK;
}

/* Power the card down and forget all state. Any open file is dropped without
 * publishing (its clusters stay allocated until a chkdsk). */
void fs_unmount(void)
{
    if (V.mounted)
        hal_sd_close();
    V.mounted = false;
    busy = false;
    sec_lba = fsec_lba = 0xFFFFFFFFu;
}

bool fs_mounted(void) { return V.mounted; }

/* ------------------------------------------------------------ FAT */
/* End-of-chain value written by this code, and the test for any EOC marker. */
static uint32_t eoc(void) { return V.type == 32 ? 0x0FFFFFFFu : 0xFFFFu; }
static bool is_eoc(uint32_t v) { return V.type == 32 ? v >= 0x0FFFFFF8u : v >= 0xFFF8u; }
static uint32_t clus_lba(uint32_t c) { return V.data_start + (c - 2) * V.spc; } /* first sector of cluster c */

/* Read the FAT entry of cluster c (from FAT copy 0, through the FAT sector
 * cache). FAT32 entries are 28 bits: the top 4 bits are reserved and masked
 * off. Out-of-range clusters give FS_EIO, which also stops loops over a
 * corrupt chain. */
static int fat_get(uint32_t c, uint32_t *v)
{
    uint32_t off = c * (V.type == 32 ? 4 : 2), lba = V.fat_start + off / SEC;
    if (c >= V.nclus + 2)
        return FS_EIO;
    if (fsec_lba != lba) {
        fsec_lba = 0xFFFFFFFFu;
        if (rd(lba, fsec))
            return FS_EIO;
        fsec_lba = lba;
    }
    *v = V.type == 32 ? rd32(fsec + off % SEC) & 0x0FFFFFFFu : rd16(fsec + off % SEC);
    return FS_OK;
}

/* Set the FAT entry of cluster c and write the sector to every FAT copy (the
 * copies stay identical, as Windows/chkdsk expect). FAT32 keeps the reserved
 * top 4 bits of the entry, as the specification requires. */
static int fat_set(uint32_t c, uint32_t v)
{
    uint32_t old;
    if (fat_get(c, &old)) /* loads the sector */
        return FS_EIO;
    uint32_t off = c * (V.type == 32 ? 4 : 2);
    if (V.type == 32)
        wr32(fsec + off % SEC, (rd32(fsec + off % SEC) & 0xF0000000u) | (v & 0x0FFFFFFFu));
    else
        wr16(fsec + off % SEC, v);
    for (uint32_t k = 0; k < V.nfats; k++)
        if (wr(V.fat_start + k * V.fat_sz + off / SEC, fsec))
            return FS_EIO;
    return FS_OK;
}

/* invalidate the FSInfo hints once per mount before the first allocation:
 * free count and next-free (+488/+492) = 0xFFFFFFFF ("unknown"), which the
 * specification allows; this avoids rewriting FSInfo on every allocation and
 * can never leave a wrong free count behind. FSInfo is recognised by its
 * lead signature "RRaA" (0x41615252). */
static void fsinfo_invalidate(void)
{
    if (V.fsinfo_done || !V.fsinfo)
        return;
    V.fsinfo_done = true;
    if (load(V.fsinfo) || rd32(sec) != 0x41615252u)
        return;
    wr32(sec + 488, 0xFFFFFFFFu);
    wr32(sec + 492, 0xFFFFFFFFu);
    store(V.fsinfo);
}

/* allocate a cluster (marked end of chain), linked after `prev` (0: none).
 * Next-fit search from V.hint, wrapping once over all clusters (FS_EFULL if
 * none is free). With `zero` the cluster's sectors are cleared first (needed
 * for directory clusters, where a 0 byte marks the end of the entries); this
 * goes through sec[], so it replaces the cached sector. The new cluster is
 * marked EOC before the predecessor points to it, so a power loss in between
 * leaves at worst a lost cluster, never a chain into a free cluster. */
static int alloc(uint32_t prev, uint32_t *out, bool zero)
{
    fsinfo_invalidate();
    uint32_t n = V.nclus, c = V.hint;
    for (uint32_t i = 0; i < n; i++, c++) {
        if (c >= n + 2)
            c = 2;
        uint32_t v;
        if (fat_get(c, &v))
            return FS_EIO;
        if (v)
            continue;
        if (zero) {
            memset(sec, 0, SEC);
            for (uint32_t s = 0; s < V.spc; s++)
                if (store(clus_lba(c) + s))
                    return FS_EIO;
        }
        if (fat_set(c, eoc()) || (prev && fat_set(prev, c)))
            return FS_EIO;
        V.hint = c + 1;
        *out = c;
        return FS_OK;
    }
    return FS_EFULL;
}

/* Free a cluster chain starting at c (0 = empty file: nothing to do). The
 * guard bounds the walk by the cluster count, so a cyclic chain on a corrupt
 * card cannot loop forever. Freed clusters pull the allocation hint back. */
static int free_chain(uint32_t c)
{
    for (uint32_t guard = 0; c >= 2 && c < V.nclus + 2 && guard < V.nclus; guard++) {
        uint32_t next;
        if (fat_get(c, &next) || fat_set(c, 0))
            return FS_EIO;
        if (c < V.hint)
            V.hint = c;
        if (is_eoc(next))
            break;
        c = next;
    }
    return FS_OK;
}

/* ------------------------------------------------------------ directories */
/* Directory iterator over 32-byte entries, 16 per sector. The FAT16 root is a
 * fixed run of sectors; every other directory (and the FAT32 root) is a
 * cluster chain. The current sector is held in sec[]. */
typedef struct {
    uint32_t clus; /* 0: FAT16 fixed root */
    uint32_t n;    /* sector index within the cluster (or the root region) */
    uint32_t lba;
    int idx;       /* entry in the sector */
} dit_t;

/* Start before the first entry of directory `clus` (0 = root on both FAT
 * types: on FAT32 it is mapped to the root cluster). */
static void dit_start(dit_t *d, uint32_t clus)
{
    if (!clus && V.type == 32)
        clus = V.root_clus;
    d->clus = clus;
    d->n = 0;
    d->lba = clus ? clus_lba(clus) : V.root_start;
    d->idx = -1;
}

/* next entry: 1 = ok (entry at sec + idx*32), 0 = end of directory, <0 error */
static int dit_next(dit_t *d)
{
    if (++d->idx >= 16) {
        d->idx = 0;
        d->n++;
        if (!d->clus) {
            if (d->n >= V.root_secs)
                return 0;
            d->lba++;
        } else if (d->n < V.spc) {
            d->lba++;
        } else {
            uint32_t nx;
            if (fat_get(d->clus, &nx))
                return FS_EIO;
            if (is_eoc(nx) || nx < 2)
                return 0;
            d->clus = nx;
            d->n = 0;
            d->lba = clus_lba(nx);
        }
    }
    return load(d->lba) ? FS_EIO : 1;
}

static uint8_t up(uint8_t c) { return c >= 'a' && c <= 'z' ? (uint8_t)(c - 32) : c; } /* ASCII upper case */

/* "data.csv" -> "DATA    CSV": the on-disk 11-byte short name, upper case,
 * space padded, no dot. Strict: at most 8 + 3 characters, one dot, only
 * letters (folded to upper case), digits and the punctuation the
 * specification permits (no spaces, no non-ASCII, no "~1" mangling) and a
 * non-empty base name; anything else is FS_ENAME,
 * so a name the user types either maps 1:1 to a short name or is refused. */
static int to83(const char *s, char out[11])
{
    memset(out, ' ', 11);
    static const char ok[] = "!#$%&'()-@^_`{}~";
    int i = 0, lim = 8, pos = 0;
    if (!*s || !strcmp(s, ".") || !strcmp(s, ".."))
        return FS_ENAME;
    for (; *s; s++) {
        uint8_t c = up((uint8_t)*s);
        if (c == '.') {
            if (lim == 3)
                return FS_ENAME;
            lim = 3, pos = 8, i = 0;
            continue;
        }
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(ok, c)) || i >= lim)
            return FS_ENAME;
        out[pos + i++] = (char)c;
    }
    return out[0] == ' ' ? FS_ENAME : FS_OK;
}

/* Short name of entry e as "NAME.EXT" (out: 13 bytes). */
static void from83(const uint8_t *e, char *out)
{
    int o = 0;
    for (int i = 0; i < 8 && e[i] != ' '; i++)
        out[o++] = (char)e[i];
    if (e[8] != ' ') {
        out[o++] = '.';
        for (int i = 8; i < 11 && e[i] != ' '; i++)
            out[o++] = (char)e[i];
    }
    out[o] = 0;
}

static bool ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (up((uint8_t)*a) != up((uint8_t)*b))
            return false;
    return *a == *b;
}

/* iterate a directory with long names: calls fn(entry, long name) for each live entry */
typedef int (*dir_fn)(const uint8_t *e, const char *lname, dit_t *d, void *user);

/* Walk directory `clus`, assembling VFAT long names: LFN entries precede their
 * short entry in reverse order (the one with bit 0x40 in its sequence byte
 * comes first and holds the last piece), each carrying 13 UTF-16 characters at
 * fixed offsets. Characters beyond ASCII become '?', names are cut at 63
 * characters. A long name is used only if its parts carry consecutive
 * sequence numbers down to 1 and the checksum of the short entry that follows
 * (orphans of a deleted or foreign entry are ignored); its first part is
 * recorded in lfn_start/lfn_count for fs_remove while fn runs. Stops at
 * the first entry starting with 0 (end of directory), skips deleted entries
 * (0xE5) and volume labels. A nonzero return from fn stops the walk and is
 * returned; otherwise 0, or <0 on an I/O error. e points into sec[]. */
static dit_t lfn_start; /* first long-name entry of the current short entry */
static int lfn_count;   /*   and the number of them (0: no valid long name) */

/* Checksum of an 11-byte short name, stored in each of its long-name entries. */
static uint8_t sfn_sum(const uint8_t *e)
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + e[i]);
    return sum;
}

static int dir_walk(uint32_t clus, dir_fn fn, void *user)
{
    dit_t d;
    char lname[64];
    int have_l = 0, next_seq = 0, nl = 0;
    uint8_t chk = 0;
    dit_start(&d, clus);
    for (;;) {
        int r = dit_next(&d);
        if (r <= 0)
            return r;
        const uint8_t *e = sec + d.idx * 32;
        if (e[0] == 0)
            return 0;
        if (e[0] == 0xE5) {
            have_l = 0;
            continue;
        }
        if ((e[11] & 0x3F) == 0x0F) { /* long-name part (attr RO|HID|SYS|VOL) */
            int seq = e[0] & 0x1F;
            if (e[0] & 0x40) {
                memset(lname, 0, sizeof lname);
                have_l = seq > 0;
                chk = e[13];
                lfn_start = d;
                nl = 0;
            } else if (seq != next_seq || e[13] != chk) {
                have_l = 0;
            }
            if (!have_l)
                continue;
            next_seq = seq - 1;
            nl++;
            /* byte offsets of the 13 UTF-16LE characters: 5 at +1, 6 at +14, 2 at +28 */
            static const uint8_t pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
            for (int k = 0; k < 13; k++) {
                int at = (seq - 1) * 13 + k;
                uint16_t ch = rd16(e + pos[k]);
                if (at < (int)sizeof lname - 1 && ch && ch != 0xFFFF)
                    lname[at] = ch < 128 ? (char)ch : '?';
            }
            continue;
        }
        if (e[11] & 0x08) { /* volume label */
            have_l = 0;
            continue;
        }
        if (have_l && (next_seq || sfn_sum(e) != chk))
            have_l = 0;
        lfn_count = have_l ? nl : 0;
        r = fn(e, have_l ? lname : NULL, &d, user);
        have_l = 0;
        if (r)
            return r;
    }
}

/* dir_find state: the name sought and, when found, a copy of the entry and
 * where it lives on the card (so it can be rewritten later). */
typedef struct {
    const char *name;
    char n83[11];
    bool valid83;   /* name is a valid 8.3 name (n83 holds it) */
    uint8_t ent[32];
    uint32_t lba;
    uint16_t off;   /* byte offset of the entry in sector lba */
    dit_t lfn;      /* its first long-name entry */
    int nlfn;       /*   and the number of them (0: none) */
} find_t;

/* Match by short name (exact 11-byte compare after to83) or by long name
 * (case-insensitive). */
static int find_cb(const uint8_t *e, const char *lname, dit_t *d, void *user)
{
    find_t *f = user;
    if ((f->valid83 && !memcmp(e, f->n83, 11)) || (lname && ieq(lname, f->name))) {
        memcpy(f->ent, e, 32);
        f->lba = d->lba;
        f->off = (uint16_t)(d->idx * 32);
        f->lfn = lfn_start;
        f->nlfn = lfn_count;
        return 1;
    }
    return 0;
}

/* First cluster of an entry: low word at +26, high word at +20 (FAT32 only). */
static uint32_t ent_clus(const uint8_t *e)
{
    return (V.type == 32 ? (uint32_t)rd16(e + 20) << 16 : 0) | rd16(e + 26);
}

/* find `name` in directory cluster `dir`; 1 = found (f filled), 0 = not found, <0 error */
static int dir_find(uint32_t dir, const char *name, find_t *f)
{
    f->name = name;
    f->valid83 = to83(name, f->n83) == FS_OK;
    return dir_walk(dir, find_cb, f);
}

/* resolve the directory part of `path`; *last = final component.
 * Leading slashes are ignored (paths are always from the root) and a trailing
 * slash is allowed. *dir is the cluster of the containing directory, 0 for the
 * root. FS_ENAME if a component is >= lastn characters, FS_ENOENT if an
 * intermediate directory is missing, FS_EISDIR if it is a file. */
static int resolve(const char *path, uint32_t *dir, char *last, size_t lastn)
{
    *dir = 0;
    while (*path == '/')
        path++;
    for (;;) {
        const char *sl = strchr(path, '/');
        size_t n = sl ? (size_t)(sl - path) : strlen(path);
        if (n >= lastn)
            return FS_ENAME;
        memcpy(last, path, n);
        last[n] = 0;
        if (!sl || !sl[1])
            return FS_OK;
        find_t f;
        int r = dir_find(*dir, last, &f);
        if (r < 0)
            return r;
        if (!r)
            return FS_ENOENT;
        if (!(f.ent[11] & 0x10))
            return FS_EISDIR;
        *dir = ent_clus(f.ent);
        path = sl + 1;
    }
}

/* Set the creation, access and modification time stamps of entry e (FAT date:
 * year-1980 << 9 | month << 5 | day; time: hour << 11 | min << 5 | sec/2). */
static void stamp(uint8_t *e)
{
    /* no clock: 2026-01-01 12:00 */
    uint32_t date = ((2026 - 1980) << 9) | (1 << 5) | 1, time = 12 << 11;
    wr16(e + 14, time), wr16(e + 16, date), wr16(e + 18, date), wr16(e + 22, time), wr16(e + 24, date);
}

/* Inverse of ent_clus (the high word is 0 on FAT16). */
static void set_clus(uint8_t *e, uint32_t c)
{
    wr16(e + 20, V.type == 32 ? c >> 16 : 0);
    wr16(e + 26, c & 0xFFFF);
}


/* A name that is not 8.3 gets a short alias ("Plan for May.ipynb" ->
 * "PLANFO~1.IPY": the first six letters and digits, ~k with the first k not
 * taken in `dir`, the first three of the extension) and long-name entries
 * before it (dir_add). 0 with the alias, or FS_ENAME / an I/O error. */
static char new_lname[64]; /* the long name of the entry being created ("": none) */
static int short_alias(uint32_t dir, const char *name, char n83[11])
{
    new_lname[0] = 0;
    if (to83(name, n83) == FS_OK)
        return FS_OK;
    size_t n = strlen(name);
    if (!n || n >= sizeof new_lname || name[0] == '.')
        return FS_ENAME;
    const char *dot = strrchr(name, '.');
    uint32_t hash = 5381;
    for (const char *p = name; *p; p++)
        hash = hash * 33 + (uint8_t)*p;
    /* NAME~1 .. NAME~4, then (as Windows does) two letters and four hex digits of a hash: NA3F2C~1 */
    for (int k = 1; k <= 20; k++) {
        memset(n83, ' ', 11);
        int o = 0;
        for (const char *p = name; *p && p != dot && o < (k <= 4 ? 6 : 2); p++) {
            uint8_t c = up((uint8_t)*p);
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
                n83[o++] = (char)c;
        }
        if (!o)
            n83[o++] = '_';
        if (k > 4)
            for (int i = 0; i < 4; i++)
                n83[o++] = "0123456789ABCDEF"[((hash + (uint32_t)k * 0x9E37u) >> (4 * i)) & 15];
        n83[o] = '~', n83[o + 1] = (char)('0' + (k <= 4 ? k : 1));
        for (int i = 0, j = 8; dot && dot[1 + i] && j < 11; i++) {
            uint8_t c = up((uint8_t)dot[1 + i]);
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
                n83[j++] = (char)c;
        }
        find_t f;
        f.name = "";
        f.valid83 = true;
        memcpy(f.n83, n83, 11);
        int r = dir_walk(dir, find_cb, &f);
        if (r < 0)
            return r;
        if (!r) {
            memcpy(new_lname, name, n + 1);
            return FS_OK;
        }
    }
    return FS_EFULL;
}

/* place a new 32-byte entry in directory `dir` (extends a cluster directory),
 * with the long-name entries of new_lname before it when it is set. Uses the
 * first run of free (0xE5) or end (0x00) slots long enough. Reusing end slots
 * is safe without writing a new end marker because every slot after the
 * first 0x00 is 0x00 as well (new directory clusters are zero-filled). When
 * the chain is full a zeroed cluster is appended and the run continues there.
 * The fixed FAT16 root cannot grow (FS_EFULL). */
static int dir_add(uint32_t dir, const uint8_t *ent)
{
    size_t ln = strlen(new_lname);
    int need = ln ? (int)(ln + 12) / 13 + 1 : 1, run = 0, r;
    dit_t d, start;
    dit_start(&d, dir);
    uint32_t lastc = d.clus;
    for (;;) {
        if ((r = dit_next(&d)) < 0)
            return r;
        if (r == 0)
            break;
        if (d.clus)
            lastc = d.clus;
        uint8_t *e = sec + d.idx * 32;
        if (e[0] == 0 || e[0] == 0xE5) {
            if (!run++)
                start = d;
            if (run == need)
                goto put;
        } else {
            run = 0;
        }
    }
    if (!lastc)
        return FS_EFULL; /* FAT16 root is full */
    uint32_t nc;
    if ((r = alloc(lastc, &nc, true)))
        return r;
    if (!run)
        start = (dit_t){nc, 0, clus_lba(nc), 0};
put:
    /* the long-name parts (the last part first), then the 8.3 entry */
    d = start;
    for (int k = 0; k < need; k++) {
        if (k ? dit_next(&d) != 1 : load(d.lba) != FS_OK)
            return FS_EIO;
        uint8_t *e = sec + d.idx * 32;
        if (k == need - 1) {
            memcpy(e, ent, 32);
        } else {
            static const uint8_t pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
            int seq = need - 1 - k;
            memset(e, 0, 32);
            e[0] = (uint8_t)(seq | (k ? 0 : 0x40));
            e[11] = 0x0F, e[13] = sfn_sum(ent);
            for (int j = 0; j < 13; j++) {
                size_t ci = (size_t)(seq - 1) * 13 + (size_t)j;
                wr16(e + pos[j], ci < ln ? (uint8_t)new_lname[ci] : ci == ln ? 0 : 0xFFFF);
            }
        }
        if ((r = store(d.lba)))
            return r;
    }
    new_lname[0] = 0;
    return FS_OK;
}


/* ------------------------------------------------------------ files */
/* Open for reading, or for writing (create or replace). Writing does not touch
 * the card yet: it records where the existing entry is (to be rewritten by
 * fs_close) or the 8.3 name for a new entry. Only one file may be open. */
int fs_open(fs_file_t *f, const char *path, int mode)
{
    if (busy)
        return FS_EBUSY; /* *f untouched: it may be the open file's handle */
    memset(f, 0, sizeof *f);
    int r = fs_mount();
    if (r)
        return r;
    char last[64];
    uint32_t dir;
    if ((r = resolve(path, &dir, last, sizeof last)))
        return r;
    find_t fd;
    int found = dir_find(dir, last, &fd);
    if (found < 0)
        return found;
    if (found && (fd.ent[11] & 0x10))
        return FS_EISDIR;
    f->parent = dir;
    if (mode == FS_READ) {
        if (!found)
            return FS_ENOENT;
        f->first = f->cur = ent_clus(fd.ent);
        f->size = rd32(fd.ent + 28);
    } else {
        if (found) {
            memcpy(f->name83, fd.ent, 11);
            f->dir_lba = fd.lba;
            f->dir_off = fd.off;
            f->old_first = ent_clus(fd.ent);
        } else if ((r = short_alias(dir, last, f->name83))) {
            return r;
        }
        f->writing = true;
    }
    f->open = true;
    busy = true;
    return FS_OK;
}

/* Copy up to n bytes from the current position, a sector at a time through
 * sec[]. The chain is followed lazily: crossing a cluster boundary reads the
 * FAT entry of the current cluster. A chain shorter than the size in the
 * directory entry is FS_EIO. */
int fs_read(fs_file_t *f, void *buf, size_t n)
{
    if (!f->open || f->writing)
        return FS_EINVAL;
    uint8_t *out = buf;
    size_t done = 0;
    uint32_t csize = V.spc * SEC;
    while (done < n && f->pos < f->size) {
        if (f->pos && f->pos % csize == 0) {
            uint32_t nx;
            if (fat_get(f->cur, &nx) || nx < 2 || is_eoc(nx))
                return FS_EIO;
            f->cur = nx;
        }
        if (f->cur < 2 || load(clus_lba(f->cur) + (f->pos % csize) / SEC))
            return FS_EIO;
        uint32_t o = f->pos % SEC, k = SEC - o;
        if (k > f->size - f->pos)
            k = f->size - f->pos;
        if (k > n - done)
            k = (uint32_t)(n - done);
        memcpy(out + done, sec + o, k);
        done += k;
        f->pos += k;
    }
    return (int)done;
}

/* Write wbuf as the file's sector containing byte pos-1 (the sector just
 * completed). The first sector of each cluster first allocates a new cluster
 * chained after the current one. The data goes straight to the card (wbuf is
 * separate from sec[], so the caches are not disturbed). */
static int flush_sector(fs_file_t *f)
{
    uint32_t s = (f->pos - 1) / SEC; /* sector index in the file */
    if (s % V.spc == 0) {
        uint32_t nc;
        int r = alloc(f->cur, &nc, false);
        if (r)
            return r;
        if (!f->first)
            f->first = nc;
        f->cur = nc;
    }
    return wr(clus_lba(f->cur) + s % V.spc, wbuf);
}

/* Append n bytes (writes are sequential; there is no seek). Full sectors are
 * written immediately, a partial sector stays in wbuf until fs_close. On an
 * error the file is aborted (closed, new clusters freed). */
int fs_write(fs_file_t *f, const void *buf, size_t n)
{
    if (!f->open || !f->writing)
        return FS_EINVAL;
    const uint8_t *in = buf;
    for (size_t i = 0; i < n; i++) {
        wbuf[f->pos % SEC] = in[i];
        f->pos++;
        if (f->pos % SEC == 0) {
            int r = flush_sector(f);
            if (r) {
                fs_abort(f);
                return r;
            }
        }
    }
    f->size = f->pos;
    return (int)n;
}

/* Close without publishing: a new version's clusters are freed, the old
 * version (if any) is untouched. */
void fs_abort(fs_file_t *f)
{
    if (f->open && f->writing && f->first)
        free_chain(f->first);
    f->open = false;
    busy = false;
}

/* Close; for a written file: flush the last (zero-padded) sector, then publish
 * by writing the directory entry (existing entry rewritten in place, keeping
 * its attributes and any long name, or a new entry added), then free the old
 * version's chain. Until the entry write, the card still describes the old
 * file; between it and the end of free_chain a power loss only leaks the old
 * clusters. */
int fs_close(fs_file_t *f)
{
    if (!f->open)
        return FS_EINVAL;
    if (!f->writing) {
        f->open = false;
        busy = false;
        return FS_OK;
    }
    int r = FS_OK;
    if (f->pos % SEC) {
        memset(wbuf + f->pos % SEC, 0, SEC - f->pos % SEC);
        r = flush_sector(f);
    }
    if (r) {
        fs_abort(f);
        return r;
    }
    /* publish: one sector write switches readers to the new data */
    uint8_t e[32];
    if (f->dir_lba) {
        if ((r = load(f->dir_lba)))
            goto fail;
        memcpy(e, sec + f->dir_off, 32);
    } else {
        memset(e, 0, 32);
        memcpy(e, f->name83, 11);
        e[11] = 0x20;
    }
    stamp(e);
    set_clus(e, f->first);
    wr32(e + 28, f->size);
    if (f->dir_lba) {
        memcpy(sec + f->dir_off, e, 32);
        r = store(f->dir_lba);
    } else {
        r = dir_add(f->parent, e);
    }
    if (r)
        goto fail;
    if (f->old_first)
        free_chain(f->old_first);
    f->open = false;
    busy = false;
    return FS_OK;
fail:
    fs_abort(f);
    return r;
}

/* ------------------------------------------------------------ listing etc. */
typedef struct {
    int (*cb)(const fs_dirent_t *e, void *user);
    void *user;
} list_t;

/* Convert an entry to fs_dirent_t for the user callback; "." and ".." (and
 * any other name starting with '.') are skipped. */
static int list_cb(const uint8_t *e, const char *lname, dit_t *d, void *user)
{
    list_t *l = user;
    if (e[0] == '.')
        return 0;
    fs_dirent_t de;
    from83(e, de.short_name);
    if (lname) {
        strncpy(de.name, lname, sizeof de.name - 1);
        de.name[sizeof de.name - 1] = 0;
    } else {
        strcpy(de.name, de.short_name);
    }
    de.size = rd32(e + 28);
    de.is_dir = (e[11] & 0x10) != 0;
    /* the callback may use the sector cache: keep our position valid */
    uint32_t keep = sec_lba;
    int r = l->cb(&de, l->user);
    if (sec_lba != keep && keep != 0xFFFFFFFFu)
        load(keep);
    return r ? 1 : 0;
}

int fs_list(const char *dir, int (*cb)(const fs_dirent_t *e, void *user), void *user)
{
    int r = fs_mount();
    if (r)
        return r;
    uint32_t clus = 0;
    if (dir && *dir && strcmp(dir, "/")) {
        char last[64];
        uint32_t parent;
        if ((r = resolve(dir, &parent, last, sizeof last)))
            return r;
        find_t f;
        if ((r = dir_find(parent, last, &f)) <= 0)
            return r ? r : FS_ENOENT;
        if (!(f.ent[11] & 0x10))
            return FS_EISDIR;
        clus = ent_clus(f.ent);
    }
    list_t l = {cb, user};
    r = dir_walk(clus, list_cb, &l);
    return r < 0 ? r : FS_OK;
}

/* Delete a file: mark its long-name entries deleted (0xE5), then its short
 * entry, then free its chain (a power loss in between leaves at worst a file
 * without its long name, or lost clusters). Directories are refused
 * (FS_EISDIR). */
int fs_remove(const char *path)
{
    if (busy)
        return FS_EBUSY;
    int r = fs_mount();
    if (r)
        return r;
    char last[64];
    uint32_t dir;
    if ((r = resolve(path, &dir, last, sizeof last)))
        return r;
    find_t f;
    if ((r = dir_find(dir, last, &f)) <= 0)
        return r ? r : FS_ENOENT;
    if (f.ent[11] & 0x10)
        return FS_EISDIR;
    /* The long-name entries directly precede the short one, possibly across
     * sectors and clusters: walk them with the iterator from the first. */
    dit_t d = f.lfn;
    for (int k = 0; k < f.nlfn; k++) {
        if (k ? dit_next(&d) != 1 : load(d.lba) != FS_OK)
            return FS_EIO;
        sec[d.idx * 32] = 0xE5;
        if ((d.idx == 15 || k == f.nlfn - 1) && (r = store(d.lba)))
            return r;
    }
    if ((r = load(f.lba)))
        return r;
    sec[f.off] = 0xE5;
    if ((r = store(f.lba)))
        return r;
    return free_chain(ent_clus(f.ent));
}

/* Create a directory: allocate a zeroed cluster, write its "." (itself) and
 * ".." (parent; 0 means the root, as the specification requires also on
 * FAT32) entries, then add the entry to the parent, which makes it visible.
 * Succeeds without change if the directory already exists. */
int fs_mkdir(const char *path)
{
    if (busy)
        return FS_EBUSY;
    int r = fs_mount();
    if (r)
        return r;
    char last[64], n83[11];
    uint32_t dir, c;
    if ((r = resolve(path, &dir, last, sizeof last)))
        return r;
    find_t f;
    if ((r = dir_find(dir, last, &f)) < 0)
        return r;
    if (r)
        return f.ent[11] & 0x10 ? FS_OK : FS_EISDIR;
    if ((r = short_alias(dir, last, n83)) || (r = alloc(0, &c, true)))
        return r;
    uint8_t e[32];
    memset(e, 0, 32);
    memset(e, ' ', 11);
    e[0] = '.';
    e[11] = 0x10;
    stamp(e);
    set_clus(e, c);
    if (load(clus_lba(c))) {
        free_chain(c);
        return FS_EIO;
    }
    memcpy(sec, e, 32);
    e[1] = '.';
    set_clus(e, dir == V.root_clus ? 0 : dir);
    memcpy(sec + 32, e, 32);
    if ((r = store(clus_lba(c)))) {
        free_chain(c);
        return r;
    }
    memcpy(e, n83, 11);
    set_clus(e, c);
    if ((r = dir_add(dir, e)))
        free_chain(c); /* never became reachable */
    return r;
}

/* Count free clusters by reading the whole FAT (FSInfo is not trusted, and
 * this code invalidates it anyway): up to fat_sz sector reads. free_kb =
 * clusters * sectors per cluster / 2 (512-byte sectors). */
int fs_info(uint32_t *blocks, uint32_t *free_kb, int *fat_type)
{
    int r = fs_mount();
    if (r)
        return r;
    uint32_t nfree = 0;
    for (uint32_t c = 2; c < V.nclus + 2; c++) {
        uint32_t v;
        if (fat_get(c, &v))
            return FS_EIO;
        nfree += v == 0;
    }
    *blocks = V.blocks;
    *free_kb = nfree * V.spc / 2;
    *fat_type = V.type;
    return FS_OK;
}

/* ------------------------------------------------------------ mkfs */
#if defined(PLATFORM_STM32)
int fs_mkfs(void) { return FS_EINVAL; } /* simulators only (flash budget; cards come formatted) */
#else
/* Format the whole card without a partition table (sector 0 = boot sector):
 * FAT16 below 64 MB, else FAT32, two FATs, 512-byte sectors. Cluster size:
 * FAT32 follows the Microsoft default table (fatgen103), FAT16 uses the
 * smallest power of two with card sectors / spc <= 65000 (cluster count below
 * the FAT16 limit of 65525). The FAT size and the cluster count depend on each
 * other, so they are iterated a few rounds; the result is range-checked.
 * FAT32 gets 32 reserved sectors with FSInfo at 1 and the backup boot sector
 * at 6 (backup FSInfo at 7), and the root directory in cluster 2. */
int fs_mkfs(void)
{
    if (busy)
        return FS_EBUSY;
    fs_unmount();
    uint32_t n;
    if (!hal_sd_present() || hal_sd_open(&n))
        return FS_ENOCARD;
    bool f32 = n >= 131072; /* >= 64 MB */
    uint32_t spc, rsvd = f32 ? 32 : 1, rootsec = f32 ? 0 : 32, fatsz = 1, nclus = 0;
    if (f32)
        spc = n <= 532480 ? 1 : n <= 16777216 ? 8 : n <= 33554432 ? 16 : n <= 67108864 ? 32 : 64;
    else
        for (spc = 1; spc < 128 && n / spc > 65000; spc *= 2) {
        }
    for (int it = 0; it < 8; it++) {
        nclus = (n - rsvd - 2 * fatsz - rootsec) / spc;
        fatsz = ((nclus + 2) * (f32 ? 4 : 2) + SEC - 1) / SEC;
    }
    nclus = (n - rsvd - 2 * fatsz - rootsec) / spc;
    if (f32 ? nclus < 65525 : (nclus < 4085 || nclus >= 65525))
        return FS_EINVAL;
    /* boot sector */
    memset(sec, 0, SEC);
    sec_lba = 0xFFFFFFFFu;
    fsec_lba = 0xFFFFFFFFu;
    memcpy(sec, f32 ? "\xEB\x58\x90" "CALC1.0 " : "\xEB\x3C\x90" "CALC1.0 ", 11);
    wr16(sec + 11, SEC);
    sec[13] = (uint8_t)spc;
    wr16(sec + 14, rsvd);
    sec[16] = 2;
    wr16(sec + 17, f32 ? 0 : 512);
    if (!f32 && n < 65536)
        wr16(sec + 19, n);
    else
        wr32(sec + 32, n);
    sec[21] = 0xF8;      /* media: fixed disk */
    wr16(sec + 24, 63);  /* nominal CHS geometry: sectors per track */
    wr16(sec + 26, 255); /*   and heads (unused with LBA) */
    uint8_t *x = sec + (f32 ? 64 : 36); /* extended BPB */
    if (f32) {
        wr32(sec + 36, fatsz);
        wr32(sec + 44, 2);  /* root cluster */
        wr16(sec + 48, 1);  /* FSInfo */
        wr16(sec + 50, 6);  /* backup boot sector */
    } else {
        wr16(sec + 22, fatsz);
    }
    x[0] = 0x80;              /* drive number */
    x[2] = 0x29;              /* extended boot signature: serial, label, type follow */
    wr32(x + 3, 0x20261019u); /* volume serial number */
    memcpy(x + 7, "CALC       ", 11);
    memcpy(x + 18, f32 ? "FAT32   " : "FAT16   ", 8);
    wr16(sec + 510, 0xAA55);
    if (wr(0, sec) || (f32 && wr(6, sec)))
        return FS_EIO;
    if (f32) {
        /* FSInfo: signatures, free count (all but the root's cluster 2) and
         * next free cluster hint */
        memset(sec, 0, SEC);
        wr32(sec, 0x41615252u);
        wr32(sec + 484, 0x61417272u);
        wr32(sec + 488, nclus - 1);
        wr32(sec + 492, 3);
        wr16(sec + 510, 0xAA55);
        if (wr(1, sec) || wr(7, sec))
            return FS_EIO;
    }
    /* FATs and the root directory */
    memset(sec, 0, SEC);
    for (uint32_t s = rsvd; s < rsvd + 2 * fatsz + rootsec; s++)
        if (wr(s, sec))
            return FS_EIO;
    if (f32)
        for (uint32_t s = 0; s < spc; s++)
            if (wr(rsvd + 2 * fatsz + s, sec))
                return FS_EIO;
    /* first FAT sector of both copies: entry 0 = media byte, entry 1 = EOC
     * (clean-shutdown bits set), FAT32 entry 2 = root directory (EOC) */
    if (f32) {
        wr32(sec, 0x0FFFFFF8u), wr32(sec + 4, 0x0FFFFFFFu), wr32(sec + 8, 0x0FFFFFFFu);
    } else {
        wr16(sec, 0xFFF8), wr16(sec + 2, 0xFFFF);
    }
    if (wr(rsvd, sec) || wr(rsvd + fatsz, sec))
        return FS_EIO;
    sec_lba = 0xFFFFFFFFu;
    hal_sd_close();
    return fs_mount();
}
#endif
