/*
 * expr.c -- symbol table, node constructors, canonical ordering, equality.
 *
 * This is the lowest expression layer of the CAS; almost every other module
 * depends on it:
 *
 *  - Symbol table.  Names are interned once (sym_intern) into K->syms with an
 *    FNV-1a hash into K->shash (HSIZE buckets, chains through symrec.next).
 *    Each symbol owns exactly one permanent E_SYM node (in K->symnodes, outside
 *    scratch and heap), so mk_sym() never allocates and symbols can be compared
 *    by id.  The parser (parse.c) interns identifiers; eval.c looks up the
 *    variable values / user functions stored in the records.
 *  - Raw constructors (mk_*).  They build nodes in scratch WITHOUT
 *    simplification and without setting F_SIMP (except mk_str); the parser
 *    produces such raw trees and simp() (simp.c) canonicalises them.  Algebra
 *    code should use the s_* constructors of simp.c instead.
 *  - Canonical order (expr_cmp).  The classic "automatic simplification" order
 *    of Cohen, "Computer Algebra and Symbolic Computation: Mathematical
 *    Methods", sec. 3.1 (order relation O-1 .. O-13): numbers first by value,
 *    symbols alphabetically, sums/products compared from their last operands,
 *    powers by base then exponent, functions by name then arguments, plus the
 *    mixed-type rules that compare e.g. x with x^2 or x with x*y as if x were
 *    x^1 or a one-factor product.  Extended here to lists and strings.  It is
 *    a total order on canonical expressions, so sorting operand lists with it
 *    (esort, simp.c) yields a unique representation, and expr_eq can be a
 *    plain structural comparison.
 *  - Structural predicates (expr_eq, freeof, is_const_expr, ...), used
 *    everywhere.
 *  - Symbol-table garbage collection (sym_gc), run by cas_eval (cas.c) when
 *    the table is 3/4 full.
 *
 * Memory: constructors allocate in scratch via ealloc/enode (mem.c) and throw
 * CAS_ERR_MEMORY when it is full.  Symbol records/nodes live in the permanent
 * part of the arena.  Recursive walkers call stack_check() so deep inputs end
 * in CAS_ERR_MEMORY instead of a stack overflow.
 */
#include "cas_int.h"
#include <string.h>

/* Number of hash buckets in K->shash (power of two, so "& (HSIZE - 1)" is the
 * bucket index).  Must match the bucket count allocated by cas_init. */
#define HSIZE 1024u

/* ------------------------------------------------------------ symbols --- */
/* 32-bit FNV-1a hash of the n bytes at s (offset basis 2166136261, prime
 * 16777619).  Used for symbol-table buckets and for hashing string nodes. */
static uint32_t hash_str(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
    return h;
}

/*
 * Look up the symbol named by the n bytes at s (not NUL-terminated).
 * Returns its id, or -1 if it has never been interned.  Pure lookup: no
 * allocation, no side effects.  Expected O(1 + chain length).
 */
int sym_find(const char *s, size_t n) {
    uint32_t h = hash_str(s, n) & (HSIZE - 1);
    for (uint16_t i = K->shash[h]; i != 0xffff; i = K->syms[i].next) {
        symrec *r = &K->syms[i];
        if (r->len == n && memcmp(K->names + r->name, s, n) == 0) return i;
    }
    return -1;
}

/* Symbol nodes live in a permanent array right after the symbol records.
 * sym_node_area(i) is the address of the node slot for symbol id i (defined
 * in cas.c, which lays out the symbol area at cas_init). */
extern E *sym_node_area(uint32_t i);

/*
 * Return the id of the symbol named by the n bytes at s, creating it if
 * needed.  A new symbol gets a fresh record (no value, no function
 * definition, no assumptions), its name is appended to the name pool
 * K->names (NUL-terminated), it is pushed onto the front of its hash chain,
 * and its permanent E_SYM node is initialised (already flagged F_SIMP).
 *
 * Errors: CAS_ERR_SYNTAX for names longer than 200 bytes (symrec.len is a
 * uint8_t); CAS_ERR_MEMORY when the record table or name pool is full.  The
 * table is compacted by sym_gc at the start of the next cas_eval once it is
 * 3/4 full, so long sessions with many distinct names keep working.
 */
