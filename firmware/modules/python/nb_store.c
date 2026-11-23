/*
 * The notebook store (nb.h): notebooks in a region of the QSPI flash
 * (app/py_core.h NB_STORE_*: 4 MB at 0xA00000), without a microSD card.
 *
 * Layout: the region is a pool of 4 KB sectors. A version of a notebook (its
 * whole .ipynb) is written as a chain of sectors, each with a 64-byte header
 * and up to 4032 bytes of the file:
 *
 *   0  "NBK1"     4  seq (the version: store-wide, increasing)
 *   8  idx (u16)  10 flags (u16, bit 0: the last sector)
 *   12 length     16 CRC-32 of this sector's bytes
 *   20 total size of the file
 *   24 name (35 characters and a NUL)
 *   60 CRC-32 of the header's first 60 bytes
 *
 * There is no directory: mounting reads the header of every sector (1024 of
 * them) and collects the versions. A version is complete when every sector
 * 0..last is there and the last one is marked; sectors of incomplete versions
 * are free. Writing a version erases a sector, programs its bytes, reads them
 * back against the CRC, then programs the header; the sector marked last goes
 * last. So a power loss leaves either the new version complete or an
 * incomplete one that is ignored: the previous version stays the notebook.
 *
 * Versions: the newest complete version of a name is the notebook; older ones
 * stay (the "previous version" a notebook can go back to) until their sectors
 * are needed. Free sectors are taken in ring order from where the last write
 * ended (wear levelling); when there are not enough, older versions are
 * reclaimed, oldest first, those listed on the USB drive (pinned) last. A
 * delete programs the first word of each version's last sector to zero (NOR
 * can clear bits without an erase): the version is incomplete from then on.
 *
 * Names are compared without regard to case (the USB drive shows them as
 * files on computers that do not tell cases apart). A sector that holds a
 * module extent's header ("CMOD", from before the module store was smaller)
 * is never touched.
 */
#include <string.h>
#include "nb.h"

#define MAGIC 0x314B424Eu /* "NBK1" */
#define MOD_MAGIC 0x444F4D43u /* a module extent (core/modstore.h) */
#define F_LAST 1u

typedef struct {
    uint32_t magic, seq;
    uint16_t idx, flags;
    uint32_t len, dcrc, total;
    char name[NBS_NAME];
    uint32_t hcrc;
} hdr_t;
_Static_assert(sizeof(hdr_t) == NBS_HDR, "notebook sector header");

static uint8_t cache[NBS_DATA]; /* the sector being read (nbs_read) or verified */
static int cache_v = -1, cache_k = -1;

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static bool same(const char *a, const char *b)
{
    for (; *a && lower(*a) == lower(*b); a++, b++) {
    }
    return lower(*a) == lower(*b);
}
bool nbs_name_same(const char *a, const char *b) { return same(a, b); }
static uint32_t chunks_for(uint32_t size) { return size ? (size + NBS_DATA - 1) / NBS_DATA : 1; }

bool nbs_name_ok(const char *n)
{
    size_t k = strlen(n);
    if (!k || k >= NBS_NAME || n[0] == '.' || n[0] == ' ' || n[k - 1] == ' ' || n[k - 1] == '.')
        return false;
    for (; *n; n++)
        if ((unsigned char)*n < 0x20 || (unsigned char)*n > 0x7E || strchr("/\\:*?\"<>|", *n))
            return false;
    return true;
}

/* ---------------------------------------------------------------- the device */
static int dev_write(nbs_t *s, uint32_t off, const void *p, size_t n)
{
    if (s->fail_after >= 0 && s->fail_after-- == 0) {
        s->fail_after = 0;
        return -1;
    }
    s->writes++;
    return s->dev->write(s->dev->u, off, p, n);
}
static int dev_erase(nbs_t *s, uint32_t off)
{
    if (s->fail_after >= 0 && s->fail_after-- == 0) {
        s->fail_after = 0;
        return -1;
    }
    s->erases++;
    return s->dev->erase(s->dev->u, off, NBS_SECTOR);
}

