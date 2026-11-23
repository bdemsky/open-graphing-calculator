/*
 * Token line editor (interface and token model: app/editor.h).
 *
 * Three parts:
 *  - the token table: static_tokens[] (built-in keys whose display and CAS
 *    texts differ) and dyn[] (interned at runtime: CATALOG/menu words such
 *    as "factor(", unknown UTF-8 characters, 2D template markers). Ids:
 *    0..127 ASCII, 128.. static entries, 128 + NSTATIC.. dynamic entries.
 *  - editing primitives on editor_t (insert/overwrite, delete, cursor).
 *  - conversions: display text, CAS text (with implicit multiplication for
 *    "x(...)"), CAS output prettifying, and the text form used to save
 *    editors in the session.
 *
 * State: the token table and ed_insert_mode are globals shared by every
 * editor; nothing is allocated. Runs only in the main loop (no ISR use).
 */
#include "editor.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "font.h"

/* ------------------------------------------------------------ tokens */
typedef struct {
    const char *disp; /* UTF-8 text shown in the editor */
    const char *cas;  /* text sent to cas_eval() */
} token_t;

/* Capacity of the runtime (interned) token table, and the longest text + 1. */
#define TOK_DYN_MAX 96
#define TOK_DYN_TEXT 32
/* Built-in tokens. Their ids (128 + index) are not saved anywhere (sessions
 * store the display text), so entries may be added; tok_find_disp() looks
 * them up by display text. "→" maps to the marker byte 0x01, which the home
 * and RPN screens rewrite as an assignment ("expr→var" -> "var:=expr"). */
static const token_t static_tokens[] = {
    {"sin(", "sin("}, {"cos(", "cos("}, {"tan(", "tan("},
    {"sin⁻¹(", "asin("}, {"cos⁻¹(", "acos("}, {"tan⁻¹(", "atan("},
    {"log(", "log("}, {"ln(", "ln("}, {"10^(", "10^("}, {"e^(", "e^("},
    {"√(", "sqrt("}, {"²", "^2"}, {"³", "^3"}, {"⁻¹", "^(-1)"}, {"π", "(pi)"},
    {"ᴇ", "*10^"}, {"Ans", "(ans)"}, {"→", "\x01"}, {"θ", "theta"}, {"⁻", "-"},
    {"≠", "!="}, {"≥", ">="}, {"≤", "<="},
    /* del: ∇f, ∇·F, ∇×F, ∇²f (the CAS reads them, cas/src/parse.c) */
    {"∇", "∇"}, {"∇·", "∇·"}, {"∇×", "∇×"}, {"∇²", "∇²"},
    /* angle marks: their CAS text depends on the angle mode (see tok_cas) */
    {"°", "*(pi/180)"}, {"ʳ", ""},
};
#define NSTATIC ((int)(sizeof static_tokens / sizeof static_tokens[0]))

/* Runtime-interned tokens (ndyn used). Entries are never removed. */
static struct {
    char disp[TOK_DYN_TEXT];
    char cas[TOK_DYN_TEXT];
} dyn[TOK_DYN_MAX];
static int ndyn;

int TOK_ANS, TOK_STO, TOK_NEG, TOK_SQUARE, TOK_INV, TOK_PI, TOK_EE;
static int TOK_DEG, TOK_RAD;
bool ed_angle_deg;

/* Resolve the ids of the tokens the application inserts directly (Ans, →,
 * negation, ², ⁻¹, π, ᴇ) and of the two angle marks tok_cas() special-cases. */
void tok_init(void)
{
    TOK_ANS = tok_find_disp("Ans");
    TOK_STO = tok_find_disp("→");
    TOK_NEG = tok_find_disp("⁻");
    TOK_SQUARE = tok_find_disp("²");
    TOK_INV = tok_find_disp("⁻¹");
    TOK_PI = tok_find_disp("π");
    TOK_EE = tok_find_disp("ᴇ");
    TOK_DEG = tok_find_disp("°");
    TOK_RAD = tok_find_disp("ʳ");
}

/* One NUL-terminated 1-char string per ASCII id, so tok_disp() can return a
 * pointer that stays valid (callers keep several at a time). */
static char ascii_buf[128][2];

