/*
 * The simulators' USB: the device model behind hal_usb_*, the computer's
 * side of the CALC drive (a small FAT16 writer and reader that writes in the
 * orders macOS and Windows use, in the root or in a subdirectory), and the
 * console `usb`. See usbhost.h.
 */
#include "usbhost.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "console.h"
#include "hal.h"
#include "hostdev.h"
#include "rle.h"
#include "usbdisk.h"

/* ------------------------------------------------------------ the computer's FAT16 */
typedef struct {
    uint32_t fat0, fatsz, nfats, root0, rootents, data0, total, nclus, spc;
} vol_t;

static uint32_t r16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t r32(const uint8_t *p) { return r16(p) | r16(p + 2) << 16; }
static void w16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v) { w16(p, v), w16(p + 2, v >> 16); }
static char up(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }

static int mount(const uh_dev_t *d, vol_t *v)
{
    uint8_t b[512];
    if (d->read(d->u, 0, b) || r16(b + 11) != 512 || !b[13] || b[510] != 0x55 || b[511] != 0xAA)
        return -1;
    v->spc = b[13];
    v->fat0 = r16(b + 14);
    v->nfats = b[16];
    v->rootents = r16(b + 17);
    v->total = r16(b + 19) ? r16(b + 19) : r32(b + 32);
    v->fatsz = r16(b + 22);
    v->root0 = v->fat0 + v->nfats * v->fatsz;
    v->data0 = v->root0 + v->rootents * 32 / 512;
    v->nclus = (v->total - v->data0) / v->spc;
    return 0;
}

static uint32_t fat_get(const uh_dev_t *d, const vol_t *v, uint32_t c)
{
    uint8_t b[512];
    if (d->read(d->u, v->fat0 + c * 2 / 512, b))
        return 0xFFF7;
    return r16(b + c * 2 % 512);
}

static int fat_set(const uh_dev_t *d, const vol_t *v, uint32_t c, uint32_t val)
{
    for (uint32_t k = 0; k < v->nfats; k++) { /* every copy, as hosts do */
        uint8_t b[512];
        uint32_t lba = v->fat0 + k * v->fatsz + c * 2 / 512;
        if (d->read(d->u, lba, b))
            return -1;
        w16(b + c * 2 % 512, val);
        if (d->write(d->u, lba, b))
            return -1;
    }
    return 0;
}

/* Free clusters for n bytes (every other one with frag), searched from the
 * low end; 0 if there is no room. */
static int alloc(const uh_dev_t *d, const vol_t *v, uint32_t n, bool frag, uint32_t *out)
{
    uint32_t need = n ? (n + 511) / 512 : 0, k = 0;
    bool skip = false;
    for (uint32_t c = 2; c < v->nclus + 2 && k < need; c++) {
        if (fat_get(d, v, c))
            continue;
        if (frag && (skip = !skip) == false)
            continue;
        out[k++] = c;
    }
    return k == need ? 0 : -1;
}

/* Directories: 0 is the fixed root, else the first cluster of a
 * subdirectory's chain. Entry i lives in sector *lba at byte *off; a chain
 * that ends before it is extended by a zeroed cluster when `grow` (as hosts
 * do when a directory is full), else -1. */
static int ent_at(const uh_dev_t *d, const vol_t *v, uint32_t dir, uint32_t i, bool grow, uint32_t *lba, uint32_t *off)
{
    *off = i * 32 % 512;
    if (!dir) {
        if (i >= v->rootents)
            return -1;
        *lba = v->root0 + i * 32 / 512;
        return 0;
    }
    uint32_t per = v->spc * 16, c = dir;
    for (uint32_t k = i / per; k; k--) {
        uint32_t nx = fat_get(d, v, c);
        if (nx >= 0xFFF8) {
            uint32_t nc;
            if (!grow || alloc(d, v, 512, false, &nc) || fat_set(d, v, nc, 0xFFFF) || fat_set(d, v, c, nc))
                return -1;
            uint8_t z[512];
            memset(z, 0, sizeof z);
            for (uint32_t s = 0; s < v->spc; s++)
                if (d->write(d->u, v->data0 + (nc - 2) * v->spc + s, z))
                    return -1;
            nx = nc;
        } else if (nx < 2 || nx >= 0xFFF0) {
            return -1;
        }
        c = nx;
    }
    *lba = v->data0 + (c - 2) * v->spc + i % per * 32 / 512;
    return 0;
}

