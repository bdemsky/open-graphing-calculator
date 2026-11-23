/*
 * USB, the application's side (the device itself: hal.h hal_usb_*, the mass
 * storage units: core/usbdisk.h, the drive: core/vdrive.h).
 *
 * The CALC drive's files, generated at every media change (snapshots, except
 * SCREEN.BMP, which is converted from the framebuffer as it is read):
 *   README.TXT   what the drive is for
 *   INFO.TXT     firmware version and tier, battery, free module space, the
 *                installed modules with their versions, the event log (core/evlog.h)
 *   MODULES.TXT  the installed modules in detail (state, size, which build made them)
 *   RESULT.TXT   what happened to the last module file dropped on the drive
 *   SCREEN.BMP   the screen, 320 x 240, 24-bit
 *   NOTEBOOK.HTML    the home screen's notebook (named or not) as one static page
 *                (no scripts: phones show it too), built at every generation
 *                (app/nbhtml.c). The drive is generated again (a media change)
 *                when the notebook has changed since and the calculator has
 *                been left alone for HTML_IDLE_MS (no key, no touch, no cell
 *                running, the computer not reading the drive for
 *                HTML_HOST_QUIET_MS; at most every HTML_GAP_MS), at once when
 *                another notebook is opened, created or saved under a name,
 *                and never while the browser page follows the notebook (its
 *                serial link would drop with the device): after it.
 *   CALCULATOR.HTML  the browser notebook (web/notebook), the payload of the
 *                "webui" data module as it is, read from the QSPI as the
 *                computer reads it; only while that module is installed
 * All read-only; the free space is the staging buffer (STAGE_BYTES of SDRAM,
 * carved from the app arena at boot: the CAS keeps the rest, 27+ MB, well
 * above the 8-24 MB it is designed for). Without the SDRAM (the small
 * fallback arena) there is no CALC drive.
 *
 * Installing (usb_app_tick, main loop, never the USB interrupt): when the
 * drive has been quiet for a moment, vd_poll hands over a complete file.
 * A .MOD file is checked whole before anything is written: its header
 * (magic, format, header CRC, size = header + payload), the core interface
 * and ABI it needs, the payload's CRC-32. It is not installed while its
 * module is in use (its screen shown, or an animation playing): the
 * calculator waits until it is left. Then the store's installer
 * (modules_install_stream: the progress screen, the previous version kept,
 * the payload read back) writes it to the QSPI. A toast says what happened
 * ("Installed Geometry 0.1.0", or why not), RESULT.TXT keeps it, and the
 * drive goes through a media change (core/usbdisk.h) so the computer reads
 * the new directory: the dropped file is gone, MODULES.TXT and RESULT.TXT
 * are new. Other files are ignored (a toast says so once).
 *
 * microSD tools > Connect card to computer: the card becomes the second unit
 * (core/usbdisk.h LUN 1) while a full screen says so; the calculator's own
 * card use is locked out meanwhile (fs_lock: "microSD: connected to the
 * computer."). EXIT takes the card back (asking first if the computer has
 * not ejected it) and the next card use mounts it afresh.
 *
 * State: the statics below. Main loop only.
 */
#include "usb_app.h"
#include "nbdoc.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "calc.h"
#include "version.h"
#include "console.h"
#include "fat.h"
#include "gfx.h"
#include "hal.h"
#include "modstore.h"
#include "modules_ui.h"
#include "modx.h"
#include "py_core.h"
#include "sections.h"
#include "anim.h"
#include "evlog.h"
#include "geo_core.h"
#include "rle.h"
#include "theme.h"
#include "ui.h"
#include "usbdisk.h"

#define STAGE_BYTES (2u << 20) /* what the computer can drop at once */
#define INFO_MAX 3072
#define MODS_MAX 2048
#define RESULT_MAX 512
#define BMP_HDR 54u
#define BMP_ROW (LCD_WIDTH * 3u)
#define BMP_SIZE (BMP_HDR + BMP_ROW * LCD_HEIGHT)

/* the generated texts: in the arena block of usb_mem_init (internal RAM is full) */
static char *info_txt, *mods_txt, *result_txt;
static char last_result[96]; /* the toast of the last install (`state usb_result`) */
static bool host_was;
static vd_found_t pend;      /* a file being handled */
static bool have_pend, waiting_said;
static bool pend_checked;    /* pend passed check() (its header in pend_hdr) */
static mod_hdr_t pend_hdr;

