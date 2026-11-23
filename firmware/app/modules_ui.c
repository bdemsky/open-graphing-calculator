/*
 * Content modules in the app (docs/QSPI_CONTENT.md, core/modstore.h).
 *
 * What the user sees:
 *   - Memory > Install content: the "full install" from the module library on
 *     the microSD card (/CALC/MODULES/ *.MOD): for every module name found
 *     there, the newest version this firmware can use is installed unless
 *     it is already the active one. Progress bar while writing, a message
 *     with the result.
 *   - Memory > Modules: one row per module (installed version and state, or
 *     "not installed" for one that is only on the card), which opens Verify /
 *     Revert to the previous version / Remove; under it one row per version
 *     of that module on the card, newest first, with what installing it
 *     would do (new, upgrade, downgrade, reinstall, incompatible): selecting
 *     one installs exactly that version. The last row rescans the card.
 *     Release files carry their version in the 8.3 name (MANUA010.MOD =
 *     manual 0.1.0, tools/mkmodule.py `filename`), so versions sit side by
 *     side on the card; the firmware itself goes by the headers, never the
 *     file names.
 *     Simulators (native and web) also list the modules bundled into the
 *     emulator (content_library[]: the release library and the extras such
 *     as Math Quest) as "emulator X.Y.Z" rows, installed through the same
 *     installer and checks as a card file; a last row says so. Console
 *     `module bundled [install]` lists them / installs the newest of each.
 *   - Memory > Remove content: every module is removed (asks first).
 *   - At start-up: SAFE MODE or a flash fault is announced with a toast;
 *     otherwise, when the manual is missing and the card holds a usable
 *     manual module, the calculator offers to install it (MODULE_SD_OFFER).
 *   - Console `module ...`: list, upload (begin/put/end, the path used by
 *     tools/calcctl.py --upload-module), install from a card file, export to
 *     the card, verify, revert, remove, format, plus test hooks (fail N,
 *     corrupt NAME); `safemode info|force|off`.
 *
 * The core's interfaces (core_ifaces) are what modules are checked against:
 * "help" (the manual payload format, HELP_ABI) and, in the advanced tier,
 * "cas" (phase 2: the extension-module function table).
 *
 * Everything here runs in the main loop; installs block it (seconds) and
 * draw their own progress screen. State: the library scan (lib[]) and the
 * installer (inst) are statics; nothing is persisted outside the QSPI store.
 */
#include "modules_ui.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "console.h"
#include "fat.h"
#include "gfx.h"
#include "app.h" /* app_crash_restart */
#include "hal.h"
#include "anim.h"
#include "modstore.h"
#include "modx.h"
#if !defined(PLATFORM_STM32)
#include "hostdev.h" /* content_library: console `module library` */
#endif
#include "rle.h" /* crc32_update: module export X.Y.Z */
#include "safemode.h"
#include "sdbk.h"
#include "sections.h"
#include "session.h"
#include "ui.h"

#define MANUAL_MODULE "manual"

static mod_iface_t core_ifaces[] = { /* not const: console `module coreabi` (tests) */
    {"help", HELP_ABI},
    {MODX_IFACE, MODX_ABI}, /* code modules: the API table (app/modx.h) */
    {"webui", 1},           /* the browser notebook page: served as is (app/usb_app.c) */
#if defined(CONFIG_CAS_EXT)
    {"cas", 1}, /* phase 2: reserved, no module uses it yet */
#endif
};
#define NIFACES ((int)(sizeof core_ifaces / sizeof core_ifaces[0]))

#if defined(CONFIG_CONTENT_QSPI)
#define HAVE_MODULES 1
#else
#define HAVE_MODULES 0
#endif
/* simulators: the modules bundled into the emulator (content_library[]) are offered next to the card */
#if HAVE_MODULES && !defined(PLATFORM_STM32)
#define HAVE_BUNDLED 1
#else
#define HAVE_BUNDLED 0
#endif

void modules_boot(void)
{
    modx_forget_all(); /* the previous run's bindings (the simulators restart the app) */
#if HAVE_MODULES
    mod_boot(core_ifaces, NIFACES, safemode_active());
#endif
    help_content_changed();
}

static void vstr(const uint16_t v[3], char *out, size_t n) { snprintf(out, n, "%u.%u.%u", v[0], v[1], v[2]); }

/* ------------------------------------------------------------ library (microSD) */
#define MAX_LIB (HAVE_BUNDLED ? 24 : 16) /* simulators: room for the bundled modules too */
typedef struct {
    char path[48];            /* /CALC/MODULES/NAME.MOD (bundled: the file name) */
    char name[MOD_NAME_LEN + 1];
    uint16_t ver[3];
    uint32_t size;
    bool abi_ok;
    const uint8_t *data; /* bundled into the emulator (content_library[]); NULL: a card file */
} lib_entry_t;
static DTCM_BSS lib_entry_t lib[MAX_LIB];
static int nlib, lib_err;

static int lib_cb(const fs_dirent_t *e, void *user)
{
    (void)user;
    size_t n = strlen(e->short_name);
    if (e->is_dir || n < 5 || strcmp(e->short_name + n - 4, ".MOD") || nlib >= MAX_LIB)
        return 0;
    snprintf(lib[nlib].path, sizeof lib[nlib].path, "%s/%s", MODSTORE_SD_DIR, e->short_name);
    lib[nlib].size = e->size;
    lib[nlib].data = NULL;
    nlib++;
    return 0;
}

/* Simulators: append the modules bundled into the emulator (content_library[]),
 * each checked like a card file's header. */
