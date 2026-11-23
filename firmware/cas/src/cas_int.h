/*
 * cas_int.h -- internal declarations shared by all CAS modules.
 *
 * Design summary (see README.md for the long version):
 *  - Every value is an immutable expression node (E).  Nodes are allocated in
 *    a per-evaluation *scratch* region (bump allocator) that is reset at the
 *    start of every cas_eval.  Values that must outlive an evaluation
 *    (variables, user functions, ans, compiled functions) are deep-copied into
 *    a *persistent heap* (first-fit free list with coalescing) and freed when
 *    overwritten.  Long computations can compact scratch with skeep().
 *  - Errors (syntax, domain, out-of-memory, interrupt, recursion depth) are
 *    raised with cas_throw() which longjmps to the innermost handler.
 *  - A single "current context" pointer K is set by each public entry point.
 *
 * Conventions used by every module (students: read this first):
 *  - "E *" values are never mutated after construction, except for the
 *    F_SIMP flag and during compaction (skeep).  Subexpressions are shared
 *    freely, so an expression is a DAG, not a tree.
 *  - Functions returning E * allocate their result in scratch unless stated
 *    otherwise.  A result stays valid until the scratch region below it is
 *    released (srelease / skeep to an older mark) or the evaluation ends.
 *  - Constructors named mk_* build raw nodes; s_* constructors (simp.c)
 *    return automatically simplified, canonical nodes.  Almost all algebra is
 *    written with the s_* constructors.
 *  - Functions that "fail" softly return NULL (or 0); hard errors (domain,
 *    memory, interrupt) are raised with cas_throw and never return.  Code that
 *    wants to try something and fall back on error installs its own handler
 *    with try_enter + setjmp (see the tryf comment below).
 *  - Deep recursions call stack_check() so that pathological inputs end in
 *    CAS_ERR_MEMORY instead of a stack overflow.
 */
#ifndef CAS_INT_H
#define CAS_INT_H

#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include "../include/cas.h"
#include "stats.h" /* statistics commands (STATS_SYMS) */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

/* Bignum digits: 32-bit limbs, least significant first, with 64-bit
 * double-limbs for products and carries (portable to Cortex-M7, which has a
 * 32x32->64 multiply). */
typedef uint32_t limb;
typedef uint64_t dlimb;
#define LIMB_BITS 32

/* ------------------------------------------------------------ nodes --- *
 * Node representation.  Every node is a fixed 8-byte header (struct E below)
 * immediately followed by a type-dependent payload:
 *
 *   type     header fields          payload after the header
 *   E_INT    s = sign, n = #limbs   n limbs, least significant first; the
 *                                   top limb is nonzero (zero has n = 0)
 *   E_RAT    n = 2                  2 child pointers: numerator, denominator
 *   E_FLT    n = 0 (plain double)   a double; for arbitrary-precision results
 *            or n = #limbs          (bf.c) the double is followed by an int32
 *                                   binary exponent, an int32 digit count and
 *                                   n mantissa limbs (s = mantissa sign)
 *   E_SYM    s = symbol id          none (mk_sym returns the symbol's single
 *                                   permanent node in K->symnodes)
 *   E_ADD,   n = #operands          n child pointers (EK)
 *   E_MUL
 *   E_POW    n = 2                  base, exponent
 *   E_FN     s = function id, n     n argument pointers
 *   E_LIST   n = #elements          n child pointers
 *   E_STR    n = length             n chars + NUL
 *   E_FWD    (compaction only)      pointer to the node's new copy
 *
 * Every node occupies at least sizeof(E) + sizeof(void *) bytes so that it
 * can be overwritten by a forwarding pointer during skeep().
 */
enum {
    E_INT = 0,   /* s = sign (0/1), n = limb count, limbs follow (n=0: zero) */
    E_RAT,       /* kids: num (INT, nonzero), den (INT > 1), gcd = 1 */
    E_FLT,       /* double follows */
    E_SYM,       /* s = symbol id */
    E_ADD,       /* n-ary sum */
    E_MUL,       /* n-ary product */
    E_POW,       /* base, exponent */
    E_FN,        /* s = function symbol id, n args */
    E_LIST,      /* [a, b, ...]  (matrices are lists of equal-length lists) */
    E_STR,       /* string literal: n = length, chars follow */
    E_FWD        /* forwarding pointer used during compaction */
};

#define F_SIMP 1  /* node is in automatically-simplified canonical form */

/* Node header: t = E_* type, f = flags (F_SIMP), s = sign / symbol id /
 * function id (see table above), n = limb, child or character count. */
