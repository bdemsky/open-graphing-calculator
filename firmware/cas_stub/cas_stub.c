/*
 * cas_stub.c — numeric stand-in for the CAS library (firmware/cas).
 *
 * Implements the whole cas.h contract with double-precision numerics so the
 * calculator application works end to end before the real symbolic engine is
 * available. Select it with `make CAS=stub` (the default).
 *
 * Pipeline: UTF-8 text -> tokens -> AST (node pool inside the context) ->
 *   - tree-walking evaluation (cas_eval), or
 *   - RPN bytecode (cas_compile / cas_fn_eval) for graphing.
 *
 * Symbolic commands get numeric fallbacks where that is meaningful:
 *   diff(f,x,a)          numeric derivative at a (Richardson-extrapolated central difference)
 *   integrate(f,x,a,b)   adaptive Simpson
 *   solve(lhs=rhs,x)     sign-change scan of [-100,100] + bisection, plus tangent zeros
 *   limit(f,x,a)         two-sided numeric limit (a may be inf / -inf)
 *   simplify/approx/factor/expand(f)  evaluate when f has no free variables
 * Anything genuinely symbolic returns CAS_ERR_UNSUPPORTED with a clear message.
 *
 * No malloc: everything lives in the struct cas_ctx placed in the caller's arena.
 *
 * Role in the project: firmware/Makefile builds the calculator against either
 * this file (CAS=stub) or the real engine in firmware/cas (CAS=src / lib).
 * The unit test tests/unit/test_cas_stub.c always uses the stub.  Keeping a
 * small, obviously-correct implementation of cas.h lets the application, the
 * simulator and the UI tests be developed and run without the full engine,
 * and documents the minimum behaviour the application relies on.
 *
 * Data structures (all fixed-size arrays inside struct cas_ctx):
 *   toks[]   token array produced by tokenize(), terminated by T_END;
 *   nodes[]  AST node pool, indices instead of pointers (children a/b, -1 =
 *            none; call arguments form a singly linked list through `next`);
 *            the pool is reset for every input, so ASTs never outlive a call;
 *   vars[]   user variables (name -> double);
 *   fns[]    compiled functions (RPN programs), handed out by cas_compile.
 * Values are plain doubles: a "result" is one number (or a list of roots for
 * solve), ans is a double, and the saved session contains only numbers.
 *
 * Differences from the real engine worth knowing: no symbolic results at
 * all; exact mode only means that results that are close to a fraction with
 * a denominator <= 1000 are shown as that fraction; user functions,
 * assumptions, cas_set_poll and cas_set_stack_limit are accepted but do
 * nothing.
 */
#include "cas.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ limits --- */
#define NAME_MAX_LEN 24   /* identifier length incl. NUL */
#define MAX_TOKENS 512    /* tokens per input line */
#define MAX_NODES 1024    /* AST nodes per input line */
#define MAX_VARS 64       /* user variables */
#define MAX_FNS 64        /* simultaneously live compiled functions */
#define MAX_OPS 192       /* RPN instructions per compiled function */
#define FN_STACK 32       /* evaluation stack depth of a compiled function */
#define MAX_DEPTH 64      /* parser nesting depth (bounds the C recursion) */
#define MAX_BIND 8        /* nested command variable bindings (diff in integrate ...) */
#define MAX_ROOTS 8       /* roots reported by solve */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif
/* NAN/INFINITY are float constants; use double ones to avoid promotion warnings. */
#define DNAN ((double)NAN)
#define DINF ((double)INFINITY)
#define DEG2RAD (M_PI / 180.0)

/* ------------------------------------------------------------- types --- */
/* token kinds; T_ASSIGN is ":=" */
enum tok_kind { T_END, T_NUM, T_NAME, T_OP, T_ASSIGN };

typedef struct {
    uint8_t kind;
    char op; /* for T_OP: + - * / ^ ( ) , = ! and 'r' for sqrt prefix */
    double num;
    char name[NAME_MAX_LEN];
} token;

/* AST node kinds.  Unary nodes (N_NEG, N_FACT, N_SQRT) use only `a`; binary
 * nodes and N_EQ (lhs = rhs) use `a` and `b`; N_NUM stores its value in `v`;
 * N_NAME and N_CALL store the identifier in `name`. */
enum node_kind { N_NUM, N_NAME, N_NEG, N_ADD, N_SUB, N_MUL, N_DIV, N_POW, N_FACT, N_SQRT, N_CALL, N_EQ };

typedef struct {
    uint8_t kind;
    int16_t a, b;    /* children; for N_CALL: a = first arg, b = arg count */
    int16_t next;    /* next argument in a call's argument list */
    double v;
    char name[NAME_MAX_LEN];
} node;

/* user variable slot (used = 0: free) */
typedef struct {
    char name[NAME_MAX_LEN];
    double val;
    int used;
} cvar;

/* Elementary functions (usable in compiled code). */
enum fn_id {
    F_SIN, F_COS, F_TAN, F_ASIN, F_ACOS, F_ATAN, F_SINH, F_COSH, F_TANH, F_ASINH, F_ACOSH,
    F_ATANH, F_SQRT, F_CBRT, F_ABS, F_LN, F_LOG, F_EXP, F_FLOOR, F_CEIL, F_ROUND, F_MIN,
    F_MAX, F_SIGN, F_MOD, F_NTHROOT
};

/* builtin function table entry: name, fn_id and accepted argument counts */
typedef struct {
    const char *name;
    uint8_t id, minargs, maxargs;
} builtin;

static const builtin builtins[] = {
    {"sin", F_SIN, 1, 1},     {"cos", F_COS, 1, 1},     {"tan", F_TAN, 1, 1},
    {"asin", F_ASIN, 1, 1},   {"acos", F_ACOS, 1, 1},   {"atan", F_ATAN, 1, 1},
    {"sinh", F_SINH, 1, 1},   {"cosh", F_COSH, 1, 1},   {"tanh", F_TANH, 1, 1},
    {"asinh", F_ASINH, 1, 1}, {"acosh", F_ACOSH, 1, 1}, {"atanh", F_ATANH, 1, 1},
    {"sqrt", F_SQRT, 1, 1},   {"cbrt", F_CBRT, 1, 1},   {"abs", F_ABS, 1, 1},
    {"ln", F_LN, 1, 1},       {"log", F_LOG, 1, 2},     {"exp", F_EXP, 1, 1},
    {"floor", F_FLOOR, 1, 1}, {"ceil", F_CEIL, 1, 1},   {"round", F_ROUND, 1, 2},
    {"min", F_MIN, 1, 8},     {"max", F_MAX, 1, 8},     {"sign", F_SIGN, 1, 1},
    {"mod", F_MOD, 2, 2},     {"nthroot", F_NTHROOT, 2, 2},
};
#define NBUILTINS ((int)(sizeof builtins / sizeof builtins[0]))

/* CAS commands: arguments are not evaluated up front. */
static const char *const commands[] = {
    "diff", "integrate", "solve", "limit", "simplify", "approx", "factor", "expand",
};
#define NCOMMANDS ((int)(sizeof commands / sizeof commands[0]))

/* names with a fixed meaning (cannot be assigned; resolved by lookup()) */
static const char *const constants[] = {"pi", "e", "inf", "ans", "Ans"};
#define NCONSTANTS ((int)(sizeof constants / sizeof constants[0]))

/* Compiled function (RPN).  A program is a postfix instruction list run on a
 * small double stack: OP_CONST pushes v, OP_X pushes variable number `fn`,
 * OP_NEG / OP_FACT replace the top, binary ops pop two and push one, OP_FN
 * pops `nargs` arguments and pushes apply_fn(fn, ...). */
enum op_code { OP_CONST, OP_X, OP_NEG, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_FACT, OP_FN };

typedef struct {
    uint8_t op, fn, nargs;
    double v;
} rpn_op;

/* A compiled function lives in ctx->fns[] (no allocation); `owner` guards
 * cas_fn_free against foreign pointers, and the angle mode is captured at
 * compile time so later mode changes do not alter an existing graph. */
struct cas_fn {
    int used;
    cas_ctx *owner;
    cas_angle_mode angle;
    int nops;
    rpn_op ops[MAX_OPS];
};

/* The stub's whole state; cas_init places it at the aligned start of the
 * caller's arena.  toks/nodes are per-call scratch, the rest persists. */
struct cas_ctx {
    size_t arena_size;
    cas_settings settings;
    volatile int interrupt;
    double ans;
    uint32_t ans_serial; /* cas_ans_serial */
    char err[112];
    cvar vars[MAX_VARS];
    int ntok;
    token toks[MAX_TOKENS];
    int nnodes;
    node nodes[MAX_NODES];
    struct cas_fn fns[MAX_FNS];
    size_t peak;
    const char *const *cvars; /* cas_compile_vars: variable names kept whole while tokenizing */
    int ncvars;
};

/* ----------------------------------------------------------- helpers --- */
/* printf-style: format the message for cas_last_error and return st, so
 * error paths read "return set_err(c, CAS_ERR_..., ...)". */
static cas_status set_err(cas_ctx *c, cas_status st, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->err, sizeof c->err, fmt, ap);
    va_end(ap);
    return st;
}

