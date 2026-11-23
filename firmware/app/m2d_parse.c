/*
 * Linear math text (CAS output after display prettifying, or the home
 * screen's input text) -> 2D boxes: fractions stacked, roots with a vinculum,
 * exponents/subscripts, |x|, e^x, log_b, d/dx, integrals, sums, limits,
 * matrices, sized parentheses, 1.5×10⁻⁷, repeating decimals 0.(3) with a bar.
 * Anything not understood makes m2d_parse() fail; the caller shows the text.
 *
 * It is a small recursive-descent parser that builds boxes directly while
 * parsing (no separate syntax tree). Grammar, lowest precedence first:
 *
 *   expr    := add (REL add)*                  = < > ≤ ≥ ≠ := == != → ∠
 *   add     := [− | ±] mul ((+ | − | ±) mul)*
 *   mul     := unary ((× | ÷ | <juxtaposition>) unary)*   left-assoc.
 *   unary   := (− | + | ⁻) unary | power
 *   power   := postfix [^ unary]               right-assoc. (unary -> power)
 *   postfix := prim (² | ³ | ⁻¹ | ! | ° | ʳ | ')*
 *   prim    := NUM | ID | FUNC list ')' | √( expr ) | '(' list ')'
 *            | '[' list ']' | '{' list '}' | (∇ | ∇· | ∇× | ∇²) unary
 *
 * The del operator (the CAS reads it, cas/src/parse.c) applies to the
 * following unary; the one-argument calls grad(f), divergence(F), curl(F)
 * and laplacian(f) are shown in the same notation.
 *
 * The 2D forms come from recognising shapes: a/b becomes a stacked fraction
 * (the operands lose their outer parentheses), x^(1/n) a root, sqrt/abs/exp/
 * log(a,b)/diff/integrate/sum/product/limit calls their textbook notation,
 * [[..],[..]] a matrix, a name with "_" a subscript, number exponents
 * ×10ⁿ. Each sub-result carries its first/last "atom class" (res_t) so that
 * products can decide between juxtaposition (2x, 2(x+1)) and a visible dot
 * (2·3), and parenthesised groups can be unwrapped where the 2D layout makes
 * the grouping obvious (fraction parts, exponents, radicands).
 *
 * Nesting levels (fonts): exponents, subscripts, limits and root indices are
 * one level smaller than their base (relevel() shifts an already built
 * subtree). Fractions at level 0 keep the full font for both parts (display
 * style); fractions inside smaller levels shrink their parts by one more.
 *
 * Boxes point into the input string `s`, which must outlive the pool's use.
 * Recursion depth is bounded (40 nested expressions) for the small stack.
 */
#include <string.h>
#include "m2d.h"
#include "m2d_box.h"

/* token types: end, number, identifier, function name followed by "(" (the
 * "(" is consumed), "√(", infix operator, postfix operator, ( ) [ ] { } , and
 * an unknown character (makes the parse fail) */
enum { T_END, T_NUM, T_ID, T_FUNC, T_SQRT, T_OP, T_LP, T_RP, T_LB, T_RB, T_LC, T_RC, T_COMMA, T_POST, T_ERR,
       T_NABLA /* op: 0 ∇, 1 ∇·, 2 ∇×, 3 ∇² */ };
/* operator codes (T_OP / T_POST) */
enum {
    O_ADD = 1, O_SUB, O_NEG, O_PM, O_MUL, O_DIV, O_POW, O_REL, /* relations: text in the token */
    P_SQ, P_CUBE, P_INV, P_TEXT,                       /* postfix */
};

/* the current token (one token of lookahead) */
typedef struct {
    int type, op;
    const char *s; /* token text */
    int len;
    int rep;       /* numbers: byte offset of a repeating group "(ddd)" or -1 */
    int exp;       /* numbers: byte offset of the exponent mark or -1 */
    int explen;    /* ... and its length in bytes */
} tok_t;

/* parser state */
typedef struct {
    bxpool_t *p;   /* box pool being filled */
    const char *s; /* input position after the current token */
    tok_t t;       /* current token */
    bool err;      /* syntax not understood: the whole parse fails */
    int depth;     /* p_expr recursion depth (bounded) */
} ps_t;

/* atom classes for juxtaposition: what a sub-result starts/ends with. An
 * explicit × before an operand that starts with a number (or anything
 * unclassified) is shown as "·" (2·3), otherwise it is dropped (2x, 2(x+1)).
 * They also decide where function arguments need parentheses (arg_box). */
enum { A_OTHER, A_NUM, A_ID, A_FENCE, A_FRAC };

/* result of a parse function: the box built and how it looks at its ends */
typedef struct {
    int box;              /* root box of the sub-result (-1 on error) */
    uint8_t first, last;  /* A_* class of its first / last atom */
    bool paren;    /* a parenthesized group: FENCE '(' ... ')' */
    bool additive; /* top level is a sum/difference */
} res_t;