static void lib_add_bundled(void)
{
#if HAVE_BUNDLED
    for (int i = 0; i < content_library_all && nlib < MAX_LIB; i++) {
        const struct content_file *c = &content_library[i];
        mod_hdr_t h;
        if (c->size < sizeof h)
            continue;
        memcpy(&h, c->data, sizeof h);
        if (!mod_header_ok(&h, 0xFFFFFFFFu))
            continue;
        lib_entry_t *e = &lib[nlib++];
        snprintf(e->path, sizeof e->path, "%s", c->name);
        memcpy(e->name, h.name, MOD_NAME_LEN);
        e->name[MOD_NAME_LEN] = 0;
        memcpy(e->ver, h.ver, sizeof h.ver);
        e->size = c->size;
        e->abi_ok = mod_abi_ok(&h, core_ifaces, NIFACES);
        e->data = c->data;
    }
#endif
}

/* Scan /CALC/MODULES and read every module file's header; with `bundled`
 * (simulators), the emulator's bundled modules follow. Returns the number
 * of usable entries; lib_err holds a card error (<0) if the card failed. */
static int lib_scan_ex(bool bundled)
{
    nlib = 0;
    lib_err = 0;
    if (!hal_sd_present()) {
        lib_err = FS_ENOCARD;
        if (bundled)
            lib_add_bundled();
        return nlib;
    }
    int r = fs_list(MODSTORE_SD_DIR, lib_cb, NULL);
    if (r && r != FS_ENOENT)
        lib_err = r;
    int k = 0;
    for (int i = 0; i < nlib; i++) {
        fs_file_t f;
        mod_hdr_t h;
        if (fs_open(&f, lib[i].path, FS_READ))
            continue;
        int got = fs_read(&f, &h, sizeof h);
        fs_close(&f);
        if (got != (int)sizeof h || !mod_header_ok(&h, 0xFFFFFFFFu))
            continue;
        lib[k] = lib[i];
        memcpy(lib[k].name, h.name, MOD_NAME_LEN);
        lib[k].name[MOD_NAME_LEN] = 0;
        memcpy(lib[k].ver, h.ver, sizeof h.ver);
        lib[k].abi_ok = mod_abi_ok(&h, core_ifaces, NIFACES);
        k++;
    }
    nlib = k;
    fs_unmount();
    if (bundled)
        lib_add_bundled();
    return nlib;
}

/* What installing library entry i would do, relative to the active version. */
static const char *lib_action(int i)
{
    if (!lib[i].abi_ok)
        return "incompatible";
    const mod_hdr_t *h = mod_header(lib[i].name);
    if (!h)
        return "new";
    int c = mod_vercmp(lib[i].ver, h->ver);
    return c > 0 ? "upgrade" : c < 0 ? "downgrade" : "reinstall";
}

/* ------------------------------------------------------------ install */
static DTCM_BSS mod_inst_t inst;
static uint32_t prog_last;

/* Progress screen: title, module, bar, percentage. Drawn straight to the
 * panel (the main loop is blocked while installing). */
static void progress(const char *what, uint32_t done, uint32_t total)
{
    if (done && done - prog_last < 16384u && done != total)
        return;
    prog_last = done;
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    gfx_text(&font_medium, 12, BODY_Y + 40, "Installing content", C_NAVY, GFX_TRANSPARENT);
    gfx_text(&font_medium, 12, BODY_Y + 62, what, C_BLACK, GFX_TRANSPARENT);
    int w = LCD_WIDTH - 24;
    int fill = total ? (int)((uint64_t)w * done / total) : 0;
    gfx_rect(12, BODY_Y + 90, w, 16, C_DKGRAY);
    gfx_fill_rect(13, BODY_Y + 91, fill > 2 ? fill - 2 : 0, 14, C_BLUE);
    char b[32];
    snprintf(b, sizeof b, "%lu %%", total ? (unsigned long)((uint64_t)100 * done / total) : 0ul);
    gfx_text(&font_medium, 12, BODY_Y + 112, b, C_BLACK, GFX_TRANSPARENT);
    gfx_text(&font_small, 12, BODY_Y + 140, "Do not switch off. A power cut is safe:", C_DKGRAY, GFX_TRANSPARENT);
    gfx_text(&font_small, 12, BODY_Y + 154, "the previous version stays in use.", C_DKGRAY, GFX_TRANSPARENT);
    gfx_flush();
}

/* After any change to the store: re-resolve which versions serve, and let
 * help forget its cached manual header. */
static void changed(void)
{
    modx_unbind_all(); /* code modules may have moved or gone: bind again on next use */
    mod_remount();
    help_content_changed();
    anim_module_changed();
}

/* Install one module file from a sequential source (the card, a file bundled
 * into the emulator, the USB drive's staging buffer): the same installer and
 * checks for all, fed in the same chunks. `what` names the source on the
 * progress screen ("microSD"), `path` the file in failure messages. msg gets a
 * sentence. 0 / <0. */
