/*
 * Alternate forms browser (app/forms_ui.c): the REWRITE menu's "forms"
 * item lists the equivalent forms the CAS command forms() finds for a
 * result; the chosen one becomes a new result. Interface for app_init (work
 * area), the home and RPN screens (forms_open with their paste callback),
 * the clipboard (copy) and the console `state`.
 */
#ifndef FORMS_UI_H
#define FORMS_UI_H
#include <stdbool.h>
#include <stddef.h>
#include "ui.h"

/* Takes the chosen form: in = the history input to show ("forms(Ans)"),
 * value = the form as CAS text. Called with the opening screen current. */
typedef void (*forms_paste_fn)(const char *in, const char *value);

extern const screen_t scr_forms;
/* Carve the work area (app_init); returns the bytes used. */
size_t forms_mem_init(void *work, bool big);
/* Evaluate forms(arg) and show the list (or a message if there is none). */
void forms_open(const char *arg, const char *disp, forms_paste_fn paste);
/* The selected form's display text (copy); false if the list is empty. */
bool forms_clip_text(char *out, size_t n);
void forms_state(void (*kv)(const char *, const char *, void *), void *user);
/* app/home.c, app/rpn.c: REWRITE "forms" for the current result / level 1 */
void home_result_forms(void);
void rpn_result_forms(void);
#endif
