/* Virtual FAT16 drive: generation and write capture. Design in vdrive.h. */
#include "vdrive.h"
#include <string.h>

#define FAT_BAD 0xFFF7u
#define FAT_EOC 0xFFFFu
#define ENTRY 32u

static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v), wr16(p + 2, v >> 16); }
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }
static uint32_t div_up(uint32_t a, uint32_t b) { return (a + b - 1) / b; }
static char upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }

#define ROOT_SECTORS (VD_ROOT_ENTRIES * ENTRY / VD_SECTOR)
#define SUB_ENTRIES (VD_SUB_SECTORS * VD_SECTOR / ENTRY)
/* FAT sectors for up to max_clusters() clusters: the staging region, the
 * generated files and the subdirectory (VD_GEN_CLUSTERS), the padding */
static uint32_t fat_sectors_for(uint32_t clusters) { return div_up((clusters + 2) * 2, VD_SECTOR); }
static uint32_t max_clusters(uint32_t stage_clusters) { return stage_clusters + VD_GEN_CLUSTERS + VD_MIN_CLUSTERS; }
static const uint8_t lfn_pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30}; /* characters of a long-name entry */

size_t vd_mem_need(uint32_t stage_bytes)
{
    uint32_t sc = stage_bytes / VD_SECTOR;
    return VD_MAX_FILES * sizeof(vd_file_t) + (size_t)fat_sectors_for(max_clusters(sc)) * VD_SECTOR +
           (ROOT_SECTORS + VD_SUB_SECTORS) * VD_SECTOR + div_up(sc, 8) + 4;
}

void vd_init(vdrive_t *vd, uint8_t *mem, uint8_t *stage, uint32_t stage_bytes)
{
    memset(vd, 0, sizeof *vd);
    vd->stage = stage;
    vd->stage_clusters = stage ? stage_bytes / VD_SECTOR : 0;
    vd->files = (vd_file_t *)(void *)mem; /* (mem is aligned for it) */
    vd->fat = mem + VD_MAX_FILES * sizeof(vd_file_t);
    vd->root = vd->fat + fat_sectors_for(max_clusters(vd->stage_clusters)) * VD_SECTOR;
    vd->sub = vd->root + ROOT_SECTORS * VD_SECTOR;
    vd->written = vd->sub + VD_SUB_SECTORS * VD_SECTOR;
    vd->root_sectors = ROOT_SECTORS;
}

bool vd_name83(const char *name, char out[11])
{
    memset(out, ' ', 11);
    int i = 0, k = 0;
    for (; name[i] && name[i] != '.'; i++) {
        if (k == 8)
            return false;
        out[k++] = upper(name[i]);
    }
    if (name[i] == '.')
        for (i++, k = 8; name[i]; i++) {
            if (k == 11)
                return false;
            out[k++] = upper(name[i]);
        }
    return out[0] != ' ';
}

void vd_begin(vdrive_t *vd, const char *label, uint32_t serial, uint16_t date, uint16_t time)
{
    memset(vd->label, ' ', sizeof vd->label);
    for (int i = 0; label[i] && i < 11; i++)
        vd->label[i] = upper(label[i]);
    vd->serial = serial;
    vd->date = date, vd->time = time;
    vd->nfiles = 0;
    vd->gen_clusters = 0;
    vd->subname[0] = 0;
}

/* directory entries a file takes: its long-name parts and the 8.3 entry */
static uint32_t entries_of(const vd_file_t *f) { return f->lname ? (uint32_t)(strlen(f->lname) + 12) / 13 + 1 : 1; }

static vd_file_t *add(vdrive_t *vd, int dir, uint8_t attr, uint32_t size,
                      void (*read)(void *user, uint32_t off, uint8_t *buf, uint32_t n), void *user)
{
    uint32_t k = div_up(size, VD_SECTOR);
    if (vd->nfiles >= VD_MAX_FILES || vd->gen_clusters + k + VD_SUB_SECTORS > VD_GEN_CLUSTERS)
        return NULL;
    vd_file_t *f = &vd->files[vd->nfiles];
    memset(f, 0, sizeof *f);
    f->attr = attr, f->size = size, f->read = read, f->user = user, f->dir = (uint8_t)dir;
    return f;
}