typedef struct E {
    uint8_t t, f;
    uint16_t s;
    uint32_t n;
} E;

/* Payload accessors: children (EK), bignum limbs (EL), the double of a float
 * (EF), characters of a string (ESTR); K0 / K1 are the first two children
 * (base and exponent of a power, numerator and denominator of a rational). */
#define EK(e) ((E **)((E *)(e) + 1))
#define EL(e) ((limb *)((E *)(e) + 1))
#define EF(e) (*(double *)((E *)(e) + 1))
#define ESTR(e) ((char *)((E *)(e) + 1))
#define K0(e) (EK(e)[0])
#define K1(e) (EK(e)[1])

/* ---------------------------------------------------- builtin symbols --- *
 * Symbol ids 0..S__COUNT-1 are pre-interned in this order.  Function nodes
 * (E_FN) refer to their name by symbol id, so user functions and builtin
 * functions share one namespace.                                           *
 *                                                                          *
 * X-macro: BUILTIN_SYMS(X) expands X(ID, "name") once per builtin, and is  *
 * used to generate both the enum of ids (below) and the name table that    *
 * cas_init interns in the same order (cas.c).  The order defines ranges:   *
 *   [0, F_EQ)               constants and special symbols                  *
 *   [F_EQ, F_SQRT)          operators represented as E_FN nodes            *
 *   [F_SQRT, S__FIRSTCMD)   mathematical functions: auto-simplified by     *
 *                           simp.c, differentiable, numerically evaluable  *
 *   (S__FIRSTCMD, S__COUNT) commands, dispatched by eval_cmd (cmds.c);     *
 *                           they are evaluated, never kept as values       *
 * so new functions must be inserted in the right section.  The statistics  *
 * commands come from STATS_SYMS (stats.h).  C_CURRENT ("_end_") closes the *
 * list.  Ids >= S__COUNT are user symbols created by sym_intern.           */
