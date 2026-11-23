/*
 * Token-based line editor ("sin(" is one token, deleted as a unit).
 * Token ids < 128 are plain ASCII characters; ids >= 128 index a token table
 * of {display text, CAS text} pairs (static entries + ones interned at runtime).
 *
 * Every text field of the application is an editor_t: the home entry line,
 * the Y= functions, WINDOW/TBLSET fields, form fields, the RPN command line.
 * An editor stores token ids, not characters, so a function name such as
 * "sin(" moves and deletes as a unit, and the same token can show one text
 * ("√(", "π", "Ans") while sending another to the CAS ("sqrt(", "(pi)",
 * "(ans)"). ed_display() builds the UTF-8 text a screen draws, ed_cas() the
 * text handed to cas_eval(). Cursor positions are token indices; screens
 * convert them to character columns with ed_col_of() / ed_cursor_col().
 *
 * The token table is global (one per firmware): static entries in editor.c
 * plus up to 96 entries interned at runtime (CATALOG words such as
 * "factor(", unknown UTF-8 characters, 2D-math template markers from
 * app/m2d_edit.c). Ids of interned tokens depend on interning order, which is
 * why sessions are saved with ed_serialize() (text) and not as raw ids.
 *
 * Fixed size (ED_MAX tokens), no allocation; insertion beyond the capacity
 * is silently ignored.
 */
#ifndef EDITOR_H
#define EDITOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ED_MAX 240

/* One editable line. Invariant: 0 <= cur <= len <= ED_MAX. */
typedef struct {
    uint16_t t[ED_MAX]; /* token ids (see above) */
    int len;            /* number of tokens in t[] */
    int cur;   /* cursor: insert position (0..len) */
    int scroll;/* first visible column for single-line views */
} editor_t;

/* Token table. tok_intern() returns the id of a token with this display text,
 * adding it (with CAS text `cas`, NULL = same as disp) if it is new; when the
 * dynamic table is full or a text is longer than 31 bytes it returns '?'.
 * tok_disp()/tok_cas() return the token's texts ("?" for an unknown id); the
 * returned strings are static and stay valid. TOK_* hold the ids of the
 * tokens the application refers to directly (set by tok_init()). */
int tok_intern(const char *disp, const char *cas); /* returns id (ASCII for 1-char same text) */
const char *tok_disp(int id);
const char *tok_cas(int id);
int tok_find_disp(const char *disp); /* -1 if unknown */
extern int TOK_ANS, TOK_STO, TOK_NEG, TOK_SQUARE, TOK_INV, TOK_PI, TOK_EE;
void tok_init(void); /* look up the TOK_* ids (once, from app_init) */
/* Angle mode for the ° and ʳ tokens' CAS text (set by the app's settings). */
extern bool ed_angle_deg;

/* Editing (insert vs overwrite mode is global: ed_insert_mode) */
extern bool ed_insert_mode;
void ed_clear(editor_t *e);
/* Insert token `tok` at the cursor and move past it; in overwrite mode
 * replace the token under the cursor (append at the end). */
void ed_insert(editor_t *e, int tok);
/* Insert display text (tokenised greedily against the table); `suffix` is inserted
 * after the cursor without moving it (template completion). Either may be NULL. */
void ed_insert_text(editor_t *e, const char *text, const char *suffix);
void ed_set_text(editor_t *e, const char *text); /* replace the line; cursor at the end */
void ed_delete(editor_t *e); /* delete token at cursor (or before cursor at end) */
void ed_backspace(editor_t *e); /* ⌫ key: token before the cursor (at the start: under it) */
/* Cursor movement by one token, to the start and to the end. */
void ed_left(editor_t *e);
void ed_right(editor_t *e);
void ed_home(editor_t *e);
void ed_end(editor_t *e);
bool ed_empty(const editor_t *e);

/* Conversions */
void ed_display(const editor_t *e, char *out, size_t n); /* UTF-8 display text */
void ed_cas(const editor_t *e, char *out, size_t n);     /* text for cas_eval */
/* Set by the app: true if `name` is a user function, so "f(2)" is a call and
 * not f*(2) (one-letter names followed by '(' multiply otherwise). */
extern bool (*ed_function_hook)(const char *name);
int ed_cursor_col(const editor_t *e);                     /* codepoint column of the cursor */
int ed_col_of(const editor_t *e, int tokpos);             /* codepoint column of token `tokpos` */
/* First codepoint of the token under the cursor (or ' ' at end). */
uint32_t ed_char_at_cursor(const editor_t *e);

/* Persistence: the token sequence as text that keeps token boundaries (so a
 * restored "factor(" is still one token). ASCII tokens are themselves; others
 * are "\x01DISP\x03" (static) or "\x01DISP\x02CAS\x03" (interned). */
void ed_serialize(const editor_t *e, char *out, size_t n);
void ed_deserialize(editor_t *e, const char *s);

/* Structured editors (2D templates, app/m2d_edit.c): when set, ed_display()
 * and ed_cas() call it first; it returns true if it produced the text
 * (the line contains template markers). */
extern bool (*ed_struct_hook)(const editor_t *e, char *out, size_t n, bool cas);

/* Called with an editor just before ed_insert(), ed_delete() or ed_clear()
 * (and so ed_set_text(), ed_insert_text(), ed_deserialize()) change it; the
 * undo log (app/input_undo.c) snapshots the line there. NULL: no hook. */
extern void (*ed_change_hook)(editor_t *e);

/* CAS output -> display ("sqrt(" -> "√(", "pi" -> "π", ...). Names are only
 * replaced as whole words ("pi" in "spin" stays). `out` is always terminated. */
void pretty_from_cas(const char *in, char *out, size_t n);

#endif
