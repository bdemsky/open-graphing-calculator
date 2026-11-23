/*
 * parse.c -- recursive-descent parser producing raw (unsimplified) trees.
 *
 * Input: one line of calculator text (ASCII plus a few UTF-8 symbols).
 * Output: a raw expression tree in scratch, built with the mk_* constructors
 * (no simplification, no evaluation); eval.c evaluates it.  Called by
 * cas_eval / cas_compile (cas.c) and cas_restore (session.c).  Every syntax
 * problem raises CAS_ERR_SYNTAX with cas_throw; the parser never returns
 * NULL.
 *
 * Grammar, from lowest to highest precedence (one C function per rule):
 *
 *   input  := stmt {';' stmt} [';']             (several -> ';' function node)
 *   stmt   := expr [ ':=' expr | '=>' ident ]
 *   expr   := and {('or' | '||') and}
 *   and    := not {('and' | '&&') not}
 *   not    := 'not' not | rel
 *   rel    := sum [relop sum]                   (not chained: a<b<c is an error)
 *   sum    := term {('+'|'-'|'±') term}            (a±b -> pm(a, b))
 *   term   := unary {('*'|'/'|'mod') unary | <implicit> power}
 *   unary  := ('-'|'+') unary | power
 *   power  := postfix ['^' unary]               (right associative, -2^2 = -4)
 *   postfix:= primary {'!' | "'"... ['(' expr ')'] | superscript digit}
 *   primary:= number [unit tail] | string | ident ['(' args ')']
 *           | '(' expr {',' expr} ')' | '[' args ']' | '{' args '}'
 *           | '|' expr '|' | 'd/dx' ...
 *
 * Representation of operators in the raw tree (the same as after
 * simplification, so the simplifier needs no separate operator nodes):
 *   a - b  ->  a + (-1)*b          a / b  ->  a * b^(-1)
 *   -a     ->  (-1)*a (a negated literal for numbers)
 *   relations, and/or/not, :=, ';', factorial -> E_FN nodes with the builtin
 *   operator ids (F_EQ, F_LT, ..., F_ASSIGN, F_SEQ, F_FACT)
 *   (a, b) and [a, b] -> E_LIST;  |x| -> abs(x);  'x mod y' -> mod(x, y)
 *   x == y is the equation x = y; <> means !=; ** means ^.
 *
 * Implicit multiplication: 2x, 2(x+1), (a+b)(a-b), x y, 2sin(x), x^2y.  It
 * has the same precedence as '*' and associates left, so 1/2x = (1/2)*x.  A
 * number cannot follow implicitly (x 2 is an error).
 *
 * ident '(' is a call if ident is a known function, a user function, a
 * multi-letter name, or one of the single letters f g h u v w y F G H;
 * otherwise (e.g. x(x+1)) it is a product.  A name that has a stored value
 * (and no function definition) is never a call: a:=2 makes a(3) mean 6.
 * Builtin functions and commands also accept a space before '(' (sin (x)).
 * This decision depends on the current symbol table, so the same text can
 * parse differently after definitions change.
 *
 * Other notations: "sin x" applies a math function to the following
 * unary when the next token is a number, name or string; f'(a) / f''(x)
 * (derivatives, see postfix); d/dx(expr); ∇f, ∇·F, ∇×F, ∇²f (p_nabla:
 * grad, divergence, curl, laplacian); unit literals such as 9.8_m/s^2
 * (units.c).  A number directly followed (no space) by a unit, together
 * with the unit's tail, is a single primary (a quantity): 1/2_s = 1/(2_s),
 * 6_m/2_s/3_s = (6_m)/(2_s)/(3_s), and 1/2_m/s = 1/(2_m/s) since the tail
 * "/s" belongs to the quantity; a parenthesised value directly followed
 * by a unit is a quantity too: (2±0.1)_m/(0.5±0.02)_s divides two quantities.  The tail continues through '*', '/' and
 * integer powers written without spaces (see p_unit_tail), so a space ends
 * it: "2_m / s" divides by the variable s.  Numeric literals 0x1F 0b101
 * 0o17 (xcmds.c), decimals and 1e-5
 * (become doubles, i.e. approximate numbers).
 *
 * Nesting depth is bounded twice: L->depth counts brackets/exponents/unary
 * signs (syntax error past 200/400), and stack_check() guards the C stack.
 */
#include "cas_int.h"
#include <ctype.h>
#include <string.h>
#include <math.h>

/* token classes (see next()) */
enum {
    T_END, T_NUM, T_ID, T_STR, T_OP
};

/* Lexer state.  The lexer is on-demand: next() scans exactly one token
 * ahead, and the parser looks at the current token (tok / op / ts, tl)
 * before deciding what to do.  Backtracking is done by copying the whole
 * struct (lexer save = *L; ... *L = save;), which is cheap because it holds
 * only positions.
 *   ts == (size_t)-1 marks an identifier produced from a UTF-8 symbol (pi,
 *   inf, sqrt, ...); tl is then the index into utf[] instead of a length.
 *   space_before records whether whitespace preceded the token; it matters
 *   for f (x) vs f(x) and for unit tails.
 *   depth counts open brackets / exponents / unary signs (nesting limit). */
typedef struct {
    const char *s;
    size_t pos;
    int tok;
    size_t ts, tl;  /* token start, length (for T_ID/T_NUM/T_STR) */
    char op[4];     /* operator text, canonicalised */
    int space_before;
    int depth;
} lexer;