#define BUILTIN_SYMS(X) \
    X(S_E, "e") X(S_PI, "pi") X(S_I, "i") X(S_INF, "inf") X(S_UNDEF, "undef") \
    X(S_ANS, "ans") X(S_X, "x") X(S_T__, "_t") X(S_TRUE, "true") X(S_FALSE, "false") X(S_EULERG, "euler_gamma") \
    /* operators represented as functions */ \
    X(F_EQ, "=") X(F_ASSIGN, ":=") X(F_LT, "<") X(F_GT, ">") X(F_LE, "<=") \
    X(F_GE, ">=") X(F_NE, "!=") X(F_FACT, "factorial") X(F_AND, "and") X(F_OR, "or") \
    X(F_NOT, "not") X(F_LAMBDA, "->") X(F_SEQ, ";") \
    /* elementary functions (auto-simplified, differentiable, numeric) */ \
    X(F_SQRT, "sqrt") X(F_EXP, "exp") X(F_LN, "ln") X(F_LOG, "log") X(F_LOG10, "log10") \
    X(F_LOG2, "log2") \
    X(F_SIN, "sin") X(F_COS, "cos") X(F_TAN, "tan") X(F_COT, "cot") X(F_SEC, "sec") \
    X(F_CSC, "csc") X(F_ASIN, "asin") X(F_ACOS, "acos") X(F_ATAN, "atan") \
    X(F_ACOT, "acot") X(F_ASEC, "asec") X(F_ACSC, "acsc") \
    X(F_SINH, "sinh") X(F_COSH, "cosh") X(F_TANH, "tanh") X(F_ASINH, "asinh") \
    X(F_ACOSH, "acosh") X(F_ATANH, "atanh") X(F_ABS, "abs") X(F_SIGN, "sign") \
    X(F_FLOOR, "floor") X(F_CEIL, "ceil") X(F_ROUND, "round") X(F_FRAC, "frac") \
    X(F_RE, "re") X(F_IM, "im") X(F_CONJ, "conj") X(F_ARG, "arg") X(F_ATAN2, "atan2") \
    X(F_GAMMA, "gamma") X(F_ERF, "erf") X(F_HEAVISIDE, "heaviside") X(F_DIRAC, "dirac") \
    X(F_MAX, "max") X(F_MIN, "min") X(F_MOD, "mod") X(F_BINOM, "binomial") \
    X(F_ROOTOF, "rootof") X(F_PIECEWISE, "piecewise") X(F_DERIV, "derivative") \
    X(F_INTEGRAL, "integral") X(F_SUMU, "sum_") X(F_LIMIT_U, "limit_") X(F_ZETA, "zeta") \
    X(F_SI, "Si") X(F_CI, "Ci") X(F_EI, "Ei") X(F_ERFC, "erfc") X(F_ERFI, "erfi") \
    X(S__FIRSTCMD, "_cmd") \
    /* commands */ \
    X(C_DIFF, "diff") X(C_INTEGRATE, "integrate") X(C_INT, "int") X(C_LIMIT, "limit") \
    X(C_SERIES, "series") X(C_TAYLOR, "taylor") X(C_SUM, "sum") X(C_PRODUCT, "product") \
    X(C_SOLVE, "solve") X(C_CSOLVE, "csolve") X(C_FSOLVE, "fsolve") X(C_NSOLVE, "nsolve") \
    X(C_LINSOLVE, "linsolve") \
    X(C_SIMPLIFY, "simplify") X(C_EXPAND, "expand") X(C_FACTOR, "factor") \
    X(C_COLLECT, "collect") X(C_NORMAL, "normal") X(C_RATSIMP, "ratsimp") \
    X(C_PARTFRAC, "partfrac") X(C_APART, "apart") X(C_TOGETHER, "together") \
    X(C_SUBST, "subst") X(C_SUBS, "subs") X(C_APPROX, "approx") X(C_EVALF, "evalf") \
    X(C_EXACT, "exact") X(C_NUMER, "numer") X(C_DENOM, "denom") X(C_QUO, "quo") \
    X(C_REM, "rem") X(C_GCD, "gcd") X(C_LCM, "lcm") X(C_DEGREE, "degree") \
    X(C_COEFF, "coeff") X(C_COEFFS, "coeffs") X(C_LCOEFF, "lcoeff") X(C_ROOTS, "roots") \
    X(C_PROOT, "proot") X(C_RESULTANT, "resultant") X(C_DISCRIM, "discriminant") \
    X(C_TEXPAND, "texpand") X(C_TCOLLECT, "tcollect") X(C_TLIN, "tlin") \
    X(C_TRIGSIMP, "trigsimp") X(C_EXPLN, "exp2trig") X(C_TRIG2EXP, "trig2exp") \
    X(C_IFACTOR, "ifactor") X(C_ISPRIME, "isprime") X(C_NEXTPRIME, "nextprime") \
    X(C_PREVPRIME, "prevprime") X(C_IQUO, "iquo") X(C_IREM, "irem") X(C_DIVISORS, "divisors") \
    X(C_EULER, "euler") X(C_PERM, "perm") X(C_COMB, "comb") X(C_NCR, "nCr") X(C_NPR, "nPr") \
    X(C_DET, "det") X(C_INV, "inv") X(C_TRANSPOSE, "transpose") X(C_TRN, "trn") \
    X(C_RREF, "rref") X(C_RANK, "rank") X(C_TRACE, "trace") X(C_IDENTITY, "identity") \
    X(C_IDN, "idn") X(C_ZEROS, "zeros") X(C_CHARPOLY, "charpoly") \
    X(C_EIGENVALS, "eigenvals") X(C_EIGENVECTS, "eigenvects") X(C_KER, "ker") X(C_DOT, "dot") \
    X(C_CROSS, "cross") X(C_NORM, "norm") X(C_MATRIX, "matrix") X(C_SIZE, "size") \
    X(C_LENGTH, "length") X(C_SEQ, "seq") X(C_MAKELIST, "makelist") X(C_LHS, "lhs") \
    X(C_RHS, "rhs") X(C_LEFT, "left") X(C_RIGHT, "right") X(C_FREEOF, "has") \
    X(C_TYPE, "type") X(C_PURGE, "purge") X(C_RESTART, "restart") \
    X(C_TANGENT, "tangent") X(C_DIVIDE, "divide") X(C_SQRFREE, "sqrfree") \
    X(C_FACTORS, "factors") X(C_CFACTOR, "cfactor") X(C_PADE, "pade") \
    X(C_DESOLVE, "desolve") X(C_ODESOLVE, "dsolve") X(C_LAPLACE, "laplace") \
    X(C_ILAPLACE, "ilaplace") X(C_MEAN, "mean") X(C_MEDIAN, "median") \
    X(C_VARIANCE, "variance") X(C_STDDEV, "stddev") X(C_SORT, "sort") \
    X(C_REVERSE, "reverse") X(C_HORNER, "horner") X(C_ROMBERG, "nint") \
    X(C_NDIFF, "nderiv") X(C_LNCOLLECT, "lncollect") X(C_LNEXPAND, "lnexpand") \
    X(C_EXPEXPAND, "expexpand") X(C_POWEXPAND, "powexpand") X(C_CONTENT, "content") \
    X(C_PRIMPART, "primpart") X(C_CHINREM, "ichinrem") X(C_IEGCD, "iegcd") \
    X(C_EGCD, "egcd") X(C_INVMOD, "invmod") X(C_POWMOD, "powmod") \
    X(C_FLOATS, "float") X(C_CONCAT, "concat") X(C_APPLY, "apply") X(C_MAP, "map") \
    X(C_DIGITS, "digits") X(C_FOURIERA, "fourier_an") X(C_SIGNUM, "signum") \
    X(C_WHEN, "when") X(C_IFTE, "ifte") X(C_HESSIAN, "hessian") X(C_GRAD, "grad") \
    X(C_JACOBIAN, "jacobian") X(C_DIVERGENCE, "divergence") X(C_CURL, "curl") \
    X(C_LU, "lu") X(C_QR, "qr") X(C_ADJOINT, "adjoint") X(C_RANDPOLY, "randpoly") \
    X(C_INTERP, "lagrange") X(C_GBASIS, "gbasis") X(C_CHOLESKY, "cholesky") X(C_ASSUME, "assume") \
    X(C_UNASSUME, "unassume") X(C_ABOUT, "about") X(C_IMPLICITDIFF, "implicitdiff") \
    X(C_ODESOLVE_N, "odesolve") X(C_PROPFRAC, "propfrac") \
    STATS_SYMS(X) \
    X(C_CURRENT, "_end_")