static int install_from(const char *path, const char *what, mod_reader_fn rd, void *user, bool force, char *msg,
                        size_t n)
{
    modx_unbind_all(); /* no module code may run from the QSPI while it is written */
    int r = mod_inst_begin(&inst, mod_store(MODSTORE_QSPI), core_ifaces, NIFACES, force);
    uint8_t buf[1024]; /* on the stack: AXI SRAM has no room for a static one */
    prog_last = 0;
    while (!r) {
        int k = rd(user, buf, sizeof buf);
        if (k < 0) {
            if (k == FS_EIO || k == FS_ENOCARD || k == FS_EINVAL)
                sdbk_card_error(k, msg, n);
            else
                snprintf(msg, n, "%s: read error.", path);
            changed();
            return -1;
        }
        if (k == 0)
            break;
        r = mod_inst_write(&inst, buf, (size_t)k);
        hal_watchdog_kick(); /* an install blocks the main loop for seconds (the QSPI driver kicks during erases) */
        char where[48];
        snprintf(where, sizeof where, "%.16s from %s", inst.hdr.name, what);
        progress(where, inst.pos, mod_inst_total(&inst));
    }
    if (!r)
        r = mod_inst_finish(&inst);
    changed();
    char v[20];
    vstr(inst.hdr.ver, v, sizeof v);
    if (r)
        snprintf(msg, n, "%.16s %s: %s.%s", inst.pos >= MOD_HDR_BYTES ? inst.hdr.name : path,
                 inst.pos >= MOD_HDR_BYTES ? v : "", mod_strerror(r),
                 inst.dropped_active ? " The previous version had to be removed for space." : "");
    else
        snprintf(msg, n, "%.16s %s installed and verified.", inst.hdr.name, v);
    return r;
}

/* sources: a card file, a block of memory */
static int rd_card(void *user, void *buf, size_t n) { return fs_read(user, buf, n); }
typedef struct {
    const uint8_t *data;
    uint32_t size, at;
} mem_src_t;
static int rd_mem(void *user, void *buf, size_t n)
{
    mem_src_t *m = user;
    size_t k = m->size - m->at < n ? m->size - m->at : n;
    memcpy(buf, m->data + m->at, k);
    m->at += (uint32_t)k;
    return (int)k;
}

/* Install one module file from the card, or (data != NULL, simulators) one
 * bundled into the emulator. */
static int install_src(const char *path, const uint8_t *data, uint32_t size, bool force, char *msg, size_t n)
{
    if (data) {
        mem_src_t m = {data, size, 0};
        return install_from(path, "the emulator", rd_mem, &m, force, msg, n);
    }
    modx_unbind_all();
    fs_file_t f;
    int r = fs_open(&f, path, FS_READ);
    if (r) {
        sdbk_card_error(r, msg, n);
        fs_unmount();
        return -1;
    }
    r = install_from(path, "microSD", rd_card, &f, force, msg, n);
    fs_close(&f);
    fs_unmount();
    return r;
}

int modules_install_stream(const char *path, const char *what, mod_reader_fn rd, void *user, char *msg, size_t n)
{
#if HAVE_MODULES
    if (safemode_active()) {
        snprintf(msg, n, "Safe mode is on: switch off and on, then install.");
        return -1;
    }
    if (!mod_store(MODSTORE_QSPI)->dev) {
        snprintf(msg, n, "The QSPI flash is not responding (Memory > About).");
        return -1;
    }
    return install_from(path, what, rd, user, false, msg, n);
#else
    (void)rd, (void)user, (void)what;
    snprintf(msg, n, "%s: this firmware (core tier) has no module store.", path);
    return -1;
#endif
}

static char msgbuf[256];

/* library entry i, from the card or the emulator */
static int install_lib(int i, char *msg, size_t n)
{
    return install_src(lib[i].path, lib[i].data, lib[i].size, false, msg, n);
}

#if HAVE_MODULES
static bool install_possible(void)
{
    if (safemode_active()) {
        ui_message("INSTALL NOT POSSIBLE", "Safe mode is on: switch off and on, then install.");
        return false;
    }
    if (!mod_store(MODSTORE_QSPI)->dev) {
        ui_message("INSTALL FAILED", "The QSPI flash is not responding (Memory > About). The core firmware "
                                     "works without it.");
        return false;
    }
    return true;
}

/* Per module name in lib[], the newest compatible version unless it is already
 * the active one; a message with the result. Returns the number that failed. */
static int install_newest(const char *none)
{
    int done = 0, failed = 0, uptodate = 0;
    size_t o = 0;
    msgbuf[0] = 0;
    for (int i = 0; i < nlib; i++) {
        /* the newest compatible version of each name, once */
        bool first = true;
        for (int j = 0; j < i; j++)
            if (!strcmp(lib[j].name, lib[i].name))
                first = false;
        if (!first)
            continue;
        int best = -1;
        for (int j = 0; j < nlib; j++)
            if (!strcmp(lib[j].name, lib[i].name) && lib[j].abi_ok &&
                (best < 0 || mod_vercmp(lib[j].ver, lib[best].ver) > 0))
                best = j;
        if (best < 0)
            continue;
        const mod_hdr_t *h = mod_header(lib[best].name);
        if (h && !mod_vercmp(h->ver, lib[best].ver) && mod_state(lib[best].name) == MOD_OK &&
            mod_serving_store(lib[best].name) == MODSTORE_QSPI) {
            uptodate++;
            continue;
        }
        char one[160];
        if (install_lib(best, one, sizeof one))
            failed++;
        else
            done++;
        if (o < sizeof msgbuf)
            o += (size_t)snprintf(msgbuf + o, sizeof msgbuf - o, "%s%s", o ? " " : "", one);
    }
    if (!done && !failed)
        snprintf(msgbuf, sizeof msgbuf, "%s", uptodate ? "Everything is already installed." : none);
    ui_message(failed ? "INSTALL FAILED" : "INSTALLED", msgbuf);
    return failed;
}
#endif

void modules_install_all(void)
{
#if !HAVE_MODULES
    ui_message("INSTALL", "This firmware (core / rollback build) uses no modules; it has the essential help pages built in.");
#else
    if (!install_possible())
        return;
    lib_scan_ex(false);
    if (lib_err) {
        sdbk_card_error(lib_err, msgbuf, sizeof msgbuf);
        ui_message("INSTALL FAILED", msgbuf);
        return;
    }
    install_newest("No module files for this firmware in " MODSTORE_SD_DIR " on the card.");
#endif
}