int sym_intern(const char *s, size_t n) {
    int f = sym_find(s, n);
    if (f >= 0) return f;
    if (n > 200) cas_throw(CAS_ERR_SYNTAX, "name too long");
    if (K->nsyms >= K->maxsyms || K->names_used + n + 1 > K->names_cap)
        cas_throw(CAS_ERR_MEMORY, "symbol table full");
    uint32_t id = K->nsyms++;
    symrec *r = &K->syms[id];
    r->name = K->names_used;
    r->len = (uint8_t)n;
    r->assume = 0;
    memcpy(K->names + K->names_used, s, n);
    K->names[K->names_used + n] = 0;
    K->names_used += (uint32_t)n + 1;
    uint32_t h = hash_str(s, n) & (HSIZE - 1);
    r->next = K->shash[h];
    K->shash[h] = (uint16_t)id;
    r->val = NULL;
    r->fdef = NULL;
    E *node = sym_node_area(id);
    node->t = E_SYM;
    node->f = F_SIMP;
    node->s = (uint16_t)id;
    node->n = 0;
    r->node = node;
    return (int)id;
}

/* Name of symbol id (NUL-terminated, points into the permanent name pool). */
const char *sym_name(int id) { return K->names + K->syms[id].name; }

/* The unique permanent node of symbol id; never allocates. */
E *mk_sym(int id) { return K->syms[id].node; }

/* Symbol node for a C string, interning it if new (may throw like sym_intern). */
E *mk_sym_name(const char *s) { return mk_sym(sym_intern(s, strlen(s))); }

/* ------------------------------------------------------- constructors --- */
/*
 * Allocate a scratch node of type t with room for n child pointers; e->n is
 * set, the children are left for the caller to fill (ealloc zeroes the header
 * fields, so s = 0 and f = 0, i.e. not simplified).  Throws CAS_ERR_MEMORY.
 */
E *enode(int t, uint32_t n) {
    E *e = ealloc(t, sizeof(E *) * n);
    e->n = n;
    return e;
}

/* String node holding a copy of the n bytes at s plus a terminating NUL.
 * Strings are atomic, so the node is marked F_SIMP immediately. */
E *mk_str(const char *s, size_t n) {
    E *e = ealloc(E_STR, n + 1);
    e->n = (uint32_t)n;
    memcpy(ESTR(e), s, n);
    ESTR(e)[n] = 0;
    e->f = F_SIMP;
    return e;
}

/*
 * Raw function application id(args[0], ..., args[n-1]) (copies the pointer
 * array).  No simplification and no arity check: s_fn (simp.c) is the
 * simplifying counterpart.  mk_fn1 / mk_fn2 are convenience forms.
 */
E *mk_fn(int id, uint32_t n, E **args) {
    E *e = enode(E_FN, n);
    e->s = (uint16_t)id;
    for (uint32_t i = 0; i < n; i++) EK(e)[i] = args[i];
    return e;
}
E *mk_fn1(int id, E *a) { return mk_fn(id, 1, &a); }
/* Raw two-argument application id(a, b). */
E *mk_fn2(int id, E *a, E *b) {
    E *v[2] = {a, b};
    return mk_fn(id, 2, v);
}
/* Raw list [args...] (copies the pointer array, elements not simplified). */
E *mk_list(uint32_t n, E **args) {
    E *e = enode(E_LIST, n);
    for (uint32_t i = 0; i < n; i++) EK(e)[i] = args[i];
    return e;
}
/* Raw binary sum / product / power nodes, as produced by the parser.  They are
 * not flattened or sorted; simp() turns them into canonical form. */