const char *tok_disp(int id)
{
    if (id < 128) {
        ascii_buf[id][0] = (char)id;
        ascii_buf[id][1] = 0;
        return ascii_buf[id];
    }
    id -= 128;
    if (id < NSTATIC)
        return static_tokens[id].disp;
    id -= NSTATIC;
    return id < ndyn ? dyn[id].disp : "?";
}

/* CAS text of a token. ASCII is itself; the angle marks depend on the
 * current angle mode (ed_angle_deg, set by app_apply_settings()). */
const char *tok_cas(int id)
{
    if (id < 128)
        return tok_disp(id);
    /* 30° is 30 degrees in either mode, 2ʳ is 2 radians. The CAS
     * has no degree operator, so convert to the current angle unit. */
    if (id == TOK_DEG)
        return ed_angle_deg ? "" : "*(pi/180)";
    if (id == TOK_RAD)
        return ed_angle_deg ? "*(180/pi)" : "";
    id -= 128;
    if (id < NSTATIC)
        return static_tokens[id].cas;
    id -= NSTATIC;
    return id < ndyn ? dyn[id].cas : "?";
}

/* Id of the token whose display text is exactly `disp` (single ASCII
 * characters map to themselves), searching static then dynamic entries;
 * -1 if there is none. */
int tok_find_disp(const char *disp)
{
    if (disp[0] && !disp[1] && (unsigned char)disp[0] < 128)
        return (unsigned char)disp[0];
    for (int i = 0; i < NSTATIC; i++)
        if (!strcmp(static_tokens[i].disp, disp))
            return 128 + i;
    for (int i = 0; i < ndyn; i++)
        if (!strcmp(dyn[i].disp, disp))
            return 128 + NSTATIC + i;
    return -1;
}

/* Find or add a token (see editor.h). An existing token with the same display
 * text is returned even if its CAS text differs. Returns '?' (an ASCII token)
 * when the table is full or a text does not fit, so callers always get a
 * valid id. */
int tok_intern(const char *disp, const char *cas)
{
    if (!cas)
        cas = disp;
    if (disp[0] && !disp[1] && (unsigned char)disp[0] < 128 && !strcmp(disp, cas))
        return (unsigned char)disp[0];
    int id = tok_find_disp(disp);
    if (id >= 0)
        return id;
    if (ndyn == TOK_DYN_MAX || strlen(disp) >= TOK_DYN_TEXT || strlen(cas) >= TOK_DYN_TEXT)
        return '?';
    strcpy(dyn[ndyn].disp, disp);
    strcpy(dyn[ndyn].cas, cas);
    return 128 + NSTATIC + ndyn++;
}

/* ------------------------------------------------------------ editing */
/* Insert (true, default) or overwrite mode; SHIFT ⌫ toggles it for every
 * editor, and it is saved with the settings. */
bool ed_insert_mode = true;

void (*ed_change_hook)(editor_t *e);

void ed_clear(editor_t *e)
{
    if (ed_change_hook)
        ed_change_hook(e);
    e->len = e->cur = e->scroll = 0;
}

bool ed_empty(const editor_t *e) { return e->len == 0; }

void ed_insert(editor_t *e, int tok)
{
    if (ed_change_hook)
        ed_change_hook(e);
    if (!ed_insert_mode && e->cur < e->len) {
        e->t[e->cur++] = (uint16_t)tok;
        return;
    }
    if (e->len >= ED_MAX)
        return;
    memmove(&e->t[e->cur + 1], &e->t[e->cur], (size_t)(e->len - e->cur) * sizeof e->t[0]);
    e->t[e->cur++] = (uint16_t)tok;
    e->len++;
}

/* Longest token whose display text prefixes s; returns its length (bytes).
 * Only multi-byte table entries take part in the longest match (a single
 * ASCII character is always its own token), so "sin(" typed as text becomes
 * the sin( token but "s" stays a letter. Characters outside the table are
 * interned one codepoint at a time. Requires *s != 0. */