/* ------------------------------------------------------------ boot notice */
static void offer_yes(void) { modules_install_all(); }

void modules_boot_notice(void)
{
    if (safemode_active()) {
        ui_toast("SAFE MODE: Memory > About");
        return;
    }
    if (hal_storage_size() == 0) {
        ui_toast("flash fault: Memory > About");
        return;
    }
#if HAVE_MODULES && defined(CONFIG_MODULE_SD_OFFER)
    if (mod_state(MANUAL_MODULE) == MOD_OK || !mod_store(MODSTORE_QSPI)->dev || !hal_sd_present())
        return;
    lib_scan_ex(false);
    for (int i = 0; i < nlib; i++)
        if (!strcmp(lib[i].name, MANUAL_MODULE) && lib[i].abi_ok) {
            ui_confirm("Install the manual from microSD?", offer_yes);
            return;
        }
#endif
}

/* ------------------------------------------------------------ Memory > Modules */
/* One list: per module name (installed, or only on the card) a row with the
 * installed version and state, followed by one row per version of it found
 * on the card ("card 0.2.0: upgrade"), newest first, so every version sits
 * next to the installed one and any of them can be picked; then a row to
 * rescan the card. The card is scanned when the screen opens and on rescan
 * (lib_scan, sorted by lib_sort). */
enum { ROW_MODULE, ROW_CARD, ROW_RESCAN, ROW_NOTE };
typedef struct {
    uint8_t kind;
    int8_t idx; /* ROW_MODULE: index into names[]; ROW_CARD: into lib[] */
} mrow_t;
#define MAX_NAMES (MOD_MAX_EXTENTS + MAX_LIB)
static const char *names[MAX_NAMES];
static bool installed[MAX_NAMES];
static int nnames;
static mrow_t rows[MAX_NAMES + MAX_LIB + 2];
static int nrows;
static char sel_name[MOD_NAME_LEN + 1];
static char item[64];

/* lib[]: the card's files, then the emulator's bundled ones (simulators); each
 * by name, then newest version first (insertion sort: <= MAX_LIB). */
static void lib_sort(void)
{
    for (int i = 1; i < nlib; i++)
        for (int j = i; j > 0; j--) {
            int c = (lib[j - 1].data != NULL) - (lib[j].data != NULL);
            if (!c)
                c = strcmp(lib[j - 1].name, lib[j].name);
            if (c < 0 || (c == 0 && mod_vercmp(lib[j - 1].ver, lib[j].ver) >= 0))
                break;
            lib_entry_t t = lib[j];
            lib[j] = lib[j - 1];
            lib[j - 1] = t;
        }
}

static int mods_count(void)
{
    const char *inst_names[MOD_MAX_EXTENTS];
    int k = mod_names(inst_names, MOD_MAX_EXTENTS);
    nnames = 0;
    for (int i = 0; i < k && nnames < MAX_NAMES; i++) {
        installed[nnames] = true;
        names[nnames++] = inst_names[i];
    }
    for (int i = 0; i < nlib && nnames < MAX_NAMES; i++) { /* modules only on the card */
        bool seen = false;
        for (int j = 0; j < nnames && !seen; j++)
            seen = !strcmp(names[j], lib[i].name);
        if (!seen) {
            installed[nnames] = false;
            names[nnames++] = lib[i].name;
        }
    }
    nrows = 0;
    for (int i = 0; i < nnames; i++) {
        rows[nrows++] = (mrow_t){ROW_MODULE, (int8_t)i};
        for (int j = 0; j < nlib; j++)
            if (!strcmp(lib[j].name, names[i]))
                rows[nrows++] = (mrow_t){ROW_CARD, (int8_t)j};
    }
    rows[nrows++] = (mrow_t){ROW_RESCAN, 0};
    if (HAVE_BUNDLED)
        rows[nrows++] = (mrow_t){ROW_NOTE, 0};
    return nrows;
}

static void mods_get(int i, menu_item_t *out)
{
    const mrow_t *r = &rows[i];
    if (r->kind == ROW_MODULE) {
        const char *nm = names[r->idx];
        const mod_hdr_t *h = installed[r->idx] ? mod_header(nm) : NULL;
        char v[20] = "-";
        if (h)
            vstr(h->ver, v, sizeof v);
        if (installed[r->idx])
            snprintf(item, sizeof item, "%s %s (%s)", nm, v, mod_state_name(mod_state(nm)));
        else
            snprintf(item, sizeof item, "%s: not installed", nm);
    } else if (r->kind == ROW_CARD) {
        char v[20];
        vstr(lib[r->idx].ver, v, sizeof v);
        snprintf(item, sizeof item, "  %s %s: %s", lib[r->idx].data ? "emulator" : "card", v, lib_action(r->idx));
    } else if (r->kind == ROW_NOTE) {
        snprintf(item, sizeof item, "Emulator: bundled modules");
    } else {
        snprintf(item, sizeof item, "%s", lib_err == FS_ENOCARD ? "microSD: no card (insert, then rescan)"
                                          : lib_err                ? "microSD: card error (details)"
                                                                   : "Rescan the microSD card");
    }
    *out = (menu_item_t){item, NULL, NULL, i, "install-content"};
}