/* index in builtins[] or -1 */
static int find_builtin(const char *name)
{
    for (int i = 0; i < NBUILTINS; i++)
        if (strcmp(builtins[i].name, name) == 0)
            return i;
    return -1;
}

/* name of a CAS command (arguments evaluated lazily by ev_command) */
static int is_command(const char *name)
{
    for (int i = 0; i < NCOMMANDS; i++)
        if (strcmp(commands[i], name) == 0)
            return 1;
    return 0;
}

/* one of the reserved constant names */
static int is_constant(const char *name)
{
    for (int i = 0; i < NCONSTANTS; i++)
        if (strcmp(constants[i], name) == 0)
            return 1;
    return 0;
}

/* the variable slot named `name`, or NULL */
static cvar *find_var(cas_ctx *c, const char *name)
{
    for (int i = 0; i < MAX_VARS; i++)
        if (c->vars[i].used && strcmp(c->vars[i].name, name) == 0)
            return &c->vars[i];
    return NULL;
}

/* A name the tokenizer should keep whole rather than split into letters. */
static int known_name(cas_ctx *c, const char *name)
{
    for (int i = 0; i < c->ncvars; i++)
        if (strcmp(c->cvars[i], name) == 0)
            return 1;
    return find_builtin(name) >= 0 || is_command(name) || is_constant(name) || find_var(c, name);
}

static int is_alpha(unsigned char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'; }
static int is_digit(unsigned char ch) { return ch >= '0' && ch <= '9'; }

/* Length of an identifier "letter" at s: ASCII letter (1) or theta U+03B8 (2), else 0. */
static int letter_len(const char *s)
{
    if (is_alpha((unsigned char)s[0]))
        return 1;
    if ((unsigned char)s[0] == 0xCE && (unsigned char)s[1] == 0xB8)
        return 2;
    return 0;
}

/* ---------------------------------------------------------- tokenizer --- */
/* Append a token (one slot is always kept free for the T_END terminator). */
static cas_status push_tok(cas_ctx *c, const token *t)
{
    if (c->ntok >= MAX_TOKENS - 1)
        return set_err(c, CAS_ERR_MEMORY, "expression too long");
    c->toks[c->ntok++] = *t;
    return CAS_OK;
}

/* append a T_OP token for the operator character op */
static cas_status push_op(cas_ctx *c, char op)
{
    token t;
    memset(&t, 0, sizeof t);
    t.kind = T_OP;
    t.op = op;
    return push_tok(c, &t);
}

/* Append a T_NAME token of length n (the Greek letter pi becomes "pi"). */
static cas_status push_name(cas_ctx *c, const char *s, size_t n)
{
    token t;
    memset(&t, 0, sizeof t);
    if (n >= NAME_MAX_LEN)
        return set_err(c, CAS_ERR_SYNTAX, "name too long");
    t.kind = T_NAME;
    memcpy(t.name, s, n);
    t.name[n] = 0;
    if (strcmp(t.name, "\xCF\x80") == 0) /* pi symbol */
        strcpy(t.name, "pi");
    return push_tok(c, &t);
}

/* Append a T_NUM token holding the value of the n-character literal at s. */
static cas_status push_num(cas_ctx *c, const char *s, size_t n)
{
    char buf[64];
    token t;
    memset(&t, 0, sizeof t);
    if (n >= sizeof buf)
        return set_err(c, CAS_ERR_SYNTAX, "number too long");
    memcpy(buf, s, n);
    buf[n] = 0;
    t.kind = T_NUM;
    t.num = strtod(buf, NULL);
    return push_tok(c, &t);
}

/*
 * An alphanumeric run that is not a known name is split into single
 * letters (and digit runs become numbers): "xy" -> x*y, "xsin" -> x, sin,
 * "x2" -> x*2. Known names (functions, commands, constants, defined variables)
 * are matched greedily from the left.
 * keep_whole forces the run to be a single name (assignment targets).
 */
static cas_status split_run(cas_ctx *c, const char *s, size_t n, int keep_whole)
{
    char buf[NAME_MAX_LEN];
    cas_status st;
    if (n < NAME_MAX_LEN) {
        memcpy(buf, s, n);
        buf[n] = 0;
        if (keep_whole || known_name(c, buf))
            return push_name(c, s, n);
    } else if (keep_whole) {
        return set_err(c, CAS_ERR_SYNTAX, "name too long");
    }
    size_t i = 0;
    while (i < n) {
        if (is_digit((unsigned char)s[i])) {
            size_t j = i;
            while (j < n && is_digit((unsigned char)s[j]))
                j++;
            if ((st = push_num(c, s + i, j - i)) != CAS_OK)
                return st;
            i = j;
            continue;
        }
        /* longest known name starting at i */
        size_t best = 0;
        for (size_t len = n - i; len >= 2; len--) {
            if (len >= NAME_MAX_LEN)
                continue;
            memcpy(buf, s + i, len);
            buf[len] = 0;
            if (known_name(c, buf)) {
                best = len;
                break;
            }
        }
        if (!best)
            best = (size_t)(letter_len(s + i) ? letter_len(s + i) : 1);
        if ((st = push_name(c, s + i, best)) != CAS_OK)
            return st;
        i += best;
    }
    return CAS_OK;
}

/* Convert UTF-8 input into c->toks.  Numbers accept decimals and an exponent
 * ("1.5e-3"); identifiers are runs of letters (ASCII, '_', Greek theta) and
 * digits, split by split_run; the Unicode symbols pi, sqrt, infinity, minus,
 * times and divide are mapped to their ASCII equivalents ('r' is the prefix
 * square-root operator).  Returns CAS_ERR_SYNTAX on any other character. */
static cas_status tokenize(cas_ctx *c, const char *s)
{
    cas_status st = CAS_OK;
    c->ntok = 0;
    while (*s) {
        unsigned char ch = (unsigned char)*s;
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            s++;
        } else if (is_digit(ch) || (ch == '.' && is_digit((unsigned char)s[1]))) {
            const char *p = s;
            while (is_digit((unsigned char)*p))
                p++;
            if (*p == '.') {
                p++;
                while (is_digit((unsigned char)*p))
                    p++;
            }
            if (*p == 'e' || *p == 'E') {
                const char *q = p + 1;
                if (*q == '+' || *q == '-')
                    q++;
                if (is_digit((unsigned char)*q)) {
                    while (is_digit((unsigned char)*q))
                        q++;
                    p = q;
                }
            }
            st = push_num(c, s, (size_t)(p - s));
            s = p;
        } else if (letter_len(s)) {
            const char *p = s;
            while (letter_len(p) || is_digit((unsigned char)*p))
                p += letter_len(p) ? letter_len(p) : 1;
            /* keep the whole run if it is an assignment target */
            const char *q = p;
            while (*q == ' ')
                q++;
            int assign = (q[0] == ':' && q[1] == '=');
            st = split_run(c, s, (size_t)(p - s), assign);
            s = p;
        } else if (ch == ':' && s[1] == '=') {
            token t;
            memset(&t, 0, sizeof t);
            t.kind = T_ASSIGN;
            st = push_tok(c, &t);
            s += 2;
        } else if (strchr("+-*/^(),=!", ch)) {
            st = push_op(c, (char)ch);
            s++;
        } else if (ch == 0xCF && (unsigned char)s[1] == 0x80) { /* π */
            st = push_name(c, "pi", 2);
            s += 2;
        } else if (!strncmp(s, "\xE2\x88\x9A", 3)) { /* √ */
            st = push_op(c, 'r');
            s += 3;
        } else if (!strncmp(s, "\xE2\x88\x9E", 3)) { /* ∞ */
            st = push_name(c, "inf", 3);
            s += 3;
        } else if (!strncmp(s, "\xE2\x88\x92", 3)) { /* − minus sign */
            st = push_op(c, '-');
            s += 3;
        } else if (!strncmp(s, "\xC3\x97", 2)) { /* × */
            st = push_op(c, '*');
            s += 2;
        } else if (!strncmp(s, "\xC3\xB7", 2)) { /* ÷ */
            st = push_op(c, '/');
            s += 2;
        } else {
            return set_err(c, CAS_ERR_SYNTAX, "unexpected character '%c'", ch < 128 ? ch : '?');
        }
        if (st != CAS_OK)
            return st;
    }
    token end;
    memset(&end, 0, sizeof end);
    end.kind = T_END;
    c->toks[c->ntok] = end;
    return CAS_OK;
}

/* ------------------------------------------------------------- parser --- *
 * Recursive descent over the token array, one function per precedence level:
 *
 *   expr    := term (('+' | '-') term)*
 *   term    := unary (('*' | '/') unary | power)*     juxtaposition = implicit '*'
 *   unary   := ('-' | '+') unary | power
 *   power   := postfix ['^' unary]                    right associative
 *   postfix := primary '!'*
 *   primary := number | name | name '(' args ')' | '(' expr ')' | sqrt-sign primary
 *
 * So -x^2 = -(x^2), 2^-1 is allowed, and "2x" / "2(x+1)" / "x sin(x)" multiply.
 * Each function returns a node index, or -1 after recording the first error
 * in the parser state (later errors are ignored).  `depth` bounds the
 * recursion. */