E *mk_add2(E *a, E *b) {
    E *e = enode(E_ADD, 2);
    K0(e) = a;
    K1(e) = b;
    return e;
}
/* Raw binary product a*b (unsorted, not flattened). */
E *mk_mul2(E *a, E *b) {
    E *e = enode(E_MUL, 2);
    K0(e) = a;
    K1(e) = b;
    return e;
}
/* Raw power a^b (no power rules applied). */
E *mk_pow(E *a, E *b) {
    E *e = enode(E_POW, 2);
    K0(e) = a;
    K1(e) = b;
    return e;
}

/*
 * Fresh, unsimplified copy of a compound node e (type, s and child pointers;
 * the children themselves are shared).  Only meaningful for node types whose
 * payload is child pointers (sum, product, power, function, list, rational).
 */
E *expr_copy_shallow(E *e) {
    E *c = enode(e->t, e->n);
    c->s = e->s;
    for (uint32_t i = 0; i < e->n; i++) EK(c)[i] = EK(e)[i];
    return c;
}

/* ------------------------------------------------------------- order --- */
/*
 * Compare two operand sequences from the END (Cohen's rule O-3 for sums and
 * products): the last operands are the "most significant" because canonical
 * sums and products are sorted ascending.  If one sequence is a suffix of the
 * other, the shorter one is smaller.  E.g. x*y < x*z (compare y with z) and
 * y < x*y (y is a suffix of x*y).
 */
static int cmp_seq_rev(E **a, uint32_t an, E **b, uint32_t bn) {
    uint32_t i = an, j = bn;
    while (i > 0 && j > 0) {
        int c = expr_cmp(a[--i], b[--j]);
        if (c) return c;
    }
    if (an == bn) return 0;
    return an < bn ? -1 : 1;
}

/* Lexicographic comparison from the front (function arguments, lists); a
 * proper prefix is smaller. */
static int cmp_seq_fwd(E **a, uint32_t an, E **b, uint32_t bn) {
    uint32_t i;
    for (i = 0; i < an && i < bn; i++) {
        int c = expr_cmp(a[i], b[i]);
        if (c) return c;
    }
    if (an == bn) return 0;
    return an < bn ? -1 : 1;
}

/* Coarse rank of a node type: numbers (0) < ordinary expressions (1) < lists
 * (2) < strings (3).  Values of different rank compare by rank alone. */
static int type_rank(int t) {
    switch (t) {
    case E_INT: case E_RAT: case E_FLT: return 0;
    case E_LIST: return 2;
    case E_STR: return 3;
    default: return 1;
    }
}

/*
 * The canonical total order on simplified expressions: returns -1, 0 or +1.
 *
 * Rules, following Cohen's order relation (O-1 .. O-13):
 *   - numbers: by numeric value; equal values of different representation
 *     order by node type (E_INT < E_RAT < E_FLT), so 1 and 1.0 stay distinct;
 *   - symbols: alphabetically by name (strcmp);
 *   - sums and products: operand lists compared from the last operand
 *     (cmp_seq_rev);
 *   - powers: by base, then by exponent;
 *   - functions: by name, then arguments left to right;
 *   - lists: elementwise from the front; strings: strcmp.
 * Mixed types (the cases that make the order agree with how terms "look"):
 *   - product u vs. non-product v: compare u with the one-factor product (v);
 *   - power u vs. non-power v: compare u with v^1;
 *   - sum u vs. symbol/function v: compare u with the one-term sum (v);
 *   - function vs. symbol: by name; if the names agree the function is
 *     larger (f < f(x));
 * the symmetric cases are handled by swapping the arguments and negating.
 *
 * Callers rely on: expr_cmp(u, v) == 0 iff u and v are structurally equal
 * (for canonical inputs), which is what makes sorted operand lists canonical.
 * Recursive (stack_check); cost is linear in the size of the common prefix
 * that has to be inspected.
 */
