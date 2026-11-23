/*
 * microSD tools: screenshots (PNG) and session backup / restore.
 *
 * UI. APPS > microSD tools opens the menu "MICROSD": Screenshot (of the
 * screen the menu was opened from), Backup session, Restore session...,
 * Card info. Restore asks No/Yes first (it replaces everything). Card
 * errors say what to do (sdbk_card_error). ALPHA then MENU takes a screenshot from any screen (app.c,
 * sdbk_hotkey) without opening anything. Files go to the folder /CALC:
 * SHOT001.PNG, SHOT002.PNG, ... (first free number) and BACKUP.SES (an
 * existing backup is replaced only when the new one is complete, see fat.h).
 * Console: `sdtool shot`, `sdtool backup [FILE]`,
 * `sdtool restore [FILE]` (row in
 * app/app_console.c).
 *
 * PNG writer (W3C PNG 3rd ed.; RFC 1950 zlib, RFC 1951 deflate): 320 x 240,
 * 8-bit RGB (colour type 2) from the RGB565 framebuffer (gfx_framebuffer(),
 * i.e. what the panel shows, dark mode included; 5/6-bit channels widened
 * by bit replication). Each row is filtered with Sub or Up, whichever leaves
 * more zero bytes; the zlib stream is one fixed-Huffman deflate block that
 * codes runs of a repeated byte as matches at distance 1 (after filtering,
 * flat areas are runs of zeros), so a typical screen takes 5-20 KB instead of
 * 225 KB, with ~2 KB of stack and no tables. The stream is written in IDAT
 * chunks of up to SHOT_CHUNK bytes as it is produced (no frame-sized buffer).
 * CRCs: crc32_update (core/rle.c, the PNG/zlib CRC-32); Adler-32 inline.
 *
 * Backup file (BACKUP.SES): "CALCBAK1", u32 payload length, u32 CRC-32 of
 * the payload (little-endian), then the session payload exactly as saved to
 * flash (session_snapshot: every persist_registry blob, the CAS variables
 * included). Restore reads it into the session buffer, checks length, CRC
 * and frame structure, installs it as the flash snapshot (session_install)
 * and restarts the app (app_resume), which loads it like after a power cycle.
 * A failed check changes nothing.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "fat.h"
#include "gfx.h"
#include "registry.h"
#include "rle.h" /* crc32_update */
#include "console.h"
#include "sdbk.h"
#include "session.h"
#include "ui.h"
#include "usb_app.h"

#define SHOT_CHUNK 1024
#define W LCD_WIDTH
#define H LCD_HEIGHT

/* ------------------------------------------------------------ card errors */
/* "microSD: no card." plus what to do about it ("Insert a FAT32 card.") for
 * the errors a user can fix (the message box wraps it at words). */
void sdbk_card_error(int err, char *msg, size_t n)
{
    static const char *const fix[] = {
        NULL, " Insert a FAT32 card.", " Reinsert it and try again.", " Format it as FAT32.", NULL,
        " Use a name like DATA.CSV.", " Delete files to make room.",
    };
    const char *f = err < 0 && -err < (int)(sizeof fix / sizeof fix[0]) ? fix[-err] : NULL;
    snprintf(msg, n, "microSD: %s.%s", fs_strerror(err), f ? f : "");
}

/* ------------------------------------------------------------ PNG writer */
typedef struct {
    fs_file_t f;
    uint8_t out[SHOT_CHUNK]; /* compressed bytes of the current IDAT chunk */
    int n;
    uint32_t bits, nbits;   /* deflate bit buffer (LSB first) */
    uint32_t a1, a2;        /* Adler-32 halves */
    int err;
    int prev, run;          /* run-length state: last byte (-1 none), pending repeats */
} png_t;

static void wr(png_t *p, const void *d, size_t n)
{
    if (!p->err && fs_write(&p->f, d, n) != (int)n)
        p->err = FS_EIO;
}
static void be32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)(v >> 24), b[1] = (uint8_t)(v >> 16), b[2] = (uint8_t)(v >> 8), b[3] = (uint8_t)v; }