/* read/write one 32-byte entry */
static int ent_rw(const uh_dev_t *d, const vol_t *v, uint32_t dir, uint32_t i, uint8_t *e, bool write)
{
    uint8_t b[512];
    uint32_t lba, off;
    if (ent_at(d, v, dir, i, write, &lba, &off) || d->read(d->u, lba, b))
        return -1;
    if (!write) {
        memcpy(e, b + off, 32);
        return 0;
    }
    memcpy(b + off, e, 32);
    return d->write(d->u, lba, b);
}

/* n consecutive free entries (a subdirectory grows past its end) */
static int free_entries(const uh_dev_t *d, const vol_t *v, uint32_t dir, uint32_t n)
{
    uint32_t run = 0;
    for (uint32_t i = 0;; i++) {
        uint8_t e[32];
        if (ent_rw(d, v, dir, i, e, false)) {
            if (!dir)
                return -1;
            return (int)(i - run); /* the chain ends: the rest goes into new clusters */
        }
        run = e[0] == 0 || e[0] == 0xE5 ? run + 1 : 0;
        if (run == n)
            return (int)(i + 1 - n);
    }
}

/* A short name for a long one ("._GEOME010.MOD" -> "_GEOME~1MOD") */
static void short_for(const char *name, uint8_t sn[11], int k1)
{
    memset(sn, ' ', 11);
    const char *dot = strrchr(name, '.');
    int k = 0;
    for (const char *p = name; *p && p != dot && k < 6; p++)
        if (*p != '.' && *p != ' ')
            sn[k++] = (uint8_t)up(*p);
    sn[k++] = '~', sn[k] = (uint8_t)('0' + k1);
    for (int i = 0; dot && dot[1 + i] && i < 3; i++)
        sn[8 + i] = (uint8_t)up(dot[1 + i]);
}

static bool fits83(const char *name)
{
    int k = 0;
    const char *p = name;
    for (; *p && *p != '.'; p++, k++)
        if (k >= 8 || *p == ' ' || (*p >= 'a' && *p <= 'z'))
            return false;
    if (p == name)
        return false;
    if (*p == '.')
        for (p++, k = 0; *p; p++, k++)
            if (k >= 3 || *p == '.' || (*p >= 'a' && *p <= 'z'))
                return false;
    return true;
}

static uint8_t lfn_sum(const uint8_t *n)
{
    uint8_t s = 0;
    for (int i = 0; i < 11; i++)
        s = (uint8_t)(((s & 1) << 7) + (s >> 1) + n[i]);
    return s;
}

/* Walk a directory: cb(name, entry, index of the 8.3 entry, index of its first long-name part) per
 * file or directory (not labels, not deleted). */
typedef int (*walk_fn)(const char *name, const uint8_t *e, uint32_t at, uint32_t from, void *u);
static int walk(const uh_dev_t *d, const vol_t *v, uint32_t dir, walk_fn cb, void *u)
{
    char lfn[80] = "";
    bool have = false;
    uint32_t from = 0;
    for (uint32_t i = 0;; i++) {
        uint8_t e[32];
        if (ent_rw(d, v, dir, i, e, false))
            return dir ? 0 : -1;
        if (!e[0])
            break;
        if (e[0] == 0xE5) {
            have = false;
            continue;
        }
        if ((e[11] & 0x3F) == 0x0F) {
            static const uint8_t pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
            int k = e[0] & 0x1F;
            if (e[0] & 0x40)
                memset(lfn, 0, sizeof lfn), have = true, from = i;
            for (int j = 0; j < 13 && k >= 1; j++) {
                int at = (k - 1) * 13 + j;
                uint32_t ch = r16(e + pos[j]);
                if (at < (int)sizeof lfn - 1 && ch && ch != 0xFFFF)
                    lfn[at] = (char)ch;
            }
            continue;
        }
        if (e[11] & 0x08) {
            have = false;
            continue;
        }
        char name[80];
        if (have)
            snprintf(name, sizeof name, "%s", lfn);
        else {
            int k = 0;
            for (int j = 0; j < 8 && e[j] != ' '; j++)
                name[k++] = (char)e[j];
            if (e[8] != ' ')
                name[k++] = '.';
            for (int j = 8; j < 11 && e[j] != ' '; j++)
                name[k++] = (char)e[j];
            name[k] = 0;
            from = i;
        }
        have = false;
        int r = cb(name, e, i, from, u);
        if (r)
            return r;
    }
    return 0;
}

