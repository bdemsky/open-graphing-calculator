/*
 * casx.h -- the CAS extension interface (docs/QSPI_CONTENT.md 7 and 12.6).
 *
 * An extension is a set of name-dispatched commands whose code lives outside
 * the core CAS: on the calculator in a code module that runs in place from
 * the QSPI (modules/casx, app/modx.h), in the core tier and in every host
 * build (CAS library, unit tests, REPL, Wester and suite runners, the
 * simulators and the web build) linked into the binary.  Either way the
 * extension reaches the core only through the versioned table casx_api_t
 * below and registers its commands by name with api->reg; the core never
 * calls extension code except through that registry.
 *
 * Extension code (cas/ext/<name>/) is written against cas/ext/casx_mod.h,
 * which maps the core functions it uses onto the table: every function of
 * an extension takes `const casx_api_t *X` and the names (s_add2, salloc,
 * K, ...) expand to X->... .  The same source therefore runs unchanged as a
 * position-independent XIP image (no static data, no GOT, no direct call
 * into the core: the image link fails on any of them) and linked into a
 * host binary.
 *
 * Commands.  A table of casx_cmd_t (names as char arrays: an XIP image may
 * not hold pointers in constant data) and one handler per registration;
 * the handler gets the command's index in the table and the evaluated
 * arguments (count already checked against lo..hi) and returns the value,
 * or NULL to leave the call unevaluated.  It may throw (cas_throw) like any
 * core code.
 *
 * Lookup (xcmds.c ext_cmd -> casx_dispatch): core commands first, then the
 * registered tables (the first entry with the name and an argument count in
 * lo..hi: two tables may share a name with disjoint counts); a name of the
 * known-extension list (casx.c) that is not
 * registered asks the application's loader (cas_ext_set_loader: binds the
 * module) once, and otherwise fails with "NAME needs the advanced CAS
 * module (Memory > Modules)" instead of staying an unknown function.
 *
 * Versioning: CASX_ABI changes when an entry changes meaning or signature
 * or the layout of anything an extension reads (E, cas_ctx up to the
 * constants, UP); new entries are appended (size grows) and an extension
 * refuses a table smaller than the one it was built for.
 */
#ifndef CASX_H
#define CASX_H
#include "cas_int.h"
#include "upoly.h"
#include "poly.h"

#define CASX_ABI 1
#define CASX_MAX_REG 12 /* registrations (extension command tables) at the same time */

typedef struct {
    char name[14];
    uint8_t lo, hi; /* argument count */
} casx_cmd_t;

struct casx_api;
typedef E *(*casx_fn)(const struct casx_api *X, int op, uint32_t n, E **a);