typedef struct {
    cas_ctx *c;
    int pos;
    int depth;
    cas_status st;
} parser;

static const token *peek(parser *p) { return &p->c->toks[p->pos]; }
static int peek_op(parser *p, char op) { return peek(p)->kind == T_OP && peek(p)->op == op; }

/* record the first parse error and return -1 */
static int perr(parser *p, cas_status st, const char *msg)
{
    if (p->st == CAS_OK) {
        p->st = st;
        set_err(p->c, st, "%s", msg);
    }
    return -1;
}

/* allocate a node from the pool; -1 (with CAS_ERR_MEMORY) when it is full */
static int new_node(parser *p, int kind, int a, int b)
{
    cas_ctx *c = p->c;
    if (c->nnodes >= MAX_NODES)
        return perr(p, CAS_ERR_MEMORY, "expression too large");
    node *n = &c->nodes[c->nnodes];
    memset(n, 0, sizeof *n);
    n->kind = (uint8_t)kind;
    n->a = (int16_t)a;
    n->b = (int16_t)b;
    n->next = -1;
    return c->nnodes++;
}

static int parse_expr(parser *p);
static int parse_unary(parser *p);

/* Primary expressions.  A builtin or command name must be followed by an
 * argument list; inside it each argument may be an equation "lhs = rhs"
 * (used by solve).  Any other name becomes N_NAME and is resolved only at
 * evaluation time. */
static int parse_primary(parser *p)
{
    const token *t = peek(p);
    if (t->kind == T_NUM) {
        int n = new_node(p, N_NUM, -1, -1);
        if (n >= 0)
            p->c->nodes[n].v = t->num;
        p->pos++;
        return n;
    }
    if (t->kind == T_NAME) {
        char name[NAME_MAX_LEN];
        strcpy(name, t->name);
        p->pos++;
        int is_fn = find_builtin(name) >= 0 || is_command(name);
        if (is_fn) {
            if (!peek_op(p, '('))
                return perr(p, CAS_ERR_SYNTAX, "missing '(' after function name");
            p->pos++;
            int call = new_node(p, N_CALL, -1, 0);
            if (call < 0)
                return -1;
            strcpy(p->c->nodes[call].name, name);
            int last = -1, count = 0;
            if (!peek_op(p, ')')) {
                for (;;) {
                    int arg = parse_expr(p);
                    if (arg < 0)
                        return -1;
                    if (peek_op(p, '=')) { /* equation (only meaningful for solve) */
                        p->pos++;
                        int rhs = parse_expr(p);
                        if (rhs < 0)
                            return -1;
                        arg = new_node(p, N_EQ, arg, rhs);
                        if (arg < 0)
                            return -1;
                    }
                    if (last < 0)
                        p->c->nodes[call].a = (int16_t)arg;
                    else
                        p->c->nodes[last].next = (int16_t)arg;
                    last = arg;
                    count++;
                    if (peek_op(p, ',')) {
                        p->pos++;
                        continue;
                    }
                    break;
                }
            }
            if (!peek_op(p, ')'))
                return perr(p, CAS_ERR_SYNTAX, "missing ')'");
            p->pos++;
            p->c->nodes[call].b = (int16_t)count;
            return call;
        }
        int n = new_node(p, N_NAME, -1, -1);
        if (n >= 0)
            strcpy(p->c->nodes[n].name, name);
        return n;
    }
    if (t->kind == T_OP && t->op == '(') {
        p->pos++;
        int e = parse_expr(p);
        if (e < 0)
            return -1;
        if (!peek_op(p, ')'))
            return perr(p, CAS_ERR_SYNTAX, "missing ')'");
        p->pos++;
        return e;
    }
    if (t->kind == T_OP && t->op == 'r') { /* √operand */
        p->pos++;
        int a = parse_primary(p);
        if (a < 0)
            return -1;
        return new_node(p, N_SQRT, a, -1);
    }
    if (t->kind == T_END)
        return perr(p, CAS_ERR_SYNTAX, "unexpected end of input");
    return perr(p, CAS_ERR_SYNTAX, "syntax error");
}

/* postfix factorial: primary followed by any number of '!' */
static int parse_postfix(parser *p)
{
    int n = parse_primary(p);
    while (n >= 0 && peek_op(p, '!')) {
        p->pos++;
        n = new_node(p, N_FACT, n, -1);
    }
    return n;
}

/* power := postfix ['^' unary]   (right-associative, exponent may be signed) */
static int parse_power(parser *p)
{
    int base = parse_postfix(p);
    if (base < 0)
        return -1;
    if (peek_op(p, '^')) {
        p->pos++;
        if (++p->depth > MAX_DEPTH)
            return perr(p, CAS_ERR_SYNTAX, "too deeply nested");
        int ex = parse_unary(p);
        p->depth--;
        if (ex < 0)
            return -1;
        return new_node(p, N_POW, base, ex);
    }
    return base;
}

/* prefix sign; binds more loosely than '^' */
static int parse_unary(parser *p)
{
    if (peek_op(p, '-') || peek_op(p, '+')) {
        int neg = peek(p)->op == '-';
        p->pos++;
        if (++p->depth > MAX_DEPTH)
            return perr(p, CAS_ERR_SYNTAX, "too deeply nested");
        int a = parse_unary(p);
        p->depth--;
        if (a < 0)
            return -1;
        return neg ? new_node(p, N_NEG, a, -1) : a;
    }
    return parse_power(p);
}

/* can the next token start an implicitly multiplied factor? */
static int starts_operand(parser *p)
{
    const token *t = peek(p);
    return t->kind == T_NUM || t->kind == T_NAME || (t->kind == T_OP && (t->op == '(' || t->op == 'r'));
}

/* products and quotients, including implicit multiplication; the implicit
 * factor is parsed at power level, so "2-x" is not read as 2*(-x) */
static int parse_term(parser *p)
{
    int a = parse_unary(p);
    while (a >= 0) {
        if (peek_op(p, '*') || peek_op(p, '/')) {
            int div = peek(p)->op == '/';
            p->pos++;
            int b = parse_unary(p);
            if (b < 0)
                return -1;
            a = new_node(p, div ? N_DIV : N_MUL, a, b);
        } else if (starts_operand(p)) { /* implicit multiplication */
            int b = parse_power(p);
            if (b < 0)
                return -1;
            a = new_node(p, N_MUL, a, b);
        } else {
            break;
        }
    }
    return a;
}

/* sums and differences (left associative); counts nesting depth */
static int parse_expr(parser *p)
{
    if (++p->depth > MAX_DEPTH)
        return perr(p, CAS_ERR_SYNTAX, "too deeply nested");
    int a = parse_term(p);
    while (a >= 0 && (peek_op(p, '+') || peek_op(p, '-'))) {
        int sub = peek(p)->op == '-';
        p->pos++;
        int b = parse_term(p);
        if (b < 0)
            return -1;
        a = new_node(p, sub ? N_SUB : N_ADD, a, b);
    }
    p->depth--;
    return a;
}

/* Parse a whole input (tokens already in ctx) into an AST root. */
static cas_status parse_all(cas_ctx *c, int start, int *root)
{
    parser p = {c, start, 0, CAS_OK};
    c->nnodes = 0;
    if (peek(&p)->kind == T_END)
        return set_err(c, CAS_ERR_SYNTAX, "empty input");
    int r = parse_expr(&p);
    if (r >= 0 && peek_op(&p, '=')) {
        p.pos++;
        int rhs = parse_expr(&p);
        r = rhs < 0 ? -1 : new_node(&p, N_EQ, r, rhs);
    }
    if (p.st != CAS_OK)
        return p.st;
    if (peek(&p)->kind != T_END) {
        if (peek_op(&p, ')'))
            return set_err(c, CAS_ERR_SYNTAX, "unmatched ')'");
        return set_err(c, CAS_ERR_SYNTAX, "syntax error near token %d", p.pos + 1);
    }
    *root = r;
    return CAS_OK;
}

/* ---------------------------------------------------------- functions --- */
/* Round a trig result to 0 when it is pure rounding noise (|r| < 1e-15 at a
 * non-tiny argument), so sin(pi) displays 0.  Arguments near 0 are left
 * alone, since sin(1e-20) = 1e-20 is a genuine small value. */
static double snap_trig(double r, double arg)
{
    /* sin(pi) is 1.2e-16 in floating point; show 0 instead */
    return (fabs(r) < 1e-15 && fabs(arg) > 1e-6) ? 0.0 : r;
}

/* sin/cos/tan in degrees, exact at multiples of 90 degrees. */
static double trig_deg(int id, double x)
{
    double r = fmod(x, 360.0);
    if (r < 0)
        r += 360.0;
    if (r == floor(r) && fmod(r, 90.0) == 0.0) {
        int q = (int)(r / 90.0);
        static const double s[4] = {0, 1, 0, -1}, co[4] = {1, 0, -1, 0};
        if (id == F_SIN)
            return s[q];
        if (id == F_COS)
            return co[q];
        return (q & 1) ? DNAN : 0.0;
    }
    double rad = x * DEG2RAD;
    return id == F_SIN ? sin(rad) : id == F_COS ? cos(rad) : tan(rad);
}