typedef struct {
    const char *want;
    uint32_t first, size, at, from;
    uint8_t attr;
    bool found;
} find_t;
static int find_cb(const char *name, const uint8_t *e, uint32_t at, uint32_t from, void *u)
{
    find_t *f = u;
    for (size_t i = 0;; i++) {
        if (up(name[i]) != up(f->want[i]))
            return 0;
        if (!name[i])
            break;
    }
    f->first = r16(e + 26), f->size = r32(e + 28), f->attr = e[11], f->at = at, f->from = from, f->found = true;
    return 1;
}

/* "DIR/NAME" -> the directory (its first cluster) and the name; "NAME" -> the root */
static int resolve(const uh_dev_t *d, const vol_t *v, const char *path, uint32_t *dir, const char **name)
{
    const char *sl = strchr(path, '/');
    *dir = 0, *name = path;
    if (!sl)
        return 0;
    char dn[64];
    snprintf(dn, sizeof dn, "%.*s", (int)(sl - path), path);
    find_t f = {dn, 0, 0, 0, 0, 0, false};
    walk(d, v, 0, find_cb, &f);
    if (!f.found || !(f.attr & 0x10) || f.first < 2)
        return -2;
    *dir = f.first, *name = sl + 1;
    return 0;
}

/* Directory entry (with long-name entries before it when the name is not 8.3):
 * returns the index of the 8.3 entry, <0 if the directory is full. */
static int make_entry(const uh_dev_t *d, const vol_t *v, uint32_t dir, const char *name, uint8_t attr, uint32_t first,
                      uint32_t size)
{
    uint8_t sn[11];
    bool lfn = !fits83(name);
    if (lfn) { /* ~1, ~2, ...: the first short name not taken in this directory */
        for (int k = 1; k <= 9; k++) {
            short_for(name, sn, k);
            char s83[13];
            int o = 0;
            for (int j = 0; j < 8 && sn[j] != ' '; j++)
                s83[o++] = (char)sn[j];
            if (sn[8] != ' ')
                s83[o++] = '.';
            for (int j = 8; j < 11 && sn[j] != ' '; j++)
                s83[o++] = (char)sn[j];
            s83[o] = 0;
            find_t f = {s83, 0, 0, 0, 0, 0, false};
            walk(d, v, dir, find_cb, &f);
            if (!f.found)
                break;
        }
    } else {
        memset(sn, ' ', 11);
        const char *dot = strrchr(name, '.');
        for (int i = 0; name + i != dot && name[i] && i < 8; i++)
            sn[i] = (uint8_t)name[i];
        for (int i = 0; dot && dot[1 + i] && i < 3; i++)
            sn[8 + i] = (uint8_t)dot[1 + i];
    }
    int nl = lfn ? (int)(strlen(name) + 12) / 13 : 0;
    int at = free_entries(d, v, dir, (uint32_t)nl + 1);
    if (at < 0)
        return -1;
    static const uint8_t pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    size_t len = strlen(name);
    for (int k = nl; k >= 1; k--) { /* the last part first */
        uint8_t e[32];
        memset(e, 0, sizeof e);
        e[0] = (uint8_t)(k | (k == nl ? 0x40 : 0));
        e[11] = 0x0F, e[13] = lfn_sum(sn);
        for (int j = 0; j < 13; j++) {
            size_t ci = (size_t)(k - 1) * 13 + (size_t)j;
            uint32_t ch = ci < len ? (uint8_t)name[ci] : ci == len ? 0 : 0xFFFF;
            w16(e + pos[j], ch);
        }
        if (ent_rw(d, v, dir, (uint32_t)(at + nl - k), e, true))
            return -1;
    }
    uint8_t e[32];
    memset(e, 0, sizeof e);
    memcpy(e, sn, 11);
    e[11] = attr;
    w16(e + 26, first);
    w32(e + 28, size);
    if (ent_rw(d, v, dir, (uint32_t)(at + nl), e, true))
        return -1;
    return at + nl;
}