typedef struct casx_api {
    uint16_t abi, size; /* CASX_ABI, sizeof(casx_api_t) */
    cas_ctx **ctx;      /* &K: the current context */
    /* register the commands tab[0..n) of extension `module`; 0, or -1 if full */
    int (*reg)(const char *module, const casx_cmd_t *tab, int n, casx_fn fn);
    /* memory, errors, interrupts (cas_int.h) */
    void *(*salloc)(size_t n);
    void (*cas_throw)(cas_status st, const char *msg); /* never returns */
    void (*cas_poll_step)(void);
    /* construction (raw and auto-simplified) */
    E *(*mk_int)(int64_t v);
    E *(*mk_frac)(int64_t n, int64_t d);
    E *(*mk_sym)(int id);
    E *(*mk_sym_name)(const char *s);
    E *(*s_list)(uint32_t n, E **a);
    E *(*s_add)(uint32_t n, E **a);
    E *(*s_add2)(E *a, E *b);
    E *(*s_sub)(E *a, E *b);
    E *(*s_mul)(uint32_t n, E **a);
    E *(*s_mul2)(E *a, E *b);
    E *(*s_div)(E *a, E *b);
    E *(*s_inv)(E *a);
    E *(*s_neg)(E *a);
    E *(*s_pow)(E *a, E *b);
    E *(*s_powi)(E *a, long k);
    E *(*s_sqrt)(E *a);
    E *(*s_exp)(E *a);
    E *(*s_fn1)(int id, E *a);
    E *(*s_fn2)(int id, E *a, E *b);
    /* numbers */
    int (*num_is_small_int)(E *a, long *v);
    E *(*int_mul_si)(E *a, int64_t b);
    E *(*int_add)(E *a, E *b);
    E *(*int_sub)(E *a, E *b);
    E *(*int_neg)(E *a);
    int (*int_isprime)(E *n);
    E *(*int_factor)(E *n);
    E *(*int_mod)(E *a, E *b);
    E *(*num_floor)(E *a);
    E *(*num_sub)(E *a, E *b);
    int (*num_is_zero)(E *a);
    E *(*num_inv)(E *a);
    int (*to_double)(E *e, double *out);
    E *(*evalf_digits)(E *e, int digits);
    E *(*bernoulli)(long n);
    /* expressions */
    int (*is_const_expr)(E *e);
    int (*expr_eq)(E *a, E *b);
    int (*is_zero)(E *e);
    int (*freeof)(E *e, E *x);
    int (*has_float)(E *e);
    E *(*subst)(E *e, E *x, E *v);
    E *(*subst_many)(E *e, uint32_t n, E **xs, E **vs);
    E *(*diff)(E *e, E *x);
    E *(*expand)(E *e);
    E *(*rat_normal)(E *e);
    E *(*factor_expr)(E *e);
    E *(*default_var)(E *e);
    E *(*eval_cmd)(int id, uint32_t n, E **args);
    /* polynomials */
    int (*poly_degree_in)(E *e, E *x, long *deg);
    E *(*poly_coeff)(E *e, E *x, long k);
    int (*up_from)(E *e, E *x, UP *out);
    /* linear algebra and matrix functions (linalg.c, matfn.c) */
    int (*is_matrix)(E *e, uint32_t *rows, uint32_t *cols);
    E *(*mat_mul_lists)(E *a, E *b);
    E *(*mat_identity)(long n);
    E *(*mat_rref)(E *m, long *rank);
    E *(*mat_kernel)(E *e);
    E *(*mat_charpoly)(E *m, E *x);
    E *(*mat_fun)(E *A, E *fx, E *X);
    E *(*mat_rpow)(E *A, E *e);
    int (*roots_mult)(E *p, E *L, uint32_t n, E **lam, long *mult);
    E *(*lin_fit)(E *sol, uint32_t ne, E **eqs, uint32_t nc, E **cs);
    /* ---- appended (size 2) ---- */
    void (*stack_check)(void); /* throws when the C stack limit is reached or on an interrupt */
    /* rewrite hooks: fn(X, e) returns a new expression or NULL (no change) */
    int (*reg_hook)(int phase, E *(*fn)(const struct casx_api *X, E *e));
    E *(*rebuild)(E *e, E **k);
    void (*numden)(E *e, E **num, E **den);
    E *(*factor_list_in)(E *e, E *x, E **content);
    int (*sign_of)(E *e);
    int (*eval_complex)(E *e, double *re, double *im);
    double (*sqrt)(double v);
    double (*pow)(double a, double b);
    E *(*solve_expr)(E *eqs, E *vars, int complex_mode);
    /* ---- appended (CAS round 3: cas/ext/sci) ---- */
    E *(*mk_fn)(int id, uint32_t n, E **a); /* raw node (no simplification; set F_SIMP for an inert call) */
    E *(*mk_flt)(double v);
    const char *(*sym_name)(int id);
    E *(*try_cmd)(int id, uint32_t n, E **a); /* eval_cmd, NULL if it throws (an interrupt is passed on) */
    /* algorithm slots (CASX_FN_*): the core calls fn instead of / before its own method */
    int (*reg_fn)(int slot, void *fn);
    /* polynomials (poly.h: the layout of P is part of the ABI) and integers */
    P *(*p_mk)(int v, uint32_t n);
    P *(*p_norm)(P *p);
    P *(*p_const)(E *c);
    P *(*p_divexact)(P *a, P *b);
    E *(*p_lnum)(P *p);
    E *(*int_mul)(E *a, E *b);
    E *(*int_gcd)(E *a, E *b);
    int (*num_cmp)(E *a, E *b);
    int (*num_sgn)(E *a);
    P *(*p_var)(int v);
    P *(*p_content)(P *p);
    P *(*p_eval)(P *p, int v, E *val);
    uint32_t (*p_factor)(P *f, P **facs, uint32_t *mult, uint32_t cap);
    UP (*up_mul)(UP a, UP b);
    UP (*up_add)(UP a, UP b, int sub);
    void (*up_divrem)(UP a, UP b, UP *q, UP *r);
    UP (*up_egcd)(UP a, UP b, UP *s, UP *t);
    UP (*up_scale)(UP a, E *c);
    P *(*p_intprim)(P *p, E **c);
    /* ---- appended (CAS round 5) ---- */
    /* fn(X, a) with errors caught: its result, or NULL when it threw (an interrupt is passed on) */
    E *(*protect)(E *(*fn)(const struct casx_api *X, void **a), void **a);
    /* ---- appended ---- */
    /* protect(fn, a) that runs undo(X, a) on every way out (an interrupt included, passed on after
     * the undo): for state set only for the call, such as a temporary assumption */
    E *(*protect_undo)(E *(*fn)(const struct casx_api *X, void **a), void (*undo)(const struct casx_api *X, void **a),
                       void **a);
} casx_api_t;

/* algorithm slots: CASX_FN_PGCD  P *fn(X, a, b): gcd of two primitive integer polynomials
 * (primitive, positive leading numeric coefficient), or NULL to let the core do it */