/* room for f in its directory (the root also holds the label and the subdirectory's entry) */
static bool fits(const vdrive_t *vd, const vd_file_t *f)
{
    uint32_t used = 4; /* root: the label and the subdirectory (with a long name); subdirectory: . and .. */
    for (int i = 0; i < vd->nfiles; i++)
        if (vd->files[i].dir == f->dir)
            used += entries_of(&vd->files[i]);
    return used + entries_of(f) <= (f->dir ? SUB_ENTRIES : VD_ROOT_ENTRIES);
}

int vd_add_file(vdrive_t *vd, const char *name83, uint8_t attr, uint32_t size,
                void (*read)(void *user, uint32_t off, uint8_t *buf, uint32_t n), void *user)
{
    vd_file_t *f = add(vd, 0, attr, size, read, user);
    if (!f || !vd_name83(name83, f->name) || !fits(vd, f))
        return -1;
    vd->gen_clusters += div_up(size, VD_SECTOR);
    vd->nfiles++;
    return 0;
}

int vd_add_dir(vdrive_t *vd, const char *name)
{
    vd->sublong = NULL;
    if (!vd_name83(name, vd->subname)) { /* a long name: NOTEBO~1 */
        size_t n = strlen(name);
        if (!n || n > 63)
            return vd->subname[0] = 0, -1;
        memset(vd->subname, ' ', 11);
        for (size_t i = 0; i < 6 && i < n; i++)
            vd->subname[i] = upper(name[i]);
        vd->subname[6] = '~', vd->subname[7] = '1';
        vd->sublong = name;
    }
    return 0;
}

int vd_add_long(vdrive_t *vd, int dir, const char *lname, uint8_t attr, uint32_t size,
                void (*read)(void *user, uint32_t off, uint8_t *buf, uint32_t n), void *user)
{
    if ((dir && !vd->subname[0]) || !lname[0] || strlen(lname) > 63)
        return -1;
    vd_file_t *f = add(vd, dir ? 1 : 0, attr, size, read, user);
    if (!f)
        return -1;
    f->lname = lname;
    /* the 8.3 name: NBnnnn~1 and the first three characters of the extension (unique on the volume) */
    memset(f->name, ' ', 11);
    unsigned k = (unsigned)vd->nfiles + 1;
    const char *dot = strrchr(lname, '.');
    f->name[0] = 'N', f->name[1] = 'B';
    for (int i = 5; i >= 2; i--, k /= 10)
        f->name[i] = (char)('0' + k % 10);
    f->name[6] = '~', f->name[7] = '1';
    for (int i = 0; dot && dot[1 + i] && i < 3; i++)
        f->name[8 + i] = upper(dot[1 + i]);
    if (!fits(vd, f))
        return -1;
    vd->gen_clusters += div_up(size, VD_SECTOR);
    vd->nfiles++;
    return 0;
}

/* Long-name checksum of an 8.3 name (fatgen103 section 7). */
static uint8_t lfn_sum(const uint8_t *n)
{
    uint8_t s = 0;
    for (int i = 0; i < 11; i++)
        s = (uint8_t)(((s & 1) << 7) + (s >> 1) + n[i]);
    return s;
}

/* The directory entries of a file at e (its long-name parts, the last part
 * first, then the 8.3 entry); returns the position after them. */
static uint8_t *put_entries(const vdrive_t *vd, uint8_t *e, const char name[11], const char *lname, uint8_t attr,
                            uint32_t first, uint32_t size)
{
    if (lname) {
        uint32_t len = (uint32_t)strlen(lname), nl = (len + 12) / 13;
        uint8_t sum = lfn_sum((const uint8_t *)name);
        for (uint32_t k = nl; k >= 1; k--, e += ENTRY) {
            e[0] = (uint8_t)(k | (k == nl ? 0x40 : 0));
            e[11] = VD_ATTR_LFN, e[13] = sum;
            for (uint32_t j = 0; j < 13; j++) {
                uint32_t ci = (k - 1) * 13 + j;
                wr16(e + lfn_pos[j], ci < len ? (uint8_t)lname[ci] : ci == len ? 0 : 0xFFFF);
            }
        }
    }
    memcpy(e, name, 11);
    e[11] = attr;
    wr16(e + 14, vd->time), wr16(e + 16, vd->date); /* created */
    wr16(e + 18, vd->date);                          /* accessed */
    wr16(e + 22, vd->time), wr16(e + 24, vd->date); /* written */
    wr16(e + 26, first);
    wr32(e + 28, size);
    return e + ENTRY;
}