int expr_cmp(E *u, E *v) {
    if (u == v) return 0;
    stack_check();
    int ru = type_rank(u->t), rv = type_rank(v->t);
    if (ru != rv) return ru < rv ? -1 : 1;
    if (ru == 0) {
        int c = num_cmp(u, v);
        if (c) return c;
        if (u->t != v->t) return u->t < v->t ? -1 : 1;
        if (u->t == E_FLT) return 0;
        return 0;
    }
    if (ru == 2) return cmp_seq_fwd(EK(u), u->n, EK(v), v->n);
    if (ru == 3) {
        int c = strcmp(ESTR(u), ESTR(v));
        return c < 0 ? -1 : c > 0;
    }
    if (u->t == v->t) {
        switch (u->t) {
        case E_SYM: {
            if (u->s == v->s) return 0;
            int c = strcmp(sym_name(u->s), sym_name(v->s));
            return c < 0 ? -1 : 1;
        }
        case E_ADD:
        case E_MUL: return cmp_seq_rev(EK(u), u->n, EK(v), v->n);
        case E_POW: {
            int c = expr_cmp(K0(u), K0(v));
            if (c) return c;
            return expr_cmp(K1(u), K1(v));
        }
        case E_FN: {
            if (u->s != v->s) {
                int c = strcmp(sym_name(u->s), sym_name(v->s));
                if (c) return c < 0 ? -1 : 1;
                return u->s < v->s ? -1 : 1;
            }
            return cmp_seq_fwd(EK(u), u->n, EK(v), v->n);
        }
        }
    }
    /* Different non-number types.  The order of these tests matters: product
     * is tested before power, power before sum, so e.g. (x+1)^2 vs. x*y goes
     * through the product rule and x^2 vs. (x+1) through the power rule. */
    if (u->t == E_MUL) return cmp_seq_rev(EK(u), u->n, &v, 1);
    if (v->t == E_MUL) return -expr_cmp(v, u);
    if (u->t == E_POW) {
        int c = expr_cmp(K0(u), v);
        if (c) return c;
        return expr_cmp(K1(u), K->c1);
    }
    if (v->t == E_POW) return -expr_cmp(v, u);
    if (u->t == E_ADD) return cmp_seq_rev(EK(u), u->n, &v, 1);
    if (v->t == E_ADD) return -expr_cmp(v, u);
    if (u->t == E_FN && v->t == E_SYM) {
        int c = strcmp(sym_name(u->s), sym_name(v->s));
        if (c == 0) return 1;
        return c < 0 ? -1 : 1;
    }
    if (u->t == E_SYM && v->t == E_FN) return -expr_cmp(v, u);
    return u->t < v->t ? -1 : 1;
}

/*
 * Structural equality (1/0).  Pointer equality is a fast path; otherwise the
 * headers (type, count, sign / symbol id / function id) must agree and then:
 * integers compare limbs, floats compare the double (so 0.0 == -0.0 and a NaN
 * node equals only itself by pointer; for arbitrary-precision float nodes
 * only the double and limb count are compared, not the mantissa limbs),
 * symbols by id, strings by content, compound nodes child by child.  F_SIMP
 * is ignored.  For canonical expressions this is mathematical identity of
 * the representation, not mathematical equivalence (x*(x+1) != x^2+x).
 */
int expr_eq(E *a, E *b) {
    if (a == b) return 1;
    if (a->t != b->t || a->n != b->n || a->s != b->s) return 0;
    switch (a->t) {
    case E_INT: return memcmp(EL(a), EL(b), sizeof(limb) * a->n) == 0;
    case E_FLT: return EF(a) == EF(b);
    case E_SYM: return 1;
    case E_STR: return strcmp(ESTR(a), ESTR(b)) == 0;
    default:
        stack_check();
        for (uint32_t i = 0; i < a->n; i++)
            if (!expr_eq(EK(a)[i], EK(b)[i])) return 0;
        return 1;
    }
}

/*
 * Structural hash consistent with expr_eq for exact data (but note that 0.0
 * and -0.0 are expr_eq-equal and hash differently).  Visits every node
 * (shared subexpressions are hashed once per occurrence).
 */
