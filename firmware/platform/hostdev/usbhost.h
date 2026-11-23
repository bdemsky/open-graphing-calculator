/*
 * The simulators' USB (platform/hostdev/usbhost.c): the device model behind
 * hal_usb_* (VBUS, a computer that configures the device) and a model of the
 * computer's side of the mass-storage drive, enough to drop a file on the
 * CALC drive the way macOS or Windows would and to read files back. The
 * console `usb` drives it in the UI tests; tests/unit/test_vdrive.c uses the
 * computer model directly on a vdrive (core/vdrive.h).
 */
#ifndef USBHOST_H
#define USBHOST_H
#include <stdbool.h>
#include <stdint.h>

/* A block device as the computer sees it (512-byte sectors). */
typedef struct {
    int (*read)(void *u, uint32_t lba, uint8_t *buf);
    int (*write)(void *u, uint32_t lba, const uint8_t *buf);
    void *u;
} uh_dev_t;

/* How the computer writes a new file:
 *   UH_MAC: AppleDouble "._NAME" file and .fseventsd first, then the file's
 *     directory entry (long name, size 0), its data, its FAT chain, and the
 *     size last (the order macOS uses);
 *   UH_WIN: "System Volume Information" first, then the data, the FAT chain
 *     and the directory entry last (the order Windows uses);
 *   | UH_FRAG: the file's clusters are not contiguous (every other free
 *     cluster), so the drive must follow the chain;
 *   | UH_REPLACE: a file of that name is deleted first (its entries marked
 *     deleted, its chain freed), as when the computer replaces a file.
 * The name may be "DIR/NAME": the file goes into that subdirectory of the
 * root (its chain grows by a cluster when its entries are used up). */
enum { UH_MAC = 0, UH_WIN = 1, UH_FRAG = 0x100, UH_REPLACE = 0x200 };
int uh_write_file(const uh_dev_t *d, const char *path, const uint8_t *data, uint32_t size, int style);
/* Read a file ("NAME" or "DIR/NAME", 8.3 or long names): bytes, -2 not found, -1 not ready. */
int uh_read_file(const uh_dev_t *d, const char *path, uint8_t *out, uint32_t max);
/* List the root (dirname NULL) or a subdirectory of it: cb(name, size, attributes) per entry
 * (not the label); -2: no such directory. */
int uh_list(const uh_dev_t *d, const char *dirname,
            void (*cb)(const char *name, uint32_t size, uint8_t attr, void *u), void *u);

/* console `usb ...` of the simulators */
struct console_cmd;
int hostdev_cmd_usb(int argc, char **argv);

#endif