/* Symbol ids of the builtins; S__COUNT is the first user symbol id. */
#define X_ENUM(id, name) id,
enum { BUILTIN_SYMS(X_ENUM) S__COUNT };
#undef X_ENUM

/* ----------------------------------------------------------- context --- */
/* free block of the persistent heap (layout private to mem.c) */
typedef struct hblk hblk;

/* assumption flags on symbols (assume command); consulted by the sign
 * analysis in simp.c (sign_of, is_int_assumed) and saved by cas_save */
#define A_POS 1
#define A_NEG 2
#define A_NONNEG 4
#define A_INT 8

/* One symbol-table entry.  Symbols are interned by name (expr.c: FNV-1a hash
 * into K->shash, 1024 buckets, chained through `next`).  Variables and user
 * functions are bound by storing persistent-heap copies in val / fdef. */
typedef struct {
    uint32_t name;      /* offset in name pool */
    uint16_t next;      /* hash chain (0xffff = end) */
    uint8_t len;
    uint8_t assume;     /* A_* flags */
    E *node;            /* persistent E_SYM node */
    E *val;             /* persistent value (variable), or NULL */
    E *fdef;            /* persistent user function: LIST[params..., body] */
} symrec;

/* Error handler frame.  Handlers form a stack through `prev` (K->tf is the
 * innermost).  Idiom:
 *
 *     tryf tf;
 *     try_enter(&tf);
 *     if (setjmp(tf.jb)) {          reached via cas_throw / rethrow
 *         try_leave(&tf);
 *         ... recover, or rethrow() to the outer handler ...
 *     }
 *     ... code that may throw ...
 *     try_leave(&tf);
 *
 * Every path out of the protected region must call try_leave exactly once.
 * Scratch allocations made inside the region are not released automatically;
 * callers usually take smark() before and srelease() in the handler. */
typedef struct tryf {
    jmp_buf jb;
    struct tryf *prev;
} tryf;

/* The engine state (opaque in cas.h).  Placed at the start of the arena by
 * cas_init; the pointers below delimit the regions carved from the arena:
 *   [ctx][syms][shash][names][symnodes][permanent constants]
 *   [h_lo .. h_hi) persistent heap   [s_lo .. s_hi) scratch, s_top = bump
 */
