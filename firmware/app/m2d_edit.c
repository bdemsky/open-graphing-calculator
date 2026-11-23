/*
 * Textbook entry: templates (fraction, exponent, roots, |x|, log_b, d/dx, ∫,
 * Σ, Π, matrices) in the home editor.
 *
 * A template is a run of marker tokens in the ordinary token editor
 * (editor.h): OPEN(type) slot0 SEP slot1 ... CLOSE. The markers are interned
 * tokens with private display texts ("\x1b/" ...), so ed_serialize() /
 * ed_deserialize() keep them across power-off unchanged. ed_display() and
 * ed_cas() call ed_struct_hook, which serializes a line with templates to
 * linear text: ed_display gives re-enterable Classic text ("(x+1)/2",
 * "x^2", "√(2)", "log(8,2)", "diff(x^3,x)", "[[1,2],[3,4]]"), ed_cas gives the
 * CAS input ("((x+1)/(2))", "x^(2)", ...). The cursor positions between
 * tokens are exactly the caret positions in the boxes, so LEFT/RIGHT need no
 * special code; UP/DOWN use the layout (m2d_home.c).
 *
 * Example: the fraction (x+1)/2 typed with FRAC is the token sequence
 *   OPEN(frac) x + 1 SEP 2 CLOSE
 * and 2^(n+1) typed with the xʸ key is  2 OPEN(pow) n + 1 CLOSE.
 * Slots are numbered in reading order of the textbook form (see tmpl[]):
 *   FRAC  0 numerator, 1 denominator     NROOT 0 index, 1 radicand
 *   LOGB  0 base, 1 argument             DERIV 0 variable, 1 expression
 *   INTEG 0 lower, 1 upper, 2 integrand, 3 variable
 *   INDEF 0 integrand, 1 variable
 *   SUM/PROD 0 index variable, 1 lower bound, 2 upper bound, 3 body
 *   MATRIX r x c cells, row-major (the opening marker encodes r and c).
 * A template nests freely inside another template's slot; markers are matched
 * by counting OPEN/CLOSE depth (scan(), enclosing()).
 *
 * Sections of this file:
 *   - marker tokens (m2d_init, kind)
 *   - serialization to linear text (ed_struct_hook): Classic display text or
 *     CAS input, with implicit multiplication made explicit where needed
 *   - editing keys in Textbook mode (m2d_edit_key): xʸ, √, ∛, |x|, Σ, 10^,
 *     e^, ⌫ on markers, ")" leaving a box, overwrite mode at box boundaries
 *   - UP/DOWN targets between boxes (m2d_vertical_target)
 *   - linear text -> templates (m2d_insert_linear, history recall/paste)
 *   - layout of the editor as boxes (m2d_layout_editor, used by m2d_home.c)
 *   - the FRAC / FUNC / MTRX template menus
 *
 * State: the marker token ids (interned once), the three MODE settings
 * m2d_textbook / m2d_preview / m2d_repeat (saved by persist_m2d in
 * m2d_home.c) and fresh_pow (typing into a just-opened exponent).
 */
#include <string.h>
#include "m2d.h"
#include "m2d_box.h"
#include "theme.h"

/* One template type. The format strings are expanded by tpl(): "%N" is slot
 * N; in `lin`, "%(N" is slot N wrapped in parentheses unless it is atomic
 * (a name, number or single call/group), so the Classic text reads
 * naturally ((x+1)/2, but x/2). The CAS form always parenthesises. */
typedef struct {
    const char *open; /* display text of the opening marker */
    uint8_t nslots;
    const char *cas;  /* CAS format: %N = slot N */
    const char *lin;  /* linear format: %N slot, %(N slot in parentheses unless atomic */
    bool wrap;        /* linear: parenthesize the whole unless the neighbours are safe */
} tmpl_t;

static const tmpl_t tmpl[TPL_COUNT] = {
    [TPL_FRAC] = {"\x1b/", 2, "((%0)/(%1))", "%(0/%(1", true},
    [TPL_POW] = {"\x1b^", 1, "^(%0)", "^%(0", false},
    [TPL_SQRT] = {"\x1bR", 1, "sqrt(%0)", "√(%0)", false},
    [TPL_NROOT] = {"\x1bN", 2, "((%1)^(1/(%0)))", "%(1^(1/%(0)", true},
    [TPL_ABS] = {"\x1b|", 1, "abs(%0)", "abs(%0)", false},
    [TPL_LOGB] = {"\x1bL", 2, "log(%1,%0)", "log(%1,%0)", false},
    [TPL_DERIV] = {"\x1b" "D", 2, "diff(%1,%0)", "diff(%1,%0)", false},
    [TPL_INTEG] = {"\x1bI", 4, "integrate(%2,%3,%0,%1)", "integrate(%2,%3,%0,%1)", false},
    [TPL_INDEF] = {"\x1bJ", 2, "integrate(%0,%1)", "integrate(%0,%1)", false},
    [TPL_SUM] = {"\x1bS", 4, "sum(%3,%0,%1,%2)", "sum(%3,%0,%1,%2)", false},
    [TPL_PROD] = {"\x1bP", 4, "product(%3,%0,%1,%2)", "product(%3,%0,%1,%2)", false},
    /* exponent opened by the keys whose Classic text has a parenthesis (10^(, e^():
     * ")" closes it, and the linear form keeps the parentheses */
    [TPL_POWP] = {"\x1b^(", 1, "^(%0)", "^(%0)", false},
    [TPL_MATRIX] = {"\x1bM", 0, NULL, NULL, false},
};

#define MAXSLOTS 9 /* most slots of a template: a 3x3 matrix */
/* Token ids of the markers, interned by m2d_init(): one OPEN per template
 * type, one per matrix size (rows x cols, 1..3 each), the shared SEP and
 * CLOSE. [id_min, id_max] spans them all, which gives kind() a cheap first
 * test; the range may also hold ordinary tokens (tok_intern returns the
 * existing id of a text interned earlier), so kind() then compares exactly. */
static int open_id[TPL_COUNT], mat_id[3][3], sep_id = -1, close_id = -1;
static int id_min = 0x7FFF, id_max = -1;
static bool inited;

bool m2d_textbook = true, m2d_preview = true, m2d_repeat = true;

static bool struct_hook(const editor_t *e, char *out, size_t n, bool cas);

/* intern a marker token (display = CAS text; never shown or sent as such,
 * the struct hook converts lines with markers) and track the id range */
static int intern(const char *d)
{
    int id = tok_intern(d, d);
    if (id < id_min)
        id_min = id;
    if (id > id_max)
        id_max = id;
    return id;
}

/* Install the editor hook and intern the marker tokens (once). The marker
 * texts start with ESC (0x1b), which no key or CAS output produces, so they
 * can never clash with ordinary tokens; ed_serialize() stores them by their
 * text, so a saved session restores them whatever ids they get. */