enum { A_VERIFY, A_REVERT, A_REMOVE };
static char revert_label[48];
static int act_count(void) { return 3; }
static void act_get(int i, menu_item_t *out)
{
    static const char *const labels[] = {"Verify", NULL, "Remove"};
    if (i == A_REVERT) {
        modstore_t *st = mod_store(MODSTORE_QSPI);
        int v[MOD_MAX_EXTENTS];
        int n = modstore_versions(st, sel_name, v, MOD_MAX_EXTENTS);
        char vs[20];
        if (n >= 2) {
            vstr(st->ext[v[1]].ver, vs, sizeof vs);
            snprintf(revert_label, sizeof revert_label, "Revert to %s", vs);
        } else
            snprintf(revert_label, sizeof revert_label, "Revert (no previous version)");
        *out = (menu_item_t){revert_label, NULL, NULL, i, "install-content"};
        return;
    }
    *out = (menu_item_t){labels[i], NULL, NULL, i, "install-content"};
}

static void act_select(const menu_item_t *it)
{
    int r;
    switch (it->action) {
    case A_VERIFY:
        r = mod_verify(sel_name);
        snprintf(msgbuf, sizeof msgbuf, "%s: %s.", sel_name, r ? "verification FAILED" : "verified, no errors");
        ui_message(r ? "VERIFY FAILED" : "MODULES", msgbuf);
        break;
    case A_REVERT:
        r = modstore_revert(mod_store(MODSTORE_QSPI), sel_name);
        changed();
        snprintf(msgbuf, sizeof msgbuf, "%s: %s.", sel_name, r ? mod_strerror(r) : "previous version active again");
        ui_message(r ? "REVERT FAILED" : "MODULES", msgbuf);
        break;
    default:
        r = modstore_remove(mod_store(MODSTORE_QSPI), sel_name);
        changed();
        snprintf(msgbuf, sizeof msgbuf, "%s: %s.", sel_name, r ? mod_strerror(r) : "removed");
        ui_message(r ? "REMOVE FAILED" : "MODULES", msgbuf);
        break;
    }
}
static const menu_def_t menu_act = {"MODULE", NULL, 0, act_select, false, "install-content", act_count, act_get};

static int sel_lib;
static char confirm_q[64];
static void lib_install_yes(void)
{
    int r = install_lib(sel_lib, msgbuf, sizeof msgbuf);
    ui_message(r ? "INSTALL FAILED" : "INSTALLED", msgbuf);
}

static const menu_def_t menu_mods;
static void mods_select(const menu_item_t *it)
{
    const mrow_t *r = &rows[it->action];
    if (r->kind == ROW_NOTE) {
        ui_message("MODULES", "This emulator carries every module it was built with. The \"emulator\" rows "
                              "install them as from a microSD card, with the same checks.");
        return;
    }
    if (r->kind == ROW_RESCAN) {
        if (lib_err && lib_err != FS_ENOCARD) {
            sdbk_card_error(lib_err, msgbuf, sizeof msgbuf);
            ui_message("MODULES", msgbuf);
            return;
        }
        lib_scan_ex(true);
        lib_sort();
        menu_open(&menu_mods);
        return;
    }
    if (r->kind == ROW_CARD) {
        sel_lib = r->idx;
        if (!lib[sel_lib].abi_ok) {
            ui_message("INSTALL NOT POSSIBLE", "This module needs a different firmware version.");
            return;
        }
        char v[20];
        vstr(lib[sel_lib].ver, v, sizeof v);
        snprintf(confirm_q, sizeof confirm_q, "Install %s %s (%s)?", lib[sel_lib].name, v, lib_action(sel_lib));
        ui_confirm(confirm_q, lib_install_yes);
        return;
    }
    if (!installed[r->idx]) {
        ui_message("MODULES", HAVE_BUNDLED ? "Not installed: pick one of its versions (the rows below it)."
                                           : "Not installed: pick one of its versions on the card (the rows below it).");
        return;
    }
    snprintf(sel_name, sizeof sel_name, "%s", names[r->idx]);
    menu_open(&menu_act);
}
static const menu_def_t menu_mods = {"MODULES", NULL, 0, mods_select, false, "install-content", mods_count,
                                     mods_get};

void modules_open(void)
{
#if !HAVE_MODULES
    ui_message("MODULES", "This firmware (core / rollback build) uses no modules; it has the essential help pages built in.");
#else
    if (safemode_active()) {
        ui_message("MODULES", "Safe mode is on: installed modules are not used. Switch off and on to leave it.");
        return;
    }
    lib_scan_ex(true); /* the card's versions (and the emulator's) are listed next to the installed ones */
    lib_sort();
    menu_open(&menu_mods);
#endif
}

static void remove_all_yes(void)
{
    int r = modstore_format(mod_store(MODSTORE_QSPI));
    changed();
    ui_message(r ? "REMOVE FAILED" : "MEM", r ? mod_strerror(r) : "All content modules were removed.");
}

void modules_remove_all(void)
{
#if !HAVE_MODULES
    ui_message("MEM", "This firmware (core build) uses no content modules.");
#else
    ui_confirm("Remove all content modules?", remove_all_yes);
#endif
}

/* ------------------------------------------------------------ About */
static void dev_line(const char *dev, const char *status, void *user)
{
    char **p = user;
    char *out = p[0];
    size_t n = (size_t)(p[1] - p[0]);
    size_t o = strlen(out);
    if (o < n)
        snprintf(out + o, n - o, "- %s: %s\n", dev, status);
}