#if defined(CONFIG_USB) || !defined(PLATFORM_STM32)
#define HAVE_DRIVE 1
#else
#define HAVE_DRIVE 0
#endif
#if HAVE_DRIVE && defined(CONFIG_PYTHON) && !defined(PLATFORM_WEB)
#define HAVE_NB 1
#else
#define HAVE_NB 0
#endif

/* ------------------------------------------------------------ notebooks
 * The NOTEBOOKS folder: at every generation the Python module lists the
 * stored notebooks (`nb usb`: NBIO_USB_FILE each), and the drive reads them
 * straight from their QSPI sectors (rd_nb). While module code runs (a cell,
 * a long program polling USB) it cannot be asked: the folder is generated
 * empty and again from the main loop (nb_stale). A notebook dropped on the
 * drive is handed to the module (`nb import NAME`), which reads it with
 * NBIO_USB_READ and reports with NBIO_USB_RESULT. */
#if HAVE_NB
#define NBX_MAX 64
typedef struct {
    char name[48];
    uint32_t size, first; /* first: index into nbx_off */
} nbx_t;
static nbx_t *nbx;          /* NBX_MAX entries (arena) */
static uint32_t *nbx_off;   /* sector offsets of all of them (arena) */
DTCM_BSS static int nbx_n, nbx_noff;
DTCM_BSS static vdrive_t *nb_gen;  /* the drive being generated */
DTCM_BSS static bool nb_stale, nb_changed, nb_result_set;
#define NBX_OFFS (NB_STORE_SIZE / NB_SECTOR)

static void rd_nb(void *user, uint32_t off, uint8_t *buf, uint32_t n)
{
    const nbx_t *e = user;
    while (n) {
        uint32_t k = off / NB_SECTOR_DATA, in = off % NB_SECTOR_DATA, m = NB_SECTOR_DATA - in < n ? NB_SECTOR_DATA - in : n;
        if (e->first + k >= NBX_OFFS || hal_storage_read(NB_STORE_BASE + nbx_off[e->first + k] + NB_SECTOR_HDR + in, buf, m))
            memset(buf, 0, m);
        buf += m, off += m, n -= m;
    }
}

static void nb_generate(vdrive_t *vd)
{
    vd_add_dir(vd, "NOTEBOOKS");
    nbx_n = nbx_noff = 0;
    nb_stale = nb_changed = false;
    if (!nbx || !usbdisk_host_present())
        return; /* (no computer: nothing to list; a connection generates the drive again) */
    nb_gen = vd;
    if (py_nb_call("usb", NULL) == -2)
        nb_stale = true;
    nb_gen = NULL;
}
#endif

/* NBIO_USB_* (app/py_core.h) */
static void result(bool ok, const char *file, const char *toast, const char *detail);
int usb_nb_io(int op, uint32_t a, void *p, size_t n)
{
#if HAVE_NB
    switch (op) {
    case NBIO_USB_HOST:
        return usbdisk_host_present();
    case NBIO_USB_READ: {
        vdrive_t *vd = usbdisk_calc();
        return vd && have_pend ? vd_file_read(vd, &pend, a, p, (uint32_t)n) : -1;
    }
    case NBIO_USB_FILE: {
        const nbio_usb_file_t *f = p;
        uint32_t k = (f->size + NB_SECTOR_DATA - 1) / NB_SECTOR_DATA;
        if (!nb_gen || nbx_n >= NBX_MAX || nbx_noff + k > NBX_OFFS)
            return -1;
        nbx_t *e = &nbx[nbx_n];
        snprintf(e->name, sizeof e->name, "%s", f->name);
        e->size = f->size, e->first = (uint32_t)nbx_noff;
        if (vd_add_long(nb_gen, 1, e->name, VD_ATTR_RO, f->size, rd_nb, e))
            return -1;
        memcpy(nbx_off + nbx_noff, f->off, k * sizeof *nbx_off);
        nbx_noff += (int)k, nbx_n++;
        return 0;
    }
    case NBIO_USB_RESULT: {
        const char *t = p, *nl = strchr(t, '\n');
        char toast[72];
        snprintf(toast, sizeof toast, "%.*s", nl ? (int)(nl - t) : (int)strlen(t), t);
        result(a != 0, have_pend ? pend.name : "", toast, nl ? nl + 1 : "");
        nb_result_set = true;
        return 0;
    }
    case NBIO_USB_REFRESH:
        nb_changed = true;
        return 0;
    }
#endif
    (void)op, (void)a, (void)p, (void)n;
    return -1;
}