void m2d_init(void)
{
    ed_struct_hook = struct_hook;
    if (inited)
        return;
    inited = true;
    for (int t = 0; t < TPL_COUNT; t++)
        open_id[t] = t == TPL_MATRIX ? -1 : intern(tmpl[t].open);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            char d[8] = {'\x1b', 'M', (char)('1' + r), (char)('1' + c), 0};
            mat_id[r][c] = intern(d);
        }
    sep_id = intern("\x1b,");
    close_id = intern("\x1b)");
}

/* ------------------------------------------------------------ token kinds */
enum { K_NONE, K_OPEN, K_SEP, K_CLOSE };

/* Classify token id t: ordinary token or OPEN / SEP / CLOSE marker. For an
 * OPEN, *type gets the template type and *rows, *cols the matrix size (0 for
 * other templates); the out-pointers may be NULL. */
static int kind(int t, int *type, int *rows, int *cols)
{
    if (t < id_min || t > id_max)
        return K_NONE;
    if (t == sep_id)
        return K_SEP;
    if (t == close_id)
        return K_CLOSE;
    for (int k = 0; k < TPL_COUNT; k++)
        if (open_id[k] == t) {
            if (type)
                *type = k;
            if (rows)
                *rows = *cols = 0;
            return K_OPEN;
        }
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            if (mat_id[r][c] == t) {
                if (type)
                    *type = TPL_MATRIX;
                if (rows)
                    *rows = r + 1, *cols = c + 1;
                return K_OPEN;
            }
    return K_NONE;
}

/* kind() of the token at index i (K_NONE outside the line) */
static int kind_at(const editor_t *e, int i) { return i >= 0 && i < e->len ? kind(e->t[i], NULL, NULL, NULL) : K_NONE; }

/* Does the line contain any marker (and so need the struct hook)? */
bool m2d_has_templates(const editor_t *e)
{
    if (id_max < 0)
        return false;
    for (int i = 0; i < e->len; i++)
        if (kind(e->t[i], NULL, NULL, NULL) != K_NONE)
            return true;
    return false;
}

/* Slots of the template opened at index o: starts[k]..ends[k] (exclusive;
 * ends[k] is the SEP/CLOSE). Returns the CLOSE index (e->len if missing).
 * SEPs and CLOSEs of nested templates are skipped by tracking the depth;
 * separators beyond MAXSLOTS are ignored (they merge into the last slot). */
static int scan(const editor_t *e, int o, int *starts, int *ends, int *nslots)
{
    int depth = 0, k = 0;
    starts[0] = o + 1;
    for (int j = o + 1; j < e->len; j++) {
        int kd = kind(e->t[j], NULL, NULL, NULL);
        if (kd == K_OPEN) {
            depth++;
        } else if (kd == K_SEP && depth == 0) {
            if (k < MAXSLOTS - 1) {
                ends[k] = j;
                starts[++k] = j + 1;
            }
        } else if (kd == K_CLOSE) {
            if (depth == 0) {
                ends[k] = j;
                *nslots = k + 1;
                return j;
            }
            depth--;
        }
    }
    ends[k] = e->len;
    *nslots = k + 1;
    return e->len;
}

/* the template enclosing position pos (the OPEN index), or -1: walk left
 * from the caret, skipping complete nested templates */
static int enclosing(const editor_t *e, int pos)
{
    int depth = 0;
    for (int j = pos - 1; j >= 0; j--) {
        int kd = kind_at(e, j);
        if (kd == K_CLOSE)
            depth++;
        else if (kd == K_OPEN) {
            if (depth == 0)
                return j;
            depth--;
        }
    }
    return -1;
}

/* template type of the OPEN marker at o (and the matrix size) */
static int tpl_type(const editor_t *e, int o, int *rows, int *cols)
{
    int t = -1, r = 0, c = 0;
    kind(e->t[o], &t, &r, &c);
    if (rows)
        *rows = r, *cols = c;
    return t;
}

/* ------------------------------------------------------------ serialization */
/* bounded output buffer for the linear text */
typedef struct {
    char *out;   /* caller's buffer, always NUL-terminated */
    size_t n, o; /* capacity, bytes written */
    bool cas;    /* CAS input (true) or Classic display text (false) */
} sb_t;

/* append s[0..l), truncating silently at the capacity */
static void put(sb_t *b, const char *s, size_t l)
{
    if (b->o + l >= b->n)
        l = b->n > b->o + 1 ? b->n - 1 - b->o : 0;
    memcpy(b->out + b->o, s, l);
    b->o += l;
    b->out[b->o] = 0;
}
static void puts_(sb_t *b, const char *s) { put(b, s, strlen(s)); }