/* Evaluate builtin `id` on n arguments a[0..n-1].  In degree mode trig
 * arguments are degrees and inverse trig results are converted to degrees.
 * Returns NaN outside the real domain (sqrt(-1), ln(0), asin(2), tan(90 deg),
 * mod(x, 0)); the caller decides whether that is an error.
 * log(x, b) is the base-b log, log(x) is base 10; round(x, k) rounds to k
 * decimals; nthroot(x, k) of negative x is real only for odd integer k. */
static double apply_fn(int id, const double *a, int n, cas_angle_mode angle)
{
    double x = a[0];
    int deg = angle == CAS_ANGLE_DEG;
    double inv = deg ? 1.0 / DEG2RAD : 1.0;
    switch (id) {
    case F_SIN: return deg ? trig_deg(id, x) : snap_trig(sin(x), x);
    case F_COS: return deg ? trig_deg(id, x) : snap_trig(cos(x), x);
    case F_TAN: return deg ? trig_deg(id, x) : snap_trig(tan(x), x);
    case F_ASIN: return (x < -1 || x > 1) ? DNAN : asin(x) * inv;
    case F_ACOS: return (x < -1 || x > 1) ? DNAN : acos(x) * inv;
    case F_ATAN: return atan(x) * inv;
    case F_SINH: return sinh(x);
    case F_COSH: return cosh(x);
    case F_TANH: return tanh(x);
    case F_ASINH: return asinh(x);
    case F_ACOSH: return x < 1 ? DNAN : acosh(x);
    case F_ATANH: return (x <= -1 || x >= 1) ? DNAN : atanh(x);
    case F_SQRT: return x < 0 ? DNAN : sqrt(x);
    case F_CBRT: return cbrt(x);
    case F_ABS: return fabs(x);
    case F_LN: return x <= 0 ? DNAN : log(x);
    case F_LOG:
        if (x <= 0)
            return DNAN;
        if (n == 2)
            return (a[1] <= 0 || a[1] == 1) ? DNAN : log(x) / log(a[1]);
        return log10(x);
    case F_EXP: return exp(x);
    case F_FLOOR: return floor(x);
    case F_CEIL: return ceil(x);
    case F_ROUND:
        if (n == 2) {
            double s = pow(10.0, floor(a[1]));
            return round(x * s) / s;
        }
        return round(x);
    case F_MIN: {
        double m = x;
        for (int i = 1; i < n; i++)
            m = a[i] < m ? a[i] : m;
        return m;
    }
    case F_MAX: {
        double m = x;
        for (int i = 1; i < n; i++)
            m = a[i] > m ? a[i] : m;
        return m;
    }
    case F_SIGN: return x > 0 ? 1.0 : x < 0 ? -1.0 : 0.0;
    case F_MOD: return a[1] == 0 ? DNAN : x - a[1] * floor(x / a[1]);
    case F_NTHROOT: {
        double k = a[1];
        if (k == 0)
            return DNAN;
        if (x < 0) {
            if (k == floor(k) && fmod(fabs(k), 2.0) == 1.0)
                return -pow(-x, 1.0 / k);
            return DNAN;
        }
        return pow(x, 1.0 / k);
    }
    }
    return DNAN;
}

/* a^b with the real root for a negative base when b is (within 1e-9) a
 * fraction p/q with q odd, q <= 99: (-1)^p |a|^b, as the real engine does
 * in COMPLEX mode REAL (cas_settings.real) and in compiled functions; other
 * exponents of negative bases give NaN (pow). */
static double real_pow(double a, double b)
{
    if (a < 0 && b != floor(b))
        for (int q = 3; q <= 99; q += 2) {
            double p = b * q, r = floor(p + 0.5);
            if (fabs(p - r) < 1e-9) {
                double v = pow(-a, b);
                return fmod(fabs(r), 2.0) == 1.0 ? -v : v;
            }
        }
    return pow(a, b);
}

/* x! : exact product for integers up to 170 (171! overflows a double),
 * Gamma(x + 1) for non-integers, NaN for negative integers (poles). */
static double factorial(double x)
{
    if (x < 0 && x == floor(x))
        return DNAN;
    if (x == floor(x) && x <= 170) {
        double r = 1;
        for (int i = 2; i <= (int)x; i++)
            r *= i;
        return r;
    }
    return tgamma(x + 1.0);
}

/* --------------------------------------------------------- evaluation --- */
/* a temporary value for a command's variable, e.g. x while integrating */
typedef struct {
    const char *name;
    double val;
} binding;

/* Evaluation state: binding stack (innermost last, so it shadows stored
 * variables), first error, and a flag remembering that a division by zero
 * produced a NaN (reported as CAS_ERR_DOMAIN "division by zero" if the final
 * result is not finite). */
typedef struct {
    cas_ctx *c;
    binding bind[MAX_BIND];
    int nb;
    cas_status st;
    int divzero;
} evst;

/* record the first evaluation error and return NaN (evaluation continues
 * but all later results are ignored) */
static double ev_fail(evst *s, cas_status st, const char *fmt, const char *arg)
{
    if (s->st == CAS_OK) {
        s->st = st;
        set_err(s->c, st, fmt, arg);
    }
    return DNAN;
}

static double ev(evst *s, int n);
static double ev_command(evst *s, node *nd);

/* Resolve a name: bindings (innermost first), then user variables, then the
 * constants pi, e, inf and ans.  Returns 0 if the name is undefined. */
static int lookup(evst *s, const char *name, double *out)
{
    for (int i = s->nb - 1; i >= 0; i--)
        if (strcmp(s->bind[i].name, name) == 0) {
            *out = s->bind[i].val;
            return 1;
        }
    cvar *v = find_var(s->c, name);
    if (v) {
        *out = v->val;
        return 1;
    }
    if (!strcmp(name, "pi")) {
        *out = M_PI;
        return 1;
    }
    if (!strcmp(name, "e")) {
        *out = M_E;
        return 1;
    }
    if (!strcmp(name, "inf")) {
        *out = DINF;
        return 1;
    }
    if (!strcmp(name, "ans") || !strcmp(name, "Ans")) {
        *out = s->c->ans;
        return 1;
    }
    return 0;
}

/* Tree-walking evaluator: value of node n as a double.  NaN propagates
 * through IEEE arithmetic; errors are reported through s->st (see ev_fail).
 * Recursion depth is bounded by the parser's MAX_DEPTH. */
static double ev(evst *s, int n)
{
    if (s->st != CAS_OK)
        return DNAN;
    if (s->c->interrupt)
        return ev_fail(s, CAS_ERR_INTERRUPTED, "%s", "interrupted");
    node *nd = &s->c->nodes[n];
    double a, b;
    switch (nd->kind) {
    case N_NUM: return nd->v;
    case N_NAME:
        if (lookup(s, nd->name, &a))
            return a;
        return ev_fail(s, CAS_ERR_UNDEFINED, "undefined: %s", nd->name);
    case N_NEG: return -ev(s, nd->a);
    case N_ADD: a = ev(s, nd->a); return a + ev(s, nd->b);
    case N_SUB: a = ev(s, nd->a); return a - ev(s, nd->b);
    case N_MUL: a = ev(s, nd->a); return a * ev(s, nd->b);
    case N_DIV:
        a = ev(s, nd->a);
        b = ev(s, nd->b);
        if (b == 0) {
            s->divzero = 1;
            return DNAN;
        }
        return a / b;
    case N_POW:
        a = ev(s, nd->a);
        b = ev(s, nd->b);
        if (a == 0 && b < 0) {
            s->divzero = 1;
            return DNAN;
        }
        return s->c->settings.real ? real_pow(a, b) : pow(a, b);
    case N_FACT: return factorial(ev(s, nd->a));
    case N_SQRT: a = ev(s, nd->a); return a < 0 ? DNAN : sqrt(a);
    case N_EQ: return ev_fail(s, CAS_ERR_SYNTAX, "%s", "'=' is only allowed inside solve()");
    case N_CALL: {
        int bi = find_builtin(nd->name);
        if (bi < 0)
            return ev_command(s, nd);
        const builtin *f = &builtins[bi];
        if (nd->b < f->minargs || nd->b > f->maxargs)
            return ev_fail(s, CAS_ERR_SYNTAX, "wrong number of arguments to %s", f->name);
        double args[8];
        int i = 0;
        for (int arg = nd->a; arg >= 0; arg = s->c->nodes[arg].next)
            args[i++] = ev(s, arg);
        return apply_fn(f->id, args, i, s->c->settings.angle);
    }
    }
    return ev_fail(s, CAS_ERR_SYNTAX, "%s", "internal error");
}

/* Evaluate node n (or lhs-rhs of an equation) with `var` bound to x. */
static double ev_at(evst *s, int n, const char *name, double x)
{
    if (s->nb >= MAX_BIND)
        return ev_fail(s, CAS_ERR_UNSUPPORTED, "%s", "commands nested too deeply");
    s->bind[s->nb].name = name;
    s->bind[s->nb].val = x;
    s->nb++;
    node *nd = &s->c->nodes[n];
    double r = nd->kind == N_EQ ? ev(s, nd->a) - ev(s, nd->b) : ev(s, n);
    s->nb--;
    return r;
}