/* Raise CAS_ERR_SYNTAX with msg.  The lexer is not used yet (no position is
 * reported); it is passed so that a position could be added later. */
static _Noreturn void syntax(lexer *L, const char *msg) {
    (void)L;
    cas_throw(CAS_ERR_SYNTAX, msg);
}

/* Helpers from other modules: base-n literals (xcmds.c) and unit names
 * (units.c). */
size_t basen_lexlen(const char *s);
E *basen_value(const char *s, size_t n);
int units_mark(int id);
int units_name_sym(const char *s, size_t n);

/* Identifier characters: letters and '_' start a name; digits may follow. */
static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }

/* UTF-8 sequences accepted in the input and their ASCII meaning: pi, infinity,
 * square root, <= >= !=, times / divide / minus / middle dot, store arrows,
 * superscript 2 and 3 (lexed as the pseudo-operator "^2" / "^3"),
 * double-struck e and i, and the integral sign. */
static const struct {
    const char *u;
    const char *a;
    int kind;
} utf[] = {
    {"\xcf\x80", "pi", T_ID},       {"\xe2\x88\x9e", "inf", T_ID},   {"\xe2\x88\x9a", "sqrt", T_ID},
    {"\xe2\x89\xa4", "<=", T_OP},   {"\xe2\x89\xa5", ">=", T_OP},    {"\xe2\x89\xa0", "!=", T_OP},
    {"\xc3\x97", "*", T_OP},        {"\xc3\xb7", "/", T_OP},         {"\xe2\x88\x92", "-", T_OP},
    {"\xc2\xb7", "*", T_OP},        {"\xe2\x96\xb6", "=>", T_OP},    {"\xe2\x86\x92", "=>", T_OP},
    {"\xc2\xb2", "^2", T_OP},       {"\xc2\xb3", "^3", T_OP},        {"\xe2\x85\x87", "e", T_ID},
    {"\xe2\x85\x88", "i", T_ID},    {"\xe2\x88\xab", "integrate", T_ID},
    {"\xc2\xb1", "+/-", T_OP}, /* plus-minus: a measurement with its uncertainty (unc.c) */
    {"\xe2\x88\x87", "nabla", T_ID}, /* del, the last entry (NABLA): p_nabla */
};
#define NABLA (sizeof utf / sizeof utf[0] - 1)

/* Scan the next token starting at L->pos into L (tok, ts/tl or op), skipping
 * whitespace.  Token classes:
 *   T_NUM  base-n literal (0x.., 0b.., 0o..), or digits with an optional
 *          fraction ".digits" and exponent "e[+-]digits".  "1..5" stops
 *          before the "..", and "2e" without exponent digits is the number 2
 *          followed by the identifier e (so 2e = 2*e);
 *   T_ID   [A-Za-z_][A-Za-z0-9_]*, or a UTF-8 identifier from utf[];
 *   T_STR  "..." (no escapes; ts/tl exclude the quotes);
 *   T_OP   two-character operators first (:= <= >= != == ** => <> && ||),
 *          canonicalised (** -> ^, <> -> !=, == -> =), then single
 *          characters, then UTF-8 operators;
 *   T_END  end of input.
 * Unknown characters throw CAS_ERR_SYNTAX. */
static void next(lexer *L) {
    const char *s = L->s;
    size_t p = L->pos;
    L->space_before = 0;
    while (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r') {
        p++;
        L->space_before = 1;
    }
    L->ts = p;
    if (!s[p]) {
        L->tok = T_END;
        L->pos = p;
        return;
    }
    int c = (unsigned char)s[p];
    if (c == '0' && (L->tl = basen_lexlen(s + p)) != 0) { /* 0x1F 0b101 0o17 */
        L->tok = T_NUM;
        L->pos = p + L->tl;
        return;
    }
    if (is_digit(c) || (c == '.' && is_digit((unsigned char)s[p + 1]))) {
        while (is_digit((unsigned char)s[p])) p++;
        if (s[p] == '.' && !(s[p + 1] == '.')) {
            p++;
            while (is_digit((unsigned char)s[p])) p++;
        }
        if ((s[p] == 'e' || s[p] == 'E') &&
            (is_digit((unsigned char)s[p + 1]) ||
             ((s[p + 1] == '-' || s[p + 1] == '+') && is_digit((unsigned char)s[p + 2])))) {
            p += 2;
            while (is_digit((unsigned char)s[p])) p++;
        }
        L->tok = T_NUM;
        L->tl = p - L->ts;
        L->pos = p;
        return;
    }
    if (is_alpha(c)) {
        while (is_alpha((unsigned char)s[p]) || is_digit((unsigned char)s[p])) p++;
        L->tok = T_ID;
        L->tl = p - L->ts;
        L->pos = p;
        return;
    }
    if (c == '"') {
        p++;
        while (s[p] && s[p] != '"') p++;
        if (!s[p]) syntax(L, "unterminated string");
        L->tok = T_STR;
        L->ts++;
        L->tl = p - L->ts;
        L->pos = p + 1;
        return;
    }
    if (c >= 0x80) {
        for (size_t i = 0; i < sizeof utf / sizeof utf[0]; i++) {
            size_t n = strlen(utf[i].u);
            if (strncmp(s + p, utf[i].u, n) == 0) {
                L->pos = p + n;
                if (utf[i].kind == T_ID) {
                    /* identifier substitution: store name in op[] and mark */
                    L->tok = T_ID;
                    L->ts = (size_t)-1;
                    L->tl = (size_t)i;
                } else {
                    L->tok = T_OP;
                    strcpy(L->op, utf[i].a);
                }
                return;
            }
        }
        syntax(L, "unexpected character");
    }
    static const char *ops2[] = {":=", "<=", ">=", "!=", "==", "**", "=>", "<>", "&&", "||"};
    for (size_t i = 0; i < sizeof ops2 / sizeof ops2[0]; i++)
        if (s[p] == ops2[i][0] && s[p + 1] == ops2[i][1]) {
            L->tok = T_OP;
            L->op[0] = ops2[i][0];
            L->op[1] = ops2[i][1];
            L->op[2] = 0;
            if (!strcmp(L->op, "**")) strcpy(L->op, "^");
            if (!strcmp(L->op, "<>")) strcpy(L->op, "!=");
            if (!strcmp(L->op, "==")) strcpy(L->op, "=");
            L->pos = p + 2;
            return;
        }
    if (strchr("+-*/^()[]{},;=<>!'|", c)) {
        L->tok = T_OP;
        L->op[0] = (char)c;
        L->op[1] = 0;
        L->pos = p + 1;
        return;
    }
    syntax(L, "unexpected character");
}

