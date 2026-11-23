/*
 * Math Quest: theme packs (.THM files in /CALC/THEMES on the microSD card).
 *
 * A theme pack replaces the look and the names of the cast (and optionally
 * the colours of each world) without changing the game. Built with
 * tools/make_theme.py from a PNG sprite sheet and a JSON manifest; the format
 * is documented in docs/MATHQUEST.md ("Theme packs"). All numbers are
 * little-endian.
 *
 *   header (64 bytes)
 *     0  "MQTH"        4  u16 version (1, or 2 with home items)   6  u16 header size (64)
 *     8  char title[24]
 *    32  u8 cell width (1..48)   33 u8 cell height (1..48; older modules: 32)
 *    34  u8 sprites (1..160; version 2: 0..160) 35 u8 flags (bit 0: world skins present)
 *    36  u16 colours (1..256; index 0 is transparent)   38 u16 home items (version 2; 0 in version 1)
 *    40  u32 payload bytes       44 u32 CRC-32 (zlib) of the payload
 *    48  16 bytes reserved (0)
 *   payload
 *     palette   colours x u16 RGB565
 *     sprites   sprites x {char name[12]; u8 slot; u8 anim; u8 frame; u8 0}   slot = cast slot 0..8,
 *               255 = unused; anim 0 idle, 1 happy, 2 think, 3 dance, 4 pout, 5 fly,
 *               6 talk, 7 wow, 8 look, 9 blink; frame 0.. (both 0 in packs
 *               made before animations: an old pack is all idle frame 0, and an old module
 *               shows the first sprite of each slot, so animated packs stay version 1)
 *     skins     (flag) 8 worlds x {u16 background, accent, ground} RGB565, 0 = keep the built-in
 *     pixels    sprites x width x height palette indices, row by row
 *     traits    (after the items; version 3, flag bit 1) u8 n, then n x {u8 slot; u8 pronoun (1 she, 2 he, 3 they);
 *               char food[16], colour[16], hobby[16], thing[16]}: the characters in the stories
 *     variants  (after the traits; version 3, flag bit 2) u16 n, then n x {u8 sprite; u8 scale; u16 w, h; w x h u16
 *               RGB565, 0x0001 transparent}: the same sprites at another display scale (a 1x
 *               display reads past them)
 *     items     (version 2) items x {u16 item id; u8 width, height (1..64)}, then their
 *               pixels (width x height palette indices each, row by row): pictures that
 *               replace built-in home items (ids of tools/gen_mq_items.py, mq_items.h);
 *               ids this module does not know are skipped
 *
 * Version 1 files load unchanged; a pack without home items is still written as
 * version 1 by tools/make_theme.py, so older modules keep reading it.
 * A file that is missing, damaged (CRC), of another version or out of range
 * is refused as a whole and the built-in cast stays (the settings say why).
 * The sprites are kept run-length coded in mq_t.theme (cells up to 48 x 48; 48 KB coded) and
 * decoded a few at a time when drawn (mq_theme_px).
 */
#include "mq.h"