/* ------------------------------------------------------------ lexer */
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

static bool starts(const char *s, const char *w) { return !strncmp(s, w, strlen(w)); }

/* Read the next token from ps->s into ps->t. Multi-byte UTF-8 symbols are
 * matched as strings (the sym[] table is ordered so that longer spellings
 * such as "<=" come before their prefixes). Numbers include an optional
 * repeating group "0.1(6)" (only directly after the decimal digits) and an
 * exponent marked e/E/ᴇ with an optional sign (-, +, −, ⁻); the byte offsets
 * of both are recorded for number(). Identifiers are ASCII words (letters,
 * digits, _) or one of π θ ∞; an ASCII word directly followed by "(" (or by
 * "⁻¹(" as in sin⁻¹) is a T_FUNC whose "(" is consumed. */
static void lex(ps_t *ps)
{
    const char *s = ps->s;
    while (*s == ' ')
        s++;
    tok_t *t = &ps->t;
    t->s = s;
    t->rep = t->exp = -1;
    t->op = 0;
    if (!*s) {
        t->type = T_END;
        t->len = 0;
        ps->s = s;
        return;
    }
    static const struct {
        const char *w;
        int type, op;
    } sym[] = {
        {"∇·", T_NABLA, 1}, {"∇×", T_NABLA, 2}, {"∇²", T_NABLA, 3}, {"∇", T_NABLA, 0},
        {"⁻¹", T_POST, P_INV}, {"√(", T_SQRT, 0}, {":=", T_OP, O_REL}, {"==", T_OP, O_REL}, {"!=", T_OP, O_REL},
        {"<=", T_OP, O_REL}, {">=", T_OP, O_REL}, {"≤", T_OP, O_REL}, {"≥", T_OP, O_REL}, {"≠", T_OP, O_REL},
        {"→", T_OP, O_REL}, {"∠", T_OP, O_REL}, {"=", T_OP, O_REL}, {"<", T_OP, O_REL}, {">", T_OP, O_REL},
        {"+", T_OP, O_ADD}, {"±", T_OP, O_PM}, {"∓", T_OP, O_PM}, {"-", T_OP, O_SUB}, {"−", T_OP, O_SUB}, {"⁻", T_OP, O_NEG}, {"*", T_OP, O_MUL},
        {"×", T_OP, O_MUL}, {"·", T_OP, O_MUL}, {"/", T_OP, O_DIV}, {"÷", T_OP, O_DIV}, {"^", T_OP, O_POW},
        {"²", T_POST, P_SQ}, {"³", T_POST, P_CUBE}, {"!", T_POST, P_TEXT}, {"°", T_POST, P_TEXT},
        {"ʳ", T_POST, P_TEXT}, {"'", T_POST, P_TEXT}, {"(", T_LP, 0}, {")", T_RP, 0}, {"[", T_LB, 0},
        {"]", T_RB, 0}, {"{", T_LC, 0}, {"}", T_RC, 0}, {",", T_COMMA, 0},
    };
    if (is_digit(*s) || (*s == '.' && is_digit(s[1]))) {
        const char *p = s;
        while (is_digit(*p))
            p++;
        if (*p == '.') {
            p++;
            while (is_digit(*p))
                p++;
            if (*p == '(' && is_digit(p[1])) { /* repeating group 0.1(6) */
                const char *q = p + 1;
                while (is_digit(*q))
                    q++;
                if (*q == ')') {
                    t->rep = (int)(p - s);
                    p = q + 1;
                }
            }
        }
        const char *e = p;
        int el = *e == 'e' || *e == 'E' ? 1 : starts(e, "ᴇ") ? 3 : 0;
        if (el) {
            const char *q = e + el;
            if (*q == '-' || *q == '+')
                q++;
            else if (starts(q, "−") || starts(q, "⁻"))
                q += 3;
            if (is_digit(*q)) {
                while (is_digit(*q))
                    q++;
                t->exp = (int)(e - s);
                t->explen = el;
                p = q;
            }
        }
        t->type = T_NUM;
        t->len = (int)(p - s);
        ps->s = p;
        return;
    }
    if (is_alpha(*s) || starts(s, "π") || starts(s, "θ") || starts(s, "∞")) {
        const char *p = s;
        if ((unsigned char)*p >= 0x80) {
            utf8_next(&p);
        } else {
            while (is_alpha(*p) || is_digit(*p))
                p++;
        }
        if (starts(p, "⁻¹("))
            p += 5; /* sin⁻¹( */
        t->len = (int)(p - s);
        if (*p == '(' && (unsigned char)*s < 0x80) {
            t->type = T_FUNC;
            ps->s = p + 1;
        } else {
            t->type = T_ID;
            ps->s = p;
        }
        return;
    }
    for (size_t k = 0; k < sizeof sym / sizeof sym[0]; k++)
        if (starts(s, sym[k].w)) {
            t->type = sym[k].type;
            t->op = sym[k].op;
            t->len = (int)strlen(sym[k].w);
            ps->s = s + t->len;
            return;
        }
    t->type = T_ERR;
    t->len = 1;
    ps->err = true;
}