/* Delete a file as hosts do when it is replaced: its entries marked deleted, its chain freed. */
static int remove_file(const uh_dev_t *d, const vol_t *v, uint32_t dir, const char *name)
{
    find_t f = {name, 0, 0, 0, 0, 0, false};
    walk(d, v, dir, find_cb, &f);
    if (!f.found)
        return 0;
    for (uint32_t i = f.from; i <= f.at; i++) {
        uint8_t e[32];
        if (ent_rw(d, v, dir, i, e, false))
            return -1;
        e[0] = 0xE5;
        if (ent_rw(d, v, dir, i, e, true))
            return -1;
    }
    for (uint32_t c = f.first; c >= 2 && c < 0xFFF0;) {
        uint32_t nx = fat_get(d, v, c);
        if (fat_set(d, v, c, 0))
            return -1;
        c = nx;
    }
    return 0;
}

static int write_data(const uh_dev_t *d, const vol_t *v, const uint32_t *cl, uint32_t n, const uint8_t *data,
                      uint32_t size)
{
    for (uint32_t i = 0; i < n; i++) {
        uint8_t b[512];
        memset(b, 0, sizeof b);
        uint32_t k = size - i * 512 < 512 ? size - i * 512 : 512;
        memcpy(b, data + i * 512, k);
        if (d->write(d->u, v->data0 + cl[i] - 2, b))
            return -1;
    }
    return 0;
}

static int write_chain(const uh_dev_t *d, const vol_t *v, const uint32_t *cl, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (fat_set(d, v, cl[i], i + 1 < n ? cl[i + 1] : 0xFFFF))
            return -1;
    return 0;
}

/* One file in directory `dir`, in the given order: entry first and size last (mac), or data,
 * chain and entry (win). */
static int put(const uh_dev_t *d, const vol_t *v, uint32_t dir, const char *name, uint8_t attr, const uint8_t *data,
               uint32_t size, bool mac, bool frag)
{
    uint32_t n = (size + 511) / 512;
    uint32_t *cl = calloc(n + 1, sizeof *cl);
    if (!cl || alloc(d, v, size, frag, cl)) {
        free(cl);
        return -1;
    }
    int r = 0, at = -1;
    if (mac) {
        at = make_entry(d, v, dir, name, attr, 0, 0);
        r = at < 0 || write_data(d, v, cl, n, data, size) || write_chain(d, v, cl, n);
        if (!r) { /* the size and first cluster last */
            uint8_t e[32];
            r = ent_rw(d, v, dir, (uint32_t)at, e, false);
            w16(e + 26, n ? cl[0] : 0);
            w32(e + 28, attr & 0x10 ? 0 : size);
            r = r || ent_rw(d, v, dir, (uint32_t)at, e, true);
        }
    } else {
        r = write_data(d, v, cl, n, data, size) || write_chain(d, v, cl, n) ||
            make_entry(d, v, dir, name, attr, n ? cl[0] : 0, attr & 0x10 ? 0 : size) < 0;
    }
    free(cl);
    return r ? -1 : 0;
}