/* Is the current token the operator o? */
static int is_op(lexer *L, const char *o) { return L->tok == T_OP && strcmp(L->op, o) == 0; }

/* Consume operator o or throw a syntax error (with a specific message for a
 * missing ')' or ']'). */
static void expect(lexer *L, const char *o) {
    if (!is_op(L, o)) {
        if (!strcmp(o, ")")) syntax(L, "missing ')'");
        if (!strcmp(o, "]")) syntax(L, "missing ']'");
        syntax(L, "syntax error");
    }
    next(L);
}

/* Intern the current identifier token and return its symbol id (resolving
 * UTF-8 identifiers through utf[]). */
static int id_of(lexer *L) {
    if (L->ts == (size_t)-1) {
        const char *a = utf[L->tl].a;
        return sym_intern(a, strlen(a));
    }
    return sym_intern(L->s + L->ts, L->tl);
}

/* Is the current token the plain ASCII identifier w (used for the keyword
 * operators and / or / not / mod and for d/dx)?  Keywords are not reserved
 * by the lexer; the parser recognises them by spelling where it expects
 * them. */
static int is_id(lexer *L, const char *w) {
    if (L->tok != T_ID || L->ts == (size_t)-1) return 0;
    size_t n = strlen(w);
    return L->tl == n && strncmp(L->s + L->ts, w, n) == 0;
}

static E *p_expr(lexer *L);
static E *p_unary(lexer *L);
static E *p_power(lexer *L);

/* Convert the current T_NUM token to a node (does not advance).
 *   - base-n literal: exact integer (xcmds.c);
 *   - digits only: exact integer (bignum, at most 20000 digits);
 *   - with '.' or an exponent: a double (an "approximate" number).  The
 *     decimal is first converted exactly, m * 10^exp10 with m an integer,
 *     and then rounded once to the nearest double by num_to_double; this
 *     avoids the double rounding of naive digit-by-digit accumulation, so
 *     e.g. 0.1 becomes the double closest to 1/10.  Exponents beyond the
 *     double range short-cut to HUGE_VAL (inf) or 0.  The scratch used for
 *     the conversion is released before the result node is allocated. */
static E *num_token(lexer *L) {
    const char *s = L->s + L->ts;
    size_t n = L->tl;
    size_t i = 0;
    int isf = 0;
    if (s[0] == '0' && basen_lexlen(s) == n) return basen_value(s, n);
    for (i = 0; i < n; i++)
        if (s[i] == '.' || s[i] == 'e' || s[i] == 'E') isf = 1;
    if (!isf) {
        if (n > 20000) syntax(L, "number too long");
        return int_from_dec(s, n);
    }
    /* decimal: digits '.' digits [e exp] -> exact rational -> double */
    char *mark = smark();
    char *dig = salloc(n + 1);
    size_t nd = 0;
    long exp10 = 0;
    i = 0;
    while (i < n && is_digit((unsigned char)s[i])) dig[nd++] = s[i++];
    if (i < n && s[i] == '.') {
        i++;
        while (i < n && is_digit((unsigned char)s[i])) {
            dig[nd++] = s[i++];
            exp10--;
        }
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        int neg = 0;
        if (s[i] == '-' || s[i] == '+') neg = s[i++] == '-';
        long ev = 0;
        while (i < n && is_digit((unsigned char)s[i])) {
            if (ev < 100000) ev = ev * 10 + (s[i] - '0');
            i++;
        }
        exp10 += neg ? -ev : ev;
    }
    if (nd == 0) dig[nd++] = '0';
    E *m = int_from_dec(dig, nd);
    double v;
    if (int_is_zero(m)) v = 0;
    else if (exp10 > 400) v = HUGE_VAL;
    else if (exp10 < -400 - (long)nd) v = 0;
    else if (exp10 >= 0) v = num_to_double(int_mul(m, int_pow(mk_int(10), (uint32_t)exp10)));
    else v = num_to_double(mk_rat(m, int_pow(mk_int(10), (uint32_t)(-exp10))));
    srelease(mark);
    return mk_flt(v);
}