void modules_about(char *out, size_t n)
{
    char line[160];
    size_t o = 0;
    out[0] = 0;
    /* first, because after a hang or in safe mode this is what the user came for */
    o += (size_t)snprintf(out + o, n - o, "## Start-up\n- Last reset: %s\n", safemode_reset_text());
    if (safemode_active())
        o += (size_t)snprintf(out + o, n - o, "- SAFE MODE: %s. Content and the saved session are not used; "
                                              "switch off and on to leave it ([[safe-mode]]).\n",
                              safemode_reason_text());
    else
        o += (size_t)snprintf(out + o, n - o, "- Safe mode: off\n");
    help_manual_status(line, sizeof line);
    if (o < n)
        o += (size_t)snprintf(out + o, n - o, "\n## Content\n- %s\n", line);
#if HAVE_MODULES
    const char *nm[32];
    int k = mod_names(nm, 32);
    for (int i = 0; i < k && o < n; i++) {
        if (!strcmp(nm[i], MANUAL_MODULE))
            continue;
        const mod_hdr_t *h = mod_header(nm[i]);
        char v[20] = "-";
        if (h)
            vstr(h->ver, v, sizeof v);
        o += (size_t)snprintf(out + o, n - o, "- Module %s %s: %s\n", nm[i], v, mod_state_text(mod_state(nm[i])));
    }
    modstore_t *st = mod_store(MODSTORE_QSPI);
    if (st->dev && o < n)
        o += (size_t)snprintf(out + o, n - o, "- QSPI module store: %lu KB of %lu KB used\n",
                              (unsigned long)(st->used >> 10), (unsigned long)(st->dev->size >> 10));
#endif
    if (o < n)
        o += (size_t)snprintf(out + o, n - o, "- CAS: %s\n",
#if defined(CONFIG_CAS_STUB)
                              "numeric stand-in (stub build)"
#else
                              "full CAS, built into the firmware"
#endif
        );
    if (o < n) {
        o += (size_t)snprintf(out + o, n - o, "\n## Devices\n");
        char *p[2] = {out, out + n};
        hal_device_report(dev_line, p);
    }
}

void modules_state(void (*kv)(const char *, const char *, void *), void *user)
{
    kv("safemode", safemode_active() ? "1" : "0", user);
    char b[24];
    snprintf(b, sizeof b, "%lu", (unsigned long)safemode_attempts());
    kv("boot_attempts", b, user);
    static const char *const causes[] = {"poweron", "wake", "software", "fault", "watchdog", "pin", "brownout", "other"};
    int c = safemode_reset_cause();
    kv("last_reset", c >= 0 && c < (int)(sizeof causes / sizeof causes[0]) ? causes[c] : "other", user);
    kv("watchdog", hal_watchdog_running() ? "on" : "off", user);
#if HAVE_MODULES
    kv("module_manual", mod_state_name(mod_state(MANUAL_MODULE)), user);
    const mod_hdr_t *h = mod_header(MANUAL_MODULE);
    char v[20] = "";
    if (h)
        vstr(h->ver, v, sizeof v);
    kv("module_manual_version", v, user);
#else
    kv("module_manual", "builtin", user);
#endif
}

/* ------------------------------------------------------------ console */
static bool uploading;

static int hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static void list_store(const char *label, modstore_t *st)
{
    if (!st->dev) {
        con_printf("%s: none\n", label);
        return;
    }
    con_printf("%s: %lu of %lu bytes used, %d extents%s\n", label, (unsigned long)st->used,
               (unsigned long)st->dev->size, st->n, st->io_error ? " (read error)" : "");
    for (int i = 0; i < st->n; i++) {
        const mod_extent_t *x = &st->ext[i];
        char v[20];
        vstr(x->ver, v, sizeof v);
        con_printf("  %.16s %s at 0x%06lx span %lu act %lu%s%s\n", x->name, v, (unsigned long)x->off,
                   (unsigned long)x->span, (unsigned long)x->act, x->abi_ok ? "" : " (incompatible)",
                   x->crc_bad ? " (damaged)" : "");
    }
}

