/*
 * RPN mode (classic RPN style): a stack-based home screen that
 * replaces the algebraic home screen when MODE > ENTRY is RPN. See help/rpn.txt
 * for the user-level description and app/EXTENDING.md for the extension points
 * it uses (ui_set_home, mode row, persist blob).
 *
 * Storage: every stack level is a CAS value kept in a hidden CAS variable
 * _rpnN (assigned by the evaluation that produced it, _rpnN:=(...), so
 * uncertainty correlations, float digits and exact values survive stack
 * operations), plus its linear CAS text for display and editing. rpn_stack.c
 * stores one text entry per level, "N\x1fTEXT" (see app/rpn.c). The levels
 * live in one buffer carved from the application arena (the same SDRAM arena
 * the CAS gets its memory from, see app_init), oldest level first, each
 * NUL-terminated; level 1 is the last string. The undo snapshot is a second
 * buffer of the same size.
 */
#ifndef RPN_H
#define RPN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------ stack storage
 * rpn_stack.c, no UI or CAS dependencies (tests/unit/test_rpn.c). Levels are
 * numbered like on the screen: 1 = top (most recent). Functions returning bool
 * leave the stack unchanged when they fail (too few levels / buffer full). */
typedef struct {
    char *buf;       /* storage (not owned: carved from the arena by rpn_init); NULL = no stack */
    size_t cap, len; /* capacity of buf; bytes used, including the NULs */
    int depth;       /* number of levels (= number of NULs in buf[0..len)) */
} rstack_t;

void rs_init(rstack_t *s, char *buf, size_t cap); /* empty stack over buf (buf NULL: capacity 0) */
void rs_clear(rstack_t *s);                       /* drop every level (O(1)) */
const char *rs_get(const rstack_t *s, int level);   /* NULL if level < 1 or > depth */
size_t rs_free(const rstack_t *s);                  /* bytes still available */
bool rs_push(rstack_t *s, const char *text);         /* push text as the new level 1 */
bool rs_drop(rstack_t *s, int n);                    /* remove levels 1..n */
bool rs_pick(rstack_t *s, int n);                    /* push a copy of level n (DUP = 1, OVER = 2) */
bool rs_roll(rstack_t *s, int n);                    /* move level n to level 1 (SWAP = 2, ROT = 3) */
bool rs_rolld(rstack_t *s, int n);                   /* move level 1 to level n */
bool rs_replace(rstack_t *s, int level, const char *text); /* text must not point into s->buf */
bool rs_copy(rstack_t *dst, const rstack_t *src);    /* false if dst is too small */
/* Bytes of the top levels that fit in `max` bytes: the number of levels
 * (from level 1 up) whose texts together need <= max bytes. */
int rs_levels_within(const rstack_t *s, size_t max);

/* ------------------------------------------------------------ RPN screen */
/* Work area taken from the arena by app_init (stack + undo + scratch). The
 * small size is for the hardware's SDRAM-failure fallback arena. rpn_init
 * splits it into fixed scratch buffers (expression, CAS result, entry,
 * display formatting, LASTARG) and two equal halves for the stack and its undo copy;
 * a work area too small for the scratch buffers leaves RPN unavailable (the
 * MODE row then cannot switch it on). */
#define RPN_WORK (256u << 10)
#define RPN_WORK_SMALL (12u << 10)
void rpn_init(void *work, size_t size); /* resets the stack and selects algebraic entry */
bool rpn_active(void);                  /* MODE > ENTRY is RPN */
/* console `state` keys: rpn, rpn_depth, rpn_1..rpn_6 (display text of the
 * levels), rpn_line, rpn_sel, rpn_scroll, rpn_edit, rpn_error, rpn_vars (level
 * variables in use) and rpn_cas_vars (_rpnN variables defined in the CAS) */
void rpn_state(void (*kv)(const char *, const char *, void *), void *user);
/* Copy hook of the edit bar (app/input_clip.c): the selected level of the
 * interactive stack, else the command line, else level 1 (display text, as
 * on the screen). false if there is nothing to copy. */
bool rpn_clip_text(char *out, size_t n);
extern const struct screen scr_rpn;

/* Per-level rendering. The built-in renderer draws the display text in the
 * large font, right-aligned, wrapped onto up to RPN_TEXT_ROWS rows. A 2D
 * (textbook) renderer can replace it: `measure` returns the height in pixels
 * the value needs at width `w` (the screen clips values taller than the stack
 * area), or -1 to have this level drawn by the built-in renderer; `draw`
 * draws it into the box (x, y, w, h) with the given colours (the box is
 * already filled with `bg`; drawing is clipped to the stack area). `cas` is
 * the linear CAS text of the level, `disp` the display text after the MODE
 * number format and prettifying (√, π, ᴇ). NULL restores the built-in.
 * app_init installs m2d_rpn_renderer (app/m2d_home.c). */
typedef struct rpn_renderer {
    int (*measure)(const char *cas, const char *disp, int w);
    void (*draw)(const char *cas, const char *disp, int x, int y, int w, int h, uint16_t fg, uint16_t bg);
} rpn_renderer_t;
void rpn_set_renderer(const rpn_renderer_t *r);
extern const rpn_renderer_t m2d_rpn_renderer; /* textbook levels (Textbook display on) */
#define RPN_TEXT_ROWS 4 /* built-in renderer: longer values are cut with "..." */

#endif
