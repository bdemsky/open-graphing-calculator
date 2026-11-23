/* Key event queue: producers are the keypad scanner (ISR on hw), the
 * simulator, and console key injection; the consumer is the main loop.
 *
 * A 128-entry ring of press/release events, protected by short interrupt-off
 * sections (hal_irq_save/restore), so it is safe with one producer in an ISR
 * and the consumer in thread mode. When the ring is nearly full new presses
 * are dropped (with their releases); releases of queued presses always fit. Key codes and names come from the generated keymap
 * (core/gen/keymap_gen.h, from keys/layout.py via tools/gen_keymap.py). */
#ifndef KEYQ_H
#define KEYQ_H
#include <stdbool.h>
#include <stdint.h>
#include "gen/keymap_gen.h"

typedef struct {
    uint8_t key;     /* enum key_code */
    uint8_t pressed; /* 1 = press, 0 = release */
} key_event_t;

void keyq_push(uint8_t key, bool pressed); /* ISR-safe; drops presses if nearly full */
bool keyq_pop(key_event_t *ev);            /* false if empty */
bool keyq_empty(void);
void keyq_clear(void);                     /* discard pending events */
/* Called (possibly from an ISR) when ON is pressed, e.g. to interrupt the CAS.
 * The hook runs from keyq_push itself, i.e. in the producer's context and
 * before the main loop sees the event, so a long CAS computation that never
 * returns to the main loop can still be stopped. Keep it short and ISR-safe
 * (app/app.c installs cas_break, which calls cas_interrupt()). */
void keyq_set_on_hook(void (*hook)(void));
/* The ON hook without a key event: a break from the console (Ctrl-C, byte
 * 0x03, on the USB serial port or the simulator's input), so a computer can
 * stop a running computation (the browser notebook's Interrupt). */
void keyq_break(void);

/* Look up a key by canonical name or alias (case-insensitive). KEY_NONE if unknown.
 * A "KEY_" prefix is accepted too ("KEY_ENTER" = "enter"). Used by the
 * console key-injection commands and the WebAssembly front end. */
int key_from_name(const char *name);

#endif