static bool hdr_ok(const hdr_t *h)
{
    return h->magic == MAGIC && crc32_update(0, h, offsetof(hdr_t, hcrc)) == h->hcrc && h->len <= NBS_DATA &&
           h->total <= NBS_MAX_BYTES && h->name[NBS_NAME - 1] == 0;
}

/* ---------------------------------------------------------------- mount */
static int find_seq(nbs_t *s, uint32_t seq)
{
    for (int i = 0; i < s->nver; i++)
        if (s->ver[i].seq == seq && s->ver[i].nchunks)
            return i;
    return -1;
}

int nbs_mount(nbs_t *s, const nbs_dev_t *dev)
{
    memset(s, 0, sizeof *s);
    s->fail_after = -1;
    s->keep = -1;
    cache_v = cache_k = -1;
    s->dev = dev;
    if (!dev || dev->size < NBS_SECTOR)
        return NBS_E_NODEV;
    s->nsec = dev->size / NBS_SECTOR < NBS_MAX_SECT ? dev->size / NBS_SECTOR : NBS_MAX_SECT;
    uint32_t newest = 0, newest_sec = 0;
    for (uint32_t i = 0; i < s->nsec; i++) {
        hdr_t h;
        s->owner[i] = -1;
        if (dev->read(dev->u, i * NBS_SECTOR, &h, sizeof h))
            return NBS_E_IO;
        if (h.magic == MOD_MAGIC) {
            s->owner[i] = -2; /* a module's extent: keep out */
            continue;
        }
        if (!hdr_ok(&h) || h.idx >= chunks_for(h.total))
            continue;
        if (h.seq > s->max_seq)
            s->max_seq = h.seq;
        int v = find_seq(s, h.seq);
        if (v < 0) {
            if (s->nver >= NBS_MAX_VER)
                continue;
            v = s->nver++;
            nbs_ver_t *nv = &s->ver[v];
            memset(nv, 0, sizeof *nv);
            nv->seq = h.seq, nv->size = h.total, nv->last = -1;
            memcpy(nv->name, h.name, NBS_NAME);
        }
        nbs_ver_t *ver = &s->ver[v];
        if (h.total != ver->size || strcmp(h.name, ver->name))
            continue; /* (cannot happen: one seq is one write) */
        s->owner[i] = (int16_t)v;
        s->cidx[i] = h.idx;
        ver->nchunks++;
        if (h.flags & F_LAST)
            ver->last = (int16_t)h.idx;
        if (h.seq >= newest)
            newest = h.seq, newest_sec = i;
    }
    for (int v = 0; v < s->nver; v++) {
        nbs_ver_t *ver = &s->ver[v];
        ver->live = ver->last >= 0 && (uint32_t)ver->last + 1 == chunks_for(ver->size) &&
                    ver->nchunks == (uint32_t)ver->last + 1;
        if (!ver->live)
            for (uint32_t i = 0; i < s->nsec; i++)
                if (s->owner[i] == v)
                    s->owner[i] = -1;
    }
    s->cursor = (newest_sec + 1) % s->nsec;
    s->mounted = true;
    return NBS_OK;
}

/* ---------------------------------------------------------------- lookups */
/* The newest live version of `name` (which 0), the one before it (1), ... */
int nbs_find(nbs_t *s, const char *name, int which)
{
    uint32_t below = UINT32_MAX;
    int best = -1;
    for (int k = 0; k <= which; k++) {
        best = -1;
        for (int v = 0; v < s->nver; v++)
            if (s->ver[v].live && s->ver[v].seq < below && same(s->ver[v].name, name) &&
                (best < 0 || s->ver[v].seq > s->ver[best].seq))
                best = v;
        if (best < 0)
            return NBS_E_NOTFOUND;
        below = s->ver[best].seq;
    }
    return best;
}