struct cas_ctx {
    cas_settings set;
    volatile int intr;   /* interrupt request (cas_interrupt, poll callback) */
    int (*poll)(void *); /* cas_set_poll */
    void *poll_user;
    unsigned poll_n;     /* counts checks; the callback runs every 1024th */
    char *base;          /* aligned arena start (== this struct) */
    size_t size;         /* usable arena bytes */
    /* scratch */
    char *s_lo, *s_hi, *s_top; /* region bounds; s_top = next free byte */
    size_t s_peak;             /* high-water mark of s_top - s_lo */
    /* persistent heap */
    char *h_lo, *h_hi;
    hblk *freel;               /* address-ordered free list */
    size_t h_used, h_peak;     /* bytes in allocated blocks (with headers) */
    /* symbols */
    symrec *syms;              /* maxsyms records, nsyms in use */
    uint16_t *shash;           /* 1024 hash buckets, 0xffff = empty */
    char *names;               /* NUL-terminated names, names_used bytes used */
    uint32_t nsyms, maxsyms, names_used, names_cap;
    char *symnodes;            /* permanent E_SYM node of symbol i at
                                  symnodes + i * symnode_size */
    size_t symnode_size;
    /* errors */
    tryf *tf;                  /* innermost error handler */
    cas_status err;            /* status of the last cas_throw */
    char errmsg[96];           /* its message (truncated), "" if none */
    /* recursion */
    char *stack_base;          /* address of a local in the API entry point */
    size_t stack_limit;        /* max bytes of C stack below stack_base */
    unsigned depth;            /* user-function call depth (eval.c) */
    /* state */
    E *ans;
    struct pfree *pending; /* deferred frees of persistent blocks */
    uint32_t rng;              /* xorshift32 state, reseeded per cas_eval so
                                  results are reproducible */
    int restoring; /* cas_restore: no variable/function substitution */
    /* constants: 0, 1, -1, 2, 1/2, -1/2 (built once in the permanent area,
       so they survive scratch resets) and the symbols e, pi, i, inf, undef */
    E *c0, *c1, *cm1, *c2, *chalf, *cmhalf, *e_, *pi_, *i_, *inf_, *undef_;
    /* scratch bytes evaluation may not take (salloc): kept for printing the
     * result and storing ans after a near-exhaustion, released by eval_impl
     * before those steps (appended: extensions read the fields above only) */
    size_t s_reserve;
    uint32_t ans_serial; /* bumped whenever ans is replaced (cas_ans_serial; appended) */
};

/* The current context.  Set by every public entry point in cas.c (and by
 * session.c); all internal code reaches the engine state through it.  This
 * global is why the library is single-threaded. */
extern cas_ctx *K;

/* ----------------------------------------------------------- memory --- *
 * Scratch (per evaluation, bump allocated, mem.c):
 *   salloc(n)        n bytes, 8-byte aligned; throws CAS_ERR_MEMORY when full
 *                    and polls for interrupts
 *   ealloc(t, p)     node header of type t + p payload bytes, fields zeroed
 *   smark()          current top; everything allocated later can be dropped
 *   srelease(m)      drop everything above mark m (no destructors needed)
 *   skeep(m, r)      drop everything above m except expression r, which is
 *                    copied down (Cheney-style copy with forwarding pointers,
 *                    then slid to m); returns the relocated r.  Any other
 *                    pointer into the released region becomes invalid.
 * Persistent heap (survives evaluations; first-fit, address-ordered free list
 * with coalescing):
 *   halloc / hfree   raw blocks (halloc throws CAS_ERR_MEMORY when full)
 *   hstore(e)        deep copy of e into one heap block, preserving sharing;
 *                    permanent nodes (symbols, constants) are referenced, not
 *                    copied
 *   hfree_expr(e)    free a value made by hstore.  While an evaluation is
 *                    running the free is deferred (scratch expressions may
 *                    still point into it) until flush_frees() at the end.
 *   heap_used()      bytes currently allocated in the heap
 */
void *salloc(size_t n);
E *ealloc(int t, size_t payload);
#define smark() (K->s_top)
void srelease(char *m);
E *skeep(char *mark, E *r);
void *halloc(size_t n);
void hfree(void *p);
E *hstore(E *e);
void hfree_expr(E *e);
void flush_frees(void);
size_t heap_used(void);

/* Errors and limits (mem.c):
 *   cas_throw(st, msg)  record status + message in K and longjmp to K->tf
 *   rethrow()           propagate the recorded error to the next handler
 *   try_enter/leave     push / pop a handler frame (see tryf)
 *   stack_check()       throw if the C stack used since the API entry point
 *                       exceeds K->stack_limit, or if interrupted
 *   cas_poll_step()     run the application's poll callback every 1024 calls
 *   poll_intr()         cheap interrupt check for long loops */
_Noreturn void cas_throw(cas_status st, const char *msg);
void rethrow(void);
void try_enter(tryf *t);
void try_leave(tryf *t);
void stack_check(void);
void cas_poll_step(void);
static inline void poll_intr(void) {
    if (K->poll) cas_poll_step();
    if (K->intr) cas_throw(CAS_ERR_INTERRUPTED, "interrupted");
}

/* --------------------------------------------------------- utilities --- *
 * Thin wrappers over <string.h>, and a xorshift32 generator (K->rng) for the
 * randomized algorithms (probabilistic primality and factoring, modular
 * polynomial factorisation, randpoly). */
