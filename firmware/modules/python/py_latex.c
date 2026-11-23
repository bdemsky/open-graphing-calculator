/*
 * LaTeX from the CAS's linear form (calc.latex, e.latex(), a notebook
 * cell's value): "sqrt(x)/2" -> "\frac{\sqrt{x}}{2}".
 *
 * The CAS has its own LaTeX printer (cas_latex), but it is not part of the
 * firmware (nothing else uses it, and internal flash has no room for it), so
 * the module renders the linear text the CAS prints, which already carries
 * every parenthesis the meaning needs. Recursive descent:
 *
 *   rel    := sum (( = | < | > | <= | >= | != ) sum)*
 *   sum    := term (( + | - ) term)*
 *   term   := unary (( * | / ) unary)*      products over / become \frac
 *   unary  := - unary | power
 *   power  := post ( ^ unary )?
 *   post   := atom !*
 *   atom   := number | name | name ( args ) | ( rel ) | [ items ] | { items }
 *
 * Text it does not understand is copied as it is (escaped), so the result is
 * always usable.
 */
#include <string.h>
#include "py_mod.h"
#include "py/runtime.h"

typedef struct {
    const char *s;
    size_t i, n;
} lx_t;

enum { K_ATOM, K_GROUP, K_POW, K_PROD, K_FRAC, K_NEG, K_SUM, K_REL };

static int lx_rel(lx_t *p, vstr_t *o);
static int lx_unary(lx_t *p, vstr_t *o);