/* node index of argument i of a call, or -1 */
static int arg_n(cas_ctx *c, const node *call, int i)
{
    int a = call->a;
    while (a >= 0 && i-- > 0)
        a = c->nodes[a].next;
    return a;
}

/* Adaptive Simpson quadrature on [a, b] (Lyness 1969 / Kuncir 1962).
 * `whole` is the Simpson estimate on [a, b] with the endpoint and midpoint
 * values fa, fm, fb; the interval is split in two, and if the two half
 * estimates agree with `whole` to 15*eps the Richardson-corrected sum
 * (left + right + delta/15, exact for quintics) is accepted; otherwise both
 * halves recurse with eps/2, up to `depth` levels.  Each level costs two new
 * function evaluations.  A non-finite estimate (NaN or infinite integrand
 * values, e.g. at a singularity) stops the recursion at once with NaN:
 * otherwise the test would never pass and both halves would recurse to the
 * full depth (2^40 evaluations). */
static double simpson_rec(evst *s, int f, const char *v, double a, double b, double fa, double fm,
                          double fb, double whole, double eps, int depth)
{
    double m = 0.5 * (a + b), lm = 0.5 * (a + m), rm = 0.5 * (m + b);
    double flm = ev_at(s, f, v, lm), frm = ev_at(s, f, v, rm);
    double left = (m - a) / 6.0 * (fa + 4.0 * flm + fm);
    double right = (b - m) / 6.0 * (fm + 4.0 * frm + fb);
    double delta = left + right - whole;
    if (s->st != CAS_OK)
        return DNAN;
    if (s->c->interrupt)
        return ev_fail(s, CAS_ERR_INTERRUPTED, "%s", "interrupted");
    if (!isfinite(delta))
        return DNAN;
    if (depth <= 0 || fabs(delta) <= 15.0 * eps)
        return left + right + delta / 15.0;
    return simpson_rec(s, f, v, a, m, fa, flm, fm, left, eps / 2.0, depth - 1) +
           simpson_rec(s, f, v, m, b, fm, frm, fb, right, eps / 2.0, depth - 1);
}

/* integral of node f over v in [a, b], absolute tolerance 1e-11 */
static double integrate_num(evst *s, int f, const char *v, double a, double b)
{
    double fa = ev_at(s, f, v, a), fb = ev_at(s, f, v, b), fm = ev_at(s, f, v, 0.5 * (a + b));
    double whole = (b - a) / 6.0 * (fa + 4.0 * fm + fb);
    return simpson_rec(s, f, v, a, b, fa, fm, fb, whole, 1e-11, 40);
}

/* Derivative of node f with respect to v at x: central differences with
 * steps h, h/2, h/4 (h = 1e-3 relative to |x|) combined by Richardson
 * extrapolation.  The central difference error is a series in h^2, h^4, ...;
 * (4 D(h/2) - D(h)) / 3 cancels the h^2 term and (16 R2 - R1) / 15 the h^4
 * term. */
static double diff_num(evst *s, int f, const char *v, double x)
{
    double h = 1e-3 * (fabs(x) > 1 ? fabs(x) : 1.0);
    double d1 = (ev_at(s, f, v, x + h) - ev_at(s, f, v, x - h)) / (2 * h);
    double h2 = h / 2;
    double d2 = (ev_at(s, f, v, x + h2) - ev_at(s, f, v, x - h2)) / (2 * h2);
    double h4 = h / 4;
    double d3 = (ev_at(s, f, v, x + h4) - ev_at(s, f, v, x - h4)) / (2 * h4);
    /* two rounds of Richardson extrapolation (error O(h^6)) */
    double r1 = (4 * d2 - d1) / 3, r2 = (4 * d3 - d2) / 3;
    return (16 * r2 - r1) / 15;
}

/* Heuristic numeric limit of f as v -> a.
 * At +-inf: compare f at 1e8 and 1e12 (times the sign); treat small and
 * decreasing values as 0, otherwise accept agreement to 1e-6.  At a finite
 * point: evaluate on both sides at distances 1e-4 .. 1e-9 (relative to |a|)
 * and accept when the left and right values agree and have stopped moving.
 * Anything else is reported as CAS_ERR_DOMAIN. */
static double limit_num(evst *s, int f, const char *v, double a)
{
    if (isinf(a)) {
        double sg = a > 0 ? 1.0 : -1.0;
        double f1 = ev_at(s, f, v, sg * 1e8), f2 = ev_at(s, f, v, sg * 1e12);
        if (s->st != CAS_OK)
            return DNAN;
        if (isfinite(f1) && isfinite(f2) && fabs(f2) <= 1e-6 && fabs(f2) <= fabs(f1))
            return 0.0; /* decaying to zero, e.g. 1/x */
        if (isfinite(f1) && isfinite(f2) && fabs(f1 - f2) <= 1e-6 * (fabs(f2) > 1 ? fabs(f2) : 1))
            return f2;
        return ev_fail(s, CAS_ERR_DOMAIN, "%s", "limit does not exist (numerically)");
    }
    double scale = fabs(a) > 1 ? fabs(a) : 1.0;
    double prevl = DNAN, prevr = DNAN, l = DNAN, r = DNAN;
    for (double h = 1e-4; h >= 1e-9; h /= 10) {
        prevl = l;
        prevr = r;
        l = ev_at(s, f, v, a - h * scale);
        r = ev_at(s, f, v, a + h * scale);
    }
    if (s->st != CAS_OK)
        return DNAN;
    double tol = 1e-5 * (fabs(r) > 1 ? fabs(r) : 1);
    if (isfinite(l) && isfinite(r) && fabs(l - r) <= tol && fabs(l - prevl) <= 10 * tol &&
        fabs(r - prevr) <= 10 * tol)
        return 0.5 * (l + r);
    return ev_fail(s, CAS_ERR_DOMAIN, "%s", "limit does not exist (numerically)");
}

/* CAS commands that produce a number (solve is handled at top level).
 * simplify/approx/factor/expand only evaluate their argument (and report a
 * free variable as "needs the CAS library"); diff, integrate and limit need a
 * variable name as second argument and an evaluation point / bounds. */
static double ev_command(evst *s, node *nd)
{
    cas_ctx *c = s->c;
    const char *name = nd->name;
    int nargs = nd->b;
    int a0 = arg_n(c, nd, 0), a1 = arg_n(c, nd, 1);

    if (!strcmp(name, "simplify") || !strcmp(name, "approx") || !strcmp(name, "factor") ||
        !strcmp(name, "expand")) {
        if (nargs != 1)
            return ev_fail(s, CAS_ERR_SYNTAX, "%s takes one argument", name);
        double r = ev(s, a0);
        if (s->st == CAS_ERR_UNDEFINED) {
            s->st = CAS_ERR_UNSUPPORTED;
            set_err(c, CAS_ERR_UNSUPPORTED, "%s needs the CAS library (stub build)", name);
        }
        return r;
    }
    if (!strcmp(name, "solve"))
        return ev_fail(s, CAS_ERR_UNSUPPORTED, "%s", "solve() must be the whole input");

    /* the remaining commands take a variable as 2nd argument */
    if (nargs < 2 || c->nodes[a1].kind != N_NAME)
        return ev_fail(s, CAS_ERR_SYNTAX, "%s: second argument must be a variable", name);
    const char *v = c->nodes[a1].name;
    if (!strcmp(name, "diff")) {
        if (nargs == 2)
            return ev_fail(s, CAS_ERR_UNSUPPORTED, "%s", "symbolic diff needs the CAS library");
        if (nargs != 3)
            return ev_fail(s, CAS_ERR_SYNTAX, "%s", "diff(f, x, at)");
        double at = ev(s, arg_n(c, nd, 2));
        return diff_num(s, a0, v, at);
    }
    if (!strcmp(name, "integrate")) {
        if (nargs == 2)
            return ev_fail(s, CAS_ERR_UNSUPPORTED, "%s",
                           "symbolic integrate needs the CAS library; use integrate(f, x, a, b)");
        if (nargs != 4)
            return ev_fail(s, CAS_ERR_SYNTAX, "%s", "integrate(f, x, a, b)");
        double a = ev(s, arg_n(c, nd, 2)), b = ev(s, arg_n(c, nd, 3));
        if (s->st != CAS_OK)
            return DNAN;
        if (!isfinite(a) || !isfinite(b))
            return ev_fail(s, CAS_ERR_UNSUPPORTED, "%s", "improper integrals need the CAS library");
        return integrate_num(s, a0, v, a, b);
    }
    if (!strcmp(name, "limit")) {
        if (nargs != 3)
            return ev_fail(s, CAS_ERR_SYNTAX, "%s", "limit(f, x, point)");
        double at = ev(s, arg_n(c, nd, 2));
        if (s->st != CAS_OK)
            return DNAN;
        return limit_num(s, a0, v, at);
    }
    return ev_fail(s, CAS_ERR_UNDEFINED, "unknown function %s", name);
}