int uh_write_file(const uh_dev_t *d, const char *path, const uint8_t *data, uint32_t size, int style)
{
    vol_t v;
    if (mount(d, &v) || v.spc != 1) /* (writing: one sector per cluster, as the CALC drive has) */
        return -1;
    uint32_t dir;
    const char *name;
    if (resolve(d, &v, path, &dir, &name))
        return -1;
    bool mac = (style & 0xFF) == UH_MAC, frag = (style & UH_FRAG) != 0;
    if ((style & UH_REPLACE) && remove_file(d, &v, dir, name))
        return -1;
    uint8_t dirc[512];
    memset(dirc, 0, sizeof dirc); /* a directory cluster: "." and ".." */
    memcpy(dirc, ".          ", 11), dirc[11] = 0x10;
    memcpy(dirc + 32, "..         ", 11), dirc[32 + 11] = 0x10;
    if (mac) {
        /* the Finder: its event log directory, then the AppleDouble file of the copy (next to it) */
        find_t f = {".fseventsd", 0, 0, 0, 0, 0, false};
        walk(d, &v, 0, find_cb, &f);
        if (!f.found && put(d, &v, 0, ".fseventsd", 0x12, dirc, 512, true, false))
            return -1;
        uint8_t ad[4096];
        memset(ad, 0, sizeof ad);
        ad[1] = 0x05, ad[2] = 0x16, ad[3] = 0x07; /* AppleDouble magic 0x00051607 */
        char adn[80];
        snprintf(adn, sizeof adn, "._%s", name);
        if (style & UH_REPLACE)
            remove_file(d, &v, dir, adn);
        if (put(d, &v, dir, adn, 0x02, ad, sizeof ad, true, false))
            return -1;
    } else {
        find_t f = {"System Volume Information", 0, 0, 0, 0, 0, false};
        walk(d, &v, 0, find_cb, &f);
        if (!f.found && put(d, &v, 0, "System Volume Information", 0x16, dirc, 512, false, false))
            return -1;
    }
    return put(d, &v, dir, name, 0x20, data, size, mac, frag);
}

typedef struct {
    void (*cb)(const char *name, uint32_t size, uint8_t attr, void *u);
    void *u;
} list_t;
static int list_cb(const char *name, const uint8_t *e, uint32_t at, uint32_t from, void *u)
{
    (void)at, (void)from;
    list_t *l = u;
    l->cb(name, r32(e + 28), e[11], l->u);
    return 0;
}

int uh_list(const uh_dev_t *d, const char *dirname,
            void (*cb)(const char *name, uint32_t size, uint8_t attr, void *u), void *u)
{
    vol_t v;
    if (mount(d, &v))
        return -1;
    uint32_t dir = 0;
    if (dirname && dirname[0]) {
        char p[80];
        const char *nm;
        snprintf(p, sizeof p, "%s/x", dirname);
        if (resolve(d, &v, p, &dir, &nm))
            return -2;
    }
    list_t l = {cb, u};
    return walk(d, &v, dir, list_cb, &l);
}

int uh_read_file(const uh_dev_t *d, const char *path, uint8_t *out, uint32_t max)
{
    vol_t v;
    if (mount(d, &v))
        return -1;
    uint32_t dir;
    const char *name;
    if (resolve(d, &v, path, &dir, &name))
        return -2;
    find_t f = {name, 0, 0, 0, 0, 0, false};
    walk(d, &v, dir, find_cb, &f);
    if (!f.found)
        return -2;
    uint32_t got = 0, c = f.first;
    while (got < f.size && got < max && c >= 2 && c < 0xFFF0) {
        for (uint32_t s = 0; s < v.spc && got < f.size && got < max; s++) {
            uint8_t b[512];
            if (d->read(d->u, v.data0 + (c - 2) * v.spc + s, b))
                return -1;
            uint32_t k = f.size - got < 512 ? f.size - got : 512;
            if (k > max - got)
                k = max - got;
            memcpy(out + got, b, k);
            got += k;
        }
        c = fat_get(d, &v, c);
    }
    return (int)got;
}

/* ------------------------------------------------------------ the device model */
/* VBUS is present (the simulators always reported USB power); a computer
 * configures the device only after `usb host on`. */
static bool vbus = true, configured;

bool hal_usb_present(void) { return vbus; }
bool hal_usb_configured(void) { return vbus && configured; }
void hal_usb_poll(void)
{
#if defined(PLATFORM_SIM)
    sim_console_readahead(); /* a Ctrl-C on the input stops a running computation, as over USB */
#endif
}
void hal_usb_status(void (*kv)(const char *k, const char *v, void *u), void *u)
{
    kv("VBUS", vbus ? "present (simulated)" : "absent (simulated)", u);
    kv("Device", !vbus ? "detached (no VBUS)" : configured ? "configured" : "powered (waiting for a host)", u);
    kv("Error", "none", u);
    kv("Drives", usbdisk_card_connected() ? "CALC; card connected" : "CALC", u);
}
void hal_usb_detach(void)
{
    configured = false;
    usbdisk_host(false);
}
/* The model's computer mounts the drive again at once. */
bool hal_usb_reconnect(uint32_t ms)
{
    (void)ms;
    if (!hal_usb_configured())
        return false;
    usbdisk_host(false);
    usbdisk_host(true);
    return true;
}