size_t c_strlen(const char *s);
int c_strcmp(const char *a, const char *b);
int c_strncmp(const char *a, const char *b, size_t n);
void c_memcpy(void *d, const void *s, size_t n);
void c_memmove(void *d, const void *s, size_t n);
void c_memset(void *d, int c, size_t n);
uint32_t rng_next(void);

/* string builder (bounded, tracks overflow): buf always stays NUL-terminated;
 * characters that do not fit are dropped and `overflow` is set, which the
 * API turns into CAS_ERR_OUTPUT_TRUNC */
typedef struct {
    char *buf;
    size_t cap, len;
    int overflow;
} sbuf;
void sb_putc(sbuf *b, char c);
void sb_puts(sbuf *b, const char *s);
void sb_putn(sbuf *b, const char *s, size_t n);
void sb_int(sbuf *b, long v);

/* ----------------------------------------------------------- bignums --- *
 * Arbitrary-precision integers (big.c).  Arguments are E_INT nodes and are
 * not modified; results are new E_INT nodes in scratch. */
E *mk_int(int64_t v);
E *mk_uint(uint64_t v);
E *int_alloc(uint32_t n);          /* uninitialised limbs, s=0 */
E *int_norm(E *a);                 /* strip leading zero limbs */
int int_sgn(const E *a);
int int_is_zero(const E *a);
int int_is_one(const E *a);
int int_cmp(const E *a, const E *b);
int int_cmpabs(const E *a, const E *b);
int int_fits(const E *a, int64_t *out);
int int_is_even(const E *a);
E *int_neg(E *a);
E *int_abs(E *a);
E *int_add(E *a, E *b);
E *int_sub(E *a, E *b);
E *int_mul(E *a, E *b);
E *int_mul_si(E *a, int64_t b);
void int_tdivmod(E *a, E *b, E **q, E **r);  /* truncating */
void int_fdivmod(E *a, E *b, E **q, E **r);  /* floor */
E *int_tdiv(E *a, E *b);
E *int_divexact(E *a, E *b);
E *int_mod(E *a, E *b);                       /* floor mod, result has sign of b */
uint32_t int_mod_u(const E *a, uint32_t m);   /* |a| mod m */
uint32_t int_modp(const E *a, uint32_t p);    /* a mod p in [0,p) respecting sign */
E *int_gcd(E *a, E *b);
E *int_lcm(E *a, E *b);
E *int_egcd(E *a, E *b, E **s, E **t);
E *int_pow(E *a, uint32_t k);
E *int_powmod(E *a, E *e, E *m);
E *int_iroot(E *a, uint32_t k, int *exact);   /* floor(a^(1/k)), a >= 0 */
E *int_shl(E *a, uint32_t bits);
E *int_shr(E *a, uint32_t bits);
uint32_t int_bits(const E *a);
double int_to_double(const E *a);
double int_to_double_exp(const E *a, long *ex); /* mantissa*2^ex */
E *int_from_dec(const char *s, size_t n);
void int_to_dec(sbuf *b, const E *a);
E *int_fact(uint32_t n);
int int_isprime(E *n);
E *int_nextprime(E *n);
E *int_factor(E *n);  /* LIST of [p, e] pairs, ascending; n > 0 */

/* ----------------------------------------------------------- numbers --- *
 * The numeric tower (num.c): E_INT, E_RAT, E_FLT.  is_num / is_exactnum rely
 * on this enum order. */
#define is_num(e) ((e)->t <= E_FLT)
#define is_int(e) ((e)->t == E_INT)
#define is_rat(e) ((e)->t == E_RAT)
#define is_flt(e) ((e)->t == E_FLT)
#define is_exactnum(e) ((e)->t <= E_RAT)
E *mk_rat(E *n, E *d);           /* normalising */
E *mk_frac(int64_t n, int64_t d);
E *mk_flt(double x);
E *num_numer(E *a);
E *num_denom(E *a);
int num_sgn(E *a);
int num_is_zero(E *a);
int num_is_one(E *a);
int num_is_m1(E *a);
int num_cmp(E *a, E *b);
int num_eq(E *a, E *b);
E *num_add(E *a, E *b);
E *num_sub(E *a, E *b);
E *num_mul(E *a, E *b);
E *num_div(E *a, E *b);
E *num_neg(E *a);
E *num_abs(E *a);
E *num_inv(E *a);
E *num_pow_int(E *a, E *k);
E *num_floor(E *a);
double num_to_double(E *a);
E *num_from_double(double x);    /* exact binary value as rational */
int num_is_small_int(E *a, long *v);