/* One PNG chunk: length, type, data, CRC over type + data. */
static void chunk(png_t *p, const char *type, const uint8_t *d, size_t n)
{
    uint8_t h[8];
    be32(h, (uint32_t)n);
    memcpy(h + 4, type, 4);
    uint32_t crc = crc32_update(crc32_update(0, type, 4), d, n);
    wr(p, h, 8);
    wr(p, d, n);
    be32(h, crc);
    wr(p, h, 4);
}

static void out_byte(png_t *p, uint8_t b)
{
    p->out[p->n++] = b;
    if (p->n == SHOT_CHUNK) {
        chunk(p, "IDAT", p->out, (size_t)p->n);
        p->n = 0;
    }
}

/* Append n bits of v (LSB first, the deflate bit order). */
static void bits(png_t *p, uint32_t v, int n)
{
    p->bits |= v << p->nbits;
    p->nbits += (uint32_t)n;
    while (p->nbits >= 8) {
        out_byte(p, (uint8_t)p->bits);
        p->bits >>= 8;
        p->nbits -= 8;
    }
}

/* A Huffman code of n bits, sent most significant bit first. */
static void huff(png_t *p, uint32_t code, int n)
{
    uint32_t r = 0;
    for (int i = 0; i < n; i++)
        r |= ((code >> i) & 1u) << (n - 1 - i);
    bits(p, r, n);
}

/* Fixed Huffman literal/length symbol (RFC 1951, 3.2.6). */
static void sym(png_t *p, int s)
{
    if (s < 144)
        huff(p, 0x30u + (uint32_t)s, 8);
    else if (s < 256)
        huff(p, 0x190u + (uint32_t)(s - 144), 9);
    else if (s < 280)
        huff(p, (uint32_t)(s - 256), 7);
    else
        huff(p, 0xC0u + (uint32_t)(s - 280), 8);
}

/* A match of length len (3..258) at distance 1: length symbol + extra
 * bits, then distance code 0 (5 bits, no extra). */
static void match(png_t *p, int len)
{
    static const uint16_t base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    int i = 28;
    while (base[i] > len)
        i--;
    sym(p, 257 + i);
    if (extra[i])
        bits(p, (uint32_t)(len - base[i]), extra[i]);
    huff(p, 0, 5);
}

static void flush_run(png_t *p)
{
    if (p->run >= 3)
        match(p, p->run);
    else
        for (int i = 0; i < p->run; i++)
            sym(p, p->prev);
    p->run = 0;
}

/* One byte of the uncompressed (filtered) image stream. */
static void zput(png_t *p, uint8_t b)
{
    p->a1 = (p->a1 + b) % 65521u;
    p->a2 = (p->a2 + p->a1) % 65521u;
    if (b == p->prev) {
        if (++p->run == 258)
            flush_run(p);
        return;
    }
    flush_run(p);
    sym(p, b);
    p->prev = b;
}

/* RGB888 of framebuffer row y into rgb[W*3]. */
static void row_rgb(const uint16_t *fb, int y, uint8_t *rgb)
{
    for (int x = 0; x < W; x++) {
        uint16_t c = fb[y * W + x];
        unsigned r = (c >> 11) & 31, gr = (c >> 5) & 63, b = c & 31;
        rgb[3 * x] = (uint8_t)(r << 3 | r >> 2);
        rgb[3 * x + 1] = (uint8_t)(gr << 2 | gr >> 4);
        rgb[3 * x + 2] = (uint8_t)(b << 3 | b >> 2);
    }
}