/* LUN 0 as the computer's block device */
/* Like a host: TEST UNIT READY first; a unit attention (the media changed) is
 * reported once, and the host simply asks again. */
static bool host_ready(int lun)
{
    usbdisk_sense_t s;
    return usbdisk_ready(lun, &s) || (s.key == 0x06 && usbdisk_ready(lun, &s));
}
static int lun_read(void *u, uint32_t lba, uint8_t *buf)
{
    usbdisk_sense_t s;
    int lun = (int)(intptr_t)u;
    if (!host_ready(lun))
        return -1;
    return usbdisk_read(lun, lba, buf, 1, &s);
}
static int lun_write(void *u, uint32_t lba, const uint8_t *buf)
{
    usbdisk_sense_t s;
    int lun = (int)(intptr_t)u;
    return usbdisk_write(lun, lba, buf, 1, &s);
}

static void print_entry(const char *name, uint32_t size, uint8_t attr, void *u)
{
    (void)u;
    con_printf("%-28s %8lu%s%s\n", name, (unsigned long)size, attr & 0x10 ? " dir" : "", attr & 0x01 ? " ro" : "");
}

/* usb [status] | host on|off | vbus on|off | ls | cat FILE | expect FILE TEXT | crc FILE
 *   | drop FILE [mac|win] [frag] [corrupt] [as NAME] | eject card */
int hostdev_cmd_usb(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "status";
    /* a last argument "card" reads the second unit (the microSD card) */
    bool card = argc > 2 && !strcmp(argv[argc - 1], "card") && strcmp(sub, "eject");
    if (card)
        argc--;
    uh_dev_t dev = {lun_read, lun_write, (void *)(intptr_t)(card ? USBDISK_CARD : USBDISK_CALC)};
    if (!strcmp(sub, "status")) {
        usbdisk_sense_t s = {0, 0, 0};
        bool ready = usbdisk_ready(USBDISK_CALC, &s);
        con_printf("vbus=%d configured=%d calc=%s sense=%02x/%02x/%02x card=%s\n", vbus, hal_usb_configured(),
                   ready ? "ready" : "not-ready", s.key, s.asc, s.ascq,
                   usbdisk_card_connected() ? (usbdisk_card_ejected() ? "ejected" : "connected") : "off");
        return 0;
    }
    if ((!strcmp(sub, "host") || !strcmp(sub, "vbus")) && argc > 2) {
        bool on = !strcmp(argv[2], "on");
        if (sub[0] == 'h')
            configured = on && vbus;
        else
            vbus = on, configured = configured && on;
        usbdisk_host(hal_usb_configured());
        return 0;
    }
    if (!hal_usb_configured())
        return con_error("no computer connected (usb host on)");
    if (!strcmp(sub, "ls")) {
        int r = uh_list(&dev, argc > 2 ? argv[2] : NULL, print_entry, NULL);
        return r == -2 ? con_error("no directory %s", argv[2]) : r ? con_error("drive not ready") : 0;
    }
    if ((!strcmp(sub, "cat") || !strcmp(sub, "expect") || !strcmp(sub, "lacks") || !strcmp(sub, "get") ||
         !strcmp(sub, "size")) && argc > 2) {
        enum { MAXF = 4u << 20 };
        uint8_t *buf = malloc(MAXF + 1);
        if (!buf)
            return con_error("out of memory");
        int n = uh_read_file(&dev, argv[2], buf, MAXF);
        int r = 0;
        if (n < 0) {
            r = con_error(n == -2 ? "no file %s" : "drive not ready", argv[2]);
        } else if (sub[0] == 'c') {
            buf[n] = 0;
            con_write((const char *)buf, (size_t)n);
            if (n && buf[n - 1] != '\n')
                con_write("\n", 1);
        } else if (sub[0] == 's') {
            con_printf("size=%d\n", n);
        } else if (sub[0] == 'g') { /* usb get FILE HOSTPATH: a copy on the computer's own disk */
#ifdef PLATFORM_WEB
            /* the web page has no disk (built without a file system: fopen would throw) */
            con_printf("(web page: %s read, %d bytes, not copied)\n", argv[2], n);
#else
            FILE *fp = argc > 3 ? fopen(argv[3], "wb") : NULL;
            if (!fp || fwrite(buf, 1, (size_t)n, fp) != (size_t)n)
                r = con_error("cannot write %s", argc > 3 ? argv[3] : "(no path)");
            if (fp)
                fclose(fp);
#endif
        } else if (sub[0] == 'l') { /* usb lacks FILE TEXT: the file does not contain TEXT */
            buf[n] = 0;
            if (argc < 4 || strstr((const char *)buf, argv[3])) {
                console_expect_failures++;
                r = con_error("%s contains \"%s\"", argv[2], argc > 3 ? argv[3] : "");
            }
        } else {
            buf[n] = 0;
            if (argc < 4 || !strstr((const char *)buf, argv[3])) {
                console_expect_failures++;
                r = con_error("%s does not contain \"%s\"", argv[2], argc > 3 ? argv[3] : "");
            }
        }
        free(buf);
        return r;
    }
    if (!strcmp(sub, "crc") && argc > 2) { /* a whole file as the computer reads it: size and CRC-32 */
        uint32_t max = 8u << 20;
        uint8_t *buf = malloc(max);
        if (!buf)
            return con_error("out of memory");
        int n = uh_read_file(&dev, argv[2], buf, max);
        if (n >= 0)
            con_printf("size=%d crc32=%08lx\n", n, (unsigned long)crc32_update(0, buf, (size_t)n));
        free(buf);
        return n < 0 ? con_error(n == -2 ? "no file %s" : "drive not ready", argv[2]) : 0;
    }
    if (!strcmp(sub, "drop") && argc > 2) {
        int style = UH_MAC;
        bool corrupt = false;
        const char *base = strrchr(argv[2], '/');
        const char *as = base ? base + 1 : argv[2];
        for (int i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "win"))
                style = (style & ~0xFF) | UH_WIN;
            else if (!strcmp(argv[i], "frag"))
                style |= UH_FRAG;
            else if (!strcmp(argv[i], "replace"))
                style |= UH_REPLACE;
            else if (!strcmp(argv[i], "corrupt"))
                corrupt = true;
            else if (!strcmp(argv[i], "as") && i + 1 < argc)
                as = argv[++i];
        }
        /* a module file of the build (the emulator's library), a file on the computer's disk
         * (a path), else a small text file */
        const uint8_t *data = (const uint8_t *)"hello\n";
        uint32_t size = 6;
        uint8_t *host = NULL;
        for (int i = 0; i < content_library_all; i++)
            if (!strcmp(content_library[i].name, argv[2]))
                data = content_library[i].data, size = content_library[i].size;