/* ------------------------------------------------------------ helpers */
/* a result with the given atom classes (not parenthesised, not a sum) */
static res_t mk(int box, int first, int last)
{
    res_t r = {box, (uint8_t)first, (uint8_t)last, false, false};
    return r;
}

/* Shift the nesting level (font size) of a built subtree by d (max 3): used
 * when an operand parsed at the current level turns out to be an exponent,
 * subscript, limit or a part of a nested fraction. */
static void relevel(bxpool_t *p, int i, int d)
{
    if (i < 0 || p->overflow)
        return;
    int l = p->b[i].lvl + d;
    p->b[i].lvl = (uint8_t)(l > 3 ? 3 : l);
    for (int c = p->b[i].child; c >= 0; c = p->b[c].next)
        relevel(p, c, d);
}

/* `box` as a row (slots of fractions, roots, fences and scripts are rows):
 * the box itself if it is one, else a new row containing it */
static int row_of(bxpool_t *p, int lvl, int box)
{
    if (box >= 0 && p->b[box].kind == BX_ROW)
        return box;
    int r = bx_new(p, BX_ROW, lvl);
    bx_add(p, r, box);
    return r;
}

/* the content of a parenthesized group, else the box itself */
static int strip(bxpool_t *p, res_t r)
{
    if (r.paren && p->b[r.box].child >= 0)
        return p->b[r.box].child;
    return r.box;
}

/* text box for a NUL-terminated string (literals) */
static int text(bxpool_t *p, int lvl, const char *s, int flags) { return bx_text(p, lvl, s, (int)strlen(s), flags); }

/* `child` between delimiters l and r (characters, 0 = none) */
static int fence(bxpool_t *p, int lvl, int l, int r, int child)
{
    int f = bx_new(p, BX_FENCE, lvl);
    p->b[f].aux = (uint8_t)l;
    p->b[f].aux2 = (uint8_t)r;
    bx_add(p, f, row_of(p, lvl, child));
    return f;
}

/* horizontal space of w pixels */
static int gap(bxpool_t *p, int w)
{
    int gp = bx_new(p, BX_GAP, 0);
    p->b[gp].aux = (uint8_t)w;
    return gp;
}

static res_t p_expr(ps_t *ps, int lvl);
static res_t p_unary(ps_t *ps, int lvl);

/* consume a token of `type`, or fail the parse */
static bool expect(ps_t *ps, int type)
{
    if (ps->t.type != type) {
        ps->err = true;
        return false;
    }
    lex(ps);
    return true;
}

/* comma list up to `close` (the opening token is already consumed); returns
 * the number of items. Only the first `max` are stored in out[]; callers
 * treat a count > max as an error. */
static int p_list(ps_t *ps, int lvl, int close, res_t *out, int max)
{
    int n = 0;
    if (ps->t.type == close) {
        lex(ps);
        return 0;
    }
    for (;;) {
        res_t r = p_expr(ps, lvl);
        if (ps->err)
            return n;
        if (n < max)
            out[n] = r;
        n++;
        if (ps->t.type == T_COMMA) {
            lex(ps);
            continue;
        }
        expect(ps, close);
        return n;
    }
}

/* row "a, b, c": items separated by "," and a 4 px gap (3 boxes per item
 * but the first; all_rows() relies on this to count matrix columns) */
static int join(bxpool_t *p, int lvl, const res_t *it, int n)
{
    int r = bx_new(p, BX_ROW, lvl);
    for (int k = 0; k < n; k++) {
        if (k) {
            bx_add(p, r, text(p, lvl, ",", 0));
            bx_add(p, r, gap(p, 4));
        }
        bx_add(p, r, it[k].box);
    }
    return r;
}

/* ------------------------------------------------------------ primaries */
/* A number literal: mantissa text, the repeating group (without its
 * parentheses) with an overline, and an exponent as ×10 with a superscript
 * (a minus shown as the typographic "−"). Always returns a row. */