static bool is_current(nbs_t *s, int v) { return nbs_find(s, s->ver[v].name, 0) == v; }

int nbs_list(nbs_t *s, nbs_entry_t *out, int max)
{
    int n = 0;
    for (int v = 0; v < s->nver && s->mounted; v++) {
        if (!s->ver[v].live || !is_current(s, v))
            continue;
        int k = 0;
        for (int u = 0; u < s->nver; u++)
            k += s->ver[u].live && same(s->ver[u].name, s->ver[v].name);
        /* insert sorted by name */
        int at = n;
        while (at > 0) {
            const char *a = out[at - 1].name, *b = s->ver[v].name;
            while (*a && lower(*a) == lower(*b))
                a++, b++;
            if (lower(*a) <= lower(*b))
                break;
            at--;
        }
        if (at >= max)
            continue;
        if (n == max)
            n--;
        memmove(out + at + 1, out + at, (size_t)(n - at) * sizeof *out);
        memcpy(out[at].name, s->ver[v].name, NBS_NAME);
        out[at].size = s->ver[v].size, out[at].seq = s->ver[v].seq, out[at].versions = k;
        n++;
    }
    return n;
}

uint32_t nbs_free(nbs_t *s)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < s->nsec; i++)
        k += s->owner[i] == -1 || (s->owner[i] >= 0 && !is_current(s, s->owner[i]));
    return k * NBS_DATA;
}

static int sector_of(nbs_t *s, int v, uint32_t idx)
{
    for (uint32_t i = 0; i < s->nsec; i++)
        if (s->owner[i] == v && s->cidx[i] == idx)
            return (int)i;
    return -1;
}

/* ---------------------------------------------------------------- reading */
int nbs_open(nbs_t *s, const char *name, int which, nbs_rd_t *r)
{
    memset(r, 0, sizeof *r);
    r->s = s;
    r->v = s->mounted ? nbs_find(s, name, which) : NBS_E_NODEV;
    return r->v < 0 ? r->v : NBS_OK;
}

static int load_chunk(nbs_t *s, int v, uint32_t k)
{
    if (cache_v == v && cache_k == (int)k)
        return NBS_OK;
    int i = sector_of(s, v, k);
    hdr_t h;
    if (i < 0 || s->dev->read(s->dev->u, (uint32_t)i * NBS_SECTOR, &h, sizeof h) || !hdr_ok(&h) ||
        s->dev->read(s->dev->u, (uint32_t)i * NBS_SECTOR + NBS_HDR, cache, h.len))
        return NBS_E_IO;
    if (crc32_update(0, cache, h.len) != h.dcrc) {
        cache_v = -1;
        return NBS_E_CRC;
    }
    cache_v = v, cache_k = (int)k;
    return NBS_OK;
}

int nbs_read(void *rd, uint8_t *buf, size_t n)
{
    nbs_rd_t *r = rd;
    nbs_ver_t *ver = &r->s->ver[r->v];
    if (r->err || r->v < 0)
        return -1;
    size_t got = 0;
    while (got < n && r->pos < ver->size) {
        uint32_t k = r->pos / NBS_DATA, in = r->pos % NBS_DATA;
        int e = load_chunk(r->s, r->v, k);
        if (e) {
            r->err = e;
            return -1;
        }
        size_t m = NBS_DATA - in < n - got ? NBS_DATA - in : n - got;
        if (m > ver->size - r->pos)
            m = ver->size - r->pos;
        memcpy(buf + got, cache + in, m);
        got += m, r->pos += (uint32_t)m;
    }
    return (int)got;
}

