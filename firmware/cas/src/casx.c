/*
 * casx.c -- the core side of the CAS extension interface (casx.h): the API
 * table, the registry of extension command tables, name dispatch, and the
 * "needs the advanced CAS module" fallback for known commands that are not
 * installed.
 */
#include "casx.h"
#include <math.h>
#include <string.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

/* Every command of the extensions this firmware knows (installed or not), so
 * that a missing module is reported as such instead of leaving an unknown
 * function call.  Keep in step with the tables in cas/ext/ (the unit test
 * checks that every registered name is listed). */
static const char known_names[] =
    /* cas/ext/adv/ntcmds.c */
    "factorial2\0pochhammer\0bernoulli\0fibonacci\0stirling1\0stirling2\0partition\0primroot\0"
    "contfrac\0quartile1\0quartile3\0mode\0stddevp\0variancep\0linreg\0factormod\0msolve\0union\0"
    "intersect\0setdiff\0gramschmidt\0"
    /* cas/ext/adv/matcmds.c */
    "expm\0sinm\0cosm\0sqrtm\0jordan\0minpoly\0rsolve\0wronskian\0wronskianmat\0"
    /* cas/ext/adv/lapl.c (laplace, ilaplace: the core forwards them) */
    "laplace\0ilaplace\0fourier\0"
    /* cas/ext/sci/veccalc.c (grad, divergence, curl: the core forwards coordinates) */
    "grad\0divergence\0curl\0laplacian\0potential\0vpotential\0lineint\0surfint\0flux\0"
    /* cas/ext/int/spfn.c */
    "chebyshevt\0chebyshevu\0legendre\0hermite\0laguerre\0legendrep\0digamma\0residue\0minimize\0maximize\0"
    "cartprod\0"
    /* cas/ext/sci/pde.c */
    "pdesolve\0pdenum\0gridval\0"
    /* cas/ext/applied (norm, rank, cond: the core forwards matrix 2-norms, tolerances) */
    "svd\0singvals\0pinv\0norm\0rank\0cond\0ifourier\0ztrans\0iztrans\0fft\0ifft\0linprog\0truthtable\0"
    "boolsimplify\0logicsimplify\0cnf\0dnf\0satisfiable\0implies\0iff\0spline\0"
    /* cas/ext/adv/ntcmds.c (statistics) */
    "covariance\0covariancep\0correlation\0ttest\0ttest2\0pairedttest\0tinterval\0"
    "ztest\0zinterval\0propinterval\0chi2gof\0chi2test\0anova\0"
    "exppdf\0expcdf\0invExp\0uniformpdf\0uniformcdf\0invUniform\0gammapdf\0gammacdf\0invGamma\0"
    "betapdf\0betacdf\0invBeta\0weibullpdf\0weibullcdf\0invWeibull\0lognormpdf\0lognormcdf\0invLognorm\0"
    "invBinom\0invPoisson\0"
    "polyreg\0expreg\0powerreg\0logreg\0multireg\0nlfit\0";

typedef struct {
    char module[16];
    const casx_cmd_t *tab;
    int n;
    casx_fn fn;
} reg_t;

static reg_t regs[CASX_MAX_REG];
static int nregs;
typedef E *(*hook_fn)(const casx_api_t *X, E *e);
static hook_fn hooks[CASX_HOOKS][4];
static void *fns[CASX_FNS];
static int nhooks[CASX_HOOKS], hooks_asked;
static int busy;
static int (*loader)(const char *module);
#if defined(CASX_BUILTIN) || defined(CONFIG_CONTENT_INTERNAL)
#define BUILTIN 1
static int builtin_done;
#else
#define BUILTIN 0
#endif

static int reg(const char *module, const casx_cmd_t *tab, int n, casx_fn fn) {
    for (int i = 0; i < nregs; i++)
        if (!strcmp(regs[i].module, module) && regs[i].tab == tab) { /* again (rebind): replace */
            regs[i].n = n, regs[i].fn = fn;
            return 0;
        }
    if (nregs == CASX_MAX_REG) return -1;
    reg_t *r = &regs[nregs++];
    size_t l = strlen(module);
    if (l >= sizeof r->module) l = sizeof r->module - 1;
    memcpy(r->module, module, l);
    r->module[l] = 0;
    r->tab = tab, r->n = n, r->fn = fn;
    return 0;
}

static void ensure_builtin(void) {
#if BUILTIN
    if (!builtin_done) {
        builtin_done = 1;
        casx_adv_init(&casx_api);
    }
#endif
}

void cas_ext_set_loader(int (*load)(const char *module)) { loader = load; }
const void *cas_ext_api(void) { return &casx_api; }
int cas_ext_busy(void) { return busy > 0; }