static const char readme_txt[] =
    "CALC: the calculator's drive\r\n"
    "\r\n"
    "Drop .MOD files here to install them.\r\n"
    "\r\n"
    "NOTEBOOKS holds the calculator's notebooks (Jupyter .ipynb): copy them\r\n"
    "to the computer; drop .ipynb files here or there to import them.\r\n"
    "\r\n"
    "A module file is checked completely before it is written to the\r\n"
    "calculator; the version it replaces is kept, so a failed install never\r\n"
    "leaves you without one. The calculator shows what happened, RESULT.TXT\r\n"
    "keeps it, and the drive then refreshes itself (the computer may say it\r\n"
    "was not ejected properly: that is expected, nothing is lost).\r\n"
    "\r\n"
    "INFO.TXT     firmware version, battery, free module space, modules,\r\n"
    "             the last events (as Memory > About shows them)\r\n"
    "MODULES.TXT  the installed modules in detail\r\n"
    "RESULT.TXT   the result of the last install\r\n"
    "SCREEN.BMP   a picture of the calculator's screen\r\n"
    "NOTEBOOK.HTML  the open notebook as a page any phone or computer shows\r\n"
    "             (copy it anywhere; it changes when the drive is opened again)\r\n"
#if defined(CONFIG_CONTENT_QSPI)
    "CALCULATOR.HTML  use the calculator from the computer: open it in\r\n"
    "             Chrome or Edge (notebook, screen, keys)\r\n"
#endif
    "\r\n"
    "These files are read-only; nothing else you put here is kept.\r\n";

/* ------------------------------------------------------------ the files */

static void rd_text(void *user, uint32_t off, uint8_t *buf, uint32_t n) { memcpy(buf, (const char *)user + off, n); }

/* 24-bit BMP, bottom-up rows of BGR (BITMAPINFOHEADER), from the framebuffer
 * as it is now (RGB565 widened by bit replication, like the PNG writer). */
static void rd_bmp(void *user, uint32_t off, uint8_t *buf, uint32_t n)
{
    (void)user;
    const uint16_t *fb = gfx_framebuffer();
    for (uint32_t i = 0; i < n; i++, off++) {
        if (off < BMP_HDR) {
            uint8_t h[BMP_HDR] = {'B', 'M'};
            uint32_t v[] = {BMP_SIZE, 0, BMP_HDR, 40, LCD_WIDTH, LCD_HEIGHT, 1u | 24u << 16, 0, BMP_ROW * LCD_HEIGHT,
                            2835, 2835, 0, 0};
            for (int k = 0; k < 13; k++) /* little-endian words from offset 2 */
                for (int b = 0; b < 4; b++)
                    h[2 + 4 * k + b] = (uint8_t)(v[k] >> (8 * b));
            buf[i] = h[off];
            continue;
        }
        uint32_t p = off - BMP_HDR, row = p / BMP_ROW, col = p % BMP_ROW;
        uint16_t c = fb[(LCD_HEIGHT - 1 - row) * LCD_WIDTH + col / 3];
        unsigned cr = (c >> 11) & 31, cg = (c >> 5) & 63, cb = c & 31;
        switch (col % 3) {
        case 0: buf[i] = (uint8_t)(cb << 3 | cb >> 2); break;
        case 1: buf[i] = (uint8_t)(cg << 2 | cg >> 4); break;
        default: buf[i] = (uint8_t)(cr << 3 | cr >> 2); break;
        }
    }
}

#if defined(CONFIG_CONTENT_QSPI)
#define WEBUI_MODULE "webui"
static void rd_webui(void *user, uint32_t off, uint8_t *buf, uint32_t n)
{
    (void)user;
    if (mod_read(WEBUI_MODULE, off, buf, n))
        memset(buf, 0, n); /* damaged (the first read checks the CRC): zeros */
}
#endif