static char peek(lx_t *p) { return p->i < p->n ? p->s[p->i] : 0; }
static bool isal(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool isdig(char c) { return c >= '0' && c <= '9'; }

static void put_name(vstr_t *o, const char *s, size_t n)
{
    static const char greek[][8] = {"alpha", "beta", "gamma", "delta", "epsilon", "theta", "lambda", "mu",
                                    "pi", "rho", "sigma", "tau", "phi", "omega", "Gamma", "Delta", "Omega"};
    for (size_t k = 0; k < MP_ARRAY_SIZE(greek); k++)
        if (strlen(greek[k]) == n && !memcmp(greek[k], s, n)) {
            vstr_add_char(o, '\\');
            vstr_add_strn(o, s, n);
            vstr_add_char(o, ' ');
            return;
        }
    if (n == 3 && !memcmp(s, "inf", 3)) {
        vstr_add_str(o, "\\infty ");
        return;
    }
    if (n > 1) {
        vstr_add_str(o, "\\mathrm{");
        for (size_t k = 0; k < n; k++) {
            if (s[k] == '_')
                vstr_add_char(o, '\\');
            vstr_add_char(o, s[k]);
        }
        vstr_add_char(o, '}');
        return;
    }
    vstr_add_strn(o, s, n);
}

/* items of ( ... ), [ ... ] or { ... } separated by commas, each a rel; *rows counts nested rows */
static int lx_items(lx_t *p, vstr_t *o, char close, const char *sep)
{
    int count = 0;
    while (peek(p) && peek(p) != close) {
        if (count)
            vstr_add_str(o, sep);
        lx_rel(p, o);
        count++;
        if (peek(p) == ',')
            p->i++;
        else
            break;
    }
    if (peek(p) == close)
        p->i++;
    return count;
}

/* [[a,b],[c,d]] -> pmatrix */
static bool lx_matrix(lx_t *p, vstr_t *o)
{
    size_t save = p->i, olen = o->len;
    if (peek(p) != '[')
        return false;
    p->i++; /* the outer bracket; then one [ ... ] per row */
    vstr_add_str(o, "\\begin{pmatrix}");
    int rows = 0;
    while (peek(p) == '[') {
        p->i++;
        if (rows)
            vstr_add_str(o, "\\\\");
        lx_items(p, o, ']', " & ");
        rows++;
        if (peek(p) == ',')
            p->i++;
    }
    if (peek(p) != ']') {
        p->i = save, o->len = olen;
        return false;
    }
    p->i++;
    vstr_add_str(o, "\\end{pmatrix}");
    return true;
}

static int lx_atom(lx_t *p, vstr_t *o)
{
    char c = peek(p);
    if (isdig(c) || (c == '.' && isdig(p->i + 1 < p->n ? p->s[p->i + 1] : 0))) {
        while (isdig(peek(p)) || peek(p) == '.')
            vstr_add_char(o, p->s[p->i++]);
        if ((peek(p) == 'e' || peek(p) == 'E') && p->i + 1 < p->n &&
            (isdig(p->s[p->i + 1]) || p->s[p->i + 1] == '-' || p->s[p->i + 1] == '+')) {
            p->i++;
            vstr_add_str(o, "\\times 10^{");
            if (peek(p) == '-' || peek(p) == '+')
                vstr_add_char(o, p->s[p->i++]);
            while (isdig(peek(p)))
                vstr_add_char(o, p->s[p->i++]);
            vstr_add_char(o, '}');
        }
        return K_ATOM;
    }
    if (isal(c)) {
        size_t st = p->i;
        while (isal(peek(p)) || isdig(peek(p)))
            p->i++;
        const char *nm = p->s + st;
        size_t nl = p->i - st;
        if (peek(p) != '(') {
            put_name(o, nm, nl);
            return K_ATOM;
        }
        p->i++;
#define IS(w) (nl == sizeof(w) - 1 && !memcmp(nm, w, nl))
        if (IS("sqrt")) {
            vstr_add_str(o, "\\sqrt{");
            lx_items(p, o, ')', ",");
            vstr_add_char(o, '}');
        } else if (IS("abs")) {
            vstr_add_str(o, "\\left|");
            lx_items(p, o, ')', ",");
            vstr_add_str(o, "\\right|");
        } else if (IS("exp")) {
            vstr_add_str(o, "e^{");
            lx_items(p, o, ')', ",");
            vstr_add_char(o, '}');
            return K_POW;
        } else if (IS("grad") || IS("divergence") || IS("curl") || IS("laplacian")) {
            /* one argument (the variables inferred, cas/src/parse.c): del notation */
            vstr_t a;
            vstr_init(&a, 16);
            size_t at = p->i;
            if (lx_items(p, &a, ')', ", ") == 1) {
                vstr_add_str(o, IS("grad") ? "\\nabla " : IS("divergence") ? "\\nabla\\cdot "
                                                         : IS("curl")      ? "\\nabla\\times "
                                                                           : "\\nabla^{2} ");
                bool atom = a.len == 1 || (a.len >= 6 && !memcmp(a.buf, "\\left[", 6));
                vstr_add_str(o, atom ? "" : "\\left(");
                vstr_add_strn(o, a.buf, a.len);
                vstr_add_str(o, atom ? "" : "\\right)");
                vstr_clear(&a);
                return K_ATOM;
            }
            vstr_clear(&a);
            p->i = at; /* several arguments: an ordinary call */
            vstr_add_str(o, "\\operatorname{");
            vstr_add_strn(o, nm, nl);
            vstr_add_str(o, "}\\left(");
            lx_items(p, o, ')', ", ");
            vstr_add_str(o, "\\right)");
        } else {
            static const char known[][8] = {"sin", "cos", "tan", "cot", "sec", "csc", "sinh", "cosh", "tanh",
                                            "coth", "ln", "log", "exp", "arg", "det", "gcd", "max", "min"};
            static const char arcs[][6] = {"asin", "acos", "atan"};
            bool k = false;
            for (size_t j = 0; j < MP_ARRAY_SIZE(known) && !k; j++)
                k = strlen(known[j]) == nl && !memcmp(known[j], nm, nl);
            bool arc = false;
            for (size_t j = 0; j < MP_ARRAY_SIZE(arcs) && !arc; j++)
                arc = strlen(arcs[j]) == nl && !memcmp(arcs[j], nm, nl);
            if (k) {
                vstr_add_char(o, '\\');
                vstr_add_strn(o, nm, nl);
            } else if (arc) {
                vstr_add_str(o, "\\arc");
                vstr_add_strn(o, nm + 1, nl - 1);
            } else {
                vstr_add_str(o, "\\operatorname{");
                vstr_add_strn(o, nm, nl);
                vstr_add_char(o, '}');
            }
            vstr_add_str(o, "\\left(");
            lx_items(p, o, ')', ", ");
            vstr_add_str(o, "\\right)");
        }
#undef IS
        return K_ATOM;
    }
    if (c == '(') {
        p->i++;
        vstr_add_str(o, "\\left(");
        lx_rel(p, o);
        if (peek(p) == ')')
            p->i++;
        vstr_add_str(o, "\\right)");
        return K_GROUP;
    }
    if (c == '[') {
        if (p->i + 1 < p->n && p->s[p->i + 1] == '[' && lx_matrix(p, o))
            return K_ATOM;
        p->i++;
        vstr_add_str(o, "\\left[");
        lx_items(p, o, ']', ", ");
        vstr_add_str(o, "\\right]");
        return K_ATOM;
    }
    if (c == '{') {
        p->i++;
        vstr_add_str(o, "\\left\\{");
        lx_items(p, o, '}', ", ");
        vstr_add_str(o, "\\right\\}");
        return K_ATOM;
    }
    if (c == '\xe2' && p->i + 2 < p->n && !memcmp(p->s + p->i, "∇", 3)) { /* typed del */
        p->i += 3;
        vstr_add_str(o, "\\nabla ");
        return K_ATOM;
    }
    if (c) { /* anything else: copied */
        if (strchr("#$%&~^\\", c))
            vstr_add_char(o, '\\');
        vstr_add_char(o, c);
        p->i++;
    }
    return K_ATOM;
}

/* a group's \left( ... \right) taken off: ( a+b )/2 -> \frac{a+b}{2} */
static void strip_group(vstr_t *v)
{
    if (v->len >= 13 && !memcmp(v->buf, "\\left(", 6) && !memcmp(v->buf + v->len - 7, "\\right)", 7)) {
        memmove(v->buf, v->buf + 6, v->len - 13);
        v->len -= 13;
    }
}

static int lx_post(lx_t *p, vstr_t *o)
{
    int k = lx_atom(p, o);
    while (peek(p) == '!') {
        p->i++;
        vstr_add_char(o, '!');
        k = K_ATOM;
    }
    return k;
}

static int lx_power(lx_t *p, vstr_t *o)
{
    int k = lx_post(p, o);
    if (peek(p) != '^')
        return k;
    p->i++;
    vstr_t e;
    vstr_init(&e, 16);
    int ek = lx_unary(p, &e);
    if (ek == K_GROUP)
        strip_group(&e);
    vstr_add_str(o, "^{");
    vstr_add_strn(o, e.buf, e.len);
    vstr_add_char(o, '}');
    vstr_clear(&e);
    return K_POW;
}

static int lx_unary(lx_t *p, vstr_t *o)
{
    if (peek(p) == '-') {
        p->i++;
        vstr_add_char(o, '-');
        lx_unary(p, o);
        return K_NEG;
    }
    return lx_power(p, o);
}

/* factors joined: juxtaposed, or \cdot before a digit */
static void join(vstr_t *o, vstr_t *f)
{
    if (o->len && f->len && (isdig(f->buf[0]) || f->buf[0] == '-'))
        vstr_add_str(o, " \\cdot ");
    else if (o->len && f->len && isal(o->buf[o->len - 1]) && isal(f->buf[0]))
        vstr_add_char(o, ' ');
    vstr_add_strn(o, f->buf, f->len);
}

static int lx_term(lx_t *p, vstr_t *o)
{
    vstr_t num, den, f;
    vstr_init(&num, 32);
    vstr_init(&den, 16);
    vstr_init(&f, 16);
    int nf = 0, nd = 0, k = K_ATOM;
    bool over = false;
    for (;;) {
        vstr_reset(&f);
        k = lx_unary(p, &f);
        if (over && k == K_GROUP && peek(p) != '*' && peek(p) != '/')
            strip_group(&f);
        if (over)
            join(&den, &f), nd++;
        else
            join(&num, &f), nf++;
        char c = peek(p);
        if (c != '*' && c != '/')
            break;
        p->i++;
        over = c == '/';
    }
    if (nd) {
        if (nf == 1 && num.len >= 13 && !memcmp(num.buf, "\\left(", 6))
            strip_group(&num);
        size_t neg = num.len && num.buf[0] == '-'; /* -1/2 -> -\frac{1}{2} */
        vstr_add_str(o, neg ? "-\\frac{" : "\\frac{");
        vstr_add_strn(o, num.buf + neg, num.len - neg);
        vstr_add_str(o, "}{");
        vstr_add_strn(o, den.buf, den.len);
        vstr_add_char(o, '}');
        k = K_FRAC;
    } else {
        vstr_add_strn(o, num.buf, num.len);
        k = nf > 1 ? K_PROD : k;
    }
    vstr_clear(&num);
    vstr_clear(&den);
    vstr_clear(&f);
    return k;
}

static int lx_sum(lx_t *p, vstr_t *o)
{
    int k = lx_term(p, o), n = 1;
    while (peek(p) == '+' || peek(p) == '-') {
        vstr_add_char(o, p->s[p->i++]);
        lx_term(p, o);
        n++;
    }
    return n > 1 ? K_SUM : k;
}

static int lx_rel(lx_t *p, vstr_t *o)
{
    int k = lx_sum(p, o);
    for (;;) {
        char c = peek(p), d = p->i + 1 < p->n ? p->s[p->i + 1] : 0;
        const char *op = NULL;
        size_t len = 1;
        if (c == '<' && d == '=')
            op = " \\le ", len = 2;
        else if (c == '>' && d == '=')
            op = " \\ge ", len = 2;
        else if (c == '!' && d == '=')
            op = " \\ne ", len = 2;
        else if (c == '=' || c == '<' || c == '>')
            op = c == '=' ? " = " : c == '<' ? " < " : " > ";
        if (!op)
            return k;
        p->i += len;
        vstr_add_str(o, op);
        lx_sum(p, o);
        k = K_REL;
    }
}

/* LaTeX of CAS linear text into out */
void py_latex_text(const char *s, char *out, size_t n)
{
    vstr_t o;
    vstr_init(&o, 64);
    lx_t p = {s, 0, strlen(s)};
    while (p.i < p.n) /* (a stray character, a closing bracket or a comma, is copied by lx_atom) */
        lx_rel(&p, &o);
    py_api->snprintf(out, n, "%.*s", (int)o.len, o.buf);
    vstr_clear(&o);
}