/* The extension code is going away (module store change): forget every
 * registration; extensions linked into the binary come back at the next use. */
void cas_ext_unload(void) {
    nregs = 0;
    for (int p = 0; p < CASX_HOOKS; p++) nhooks[p] = 0;
    for (int p = 0; p < CASX_FNS; p++) fns[p] = NULL;
    hooks_asked = 0;
    busy = 0;
#if BUILTIN
    builtin_done = 0;
#endif
}

int casx_known(const char *nm) {
    for (const char *p = known_names; *p; p += strlen(p) + 1)
        if (!strcmp(p, nm)) return 1;
    return 0;
}

const char *casx_unlisted(void) {
    ensure_builtin();
    for (int i = 0; i < nregs; i++)
        for (int j = 0; j < regs[i].n; j++)
            if (!casx_known(regs[i].tab[j].name)) return regs[i].tab[j].name;
    return NULL;
}

/* 1: the first entry named nm that takes n arguments; 2: only entries with other counts (*ri, *op:
 * the first of them); 0: none */
static int find(const char *nm, uint32_t n, int *ri, int *op) {
    int r = 0;
    for (int i = 0; i < nregs; i++)
        for (int j = 0; j < regs[i].n; j++) {
            const casx_cmd_t *c = &regs[i].tab[j];
            if (c->name[0] == nm[0] && !strncmp(c->name, nm, sizeof c->name) && strlen(nm) < sizeof c->name) {
                if (!r || (n >= c->lo && n <= c->hi)) *ri = i, *op = j;
                if (n >= c->lo && n <= c->hi) return 1;
                r = 2;
            }
        }
    return r;
}

/* Run handler r for command op; `busy` tells the application that extension
 * code is on the stack (no QSPI writes then), also when it throws. */
static E *call(reg_t *r, int op, uint32_t n, E **a) {
    tryf tf;
    busy++;
    try_enter(&tf);
    if (setjmp(tf.jb)) {
        try_leave(&tf);
        busy--;
        rethrow();
    }
    E *v = r->fn(&casx_api, op, n, a);
    try_leave(&tf);
    busy--;
    return v;
}

/* xcmds.c ext_cmd: an extension command by name; NULL = not one, or
 * unevaluated. */
E *casx_dispatch(const char *nm, uint32_t n, E **a) {
    int ri, op, f;
    ensure_builtin();
    if (!(f = find(nm, n, &ri, &op))) {
        if (!casx_known(nm) || K->restoring) return NULL;
        if (!loader || loader("casx") || !(f = find(nm, n, &ri, &op))) {
            char msg[80];
            size_t l = strlen(nm);
            if (l > 20) l = 20;
            memcpy(msg, nm, l);
            strcpy(msg + l, " needs the advanced CAS module (Memory > Modules)");
            cas_throw(CAS_ERR_UNSUPPORTED, msg);
        }
    }
    if (f == 2) cas_throw(CAS_ERR_DOMAIN, "wrong number of arguments");
    return call(&regs[ri], op, n, a);
}

static int reg_hook(int phase, hook_fn fn) {
    if (phase < 0 || phase >= CASX_HOOKS) return -1;
    for (int i = 0; i < nhooks[phase]; i++)
        if (hooks[phase][i] == fn) return 0;
    if (nhooks[phase] == 4) return -1;
    hooks[phase][nhooks[phase]++] = fn;
    return 0;
}

/* Apply the hooks of `phase` to e in turn (each one gets the previous result).
 * With none registered the loader is asked once (until the next unload), so
 * an installed module comes in on the first simplify. */
E *casx_hook(int phase, E *e) {
    ensure_builtin();
    if (!nregs && !hooks_asked && loader && !K->restoring) {
        hooks_asked = 1;
        loader("casx");
    }
    E *r = e;
    for (int i = 0; i < nhooks[phase]; i++) {
        tryf tf;
        busy++;
        try_enter(&tf);
        if (setjmp(tf.jb)) {
            try_leave(&tf);
            busy--;
            rethrow();
        }
        E *v = hooks[phase][i](&casx_api, r);
        try_leave(&tf);
        busy--;
        if (v) r = v;
    }
    return r == e ? NULL : r;
}

/* eval_cmd that returns NULL instead of throwing (interrupts still unwind) */
static E *try_cmd(int id, uint32_t n, E **a) {
    tryf tf;
    E *v = NULL;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        v = eval_cmd(id, n, a);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
    }
    return v;
}

static E *protect(E *(*fn)(const casx_api_t *X, void **a), void **a) {
    tryf tf;
    E *v = NULL;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        v = fn(&casx_api, a);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
    }
    return v;
}