static res_t number(ps_t *ps, int lvl)
{
    bxpool_t *p = ps->p;
    const tok_t t = ps->t;
    lex(ps);
    int mant = t.exp >= 0 ? t.exp : t.len;
    int r = bx_new(p, BX_ROW, lvl);
    if (t.rep >= 0) {
        bx_add(p, r, bx_text(p, lvl, t.s, t.rep, 0));
        int rl = mant - t.rep - 2; /* digits inside ( ) */
        bx_add(p, r, bx_text(p, lvl, t.s + t.rep + 1, rl, BXF_OVERLINE));
    } else {
        bx_add(p, r, bx_text(p, lvl, t.s, mant, 0));
    }
    if (t.exp >= 0) { /* 1.5ᴇ-7 -> 1.5×10⁻⁷ */
        bx_add(p, r, text(p, lvl, "×10", 0));
        const char *e = t.s + t.exp + t.explen;
        int el = t.len - t.exp - t.explen;
        int sup = bx_new(p, BX_ROW, lvl + 1);
        if (*e == '-' || starts(e, "−") || starts(e, "⁻")) {
            bx_add(p, sup, text(p, lvl + 1, "−", 0));
            int k = *e == '-' ? 1 : 3;
            e += k;
            el -= k;
        } else if (*e == '+') {
            e++;
            el--;
        }
        bx_add(p, sup, bx_text(p, lvl + 1, e, el, 0));
        bx_add(p, r, bx_wrap(p, BX_SUP, lvl, sup));
        return mk(r, A_NUM, A_OTHER);
    }
    return mk(r, A_NUM, A_NUM);
}

/* An identifier; "x_1" (an underscore inside the name) shows the part after
 * the first "_" as a subscript. */
static res_t ident(ps_t *ps, int lvl)
{
    bxpool_t *p = ps->p;
    const tok_t t = ps->t;
    lex(ps);
    const char *us = memchr(t.s, '_', (size_t)t.len);
    if (us && us > t.s && us + 1 < t.s + t.len) { /* x_1 */
        int r = bx_new(p, BX_ROW, lvl);
        bx_add(p, r, bx_text(p, lvl, t.s, (int)(us - t.s), 0));
        int sub = bx_new(p, BX_ROW, lvl + 1);
        bx_add(p, sub, bx_text(p, lvl + 1, us + 1, (int)(t.s + t.len - us - 1), 0));
        bx_add(p, r, bx_wrap(p, BX_SUB, lvl, sub));
        return mk(r, A_ID, A_ID);
    }
    return mk(bx_text(p, lvl, t.s, t.len, 0), A_ID, A_ID);
}

/* the token's text is exactly n */
static bool name_is(const tok_t *t, const char *n) { return (int)strlen(n) == t->len && !strncmp(t->s, n, (size_t)t->len); }

/* f(x) with parentheses unless f is a single atom: the operand of d/dx keeps
 * or gets parentheses when it is parenthesised, a sum, or starts with
 * something that is not a number/name/fence/fraction */
static int arg_box(bxpool_t *p, int lvl, res_t r)
{
    if (r.paren || r.additive || r.first == A_OTHER)
        return fence(p, lvl, '(', ')', strip(p, r));
    return r.box;
}

/* ∇f, ∇·F, ∇×F, ∇²f (kind 0..3) with the operand box `arg` */
static res_t nabla(bxpool_t *p, int lvl, int kind, int arg)
{
    int r = bx_new(p, BX_ROW, lvl);
    bx_add(p, r, text(p, lvl, kind == 1 ? "∇·" : kind == 2 ? "∇×" : "∇", 0));
    if (kind == 3) {
        int sup = bx_new(p, BX_ROW, lvl + 1);
        bx_add(p, sup, text(p, lvl + 1, "2", 0));
        bx_add(p, r, bx_wrap(p, BX_SUP, lvl, sup));
    }
    bx_add(p, r, arg);
    return mk(r, A_OTHER, A_OTHER);
}

/* A function call (the name token is current; its "(" is already consumed).
 * Textbook forms:
 *   sqrt(a)                  √ with a vinculum
 *   abs(a)                   |a|
 *   exp(a)                   e^a
 *   log(a, b)                log_b(a)
 *   diff(f, x[, n])          d/dx f, dⁿ/dxⁿ f
 *   integrate(f, x)          ∫ f dx;  integrate(f, x, a, b): with limits
 *   sum/product(f, k, a, b)  Σ/Π with k=a below and b above
 *   limit(f, x, a)           lim with x→a below
 * Anything else (or a different argument count) is name(a, b, ...), at most 6
 * arguments. Operands of big operators get parentheses only when they are
 * sums. */