/* After a unit symbol u (a name beginning with '_' that units.c knows),
 * absorb the rest of a compound unit written without spaces:
 *   _m/s^2      -> _m * _s^(-2)        _kg*m^2/s^2 -> _kg * _m^2 * _s^(-2)
 * Inside the tail the following names need no '_' (units_name_sym maps
 * them).  An exponent must be an integer, optionally negative or in
 * parentheses (^2, ^-1, ^(-1)) or a superscript digit; anything else is
 * left for the normal grammar by restoring the saved lexer state.  Note
 * that every '/' applies only to the unit right after it (_m/s*kg means
 * m*kg/s).  Whitespace ends the tail, so "_m / 2" divides normally. */
static E *p_unit_tail(lexer *L, E *u) {
    E *r = NULL;
    int div = 0;
    for (;;) {
        if (L->tok == T_OP && L->op[0] == '^' && !L->space_before) {
            lexer save = *L;
            E *k = NULL;
            if (L->op[1]) {
                k = mk_int(L->op[1] - '0');
                next(L);
            } else {
                next(L);
                int par = is_op(L, "(");
                if (par) next(L);
                int neg = is_op(L, "-");
                if (neg) next(L);
                if (L->tok == T_NUM && (k = num_token(L))->t == E_INT) {
                    next(L);
                    if (par && !is_op(L, ")")) k = NULL;
                    else if (par) next(L);
                    if (k && neg) k = int_neg(k);
                } else
                    k = NULL;
            }
            if (k) u = mk_pow(u, k);
            else *L = save;
        }
        r = r ? mk_mul2(r, div ? mk_pow(u, mk_int(-1)) : u) : u;
        if (L->space_before || !(is_op(L, "*") || is_op(L, "/"))) return r;
        lexer save = *L;
        div = is_op(L, "/");
        next(L);
        int id = L->tok == T_ID && !L->space_before && L->ts != (size_t)-1 ? units_name_sym(L->s + L->ts, L->tl) : -1;
        if (id < 0) {
            *L = save;
            return r;
        }
        next(L);
        u = mk_sym(id);
    }
}

/* Decide whether identifier `id` (of source length len) directly followed by
 * '(' is a function call (1) or an implicit product (0); see the file
 * header.  Builtins are callable from F_SQRT on (math functions and
 * commands) plus factorial and not; constants and operators (x, e, pi, ...)
 * are not. */
static char primed[16]; /* single-letter names written with a prime in this input (parse) */
static int callable_name(int id, size_t len) {
    /* x(0), z(0) after x' or z' in the same input (the conditions of an ODE
     * system, dsolve([x'=y, y'=-x, x(0)=1, y(0)=0])): the unknown function */
    if (len == 1 && primed[0] && strchr(primed, sym_name(id)[0])) return 1;
    if (id < S__COUNT) return id >= F_SQRT || id == F_FACT || id == F_NOT;
    symrec *r = &K->syms[id];
    if (r->fdef) return 1;
    if (r->val) return 0;
    if (len > 1) return 1;
    const char *nm = sym_name(id);
    return strchr("fghuvwyFGH", nm[0]) != NULL;
}

/* 1 if a[0..n) and b[0..n) are equal ignoring ASCII case. */
int name_eq_ci(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
    }
    return 1;
}

/* Case-insensitive builtin names (COS(x), Sin(x), LN(2), SQRT(2), NCR(5,2),
 * ROWSWAP(...), PI, Pi, INF, ANS).  For a user symbol `id` that has neither a
 * value nor a function definition of its own, the builtin whose name equals
 * it ignoring case: with fn = 1 a mathematical function or command
 * (factorial, sqrt ... the commands, the name-dispatched extension commands
 * of xcmds.c), with fn = 0 one of the constants pi, inf, undef, ans.  Returns
 * id itself when there is none or more than one, so the exact spelling
 * always wins and a user's own COS(x):=... or PI:=3 keeps its meaning.
 * e, i, E and I are deliberately not mapped: E and I are common variable
 * names (energy, current) and e / i are distinct single-letter constants, so
 * an uppercase E or I stays an ordinary variable.  Variables stay case
 * sensitive (X and x differ), as in every CAS. */
static int ci_builtin(int id, int fn) {
    if (id < S__COUNT || K->syms[id].val || K->syms[id].fdef) return id;
    const char *nm = sym_name(id);
    size_t n = strlen(nm);
    if (!fn) {
        static const int consts[] = {S_PI, S_INF, S_UNDEF, S_ANS};
        for (size_t i = 0; i < sizeof consts / sizeof consts[0]; i++) {
            const char *b = sym_name(consts[i]);
            if (strlen(b) == n && name_eq_ci(nm, b, n)) return consts[i];
        }
        return id;
    }
    int found = -1;
    for (int b = F_FACT; b < S__COUNT; b++) {
        if (b > F_FACT && b < F_SQRT) continue; /* operators */
        const char *bn = sym_name(b);
        size_t bl = strlen(bn);
        if (bl != n || bn[0] == '_' || bn[bl - 1] == '_' || !name_eq_ci(nm, bn, n)) continue;
        if (found >= 0) return id;
        found = b;
    }
    int x = ext_name_ci(nm, n);
    if (x >= 0) {
        if (found >= 0) return id;
        found = x;
    }
    return found >= 0 ? found : id;
}