/* ---------------------------------------------------------- formatting --- */
/* Append s to out (capacity len) at *pos.  If it does not fit, as much as
 * possible is copied (NUL-terminated) and CAS_ERR_OUTPUT_TRUNC returned. */
static cas_status put(cas_ctx *c, char *out, size_t len, size_t *pos, const char *s)
{
    size_t n = strlen(s);
    if (*pos + n + 1 > len) {
        if (len) {
            size_t k = len - 1 > *pos ? len - 1 - *pos : 0;
            memcpy(out + *pos, s, k);
            out[*pos + k] = 0;
        }
        return set_err(c, CAS_ERR_OUTPUT_TRUNC, "result too long for the output buffer");
    }
    memcpy(out + *pos, s, n + 1);
    *pos += n;
    return CAS_OK;
}

/* Find p/q with q <= 1000 equal to x within 1e-12 relative.
 * Uses the continued fraction expansion of |x|: the convergents h/k obey
 * h_i = a_i h_(i-1) + h_(i-2) (likewise k) and are the best rational
 * approximations, so the first one within tolerance is the simplest
 * fraction.  Integers and |x| >= 1e12 are rejected (returns 0). */
static int as_fraction(double x, long *p, long *q)
{
    if (!(fabs(x) < 1e12) || x == floor(x))
        return 0;
    double ax = fabs(x), r = ax;
    long h0 = 0, h1 = 1, k0 = 1, k1 = 0;
    for (int i = 0; i < 40; i++) {
        double fl = floor(r);
        long ai = (long)fl;
        long h2 = ai * h1 + h0, k2 = ai * k1 + k0;
        if (k2 > 1000)
            break;
        h0 = h1; h1 = h2; k0 = k1; k1 = k2;
        if (fabs((double)h1 / (double)k1 - ax) <= 1e-12 * ax) {
            if (k1 == 1)
                return 0;
            *p = x < 0 ? -h1 : h1;
            *q = k1;
            return 1;
        }
        double frac = r - fl;
        if (frac < 1e-15)
            break;
        r = 1.0 / frac;
    }
    return 0;
}

/* Format a result: as p/q in exact mode when as_fraction finds one, otherwise
 * with settings.digits (clamped to 1..17) significant digits, trailing zeros
 * removed, "-0" shown as "0" and the exponent written with a capital E
 * ("1.5E-7"), as on the calculator display. */
static void fmt_number(const cas_ctx *c, double v, char *buf, size_t n)
{
    long p, q;
    if (c->settings.exact && as_fraction(v, &p, &q)) {
        snprintf(buf, n, "%ld/%ld", p, q);
        return;
    }
    int digits = c->settings.digits;
    if (digits <= 0)
        digits = 12;
    if (digits > 17)
        digits = 17;
    char tmp[48];
    snprintf(tmp, sizeof tmp, "%.*g", digits, v);
    char mant[48];
    int exp10 = 0, has_exp = 0;
    char *e = strchr(tmp, 'e');
    if (e) {
        has_exp = 1;
        exp10 = atoi(e + 1);
        *e = 0;
    }
    strcpy(mant, tmp);
    if (strchr(mant, '.')) { /* strip trailing zeros */
        size_t l = strlen(mant);
        while (l > 0 && mant[l - 1] == '0')
            mant[--l] = 0;
        if (l > 0 && mant[l - 1] == '.')
            mant[--l] = 0;
    }
    if (!strcmp(mant, "-0"))
        strcpy(mant, "0");
    if (has_exp)
        snprintf(buf, n, "%sE%d", mant, exp10);
    else
        snprintf(buf, n, "%s", mant);
}

/* --------------------------------------------------------------- solve --- */
/* solve(f = g, x) or solve(f, x): numeric real roots of f - g in [-100, 100].
 * The interval is scanned in 4000 steps.  A sign change between neighbouring
 * samples is refined by bisection and accepted only when the residual is
 * small (a sign change across a pole such as 1/x is rejected).  Roots of even
 * multiplicity, which do not change sign, are found as local minima of |f|
 * that are below 1e-2, refined by golden-section search and accepted when
 * |f| < 1e-10.  Values within 1e-9 of an integer are snapped to it,
 * duplicates removed, at most MAX_ROOTS reported in ascending order as
 * "x=r1 or x=r2 ...".  ans becomes the smallest root. */
static cas_status do_solve(cas_ctx *c, evst *s, node *nd, char *out, size_t len)
{
    if (nd->b != 2)
        return set_err(c, CAS_ERR_SYNTAX, "solve(equation, x)");
    int f = arg_n(c, nd, 0), a1 = arg_n(c, nd, 1);
    if (c->nodes[a1].kind != N_NAME)
        return set_err(c, CAS_ERR_SYNTAX, "solve: second argument must be a variable");
    const char *v = c->nodes[a1].name;
    double roots[MAX_ROOTS];
    int nroots = 0;
    const int N = 4000;
    const double lo = -100.0, hi = 100.0, step = (hi - lo) / N;
    double xp = lo, fp = ev_at(s, f, v, lo), fpp = DNAN;
    for (int i = 1; i <= N && nroots < MAX_ROOTS; i++) {
        if (s->st != CAS_OK)
            return s->st;
        if (c->interrupt)
            return set_err(c, CAS_ERR_INTERRUPTED, "interrupted");
        double x = lo + i * step, fx = ev_at(s, f, v, x);
        double cand = DNAN;
        if (fp == 0) {
            cand = xp;
        } else if (isfinite(fp) && isfinite(fx) && (fp < 0) != (fx < 0) && fx != 0) {
            double a = xp, b = x, fa = fp; /* bisection */
            for (int k = 0; k < 200 && b - a > 1e-15 * (fabs(a) + 1); k++) {
                double m = 0.5 * (a + b), fm = ev_at(s, f, v, m);
                if (!isfinite(fm))
                    break;
                if ((fm < 0) == (fa < 0)) {
                    a = m;
                    fa = fm;
                } else {
                    b = m;
                }
            }
            double r = 0.5 * (a + b), fr = ev_at(s, f, v, r);
            /* reject poles: a genuine root has a small residual */
            double scale = fabs(fp) < fabs(fx) ? fabs(fp) : fabs(fx);
            if (isfinite(fr) && fabs(fr) <= 1e-6 * (scale > 1 ? scale : 1))
                cand = r;
        } else if (isfinite(fpp) && isfinite(fp) && isfinite(fx) && fabs(fp) < fabs(fpp) &&
                   fabs(fp) < fabs(fx) && fabs(fp) < 1e-2) {
            /* tangent zero: minimise |f| around xp by golden-section search */
            double a = xp - step, b = x;
            for (int k = 0; k < 120; k++) {
                double m1 = a + (b - a) * 0.381966, m2 = a + (b - a) * 0.618034;
                if (fabs(ev_at(s, f, v, m1)) < fabs(ev_at(s, f, v, m2)))
                    b = m2;
                else
                    a = m1;
            }
            double r = 0.5 * (a + b);
            if (fabs(ev_at(s, f, v, r)) < 1e-10)
                cand = r;
        }
        if (isfinite(cand)) {
            if (fabs(cand - round(cand)) < 1e-9)
                cand = round(cand);
            int dup = 0;
            for (int k = 0; k < nroots; k++)
                if (fabs(roots[k] - cand) < 1e-7 * (fabs(cand) + 1))
                    dup = 1;
            if (!dup)
                roots[nroots++] = cand;
        }
        fpp = fp;
        xp = x;
        fp = fx;
    }
    if (s->st != CAS_OK)
        return s->st;
    if (!nroots)
        return set_err(c, CAS_ERR_UNSUPPORTED,
                       "no solution found in [-100,100] (numeric stub; the CAS library solves symbolically)");
    /* sort ascending */
    for (int i = 1; i < nroots; i++)
        for (int k = i; k > 0 && roots[k] < roots[k - 1]; k--) {
            double t = roots[k];
            roots[k] = roots[k - 1];
            roots[k - 1] = t;
        }
    size_t pos = 0;
    if (len)
        out[0] = 0;
    for (int i = 0; i < nroots; i++) {
        char num[48], part[NAME_MAX_LEN + 56];
        fmt_number(c, roots[i], num, sizeof num);
        snprintf(part, sizeof part, "%s%s=%s", i ? " or " : "", v, num);
        cas_status st = put(c, out, len, &pos, part);
        if (st != CAS_OK)
            return st;
    }
    c->ans = roots[0];
    c->ans_serial++;
    return CAS_OK;
}

/* ----------------------------------------------------------------- API --- */
/* The arena only has to hold the (16-byte aligned) context struct. */
cas_ctx *cas_init(void *arena, size_t size)
{
    uintptr_t p = (uintptr_t)arena;
    uintptr_t aligned = (p + 15u) & ~(uintptr_t)15u;
    if (!arena || size < (aligned - p) + sizeof(cas_ctx))
        return NULL;
    cas_ctx *c = (cas_ctx *)aligned;
    memset(c, 0, sizeof *c);
    c->arena_size = size;
    c->settings.angle = CAS_ANGLE_RAD;
    c->settings.exact = 1;
    c->settings.digits = 12;
    c->settings.real = 0;
    c->peak = sizeof *c;
    strcpy(c->err, "no error");
    return c;
}