static res_t func(ps_t *ps, int lvl)
{
    bxpool_t *p = ps->p;
    const tok_t f = ps->t;
    lex(ps);
    res_t a[6];
    int n = p_list(ps, lvl, T_RP, a, 6);
    if (ps->err)
        return mk(-1, A_OTHER, A_OTHER);
    static const char *const del[] = {"grad", "divergence", "curl", "laplacian"};
    for (int k = 0; n == 1 && k < 4; k++)
        if (name_is(&f, del[k])) { /* ∇ notation; the operand in parentheses unless atomic */
            int b = a[0].box;
            bool atom = p->b[b].kind == BX_TEXT || (p->b[b].kind == BX_ROW && bx_nchildren(p, b) == 1) ||
                        (a[0].first == A_FENCE && a[0].last == A_FENCE && !a[0].additive);
            return nabla(p, lvl, k, atom ? b : fence(p, lvl, '(', ')', b));
        }
    if (n == 1 && name_is(&f, "sqrt")) {
        int r = bx_new(p, BX_ROOT, lvl);
        bx_add(p, r, row_of(p, lvl, strip(p, a[0])));
        return mk(r, A_FENCE, A_FENCE);
    }
    if (n == 1 && name_is(&f, "abs"))
        return mk(fence(p, lvl, '|', '|', strip(p, a[0])), A_FENCE, A_FENCE);
    if (n == 1 && name_is(&f, "exp")) {
        int r = bx_new(p, BX_ROW, lvl);
        bx_add(p, r, text(p, lvl, "e", 0));
        int e = strip(p, a[0]);
        relevel(p, e, 1);
        bx_add(p, r, bx_wrap(p, BX_SUP, lvl, row_of(p, lvl + 1, e)));
        return mk(r, A_ID, A_OTHER);
    }
    if (n == 2 && name_is(&f, "log")) {
        int r = bx_new(p, BX_ROW, lvl);
        bx_add(p, r, text(p, lvl, "log", 0));
        int b = strip(p, a[1]);
        relevel(p, b, 1);
        bx_add(p, r, bx_wrap(p, BX_SUB, lvl, row_of(p, lvl + 1, b)));
        bx_add(p, r, fence(p, lvl, '(', ')', strip(p, a[0])));
        return mk(r, A_FENCE, A_FENCE);
    }
    if ((n == 2 || n == 3) && name_is(&f, "diff")) {
        int r = bx_new(p, BX_ROW, lvl);
        int num = bx_new(p, BX_ROW, lvl), den = bx_new(p, BX_ROW, lvl);
        bx_add(p, num, text(p, lvl, "d", 0));
        bx_add(p, den, text(p, lvl, "d", 0));
        bx_add(p, den, a[1].box);
        if (n == 3) {
            int o1 = strip(p, a[2]);
            relevel(p, o1, 1);
            bx_add(p, num, bx_wrap(p, BX_SUP, lvl, row_of(p, lvl + 1, o1)));
            /* the order once more for the denominator: a second copy of its
             * text (a name is a text box, a number a row holding one; any
             * other order shows as "n") */
            int ot = o1;
            if (p->b[ot].kind == BX_ROW && bx_nchildren(p, ot) == 1)
                ot = p->b[ot].child;
            int o2 = p->b[ot].kind == BX_TEXT ? bx_text(p, lvl + 1, p->b[ot].s, p->b[ot].len, 0)
                                              : bx_text(p, lvl + 1, "n", 1, 0);
            bx_add(p, den, bx_wrap(p, BX_SUP, lvl, row_of(p, lvl + 1, o2)));
        }
        int fr = bx_new(p, BX_FRAC, lvl);
        bx_add(p, fr, num);
        bx_add(p, fr, den);
        bx_add(p, r, fr);
        bx_add(p, r, arg_box(p, lvl, a[0]));
        return mk(r, A_FRAC, A_FENCE);
    }
    bool integ = name_is(&f, "integrate") && (n == 2 || n == 4);
    bool sum = (name_is(&f, "sum") || name_is(&f, "product")) && n == 4;
    bool lim = name_is(&f, "limit") && n == 3;
    if (integ || sum || lim) {
        int r = bx_new(p, BX_ROW, lvl);
        int op = bx_new(p, BX_BIGOP, lvl);
        p->b[op].aux = (uint8_t)(integ ? BIG_INT : lim ? BIG_LIM : name_is(&f, "sum") ? BIG_SUM : BIG_PROD);
        int lo = bx_new(p, BX_ROW, lvl + 1), up = bx_new(p, BX_ROW, lvl + 1);
        if (integ && n == 4) {
            int x = strip(p, a[2]), y = strip(p, a[3]);
            relevel(p, x, 1);
            relevel(p, y, 1);
            bx_add(p, lo, x);
            bx_add(p, up, y);
        } else if (sum) {
            relevel(p, a[1].box, 1);
            relevel(p, a[2].box, 1);
            relevel(p, a[3].box, 1);
            bx_add(p, lo, a[1].box);
            bx_add(p, lo, text(p, lvl + 1, "=", 0));
            bx_add(p, lo, a[2].box);
            bx_add(p, up, a[3].box);
        } else if (lim) {
            relevel(p, a[1].box, 1);
            relevel(p, a[2].box, 1);
            bx_add(p, lo, a[1].box);
            bx_add(p, lo, text(p, lvl + 1, "→", 0));
            bx_add(p, lo, a[2].box);
        }
        bx_add(p, op, lo);
        bx_add(p, op, up);
        bx_add(p, r, op);
        bx_add(p, r, gap(p, 2));
        bx_add(p, r, a[0].additive ? fence(p, lvl, '(', ')', strip(p, a[0])) : a[0].box);
        if (integ) {
            bx_add(p, r, gap(p, 3));
            bx_add(p, r, text(p, lvl, "d", 0));
            bx_add(p, r, a[1].box);
        }
        return mk(r, A_OTHER, integ ? A_ID : A_OTHER);
    }
    /* any other function: name(args); the O of a series' order term in bold */
    int r = bx_new(p, BX_ROW, lvl);
    bx_add(p, r, bx_text(p, lvl, f.s, f.len, name_is(&f, "O") ? BXF_BOLD : 0));
    bx_add(p, r, fence(p, lvl, '(', ')', join(p, lvl, a, n < 6 ? n : 6)));
    if (n > 6)
        ps->err = true;
    return mk(r, A_FENCE, A_FENCE);
}