/* Write the framebuffer as a PNG to path. 0 or an FS_E* code. */
static int write_png(const char *path)
{
    png_t p; /* ~1 KB on the (DTCM) stack: no static RAM */
    memset(&p, 0, sizeof p);
    p.a1 = 1;
    p.prev = -1;
    int r = fs_open(&p.f, path, FS_WRITE);
    if (r)
        return r;
    const uint16_t *fb = gfx_framebuffer();
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    wr(&p, sig, 8);
    uint8_t ih[13] = {0};
    be32(ih, W);
    be32(ih + 4, H);
    ih[8] = 8; /* bit depth */
    ih[9] = 2; /* RGB */
    chunk(&p, "IHDR", ih, 13);
    out_byte(&p, 0x78); /* zlib: deflate, 32 KB window */
    out_byte(&p, 0x01); /* no dictionary, fastest; (0x7801 % 31 == 0) */
    bits(&p, 1, 1);     /* BFINAL */
    bits(&p, 1, 2);     /* BTYPE = 01 fixed Huffman */
    uint8_t cur[W * 3], prev[W * 3];
    memset(prev, 0, sizeof prev);
    for (int y = 0; y < H && !p.err; y++) {
        row_rgb(fb, y, cur);
        int zsub = 0, zup = 0;
        for (int i = 0; i < W * 3; i++) {
            zsub += (uint8_t)(cur[i] - (i >= 3 ? cur[i - 3] : 0)) == 0;
            zup += cur[i] == prev[i];
        }
        bool up = zup > zsub;
        zput(&p, up ? 2 : 1);
        for (int i = 0; i < W * 3; i++)
            zput(&p, (uint8_t)(cur[i] - (up ? prev[i] : i >= 3 ? cur[i - 3] : 0)));
        memcpy(prev, cur, sizeof prev);
    }
    flush_run(&p);
    sym(&p, 256);
    if (p.nbits)
        bits(&p, 0, 8 - (int)p.nbits);
    uint32_t ad = p.a2 << 16 | p.a1;
    out_byte(&p, (uint8_t)(ad >> 24));
    out_byte(&p, (uint8_t)(ad >> 16));
    out_byte(&p, (uint8_t)(ad >> 8));
    out_byte(&p, (uint8_t)ad);
    if (p.n)
        chunk(&p, "IDAT", p.out, (size_t)p.n);
    chunk(&p, "IEND", NULL, 0);
    if (p.err) {
        fs_abort(&p.f);
        return p.err;
    }
    return fs_close(&p.f);
}

/* ------------------------------------------------------------ files */
static int exists_cb(const fs_dirent_t *e, void *user)
{
    int *maxn = user, n = 0;
    if (!strncmp(e->short_name, "SHOT", 4) && strstr(e->short_name, ".PNG")) {
        for (const char *s = e->short_name + 4; *s >= '0' && *s <= '9'; s++)
            n = n * 10 + (*s - '0');
        if (n > *maxn)
            *maxn = n;
    }
    return 0;
}

static char last_file[40]; /* for `state sd_last` */

/* Save the current screen as /CALC/SHOTnnn.PNG (one more than the highest
 * number there). msg gets "saved /CALC/SHOT001.PNG" or the error. */
int sdbk_screenshot(char *msg, size_t n)
{
    int r = fs_mkdir("/CALC"), maxn = 0;
    if (!r)
        r = fs_list("/CALC", exists_cb, &maxn);
    char path[32];
    snprintf(path, sizeof path, "/CALC/SHOT%03d.PNG", maxn + 1 > 999 ? 999 : maxn + 1);
    if (!r)
        r = write_png(path);
    fs_unmount();
    if (r)
        sdbk_card_error(r, msg, n);
    else
        snprintf(msg, n, "Saved %s.", path), snprintf(last_file, sizeof last_file, "%s", path);
    return r;
}

/* Write the session to path (default /CALC/BACKUP.SES). */
int sdbk_backup(const char *path, char *msg, size_t n)
{
    size_t len = 0;
    const uint8_t *s = session_snapshot(&len);
    if (!s) {
        snprintf(msg, n, "No session buffer.");
        return -1;
    }
    uint8_t h[16] = "CALCBAK1";
    uint32_t crc = crc32_update(0, s, len), l = (uint32_t)len;
    memcpy(h + 8, &l, 4);  /* little-endian targets only (ARM, x86, wasm) */
    memcpy(h + 12, &crc, 4);
    fs_file_t f;
    int r = fs_mkdir("/CALC");
    if (!r)
        r = fs_open(&f, path, FS_WRITE);
    if (!r && (fs_write(&f, h, 16) != 16 || fs_write(&f, s, len) != (int)len))
        r = FS_EIO; /* fs_write dropped the file */
    if (!r)
        r = fs_close(&f);
    fs_unmount();
    if (r)
        sdbk_card_error(r, msg, n);
    else
        snprintf(msg, n, "Saved %s (%lu bytes).", path, (unsigned long)len), snprintf(last_file, sizeof last_file, "%s", path);
    return r;
}

