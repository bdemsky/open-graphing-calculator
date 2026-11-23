/* Key event ring buffer (see keyq.h). */
#include "keyq.h"
#include <ctype.h>
#include <string.h>
#include "hal.h"

#define KEYQ_SIZE 128 /* power of two (console `type` queues a whole line at once) */
#define KEYQ_RESERVE 8 /* slots only releases may use */
static volatile key_event_t q[KEYQ_SIZE];
static volatile uint64_t dropped; /* keys whose press was dropped (bit = key code) */
/* Free-running counters (never reduced modulo KEYQ_SIZE): head - tail is the
 * fill level even across the 2^32 wrap, and since KEYQ_SIZE divides 2^32 the
 * slot index `counter % KEYQ_SIZE` stays continuous across that wrap. */
static volatile uint32_t head, tail; /* head: write, tail: read */
static void (*volatile on_hook)(void);

/* Append an event. Interrupts are masked around the update because both the
 * keypad scan ISR and thread-mode code (console, simulator) may push. When
 * the queue is nearly full (the main loop is stuck or far behind) presses are
 * dropped, and so is the later release of a dropped press; the last
 * KEYQ_RESERVE slots are kept for the releases of queued presses, so the app
 * never sees a key stay down. The ON hook runs after the critical section,
 * with interrupts restored. */
void keyq_push(uint8_t key, bool pressed)
{
    uint32_t s = hal_irq_save();
    uint64_t bit = 1ULL << (key & 63);
    uint32_t room = pressed ? KEYQ_SIZE - KEYQ_RESERVE : KEYQ_SIZE;
    if (!pressed && (dropped & bit)) {
        dropped &= ~bit;
    } else if (head - tail < room) {
        q[head % KEYQ_SIZE].key = key;
        q[head % KEYQ_SIZE].pressed = pressed;
        head++;
        if (pressed)
            dropped &= ~bit;
    } else if (pressed) {
        dropped |= bit;
    }
    hal_irq_restore(s);
    if (key == KEY_ON && pressed && on_hook)
        on_hook();
}

bool keyq_pop(key_event_t *ev)
{
    uint32_t s = hal_irq_save();
    bool ok = head != tail;
    if (ok) {
        ev->key = q[tail % KEYQ_SIZE].key;
        ev->pressed = q[tail % KEYQ_SIZE].pressed;
        tail++;
    }
    hal_irq_restore(s);
    return ok;
}

/* Unlocked read: a snapshot that may be stale by the time it is used, which is
 * fine for the main loop's idle decision. */
bool keyq_empty(void) { return head == tail; }

void keyq_clear(void)
{
    uint32_t s = hal_irq_save();
    tail = head;
    hal_irq_restore(s);
}

void keyq_set_on_hook(void (*hook)(void)) { on_hook = hook; }
void keyq_break(void)
{
    if (on_hook)
        on_hook();
}

/* ASCII case-insensitive string equality. */
static int ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
        a++, b++;
    }
    return *a == *b;
}

/* Linear search of the generated tables: canonical names first (index 0 is
 * KEY_NONE), then aliases, then retry without a "KEY_" prefix. */
int key_from_name(const char *name)
{
    for (int k = 1; k < KEY_COUNT; k++)
        if (ieq(name, key_names[k]))
            return k;
    for (const key_alias_t *a = key_aliases; a->alias; a++)
        if (ieq(name, a->alias))
            return a->key;
    if (!strncmp(name, "KEY_", 4) || !strncmp(name, "key_", 4))
        return key_from_name(name + 4);
    return KEY_NONE;
}
