/*
 * modules_ui.h -- content modules in the app (app/modules_ui.c): boot,
 * Memory > Install content / Modules / Remove content, the About lines, the
 * console `module` and `safemode` commands. Design: docs/QSPI_CONTENT.md;
 * store format: core/modstore.h.
 */
#ifndef MODULES_UI_H
#define MODULES_UI_H
#include <stddef.h>

/* Version of the manual module payload format app/help.c reads: the "help"
 * interface ABI (tools/gen_help.py HELP_ABI, Makefile HELP_ABI). */
#define HELP_ABI 1

/* app_init: mount the module stores (skipped in safe mode and in the core
 * tier), before anything uses the manual. */
void modules_boot(void);
/* app_init, after the home screen is shown: safe-mode / flash-fault toast,
 * or the offer to install missing modules from the microSD card. */
void modules_boot_notice(void);
void modules_install_all(void);   /* Memory > Install content */
void modules_open(void);          /* Memory > Modules */
void modules_remove_all(void);    /* Memory > Remove content (asks first) */
/* About page: markup lines (bullets) for the Content and Devices sections. */
void modules_about(char *out, size_t n);
int modules_console(int argc, char **argv);  /* console `module ...` */
int safemode_console(int argc, char **argv); /* console `safemode ...` */
int watchdog_console(int argc, char **argv); /* console `wdg ...` */
int devices_console(int argc, char **argv);  /* console `devices` */
void modules_state(void (*kv)(const char *, const char *, void *), void *user);
/* Install a module file from a sequential source (rd: next bytes into buf,
 * 0 at the end, <0 on an error), as Memory > Modules does from the card:
 * the store's installer with its checks, the progress screen, the previous
 * version kept. path names the file in msg (a sentence), what the source on
 * the progress screen. 0, or <0 (also in safe mode, without a working QSPI
 * and in the core tier, which has no module store). The USB drive's installs
 * (app/usb_app.c). */
typedef int (*mod_reader_fn)(void *user, void *buf, size_t n);
int modules_install_stream(const char *path, const char *what, mod_reader_fn rd, void *user, char *msg, size_t n);

/* help.c: the manual module changed (install, remove); reset its cache. */
void help_content_changed(void);
/* help.c: where page `id` comes from: "module", "builtin" or "none". */
const char *help_page_source(const char *id);
/* help.c: one line for the About page. */
void help_manual_status(char *out, size_t n);

#endif
