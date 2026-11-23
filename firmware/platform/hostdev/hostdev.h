/* Shared by the two simulators (platform/sim, platform/web): the pieces of
 * hal.h that model devices which can fail, so both builds behave alike and the
 * UI tests (which web-test also runs in WebAssembly) can inject faults.
 *
 *   - the factory content: the QSPI content image of this build (generated
 *     build/gen/content_img.c, docs/QSPI_CONTENT.md) is preloaded into the
 *     emulated NOR at start-up, like a calculator that left the factory with
 *     its content installed; `calcsim --no-content` starts empty instead;
 *   - fault injection (console `fault`): a QSPI that is absent (the driver's
 *     JEDEC check failed: size 0), dead (reads return all 0xFF or all 0x00,
 *     erases and writes fail), or loses power after N more erases/writes;
 *     a microSD card that is present but never answers;
 *   - hal_boot_record() (a static: kept across `reboot`, lost at exit),
 *     hal_device_report(), the reset cause, and
 *   - the watchdog model (hal_watchdog_*: expires 2 s of virtual time after
 *     the last kick; console `wdg hang` tests it).
 * Not used by the firmware. */
#ifndef HOSTDEV_H
#define HOSTDEV_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "console.h"

extern bool hostdev_no_content; /* --no-content: do not preload the factory content */
/* The watchdog model runs only on a virtual clock (sim tests, web); the
 * interactive simulator in real time clears this (a debugger pause or a slow
 * host must not "reset" it). */
extern bool hostdev_wdg_model;
/* Called once the emulated NOR exists (erased or loaded from a file): copies
 * the factory content to offset 0 unless it is already there or disabled.
 * sync (may be NULL) writes a changed range through to a backing file. */
void hostdev_flash_attach(uint8_t *flash, size_t size, void (*sync)(uint32_t off, size_t n));
/* Storage size as the HAL reports it (0 while "absent"). */
size_t hostdev_flash_size(size_t real);
/* Before a read: true if the fault mode supplied the data (dead chip). */
bool hostdev_flash_read(void *buf, size_t n);
/* Before an erase/write: 0 = go ahead, -1 = fail (dead chip or power cut). */
int hostdev_flash_modify(void);
#if defined(PLATFORM_SIM)
/* platform/sim/sim_hal.c: console input read ahead during a computation (a Ctrl-C breaks it) */
void sim_console_readahead(void);
#endif
/* Before every microSD operation: -1 = fail (dead card). */
int hostdev_sd_fail(void);
bool hostdev_sd_dead(void);
/* console `fault qspi ok|absent|dead ff|dead 00|cut N`, `fault sd ok|dead` */
int hostdev_cmd_fault(int argc, char **argv);

/* The release's module library (every .MOD of this build, generated into
 * content_img.c by tools/mkmodule.py store --library): the console `module
 * library` writes it to /CALC/MODULES on the simulated card, so tests can
 * install modules that the factory content does not have. */
struct content_file {
    const char *name;
    const uint8_t *data;
    uint32_t size;
};
extern const struct content_file content_library[];
extern const int content_library_n;
/* content_library[content_library_n .. content_library_all): modules built but not part of the
 * release (Math Quest), written to the card by the console `module library extra` */
extern const int content_library_all;

#endif