static void chain(vdrive_t *vd, uint32_t first, uint32_t k)
{
    for (uint32_t j = 0; j < k; j++)
        wr16(vd->fat + 2 * (first + j), j + 1 < k ? first + j + 1 : FAT_EOC);
}

/* Lay out the clusters, generate the FAT and the directories, forget every
 * host write. */
void vd_build(vdrive_t *vd)
{
    vd->free_first = 2;
    uint32_t c = vd->gen_first = 2 + vd->stage_clusters;
    for (int i = 0; i < vd->nfiles; i++) {
        vd_file_t *f = &vd->files[i];
        f->first = f->size ? c : 0;
        c += div_up(f->size, VD_SECTOR);
    }
    vd->sub_first = vd->subname[0] ? c : 0;
    if (vd->sub_first)
        c += VD_SUB_SECTORS;
    vd->pad_first = c;
    uint32_t n = c - 2;
    if (n < VD_MIN_CLUSTERS)
        n = VD_MIN_CLUSTERS; /* padding up to a FAT16 cluster count, marked bad */
    if (n > max_clusters(vd->stage_clusters))
        n = max_clusters(vd->stage_clusters); /* (cannot happen: vd_add_* keep within VD_GEN_CLUSTERS) */
    vd->nclusters = n;
    vd->fat_sectors = fat_sectors_for(n);
    vd->data_start = 1 + 2 * vd->fat_sectors + vd->root_sectors;
    vd->total_sectors = vd->data_start + n;

    /* FAT: media byte entries, the staging region free, each file and the
     * subdirectory one contiguous chain, padding bad */
    memset(vd->fat, 0, vd->fat_sectors * VD_SECTOR);
    wr16(vd->fat, 0xFFF8u);
    wr16(vd->fat + 2, FAT_EOC);
    for (int i = 0; i < vd->nfiles; i++)
        chain(vd, vd->files[i].first, div_up(vd->files[i].size, VD_SECTOR));
    if (vd->sub_first)
        chain(vd, vd->sub_first, VD_SUB_SECTORS);
    for (uint32_t cl = vd->pad_first; cl < n + 2; cl++)
        wr16(vd->fat + 2 * cl, FAT_BAD);

    /* root: the volume label, the files, the subdirectory; the subdirectory: . and .., its files */
    memset(vd->root, 0, vd->root_sectors * VD_SECTOR);
    memset(vd->sub, 0, VD_SUB_SECTORS * VD_SECTOR);
    uint8_t *e = vd->root, *s = vd->sub;
    memcpy(e, vd->label, 11);
    e[11] = VD_ATTR_LABEL;
    wr16(e + 22, vd->time), wr16(e + 24, vd->date);
    e += ENTRY;
    if (vd->sub_first) {
        s = put_entries(vd, s, ".          ", NULL, VD_ATTR_DIR, vd->sub_first, 0);
        s = put_entries(vd, s, "..         ", NULL, VD_ATTR_DIR, 0, 0);
    }
    for (int i = 0; i < vd->nfiles; i++) {
        const vd_file_t *f = &vd->files[i];
        if (f->dir)
            s = put_entries(vd, s, f->name, f->lname, f->attr, f->first, f->size);
        else
            e = put_entries(vd, e, f->name, f->lname, f->attr, f->first, f->size);
    }
    if (vd->sub_first)
        put_entries(vd, e, vd->subname, vd->sublong, VD_ATTR_DIR, vd->sub_first, 0);
    if (vd->written)
        memset(vd->written, 0, div_up(vd->stage_clusters, 8));
    vd->dirty = false;
    vd->writes = 0;
    vd->ndone = 0;
    vd->cur_first = 0;
}