/* ---------------------------------------------------------------- writing */
/* Reclaim the oldest version that is not the current one of its name (unpinned first). */
static bool reclaim(nbs_t *s)
{
    for (int pass = 0; pass < 2; pass++) {
        int best = -1;
        for (int v = 0; v < s->nver; v++)
            if (s->ver[v].live && s->ver[v].pinned == (pass == 1) && v != s->keep && !is_current(s, v) &&
                (best < 0 || s->ver[v].seq < s->ver[best].seq))
                best = v;
        if (best >= 0) {
            s->ver[best].live = false;
            for (uint32_t i = 0; i < s->nsec; i++)
                if (s->owner[i] == best)
                    s->owner[i] = -1;
            return true;
        }
    }
    return false;
}

int nbs_create(nbs_t *s, const char *name, uint32_t size, nbs_wr_t *w)
{
    memset(w, 0, offsetof(nbs_wr_t, buf));
    w->s = s;
    w->blen = 0, w->err = 0; /* (after buf: not cleared above) */
    if (!s->mounted)
        return w->err = NBS_E_NODEV;
    if (!nbs_name_ok(name))
        return w->err = NBS_E_NAME;
    if (size > NBS_MAX_BYTES)
        return w->err = NBS_E_SIZE;
    uint32_t k = chunks_for(size);
    for (;;) {
        int n = 0;
        for (uint32_t j = 0; j < s->nsec && n < (int)k; j++)
            if (s->owner[(s->cursor + j) % s->nsec] == -1)
                w->sec[n++] = (uint16_t)((s->cursor + j) % s->nsec);
        if (n == (int)k)
            break;
        if (!reclaim(s))
            return w->err = NBS_E_FULL;
    }
    s->cursor = (w->sec[k - 1] + 1u) % s->nsec;
    memcpy(w->name, name, strlen(name) + 1);
    w->size = size, w->nsec = (int)k, w->seq = s->max_seq + 1;
    for (int i = 0; i < w->nsec; i++)
        s->owner[w->sec[i]] = -3; /* reserved (not free, not a version yet) */
    return NBS_OK;
}

static void write_chunk(nbs_wr_t *w, bool last)
{
    nbs_t *s = w->s;
    if (w->err || w->k >= w->nsec) {
        w->err = w->err ? w->err : NBS_E_SIZE;
        return;
    }
    uint32_t off = (uint32_t)w->sec[w->k] * NBS_SECTOR;
    hdr_t h;
    memset(&h, 0, sizeof h);
    h.magic = MAGIC, h.seq = w->seq, h.idx = (uint16_t)w->k, h.flags = last ? F_LAST : 0;
    h.len = (uint32_t)w->blen, h.dcrc = crc32_update(0, w->buf, w->blen), h.total = w->size;
    memcpy(h.name, w->name, strlen(w->name) + 1);
    h.hcrc = crc32_update(0, &h, offsetof(hdr_t, hcrc));
    hdr_t back;
    cache_v = -1; /* (the read cache may hold this sector's old contents) */
    if (dev_erase(s, off) || (w->blen && dev_write(s, off + NBS_HDR, w->buf, w->blen)) ||
        s->dev->read(s->dev->u, off + NBS_HDR, cache, w->blen) || crc32_update(0, cache, w->blen) != h.dcrc ||
        dev_write(s, off, &h, sizeof h) || s->dev->read(s->dev->u, off, &back, sizeof back) ||
        memcmp(&back, &h, sizeof h)) {
        w->err = NBS_E_IO;
        return;
    }
    w->k++;
    w->blen = 0;
}

void nbs_put(void *wr, const void *p, size_t n)
{
    nbs_wr_t *w = wr;
    const uint8_t *b = p;
    while (n && !w->err) {
        if (w->blen == NBS_DATA)
            write_chunk(w, false);
        size_t m = NBS_DATA - w->blen < n ? NBS_DATA - w->blen : n;
        if (w->pos + m > w->size) {
            w->err = NBS_E_SIZE; /* more than announced */
            return;
        }
        memcpy(w->buf + w->blen, b, m);
        w->blen += m, w->pos += (uint32_t)m, b += m, n -= m;
    }
}