/* Read a backup into the session buffer, check it and install it; on
 * success the app restarts from it (the caller's screen state is gone). */
int sdbk_restore(const char *path, char *msg, size_t n)
{
    size_t cap;
    uint8_t *buf = session_buffer(&cap);
    fs_file_t f;
    uint8_t h[16];
    int r = buf ? fs_open(&f, path, FS_READ) : -1;
    if (r) {
        snprintf(msg, n, "%s: %s.", path, r == -1 ? "no session buffer" : fs_strerror(r));
        fs_unmount();
        return r;
    }
    uint32_t len = 0, crc = 0;
    int got = fs_read(&f, h, 16);
    memcpy(&len, h + 8, 4);
    memcpy(&crc, h + 12, 4);
    if (got != 16 || memcmp(h, "CALCBAK1", 8) || len > cap) {
        fs_close(&f);
        fs_unmount();
        snprintf(msg, n, "%s is not a session backup.", path);
        return FS_EINVAL;
    }
    size_t o = 0;
    while (o < len) {
        int k = fs_read(&f, buf + o, len - o > 4096 ? 4096 : len - o);
        if (k <= 0)
            break;
        o += (size_t)k;
    }
    fs_close(&f);
    fs_unmount();
    if (o != len || crc32_update(0, buf, len) != crc) {
        snprintf(msg, n, "%s is damaged (length or CRC).", path);
        session_save(true); /* the buffer was overwritten: put the current session back */
        return FS_EIO;
    }
    r = session_install(len);
    if (r) {
        snprintf(msg, n, r == -1 ? "No flash to restore into." : "%s holds bad session data.", path);
        session_save(true);
        return r;
    }
    app_resume();
    snprintf(msg, n, "Session restored from %s.", path);
    return 0;
}

/* ALPHA MENU (app.c): screenshot of the current screen, result on the
 * status line of a message box that the next key closes. */
void sdbk_hotkey(void)
{
    static char m[64];
    sdbk_screenshot(m, sizeof m);
    ui_message("SCREENSHOT", m);
}

/* ------------------------------------------------------------ menu */
enum { S_SHOT = 1, S_BACKUP, S_RESTORE, S_INFO, S_USB };
static char files[16][16];
static int nfiles;
static int ses_cb(const fs_dirent_t *e, void *user)
{
    const char *dot = strrchr(e->short_name, '.');
    if (!e->is_dir && dot && !strcmp(dot, ".SES") && nfiles < 16)
        snprintf(files[nfiles++], sizeof files[0], "%s", e->short_name);
    return 0;
}
static int rf_count(void) { return nfiles; }
static void rf_get(int i, menu_item_t *out) { *out = (menu_item_t){files[i], NULL, NULL, i, "sdtools"}; }
/* Restoring replaces everything (settings, history, variables, lists, the
 * sheet ...), so the chosen file is confirmed first; No is the first item. */
static int rf_chosen;
static char rf_title[40];
static void rf_confirm(const menu_item_t *it)
{
    static char m[96];
    if (!it->action) { /* No: back to the file list */
        menu_close();
        return;
    }
    char path[32];
    snprintf(path, sizeof path, "/CALC/%s", files[rf_chosen]);
    int r = sdbk_restore(path, m, sizeof m);
    ui_message(r ? "RESTORE FAILED" : "RESTORED", m);
}
static const menu_item_t rf_yesno[] = {
    {"No, keep the current session", NULL, NULL, 0, "sdtools"},
    {"Yes, replace everything", NULL, NULL, 1, "sdtools"},
};
static const menu_def_t menu_rf_confirm = {rf_title, rf_yesno, 2, rf_confirm, false, "sdtools"};
static void rf_select(const menu_item_t *it)
{
    rf_chosen = it->action;
    snprintf(rf_title, sizeof rf_title, "Restore %s?", files[rf_chosen]);
    menu_open(&menu_rf_confirm);
}
static const menu_def_t menu_restore = {"RESTORE SESSION", NULL, 0, rf_select, false, "sdtools", rf_count, rf_get};