void cas_set_settings(cas_ctx *ctx, const cas_settings *s) { ctx->settings = *s; }
void cas_get_settings(const cas_ctx *ctx, cas_settings *s) { *s = ctx->settings; }
const char *cas_last_error(const cas_ctx *ctx) { return ctx->err; }
uint32_t cas_ans_serial(const cas_ctx *ctx) { return ctx->ans_serial; }

/* The stub's evaluator is iterative over fixed tables, so there is no deep
 * recursion to bound; accepted for API compatibility. */
void cas_set_stack_limit(cas_ctx *ctx, size_t bytes)
{
    (void)ctx;
    (void)bytes;
}

/* LaTeX of the last result: the stub only has numbers, which are valid LaTeX. */
cas_status cas_latex(cas_ctx *ctx, char *buf, size_t len)
{
    if (!len)
        return CAS_ERR_OUTPUT_TRUNC;
    char num[48];
    fmt_number(ctx, ctx->ans, num, sizeof num);
    int n = snprintf(buf, len, "%s", num);
    return (size_t)n >= len ? CAS_ERR_OUTPUT_TRUNC : CAS_OK;
}
void cas_interrupt(cas_ctx *ctx) { ctx->interrupt = 1; }

/* the stub's evaluations are short: nothing to poll */
void cas_set_poll(cas_ctx *ctx, int (*poll)(void *user), void *user) { (void)ctx, (void)poll, (void)user; }

/* forget variables, compiled functions and ans; settings are kept */
void cas_reset(cas_ctx *ctx)
{
    memset(ctx->vars, 0, sizeof ctx->vars);
    for (int i = 0; i < MAX_FNS; i++)
        ctx->fns[i].used = 0;
    ctx->ans = 0;
    ctx->ans_serial++;
    ctx->nnodes = 0;
    ctx->ntok = 0;
    strcpy(ctx->err, "no error");
}

/* all stub memory is the fixed-size context, so used == its size */
void cas_mem(const cas_ctx *ctx, cas_mem_stats *out)
{
    out->arena_size = ctx->arena_size;
    out->used = sizeof *ctx; /* everything is reserved up front inside the context */
    out->peak = ctx->peak;
}

/* Assign a user variable (overwriting an existing one, else taking a free
 * slot).  Builtin, command and constant names are rejected. */
static cas_status store_var(cas_ctx *c, const char *name, double v)
{
    if (find_builtin(name) >= 0 || is_command(name) || is_constant(name))
        return set_err(c, CAS_ERR_SYNTAX, "cannot assign to %s", name);
    cvar *slot = find_var(c, name);
    for (int i = 0; !slot && i < MAX_VARS; i++)
        if (!c->vars[i].used)
            slot = &c->vars[i];
    if (!slot)
        return set_err(c, CAS_ERR_MEMORY, "too many variables");
    strcpy(slot->name, name);
    slot->val = v;
    slot->used = 1;
    return CAS_OK;
}

/* One input line: tokenize, recognise "name := expr", parse, evaluate (or
 * run solve, which must be the whole input), store the variable, update ans
 * and format the number.  Non-finite results are domain errors. */
cas_status cas_eval(cas_ctx *ctx, const char *input, char *text, size_t text_len)
{
    cas_status st;
    int root;
    ctx->interrupt = 0;
    if (text_len)
        text[0] = 0;
    /* purge(name): forget a variable (the application's hidden variables) */
    size_t il = strlen(input);
    if (!strncmp(input, "purge(", 6) && il > 7 && il - 7 < NAME_MAX_LEN && input[il - 1] == ')') {
        char name[NAME_MAX_LEN];
        memcpy(name, input + 6, il - 7);
        name[il - 7] = 0;
        cvar *v = find_var(ctx, name);
        if (v) {
            v->used = 0;
            size_t pos = 0;
            return put(ctx, text, text_len, &pos, "1");
        }
    }
    if ((st = tokenize(ctx, input)) != CAS_OK)
        return st;
    int start = 0;
    const char *target = NULL;
    if (ctx->ntok >= 2 && ctx->toks[0].kind == T_NAME && ctx->toks[1].kind == T_ASSIGN) {
        target = ctx->toks[0].name;
        start = 2;
    } else {
        for (int i = 0; i < ctx->ntok; i++)
            if (ctx->toks[i].kind == T_ASSIGN)
                return set_err(ctx, CAS_ERR_UNSUPPORTED, "only 'name:=expression' assignments are supported");
    }
    if ((st = parse_all(ctx, start, &root)) != CAS_OK)
        return st;

    evst s;
    memset(&s, 0, sizeof s);
    s.c = ctx;
    node *rn = &ctx->nodes[root];
    if (rn->kind == N_CALL && !strcmp(rn->name, "solve")) {
        if (target)
            return set_err(ctx, CAS_ERR_UNSUPPORTED, "cannot assign the result of solve");
        return do_solve(ctx, &s, rn, text, text_len);
    }
    double v = ev(&s, root);
    if (s.st != CAS_OK)
        return s.st;
    if (s.divzero && !isfinite(v))
        return set_err(ctx, CAS_ERR_DOMAIN, "division by zero");
    if (!isfinite(v))
        return set_err(ctx, CAS_ERR_DOMAIN, isnan(v) ? "domain error" : "overflow");
    if (v == 0)
        v = 0; /* no -0 */
    if (target && (st = store_var(ctx, target, v)) != CAS_OK)
        return st;
    ctx->ans = v;
    ctx->ans_serial++;
    char num[48];
    size_t pos = 0;
    fmt_number(ctx, v, num, sizeof num);
    return put(ctx, text, text_len, &pos, num);
}

/* ---------------------------------------------------------- compiler --- *
 * AST -> RPN translation.  `depth` tracks the run-time stack height so that
 * expressions needing more than FN_STACK slots are rejected at compile time
 * and cas_fn_eval_vars can run without bounds checks. */
typedef struct {
    cas_ctx *c;
    struct cas_fn *fn;
    const char *const *vars;
    int nvars;
    int depth, maxdepth;
    cas_status st;
} compiler;

/* append one instruction whose net stack effect is stack_delta */
static void emit(compiler *k, int op, int fnid, int nargs, double v, int stack_delta)
{
    if (k->st != CAS_OK)
        return;
    if (k->fn->nops >= MAX_OPS) {
        k->st = set_err(k->c, CAS_ERR_MEMORY, "expression too large to compile");
        return;
    }
    rpn_op *o = &k->fn->ops[k->fn->nops++];
    o->op = (uint8_t)op;
    o->fn = (uint8_t)fnid;
    o->nargs = (uint8_t)nargs;
    o->v = v;
    k->depth += stack_delta;
    if (k->depth > k->maxdepth)
        k->maxdepth = k->depth;
    if (k->maxdepth > FN_STACK)
        k->st = set_err(k->c, CAS_ERR_MEMORY, "expression too complex to compile");
}

/* Post-order code generation for node n.  Names that are compiled variables
 * become OP_X; any other name is looked up now (stored variables and
 * constants are folded in as OP_CONST), so later assignments do not change
 * the compiled function.  Commands and equations cannot be compiled. */
static void gen(compiler *k, int n)
{
    if (k->st != CAS_OK)
        return;
    node *nd = &k->c->nodes[n];
    static const uint8_t binop[] = {[N_ADD] = OP_ADD, [N_SUB] = OP_SUB, [N_MUL] = OP_MUL,
                                    [N_DIV] = OP_DIV, [N_POW] = OP_POW};
    switch (nd->kind) {
    case N_NUM: emit(k, OP_CONST, 0, 0, nd->v, 1); return;
    case N_NAME: {
        for (int vi = 0; vi < k->nvars; vi++)
            if (!strcmp(nd->name, k->vars[vi])) {
                emit(k, OP_X, vi, 0, 0, 1);
                return;
            }
        evst s;
        memset(&s, 0, sizeof s);
        s.c = k->c;
        double v;
        if (!lookup(&s, nd->name, &v)) {
            k->st = set_err(k->c, CAS_ERR_UNDEFINED, "undefined: %s", nd->name);
            return;
        }
        emit(k, OP_CONST, 0, 0, v, 1);
        return;
    }
    case N_NEG: gen(k, nd->a); emit(k, OP_NEG, 0, 0, 0, 0); return;
    case N_FACT: gen(k, nd->a); emit(k, OP_FACT, 0, 0, 0, 0); return;
    case N_SQRT: gen(k, nd->a); emit(k, OP_FN, F_SQRT, 1, 0, 0); return;
    case N_ADD: case N_SUB: case N_MUL: case N_DIV: case N_POW:
        gen(k, nd->a);
        gen(k, nd->b);
        emit(k, binop[nd->kind], 0, 0, 0, -1);
        return;
    case N_EQ:
        k->st = set_err(k->c, CAS_ERR_SYNTAX, "'=' cannot be graphed");
        return;
    case N_CALL: {
        int bi = find_builtin(nd->name);
        if (bi < 0) {
            k->st = set_err(k->c, CAS_ERR_UNSUPPORTED, "%s cannot be graphed in the stub build", nd->name);
            return;
        }
        if (nd->b < builtins[bi].minargs || nd->b > builtins[bi].maxargs) {
            k->st = set_err(k->c, CAS_ERR_SYNTAX, "wrong number of arguments to %s", nd->name);
            return;
        }
        for (int a = nd->a; a >= 0; a = k->c->nodes[a].next)
            gen(k, a);
        emit(k, OP_FN, builtins[bi].id, nd->b, 0, 1 - nd->b);
        return;
    }
    }
}