uint32_t expr_hash(E *a) {
    stack_check();
    uint32_t h = (uint32_t)a->t * 31u + a->s;
    switch (a->t) {
    case E_INT:
        for (uint32_t i = 0; i < a->n; i++) h = h * 1000003u ^ EL(a)[i];
        return h;
    case E_FLT: {
        uint64_t u;
        memcpy(&u, &EF(a), 8);
        return h ^ (uint32_t)u ^ (uint32_t)(u >> 32);
    }
    case E_SYM: return h;
    case E_STR: return h ^ hash_str(ESTR(a), a->n);
    default:
        for (uint32_t i = 0; i < a->n; i++) h = h * 1000003u ^ expr_hash(EK(a)[i]);
        return h;
    }
}

/* is_sym: e is symbol id; is_fn: e is an application of function id (function
 * versions of the ISYM / IFN macros). */
int is_sym(E *e, int id) { return e->t == E_SYM && e->s == id; }
int is_fn(E *e, int id) { return e->t == E_FN && e->s == id; }

/* True for node types whose payload is child pointers.  Note that E_RAT counts
 * (its children are the numerator/denominator integers); walkers that look for
 * symbols skip rationals explicitly. */
static int has_kids(E *e) { return e->t != E_INT && e->t != E_FLT && e->t != E_SYM && e->t != E_STR; }

/*
 * 1 if no subexpression of e is structurally equal (expr_eq) to x, else 0.
 * Matches only whole nodes: x+y is not found inside x+y+z, and integers
 * inside a rational are not visited.  Cost O(size(e) * cost of expr_eq).
 */
int freeof(E *e, E *x) {
    if (expr_eq(e, x)) return 0;
    if (!has_kids(e) || e->t == E_RAT) return 1;
    stack_check();
    for (uint32_t i = 0; i < e->n; i++)
        if (!freeof(EK(e)[i], x)) return 0;
    return 1;
}

/*
 * 1 if symbol id occurs nowhere in e, neither as a symbol node nor as the
 * head of a function application (so freeof_sym(f(x), f) == 0).  Cheaper than
 * freeof because it compares ids only.
 */
int freeof_sym(E *e, int id) {
    if (e->t == E_SYM) return e->s != id;
    if (e->t == E_FN && e->s == id) return 0;
    if (!has_kids(e) || e->t == E_RAT) return 1;
    stack_check();
    for (uint32_t i = 0; i < e->n; i++)
        if (!freeof_sym(EK(e)[i], id)) return 0;
    return 1;
}

/*
 * 1 if e is a closed constant: it contains no symbols other than e, pi, i and
 * euler_gamma, no lists, no commands, and every function is one of the
 * mathematical builtins in [F_SQRT, S__FIRSTCMD).  Operators stored as
 * functions (=, <, factorial, ...) and user functions make e non-constant.
 * inf and undef are not constants.  (Numbers and strings count as constant.)
 * Used to decide when numeric evaluation (eval_complex) may settle a
 * question such as a sign or a floor.
 */
int is_const_expr(E *e) {
    stack_check();
    if (e->t == E_SYM) return e->s == S_E || e->s == S_PI || e->s == S_I || e->s == S_EULERG;
    if (!has_kids(e) || e->t == E_RAT) return 1;
    if (e->t == E_LIST) return 0;
    if (e->t == E_FN && e->s >= S__FIRSTCMD) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (!is_const_expr(EK(e)[i])) return 0;
    if (e->t == E_FN) {
        /* unknown functions are not constants */
        if (e->s < F_SQRT || e->s >= S__FIRSTCMD) return 0;
    }
    return 1;
}

/*
 * Number of nodes of e counted as a tree (shared subexpressions count once
 * per occurrence; rationals count as one node).  Stops descending into
 * further children once the running count reaches cap, so the result is
 * only an approximate size above cap, but the walk stays bounded.
 */
