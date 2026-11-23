/*
 * usb_app.h -- USB in the application (app/usb_app.c): the CALC drive's
 * files and the modules dropped on it, the microSD card given to the
 * computer, state keys. The device and the mass-storage units are below:
 * hal.h (hal_usb_*), core/usbdisk.h, core/vdrive.h.
 */
#ifndef USB_APP_H
#define USB_APP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* app_init: the staging buffer and the drive's memory from the arena (big
 * arena only; bytes taken). */
size_t usb_mem_init(void *mem, bool big);
/* app_tick: install what the computer dropped on the CALC drive. */
void usb_app_tick(uint32_t now);
/* MEM > USB status (diagnostics) */
void usb_app_status_open(void);
/* microSD tools > Connect card to computer */
void usb_app_card_open(void);
extern const struct screen scr_usbcard;
/* `state` keys usb_host, usb_result, usb_card, usb_pending */
void usb_app_state(void (*kv)(const char *, const char *, void *), void *user);

#endif