/* single-variable form of cas_compile_vars */
cas_status cas_compile(cas_ctx *ctx, const char *expr, const char *var, cas_fn **out)
{
    return cas_compile_vars(ctx, expr, &var, 1, out);
}

/* Compile with nvars named variables; the names are passed to the tokenizer
 * (ctx->cvars) so that multi-letter variable names are not split into
 * products of letters. */
cas_status cas_compile_vars(cas_ctx *ctx, const char *expr, const char *const *vars, int nvars, cas_fn **out)
{
    cas_status st;
    int root;
    *out = NULL;
    ctx->cvars = vars;
    ctx->ncvars = nvars;
    st = tokenize(ctx, expr);
    ctx->cvars = NULL;
    ctx->ncvars = 0;
    if (st != CAS_OK)
        return st;
    for (int i = 0; i < ctx->ntok; i++)
        if (ctx->toks[i].kind == T_ASSIGN)
            return set_err(ctx, CAS_ERR_SYNTAX, "assignment cannot be graphed");
    if ((st = parse_all(ctx, 0, &root)) != CAS_OK)
        return st;
    struct cas_fn *fn = NULL;
    for (int i = 0; i < MAX_FNS; i++)
        if (!ctx->fns[i].used) {
            fn = &ctx->fns[i];
            break;
        }
    if (!fn)
        return set_err(ctx, CAS_ERR_MEMORY, "too many compiled functions");
    fn->nops = 0;
    fn->owner = ctx;
    fn->angle = ctx->settings.angle;
    if (nvars < 1 || nvars > 24)
        return set_err(ctx, CAS_ERR_UNSUPPORTED, "too many variables to compile");
    compiler k = {ctx, fn, vars, nvars, 0, 0, CAS_OK};
    gen(&k, root);
    if (k.st != CAS_OK)
        return k.st;
    fn->used = 1;
    *out = fn;
    return CAS_OK;
}

/* x is variable 0; any further compiled variables read as 0 */
double cas_fn_eval(const cas_fn *fn, double x)
{
    double v[24] = {0};
    v[0] = x;
    return cas_fn_eval_vars(fn, v);
}

/* Run the RPN program (stack height was verified by the compiler).
 * Division by zero and any non-finite result give NaN, so plots show a gap. */
double cas_fn_eval_vars(const cas_fn *fn, const double *vals)
{
    if (!fn)
        return DNAN;
    double st[FN_STACK];
    int sp = 0;
    for (int i = 0; i < fn->nops; i++) {
        const rpn_op *o = &fn->ops[i];
        switch (o->op) {
        case OP_CONST: st[sp++] = o->v; break;
        case OP_X: st[sp++] = vals[o->fn]; break;
        case OP_NEG: st[sp - 1] = -st[sp - 1]; break;
        case OP_FACT: st[sp - 1] = factorial(st[sp - 1]); break;
        case OP_ADD: sp--; st[sp - 1] += st[sp]; break;
        case OP_SUB: sp--; st[sp - 1] -= st[sp]; break;
        case OP_MUL: sp--; st[sp - 1] *= st[sp]; break;
        case OP_DIV:
            sp--;
            st[sp - 1] = st[sp] == 0 ? DNAN : st[sp - 1] / st[sp];
            break;
        case OP_POW:
            sp--;
            st[sp - 1] = real_pow(st[sp - 1], st[sp]); /* real odd roots, as cas.h promises */
            break;
        case OP_FN:
            sp -= o->nargs;
            st[sp] = apply_fn(o->fn, &st[sp], o->nargs, fn->angle);
            sp++;
            break;
        }
    }
    double r = sp ? st[sp - 1] : DNAN;
    return isfinite(r) ? r : DNAN;
}

/* return the slot to ctx->fns (ignored for NULL or another context's fn) */
void cas_fn_free(cas_ctx *ctx, cas_fn *fn)
{
    if (fn && fn->owner == ctx)
        fn->used = 0;
}

/* ------------------------------------------------------ session state --- */
/* Write the header, one "v NAME VALUE" record per variable and the "n ANS"
 * record (17 significant digits, so doubles round-trip exactly). */
/* The "v" records of the variables named prefix... (prefix NULL: every name
 * not starting with '_', as in the real engine). */
static cas_status save_vars(cas_ctx *ctx, const char *prefix, cas_writer wr, void *user)
{
    char line[NAME_MAX_LEN + 48];
    for (int i = 0; i < MAX_VARS; i++) {
        const char *name = ctx->vars[i].name;
        if (!ctx->vars[i].used || (prefix ? strncmp(name, prefix, strlen(prefix)) != 0 : name[0] == '_'))
            continue;
        int n = snprintf(line, sizeof line, "v %s %.17g\n", name, ctx->vars[i].val);
        if (wr(user, line, (size_t)n))
            return CAS_ERR_OUTPUT_TRUNC;
    }
    return CAS_OK;
}

cas_status cas_save(cas_ctx *ctx, cas_writer wr, void *user)
{
    char line[48];
    if (wr(user, "CAS1\n", 5) || save_vars(ctx, NULL, wr, user) != CAS_OK)
        return CAS_ERR_OUTPUT_TRUNC;
    int n = snprintf(line, sizeof line, "n %.17g\n", ctx->ans);
    return wr(user, line, (size_t)n) ? CAS_ERR_OUTPUT_TRUNC : CAS_OK;
}

/* only the variables named prefix..., without header and ans */
cas_status cas_save_prefix(cas_ctx *ctx, const char *prefix, cas_writer wr, void *user)
{
    return save_vars(ctx, prefix ? prefix : "", wr, user);
}

/* Read records line by line (stopping at a NUL).  The header line (starting
 * with 'C') is skipped; v and n records are restored; anything else, e.g.
 * user functions or assumptions saved by the real engine, makes the result
 * CAS_ERR_SYNTAX while the remaining records are still processed.  The
 * result is the status of the first failure (as in the real engine), e.g.
 * CAS_ERR_MEMORY when the variable table is full. */
cas_status cas_restore(cas_ctx *ctx, const char *text, size_t len)
{
    cas_status result = CAS_OK;
    size_t pos = 0;
    while (pos < len) {
        size_t e = pos;
        while (e < len && text[e] != '\n' && text[e])
            e++;
        char line[NAME_MAX_LEN + 64];
        size_t n = e - pos < sizeof line - 1 ? e - pos : sizeof line - 1;
        memcpy(line, text + pos, n);
        line[n] = 0;
        char name[NAME_MAX_LEN];
        double v;
        if (line[0] == 'v' && sscanf(line + 1, " %23s %lf", name, &v) == 2) {
            cas_status st = store_var(ctx, name, v);
            if (st != CAS_OK && result == CAS_OK)
                result = st; /* e.g. CAS_ERR_MEMORY: too many variables */
        } else if (line[0] == 'n' && sscanf(line + 1, " %lf", &v) == 1) {
            ctx->ans = v;
            ctx->ans_serial++;
        } else if (line[0] && line[0] != 'C' && result == CAS_OK) {
            result = CAS_ERR_SYNTAX; /* records the stub does not know (functions, ...) */
        }
        if (e < len && !text[e])
            break;
        pos = e + 1;
    }
    if (result != CAS_OK)
        set_err(ctx, result, "some saved records could not be restored");
    return result;
}

/* report every variable (the stub has no user functions: nparams = -1) */
void cas_vars(cas_ctx *ctx, cas_var_fn fn, void *user)
{
    for (int i = 0; i < MAX_VARS; i++)
        if (ctx->vars[i].used && ctx->vars[i].name[0] != '_') /* '_' names are internal */
            fn(user, ctx->vars[i].name, -1);
}

/* cas_eval with ans saved and restored around it */
cas_status cas_eval_quiet(cas_ctx *ctx, const char *input, char *text, size_t text_len)
{
    double ans = ctx->ans;
    cas_status st = cas_eval(ctx, input, text, text_len);
    ctx->ans = ans;
    return st;
}

/* CAS extension modules: the stand-in has no extension interface */
void cas_ext_set_loader(int (*load)(const char *module)) { (void)load; }
const void *cas_ext_api(void) { return NULL; }
void cas_ext_unload(void) {}
int cas_ext_busy(void) { return 0; }
struct casx_api;
int casx_adv_init(const struct casx_api *X); /* modules/casx: refuses (no extension table here) */
int casx_adv_init(const struct casx_api *X) { (void)X; return -1; }