static size_t size_rec(E *e, size_t cap) {
    stack_check();
    size_t n = 1;
    if (has_kids(e) && e->t != E_RAT)
        for (uint32_t i = 0; i < e->n && n < cap; i++) n += size_rec(EK(e)[i], cap);
    return n;
}
/* Size of e in nodes, capped at roughly 10^6 (used as a complexity measure). */
size_t expr_size(E *e) { return size_rec(e, 1000000); }

/* 1 if e contains a floating-point number (i.e. it is "approximate"). */
int has_float(E *e) {
    stack_check();
    if (e->t == E_FLT) return 1;
    if (!has_kids(e) || e->t == E_RAT) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_float(EK(e)[i])) return 1;
    return 0;
}

/* ------------------------------------------------- symbol table GC ---
 * Called at the start of cas_eval (no scratch references alive).  Keeps the
 * builtins and every symbol referenced from persistent values (variables,
 * user functions, ans); renumbers the survivors and rewrites the persistent
 * expressions accordingly. */
/* Is e one of the permanent per-symbol E_SYM nodes in K->symnodes?  Those
 * are rebuilt wholesale by sym_gc instead of being remapped in place. */
static int in_symnodes(const E *e) {
    return (const char *)e >= K->symnodes && (const char *)e < K->symnodes + K->symnode_size * K->maxsyms;
}

/*
 * Mark phase: set used[id] for every symbol and every function head reachable
 * from persistent value e.  Compound nodes are tagged with the temporary flag
 * bit 0x80 (unused by the rest of the CAS, which only knows F_SIMP = 1) so
 * that shared subexpressions of the DAG are visited once.  Atomic nodes are
 * not tagged.  The 0x80 tags are removed again by gc_remap.
 */
static void gc_mark(E *e, uint8_t *used) {
    if (!e || (e->f & 0x80)) return;
    if (e->t == E_SYM) {
        used[e->s] = 1;
        return;
    }
    if (in_symnodes(e)) return;
    if (e->t == E_FN) used[e->s] = 1;
    if (e->t == E_INT || e->t == E_FLT || e->t == E_STR) return;
    e->f |= 0x80;
    for (uint32_t i = 0; i < e->n; i++) gc_mark(EK(e)[i], used);
}

/*
 * Remap phase: rewrite every symbol / function id in persistent value e from
 * its old id to map[old], and clear the 0x80 visit tags left by gc_mark (a
 * node without the tag has already been remapped, which handles sharing).
 *
 * Two kinds of symbol nodes occur in stored values:
 *  - pointers to the permanent nodes in K->symnodes (hstore references
 *    permanent nodes instead of copying them).  The CHILD POINTER is
 *    redirected to the node slot of the new id; the slot itself is
 *    reinitialised at the end of sym_gc.
 *  - private heap copies of an E_SYM node (hstore of a bare symbol value
 *    makes one).  Their id is rewritten in place, guarded by the temporary
 *    flag 0x40 so a shared copy is not remapped twice; gc_clear_symflag
 *    removes the 0x40 flags afterwards.
 */
static void gc_remap(E *e, const uint16_t *map) {
    if (!e || in_symnodes(e)) return;
    if (!(e->f & 0x80) && e->t != E_SYM) return; /* already remapped (flag cleared) */
    if (e->t == E_SYM) {
        if (e->f & 0x40) return;
        e->s = map[e->s];
        e->f |= 0x40;
        return;
    }
    e->f &= (uint8_t)~0x80;
    if (e->t == E_FN) e->s = map[e->s];
    if (e->t == E_INT || e->t == E_FLT || e->t == E_STR) return;
    for (uint32_t i = 0; i < e->n; i++) {
        E *c = EK(e)[i];
        if (in_symnodes(c)) EK(e)[i] = sym_node_area(map[c->s]);
        else gc_remap(c, map);
    }
}