static E *protect_undo(E *(*fn)(const casx_api_t *X, void **a), void (*undo)(const casx_api_t *X, void **a),
                       void **a) {
    tryf tf;
    E *v = NULL;
    try_enter(&tf);
    if (!setjmp(tf.jb)) {
        v = fn(&casx_api, a);
        try_leave(&tf);
    } else {
        try_leave(&tf);
        undo(&casx_api, a);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        return NULL;
    }
    undo(&casx_api, a);
    return v;
}

static int reg_fn(int slot, void *fn) {
    if (!CASX_SLOTS || slot < 0 || slot >= CASX_FNS) return -1;
    fns[slot] = fn;
    return 0;
}

#if CASX_SLOTS

/* the slot's function, binding the module once like casx_hook */
static void *slot_fn(int slot) {
    ensure_builtin();
    if (!nregs && !hooks_asked && loader && !K->restoring) {
        hooks_asked = 1;
        loader("casx");
    }
    return fns[slot];
}

P *casx_pgcd(P *a, P *b) {
    casx_pgcd_fn f = (casx_pgcd_fn)slot_fn(CASX_FN_PGCD);
    if (!f) return NULL;
    tryf tf;
    char *volatile mark = K->s_top;
    busy++;
    try_enter(&tf);
    if (setjmp(tf.jb)) { /* out of memory: give the scratch back, the core's method may still do */
        try_leave(&tf);
        busy--;
        if (K->err != CAS_ERR_MEMORY) rethrow();
        K->s_top = mark;
        return NULL;
    }
    P *g = f(&casx_api, a, b);
    try_leave(&tf);
    busy--;
    return g;
}

uint32_t casx_pfactor(P *f, P **facs, uint32_t cap) {
    casx_pfactor_fn fn = (casx_pfactor_fn)slot_fn(CASX_FN_PFACTOR);
    if (!fn) return 0;
    tryf tf;
    char *volatile mark = K->s_top;
    busy++;
    try_enter(&tf);
    if (setjmp(tf.jb)) { /* out of memory: the core's method may still do */
        try_leave(&tf);
        busy--;
        if (K->err != CAS_ERR_MEMORY) rethrow();
        K->s_top = mark;
        return 0;
    }
    uint32_t n = fn(&casx_api, f, facs, cap);
    try_leave(&tf);
    busy--;
    return n;
}

E *casx_eslot(int slot, uint32_t n, E **a) {
    casx_efn fn = (casx_efn)slot_fn(slot);
    if (!fn) return NULL;
    tryf tf;
    char *volatile mark = K->s_top;
    busy++;
    try_enter(&tf);
    if (setjmp(tf.jb)) {
        try_leave(&tf);
        busy--;
        if (K->err != CAS_ERR_MEMORY) rethrow();
        K->s_top = mark;
        return NULL;
    }
    E *r = fn(&casx_api, n, a);
    try_leave(&tf);
    busy--;
    return r;
}
#else
E *casx_eslot(int slot, uint32_t n, E **a) { return NULL; }
P *casx_pgcd(P *a, P *b) { return NULL; }
uint32_t casx_pfactor(P *f, P **facs, uint32_t cap) { return 0; }
#endif

static double c_sqrt(double v) { return sqrt(v); }
static double c_pow(double a, double b) { return pow(a, b); }

const casx_api_t casx_api = {
    CASX_ABI, sizeof(casx_api_t), &K, reg,
    salloc, cas_throw, cas_poll_step,
    mk_int, mk_frac, mk_sym, mk_sym_name, s_list, s_add, s_add2, s_sub, s_mul, s_mul2, s_div, s_inv, s_neg,
    s_pow, s_powi, s_sqrt, s_exp, s_fn1, s_fn2,
    num_is_small_int, int_mul_si, int_add, int_sub, int_neg, int_isprime, int_factor, int_mod, num_floor, num_sub,
    num_is_zero, num_inv, to_double, evalf_digits, bernoulli,
    is_const_expr, expr_eq, is_zero, freeof, has_float, subst, subst_many, diff, expand, rat_normal, factor_expr,
    default_var, eval_cmd,
    poly_degree_in, poly_coeff, up_from,
    is_matrix, mat_mul_lists, mat_identity, mat_rref, mat_kernel, mat_charpoly, mat_fun, mat_rpow, roots_mult, lin_fit,
    stack_check, reg_hook, rebuild, numden, factor_list_in, sign_of, eval_complex, c_sqrt, c_pow, solve_expr,
    mk_fn, mk_flt, sym_name, try_cmd,
    reg_fn, p_mk, p_norm, p_const, p_divexact, p_lnum, int_mul, int_gcd, num_cmp, num_sgn,
    p_var, p_content, p_eval, p_factor, up_mul, up_add, up_divrem, up_egcd, up_scale, p_intprim,
    protect, protect_undo,
};