static int match_token(const char *s, int *tok)
{
    int best = 0;
    for (int i = 0; i < NSTATIC; i++) {
        int l = (int)strlen(static_tokens[i].disp);
        if (l > best && !strncmp(s, static_tokens[i].disp, (size_t)l) && l > 1)
            best = l, *tok = 128 + i;
    }
    for (int i = 0; i < ndyn; i++) {
        int l = (int)strlen(dyn[i].disp);
        if (l > best && !strncmp(s, dyn[i].disp, (size_t)l) && l > 1)
            best = l, *tok = 128 + NSTATIC + i;
    }
    if (best)
        return best;
    if ((unsigned char)*s < 128) {
        *tok = (unsigned char)*s;
        return 1;
    }
    /* unknown UTF-8 character: intern it as its own token */
    const char *p = s;
    utf8_next(&p);
    char tmp[8];
    int l = (int)(p - s);
    memcpy(tmp, s, (size_t)l);
    tmp[l] = 0;
    *tok = tok_intern(tmp, tmp);
    return l;
}

/* Tokenise `text` greedily (match_token) and insert the tokens at the cursor.
 * keep_cursor: force insert mode and leave the cursor where it was, i.e.
 * before the inserted text (used for template suffixes such as ")"). */
static void insert_seq(editor_t *e, const char *text, bool keep_cursor)
{
    int start = e->cur;
    bool saved = ed_insert_mode;
    if (keep_cursor)
        ed_insert_mode = true;
    while (text && *text) {
        int tok;
        int l = match_token(text, &tok);
        ed_insert(e, tok);
        text += l;
    }
    ed_insert_mode = saved;
    if (keep_cursor)
        e->cur = start;
}

/* Insert menu/catalog text before the cursor and an optional suffix after it.
 * A word ending in "(" ("factor(", "normalcdf(") becomes a single token,
 * interned on first use, so ⌫ removes it whole. The suffix is always
 * inserted (never overwrites), even in overwrite mode. */
void ed_insert_text(editor_t *e, const char *text, const char *suffix)
{
    if (text) {
        /* whole-word texts like "factor(" become one token */
        int l = (int)strlen(text);
        bool word = l > 1 && isalpha((unsigned char)text[0]) && text[l - 1] == '(';
        if (word) {
            int t = tok_find_disp(text);
            if (t < 0)
                t = tok_intern(text, text);
            if (t == '?') /* token table full: insert the characters */
                insert_seq(e, text, false);
            else
                ed_insert(e, t);
        } else {
            insert_seq(e, text, false);
        }
    }
    if (suffix && *suffix) {
        bool saved = ed_insert_mode;
        ed_insert_mode = true;
        insert_seq(e, suffix, true);
        ed_insert_mode = saved;
    }
}

void ed_set_text(editor_t *e, const char *text)
{
    ed_clear(e);
    bool saved = ed_insert_mode;
    ed_insert_mode = true;
    insert_seq(e, text, false);
    ed_insert_mode = saved;
}

/* Delete the token under the cursor; at the end of the line delete the last
 * token instead (and the cursor ends up at the new end). */
void ed_delete(editor_t *e)
{
    if (e->cur >= e->len) {
        if (e->len == 0)
            return;
        if (ed_change_hook)
            ed_change_hook(e);
        e->cur = e->len - 1; /* at end: delete the last token */
    } else if (ed_change_hook) {
        ed_change_hook(e);
    }
    memmove(&e->t[e->cur], &e->t[e->cur + 1], (size_t)(e->len - e->cur - 1) * sizeof e->t[0]);
    e->len--;
}

/* ⌫: the token before the cursor; at the start of the line the one under it */
void ed_backspace(editor_t *e)
{
    if (e->cur > 0)
        e->cur--;
    ed_delete(e);
}

void ed_left(editor_t *e)
{
    if (e->cur > 0)
        e->cur--;
}
void ed_right(editor_t *e)
{
    if (e->cur < e->len)
        e->cur++;
}
void ed_home(editor_t *e) { e->cur = 0; }
void ed_end(editor_t *e) { e->cur = e->len; }

/* ------------------------------------------------------------ conversions */
/* Append s at out[*o], truncating to fit in n bytes (n > 0, *o < n), and
 * keep the buffer NUL-terminated; *o advances by the bytes copied. */
static void append(char *out, size_t n, size_t *o, const char *s)
{
    size_t l = strlen(s);
    if (*o + l >= n)
        l = n - 1 - *o;
    memcpy(out + *o, s, l);
    *o += l;
    out[*o] = 0;
}