/* Is a bracket list a matrix (every item a [..] list of the same length, at
 * most 12x12)? *cols gets the column count. */
static bool all_rows(const bxpool_t *p, const res_t *a, int n, int *cols)
{
    /* [[..],[..]]: every item is a bracketed list with the same length */
    *cols = -1;
    for (int k = 0; k < n; k++) {
        int b = a[k].box;
        if (p->b[b].kind != BX_FENCE || p->b[b].aux != '[')
            return false;
        int inner = p->b[b].child; /* row: items separated by "," + gap */
        int items = (bx_nchildren(p, inner) + 2) / 3;
        if (*cols >= 0 && items != *cols)
            return false;
        *cols = items;
    }
    return n > 0 && *cols > 0 && *cols <= 12 && n <= 12;
}

/* primary: number, name, call, √(..), parenthesised expression or tuple,
 * [list] (or a matrix), {set} */
static res_t prim(ps_t *ps, int lvl)
{
    bxpool_t *p = ps->p;
    switch (ps->t.type) {
    case T_NUM:
        return number(ps, lvl);
    case T_ID:
        return ident(ps, lvl);
    case T_FUNC:
        return func(ps, lvl);
    case T_NABLA: { /* as typed: ∇ then the operand with its own parentheses */
        int k = ps->t.op;
        lex(ps);
        res_t a = p_unary(ps, lvl);
        if (ps->err)
            break;
        return nabla(p, lvl, k, a.box);
    }
    case T_SQRT: {
        lex(ps);
        res_t a = p_expr(ps, lvl);
        expect(ps, T_RP);
        if (ps->err)
            break;
        int r = bx_new(p, BX_ROOT, lvl);
        bx_add(p, r, row_of(p, lvl, strip(p, a)));
        return mk(r, A_FENCE, A_FENCE);
    }
    case T_LP: {
        lex(ps);
        res_t a[6];
        int n = p_list(ps, lvl, T_RP, a, 6);
        if (ps->err || n < 1 || n > 6)
            break;
        res_t r = mk(fence(p, lvl, '(', ')', n == 1 ? a[0].box : join(p, lvl, a, n)), A_FENCE, A_FENCE);
        r.paren = n == 1; /* a grouping, which 2D layouts may drop (strip) */
        return r;
    }
    case T_LB: {
        lex(ps);
        res_t a[12];
        int n = p_list(ps, lvl, T_RB, a, 12);
        if (ps->err || n > 12)
            break;
        int cols;
        if (all_rows(p, a, n, &cols)) { /* matrix: take the cells out of the row lists */
            int m = bx_new(p, BX_MATRIX, lvl);
            p->b[m].aux = (uint8_t)cols;
            for (int k = 0; k < n; k++) {
                int inner = p->b[a[k].box].child;
                int c = p->b[inner].child, idx = 0;
                while (c >= 0) {
                    int nx = p->b[c].next;
                    if (idx % 3 == 0)
                        bx_add(p, m, row_of(p, lvl, c));
                    idx++;
                    c = nx;
                }
            }
            return mk(m, A_FENCE, A_FENCE);
        }
        return mk(fence(p, lvl, '[', ']', join(p, lvl, a, n)), A_FENCE, A_FENCE);
    }
    case T_LC: {
        lex(ps);
        res_t a[12];
        int n = p_list(ps, lvl, T_RC, a, 12);
        if (ps->err || n > 12)
            break;
        return mk(fence(p, lvl, '{', '}', join(p, lvl, a, n)), A_FENCE, A_FENCE);
    }
    default:
        break;
    }
    ps->err = true;
    return mk(-1, A_OTHER, A_OTHER);
}

/* ------------------------------------------------------------ operators */
/* prim followed by postfix operators: ² ³ ⁻¹ become superscripts; ! ° ʳ '
 * stay as text after the operand */
static res_t postfix(ps_t *ps, int lvl)
{
    bxpool_t *p = ps->p;
    res_t r = prim(ps, lvl);
    while (!ps->err && ps->t.type == T_POST) {
        int row = row_of(p, lvl, r.box);
        r.box = row;
        int op = ps->t.op;
        if (op == P_TEXT) {
            bx_add(p, row, bx_text(p, lvl, ps->t.s, ps->t.len, 0));
        } else {
            int sup = bx_new(p, BX_ROW, lvl + 1);
            bx_add(p, sup, text(p, lvl + 1, op == P_SQ ? "2" : op == P_CUBE ? "3" : "−1", 0));
            bx_add(p, row, bx_wrap(p, BX_SUP, lvl, sup));
        }
        r.last = A_OTHER;
        r.paren = false;
        lex(ps);
    }
    return r;
}

