/*
 * Base-n app (programmer's calculator): an entry line evaluated with integer
 * semantics in a word size of 8/16/32/64 bits, signed or unsigned, and the
 * result shown in HEX, DEC, OCT and BIN. {▲}/{▼} select the input base: plain
 * numbers typed are read in that base (0x.. 0b.. 0o.. always work). Soft keys
 * F1..F4 (hex dec oct bin) and a tap on a base's row also select the input
 * base; F5 (bits) opens the BITS menu (and, or, xor, not, shifts, rotations,
 * word size, signed/unsigned). Uses the CAS bit functions (bitand, ...,
 * wordsize). Like on the home screen, an entry that starts with an operator
 * (+ - * / ^, and/or from BITS) starts with Ans, which here is the app's own
 * last result (subst_ans); hints under the results say what to type and, in
 * HEX, where the digits a-f are.
 *
 * How an entry is evaluated (bn_eval):
 *   1. The entry's CAS text gets its bare literals prefixed for the input base
 *      ("ff and f0" in HEX becomes "0xff and 0xf0"), so the CAS, which only
 *      knows prefixed literals, reads them correctly.
 *   2. wordsize(bits, signed) sets the CAS's global word size, and
 *      bitor(floor(expr), 0) evaluates the expression, truncates it to an
 *      integer and wraps it to the word (bitor with 0 is the identity apart
 *      from the wrapping; signed words give negative results). The previous
 *      word size is restored afterwards and the evaluation is quiet, so the
 *      home screen's wordsize and `ans` are not changed.
 *   3. The decimal result is parsed back into a uint64_t and kept as the
 *      two's-complement bit pattern of the word, from which all four bases are
 *      formatted on every redraw.
 * Integer arithmetic therefore comes from the CAS (exact big integers), and
 * this file only does the base conversions and the display.
 *
 * State: the struct bn (settings and last result) and the entry editor bed,
 * saved as the session blob persist_basen ("basen", version 1). The app is
 * registered in app_registry (APPS > Base-n); help topic "basen".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "registry.h"
#include "session.h"
#include "ui.h"
#include "xform.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

/* Bases in display order (also the soft-key order F1..F4). */
enum { B_HEX, B_DEC, B_OCT, B_BIN };
static const int base_radix[4] = {16, 10, 8, 2};
static const char *const base_name[4] = {"HEX", "DEC", "OCT", "BIN"};

static struct {
    uint8_t bits, is_signed, base; /* word size 1..64 (the menu offers 8/16/32/64), signedness, input base B_* */
    bool have;                     /* val holds a result */
    uint64_t val; /* last result, two's complement in `bits` */
    char last[96]; /* last entry (display) */
    char err[64];
} bn = {32, 1, B_DEC};
static editor_t bed; /* entry line */

/* Mask of the low `bits` bits (special case 64: 1ull << 64 is undefined). */
static uint64_t mask(void) { return bn.bits >= 64 ? ~0ull : ((1ull << bn.bits) - 1); }

/* Format the word v (masked to the word size) in radix 16, 10, 8 or 2.
 * HEX/OCT/BIN show the raw two's-complement bit pattern without leading
 * zeros; DEC shows the signed value when the word is signed and its top bit is
 * set (pattern - 2^bits), else the unsigned value. */
static void fmt_base(uint64_t v, int radix, char *out, size_t n)
{
    char tmp[72];
    int k = 0;
    size_t o = 0;
    v &= mask();
    /* DEC by hand too: newlib-nano prints no long long */
    if (radix == 10 && bn.is_signed && bn.bits && (v >> (bn.bits - 1)) & 1) {
        v = (~v + 1) & mask(); /* magnitude of pattern - 2^bits */
        if (n > 1)
            out[o++] = '-';
    }
    do {
        tmp[k++] = "0123456789ABCDEF"[v % (unsigned)radix];
        v /= (unsigned)radix;
    } while (v && k < 70);
    while (k > 0 && o + 1 < n)
        out[o++] = tmp[--k];
    out[o] = 0;
}