enum { CASX_FN_PGCD, CASX_FN_PFACTOR, CASX_FN_SUM, CASX_FN_PRODUCT, CASX_FN_LIMIT, CASX_FN_INTEGRATE, CASX_FN_EIGENVALS, CASX_FN_DIFF, CASX_FN_CEVAL, CASX_FN_FSIMP, CASX_FN_SOLVE, CASX_FN_NUMLA, CASX_FN_DSOLVE, CASX_FNS };
/* CASX_FN_SUM / CASX_FN_PRODUCT  E *fn(X, 4, [f, k, a, b]) and CASX_FN_LIMIT  E *fn(X, 4,
 * [f, x, point, dir]): tried when the core's sum, product or limit finds nothing; NULL: still
 * nothing (casx_eslot also returns NULL when the slot is empty or ran out of memory).
 * CASX_FN_INTEGRATE  E *fn(X, 2, [f, x]): an antiderivative when the core's integrate finds none
 * (integ.c, nested at most 2 deep), NULL if none.
 * CASX_FN_EIGENVALS  E *fn(X, 1, [A]): the eigenvalues of A (cmds.c eigenvals), NULL: the core's.
 * Functions defined by the module (polylog, besselj, ...; names that are not builtins):
 * CASX_FN_DIFF   E *fn(X, 2, [f(args), x]): d/dx f(args), NULL: the inert derivative;
 * CASX_FN_CEVAL  E *fn(X, 1, [f(numbers)]): its complex value as a number, NULL: not numeric;
 * CASX_FN_FSIMP  E *fn(X, 1, [f(a, b)]): f(a, b) simplified (special values), NULL: kept
 * CASX_FN_SOLVE  E *fn(X, 2 or 4, [eq, x (, a, b)]): the solve command (cmds.c) before the core's
 * solve_expr: general solutions of periodic equations, and the solutions in [a, b]; NULL: the core's
 * CASX_FN_NUMLA  E *fn(X, 2, [A, k]): numeric linear algebra (cmds.c) for a matrix with floats (or,
 * for eigenvects, numeric eigenvalues): k = 0 eigenvals, 1 eigenvects, 2 rank; NULL: the core's
 * CASX_FN_DSOLVE E *fn(X, n, [F, x, m, mode, ...]) (ode.c, F(x, _y0.._ym) = 0): mode 0 a general
 * solution in c1, c2, ... when the core's methods fail; mode 1 [.., x0, order] a power series
 * solution about x0; mode 2 [sol, x, m, 2] the general solution in a normalised form; NULL: none */
typedef E *(*casx_efn)(const struct casx_api *X, uint32_t n, E **a);
E *casx_eslot(int slot, uint32_t n, E **a);
/* The core tier without cas/ext/sci and cas/ext/alg (Kconfig CASX_CORE_SCI off) has nothing in
 * the slots: the core's calls into them compile away there */
#if defined(CONFIG_CONTENT_INTERNAL) && !defined(CONFIG_CASX_CORE_SCI)
#define CASX_SLOTS 0
#else
#define CASX_SLOTS 1
#endif
typedef P *(*casx_pgcd_fn)(const struct casx_api *X, P *a, P *b);
P *casx_pgcd(P *a, P *b); /* casx.c: through the slot, NULL if none or it declined */
/* CASX_FN_PFACTOR  uint32_t fn(X, f, facs, cap): the irreducible factors of a squarefree primitive
 * multivariate f (nonlinear in its main variable), or 0 to let the core do it */
typedef uint32_t (*casx_pfactor_fn)(const struct casx_api *X, P *f, P **facs, uint32_t cap);
uint32_t casx_pfactor(P *f, P **facs, uint32_t cap);

/* hook phases (casx_hook): simplify_full applies the PRE rewrites to its input
 * (an identity that is simpler: the start of everything else) and ranks the
 * CAND results with its other candidates by cost */
enum { CASX_HOOK_SIMPLIFY_PRE, CASX_HOOK_SIMPLIFY_CAND, CASX_HOOKS };
E *casx_hook(int phase, E *e); /* casx.c: the hooks' result, NULL if none applied */

extern const casx_api_t casx_api;

/* core side (casx.c) */
E *casx_dispatch(const char *nm, uint32_t n, E **a);
int casx_known(const char *nm); /* a command of an extension (installed or not) */
const char *casx_unlisted(void); /* tests: a registered command missing from the known list, or NULL */

/* matfn.c, for the table */
E *mat_fun(E *A, E *fx, E *X);
E *mat_rpow(E *A, E *e);
int roots_mult(E *p, E *L, uint32_t n, E **lam, long *mult);
E *lin_fit(E *sol, uint32_t ne, E **eqs, uint32_t nc, E **cs);

/* extensions linked into this binary (host builds, core tier) */
int casx_adv_init(const casx_api_t *X); /* cas/ext/adv */
int casx_rad_register(const casx_api_t *X); /* cas/ext/adv/radsimp.c */

#endif