bool (*ed_struct_hook)(const editor_t *e, char *out, size_t n, bool cas);

/* Display text: concatenation of the tokens' display texts (or the 2D
 * template hook's linear form). Truncated to n bytes. */
void ed_display(const editor_t *e, char *out, size_t n)
{
    if (ed_struct_hook && ed_struct_hook(e, out, n, false))
        return;
    size_t o = 0;
    out[0] = 0;
    for (int i = 0; i < e->len; i++)
        append(out, n, &o, tok_disp(e->t[i]));
}

/* True if position i holds a plain ASCII letter token (false out of range). */
static bool is_letter_tok(const editor_t *e, int i)
{
    return i >= 0 && i < e->len && e->t[i] < 128 && isalpha(e->t[i]);
}

bool (*ed_function_hook)(const char *name);

/* "f(...)" followed by ":=" defines a function. `open` is the index of the
 * '(' token; the matching ')' is found by depth counting. */
static bool defines_function(const editor_t *e, int open)
{
    int depth = 0;
    for (int i = open; i < e->len; i++) {
        if (e->t[i] == '(')
            depth++;
        else if (e->t[i] == ')' && --depth == 0)
            return i + 2 < e->len && e->t[i + 1] == ':' && e->t[i + 2] == '=';
    }
    return false;
}

/* "(" at `open` holds a comma at its own depth: an argument list ("u(x,t)"), not a factor */
static bool has_args(const editor_t *e, int open)
{
    int depth = 0;
    for (int i = open; i < e->len; i++) {
        if (e->t[i] == '(' || e->t[i] == '[')
            depth++;
        else if ((e->t[i] == ')' || e->t[i] == ']') && --depth == 0)
            return false;
        else if (e->t[i] == ',' && depth == 1)
            return true;
    }
    return false;
}

/* the one-letter name `c` appears with a prime ("y'"): the unknown function of an ODE */
static bool primed(const editor_t *e, int c)
{
    for (int j = 0; j + 1 < e->len; j++)
        if (e->t[j] == c && !is_letter_tok(e, j - 1) && e->t[j + 1] == '\'')
            return true;
    return false;
}

/* CAS text: concatenation of the tokens' CAS texts, with one rewrite. A
 * one-letter name directly followed by "(" ("x(x+1)") means multiplication
 * and gets an explicit "*", unless the name is a user function
 * (ed_function_hook, app/vars.c), the line defines it ("f(x):=..."), the
 * parentheses hold several arguments ("u(x,t)", an unknown function of a
 * PDE) or the line also has the name with a prime (the initial condition
 * "y(0)=3" of dsolve([y'=2*y,y(0)=3],y,x)). Longer names ("ab(") are left
 * alone: the CAS treats them as calls. */
void ed_cas(const editor_t *e, char *out, size_t n)
{
    if (ed_struct_hook && ed_struct_hook(e, out, n, true))
        return;
    size_t o = 0;
    out[0] = 0;
    for (int i = 0; i < e->len; i++) {
        /* single-letter variable followed by '(' means multiplication: x(x+1),
         * unless it is a user function (call or definition) */
        if (e->t[i] == '(' && is_letter_tok(e, i - 1) && !is_letter_tok(e, i - 2)) {
            char name[2] = {(char)e->t[i - 1], 0};
            if (!defines_function(e, i) && !has_args(e, i) && !primed(e, e->t[i - 1]) &&
                !(ed_function_hook && ed_function_hook(name)))
                append(out, n, &o, "*");
        }
        append(out, n, &o, tok_cas(e->t[i]));
    }
}

/* Display column (in codepoints, one per font cell) at which token `tokpos`
 * starts. Used to place the cursor and to scroll single-line views. */
int ed_col_of(const editor_t *e, int tokpos)
{
    int col = 0;
    for (int i = 0; i < tokpos && i < e->len; i++)
        col += utf8_len(tok_disp(e->t[i]));
    return col;
}

int ed_cursor_col(const editor_t *e) { return ed_col_of(e, e->cur); }