int modules_console(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "list";
    char msg[256];
    if (!strcmp(sub, "list")) {
#if HAVE_MODULES
        list_store("qspi", mod_store(MODSTORE_QSPI));
        const char *nm[32];
        int k = mod_names(nm, 32);
        for (int i = 0; i < k; i++)
            con_printf("module %s: %s (store %d)\n", nm[i], mod_state_name(mod_state(nm[i])), mod_serving_store(nm[i]));
#endif
        con_printf("manual: %s\n",
#if HAVE_MODULES
                   mod_state_name(mod_state(MANUAL_MODULE))
#else
                   "builtin"
#endif
        );
        return 0;
    }
#if !HAVE_MODULES
    return con_error("this build (CONTENT_INTERNAL) uses no modules");
#else
    modstore_t *q = mod_store(MODSTORE_QSPI);
    if (!strcmp(sub, "state") && argc > 2) {
        con_printf("%s\n", mod_state_name(mod_state(argv[2])));
        return 0;
    }
    if (!strcmp(sub, "remount")) {
        changed();
        return 0;
    }
    if (!strcmp(sub, "fail") && argc > 2) {
        long nfail;
        if (!con_parse_int(argv[2], &nfail))
            return con_error("bad count");
        mod_test_fail_after(nfail);
        return 0;
    }
#if defined(CONFIG_CONSOLE_MODULE_UPLOAD)
    if (!strcmp(sub, "begin")) {
        bool force = argc > 2 && !strcmp(argv[2], "force");
        modx_unbind_all();
        if (mod_inst_begin(&inst, q, core_ifaces, NIFACES, force))
            return con_error("%s", mod_strerror(inst.err));
        uploading = true;
        return 0;
    }
    if (!strcmp(sub, "put") && argc > 2) {
        if (!uploading)
            return con_error("no upload in progress (module begin)");
        uint8_t b[192];
        size_t len = strlen(argv[2]), n = 0;
        if (len % 2 || len / 2 > sizeof b)
            return con_error("bad hex data (even length, at most %u bytes)", (unsigned)sizeof b);
        for (size_t i = 0; i < len; i += 2) {
            int hi = hexval(argv[2][i]), lo = hexval(argv[2][i + 1]);
            if (hi < 0 || lo < 0)
                return con_error("bad hex digit");
            b[n++] = (uint8_t)(hi << 4 | lo);
        }
        if (mod_inst_write(&inst, b, n)) {
            uploading = false;
            changed();
            return con_error("%s", mod_strerror(inst.err));
        }
        return 0;
    }
    if (!strcmp(sub, "end") || !strcmp(sub, "abort")) {
        if (!uploading)
            return con_error("no upload in progress");
        uploading = false;
        int r = !strcmp(sub, "end") ? mod_inst_finish(&inst) : 0;
        changed();
        if (r)
            return con_error("%s", mod_strerror(r));
        if (!strcmp(sub, "end"))
            con_printf("installed %.16s %u.%u.%u\n", inst.hdr.name, inst.hdr.ver[0], inst.hdr.ver[1], inst.hdr.ver[2]);
        return 0;
    }
#endif
    if (!strcmp(sub, "install") && argc > 2) {
        /* like the menu: the result is a message box (and rc=, not ERR, so
         * tests can install into a failing flash) */
        int r = install_src(argv[2], NULL, 0, argc > 3 && !strcmp(argv[3], "force"), msg, sizeof msg);
        ui_message(r ? "INSTALL FAILED" : "INSTALLED", msg);
        con_printf("rc=%d\n%s\n", r, msg);
        return 0;
    }
    if (!strcmp(sub, "coreabi") && argc > 3) {
        /* test hook: pretend this core speaks another ABI of an interface
         * (a firmware update that changed it), then re-check the modules */
        unsigned long abi;
        for (int i = 0; i < NIFACES; i++)
            if (!strcmp(core_ifaces[i].iface, argv[2]) && con_parse_u32(argv[3], &abi)) {
                core_ifaces[i].abi = (uint16_t)abi;
                modx_unbind_all();
                mod_boot(core_ifaces, NIFACES, safemode_active());
                help_content_changed();
                anim_module_changed();
                return 0;
            }
        return con_error("usage: module coreabi IFACE N (IFACE: help%s)",
#if defined(CONFIG_CAS_EXT)
                         ", cas"
#else
                         ""
#endif
        );
    }
    if (!strcmp(sub, "installall")) {
        modules_install_all();
        return 0;
    }
    if ((!strcmp(sub, "remove") || !strcmp(sub, "revert")) && argc > 2) {
        int r = !strcmp(sub, "remove") ? modstore_remove(q, argv[2]) : modstore_revert(q, argv[2]);
        changed();
        return r ? con_error("%s", mod_strerror(r)) : 0;
    }
    if (!strcmp(sub, "verify") && argc > 2)
        return mod_verify(argv[2]) ? con_error("%s: verification failed", argv[2]) : 0;
    if (!strcmp(sub, "compact")) {
        int r = modstore_compact(q);
        changed();
        return r ? con_error("%s", mod_strerror(r)) : 0;
    }
    if (!strcmp(sub, "format")) {
        int r = modstore_format(q);
        changed();
        return r ? con_error("%s", mod_strerror(r)) : 0;
    }
    if (!strcmp(sub, "corrupt") && argc > 2) {
        /* test hook: clear the bits of one payload byte of the version that
         * serves now (after a CRC check, so already damaged ones are skipped) */
        mod_verify(argv[2]);
        int s, e = mod_serving_extent(argv[2], &s);
        if (e < 0 || s != MODSTORE_QSPI)
            return con_error("%s is not served from the QSPI", argv[2]);
        const mod_extent_t *x = &q->ext[e];
        uint32_t at = x->off + MOD_PAYLOAD_OFF + x->payload_len / 2;
        uint8_t b, zero = 0;
        if (q->dev->read(q->dev->base + at, &b, 1))
            return con_error("read failed");
        if (!b)
            at++;
        if (hal_storage_write(q->dev->base + at, &zero, 1))
            return con_error("write failed");
        changed();
        return 0;
    }
    if (!strcmp(sub, "export") && argc > 3) {
        /* the serving version to a card file, state words erased: a library
         * copy; test hook: `module export NAME FILE X.Y.Z` stamps another
         * version into the header (same payload), so tests get several
         * versions on the card */
        int s, e = mod_serving_extent(argv[2], &s);
        if (e < 0)
            return con_error("%s is not available", argv[2]);
        unsigned vv[3];
        bool restamp = argc > 4;
        if (restamp && sscanf(argv[4], "%u.%u.%u", &vv[0], &vv[1], &vv[2]) != 3)
            return con_error("bad version '%s' (X.Y.Z)", argv[4]);
        modstore_t *st = mod_store(s);
        const mod_extent_t *x = &st->ext[e];
        fs_file_t f;
        int r = fs_mkdir("/CALC");
        if (!r)
            r = fs_mkdir(MODSTORE_SD_DIR);
        if (!r)
            r = fs_open(&f, argv[3], FS_WRITE);
        uint8_t buf[1024]; /* on the stack: AXI SRAM has no room for a static one */
        for (uint32_t p = 0; !r && p < MOD_PAYLOAD_OFF + x->payload_len;) {
            uint32_t m = MOD_PAYLOAD_OFF + x->payload_len - p < sizeof buf ? MOD_PAYLOAD_OFF + x->payload_len - p
                                                                          : (uint32_t)sizeof buf;
            if (st->dev->read(st->dev->base + x->off + p, buf, m))
                r = FS_EIO;
            for (uint32_t k = 0; !r && k < m; k++) /* state words (activation, commit, remove): erased */
                if (p + k >= MOD_ACT_OFF && p + k < MOD_PAYLOAD_OFF)
                    buf[k] = 0xFF;
            if (!r && restamp && p == 0) { /* the header is in the first chunk */
                mod_hdr_t h;
                memcpy(&h, buf, sizeof h);
                for (int i = 0; i < 3; i++)
                    h.ver[i] = (uint16_t)vv[i];
                h.hcrc = crc32_update(0, &h, offsetof(mod_hdr_t, hcrc));
                memcpy(buf, &h, sizeof h);
            }
            if (!r && fs_write(&f, buf, m) != (int)m)
                r = FS_EIO;
            p += m;
        }
        if (!r)
            r = fs_close(&f);
        fs_unmount();
        if (r) {
            sdbk_card_error(r, msg, sizeof msg);
            return con_error("%s", msg);
        }
        return 0;
    }
#if HAVE_BUNDLED
    if (!strcmp(sub, "bundled")) {
        /* the modules bundled into the emulator (Memory > Modules lists them); `module bundled
         * install`: the newest of each, unless already active (the web page's "install all") */
        nlib = 0;
        lib_err = 0;
        lib_add_bundled();
        lib_sort();
        if (argc > 2 && !strcmp(argv[2], "install")) {
            if (!install_possible())
                return con_error("install not possible");
            int failed = install_newest("No bundled modules for this firmware.");
            con_printf("failed=%d\n%s\n", failed, msgbuf);
            return 0;
        }
        for (int i = 0; i < nlib; i++)
            con_printf("%s %u.%u.%u %s: %s\n", lib[i].name, lib[i].ver[0], lib[i].ver[1], lib[i].ver[2], lib[i].path,
                       lib_action(i));
        return 0;
    }
#endif
#if !defined(PLATFORM_STM32)
    if (!strcmp(sub, "library")) {
        /* simulators: the release's module files (content_img.c) onto the card */
        int r = fs_mkdir("/CALC");
        if (!r)
            r = fs_mkdir(MODSTORE_SD_DIR);
        /* `module library extra`: the modules outside the release (Math Quest) instead */
        bool extra = argc > 2 && !strcmp(argv[2], "extra");
        for (int i = extra ? content_library_n : 0; !r && i < (extra ? content_library_all : content_library_n); i++) {
            char path[48];
            fs_file_t f;
            snprintf(path, sizeof path, "%s/%s", MODSTORE_SD_DIR, content_library[i].name);
            r = fs_open(&f, path, FS_WRITE);
            if (!r && fs_write(&f, content_library[i].data, content_library[i].size) != (int)content_library[i].size)
                r = FS_EIO;
            if (!r)
                r = fs_close(&f);
            if (!r)
                con_printf("%s\n", path);
        }
        fs_unmount();
        if (r) {
            sdbk_card_error(r, msg, sizeof msg);
            return con_error("%s", msg);
        }
        return 0;
    }
#endif
    return con_error("usage: module list|state N|begin [force]|put HEX|end|abort|install FILE [force]|installall|"
                     "remove N|revert N|verify N|export N FILE [X.Y.Z]|compact|format|corrupt N|fail K|coreabi I N|remount"
#if HAVE_BUNDLED
                     "|library [extra]|bundled [install]"
#endif
    );
#endif
}