/* Appends to a text buffer (CRLF lines for every computer's text editor). */
typedef struct {
    char *b;
    size_t n, len;
} tb_t;
static void tb_add(tb_t *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void tb_add(tb_t *t, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(t->b + t->len, t->n - t->len, fmt, ap);
    va_end(ap);
    if (k > 0)
        t->len = t->len + (size_t)k < t->n ? t->len + (size_t)k : t->n - 1;
}

static const char *tier_name(void)
{
#if defined(CONFIG_CONTENT_INTERNAL)
    return "core";
#elif defined(CONFIG_CAS_EXT)
    return "advanced";
#else
    return "standard";
#endif
}

/* A module's name as the user knows it ("Geometry", "Manual"). */
static const char *mod_title(const char *name) { return !strcmp(name, "manual") ? "Manual" : modx_title(name); }

static void build_texts(void)
{
    hal_telemetry_t t;
    hal_telemetry(&t);
    tb_t a = {info_txt, INFO_MAX, 0};
    tb_add(&a, "Graphing calculator\r\n\r\nFirmware: %s (%s tier)\r\n", calc_version(), tier_name());
    if (t.vbat_v == t.vbat_v) /* not NAN */
        tb_add(&a, "Battery: %.2f V%s\r\n", (double)t.vbat_v, t.usb_present ? " (running from USB)" : "");
    else
        tb_add(&a, "Battery: not measured%s\r\n", t.usb_present ? " (running from USB)" : "");
    tb_t m = {mods_txt, MODS_MAX, 0};
    tb_add(&m, "Installed modules\r\n\r\n");
#if defined(CONFIG_CONTENT_QSPI)
    modstore_t *st = mod_store(MODSTORE_QSPI);
    if (st && st->dev)
        tb_add(&a, "Free module space: %lu KB of %lu KB\r\n",
               (unsigned long)((MODSTORE_QSPI_SIZE - (st->used < MODSTORE_QSPI_SIZE ? st->used : MODSTORE_QSPI_SIZE)) >> 10),
               (unsigned long)(MODSTORE_QSPI_SIZE >> 10));
    else
        tb_add(&a, "Free module space: the QSPI flash is not responding\r\n");
    const char *names[24];
    int n = mod_names(names, 24);
    tb_add(&a, "\r\nInstalled modules (%d):\r\n", n);
    for (int i = 0; i < n; i++) {
        const mod_hdr_t *h = mod_header(names[i]);
        mod_state_t s = mod_state(names[i]);
        char v[20] = "-";
        if (h)
            snprintf(v, sizeof v, "%u.%u.%u", h->ver[0], h->ver[1], h->ver[2]);
        tb_add(&a, "  %-12s %-8s %s\r\n", names[i], v, mod_state_name(s));
        tb_add(&m, "%s (%s)\r\n  version %s, %s\r\n", names[i], mod_title(names[i]), v, mod_state_text(s));
        if (h)
            tb_add(&m, "  %lu bytes, interface %.8s ABI %u-%u, built with %.24s\r\n",
                   (unsigned long)h->payload_len, h->iface, h->abi_min, h->abi_max, h->built_with);
    }
    if (!n)
        tb_add(&m, "(none)\r\n");
#else
    tb_add(&a, "Modules: this firmware (core tier) has no module store\r\n");
    tb_add(&m, "This firmware (core tier) has everything built in: no module store.\r\n");
#endif
    /* the event log (core/evlog.h), newest first, as MEM > About shows it */
    int ne = evlog_count();
    tb_add(&a, "\r\nLast events:%s\r\n", ne ? "" : " none");
    for (int i = ne - 1; i >= 0; i--) {
        char line[112];
        evlog_format(evlog_get(i), line, sizeof line);
        tb_add(&a, "  %s\r\n", line);
    }
    tb_add(&a, "\r\nDrop .MOD files on this drive to install them; RESULT.TXT says what happened.\r\n");
    if (!result_txt[0])
        snprintf(result_txt, RESULT_MAX, "No module file was dropped on this drive yet.\r\n");
}

/* NOTEBOOK.HTML: the notebook it was built from (its generation, name) and the last idle refresh */
#define HTML_IDLE_MS 10000u
#define HTML_HOST_QUIET_MS 2000u
#define HTML_GAP_MS 30000u
static uint32_t html_gen, html_at;
static bool html_open, html_idled;
static char html_name[NB_NAME];

/* The generator of the CALC drive (core/usbdisk.h): called at every media change. */
static void generate(vdrive_t *vd)
{
    if (nbd) { /* (NULL at the first generation: the document is made after the drive) */
        html_gen = nbd->gen, html_open = nbd->open;
        snprintf(html_name, sizeof html_name, "%s", nbd->name);
    }
    build_texts();
    /* the build date as the files' date: "Oct 28 2026" */
    static const char mon[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = fw_build_date;
    int mo = 1, day = (d[4] == ' ' ? 0 : d[4] - '0') * 10 + (d[5] - '0'), yr = 2000 + (d[9] - '0') * 10 + (d[10] - '0');
    for (int i = 0; i < 12; i++)
        if (!strncmp(d, mon + 3 * i, 3))
            mo = i + 1;
    vd_begin(vd, "CALC", 0x43414C43u, (uint16_t)((yr - 1980) << 9 | mo << 5 | day), 12u << 11);
    vd_add_file(vd, "README.TXT", VD_ATTR_RO, sizeof readme_txt - 1, rd_text, (void *)readme_txt);
    vd_add_file(vd, "INFO.TXT", VD_ATTR_RO, (uint32_t)strlen(info_txt), rd_text, info_txt);
    vd_add_file(vd, "MODULES.TXT", VD_ATTR_RO, (uint32_t)strlen(mods_txt), rd_text, mods_txt);
    vd_add_file(vd, "RESULT.TXT", VD_ATTR_RO, (uint32_t)strlen(result_txt), rd_text, result_txt);
    vd_add_file(vd, "SCREEN.BMP", VD_ATTR_RO, BMP_SIZE, rd_bmp, NULL);
    uint32_t nbh = nbhtml_build(); /* the open notebook as a page without scripts (app/nbhtml.c) */
    if (nbh)
        vd_add_long(vd, 0, "NOTEBOOK.HTML", VD_ATTR_RO, nbh, nbhtml_read, NULL);
#if defined(CONFIG_CONTENT_QSPI)
    const mod_hdr_t *web = mod_state(WEBUI_MODULE) == MOD_OK ? mod_header(WEBUI_MODULE) : NULL;
    if (web)
        vd_add_long(vd, 0, "CALCULATOR.HTML", VD_ATTR_RO, web->payload_len, rd_webui, NULL);
#endif
#if HAVE_NB
    nb_generate(vd);
#endif
}

/* Arena block: staging buffer, the drive's FAT / directory / bitmap, the texts. */
size_t usb_mem_init(void *mem, bool big)
{
    if (!big || !HAVE_DRIVE) {
        usbdisk_init(NULL, NULL, 0, generate);
        return 0;
    }
    uint8_t *m = (uint8_t *)(((uintptr_t)mem + 7) & ~(uintptr_t)7);
    size_t need = (vd_mem_need(STAGE_BYTES) + 7u) & ~(size_t)7u;
    info_txt = (char *)m + STAGE_BYTES + need;
    mods_txt = info_txt + INFO_MAX;
    result_txt = mods_txt + MODS_MAX;
    result_txt[0] = 0;
    size_t nb = 0;
#if HAVE_NB
    nbx_off = (uint32_t *)(void *)(result_txt + RESULT_MAX);
    nbx = (nbx_t *)(void *)(nbx_off + NBX_OFFS);
    nb = NBX_OFFS * sizeof *nbx_off + NBX_MAX * sizeof *nbx;
#endif
    usbdisk_init(m + STAGE_BYTES, m, STAGE_BYTES, generate);
    return (size_t)(m - (uint8_t *)mem) + STAGE_BYTES + need + INFO_MAX + MODS_MAX + RESULT_MAX + nb;
}

/* ------------------------------------------------------------ installing */

/* Is module `name` running now (its screen shown, under a menu or message
 * too, or an animation playing)? Then installing it waits. */
static bool in_use(const char *name)
{
    const screen_t *s = ui_current();
    if (s == &scr_menu || s == &scr_msg)
        s = ui_previous();
    if (!strcmp(name, GEO_MODULE))
        return s == &scr_geo;
    if (!strcmp(name, MQ_MODULE))
        return s && !strcmp(s->name, "mathquest");
    if (!strcmp(name, ANIM_MODULE))
        return anim_playing();
    if (!strcmp(name, "manual"))
        return s == &scr_help;
    return false;
}

static void result(bool ok, const char *file, const char *toast, const char *detail)
{
    snprintf(last_result, sizeof last_result, "%s", toast);
    snprintf(result_txt, RESULT_MAX, "%s\r\n%s\r\n\r\nFile: %s\r\n%s%s", toast,
             !ok ? "Nothing was installed."
             : pend.notebook ? "The notebook was imported."
                             : "The module was installed and verified; the previous version is kept.",
             file, detail, detail[0] ? "\r\n" : "");
    ui_toast(toast);
    con_log("usb: %s: %s", file, detail[0] ? detail : toast);
}

typedef struct {
    vdrive_t *vd;
    const vd_found_t *f;
    uint32_t at;
} stage_src_t;
static int rd_stage(void *user, void *buf, size_t n)
{
    stage_src_t *s = user;
    int k = vd_file_read(s->vd, s->f, s->at, buf, (uint32_t)n);
    if (k > 0)
        s->at += (uint32_t)k;
    return k;
}

/* Check the whole file; 0, or <0 with the reason in why (toast) and detail. */
static int check(vdrive_t *vd, const vd_found_t *f, mod_hdr_t *h, char *why, size_t wn, char *detail, size_t dn)
{
    detail[0] = 0;
    if (f->size < MOD_PAYLOAD_OFF || vd_file_read(vd, f, 0, h, sizeof *h) != (int)sizeof *h ||
        !mod_header_ok(h, f->size - MOD_PAYLOAD_OFF)) {
        snprintf(why, wn, "Not a module file: %.24s", f->name);
        snprintf(detail, dn, "This is not a module file (no valid module header).");
        return -1;
    }
    char v[24];
    snprintf(v, sizeof v, "%u.%u.%u", h->ver[0], h->ver[1], h->ver[2]);
    if (f->size != MOD_PAYLOAD_OFF + h->payload_len) {
        snprintf(why, wn, "%.16s %s: incomplete file", mod_title(h->name), v);
        snprintf(detail, dn, "Module %.16s %s: the file is %lu bytes, its header says %lu.", h->name, v,
                 (unsigned long)f->size, (unsigned long)(MOD_PAYLOAD_OFF + h->payload_len));
        return -1;
    }
#if defined(CONFIG_CONTENT_QSPI)
    int nifs;
    const mod_iface_t *ifs = mod_ifaces(&nifs);
    if (!mod_abi_ok(h, ifs, nifs)) {
        snprintf(why, wn, "%.16s %s: for another firmware", mod_title(h->name), v);
        snprintf(detail, dn, "Module %.16s %s needs interface %.8s ABI %u-%u, which this firmware does not "
                             "offer: use the module files of this firmware's release.",
                 h->name, v, h->iface, h->abi_min, h->abi_max);
        return -1;
    }
#endif
    uint32_t crc = 0;
    uint8_t buf[512];
    for (uint32_t at = MOD_PAYLOAD_OFF; at < f->size;) {
        int k = vd_file_read(vd, f, at, buf, sizeof buf);
        if (k <= 0)
            break;
        crc = crc32_update(crc, buf, (size_t)k);
        at += (uint32_t)k;
    }
    if (crc != h->payload_crc) {
        snprintf(why, wn, "%.16s %s: damaged file", mod_title(h->name), v);
        snprintf(detail, dn, "Module %.16s %s: the payload does not match its checksum (a damaged or incomplete "
                             "copy). Copy the file again.",
                 h->name, v);
        return -1;
    }
    return 0;
}

/* Handle the pending file; true when it is done (else: retry later). */
static bool handle(vdrive_t *vd, const vd_found_t *f)
{
    char why[72], detail[256];
    mod_hdr_t h;
#if HAVE_NB
    if (f->notebook) {
        if (py_nb_shown()) { /* the notebook being edited could be the one replaced */
            if (!waiting_said)
                ui_toast("Leave Notebook to import it");
            waiting_said = true;
            return false;
        }
        nb_result_set = false;
        int r = py_nb_call("import", f->name);
        if (r == -2)
            return false; /* module code runs: later */
        if (!nb_result_set)
            result(false, f->name, "Notebook not imported", r == -1 ? modx_why(PY_MODULE) : "");
        app_redraw();
        return true;
    }
#endif
    if (!f->module) {
        snprintf(why, sizeof why, "Not a module: %.26s", f->name);
        ui_toast(why);
        return true; /* left on the drive until the next refresh; RESULT.TXT unchanged */
    }
    if (!pend_checked) { /* once: a file waiting for its module to be left is not read again */
        if (check(vd, f, &h, why, sizeof why, detail, sizeof detail)) {
            result(false, f->name, why, detail);
            return true;
        }
        pend_checked = true, pend_hdr = h;
    }
    h = pend_hdr;
    if (in_use(h.name)) {
        if (!waiting_said) {
            snprintf(why, sizeof why, "Leave %.16s to install it", mod_title(h.name));
            ui_toast(why);
            waiting_said = true;
        }
        return false;
    }
    stage_src_t src = {vd, f, 0};
    char msg[160];
    int r = modules_install_stream(f->name, "USB", rd_stage, &src, msg, sizeof msg);
    char v[24];
    snprintf(v, sizeof v, "%u.%u.%u", h.ver[0], h.ver[1], h.ver[2]);
    if (r)
        snprintf(why, sizeof why, "%.16s %s not installed", mod_title(h.name), v);
    else
        snprintf(why, sizeof why, "Installed %.16s %s", mod_title(h.name), v);
    app_redraw(); /* the progress screen is gone */
    result(r == 0, f->name, why, msg);
    return true;
}

/* NOTEBOOK.HTML out of date and the moment to generate the drive again (see the file comment) */
static bool html_refresh(uint32_t now)
{
    if (!nbd || nbd->gen == html_gen || have_pend || modx_busy() || usbdisk_changing(USBDISK_CALC) ||
        now - usbdisk_last_io() < HTML_HOST_QUIET_MS)
        return false;
    bool other = nbd->open != html_open || strcmp(nbd->name, html_name); /* (opened, new, saved as) */
    if (!other && (now - app_last_activity() < HTML_IDLE_MS || (html_idled && now - html_at < HTML_GAP_MS)))
        return false;
    if (py_nb_following())
        return false;
    if (!other)
        html_idled = true, html_at = now;
    return true;
}

void usb_app_tick(uint32_t now)
{
    bool host = usbdisk_host_present();
    if (host != host_was) {
        host_was = host;
        have_pend = false;
    }
    vdrive_t *vd = usbdisk_calc();
    if (!vd || !host)
        return;
    if (html_refresh(now)) {
        usbdisk_media_changed(USBDISK_CALC);
        return;
    }
#if HAVE_NB
    /* the NOTEBOOKS folder could not be listed, or the store changed and the app was left */
    if ((nb_stale || (nb_changed && !py_nb_shown())) && !have_pend && !modx_busy() &&
        !usbdisk_changing(USBDISK_CALC)) {
        usbdisk_media_changed(USBDISK_CALC);
        return;
    }
#endif
    if (!have_pend) {
        if (!vd_poll(vd, now, &pend))
            return;
        have_pend = true;
        waiting_said = pend_checked = false;
    }
    if (!handle(vd, &pend))
        return;
    have_pend = false;
    vd_done(vd, &pend);
    if (pend.module || pend.notebook)
        usbdisk_media_changed(USBDISK_CALC); /* the computer reads the new directory */
}

/* ------------------------------------------------------------ the card */
static const screen_t *card_back_to;

static void card_draw(void)
{
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, TH_BG);
    gfx_text(&font_large, 12, BODY_Y + 30, "Connected to computer", TH_FG, GFX_TRANSPARENT);
    gfx_text(&font_medium, 12, BODY_Y + 70, "The microSD card is now a drive", TH_FG, GFX_TRANSPARENT);
    gfx_text(&font_medium, 12, BODY_Y + 90, "of the computer.", TH_FG, GFX_TRANSPARENT);
    gfx_text(&font_medium, 12, BODY_Y + 120, "Eject it on the computer,", TH_FG, GFX_TRANSPARENT);
    gfx_text(&font_medium, 12, BODY_Y + 140, "then press EXIT.", TH_FG, GFX_TRANSPARENT);
    const char *st = !usbdisk_host_present() ? "The computer is not connected."
                     : usbdisk_card_ejected() ? "Ejected: you can press EXIT."
                                              : "In use by the computer.";
    gfx_text(&font_small, 12, BODY_Y + 175, st, TH_DIM, GFX_TRANSPARENT);
}

static void card_take_back(void)
{
    usbdisk_card_connect(false);
    fs_lock(false);
    const screen_t *s = card_back_to ? card_back_to : ui_home();
    card_back_to = NULL;
    ui_switch(s);
    ui_toast("Card back in the calculator");
}

static void card_exit(void)
{
    if (usbdisk_host_present() && !usbdisk_card_ejected())
        ui_confirm("Not ejected on the computer: take it back?", card_take_back);
    else
        card_take_back();
}

static void card_key(int key, int mod)
{
    (void)mod;
    if (key == KEY_CLEAR || key == KEY_HOME)
        card_exit();
}

static uint32_t card_drawn;
static void card_tick(uint32_t now)
{
    if (now - card_drawn > 500) { /* the status line follows the computer */
        card_drawn = now;
        card_draw();
    }
}

static const softkeys_t card_soft = {{{NULL}, {NULL}, {NULL}, {NULL}, {"EXIT", card_exit}}, {{NULL}}, NULL};
const screen_t scr_usbcard = {"usbcard", "USB", NULL, card_draw, card_key, card_tick, NULL, NULL, "usb", &card_soft};

void usb_app_card_open(void)
{
    if (!hal_usb_configured() && !usbdisk_host_present()) {
        ui_message("CONNECT CARD", "Connect a computer first (USB cable).");
        return;
    }
    if (!hal_sd_present()) {
        ui_message("CONNECT CARD", "No card in the slot.");
        return;
    }
    const screen_t *from = ui_current() == &scr_menu ? menu_origin() : ui_current();
    fs_lock(true);
    if (usbdisk_card_connect(true)) {
        fs_lock(false);
        ui_message("CONNECT CARD", "The card does not answer.");
        return;
    }
    card_back_to = from;
    ui_switch(&scr_usbcard);
}

/* ------------------------------------------------------------ USB status page */
/* MEM > USB status: the device's diagnostics (hal_usb_status: VBUS, state,
 * the last error, clocks, counts, the last SETUP) and the CALC drive's, four
 * times a second. Reachable with USB not working at all, so a board brought
 * up without a probe can be read off the screen. */
typedef struct {
    int y;
} st_draw_t;
static void st_line(const char *k, const char *v, void *u)
{
    st_draw_t *d = u;
    if (d->y > UI_BOTTOM - 13)
        return;
    gfx_text(&font_small, 4, d->y, k, TH_DIM, GFX_TRANSPARENT);
    /* values longer than the column (38 characters) go on at the next line, split at a space */
    char b[40];
    size_t n = strlen(v);
    while (n > 38 && d->y <= UI_BOTTOM - 26) {
        size_t cut = 38;
        while (cut > 20 && v[cut] != ' ')
            cut--;
        snprintf(b, sizeof b, "%.*s", (int)cut, v);
        gfx_text(&font_small, 88, d->y, b, TH_FG, GFX_TRANSPARENT);
        d->y += 13;
        v += cut + (v[cut] == ' ');
        n = strlen(v);
    }
    gfx_text(&font_small, 88, d->y, v, TH_FG, GFX_TRANSPARENT);
    d->y += 13;
}

static void st_draw(void)
{
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, TH_BG);
    st_draw_t d = {BODY_Y + 2};
    hal_usb_status(st_line, &d);
    char b[64];
    vdrive_t *vd = usbdisk_calc();
    if (!vd)
        snprintf(b, sizeof b, "none (no SDRAM staging)");
    else
        snprintf(b, sizeof b, "%s, %lu KB free, %lu written",
                 usbdisk_changing(USBDISK_CALC) ? "refreshing" : usbdisk_host_present() ? "in use" : "idle",
                 (unsigned long)(vd_free_bytes(vd) >> 10), (unsigned long)usbdisk_host_writes());
    st_line("CALC drive", b, &d);
    st_line("Pending", have_pend ? pend.name : "-", &d);
    st_line("Last install", last_result[0] ? last_result : "-", &d);
}

static uint32_t st_drawn;
static void st_tick(uint32_t now)
{
    if (now - st_drawn >= 250) {
        st_drawn = now;
        st_draw();
    }
}
static void st_key(int key, int mod)
{
    (void)mod;
    if (key == KEY_CLEAR)
        ui_switch(ui_home());
}
const screen_t scr_usbstat = {"usbstatus", "USB STATUS", NULL, st_draw, st_key, st_tick, NULL, NULL, "usb", NULL};
void usb_app_status_open(void) { ui_switch(&scr_usbstat); }

/* ------------------------------------------------------------ state */
void usb_app_state(void (*kv)(const char *, const char *, void *), void *user)
{
    kv("usb_host", usbdisk_host_present() ? "1" : "0", user);
    kv("usb_result", last_result, user);
    kv("usb_card", usbdisk_card_connected() ? (usbdisk_card_ejected() ? "ejected" : "connected") : "off", user);
    kv("usb_pending", have_pend ? pend.name : "", user);
}