/* Parse a comma-separated argument list up to and including `close` (the
 * opening bracket has been consumed).  The array grows by doubling in
 * scratch (old copies are simply abandoned).  Returns the count; *out gets
 * the array.  "f()" gives zero arguments. */
static uint32_t p_args(lexer *L, const char *close, E ***out) {
    uint32_t cap = 8, n = 0;
    E **v = salloc(sizeof(E *) * cap);
    if (is_op(L, close)) {
        next(L);
        *out = v;
        return 0;
    }
    for (;;) {
        if (n == cap) {
            E **nv = salloc(sizeof(E *) * cap * 2);
            memcpy(nv, v, sizeof(E *) * cap);
            v = nv;
            cap *= 2;
        }
        v[n++] = p_expr(L);
        if (is_op(L, ",")) {
            next(L);
            continue;
        }
        expect(L, close);
        break;
    }
    *out = v;
    return n;
}

/* A quantity: e directly followed (no space) by a known unit and its tail
 * is one primary, so 1/2_s = 1/(2_s), 6_m/2_s = (6_m)/(2_s) and, for a
 * parenthesised value, (2.0±0.1)_m/(0.50±0.02)_s = ((2.0±0.1)_m)/((0.50±0.02)_s);
 * the tail itself (9.8_m/s^2) is read by p_unit_tail.  Returns e unchanged
 * if no unit follows. */
static E *quantity_tail(lexer *L, E *e) {
    if (L->tok == T_ID && !L->space_before && L->ts != (size_t)-1 && L->s[L->ts] == '_') {
        int id = id_of(L);
        if (units_mark(id) == 1) {
            next(L);
            return mk_mul2(e, p_unit_tail(L, mk_sym(id)));
        }
    }
    return e;
}

/* The del operator, ∇ or the word nabla (the current token), applied to the
 * following unary like "sin x", or to a parenthesised argument list:
 *   ∇f -> grad(f)   ∇·F, ∇.F -> divergence(F)   ∇×F -> curl(F)
 *   ∇²f, ∇^2 f -> laplacian(f)
 * so ∇f*g = (∇f)*g and ∇f^2 = ∇(f^2).  Without variables the calls get one
 * argument and cmds.c (nabla_vars) supplies them at evaluation;
 * ∇_[r,t]f, ∇_[r,t]·F, ∇·_[r,t]F name them; ∇(f, vars, coords) passes
 * everything on.  The operator character is read from the raw text, since
 * the lexer reads · and × as '*' (and '.' not at all): nabla_op() looks at
 * s + *p (spaces skipped) and returns 0 (none), 1 divergence, 2 curl or
 * 3 laplacian, advancing *p past it. */
static int nabla_op(const char *s, size_t *p) {
    static const char *const o[] = {"\xc2\xb7", ".", "\xc3\x97", "\xc2\xb2", "^2"};
    size_t q = *p;
    while (s[q] == ' ') q++;
    for (int i = 0; i < 5; i++) {
        size_t n = strlen(o[i]);
        if (!strncmp(s + q, o[i], n) && !is_digit((unsigned char)s[q + n])) {
            *p = q + n;
            return i < 2 ? 1 : i < 3 ? 2 : 3;
        }
    }
    return 0;
}

static E *p_nabla(lexer *L) {
    size_t p = L->pos;
    int k = nabla_op(L->s, &p);
    L->pos = p;
    next(L);
    E *v = NULL, **a;
    uint32_t n = 1;
    if (is_id(L, "_") && !L->space_before) { /* ∇_[vars] */
        next(L);
        expect(L, "[");
        uint32_t m = p_args(L, "]", &a);
        v = mk_list(m, a);
        p = L->ts;
        if (!k && L->tok == T_OP && (k = nabla_op(L->s, &p)) != 0) {
            L->pos = p;
            next(L);
        }
    }
    if (is_op(L, "(")) {
        next(L);
        n = p_args(L, ")", &a); /* room for 8 */
        if (!n) syntax(L, "syntax error");
    } else {
        a = salloc(sizeof(E *) * 2);
        a[0] = p_unary(L);
    }
    if (v) {
        if (n != 1) syntax(L, "variables given twice");
        a[n++] = v;
    }
    static const int16_t ids[] = {C_GRAD, C_DIVERGENCE, C_CURL};
    return mk_fn(k < 3 ? ids[k] : sym_intern("laplacian", 9), n, a);
}

/* primary: numbers, strings, identifiers (variables, calls, "sin x" style
 * applications, unit literals), parenthesised expressions and tuples,
 * lists, |abs|, and d/dx notation.  Throws on anything else, including an
 * unexpected end of input. */