static uint32_t crc32_upd(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* read exactly n bytes (and fold them into the CRC); -1 on a short file */
static int rd(mq_t *q, void *buf, size_t n, uint32_t *crc)
{
    uint8_t *b = buf;
    size_t got = 0;
    while (got < n) {
        int r = q->api->file_read(b + got, n - got);
        if (r <= 0)
            return -1;
        got += (size_t)r;
    }
    if (crc)
        *crc = crc32_upd(*crc, b, n);
    return 0;
}

static int load(mq_t *q, const char *file, mq_theme_t *t)
{
    char path[40];
    q->api->snprintf(path, sizeof path, "/CALC/THEMES/%s", file);
    uint32_t size = 0;
    if (q->api->file_open(path, &size))
        return -1;
    uint8_t h[64];
    int err = -2;
    if (rd(q, h, sizeof h, NULL))
        goto done;
    int ver = rd16(h + 4);
    t->nitem = 0;
    if (h[0] != 'M' || h[1] != 'Q' || h[2] != 'T' || h[3] != 'H' || ver < 1 || ver > 3 || rd16(h + 6) != 64)
        goto done;
    t->cw = h[32], t->ch = h[33], t->nspr = h[34];
    t->has_skin = h[35] & 1;
    t->ncol = rd16(h + 36);
    int nitem = ver >= 2 ? rd16(h + 38) : 0;
    t->has_traits = false, t->nvariant = 0;
    for (int k = 0; k < MQ_NCAST; k++) {
        t->pron[k] = 0;
        for (int j = 0; j < TR_COUNT; j++)
            t->trait[k][j][0] = 0;
    }
    uint32_t plen = rd32(h + 40), want = rd32(h + 44), crc = 0;
    if (!t->cw || t->cw > THM_MAXCELL || !t->ch || t->ch > THM_MAXCELL || t->nspr > THM_MAXSPR ||
        (!t->nspr && !nitem) || nitem > THM_MAXITEM || !t->ncol || t->ncol > 256 || plen + 64 != size)
        goto done;
    for (int i = 0; i < 24; i++)
        t->title[i] = (char)h[8 + i];
    t->title[23] = 0;
    uint8_t b[16];
    for (int i = 0; i < t->ncol; i++) {
        if (rd(q, b, 2, &crc))
            goto done;
        t->pal[i] = rd16(b);
    }
    for (int i = 0; i < t->nspr; i++) {
        if (rd(q, b, 16, &crc))
            goto done;
        for (int k = 0; k < 12; k++)
            t->names[i][k] = (char)b[k];
        t->names[i][11] = 0;
        t->role[i] = b[12];
        t->anim[i] = b[13] < AN_COUNT ? b[13] : AN_IDLE, t->frame[i] = b[14];
    }
    for (int w = 0; w < THM_NWORLD; w++)
        for (int k = 0; k < 3; k++) {
            t->skin[w][k] = 0;
            if (t->has_skin) {
                if (rd(q, b, 2, &crc))
                    goto done;
                t->skin[w][k] = rd16(b);
            }
        }
    /* the sprites, run-length coded into the pool as they are read (a cell at a time) */
    uint32_t pool = 0;
    for (int i = 0; i < t->nspr; i++) {
        uint8_t *px = q->art_buf[0];
        int n = t->cw * t->ch;
        if (rd(q, px, (size_t)n, &crc))
            goto done;
        if (pool + 4 > THM_PXPOOL)
            goto done;
        t->off[i] = pool;
        uint8_t *o = t->px + pool;
        o[0] = 0, o[1] = 0, o[2] = t->cw, o[3] = t->ch;
        pool += 4;
        for (int k = 0; k < n;) {
            if (px[k] >= t->ncol || pool + 2 > THM_PXPOOL)
                goto done;
            int j = k;
            while (j < n && px[j] == px[k] && j - k < 255)
                j++;
            t->px[pool++] = (uint8_t)(j - k), t->px[pool++] = px[k];
            k = j;
        }
    }
    for (int i = 0; i < 4; i++)
        q->art_key[i] = 0;
    /* version 2: home item pictures */
    uint8_t it[THM_MAXITEM][4];
    uint32_t off = 0;
    for (int i = 0; i < nitem; i++) {
        if (rd(q, it[i], 4, &crc))
            goto done;
        int w = it[i][2], hh = it[i][3];
        if (!w || w > THM_ITEMMAX || !hh || hh > THM_ITEMMAX)
            goto done;
        off += (uint32_t)(w * hh);
    }
    if (off > THM_ITEMPX)
        goto done;
    off = 0;
    for (int i = 0; i < nitem; i++) {
        int id = rd16(it[i]), w = it[i][2], hh = it[i][3];
        if (rd(q, t->item_px + off, (size_t)(w * hh), &crc))
            goto done;
        for (int k = 0; k < w * hh; k++)
            if (t->item_px[off + k] >= t->ncol)
                goto done;
        if (id < 1 || id > MQ_NITEM || !mq_item[id].aw) /* unknown here (a newer catalogue) or not drawable */
            continue;
        t->item_id[t->nitem] = (uint8_t)id, t->item_w[t->nitem] = (uint8_t)w, t->item_h[t->nitem] = (uint8_t)hh;
        t->item_off[t->nitem++] = (uint16_t)off;
        off += (uint32_t)(w * hh);
    }
    /* version 3: the characters' traits (flag bit 1), sprites at other scales (flag bit 2) */
    if (ver >= 3 && (h[35] & 2)) {
        uint8_t n;
        if (rd(q, &n, 1, &crc))
            goto done;
        for (int i = 0; i < n; i++) {
            uint8_t e[2 + TR_COUNT * 16];
            if (rd(q, e, sizeof e, &crc))
                goto done;
            if (e[0] >= MQ_NCAST)
                continue;
            t->pron[e[0]] = e[1] <= PR_THEY ? e[1] : 0;
            for (int j = 0; j < TR_COUNT; j++) {
                for (int k = 0; k < 15; k++) {
                    char c = (char)e[2 + j * 16 + k];
                    t->trait[e[0]][j][k] = c >= ' ' && c < 127 ? c : 0;
                }
                t->trait[e[0]][j][15] = 0;
            }
            t->has_traits = true;
        }
    }
    if (ver >= 3 && (h[35] & 4)) { /* a display at 1x draws the 1x sprites: the variants are read past */
        uint8_t c[2];
        if (rd(q, c, 2, &crc))
            goto done;
        int n = rd16(c);
        for (int i = 0; i < n; i++) {
            uint8_t e[6];
            if (rd(q, e, 6, &crc))
                goto done;
            uint32_t left = (uint32_t)rd16(e + 2) * rd16(e + 4) * 2u;
            uint8_t buf[64];
            while (left) {
                uint32_t k = left > sizeof buf ? sizeof buf : left;
                if (rd(q, buf, k, &crc))
                    goto done;
                left -= k;
            }
        }
        t->nvariant = (uint16_t)n;
    }
    err = crc == want ? 0 : -3;
    if (err)
        t->nitem = 0, t->has_traits = false;
done:
    q->api->file_close(false);
    return err;
}

/* Load theme `file` ("" = the built-in cast). 0 = loaded; <0: not loaded
 * (the built-in cast is used): -1 no such file / no card, -2 not a theme
 * pack of this version, -3 damaged. */
int mq_theme_load(mq_t *q, const char *file)
{
    q->theme.loaded = false;
    if (!file || !file[0])
        return 0;
    int r = load(q, file, &q->theme);
    q->theme.loaded = r == 0;
    return r;
}

static int scan_cb(const char *name, uint32_t size, void *user)
{
    mq_t *q = user;
    int n = 0;
    while (name[n])
        n++;
    if (n < 5 || n > 12 || q->ntheme_files >= 6)
        return 0;
    const char *e = name + n - 4;
    if (e[0] != '.' || (e[1] | 32) != 't' || (e[2] | 32) != 'h' || (e[3] | 32) != 'm')
        return 0;
    for (int i = 0; i <= n; i++)
        q->theme_files[q->ntheme_files][i] = name[i];
    q->ntheme_files++;
    (void)size;
    return 0;
}

/* The theme packs on the card (up to 6 names into q->theme_files). */
void mq_theme_scan(mq_t *q)
{
    q->ntheme_files = 0;
    q->api->dir_list("/CALC/THEMES", scan_cb, q);
}