uint32_t vd_sectors(const vdrive_t *vd) { return vd->total_sectors; }
bool vd_writable(const vdrive_t *vd) { return vd->stage_clusters > 0; }
uint32_t vd_free_bytes(const vdrive_t *vd) { return vd->stage_clusters * VD_SECTOR; }

/* Boot sector: BPB for FAT16 (fatgen103 section 3), no partition table. */
static void boot_sector(const vdrive_t *vd, uint8_t *b)
{
    memset(b, 0, VD_SECTOR);
    b[0] = 0xEB, b[1] = 0x3C, b[2] = 0x90;      /* jmp short; nop */
    memcpy(b + 3, "MSDOS5.0", 8);               /* OEM name hosts expect */
    wr16(b + 11, VD_SECTOR);                    /* bytes per sector */
    b[13] = 1;                                  /* sectors per cluster */
    wr16(b + 14, 1);                            /* reserved sectors (the boot sector) */
    b[16] = 2;                                  /* FATs */
    wr16(b + 17, VD_ROOT_ENTRIES);
    if (vd->total_sectors < 0x10000u)
        wr16(b + 19, vd->total_sectors);
    else
        wr32(b + 32, vd->total_sectors);
    b[21] = 0xF8;                               /* media: fixed disk */
    wr16(b + 22, vd->fat_sectors);
    wr16(b + 24, 63), wr16(b + 26, 255);        /* geometry (unused) */
    b[36] = 0x80;                               /* drive number */
    b[38] = 0x29;                               /* extended boot signature */
    wr32(b + 39, vd->serial);
    memcpy(b + 43, vd->label, 11);
    memcpy(b + 54, "FAT16   ", 8);
    b[510] = 0x55, b[511] = 0xAA;
}

static bool in_stage(const vdrive_t *vd, uint32_t c) { return c >= vd->free_first && c < vd->gen_first; }
static bool in_sub(const vdrive_t *vd, uint32_t c) { return vd->sub_first && c >= vd->sub_first && c < vd->pad_first; }

static bool is_written(const vdrive_t *vd, uint32_t c)
{
    uint32_t i = c - vd->free_first;
    return (vd->written[i / 8] >> (i % 8)) & 1;
}

void vd_read(vdrive_t *vd, uint32_t lba, uint8_t *buf)
{
    uint32_t fat0 = 1, root0 = 1 + 2 * vd->fat_sectors;
    if (lba == 0) {
        boot_sector(vd, buf);
        return;
    }
    if (lba < root0) { /* either FAT copy: the one buffer */
        memcpy(buf, vd->fat + ((lba - fat0) % vd->fat_sectors) * VD_SECTOR, VD_SECTOR);
        return;
    }
    if (lba < vd->data_start) {
        memcpy(buf, vd->root + (lba - root0) * VD_SECTOR, VD_SECTOR);
        return;
    }
    memset(buf, 0, VD_SECTOR);
    if (lba >= vd->total_sectors)
        return;
    uint32_t c = lba - vd->data_start + 2;
    if (in_stage(vd, c)) {
        if (is_written(vd, c))
            memcpy(buf, vd->stage + (c - vd->free_first) * VD_SECTOR, VD_SECTOR);
        return;
    }
    if (in_sub(vd, c)) {
        memcpy(buf, vd->sub + (c - vd->sub_first) * VD_SECTOR, VD_SECTOR);
        return;
    }
    for (int i = 0; i < vd->nfiles; i++) {
        const vd_file_t *f = &vd->files[i];
        uint32_t k = div_up(f->size, VD_SECTOR);
        if (f->first && c >= f->first && c < f->first + k) {
            uint32_t off = (c - f->first) * VD_SECTOR, n = f->size - off;
            f->read(f->user, off, buf, n < VD_SECTOR ? n : VD_SECTOR);
            return;
        }
    }
}