static E *p_primary(lexer *L) {
    stack_check();
    if (L->tok == T_NUM) {
        E *e = num_token(L);
        next(L);
        return quantity_tail(L, e); /* 9.8_m/s^2 */
    }
    if (L->tok == T_STR) {
        E *e = mk_str(L->s + L->ts, L->tl);
        next(L);
        return e;
    }
    if (L->tok == T_ID && is_id(L, "d")) {
        /* d/dx(expr) and d/dx expr -> diff(expr, x).  Recognised when "d" is
         * followed by '/' and an identifier of length >= 2 starting with 'd'
         * (the rest of it names the variable, so d/dt, d/dx1 work; note that
         * d/dog would also be read as a derivative in "og"), neither d nor
         * that identifier has a value or definition, and an operand follows.
         * Otherwise the lexer is restored and d is an ordinary name (so
         * d:=6; dx:=2; d/dx is a division). */
        lexer save = *L;
        int dsym = id_of(L);
        next(L);
        if (is_op(L, "/") && !K->syms[dsym].val && !K->syms[dsym].fdef) {
            next(L);
            int did = L->tok == T_ID ? id_of(L) : -1;
            if (L->tok == T_ID && L->ts != (size_t)-1 && L->tl >= 2 && L->s[L->ts] == 'd' &&
                !K->syms[did].val && !K->syms[did].fdef) {
                int vid = sym_intern(L->s + L->ts + 1, L->tl - 1);
                next(L);
                if (is_op(L, "(")) {
                    next(L);
                    E *arg = p_expr(L);
                    expect(L, ")");
                    return mk_fn2(C_DIFF, arg, mk_sym(vid));
                }
                if (L->tok == T_ID || L->tok == T_NUM) return mk_fn2(C_DIFF, p_power(L), mk_sym(vid));
            }
        }
        *L = save;
    }
    if (L->tok == T_ID && ((L->ts == (size_t)-1 && L->tl == NABLA) || is_id(L, "nabla"))) return p_nabla(L);
    if (L->tok == T_ID) {
        size_t len = L->ts == (size_t)-1 ? 2 : L->tl;
        int id = id_of(L);
        next(L);
        /* a known unit (_m, _kg, ...; units_mark also tags the symbol with
         * its unit/positivity flags): read a compound unit */
        if (sym_name(id)[0] == '_' && units_mark(id) == 1) return p_unit_tail(L, mk_sym(id));
        /* name(args) with no space: call or implicit product (callable_name) */
        if (is_op(L, "(") && !L->space_before && callable_name(id, len)) {
            next(L);
            E **args;
            uint32_t n = p_args(L, ")", &args);
            /* d/dx style is not supported; plain call.  COS(x) is cos(x)
             * unless it is being defined (COS(x):=...) */
            return mk_fn(is_op(L, ":=") ? id : ci_builtin(id, 1), n, args);
        }
        /* builtin function/command with a space before '(' (any case) */
        if (is_op(L, "(") && L->space_before && ((id < S__COUNT && id >= F_SQRT) || ci_builtin(id, 1) != id)) {
            next(L);
            E **args;
            uint32_t n = p_args(L, ")", &args);
            return mk_fn(is_op(L, ":=") ? id : ci_builtin(id, 1), n, args);
        }
        if (id >= F_SQRT && id < S__FIRSTCMD && id != F_LAMBDA && L->tok != T_END && !is_op(L, ")") &&
            !is_op(L, ",") && !is_op(L, "]") && !is_op(L, "(") && !is_op(L, ";") && !is_op(L, "=") &&
            !is_op(L, ":=") && L->tok != T_OP) {
            /* "sin x" style: function applied to the following unary.  The
             * final L->tok != T_OP test already excludes every operator, so
             * the next token is a number, a name or a string. */
            E *a = p_unary(L);
            return mk_fn1(id, a);
        }
        if (!is_op(L, ":=")) id = ci_builtin(id, 0); /* PI, Pi, INF, ANS (not when assigned) */
        return mk_sym(id);
    }
    if (is_op(L, "(")) {
        next(L);
        if (++L->depth > 200) syntax(L, "nesting too deep");
        E *e = p_expr(L);
        if (is_op(L, ",")) {
            /* tuple (a, b, ...) -> list, same as [a, b, ...] */
            uint32_t cap = 8, n = 1;
            E **v = salloc(sizeof(E *) * cap);
            v[0] = e;
            while (is_op(L, ",")) {
                next(L);
                if (n == cap) {
                    E **nv = salloc(sizeof(E *) * cap * 2);
                    memcpy(nv, v, sizeof(E *) * cap);
                    v = nv;
                    cap *= 2;
                }
                v[n++] = p_expr(L);
            }
            e = mk_list(n, v);
        }
        L->depth--;
        expect(L, ")");
        return quantity_tail(L, e); /* (2±0.1)_m */
    }
    if (is_op(L, "[") || is_op(L, "{")) {
        const char *close = is_op(L, "[") ? "]" : "}";
        next(L);
        if (++L->depth > 200) syntax(L, "nesting too deep");
        E **args;
        uint32_t n = p_args(L, close, &args);
        L->depth--;
        return mk_list(n, args);
    }
    /* |e| -> abs(e).  A '|' in operand position opens a nested bar and one
     * after a complete operand closes the innermost, so |a - |b|| works. */
    if (is_op(L, "|")) {
        next(L);
        E *e = p_expr(L);
        expect(L, "|");
        return mk_fn1(F_ABS, e);
    }
    if (L->tok == T_END) syntax(L, "unexpected end of input");
    syntax(L, "syntax error");
}

/* postfix operators, any number of them, left to right:
 *   e!        factorial(e)
 *   f'(a)     derivative(f, k, a) with k = number of primes, if f is a name
 *             followed directly by '(': the k-th derivative of the function
 *             f evaluated at a (eval.c handles user and builtin functions);
 *   e'        otherwise diff(e, x) for each prime (derivative in x);
 *   e^2, e^3  from the UTF-8 superscript digits.
 * Note that "!=" is a separate token, so x!=y is a relation. */