/* ------------------------------------------------------- expressions --- *
 * Symbols and raw constructors (expr.c), plus assorted helpers whose
 * definitions live in other modules (eval.c, factor.c, linalg.c, numeric.c). */
E *enode(int t, uint32_t n);      /* children uninitialised */
E *mk_sym(int id);
E *mk_sym_name(const char *s);
E *mk_str(const char *s, size_t n);
E *mk_fn(int id, uint32_t n, E **args);
E *mk_fn1(int id, E *a);
E *mk_fn2(int id, E *a, E *b);
E *mk_list(uint32_t n, E **args);
E *mk_add2(E *a, E *b);   /* raw (unsimplified) */
E *mk_mul2(E *a, E *b);
E *mk_pow(E *a, E *b);
int sym_intern(const char *s, size_t n);
int sym_find(const char *s, size_t n);
const char *sym_name(int id);
int expr_cmp(E *a, E *b);         /* canonical total order */
int expr_eq(E *a, E *b);          /* structural equality */
uint32_t expr_hash(E *a);
int is_sym(E *e, int id);
int is_fn(E *e, int id);
int freeof(E *e, E *x);           /* 1 if x does not occur in e */
int freeof_sym(E *e, int id);
E *nabla_vars(E *f);              /* cmds.c: [x], [x,y] or [x,y,z] for ∇f */
int is_const_expr(E *e);          /* no free symbols except e, pi, i,
                                     euler_gamma; no lists, commands or
                                     unknown functions */
size_t expr_size(E *e);           /* number of nodes (capped) */
int has_float(E *e);
void esort(E **a, uint32_t n, int (*cmp)(E *, E *));
uint32_t collect_syms(E *e, E **out, uint32_t cap);
E *default_var(E *e);
E *rebuild(E *e, E **k);
E *factor_list(E *e, E **content);
E *factor_list_in(E *e, E *x, E **content);
E *mat_kernel(E *e);
E *mat_lu(E *e);
E *mat_qr(E *e);
E *mat_cholesky(E *e);
E *mat_mul_lists(E *a, E *b);
E *mat_pow_list(E *m, E *k);
int eval_complex_at(E *e, int var, double xr, double xi, double *re, double *im);
int num_fn_double(int id, int n, const double *x, double *out);
E *expr_copy_shallow(E *e);

/* ---------------------------------------------------- simplification --- *
 * simp.c: s_* build automatically simplified (canonical) expressions from
 * simplified arguments. */
E *simp(E *e);                    /* auto-simplify (bottom-up, cached by F_SIMP) */
E *s_add(uint32_t n, E **a);
E *s_mul(uint32_t n, E **a);
E *s_add2(E *a, E *b);
E *s_sub(E *a, E *b);
E *s_mul2(E *a, E *b);
E *s_div(E *a, E *b);
E *s_neg(E *a);
E *s_pow(E *a, E *b);
E *s_powi(E *a, long k);
E *s_sqrt(E *a);
E *s_fn(int id, uint32_t n, E **a);
E *s_fn1(int id, E *a);
E *s_fn2(int id, E *a, E *b);
E *s_exp(E *a);
E *s_ln(E *a);
E *s_inv(E *a);
E *s_list(uint32_t n, E **a);
void split_coef(E *t, E **coef, E **rest);    /* t = coef*rest, coef number */
E *subst(E *e, E *x, E *v);       /* replace x by v and re-simplify */
E *subst_many(E *e, uint32_t n, E **xs, E **vs);
int is_negative_term(E *e);       /* syntactically negative (leading coef < 0) */
int is_int_val(E *e, long v);
int is_zero(E *e);
int is_one(E *e);
int is_poslit(E *e);              /* known positive real (constants, assumptions) */
int sign_of(E *e);
/* assume.c: relational assumptions (facts) and intervals */
int facts_sign(E *e);
int interval_of(E *u, double *lo, double *hi, int *lo_st, int *hi_st);
int assume_rel(E *c);
void facts_forget(int id);
E *facts_about(int id);
E *assume_decide(E *e);                /* +1 / -1 known sign of a real expression, 0 unknown */
int real_odd_root(const E *e, int *podd); /* real mode and e = p/q, q odd (real root of b < 0) */
int is_int_assumed(E *e);
E *s_frac_pi(E *q);               /* q*pi */

/* ----------------------------------------------------- polynomial etc --- *
 * Expression-level entry points of poly.c / factor.c / upoly.c. */