/* ASCII character of a name or number */
static bool ident_c(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

/* does the text end with an operand (so a following operand multiplies)? */
static bool ends_operand(const sb_t *b)
{
    if (!b->o)
        return false;
    const char *s = b->out;
    size_t o = b->o;
    char c = s[o - 1];
    if (ident_c(c) || c == '.' || c == ')' || c == ']' || c == '}' || c == '!')
        return true;
    if (o >= 5 && !memcmp(s + o - 5, "∇²", 5)) /* an operator: ∇² then a fraction */
        return false;
    static const char *const post[] = {"π", "θ", "∞", "²", "³", "¹", "°", "ʳ"};
    for (size_t k = 0; k < sizeof post / sizeof post[0]; k++) {
        size_t l = strlen(post[k]);
        if (o >= l && !memcmp(s + o - l, post[k], l))
            return true;
    }
    return false;
}

/* does the text end with a letter (a name that would swallow a following
 * name or "(": x then a template must become x*...)? */
static bool ends_letter(const sb_t *b)
{
    char c = b->o ? b->out[b->o - 1] : 0;
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

/* first character of the display text of token j starts an operand? A
 * template starts one unless it is an exponent (which attaches to what is
 * before it). *digit is set when it starts with a digit or '.', where
 * juxtaposition would merge two numbers (x² then 3 must be x²*3). */
static bool starts_operand(const editor_t *e, int j, bool *digit)
{
    *digit = false;
    if (j >= e->len)
        return false;
    int kd = kind_at(e, j);
    if (kd == K_OPEN) {
        int t = tpl_type(e, j, NULL, NULL);
        return t != TPL_POW && t != TPL_POWP;
    }
    if (kd != K_NONE)
        return false;
    const char *d = tok_disp(e->t[j]);
    char c = d[0];
    *digit = (c >= '0' && c <= '9') || c == '.';
    if (ident_c(c) || c == '.' || c == '(' || c == '[' || c == '{')
        return true;
    return !strncmp(d, "π", 2) || !strncmp(d, "θ", 2) || !strncmp(d, "√", 3) || !strcmp(d, "Ans") ||
           !strncmp(d, "∞", 3);
}

/* atomic linear text: a name/number (π and θ count as letters), or one
 * parenthesized group / call such as "(x+1)" or "sin(x)" whose opening
 * bracket matches the final one. Atomic slots need no parentheses in the
 * Classic form. */
static bool atomic(const char *s, size_t l)
{
    if (!l)
        return true;
    bool plain = true;
    for (size_t k = 0; k < l && plain; k++) {
        unsigned char c = (unsigned char)s[k];
        if (!(ident_c((char)c) || c == '.' || c >= 0x80))
            plain = false;
        if (c >= 0x80 && strncmp(s + k, "π", 2) && strncmp(s + k, "θ", 2))
            plain = plain && (c & 0xC0) == 0x80 && k > 0; /* continuation bytes of π/θ */
    }
    if (plain && s[0] != '.')
        return true;
    if (s[l - 1] != ')' && s[l - 1] != ']')
        return false;
    /* matching open bracket of the last close */
    int depth = 0;
    size_t k = l;
    while (k-- > 0) {
        if (s[k] == ')' || s[k] == ']')
            depth++;
        else if (s[k] == '(' || s[k] == '[') {
            if (--depth == 0)
                break;
        }
    }
    if (depth != 0)
        return false;
    for (size_t j = 0; j < k; j++)
        if (!ident_c(s[j]))
            return false;
    return true;
}

static void ser(const editor_t *e, int a, int b, sb_t *sb);

/* a run of ordinary tokens [a, b): the editor's own conversion, through a
 * temporary editor (ED_MAX tokens on the stack) */
static void leaf(const editor_t *e, int a, int b, sb_t *sb)
{
    editor_t tmp;
    tmp.len = b - a;
    tmp.cur = tmp.scroll = 0;
    memcpy(tmp.t, &e->t[a], sizeof tmp.t[0] * (size_t)tmp.len);
    char buf[ED_MAX * 4 + 16];
    if (sb->cas)
        ed_cas(&tmp, buf, sizeof buf);
    else
        ed_display(&tmp, buf, sizeof buf);
    puts_(sb, buf);
}

/* Serialize slot tokens [a, b); with `paren` (the "%(N" format) wrap the
 * result in parentheses in the Classic form unless it is atomic (inserted
 * after the fact by shifting the slot's text right one byte). */
static void slot(const editor_t *e, int a, int b, bool paren, sb_t *sb)
{
    size_t start = sb->o;
    ser(e, a, b, sb);
    if (paren && !sb->cas && !atomic(sb->out + start, sb->o - start) && sb->o + 2 < sb->n) {
        memmove(sb->out + start + 1, sb->out + start, sb->o - start + 1);
        sb->out[start] = '(';
        sb->o++;
        puts_(sb, ")");
    }
}

/* Serialize the template opened at o. Besides expanding the format string
 * it decides on explicit multiplication signs: in the CAS form an operand
 * directly before or after the template always gets a "*" (2√(3) ->
 * 2*sqrt(3)); in the Classic form only where the text would otherwise read
 * differently: before the template after a letter (x*√(2), x*(1/2), so the
 * name is not read as a call or a longer name) or when the template's
 * Classic text is wrapped in parentheses; after it, behind an exponent
 * (x^2*y) or before a digit (√(2)*3). Fractions
 * and n-th roots (`wrap`) are parenthesised in the Classic form unless the
 * neighbouring characters already delimit them, so "1+(x+1)/2" but
 * "2*((x+1)/2)^2" keeps its meaning when re-entered. */
static void tpl(const editor_t *e, int o, sb_t *sb)
{
    int starts[MAXSLOTS], ends[MAXSLOTS], ns, rows, cols;
    int c = scan(e, o, starts, ends, &ns);
    int type = tpl_type(e, o, &rows, &cols);
    bool digit;
    bool next_op = starts_operand(e, c + 1, &digit);
    /* implicit multiplication before the template */
    bool pow = type == TPL_POW || type == TPL_POWP;
    if (!pow && ends_operand(sb)) {
        if (sb->cas || ends_letter(sb) || tmpl[type].wrap)
            puts_(sb, "*");
    }
    if (type == TPL_MATRIX) {
        puts_(sb, "[");
        for (int r = 0; r < rows; r++) {
            puts_(sb, r ? ",[" : "[");
            for (int q = 0; q < cols; q++) {
                int k = r * cols + q;
                if (q)
                    puts_(sb, ",");
                if (k < ns)
                    slot(e, starts[k], ends[k], false, sb);
            }
            puts_(sb, "]");
        }
        puts_(sb, "]");
    } else {
        bool wrap = !sb->cas && tmpl[type].wrap;
        if (wrap) {
            /* safe without parentheses: after an operator / '(' / ',' / start
             * and before an operator / ')' / ',' / end */
            char pc = sb->o ? sb->out[sb->o - 1] : 0;
            bool before = !pc || pc == '(' || pc == ',' || pc == '+' || pc == '-' || pc == '=' || pc == '[';
            bool after = true;
            if (c + 1 < e->len) {
                const char *d = kind_at(e, c + 1) == K_NONE ? tok_disp(e->t[c + 1]) : "x";
                after = d[0] == ')' || d[0] == ',' || d[0] == '+' || d[0] == '-' || d[0] == '=' || d[0] == ']' ||
                        !strcmp(d, "→") || d[0] == '<' || d[0] == '>';
            }
            wrap = !(before && after);
        }
        if (wrap)
            puts_(sb, "(");
        for (const char *f = sb->cas ? tmpl[type].cas : tmpl[type].lin; *f; f++) {
            if (*f != '%') {
                put(sb, f, 1);
                continue;
            }
            bool paren = f[1] == '(';
            if (paren)
                f++;
            int k = f[1] - '0';
            f++;
            if (k < ns)
                slot(e, starts[k], ends[k], paren, sb);
        }
        if (wrap)
            puts_(sb, ")");
    }
    /* implicit multiplication after it */
    if (next_op && (sb->cas || pow || digit))
        puts_(sb, "*");
}

/* Serialize tokens [a, b): runs of ordinary tokens through the editor,
 * templates through tpl() (recursively for their slots). */
static void ser(const editor_t *e, int a, int b, sb_t *sb)
{
    int i = a;
    while (i < b) {
        int kd = kind_at(e, i);
        if (kd == K_OPEN) {
            int st[MAXSLOTS], en[MAXSLOTS], ns;
            int c = scan(e, i, st, en, &ns);
            tpl(e, i, sb);
            i = c + 1;
        } else if (kd != K_NONE) {
            i++; /* stray separator */
        } else {
            int j = i;
            while (j < b && kind_at(e, j) == K_NONE)
                j++;
            leaf(e, i, j, sb);
            i = j;
        }
    }
}

/* ed_struct_hook (editor.c calls it from ed_display / ed_cas): handle lines
 * with templates, leave the others to the editor (return false). */
static bool struct_hook(const editor_t *e, char *out, size_t n, bool cas)
{
    if (!n || !m2d_has_templates(e))
        return false;
    sb_t sb = {out, n, 0, cas};
    out[0] = 0;
    ser(e, 0, e->len, &sb);
    return true;
}

/* ------------------------------------------------------------ editing */
/* Insert OPEN SEP... CLOSE (always in insert mode, whatever the editor's
 * mode) and put the cursor into the first slot. Fails without changing the
 * line for an unknown type, a matrix size outside 1..3 x 1..3, or when the
 * template's markers would not fit in ED_MAX. */
bool m2d_insert_template(editor_t *e, int type, int rows, int cols)
{
    m2d_init();
    if (type < 0 || type >= TPL_COUNT || (type == TPL_MATRIX && (rows < 1 || rows > 3 || cols < 1 || cols > 3)))
        return false;
    int ns = type == TPL_MATRIX ? rows * cols : tmpl[type].nslots;
    if (ns < 1 || ns > MAXSLOTS || e->len + ns + 1 > ED_MAX)
        return false;
    bool saved = ed_insert_mode;
    ed_insert_mode = true;
    ed_insert(e, type == TPL_MATRIX ? mat_id[rows - 1][cols - 1] : open_id[type]);
    int at = e->cur;
    for (int k = 1; k < ns; k++)
        ed_insert(e, sep_id);
    ed_insert(e, close_id);
    e->cur = at;
    ed_insert_mode = saved;
    return true;
}

/* remove tokens [a, b] (inclusive) and put the cursor at a */
static void delete_range(editor_t *e, int a, int b) /* tokens [a, b] */
{
    memmove(&e->t[a], &e->t[b + 1], sizeof e->t[0] * (size_t)(e->len - b - 1));
    e->len -= b - a + 1;
    e->cur = a;
}

/* replace tokens [a, b] by the linear text of that range (Classic form,
 * re-tokenised; nested templates in the range are flattened too); the
 * cursor ends up where the range started */
static void dissolve(editor_t *e, int a, int b)
{
    char buf[ED_MAX * 4 + 16];
    sb_t sb = {buf, sizeof buf, 0, false};
    buf[0] = 0;
    ser(e, a, b + 1, &sb);
    delete_range(e, a, b);
    bool saved = ed_insert_mode;
    ed_insert_mode = true;
    int at = e->cur;
    /* tokenise without the single-word rule of ed_insert_text */
    editor_t tmp;
    ed_set_text(&tmp, buf);
    for (int k = 0; k < tmp.len && e->len < ED_MAX; k++)
        ed_insert(e, tmp.t[k]);
    e->cur = at;
    ed_insert_mode = saved;
}

/* ⌫ on a marker (the token before the cursor, at the start the one under
 * it): an empty template disappears, a filled one becomes text */
static bool delete_marker(editor_t *e)
{
    int i = e->cur > 0 ? e->cur - 1 : e->len ? 0 : -1;
    if (i < 0)
        return false;
    int kd = kind_at(e, i);
    if (kd == K_NONE)
        return false;
    int o = kd == K_OPEN ? i : enclosing(e, i);
    if (o < 0) { /* stray marker */
        delete_range(e, i, i);
        return true;
    }
    int st[MAXSLOTS], en[MAXSLOTS], ns;
    int c = scan(e, o, st, en, &ns);
    if (c >= e->len)
        c = e->len - 1;
    bool empty = true;
    for (int k = 0; k < ns; k++)
        if (en[k] > st[k])
            empty = false;
    if (empty)
        delete_range(e, o, c);
    else
        dissolve(e, o, c);
    return true;
}

/* Whole line to Classic text (DISPLAY switched to Classic). */
void m2d_flatten(editor_t *e)
{
    if (!m2d_has_templates(e))
        return;
    char buf[ED_MAX * 4 + 16];
    ed_display(e, buf, sizeof buf);
    ed_set_text(e, buf);
}

/* the slot [a, b) has no unclosed '(' (or '[' '{', or call token such as
 * "sin("), ignoring nested templates: a ")" typed there would close the
 * slot's own parenthesis, not the template */
static bool balanced(const editor_t *e, int a, int b)
{
    int depth = 0, bal = 0;
    for (int j = a; j < b; j++) {
        int kd = kind_at(e, j);
        if (kd == K_OPEN)
            depth++;
        else if (kd == K_CLOSE)
            depth--;
        else if (kd == K_NONE && depth == 0) {
            const char *d = tok_disp(e->t[j]);
            size_t l = strlen(d);
            if (l && (d[l - 1] == '(' || d[l - 1] == '[' || d[l - 1] == '{'))
                bal++;
            else if (!strcmp(d, ")") || !strcmp(d, "]") || !strcmp(d, "}"))
                bal--;
        }
    }
    return bal <= 0;
}

static bool fresh_pow; /* an exponent box opened with ^ and typed into since */

/* cursor at the end of a non-empty, balanced exponent box that was opened
 * by the xʸ key and typed into since: step past its CLOSE */
static bool exit_pow(editor_t *e)
{
    if (!fresh_pow || kind_at(e, e->cur) != K_CLOSE)
        return false;
    int o = enclosing(e, e->cur);
    if (o < 0 || tpl_type(e, o, NULL, NULL) != TPL_POW || e->cur == o + 1 || !balanced(e, o + 1, e->cur))
        return false;
    e->cur++;
    fresh_pow = false;
    return true;
}


/* Textbook key handling for the home entry line (called by home.c / the 2D
 * home view before the normal editor keys). Returns true if the key was
 * consumed; false lets the caller do its usual thing (possibly after this
 * function moved the cursor, e.g. out of an exponent). Does nothing in
 * Classic mode except clearing fresh_pow.
 *
 * Exponent behaviour (fresh_pow): after xʸ the user types the exponent into
 * a box. Like Classic entry, where 2^3+1 means (2^3)+1, the first operator
 * key (+ − × ÷ → , ) ] }) typed directly at the end of a fresh, balanced
 * exponent leaves the box before it is inserted. Cursor movement, ⌫, CLEAR
 * and ENTER end that behaviour; it is not restored after the user moved
 * into an exponent by hand. */
bool m2d_edit_key(editor_t *e, int key, int mod)
{
    m2d_init();
    bool fresh = fresh_pow;
    fresh_pow = false; /* kept below only while typing on */
    if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN || key == KEY_DEL ||
        key == KEY_CLEAR || key == KEY_ENTER)
        fresh = false;
    if (!m2d_textbook)
        return false;
    if (key == KEY_POW && mod == MOD_NONE) {
        if (ed_empty(e))
            ed_insert(e, TOK_ANS); /* starting with an operator uses Ans */
        m2d_insert_template(e, TPL_POW, 0, 0);
        fresh_pow = true;
        return true;
    }
    if (key == KEY_SQRT && mod == MOD_NONE)
        return m2d_insert_template(e, TPL_SQRT, 0, 0);
    if (key == KEY_SQRT && mod == MOD_SHIFT) { /* ∛: n-th root with index 3 */
        if (!m2d_insert_template(e, TPL_NROOT, 0, 0))
            return false;
        ed_insert_text(e, "3", NULL);
        e->cur++; /* into the radicand */
        return true;
    }
    if (key == KEY_SUB && mod == MOD_SHIFT)
        return m2d_insert_template(e, TPL_ABS, 0, 0);
    if (key == KEY_ADD && mod == MOD_SHIFT)
        return m2d_insert_template(e, TPL_SUM, 0, 0);
    if ((key == KEY_LOG || key == KEY_LN) && mod == MOD_SHIFT) { /* 10^□, e^□ */
        ed_insert_text(e, key == KEY_LOG ? "10" : "e", NULL);
        m2d_insert_template(e, TPL_POWP, 0, 0);
        return true;
    }
    if (key == KEY_DEL && mod == MOD_NONE)
        return delete_marker(e);
    if (key == KEY_RPAREN && mod == MOD_NONE) {
        /* ")" at the end of a slot whose parentheses are balanced leaves
         * the slot (to the next slot or out of the template) */
        int kd = kind_at(e, e->cur);
        if (kd == K_SEP || kd == K_CLOSE) {
            int o = enclosing(e, e->cur);
            int st[MAXSLOTS], en[MAXSLOTS], ns;
            if (o >= 0) {
                scan(e, o, st, en, &ns);
                int k = 0;
                while (k < ns - 1 && en[k] != e->cur)
                    k++;
                if (balanced(e, st[k], e->cur)) {
                    bool pow = tpl_type(e, o, NULL, NULL) == TPL_POW;
                    e->cur++;
                    if (!pow)
                        return true; /* ")" closes √( etc.: step out */
                    return false;    /* exponent: step out, then insert ")" */
                }
            }
        }
    }
    bool op = mod == MOD_NONE && (key == KEY_ADD || key == KEY_SUB || key == KEY_MUL || key == KEY_DIV ||
                                  key == KEY_ASSIGN || key == KEY_COMMA || key == KEY_RPAREN);
    if (fresh && !op) { /* ] and } (on SHIFT/ALPHA layers) close a list too: [2^10,5^10] */
        char kt[16];
        op = ui_key_text(key, mod, g.alpha_snapshot_for_key, kt, sizeof kt) && (!strcmp(kt, "]") || !strcmp(kt, "}"));
    }
    if (fresh && op) {
        fresh_pow = true;
        exit_pow(e); /* Classic precedence: 2^3+1 is (2^3)+1 */
        fresh_pow = false;
        return false;
    }
    /* overwrite mode with the cursor on a marker: insert instead, so typing
     * at the end of a box never destroys the template's structure */
    char t[16];
    if (!ed_insert_mode && kind_at(e, e->cur) != K_NONE &&
        ui_key_text(key, mod, g.alpha_snapshot_for_key, t, sizeof t)) {
        ed_insert_mode = true; /* overwrite mode never overwrites a box boundary */
        ed_insert_text(e, t, NULL);
        ed_insert_mode = false;
        fresh_pow = fresh;
        return true;
    }
    fresh_pow = fresh; /* other keys: still typing into the exponent */
    return false;
}

/* ------------------------------------------------------------ UP / DOWN
 * Which box is above/below which: numerator/denominator, the limits of ∫, Σ
 * and Π, root index/radicand, matrix rows. */
/* The slot above (dir -1) or below (+1) slot k of a template, or -1. For
 * Σ/Π both parts of the lower row (variable and lower bound) go up to the
 * upper bound, and down from it to the lower bound. */
static int slot_link(int type, int cols, int k, int ns, int dir)
{
    switch (type) {
    case TPL_FRAC:
        return dir < 0 ? (k == 1 ? 0 : -1) : (k == 0 ? 1 : -1);
    case TPL_INTEG:
        return dir < 0 ? (k == 0 ? 1 : -1) : (k == 1 ? 0 : -1);
    case TPL_SUM:
    case TPL_PROD:
        return dir < 0 ? (k == 0 || k == 1 ? 2 : -1) : (k == 2 ? 1 : -1);
    case TPL_NROOT:
        return dir < 0 ? (k == 1 ? 0 : -1) : (k == 0 ? 1 : -1);
    case TPL_MATRIX: {
        int t = k + dir * cols;
        return t >= 0 && t < ns ? t : -1;
    }
    default:
        return -1;
    }
}

/* Find the box to move to with UP/DOWN: the slot linked to the cursor's slot
 * in the innermost enclosing template that has one in that direction (so
 * UP in an exponent inside a denominator goes to the numerator). The caller
 * (m2d_home.c) picks the caret position inside [tb, te] by x coordinate. */
bool m2d_vertical_target(const editor_t *e, int dir, int *tb, int *te, bool *at_end)
{
    for (int pos = e->cur;;) {
        int o = enclosing(e, pos);
        if (o < 0)
            return false;
        int st[MAXSLOTS], en[MAXSLOTS], ns, rows, cols;
        scan(e, o, st, en, &ns);
        int type = tpl_type(e, o, &rows, &cols);
        int k = 0;
        while (k < ns - 1 && en[k] < pos)
            k++;
        int t = slot_link(type, cols, k, ns, dir);
        if (t >= 0) {
            *tb = st[t];
            *te = en[t];
            *at_end = e->cur == en[k];
            return true;
        }
        pos = o; /* try the box around this template */
    }
}

/* ------------------------------------------------------------ linear -> templates
 * Recalled history text ("(x+1)/2+√(3)", "log(8,2)", "x^(n+1)") becomes
 * templates again, so a recalled Textbook entry looks as it was typed.
 *
 * conv() is a single left-to-right scan, not a full parser: it recognises
 * √(..), the calls abs/log(a,b)/diff/integrate/sum/product, "^" followed by
 * an operand and "/" between operands, and copies everything else as typed.
 * Unrecognised constructs simply stay linear, which is always correct. */
/* append the text s[0..n) as tokens (tokenised like typed text) */
static void emit(editor_t *e, const char *s, int n)
{
    if (n <= 0)
        return;
    char buf[ED_MAX * 4 + 1];
    if (n > (int)sizeof buf - 1)
        n = (int)sizeof buf - 1;
    memcpy(buf, s, (size_t)n);
    buf[n] = 0;
    editor_t tmp;
    ed_set_text(&tmp, buf); /* tokenised like typed text */
    for (int k = 0; k < tmp.len && e->len < ED_MAX; k++)
        ed_insert(e, tmp.t[k]);
}

/* append one token (marker or a token copied from elsewhere) if there is room */
static void mark(editor_t *e, int id)
{
    if (e->len < ED_MAX)
        ed_insert(e, id);
}

/* index of the bracket matching the one at i, or -1 */
static int match(const char *s, int i, int end)
{
    char o = s[i], c = o == '(' ? ')' : o == '[' ? ']' : '}';
    int depth = 0;
    for (int k = i; k < end; k++) {
        if (s[k] == o)
            depth++;
        else if (s[k] == c && --depth == 0)
            return k;
    }
    return -1;
}

/* s[i] is part of a name/number (ASCII word character, '.', π, θ) */
static bool atom_c(const char *s, int i)
{
    char c = s[i];
    return ident_c(c) || c == '.' || !strncmp(s + i, "π", 2) || !strncmp(s + i, "θ", 2);
}

/* end of the name/number starting at s[i] (π and θ are 2 UTF-8 bytes) */
static int atom_end(const char *s, int i, int end)
{
    while (i < end && atom_c(s, i))
        i += (unsigned char)s[i] >= 0x80 ? 2 : 1;
    return i;
}

/* top-level commas of s[a..b): returns the count, splits in at[] */
static int split_args(const char *s, int a, int b, int *at, int max)
{
    int n = 0, depth = 0;
    at[n++] = a;
    for (int k = a; k < b; k++) {
        if (s[k] == '(' || s[k] == '[' || s[k] == '{')
            depth++;
        else if (s[k] == ')' || s[k] == ']' || s[k] == '}')
            depth--;
        else if (s[k] == ',' && depth == 0 && n < max)
            at[n++] = k + 1;
    }
    return n;
}

static void conv(editor_t *e, const char *s, int a, int b);

/* the operand at s[i..): its end (a group, call, root, atom, with exponents).
 * An optional leading sign belongs to it; trailing ^operand, !, ², ³ are
 * included (recursively), so x^2^3 and (a+b)! are one operand. */
static int operand_end(const char *s, int i, int b)
{
    int k = i;
    if (k < b && (s[k] == '-' || !strncmp(s + k, "⁻", 3)))
        k += s[k] == '-' ? 1 : 3;
    if (k < b && s[k] == '(') {
        int j = match(s, k, b);
        k = j < 0 ? b : j + 1;
    } else if (!strncmp(s + k, "√(", 4)) {
        int j = match(s, k + 3, b);
        k = j < 0 ? b : j + 1;
    } else {
        k = atom_end(s, k, b);
        if (k < b && s[k] == '(') {
            int j = match(s, k, b);
            k = j < 0 ? b : j + 1;
        }
    }
    while (k < b && (s[k] == '^' || s[k] == '!' || !strncmp(s + k, "²", 2) || !strncmp(s + k, "³", 2))) {
        if (s[k] == '^')
            k = operand_end(s, k + 1, b);
        else
            k += s[k] == '!' ? 1 : 2;
    }
    return k;
}

/* convert s[a..b) as one slot, without an outer pair of parentheses */
static void conv_slot(editor_t *e, const char *s, int a, int b)
{
    if (b > a && s[a] == '(' && match(s, a, b) == b - 1)
        conv(e, s, a + 1, b - 1);
    else
        conv(e, s, a, b);
}

static bool name_eq(const char *s, int a, int b, const char *n)
{
    return (int)strlen(n) == b - a && !strncmp(s + a, n, (size_t)(b - a));
}

/* name(args) with a template: emits it and returns true. The arguments
 * s[open+1..close) are split at top-level commas and placed into the slots
 * in template order (ord[k] = argument index for slot k, the inverse of the
 * tmpl[] format strings). false (nothing emitted) for other names or
 * argument counts, or if the line would overflow. */
static bool conv_call(editor_t *e, const char *s, int na, int nb, int open, int close)
{
    int at[8], n = split_args(s, open + 1, close, at, 8);
    int end[8];
    for (int k = 0; k < n; k++)
        end[k] = k + 1 < n ? at[k + 1] - 1 : close;
    int type = -1;
    static const int8_t ord_logb[] = {1, 0}, ord_diff[] = {1, 0}, ord_indef[] = {0, 1}, ord_int[] = {2, 3, 0, 1},
                        ord_sum[] = {1, 2, 3, 0}, ord_abs[] = {0};
    const int8_t *ord = NULL;
    if (name_eq(s, na, nb, "abs") && n == 1)
        type = TPL_ABS, ord = ord_abs;
    else if (name_eq(s, na, nb, "log") && n == 2)
        type = TPL_LOGB, ord = ord_logb;
    else if (name_eq(s, na, nb, "diff") && n == 2)
        type = TPL_DERIV, ord = ord_diff;
    else if (name_eq(s, na, nb, "integrate") && n == 2)
        type = TPL_INDEF, ord = ord_indef;
    else if (name_eq(s, na, nb, "integrate") && n == 4)
        type = TPL_INTEG, ord = ord_int;
    else if (name_eq(s, na, nb, "sum") && n == 4)
        type = TPL_SUM, ord = ord_sum;
    else if (name_eq(s, na, nb, "product") && n == 4)
        type = TPL_PROD, ord = ord_sum;
    if (type < 0 || e->len + n + 1 >= ED_MAX)
        return false;
    mark(e, open_id[type]);
    for (int k = 0; k < n; k++) {
        if (k)
            mark(e, sep_id);
        int src = ord[k];
        conv_slot(e, s, at[src], end[src]);
    }
    mark(e, close_id);
    return true;
}

/* Convert linear text s[a..b) into tokens appended to e. `opstart` tracks
 * where the last operand's tokens begin, so a following "/" can move them
 * into a fraction's numerator; any operator other than a postfix mark ends
 * the operand (opstart = -1), so 1+2/3 makes only 2 the numerator. */
static void conv(editor_t *e, const char *s, int a, int b)
{
    int i = a, opstart = -1; /* token index where the current operand starts */
    while (i < b && e->len < ED_MAX - 2) {
        char c = s[i];
        if (!strncmp(s + i, "√(", 4)) {
            int j = match(s, i + 3, b);
            if (j < 0)
                j = b;
            opstart = e->len;
            mark(e, open_id[TPL_SQRT]);
            conv(e, s, i + 4, j);
            mark(e, close_id);
            i = j + 1;
            continue;
        }
        if (atom_c(s, i)) {
            int k = atom_end(s, i, b);
            opstart = e->len;
            if (k < b && s[k] == '(' && (unsigned char)s[i] < 0x80 && !(c >= '0' && c <= '9')) {
                int j = match(s, k, b);
                if (j < 0)
                    j = b - 1;
                if (!conv_call(e, s, i, k, k, j)) {
                    emit(e, s + i, k + 1 - i); /* "sin(" */
                    conv(e, s, k + 1, j);
                    if (j < b && s[j] == ')')
                        emit(e, ")", 1);
                }
                i = j + 1;
                continue;
            }
            emit(e, s + i, k - i);
            i = k;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') {
            int j = match(s, i, b);
            if (j < 0)
                j = b - 1;
            opstart = e->len;
            emit(e, s + i, 1);
            conv(e, s, i + 1, j);
            if (j < b)
                emit(e, s + j, 1);
            i = j + 1;
            continue;
        }
        if (c == '^') {
            int k = operand_end(s, i + 1, b);
            mark(e, open_id[TPL_POW]);
            conv_slot(e, s, i + 1, k);
            mark(e, close_id);
            i = k;
            continue;
        }
        if (c == '/' && opstart >= 0) {
            /* numerator = the operand just emitted (its tokens are copied
             * out, removed, and re-added after the OPEN marker; an outer
             * pair of parentheses around the whole operand is dropped) */
            uint16_t num[ED_MAX];
            int nn = e->len - opstart;
            memcpy(num, &e->t[opstart], sizeof num[0] * (size_t)nn);
            int from = 0, to = nn;
            if (nn >= 2 && num[0] == '(' && num[nn - 1] == ')') {
                int depth = 0, k;
                for (k = 0; k < nn; k++) {
                    depth += num[k] == '(' ? 1 : num[k] == ')' ? -1 : 0;
                    if (!depth)
                        break;
                }
                if (k == nn - 1)
                    from = 1, to = nn - 1;
            }
            e->len = e->cur = opstart;
            mark(e, open_id[TPL_FRAC]);
            for (int k = from; k < to; k++)
                mark(e, num[k]);
            mark(e, sep_id);
            int k = operand_end(s, i + 1, b);
            conv_slot(e, s, i + 1, k);
            mark(e, close_id);
            i = k;
            continue; /* the fraction is the operand now (a/b/c) */
        }
        /* operators, postfix marks, anything else: as typed */
        const char *p = s + i;
        utf8_next(&p);
        int l = (int)(p - (s + i));
        bool postfix = c == '!' || !strncmp(s + i, "²", 2) || !strncmp(s + i, "³", 2) ||
                       !strncmp(s + i, "⁻¹", 5) || !strncmp(s + i, "°", 2);
        if (!strncmp(s + i, "⁻¹", 5))
            l = 5;
        emit(e, s + i, l);
        if (!postfix)
            opstart = -1;
        i += l;
    }
}

/* Insert linear text at the cursor, as templates in Textbook mode (history
 * recall/paste), else as typed text. */
void m2d_insert_linear(editor_t *e, const char *text)
{
    if (!m2d_textbook || !text) {
        ed_insert_text(e, text, NULL);
        return;
    }
    m2d_init();
    /* build the tokens in a scratch editor, then insert them at the cursor */
    editor_t tmp;
    ed_clear(&tmp);
    bool saved = ed_insert_mode;
    ed_insert_mode = true;
    conv(&tmp, text, 0, (int)strlen(text));
    for (int k = 0; k < tmp.len && e->len < ED_MAX; k++)
        ed_insert(e, tmp.t[k]);
    ed_insert_mode = saved;
}

/* Replace the line by linear text (as templates in Textbook mode). */
void m2d_set_linear(editor_t *e, const char *text)
{
    ed_clear(e);
    m2d_insert_linear(e, text);
}

/* ------------------------------------------------------------ layout */
/* Editor -> boxes. Every ordinary token becomes one text box; every
 * template becomes its 2D construct with one BXF_SLOT row per slot. The
 * caller gets two maps for caret handling and taps:
 *   tokbox[i] = the box whose right edge is the caret after token i (a text
 *               box, or for a CLOSE marker the whole template box);
 *   rows[]    = every row with its token range [tb, te) (the root row and
 *               each slot), so a caret position inside an empty slot can be
 *               placed at the slot's left edge. */
typedef struct {
    bxpool_t *p;
    const editor_t *e;
    int16_t *tokbox, *rows; /* output maps (caller's arrays) */
    int nrows, maxrows;
} lay_t;

static int lay_tpl(lay_t *L, int o, int lvl);

/* row for tokens [a, b) at level lvl; `slot` marks a template slot (drawn
 * as a dotted placeholder while empty) */
static int lay_row(lay_t *L, int a, int b, int lvl, bool slot)
{
    bxpool_t *p = L->p;
    int row = bx_new(p, BX_ROW, lvl);
    if (p->overflow)
        return row;
    p->b[row].flags = slot ? BXF_SLOT : 0;
    p->b[row].tb = (int16_t)a;
    p->b[row].te = (int16_t)b;
    if (L->nrows < L->maxrows)
        L->rows[L->nrows++] = (int16_t)row;
    int i = a;
    while (i < b && !p->overflow) {
        int kd = kind_at(L->e, i);
        if (kd == K_OPEN) {
            int st[MAXSLOTS], en[MAXSLOTS], ns;
            int c = scan(L->e, i, st, en, &ns);
            int box = lay_tpl(L, i, lvl);
            bx_add(p, row, box);
            if (c < L->e->len)
                L->tokbox[c] = (int16_t)box;
            i = c + 1;
        } else if (kd != K_NONE) {
            i++;
        } else {
            const char *d = tok_disp(L->e->t[i]);
            int box = bx_text(p, lvl, d, (int)strlen(d), 0);
            bx_add(p, row, box);
            L->tokbox[i] = (int16_t)box;
            i++;
        }
    }
    return row;
}

/* text box for a literal */
static int text(bxpool_t *p, int lvl, const char *s) { return bx_text(p, lvl, s, (int)strlen(s), 0); }

/* Box for the template opened at o. Slot levels (font sizes): exponents,
 * log bases, ∫/Σ/Π limits one smaller; a root index two smaller; fraction
 * parts the same at level 0 (display style) and one smaller below. Missing
 * slots (a damaged line) get empty rows. */
static int lay_tpl(lay_t *L, int o, int lvl)
{
    bxpool_t *p = L->p;
    int st[MAXSLOTS], en[MAXSLOTS], ns, rows, cols;
    scan(L->e, o, st, en, &ns);
    int type = tpl_type(L->e, o, &rows, &cols);
    int need = type == TPL_MATRIX ? rows * cols : tmpl[type].nslots;
    int s[MAXSLOTS];
    int up = lvl + 1, fl = lvl > 0 ? lvl + 1 : lvl;
    for (int k = 0; k < need; k++) {
        int l = lvl;
        if (type == TPL_POW || type == TPL_POWP || (type == TPL_LOGB && k == 0))
            l = up;
        else if (type == TPL_FRAC)
            l = fl;
        else if (type == TPL_NROOT && k == 0)
            l = lvl + 2;
        else if ((type == TPL_INTEG && k < 2) || ((type == TPL_SUM || type == TPL_PROD) && k < 3))
            l = up;
        s[k] = k < ns ? lay_row(L, st[k], en[k], l, true) : lay_row(L, 0, 0, l, true);
    }
    switch (type) {
    case TPL_FRAC: {
        int f = bx_new(p, BX_FRAC, lvl);
        bx_add(p, f, s[0]);
        bx_add(p, f, s[1]);
        return f;
    }
    case TPL_POW:
    case TPL_POWP:
        return bx_wrap(p, BX_SUP, lvl, s[0]);
    case TPL_SQRT:
        return bx_wrap(p, BX_ROOT, lvl, s[0]);
    case TPL_NROOT: {
        int r = bx_wrap(p, BX_ROOT, lvl, s[1]);
        bx_add(p, r, s[0]);
        return r;
    }
    case TPL_ABS: {
        int f = bx_wrap(p, BX_FENCE, lvl, s[0]);
        p->b[f].aux = p->b[f].aux2 = '|';
        return f;
    }
    case TPL_LOGB: {
        int r = bx_new(p, BX_ROW, lvl);
        bx_add(p, r, text(p, lvl, "log"));
        bx_add(p, r, bx_wrap(p, BX_SUB, lvl, s[0]));
        int f = bx_wrap(p, BX_FENCE, lvl, s[1]);
        p->b[f].aux = '(';
        p->b[f].aux2 = ')';
        bx_add(p, r, f);
        return r;
    }
    case TPL_DERIV: {
        int r = bx_new(p, BX_ROW, lvl);
        int num = bx_new(p, BX_ROW, lvl), den = bx_new(p, BX_ROW, lvl);
        bx_add(p, num, text(p, lvl, "d"));
        bx_add(p, den, text(p, lvl, "d"));
        bx_add(p, den, s[0]);
        int f = bx_new(p, BX_FRAC, lvl);
        bx_add(p, f, num);
        bx_add(p, f, den);
        bx_add(p, r, f);
        int fe = bx_wrap(p, BX_FENCE, lvl, s[1]);
        p->b[fe].aux = '(';
        p->b[fe].aux2 = ')';
        bx_add(p, r, fe);
        return r;
    }
    case TPL_INTEG:
    case TPL_INDEF: {
        bool def = type == TPL_INTEG;
        int r = bx_new(p, BX_ROW, lvl);
        int op = bx_new(p, BX_BIGOP, lvl);
        p->b[op].aux = BIG_INT;
        bx_add(p, op, def ? s[0] : bx_new(p, BX_ROW, up));
        bx_add(p, op, def ? s[1] : bx_new(p, BX_ROW, up));
        bx_add(p, r, op);
        bx_add(p, r, s[def ? 2 : 0]);
        int gp = bx_new(p, BX_GAP, lvl);
        p->b[gp].aux = 3;
        bx_add(p, r, gp);
        bx_add(p, r, text(p, lvl, "d"));
        bx_add(p, r, s[def ? 3 : 1]);
        return r;
    }
    case TPL_SUM:
    case TPL_PROD: {
        int r = bx_new(p, BX_ROW, lvl);
        int op = bx_new(p, BX_BIGOP, lvl);
        p->b[op].aux = type == TPL_SUM ? BIG_SUM : BIG_PROD;
        int lo = bx_new(p, BX_ROW, up);
        bx_add(p, lo, s[0]);
        bx_add(p, lo, text(p, up, "="));
        bx_add(p, lo, s[1]);
        bx_add(p, op, lo);
        bx_add(p, op, s[2]);
        bx_add(p, r, op);
        bx_add(p, r, s[3]);
        return r;
    }
    case TPL_MATRIX: {
        int m = bx_new(p, BX_MATRIX, lvl);
        p->b[m].aux = (uint8_t)cols;
        for (int k = 0; k < need; k++)
            bx_add(p, m, s[k]);
        return m;
    }
    default:
        return bx_new(p, BX_ROW, lvl);
    }
}

/* Lay out the whole line (see m2d_box.h): returns the root row; tokbox[]
 * must have ED_MAX entries. The boxes point into the token table's strings
 * (static), so they stay valid. */
int m2d_layout_editor(bxpool_t *p, const editor_t *e, int lvl, int16_t *tokbox, int16_t *rows, int *nrows,
                      int maxrows)
{
    m2d_init();
    for (int i = 0; i < ED_MAX; i++)
        tokbox[i] = -1;
    lay_t L = {p, e, tokbox, rows, 0, maxrows};
    int root = lay_row(&L, 0, e->len, lvl, false);
    *nrows = L.nrows;
    return root;
}

/* ------------------------------------------------------------ menus */
/* menu_item_t.action packs the template type (bits 0-7) and the matrix rows
 * (bits 8-11) and columns (bits 12-15) */
#define ACT(type, r, c) ((type) | ((r) << 8) | ((c) << 12))

/* Menu selection: in the Textbook home editor insert the template; in any
 * other editor (Y=, forms, Classic home) insert the item's linear form
 * (insert + suffix, cursor in the first gap). Then return to the screen the
 * menu was opened from without resetting it (ui_returning). */
static void tpl_select(const menu_item_t *it)
{
    const screen_t *dest = menu_origin() && menu_origin()->editor ? menu_origin() : ui_home();
    editor_t *e = dest->editor ? dest->editor() : NULL;
    if (e) {
        int type = it->action & 0xFF, r = (it->action >> 8) & 15, c = (it->action >> 12) & 15;
        if (dest == &scr_home && m2d_textbook) {
            m2d_insert_template(e, type, r, c);
            fresh_pow = false;
        } else {
            /* Classic or another editor: the linear form, cursor in the first gap */
            ed_insert_text(e, it->insert, it->suffix);
        }
    }
    ui_returning = true;
    ui_switch(dest);
    ui_returning = false;
}

static const menu_item_t frac_items[] = {
    {"n/d  fraction", "(", ")/()", ACT(TPL_FRAC, 0, 0), "textbook-templates"},
    {"x^y  power", "^(", ")", ACT(TPL_POW, 0, 0), "textbook-templates"},
    {"|x|  absolute value", "abs(", ")", ACT(TPL_ABS, 0, 0), "textbook-templates"},
};
const menu_def_t menu_m2d_frac = {"FRAC", frac_items, sizeof frac_items / sizeof frac_items[0], tpl_select,
                                  false, "textbook-templates"};

static const menu_item_t func_items[] = {
    {"Σ  summation", "sum(", ",k,1,n)", ACT(TPL_SUM, 0, 0), "textbook-templates"},
    {"Π  product", "product(", ",k,1,n)", ACT(TPL_PROD, 0, 0), "textbook-templates"},
    {"d/dx  derivative", "diff(", ",x)", ACT(TPL_DERIV, 0, 0), "textbook-templates"},
    {"∫  definite integral", "integrate(", ",x,0,1)", ACT(TPL_INTEG, 0, 0), "textbook-templates"},
    {"∫  antiderivative", "integrate(", ",x)", ACT(TPL_INDEF, 0, 0), "textbook-templates"},
    {"logBASE(", "log(", ",10)", ACT(TPL_LOGB, 0, 0), "textbook-templates"},
    {"√  square root", "√(", ")", ACT(TPL_SQRT, 0, 0), "textbook-templates"},
    {"ⁿ√  n-th root", "(", ")^(1/3)", ACT(TPL_NROOT, 0, 0), "textbook-templates"},
    {"|x|  absolute value", "abs(", ")", ACT(TPL_ABS, 0, 0), "textbook-templates"},
};
const menu_def_t menu_m2d_func = {"FUNC", func_items, sizeof func_items / sizeof func_items[0], tpl_select,
                                  false, "textbook-templates"};

static const menu_item_t mtrx_items[] = {
    {"2×2 matrix", "[[", ",],[,]]", ACT(TPL_MATRIX, 2, 2), "textbook-templates"},
    {"3×3 matrix", "[[", ",,],[,,],[,,]]", ACT(TPL_MATRIX, 3, 3), "textbook-templates"},
    {"2×1 column vector", "[[", "],[]]", ACT(TPL_MATRIX, 2, 1), "textbook-templates"},
    {"3×1 column vector", "[[", "],[],[]]", ACT(TPL_MATRIX, 3, 1), "textbook-templates"},
    {"1×2 row vector", "[[", ",]]", ACT(TPL_MATRIX, 1, 2), "textbook-templates"},
    {"1×3 row vector", "[[", ",,]]", ACT(TPL_MATRIX, 1, 3), "textbook-templates"},
    {"2×3 matrix", "[[", ",,],[,,]]", ACT(TPL_MATRIX, 2, 3), "textbook-templates"},
    {"3×2 matrix", "[[", ",],[,],[,]]", ACT(TPL_MATRIX, 3, 2), "textbook-templates"},
};
const menu_def_t menu_m2d_mtrx = {"MTRX", mtrx_items, sizeof mtrx_items / sizeof mtrx_items[0], tpl_select,
                                  false, "textbook-templates"};