static E *p_postfix(lexer *L) {
    E *e = p_primary(L);
    for (;;) {
        if (is_op(L, "!")) {
            /* don't take "!=" (lexed separately) */
            next(L);
            if (is_op(L, "!") && !L->space_before) { /* n!! double factorial */
                next(L);
                e = mk_fn1(sym_intern("factorial2", 10), e);
            } else
                e = mk_fn1(F_FACT, e);
        } else if (is_op(L, "'")) {
            /* f'  f''(a): derivative, optionally evaluated at a point */
            int order = 0;
            while (is_op(L, "'")) {
                next(L);
                order++;
            }
            if (e->t == E_SYM && is_op(L, "(") && !L->space_before) {
                next(L);
                E **args;
                uint32_t n = p_args(L, ")", &args);
                if (n != 1) syntax(L, "f'(a) takes one argument");
                E *v[3] = {e, mk_int(order), args[0]};
                e = mk_fn(F_DERIV, 3, v);
            } else {
                for (int k = 0; k < order; k++) e = mk_fn2(C_DIFF, e, mk_sym(S_X));
            }
        } else if (is_op(L, "[") && !L->space_before && (e->t == E_SYM || IFN(e, sym_intern("getElem", 7)))) {
            /* L[i], M[i,j], M[i][j]: getElem (matfn.c); "L [2]" stays a product */
            next(L);
            E **args, *v[3] = {e};
            uint32_t n = p_args(L, "]", &args);
            if (n < 1 || n > 2) syntax(L, "index: one or two subscripts");
            if (e->t == E_FN && n == 1 && e->n == 2) {
                v[0] = EK(e)[0], v[1] = EK(e)[1], v[2] = args[0];
                n = 2;
            } else
                memcpy(v + 1, args, sizeof(E *) * n);
            e = mk_fn(sym_intern("getElem", 7), n + 1, v);
        } else if (L->tok == T_OP && L->op[0] == '^' && L->op[1]) {
            /* superscript digit */
            e = mk_pow(e, mk_int(L->op[1] - '0'));
            next(L);
        } else
            break;
    }
    return e;
}

/* power := postfix ['^' unary].  The exponent is parsed as a unary, which
 * recurses back into power, so a^b^c = a^(b^c) (right associative) and
 * 2^-3 is allowed; the minus of -2^2 is outside (unary), giving -(2^2). */
static E *p_power(lexer *L) {
    stack_check();
    E *b = p_postfix(L);
    if (is_op(L, "^")) {
        next(L);
        if (++L->depth > 200) syntax(L, "nesting too deep");
        E *e = p_unary(L);
        L->depth--;
        return mk_pow(b, e);
    }
    return b;
}

/* unary := ('-'|'+') unary | power.  A minus applied to a literal number is
 * folded into the literal (-3 becomes the integer -3, not (-1)*3); otherwise
 * -a is represented as (-1)*a, the canonical form of negation. */
static E *p_unary(lexer *L) {
    stack_check();
    if (is_op(L, "-")) {
        next(L);
        if (++L->depth > 400) syntax(L, "nesting too deep");
        E *a = p_unary(L);
        L->depth--;
        if (a->t == E_INT && a->n) return int_neg(a);
        if (a->t == E_FLT) return mk_flt(-EF(a));
        return mk_mul2(mk_int(-1), a);
    }
    if (is_op(L, "+")) {
        next(L);
        if (++L->depth > 400) syntax(L, "nesting too deep");
        E *a = p_unary(L);
        L->depth--;
        return a;
    }
    return p_power(L);
}

/* Can the current token begin an implicitly multiplied factor?  Numbers,
 * identifiers (except the keyword operators and/or/not/mod), '(' and '['.
 * ('|' and '{' are not included, so 2|x| needs an explicit '*'.) */
static int starts_primary(lexer *L) {
    if (L->tok == T_NUM || L->tok == T_ID) {
        if (is_id(L, "and") || is_id(L, "or") || is_id(L, "not") || is_id(L, "mod") || is_id(L, "xor") ||
            is_id(L, "implies") || is_id(L, "iff"))
            return 0;
        return 1;
    }
    return is_op(L, "(") || is_op(L, "[");
}

/* term := unary {'*' unary | '/' unary | 'mod' unary | implicit power}.
 * The implicit factor is a power, not a unary ('-' never starts one, so
 * "2 -x" is a subtraction), and it may not start with a number: "x 2" and
 * "2 3" are syntax errors rather than products. */
static E *p_term(lexer *L) {
    E *e = p_unary(L);
    for (;;) {
        if (is_op(L, "*")) {
            next(L);
            e = mk_mul2(e, p_unary(L));
        } else if (is_op(L, "/")) {
            next(L);
            e = mk_mul2(e, mk_pow(p_unary(L), mk_int(-1)));
        } else if (is_id(L, "mod")) {
            next(L);
            e = mk_fn2(F_MOD, e, p_unary(L));
        } else if (starts_primary(L)) {
            if (L->tok == T_NUM) syntax(L, "syntax error");
            e = mk_mul2(e, p_power(L));
        } else
            break;
    }
    return e;
}

/* sum := term {('+'|'-'|'±') term}, left associative; a - b is a + (-1)*b;
 * a ± b is the call pm(a, b) (uncertainty, unc.c), so x+1±0.1 is
 * pm(x+1, 0.1) and 9.81±0.02*2 is pm(9.81, 0.04). */