void vd_write(vdrive_t *vd, uint32_t lba, const uint8_t *buf, uint32_t now)
{
    uint32_t fat0 = 1, root0 = 1 + 2 * vd->fat_sectors;
    vd->dirty = true;
    vd->last_write = now;
    vd->writes++;
    if (lba == 0 || lba >= vd->total_sectors)
        return; /* the boot sector stays ours */
    if (lba < root0) {
        memcpy(vd->fat + ((lba - fat0) % vd->fat_sectors) * VD_SECTOR, buf, VD_SECTOR);
        return;
    }
    if (lba < vd->data_start) {
        memcpy(vd->root + (lba - root0) * VD_SECTOR, buf, VD_SECTOR);
        return;
    }
    uint32_t c = lba - vd->data_start + 2;
    if (in_sub(vd, c)) {
        memcpy(vd->sub + (c - vd->sub_first) * VD_SECTOR, buf, VD_SECTOR);
        return;
    }
    if (!in_stage(vd, c))
        return; /* the generated files are read-only; padding is bad */
    uint32_t i = c - vd->free_first;
    memcpy(vd->stage + i * VD_SECTOR, buf, VD_SECTOR);
    vd->written[i / 8] |= (uint8_t)(1u << (i % 8));
}

/* ---------------------------------------------------------- capture */

static uint32_t fat_next(const vdrive_t *vd, uint32_t c)
{
    return c < vd->nclusters + 2 ? rd16(vd->fat + 2 * c) : FAT_BAD;
}

/* "NAME.EXT" of an 8.3 entry */
static void short_name(const uint8_t *e, char *out)
{
    int k = 0;
    for (int i = 0; i < 8 && e[i] != ' '; i++)
        out[k++] = (char)(i == 0 && e[0] == 0x05 ? 0xE5 : e[i]);
    if (e[8] != ' ') {
        out[k++] = '.';
        for (int i = 8; i < 11 && e[i] != ' '; i++)
            out[k++] = (char)e[i];
    }
    out[k] = 0;
}

/* s ends in ext (".mod", ".ipynb": lower case), case-insensitively */
static bool ends_with(const char *s, const char *ext)
{
    size_t n = strlen(s), k = strlen(ext);
    if (n <= k)
        return false;
    for (size_t i = 0; i < k; i++)
        if (upper(s[n - k + i]) != upper(ext[i]))
            return false;
    return true;
}

static bool was_done(const vdrive_t *vd, uint32_t first, uint32_t size)
{
    for (int i = 0; i < vd->ndone; i++)
        if (vd->done[i].first == first && vd->done[i].size == size)
            return true;
    return false;
}

/* The chain of a size-byte file from `first` is complete: exactly the
 * clusters it needs, each in the staging region and written, the last one
 * marked end of chain. */
static bool chain_complete(const vdrive_t *vd, uint32_t first, uint32_t size)
{
    uint32_t need = div_up(size, VD_SECTOR), c = first;
    for (uint32_t k = 0; k < need; k++) {
        if (!in_stage(vd, c) || !is_written(vd, c))
            return false;
        uint32_t nx = fat_next(vd, c);
        if (k + 1 == need)
            return nx >= 0xFFF8u;
        c = nx;
    }
    return false;
}

/* Entry i of directory `dir` (0 the root, 1 the subdirectory along its
 * chain: the generated clusters, then any the host added in the staging
 * region), NULL past its end. */
static const uint8_t *dir_entry(const vdrive_t *vd, int dir, uint32_t i)
{
    if (!dir)
        return i < VD_ROOT_ENTRIES ? vd->root + i * ENTRY : NULL;
    uint32_t c = vd->sub_first;
    for (uint32_t k = i / (VD_SECTOR / ENTRY); k; k--) {
        c = fat_next(vd, c);
        if (c < 2 || c >= 0xFFF0u)
            return NULL;
    }
    const uint8_t *s = in_sub(vd, c)                          ? vd->sub + (c - vd->sub_first) * VD_SECTOR
                       : in_stage(vd, c) && is_written(vd, c) ? vd->stage + (c - vd->free_first) * VD_SECTOR
                                                              : NULL;
    return s ? s + i % (VD_SECTOR / ENTRY) * ENTRY : NULL;
}