#ifdef PLATFORM_WEB
        FILE *fp = NULL; /* no host files in the web page */
#else
        FILE *fp = data[0] == 'h' ? fopen(argv[2], "rb") : NULL;
#endif
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long len = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            host = len >= 0 ? malloc((size_t)len + 1) : NULL;
            if (host && fread(host, 1, (size_t)len, fp) == (size_t)len)
                data = host, size = (uint32_t)len;
            fclose(fp);
        }
        uint8_t *copy = malloc(size ? size : 1);
        if (!copy) {
            free(host);
            return con_error("out of memory");
        }
        memcpy(copy, data, size);
        free(host);
        if (corrupt && size > 0x1000)
            copy[0x1000 + (size - 0x1000) / 2] ^= 0x5A; /* a payload byte */
        else if (corrupt && size > 2)
            copy[size / 2] = '\x01'; /* a small (text) file: not valid any more */
        int r = uh_write_file(&dev, as, copy, size, style);
        free(copy);
        return r ? con_error("the drive refused the file (full or not ready)") : 0;
    }
    if (!strcmp(sub, "eject") && argc > 2 && !strcmp(argv[2], "card")) {
        usbdisk_eject(USBDISK_CARD);
        return 0;
    }
    return con_error("usage: usb status|host on|off|vbus on|off|ls [DIR] [card]|cat F [card]|expect F TEXT [card]|"
                     "size F|crc F|get F HOSTPATH|drop F|PATH [mac|win] [frag] [replace] [corrupt] [as [DIR/]NAME]|eject card");
}