int nbs_close(nbs_wr_t *w)
{
    nbs_t *s = w->s;
    if (!w->err && w->pos != w->size)
        w->err = NBS_E_SIZE;
    if (!w->err)
        write_chunk(w, true);
    if (!w->err && w->k != w->nsec)
        w->err = NBS_E_SIZE;
    /* the table: a slot for the new version (a dead one, or a new one) */
    int v = -1;
    if (!w->err) {
        for (int i = 0; i < s->nver && v < 0; i++) {
            bool used = s->ver[i].live;
            for (uint32_t j = 0; j < s->nsec && !used; j++)
                used = s->owner[j] == i;
            if (!used)
                v = i;
        }
        if (v < 0 && s->nver < NBS_MAX_VER)
            v = s->nver++;
        if (v < 0)
            w->err = NBS_E_FULL; /* (written, but found at the next mount) */
    }
    for (int i = 0; i < w->nsec; i++)
        s->owner[w->sec[i]] = (int16_t)(w->err ? -1 : v);
    if (w->err)
        return w->err;
    for (int i = 0; i < w->nsec; i++)
        s->cidx[w->sec[i]] = (uint16_t)i;
    nbs_ver_t *ver = &s->ver[v];
    memset(ver, 0, sizeof *ver);
    ver->seq = w->seq, ver->size = w->size, ver->nchunks = (uint16_t)w->nsec, ver->last = (int16_t)(w->nsec - 1);
    ver->live = true;
    memcpy(ver->name, w->name, NBS_NAME);
    s->max_seq = w->seq;
    return NBS_OK;
}

/* ---------------------------------------------------------------- delete, copy */
int nbs_delete(nbs_t *s, const char *name)
{
    int found = 0;
    for (;;) {
        int oldest = -1;
        for (int v = 0; v < s->nver; v++)
            if (s->ver[v].live && same(s->ver[v].name, name) && (oldest < 0 || s->ver[v].seq < s->ver[oldest].seq))
                oldest = v;
        if (oldest < 0)
            return found ? NBS_OK : NBS_E_NOTFOUND;
        int i = sector_of(s, oldest, (uint32_t)s->ver[oldest].last);
        static const uint32_t zero = 0;
        if (i < 0 || dev_write(s, (uint32_t)i * NBS_SECTOR, &zero, sizeof zero))
            return NBS_E_IO;
        s->ver[oldest].live = false;
        for (uint32_t j = 0; j < s->nsec; j++)
            if (s->owner[j] == oldest)
                s->owner[j] = -1;
        found++;
    }
}

int nbs_copy(nbs_t *s, const char *from, int which, const char *to, nbs_wr_t *w)
{
    nbs_rd_t r;
    int e = nbs_open(s, from, which, &r);
    if (e)
        return e;
    s->keep = r.v; /* (its sectors are not reclaimed for the copy of itself) */
    if ((e = nbs_create(s, to, s->ver[r.v].size, w)) == NBS_OK) {
        uint8_t b[512];
        for (int k; (k = nbs_read(&r, b, sizeof b)) > 0;)
            nbs_put(w, b, (size_t)k);
        if (r.err && !w->err)
            w->err = r.err;
        e = nbs_close(w);
    }
    s->keep = -1;
    return e;
}

int nbs_sectors(nbs_t *s, const char *name, uint32_t *off, int max, uint32_t *size)
{
    int v = nbs_find(s, name, 0);
    if (v < 0)
        return v;
    int n = (int)chunks_for(s->ver[v].size);
    if (n > max)
        return NBS_E_SIZE;
    for (int k = 0; k < n; k++) {
        int i = sector_of(s, v, (uint32_t)k);
        if (i < 0)
            return NBS_E_IO;
        off[k] = (uint32_t)i * NBS_SECTOR;
    }
    s->ver[v].pinned = true;
    *size = s->ver[v].size;
    return n;
}

void nbs_unpin(nbs_t *s)
{
    for (int v = 0; v < s->nver; v++)
        s->ver[v].pinned = false;
}