/* digits or a short name (<= 3 bytes), looking through rows with a single
 * child: decides whether x^(1/n) can be shown as a root with index n */
static bool simple_text(const bxpool_t *p, int b, const char **s, int *len)
{
    while (b >= 0 && p->b[b].kind == BX_ROW && p->b[b].child >= 0 && p->b[p->b[b].child].next < 0)
        b = p->b[b].child;
    if (b < 0 || p->b[b].kind != BX_TEXT || p->b[b].len > 3)
        return false;
    *s = p->b[b].s;
    *len = p->b[b].len;
    return true;
}

/* postfix [^ unary]: the exponent is parsed by p_unary, which recurses back
 * into power(), so a^b^c groups as a^(b^c). x^(1/2) becomes √x and x^(1/n)
 * (n short text) the n-th root with a small index; other exponents are a
 * superscript one level smaller, without their outer parentheses. */
static res_t power(ps_t *ps, int lvl)
{
    bxpool_t *p = ps->p;
    res_t base = postfix(ps, lvl);
    if (ps->err || ps->t.type != T_OP || ps->t.op != O_POW)
        return base;
    lex(ps);
    res_t e = p_unary(ps, lvl);
    if (ps->err)
        return base;
    int eb = strip(p, e);
    /* x^(1/n) -> n-th root */
    if (p->b[eb].kind == BX_ROW && p->b[eb].child >= 0 && p->b[p->b[eb].child].next < 0)
        eb = p->b[eb].child;
    if (p->b[eb].kind == BX_FRAC) {
        const char *ns, *ds;
        int nl, dl;
        int nb = p->b[eb].child, db = p->b[nb].next;
        if (simple_text(p, nb, &ns, &nl) && nl == 1 && *ns == '1' && simple_text(p, db, &ds, &dl)) {
            int r = bx_new(p, BX_ROOT, lvl);
            bx_add(p, r, row_of(p, lvl, strip(p, base)));
            if (!(dl == 1 && *ds == '2')) {
                int ix = bx_new(p, BX_ROW, lvl + 2);
                bx_add(p, ix, bx_text(p, lvl + 2, ds, dl, 0));
                bx_add(p, r, ix);
            }
            return mk(r, A_FENCE, A_FENCE);
        }
    }
    eb = strip(p, e);
    relevel(p, eb, 1);
    int row = bx_new(p, BX_ROW, lvl);
    bx_add(p, row, base.box);
    bx_add(p, row, bx_wrap(p, BX_SUP, lvl, row_of(p, lvl + 1, eb)));
    return mk(row, base.first, A_OTHER);
}

/* unary sign: "+" vanishes, "-"/"⁻" becomes "−" in front of the operand */
static res_t p_unary(ps_t *ps, int lvl)
{
    bxpool_t *p = ps->p;
    if (ps->t.type == T_OP && (ps->t.op == O_SUB || ps->t.op == O_NEG || ps->t.op == O_ADD)) {
        bool neg = ps->t.op != O_ADD;
        lex(ps);
        res_t r = p_unary(ps, lvl);
        if (!neg || ps->err)
            return r;
        int row = bx_new(p, BX_ROW, lvl);
        bx_add(p, row, text(p, lvl, "−", 0));
        bx_add(p, row, r.box);
        return mk(row, A_OTHER, r.last);
    }
    return power(ps, lvl);
}

/* can this token start an operand (implicit multiplication: 2x, 2(x+1))? */
static bool starts_operand(const tok_t *t)
{
    return t->type == T_NUM || t->type == T_ID || t->type == T_FUNC || t->type == T_SQRT || t->type == T_LP ||
           t->type == T_LB || t->type == T_LC || t->type == T_NABLA;
}

/* Products and quotients, left to right. Factors are collected in one row
 * (built lazily, joined by a 1 px gap, a third of a character between two
 * names (x y z, so it differs from the name xyz) or, for an explicit ×
 * before a number, a "·"). A "/" turns everything accumulated so far into the numerator of a
 * stacked fraction and the next unary into its denominator, so a*b/c is
 * (a·b)/c and a/b/c is (a/b)/c. Outer parentheses of both parts are dropped:
 * the fraction bar groups them. */