E *expand(E *e);
E *expand_power_int(E *base, long k);
E *rat_normal(E *e);              /* rational normal form (cancelled num/den) */
E *together(E *e);
E *factor_expr(E *e);
int facts_any(void); /* assume.c: an assumption is stored */
E *sqrfree_expr(E *e);
E *partfrac(E *e, E *x);
E *collect_expr(E *e, E *x);
E *numer_of(E *e);
E *denom_of(E *e);
void numden(E *e, E **num, E **den);
int poly_degree_in(E *e, E *x, long *deg);    /* 1 if e is polynomial in x */
E *poly_coeff(E *e, E *x, long k);
E *poly_coeff_list(E *e, E *x, long *deg);   /* LIST descending order */
E *poly_quo_rem(E *a, E *b, E *x, E **rem);
E *poly_gcd_expr(E *a, E *b);
E *poly_lcm_expr(E *a, E *b);
E *resultant_expr(E *a, E *b, E *x);
E *simplify_full(E *e);
E *simplify_auto(E *e);           /* cheap pass of autosimplify (simplify.c) */
int name_eq_ci(const char *a, const char *b, size_t n); /* ASCII case-insensitive (parse.c) */
int ext_name_ci(const char *s, size_t n);               /* extension command, any case (xcmds.c) */

/* --------------------------------------------------------- calculus --- *
 * diff.c, integ.c, series.c, sum.c, solve.c, gbasis.c (a few prototypes are
 * repeated from the section above; the duplicates are harmless). */
E *diff(E *e, E *x);
E *diffn(E *e, E *x, long n);
E *integrate(E *e, E *x);                       /* NULL if not found */
E *integrate_def(E *e, E *x, E *a, E *b);
E *limit_expr(E *e, E *x, E *a, int dir);       /* dir: 0 two-sided, 1 right, -1 left */
E *series_expr(E *e, E *x, E *a, long n);       /* truncated Taylor/Laurent/Puiseux */
E *sum_expr(E *e, E *k, E *a, E *b);
E *product_expr(E *e, E *k, E *a, E *b);
E *solve_expr(E *eqs, E *vars, int complex_mode);
E *roots_expr(E *p, E *x);
E *proot_expr(E *p, E *x);
E *partfrac(E *e, E *x);
E *simplify_full(E *e);
E *groebner(E *polys, E *vars, int order);
E *bernoulli(long n);
E *zeta_exact(long n);
E *resultant_expr(E *a, E *b, E *x);
E *fsolve_expr(E *eq, E *x, E *guess);

/* ---------------------------------------------------- linear algebra --- *
 * linalg.c; matrices are E_LIST rows of equal length. */
int is_matrix(E *e, uint32_t *rows, uint32_t *cols);
int is_vector(E *e);
E *mat_mul(E *a, E *b);
E *mat_add(E *a, E *b, int sub);
E *mat_det(E *m);
E *mat_inv(E *m);
E *mat_pow(E *m, long k);
E *mat_transpose(E *m);
E *mat_rref(E *m, long *rank);
E *mat_identity(long n);
E *mat_charpoly(E *m, E *x);
E *linsolve(E *eqs, E *vars);

/* ---------------------------------------------------------- numeric --- *
 * numeric.c: double / complex-double evaluation and float formatting. */
E *evalf(E *e);                   /* approximate numeric value (floats) */
int to_double(E *e, double *out); /* 1 on success (closed numeric expr) */
int eval_double_at(E *e, int var, double x, double *out);
int eval_complex(E *e, double *re, double *im);
void fmt_double(sbuf *b, double x, int digits);
void fmt_float_node(sbuf *b, E *e, int digits);
E *evalf_digits(E *e, int digits);

/* ---------------------------------------------------- parse / print --- *
 * parse.c: text -> raw (unevaluated) expression, throws CAS_ERR_SYNTAX.
 * print.c: linear text that parse() reads back.
 * eval.c / cmds.c: evaluation (variable substitution, user functions,
 * commands). */
E *parse(const char *s);
void print_expr(sbuf *b, E *e);
E *eval(E *e);
E *eval_cmd(int id, uint32_t n, E **args);

/* ---- misc helpers shared between modules ----
 * ISYM / IFN: "is the symbol id" / "is a call of function id", as macros for
 * hot paths; NUM(v) is a small integer node. */
#define ISYM(e, id) ((e)->t == E_SYM && (e)->s == (id))
#define IFN(e, id) ((e)->t == E_FN && (e)->s == (id))
#define NUM(v) mk_int(v)

#endif