/* The next complete candidate in directory `dir` (1, *f), else 0. */
static int scan(vdrive_t *vd, int dir, vd_found_t *f)
{
    char lfn[64];
    int lfn_ok = 0; /* the long name collected for the next 8.3 entry (its checksum) */
    uint8_t lfn_chk = 0;
    const uint8_t *e;
    for (uint32_t i = 0; (e = dir_entry(vd, dir, i)) != NULL && e[0]; i++) {
        if (e[0] == 0xE5) {
            lfn_ok = 0;
            continue;
        }
        if ((e[11] & 0x3F) == VD_ATTR_LFN) {
            /* a long-name part: ordinal (bit 6: last), 13 UTF-16 characters */
            int ord = e[0] & 0x1F;
            if (e[0] & 0x40)
                memset(lfn, 0, sizeof lfn), lfn_ok = 1, lfn_chk = e[13];
            for (int k = 0; k < 13 && ord >= 1; k++) {
                int at = (ord - 1) * 13 + k;
                uint32_t ch = rd16(e + lfn_pos[k]);
                if (at < (int)sizeof lfn - 1 && ch != 0xFFFF && ch)
                    lfn[at] = ch < 0x80 ? (char)ch : '_';
            }
            continue;
        }
        char name[64];
        if (lfn_ok && lfn_chk == lfn_sum(e))
            memcpy(name, lfn, sizeof name);
        else
            short_name(e, name);
        lfn_ok = 0;
        if (e[11] & (VD_ATTR_LABEL | VD_ATTR_DIR) || name[0] == '.')
            continue; /* labels, directories, hidden metadata (._NAME, .DS_Store, ...) */
        uint32_t first = rd16(e + 26), size = rd32(e + 28);
        if (!in_stage(vd, first) || !size || was_done(vd, first, size))
            continue; /* ours, empty (being written), or handled */
        if (!chain_complete(vd, first, size))
            continue;
        memcpy(f->name, name, sizeof f->name);
        f->name[sizeof f->name - 1] = 0;
        f->first = first, f->size = size;
        f->module = ends_with(name, ".mod");
        f->notebook = ends_with(name, ".ipynb");
        f->dir = (uint8_t)dir;
        return 1;
    }
    return 0;
}

int vd_poll(vdrive_t *vd, uint32_t now, vd_found_t *f)
{
    /* only after new writes (a file returned but not handed back with vd_done
     * keeps it set: the scan stops at it before clearing) */
    if (!vd->stage_clusters || !vd->dirty || now - vd->last_write < VD_QUIET_MS)
        return 0;
    if (scan(vd, 0, f) || (vd->sub_first && scan(vd, 1, f)))
        return 1;
    vd->dirty = false;
    return 0;
}

void vd_done(vdrive_t *vd, const vd_found_t *f)
{
    if (vd->ndone < VD_MAX_DONE)
        vd->done[vd->ndone++] = *f;
}

int vd_file_read(vdrive_t *vd, const vd_found_t *f, uint32_t off, void *buf, uint32_t n)
{
    uint8_t *out = buf;
    uint32_t got = 0;
    if (off >= f->size)
        return 0;
    if (n > f->size - off)
        n = f->size - off;
    while (got < n) {
        uint32_t index = (off + got) / VD_SECTOR, in = (off + got) % VD_SECTOR;
        /* walk the chain from the cursor when reading on, else from the start */
        if (vd->cur_first != f->first || index < vd->cur_index)
            vd->cur_first = f->first, vd->cur_index = 0, vd->cur_cluster = f->first;
        while (vd->cur_index < index) {
            vd->cur_cluster = fat_next(vd, vd->cur_cluster);
            vd->cur_index++;
        }
        uint32_t c = vd->cur_cluster;
        if (!in_stage(vd, c)) {
            vd->cur_first = 0;
            return -1;
        }
        uint32_t k = VD_SECTOR - in < n - got ? VD_SECTOR - in : n - got;
        memcpy(out + got, vd->stage + (c - vd->free_first) * VD_SECTOR + in, k);
        got += k;
    }
    return (int)got;
}