static res_t p_mul(ps_t *ps, int lvl)
{
    bxpool_t *p = ps->p;
    res_t l = p_unary(ps, lvl);
    if (ps->err)
        return l;
    int row = -1; /* built lazily */
    res_t acc = l;
    for (;;) {
        bool explicit_mul = ps->t.type == T_OP && ps->t.op == O_MUL;
        bool div = ps->t.type == T_OP && ps->t.op == O_DIV;
        bool implicit = starts_operand(&ps->t);
        if (!explicit_mul && !div && !implicit)
            break;
        if (!implicit)
            lex(ps);
        res_t r = p_unary(ps, lvl);
        if (ps->err)
            return acc;
        if (div) {
            int num = row >= 0 ? row : strip(p, acc);
            int den = strip(p, r);
            if (lvl > 0) {
                relevel(p, num, 1);
                relevel(p, den, 1);
            }
            int lv = lvl > 0 ? lvl + 1 : lvl;
            int fr = bx_new(p, BX_FRAC, lvl);
            bx_add(p, fr, row_of(p, lv, num));
            bx_add(p, fr, row_of(p, lv, den));
            row = -1;
            acc = mk(fr, A_FRAC, A_FRAC);
            continue;
        }
        if (row < 0) {
            row = bx_new(p, BX_ROW, lvl);
            bx_add(p, row, acc.box);
        }
        bool dot = explicit_mul && (r.first == A_NUM || r.first == A_OTHER);
        if (dot)
            bx_add(p, row, text(p, lvl, "·", 0));
        else if (acc.last == A_ID && r.first == A_ID)
            /* name after name: a third of a character apart, so the product
             * x*y*z reads "x y z", not the one variable xyz */
            bx_add(p, row, gap(p, (bx_font(lvl)->w + 2) / 3));
        else
            bx_add(p, row, gap(p, 1));
        bx_add(p, row, r.box);
        acc = mk(row, acc.first, r.last);
    }
    return acc;
}

/* Sums and differences with an optional leading sign (−, ±): one row with
 * padded "+", "−", "±" between the terms; marked `additive` so that callers
 * add parentheses where precedence requires them. */
static res_t p_add(ps_t *ps, int lvl)
{
    bxpool_t *p = ps->p;
    int row = -1;
    bool lead_neg = false;
    const char *lead = "−";
    if (ps->t.type == T_OP && (ps->t.op == O_SUB || ps->t.op == O_NEG || ps->t.op == O_PM)) {
        lead_neg = true;
        if (ps->t.op == O_PM)
            lead = "±";
        lex(ps);
    }
    res_t r = p_mul(ps, lvl);
    if (ps->err)
        return r;
    res_t acc = r;
    if (lead_neg) {
        row = bx_new(p, BX_ROW, lvl);
        bx_add(p, row, text(p, lvl, lead, 0));
        bx_add(p, row, r.box);
        acc = mk(row, A_OTHER, r.last);
    }
    while (ps->t.type == T_OP && (ps->t.op == O_ADD || ps->t.op == O_SUB || ps->t.op == O_PM)) {
        const char *optext = ps->t.op == O_ADD ? "+" : ps->t.op == O_SUB ? "−" : "±";
        lex(ps);
        res_t t = p_mul(ps, lvl);
        if (ps->err)
            return acc;
        if (row < 0) {
            row = bx_new(p, BX_ROW, lvl);
            bx_add(p, row, acc.box);
        }
        bx_add(p, row, text(p, lvl, optext, BXF_PAD));
        bx_add(p, row, t.box);
        acc = mk(row, acc.first, t.last);
        acc.additive = true;
    }
    return acc;
}

/* Relations (=, <, :=, →, ∠, ...) between sums, shown with wide padding;
 * also the recursion guard for nested expressions. */
static res_t p_expr(ps_t *ps, int lvl)
{
    bxpool_t *p = ps->p;
    if (++ps->depth > 40) {
        ps->err = true;
        return mk(-1, A_OTHER, A_OTHER);
    }
    res_t acc = p_add(ps, lvl);
    int row = -1;
    while (!ps->err && ps->t.type == T_OP && ps->t.op == O_REL) {
        const tok_t t = ps->t;
        lex(ps);
        res_t r = p_add(ps, lvl);
        if (ps->err)
            break;
        if (row < 0) {
            row = bx_new(p, BX_ROW, lvl);
            bx_add(p, row, acc.box);
        }
        bx_add(p, row, bx_text(p, lvl, t.s, t.len, BXF_WIDEPAD));
        bx_add(p, row, r.box);
        acc = mk(row, A_OTHER, A_OTHER);
    }
    ps->depth--;
    return acc;
}

/* Parse all of `s` at nesting level lvl into boxes of pool p. Returns the
 * root row, or -1 if the text is empty, not understood, has trailing input,
 * or the pool overflowed (the caller then draws the linear text). The boxes
 * reference `s`. Not yet measured: call bx_measure / bx_place / bx_draw. */
int m2d_parse(bxpool_t *p, const char *s, int lvl)
{
    ps_t ps = {p, s, {0}, false, 0};
    lex(&ps);
    if (ps.t.type == T_END)
        return -1;
    res_t r = p_expr(&ps, lvl);
    if (ps.err || ps.t.type != T_END || p->overflow || r.box < 0)
        return -1;
    return row_of(p, lvl, r.box);
}
