/*
 * sdbk.h -- microSD tools (app/sdbk.c): PNG screenshots and session
 * backup / restore files in /CALC on the card. All return 0 or a negative
 * error and put a one-line result into msg.
 */
#ifndef SDBK_H
#define SDBK_H
#include <stddef.h>
int sdbk_screenshot(char *msg, size_t n);                    /* /CALC/SHOTnnn.PNG */
int sdbk_backup(const char *path, char *msg, size_t n);      /* session -> file */
int sdbk_restore(const char *path, char *msg, size_t n);     /* file -> session, restarts the app */
void sdbk_hotkey(void);                                      /* ALPHA MENU (app.c) */
int sdbk_console(int argc, char **argv);                     /* console `sdtool ...` */
/* "microSD: <error>." plus advice for errors the user can fix (no card,
 * no FAT, bad name, full), laid out for the message box */
void sdbk_card_error(int err, char *msg, size_t n);
void sdbk_state(void (*kv)(const char *, const char *, void *), void *user);
#endif