static void dev_print(const char *dev, const char *status, void *user)
{
    (void)user;
    con_printf("%s: %s\n", dev, status);
}

/* console `devices`: how the optional devices answered at boot (hal_device_report). */
int devices_console(int argc, char **argv)
{
    hal_device_report(dev_print, NULL);
    return 0;
}

int safemode_console(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "info";
    if (!strcmp(sub, "info")) {
        con_printf("active=%d\nreason=%s\nattempts=%lu\n", safemode_active(), safemode_reason_text(),
                   (unsigned long)safemode_attempts());
        return 0;
    }
    if (!strcmp(sub, "force")) {
        safemode_test_force(SAFEMODE_FORCED);
        return 0;
    }
    if (!strcmp(sub, "off")) {
        safemode_test_force(SAFEMODE_OFF);
        return 0;
    }
    return con_error("usage: safemode info|force|off (force/off apply to the next boot: reboot)");
}

/* console `wdg`: the watchdog's state and the last reset cause; `wdg hang`
 * loops without feeding it, as a hung firmware would: on hardware the IWDG
 * resets the MCU after ~2 s (the boot log then says "watchdog", the attempt
 * counts towards safe mode); in the simulators the watchdog model expires on
 * the virtual clock and the app restarts the same way. */
int watchdog_console(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "info";
    char st[96];
    hal_watchdog_status(st, sizeof st);
    if (!strcmp(sub, "info")) {
        con_printf("watchdog=%s\nlast_reset=%s\nattempts=%lu\n", st, safemode_reset_text(),
                   (unsigned long)safemode_attempts());
        return 0;
    }
    if (!strcmp(sub, "hang")) {
        if (!hal_watchdog_running())
            return con_error("the watchdog is %s", st);
        con_log("wdg: hanging without feeding the watchdog (reset in ~%u s)", HAL_WATCHDOG_MS / 1000u);
        for (;;) {
            hal_delay_ms(1);
            if (hal_watchdog_expired())
                break; /* simulators only: the hardware never gets here */
        }
        con_log("wdg: watchdog reset");
        app_crash_restart(HAL_RESET_WATCHDOG);
        gfx_flush();
        return 0;
    }
    return con_error("usage: wdg info|hang");
}