static bool hexdig(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static bool idch(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

/* bare literals in the input base -> 0x/0o/0b literals
 *
 * Scans CAS text word by word (a word = run of letters, digits and '_' not
 * preceded by one). In HEX, OCT and BIN a word that consists only of digits of
 * the base (in HEX also a-f/A-F, so "ff" or "add" are numbers) and is not a
 * function call is prefixed; already prefixed literals, other words such as
 * "and" or "shl(" and everything in DEC are copied unchanged. Returns false
 * when a number has a digit outside the base (e.g. 2 in BIN) or out[] (size n)
 * is too small. */
static bool prefix_literals(const char *s, char *out, size_t n)
{
    static const char pfx[4] = {'x', 0, 'o', 'b'};
    size_t o = 0;
    while (*s && o + 4 < n) {
        if (idch(*s) && (o == 0 || !idch(out[o - 1]))) {
            size_t l = 0;
            bool num = true;
            while (idch(s[l]))
                num &= bn.base == B_HEX ? hexdig(s[l]) : (s[l] >= '0' && s[l] <= '9'), l++;
            if (s[0] == '0' && l > 1 && (s[1] == 'x' || s[1] == 'b' || s[1] == 'o'))
                num = false; /* already prefixed */
            if (num && s[l] != '(' && bn.base != B_DEC) {
                for (size_t i = 0; i < l; i++)
                    if (bn.base != B_HEX && s[i] - '0' >= base_radix[bn.base])
                        return false;
                if (o + l + 3 >= n)
                    return false;
                out[o++] = '0';
                out[o++] = pfx[bn.base];
            }
            while (l-- && o + 1 < n)
                out[o++] = *s++;
            continue;
        }
        out[o++] = *s++;
    }
    out[o] = 0;
    return true;
}

/* Replace every word "ans" of CAS text s (Ans: the last result of this app,
 * not the home screen's) by the last result in decimal, in parentheses;
 * decimal literals are read as decimal in every input base because the
 * prefixing has already been done. Returns false if s uses ans but there is
 * no result yet, or out (n bytes) is too small. */
static bool subst_ans(const char *s, char *out, size_t n)
{
    char v[32];
    size_t o = 0;
    fmt_base(bn.val, 10, v, sizeof v);
    while (*s) {
        if (!strncmp(s, "ans", 3) && (o == 0 || !idch(out[o - 1])) && !idch(s[3])) {
            if (!bn.have || o + strlen(v) + 3 >= n)
                return false;
            o += (size_t)snprintf(out + o, n - o, "(%s)", v);
            s += 3;
            continue;
        }
        if (o + 1 >= n)
            return false;
        out[o++] = *s++;
    }
    out[o] = 0;
    return true;
}

/* ENTER: evaluate the entry (steps in the file header). Remembers the entry's
 * display text in bn.last; on success stores the result in bn.val and clears
 * the entry, otherwise sets bn.err and keeps the entry for correction.
 * No side effects on the CAS: wordsize() is queried first ("[64,true]") and
 * set back to that afterwards, and the evaluations are quiet (ans unchanged). */
static void bn_eval(void)
{
    char cas[ED_MAX * 4], lit0[ED_MAX * 4 + 64], lit[ED_MAX * 4 + 96], cmd[ED_MAX * 4 + 128], out[96];
    ed_cas(&bed, cas, sizeof cas);
    ed_display(&bed, bn.last, sizeof bn.last);
    bn.err[0] = 0;
    if (!prefix_literals(cas, lit0, sizeof lit0)) {
        snprintf(bn.err, sizeof bn.err, "ERR: digit not in %s (▲▼: input base)", base_name[bn.base]);
        return;
    }
    if (!subst_ans(lit0, lit, sizeof lit)) {
        snprintf(bn.err, sizeof bn.err, "ERR: no result yet for Ans");
        return;
    }
    char prev[32] = "";
    if (cas_eval_quiet(g.cas, "wordsize()", out, sizeof out) == CAS_OK && out[0] == '[' &&
        strlen(out) < sizeof prev)
        snprintf(prev, sizeof prev, "%.*s", (int)strlen(out) - 2, out + 1); /* "[64,true]" -> "64,true" */
    snprintf(cmd, sizeof cmd, "wordsize(%d,%s)", bn.bits, bn.is_signed ? "true" : "false");
    cas_eval_quiet(g.cas, cmd, out, sizeof out);
    snprintf(cmd, sizeof cmd, "bitor(floor(%s),0)", lit);
    cas_status st = cas_eval_quiet(g.cas, cmd, out, sizeof out);
    if (st != CAS_OK) /* before the restore, which replaces the CAS's last error */
        snprintf(bn.err, sizeof bn.err, "ERR: %s", cas_last_error(g.cas));
    if (prev[0]) {
        char restore[48], tmp[32];
        snprintf(restore, sizeof restore, "wordsize(%s)", prev);
        cas_eval_quiet(g.cas, restore, tmp, sizeof tmp);
    }
    if (st != CAS_OK)
        return;
    /* The result fits the word, so it fits int64 (signed) or uint64
     * (unsigned); a negative value cast to uint64_t is its two's complement. */
    char *end;
    if (out[0] == '-')
        bn.val = (uint64_t)strtoll(out, &end, 10);
    else
        bn.val = strtoull(out, &end, 10);
    if (*end) {
        snprintf(bn.err, sizeof bn.err, "ERR: not an integer");
        return;
    }
    bn.have = true;
    ed_clear(&bed);
}

/* Draw the whole body. Layout from the top (y in pixels):
 *   BODY_Y      16 px grey bar: word size, signedness, input base
 *   +20         the last entry (dark grey)
 *   +38         HEX, DEC, OCT rows: label at x 4, value in the 10x20 font at
 *               x 40, 22 px each; BIN row: 36 px, up to 64 digits grouped in
 *               nibbles in the small font on two lines (the last 32 digits on
 *               the second line); the input base's row is highlighted
 *   below       error message (red), if any
 *   UI_BOTTOM - CH   the entry line with the cursor */
static void bn_draw(void)
{
    ui_cursor_hide();
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    char b[96];
    snprintf(b, sizeof b, "%d-bit %s   input %s", bn.bits, bn.is_signed ? "signed" : "unsigned", base_name[bn.base]);
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, 16, C_LTGRAY);
    gfx_text(&font_medium, 4, BODY_Y + 1, b, C_BLACK, GFX_TRANSPARENT);
    int y = BODY_Y + 20;
    gfx_text(&font_medium, 4, y, bn.last, C_DKGRAY, GFX_TRANSPARENT);
    y += 18;
    for (int k = 0; k < 4; k++) {
        bool on = k == bn.base;
        if (on)
            gfx_fill_rect(0, y - 1, LCD_WIDTH, k == B_BIN ? 2 * 16 + 2 : CH, C_SEL_BG);
        gfx_text(&font_medium, 4, y + 3, base_name[k], on ? C_WHITE : C_GRAY, GFX_TRANSPARENT);
        if (bn.have)
            fmt_base(bn.val, base_radix[k], b, sizeof b);
        else
            b[0] = 0;
        if (k == B_BIN) {
            /* up to 64 digits: two lines of the small font, grouped in nibbles */
            char grp[100];
            size_t L = strlen(b), o = 0;
            for (size_t i = 0; i < L; i++) {
                if (i && (L - i) % 4 == 0)
                    grp[o++] = ' ';
                grp[o++] = b[i];
            }
            grp[o] = 0;
            /* more than 48 characters do not fit one line: the second line
             * gets the last 40 characters (8 nibbles + separators = 32 bits) */
            size_t cut = o > 48 ? o - 40 : 0;
            if (cut) {
                char first[64];
                snprintf(first, sizeof first, "%.*s", (int)cut, grp);
                gfx_text(&font_small, 40, y + 1, first, on ? C_WHITE : C_BLACK, GFX_TRANSPARENT);
            }
            gfx_text(&font_small, 40, y + (cut ? 17 : 1), grp + cut, on ? C_WHITE : C_BLACK, GFX_TRANSPARENT);
            y += 2 * 16 + 4;
        } else {
            gfx_text(FONT, 40, y, b, on ? C_WHITE : C_BLACK, GFX_TRANSPARENT);
            y += CH + 2;
        }
    }
    if (bn.err[0])
        gfx_text(&font_medium, 4, y + 2, bn.err, C_RED, GFX_TRANSPARENT);
    else { /* hints: what to type, and in HEX where the digits a-f are */
        if (!bn.have)
            gfx_text(&font_medium, 4, y + 2, "type a number or expression, ENTER", C_GRAY, GFX_TRANSPARENT);
        else
            gfx_text(&font_medium, 4, y + 2, "start with + - and or ... to use the result", C_GRAY, GFX_TRANSPARENT);
        if (bn.base == B_HEX)
            gfx_text(&font_medium, 4, y + 18, "a-f: alpha + CAS math vars x sin cos", C_GRAY, GFX_TRANSPARENT);
    }
    ui_draw_editor_line(&bed, 0, UI_BOTTOM - CH, COLS, C_BLACK, C_WHITE, true);
}

/* BITS menu: items with action 0 insert an operator or a function template
 * into the entry; the others change the word size or toggle signedness.
 * Changing the word does not re-evaluate: the stored pattern is shown masked
 * to the new size until the next ENTER. */
enum { A_W8 = 1, A_W16, A_W32, A_W64, A_SIGN };
static const menu_item_t bits_items[] = {
    {"and", " and ", NULL, 0, "bitand"},     {"or", " or ", NULL, 0, "bitor"},
    {"xor(", "xor(", ")", 0, "bitxor"},      {"not", "not ", NULL, 0, "bitnot"},
    {"shl( x,n)", "shl(", ")", 0, "shl"},    {"shr( x,n)", "shr(", ")", 0, "shr"},
    {"rotl( x,n)", "rotl(", ")", 0, "rotl"}, {"rotr( x,n)", "rotr(", ")", 0, "rotr"},
    {"8-bit words", NULL, NULL, A_W8, "basen"},   {"16-bit words", NULL, NULL, A_W16, "basen"},
    {"32-bit words", NULL, NULL, A_W32, "basen"}, {"64-bit words", NULL, NULL, A_W64, "basen"},
    {"signed / unsigned", NULL, NULL, A_SIGN, "basen"},
};
/* on_select of the BITS menu: apply the item, then return to the base-n
 * screen as a "returning" switch (the menu is replaced by the screen). */
static void bits_select(const menu_item_t *it)
{
    static const uint8_t w[] = {0, 8, 16, 32, 64};
    if (it->action == A_SIGN)
        bn.is_signed = !bn.is_signed;
    else if (it->action)
        bn.bits = w[it->action];
    else {
        if (ed_empty(&bed) && bn.have && (!strcmp(it->insert, " and ") || !strcmp(it->insert, " or ")))
            ed_insert(&bed, TOK_ANS); /* an operator first works on the result, as on the home screen */
        ed_insert_text(&bed, it->insert, it->suffix);
    }
    ui_returning = true;
    extern const screen_t scr_basen;
    ui_switch(&scr_basen);
    ui_returning = false;
}
static const menu_def_t menu_bits = {"BITS", bits_items, sizeof bits_items / sizeof bits_items[0], bits_select, false,
                                     "basen"};

/* Keys: F1..F4 select the input base, F5 opens BITS, UP/DOWN cycle the input
 * base, ENTER evaluates a non-empty entry, CLEAR clears the entry or (when
 * empty) returns home; everything else goes to the line editor. Keys the
 * editor does not use are ignored without a redraw. */
static void bn_key(int key, int mod)
{
    if (KEY_IS_SOFT(key) && mod == MOD_NONE) { /* soft keys: hex dec oct bin, bits */
        if (key == KEY_F5) {
            menu_open(&menu_bits);
            return;
        }
        bn.base = (uint8_t)KEY_SOFT_INDEX(key);
        bn_draw();
        return;
    }
    switch (key) {
    case KEY_UP: bn.base = (uint8_t)((bn.base + 3) % 4); break;
    case KEY_DOWN: bn.base = (uint8_t)((bn.base + 1) % 4); break;
    case KEY_ENTER:
        if (!ed_empty(&bed))
            bn_eval();
        break;
    case KEY_CLEAR:
        if (ed_empty(&bed)) {
            ui_switch(ui_home());
            return;
        }
        ed_clear(&bed);
        break;
    default: {
        char t[16];
        if (ed_empty(&bed) && bn.have && mod == MOD_NONE && ui_key_text(key, mod, 0, t, sizeof t) &&
            (!strcmp(t, "+") || !strcmp(t, "-") || !strcmp(t, "*") || !strcmp(t, "/") || !strcmp(t, "^")))
            ed_insert(&bed, TOK_ANS); /* an operator first works on the result, as on the home screen */
        if (!ui_editor_key(&bed, key, mod))
            return;
    }
    }
    bn_draw();
}

/* Touch: a tap on the HEX, DEC, OCT or BIN row (geometry of bn_draw) makes
 * it the input base. */
static void bn_touch(const touch_event_t *ev)
{
    if (ev->type != TEV_TAP)
        return;
    int y = BODY_Y + 37;
    for (int k = 0; k < 4; k++) {
        int h = k == B_BIN ? 2 * 16 + 4 : CH + 2;
        if (ev->y >= y && ev->y < y + h) {
            bn.base = (uint8_t)k;
            bn_draw();
            return;
        }
        y += h;
    }
}
static editor_t *bn_editor(void) { return &bed; } /* MATH/CATALOG/VARS insert here */
static void bn_enter(void) {}                      /* state is kept between visits */
/* Labels only: the keys reach bn_key() as KEY_F1..KEY_F5. */
static const softkeys_t bn_soft = {{{"hex"}, {"dec"}, {"oct"}, {"bin"}, {"bits"}}};
const screen_t scr_basen = {"basen", "BASE-N", bn_enter, bn_draw, bn_key, NULL, bn_editor, bn_touch, "basen", &bn_soft};

static void bn_open(void) { ui_switch(&scr_basen); }
const app_def_t app_basen = {"basen", "Base-n", bn_open, {{0}}, "basen"};

/* Session blob (version 1): u8 bits, signed, input base, have; u32 low and
 * high halves of val; str last entry; str entry editor (ed_serialize).
 * bp_reset restores the defaults (32-bit signed, DEC, nothing evaluated);
 * bp_load falls back to them on a truncated blob or invalid values. */
static void bp_save(pw_t *w)
{
    char s[ED_MAX * 6];
    pw_u8(w, bn.bits);
    pw_u8(w, bn.is_signed);
    pw_u8(w, bn.base);
    pw_u8(w, bn.have);
    pw_u32(w, (uint32_t)bn.val);
    pw_u32(w, (uint32_t)(bn.val >> 32));
    pw_str(w, bn.last);
    ed_serialize(&bed, s, sizeof s);
    pw_str(w, s);
}
static void bp_reset(void)
{
    bn.bits = 32, bn.is_signed = 1, bn.base = B_DEC, bn.have = false, bn.last[0] = bn.err[0] = 0;
    ed_clear(&bed);
}
static void bp_load(pr_t *r, unsigned version)
{
    char s[ED_MAX * 6];
    unsigned b = pr_u8(r), sg = pr_u8(r), base = pr_u8(r), have = pr_u8(r);
    uint64_t lo = pr_u32(r), hi = pr_u32(r);
    pr_str(r, bn.last, sizeof bn.last);
    pr_str(r, s, sizeof s);
    if (r->err || b < 1 || b > 64 || base > 3) {
        bp_reset();
        return;
    }
    bn.bits = (uint8_t)b, bn.is_signed = sg != 0, bn.base = (uint8_t)base, bn.have = have != 0;
    bn.val = lo | hi << 32;
    ed_deserialize(&bed, s);
}
const persist_def_t persist_basen = {"basen", 1, bp_save, bp_load, bp_reset};