static E *p_sum(lexer *L) {
    E *e = p_term(L);
    for (;;) {
        if (is_op(L, "+")) {
            next(L);
            e = mk_add2(e, p_term(L));
        } else if (is_op(L, "-")) {
            next(L);
            e = mk_add2(e, mk_mul2(mk_int(-1), p_term(L)));
        } else if (is_op(L, "+/-")) {
            next(L);
            e = mk_fn2(sym_intern("pm", 2), e, p_term(L));
        } else
            break;
    }
    return e;
}

/* rel := sum [relop sum] with relop one of = < > <= >= != (after lexer
 * canonicalisation).  At most one relation; the result is an E_FN node with
 * the operator id. */
static E *p_rel(lexer *L) {
    E *e = p_sum(L);
    static const struct {
        const char *o;
        int id;
    } rel[] = {{"=", F_EQ}, {"<", F_LT}, {">", F_GT}, {"<=", F_LE}, {">=", F_GE}, {"!=", F_NE}};
    for (size_t i = 0; i < sizeof rel / sizeof rel[0]; i++)
        if (is_op(L, rel[i].o)) {
            next(L);
            E *r = p_sum(L);
            return mk_fn2(rel[i].id, e, r);
        }
    return e;
}

/* not := 'not' not | rel.  'not' binds looser than relations, so
 * "not x = 1" is not(x = 1). */
static E *p_not(lexer *L) {
    if (is_id(L, "not")) {
        next(L);
        stack_check();
        return mk_fn1(F_NOT, p_not(L));
    }
    return p_rel(L);
}

/* and := not {('and' | '&&') not}, left associative. */
static E *p_and(lexer *L) {
    E *e = p_not(L);
    while (is_id(L, "and") || is_op(L, "&&")) {
        next(L);
        e = mk_fn2(F_AND, e, p_not(L));
    }
    return e;
}

/* or := and {('or' | '||' | 'xor') and}, left associative; a xor b is the call xor(a, b). */
static E *p_or(lexer *L) {
    E *e = p_and(L);
    for (int x; (x = is_id(L, "xor")) || is_id(L, "or") || is_op(L, "||");) {
        next(L);
        E *r = p_and(L);
        e = x ? mk_fn2(sym_intern("xor", 3), e, r) : mk_fn2(F_OR, e, r);
    }
    return e;
}

/* expr := or ['implies' expr | 'iff' expr]: the full expression grammar, used for arguments,
 * parentheses and statement sides; a implies b, a iff b are the calls implies(a, b), iff(a, b)
 * (right associative, looser than or). */
static E *p_expr(lexer *L) {
    stack_check();
    E *e = p_or(L);
    int im = is_id(L, "implies");
    if (im || is_id(L, "iff")) {
        next(L);
        E *r = p_expr(L);
        e = mk_fn2(im ? sym_intern("implies", 7) : sym_intern("iff", 3), e, r);
    }
    return e;
}

/* stmt := expr [':=' expr | '=>' name].  Both forms produce
 * :=(target, value) ("v => a" is stored as a := v).  The target is not
 * validated here; do_assign (eval.c) checks it. */
static E *p_stmt(lexer *L) {
    E *e = p_expr(L);
    if (is_op(L, ":=")) {
        next(L);
        E *r = p_expr(L);
        return mk_fn2(F_ASSIGN, e, r);
    }
    if (is_op(L, "=>")) {
        next(L);
        if (L->tok != T_ID) syntax(L, "expected name after =>");
        E *v = mk_sym(id_of(L));
        next(L);
        return mk_fn2(F_ASSIGN, v, e);
    }
    return e;
}

/* Public: parse the NUL-terminated string s into a raw tree in scratch.
 * Several statements separated by ';' become one F_SEQ node (a trailing ';'
 * is allowed); eval.c evaluates them in order and returns the last value.
 * Empty input and trailing garbage are syntax errors.  Identifiers are
 * interned into the symbol table as a side effect (new names persist until
 * the next symbol GC). */
E *parse(const char *s) {
    lexer L;
    memset(&L, 0, sizeof L);
    memset(primed, 0, sizeof primed);
    for (size_t i = 1, k = 0; s[i] && k < sizeof primed - 1; i++)
        if (s[i] == '\'' && isalpha((unsigned char)s[i - 1]) && (i < 2 || !isalnum((unsigned char)s[i - 2])) &&
            !strchr(primed, s[i - 1]))
            primed[k++] = s[i - 1];
    L.s = s;
    next(&L);
    if (L.tok == T_END) syntax(&L, "empty input");
    E *e = p_stmt(&L);
    if (is_op(&L, ";")) {
        uint32_t cap = 8, n = 1;
        E **v = salloc(sizeof(E *) * cap);
        v[0] = e;
        while (is_op(&L, ";")) {
            next(&L);
            if (L.tok == T_END) break;
            if (n == cap) {
                E **nv = salloc(sizeof(E *) * cap * 2);
                memcpy(nv, v, sizeof(E *) * cap);
                v = nv;
                cap *= 2;
            }
            v[n++] = p_stmt(&L);
        }
        e = mk_fn(F_SEQ, n, v);
    }
    if (L.tok != T_END) {
        if (is_op(&L, ")")) syntax(&L, "unbalanced ')'");
        syntax(&L, "syntax error");
    }
    return e;
}