/* Clear the 0x40 "already remapped" flag on heap-copied symbol nodes in e.
 * This walk has no visited marks, so shared subexpressions are traversed
 * once per occurrence (tree size, not DAG size). */
static void gc_clear_symflag(E *e) {
    if (!e || in_symnodes(e)) return;
    if (e->t == E_SYM) {
        e->f &= (uint8_t)~0x40;
        return;
    }
    if (e->t == E_INT || e->t == E_FLT || e->t == E_STR) return;
    for (uint32_t i = 0; i < e->n; i++) gc_clear_symflag(EK(e)[i]);
}

/*
 * Compact the symbol table.  Live symbols are: all builtins (ids < S__COUNT),
 * every symbol with a value, function definition or assumption, and every
 * symbol or function name referenced from a stored value, a user-function
 * body or ans.  The survivors keep their relative order and are renumbered
 * densely 0..nn-1 (builtins keep their ids, since they come first).
 *
 * Steps: (1) mark reachable ids; (2) build the old -> new id map; (3) rewrite
 * the persistent expressions; (4) if anything died, compact records and the
 * name pool (map[i] <= i, so copying in increasing i never overwrites a record
 * that is still needed), then rebuild the hash buckets and the permanent
 * symbol nodes.
 *
 * Must only run when no scratch expression is alive (cas.c calls it at the
 * start of cas_eval and resets scratch afterwards): any scratch node holding
 * an old id or an old symnode pointer would silently change meaning.  Uses
 * scratch for the work arrays; may throw CAS_ERR_MEMORY from salloc.
 */
void sym_gc(void) {
    uint32_t n = K->nsyms;
    uint8_t *used = salloc(n);
    uint16_t *map = salloc(sizeof(uint16_t) * n);
    memset(used, 0, n);
    for (uint32_t i = 0; i < S__COUNT && i < n; i++) used[i] = 1;
    for (uint32_t i = 0; i < n; i++) {
        symrec *r = &K->syms[i];
        if (r->val || r->fdef || r->assume) used[i] = 1;
        gc_mark(r->val, used);
        gc_mark(r->fdef, used);
    }
    gc_mark(K->ans, used);
    /* old id -> new id for the survivors; dead ids are left unmapped */
    uint32_t nn = 0;
    for (uint32_t i = 0; i < n; i++)
        if (used[i]) map[i] = (uint16_t)nn++;
    /* rewrite persistent expressions (clears the 0x80 marks) */
    for (uint32_t i = 0; i < n; i++) {
        gc_remap(K->syms[i].val, map);
        gc_remap(K->syms[i].fdef, map);
    }
    gc_remap(K->ans, map);
    for (uint32_t i = 0; i < n; i++) {
        gc_clear_symflag(K->syms[i].val);
        gc_clear_symflag(K->syms[i].fdef);
    }
    gc_clear_symflag(K->ans);
    if (nn == n) return;
    /* Compact records, names, nodes.  The names are first gathered into a
     * scratch copy because the new name offsets can overlap old names that
     * have not been copied yet. */
    char *names = salloc(K->names_used + 1);
    uint32_t nu = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!used[i]) continue;
        symrec r = K->syms[i];
        uint32_t len = r.len;
        memcpy(names + nu, K->names + r.name, len + 1);
        r.name = nu;
        nu += len + 1;
        K->syms[map[i]] = r;
    }
    memcpy(K->names, names, nu);
    K->names_used = nu;
    K->nsyms = nn;
    for (uint32_t i = 0; i < HSIZE; i++) K->shash[i] = 0xffff;
    for (uint32_t i = 0; i < nn; i++) {
        symrec *r = &K->syms[i];
        uint32_t h = hash_str(K->names + r->name, r->len) & (HSIZE - 1);
        r->next = K->shash[h];
        K->shash[h] = (uint16_t)i;
        E *node = sym_node_area(i);
        node->t = E_SYM;
        node->f = F_SIMP;
        node->s = (uint16_t)i;
        node->n = 0;
        r->node = node;
    }
}