static const menu_item_t sd_items[] = {
    {"Screenshot (PNG)", NULL, NULL, S_SHOT, "sdtools"},
    {"Backup session", NULL, NULL, S_BACKUP, "sdtools"},
    {"Restore session...", NULL, NULL, S_RESTORE, "sdtools"},
    {"Card info", NULL, NULL, S_INFO, "sdtools"},
    {"Connect card to computer", NULL, NULL, S_USB, "usb"},
};

static void sd_select(const menu_item_t *it)
{
    static char m[96];
    switch (it->action) {
    case S_SHOT: {
        /* back to the screen below the menu, drawn, then capture it */
        const screen_t *s = menu_origin() ? menu_origin() : ui_home();
        ui_returning = true;
        ui_switch(s);
        ui_returning = false;
        int r = sdbk_screenshot(m, sizeof m);
        ui_message(r ? "SCREENSHOT FAILED" : "SCREENSHOT", m);
        return;
    }
    case S_BACKUP: {
        int r = sdbk_backup("/CALC/BACKUP.SES", m, sizeof m);
        ui_message(r ? "BACKUP FAILED" : "BACKUP", m);
        return;
    }
    case S_RESTORE: {
        nfiles = 0;
        int r = fs_list("/CALC", ses_cb, NULL);
        fs_unmount();
        if (r || !nfiles) {
            if (r)
                sdbk_card_error(r, m, sizeof m);
            else
                snprintf(m, sizeof m, "No .SES backups in /CALC. Make one with Backup.");
            ui_message("RESTORE", m);
            return;
        }
        menu_open(&menu_restore);
        return;
    }
    case S_USB:
        usb_app_card_open(); /* the card as a drive of the computer (app/usb_app.c) */
        return;
    case S_INFO: {
        uint32_t blocks, free_kb;
        int type;
        int r = fs_info(&blocks, &free_kb, &type);
        fs_unmount();
        if (r)
            sdbk_card_error(r, m, sizeof m);
        else
            snprintf(m, sizeof m, "FAT%d, %lu MB, %lu MB free.", type, (unsigned long)(blocks / 2048u),
                     (unsigned long)(free_kb / 1024u));
        ui_message("MICROSD", m);
        return;
    }
    }
}

static const menu_def_t menu_sd = {"MICROSD", sd_items, sizeof sd_items / sizeof sd_items[0], sd_select, false,
                                   "sdtools"};
static void sd_open(void) { menu_open(&menu_sd); }
const app_def_t app_sdtools = {"sdtools", "microSD tools", sd_open, {{0}}, "sdtools"};

/* Console `sdtool shot | backup [FILE] | restore [FILE]`: prints the
 * result line and keeps it for `state sd_msg` (a failed operation is not a
 * console error, so tests can check refusals). */
static char last_msg[96];
int sdbk_console(int argc, char **argv)
{
    char *m = last_msg;
    const char *sub = argc > 1 ? argv[1] : "";
    int r;
    if (!strcmp(sub, "shot"))
        r = sdbk_screenshot(m, sizeof last_msg);
    else if (!strcmp(sub, "backup"))
        r = sdbk_backup(argc > 2 ? argv[2] : "/CALC/BACKUP.SES", m, sizeof last_msg);
    else if (!strcmp(sub, "restore"))
        r = sdbk_restore(argc > 2 ? argv[2] : "/CALC/BACKUP.SES", m, sizeof last_msg);
    else
        return con_error("usage: sdtool shot | backup [FILE] | restore [FILE]");
    con_printf("%s\nrc=%d\n", m, r);
    return 0;
}

void sdbk_state(void (*kv)(const char *, const char *, void *), void *user)
{
    kv("sd_last", last_file, user);
    kv("sd_msg", last_msg, user);
}