uint32_t ed_char_at_cursor(const editor_t *e)
{
    if (e->cur >= e->len)
        return ' ';
    const char *s = tok_disp(e->t[e->cur]);
    return utf8_next(&s);
}

/* CAS output -> display text (see editor.h). One left-to-right pass: at each
 * position try the replacements in map[] in order ("infinity" before "inf");
 * `word` entries only match when not part of a longer identifier, checked on
 * the already-written output before and the input after. */
void pretty_from_cas(const char *in, char *out, size_t n)
{
    static const struct {
        const char *from, *to;
        bool word;
    } map[] = {
        {"sqrt(", "√(", false}, {"pi", "π", true}, {"theta", "θ", true}, {"infinity", "∞", true},
        {"inf", "∞", true},
    };
    size_t o = 0;
    out[0] = 0;
    while (*in && o + 1 < n) {
        bool done = false;
        for (size_t k = 0; k < sizeof map / sizeof map[0]; k++) {
            size_t l = strlen(map[k].from);
            if (strncmp(in, map[k].from, l))
                continue;
            if (map[k].word) {
                bool before = o > 0 && (isalnum((unsigned char)out[o - 1]) || out[o - 1] == '_');
                bool after = isalnum((unsigned char)in[l]) || in[l] == '_';
                if (before || after)
                    continue;
            }
            append(out, n, &o, map[k].to);
            in += l;
            done = true;
            break;
        }
        if (!done) {
            out[o++] = *in++;
            out[o] = 0;
        }
    }
}

/* ------------------------------------------------------------ persistence */
/* Save form of a line (format in editor.h): static tokens are saved by
 * display text only (their CAS text is fixed by the firmware), interned ones
 * with both texts so they can be re-interned on load. Stops before a token
 * that would not fit, so the output never ends inside a token. */
void ed_serialize(const editor_t *e, char *out, size_t n)
{
    size_t o = 0;
    out[0] = 0;
    for (int i = 0; i < e->len; i++) {
        int t = e->t[i];
        char tmp[TOK_DYN_TEXT * 2 + 8];
        if (t < 128)
            tmp[0] = (char)t, tmp[1] = 0;
        else if (t - 128 < NSTATIC)
            snprintf(tmp, sizeof tmp, "\x01%s\x03", tok_disp(t));
        else
            snprintf(tmp, sizeof tmp, "\x01%s\x02%s\x03", tok_disp(t), tok_cas(t));
        if (o + strlen(tmp) >= n)
            break; /* whole tokens only */
        append(out, n, &o, tmp);
    }
}

/* Inverse of ed_serialize(): rebuilds the token sequence (interning tokens
 * as needed, so ids may differ from the saved session) with the cursor at the
 * end. Tolerates malformed input: unterminated records end at the string's
 * end, overlong texts are truncated. */
void ed_deserialize(editor_t *e, const char *s)
{
    ed_clear(e);
    bool saved = ed_insert_mode;
    ed_insert_mode = true;
    while (*s) {
        if (*s != '\x01') {
            if ((unsigned char)*s < 128) {
                ed_insert(e, (unsigned char)*s++);
            } else { /* stray UTF-8: tokenise like typed text */
                int tok;
                s += match_token(s, &tok);
                ed_insert(e, tok);
            }
            continue;
        }
        const char *d = ++s;
        while (*s && *s != '\x02' && *s != '\x03')
            s++;
        char disp[TOK_DYN_TEXT], cas[TOK_DYN_TEXT];
        size_t dl = (size_t)(s - d) < sizeof disp - 1 ? (size_t)(s - d) : sizeof disp - 1;
        memcpy(disp, d, dl);
        disp[dl] = 0;
        int tok;
        if (*s == '\x02') {
            const char *c = ++s;
            while (*s && *s != '\x03')
                s++;
            size_t cl = (size_t)(s - c) < sizeof cas - 1 ? (size_t)(s - c) : sizeof cas - 1;
            memcpy(cas, c, cl);
            cas[cl] = 0;
            tok = tok_intern(disp, cas);
        } else {
            tok = tok_find_disp(disp);
            if (tok < 0)
                tok = tok_intern(disp, disp);
        }
        if (*s == '\x03')
            s++;
        ed_insert(e, tok);
    }
    ed_insert_mode = saved;
    e->cur = e->len;
}
