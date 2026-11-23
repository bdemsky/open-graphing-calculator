/*
 * unc.c -- measurement uncertainty (first-order propagation with correlations).
 *
 * Input: 9.81±0.02 (parse.c reads '±' as a sum-level operator, so
 * x+1±0.1 is (x+1)±0.1) or pm(9.81, 0.02).  Results print as value±unc
 * with the uncertainty rounded to one significant figure, two when its
 * leading digit is 1, and the value rounded to the same decimal place
 * (J. R. Taylor, "An Introduction to Error Analysis", 2nd ed., sec. 2.2).
 *
 * Representation (linear error model, as in affine arithmetic): evaluating
 * pm(v, s) creates a fresh *source* symbol _uncN (one per measurement) and
 * returns the ordinary expression  v + s*_uncN, where each source stands for
 * an independent error of standard deviation 1.  Everything else in the CAS
 * then works unchanged: x:=9.81±0.02 stores the expression, x-x cancels to 0
 * exactly (the correlation is the shared symbol), units multiply through
 * (2.0±0.1)_m = 2.0_m + 0.1_m*_unc1, and functions apply symbolically,
 * sin(1±0.1) = sin(1 + 0.1*_unc1).
 * After every top-level evaluation unc_post() (ext_post in xcmds.c, before
 * the unit post-processing) linearises the result in the sources: with
 * r0 = r(all sources = 0) and c_i = dr/d_unc_i at 0 (diff.c),
 *     r  ~  r0 + sum_i c_i * _unc_i ,
 * which is first-order (linear) propagation; the printed uncertainty is the
 * root sum of squares  sigma = sqrt(sum_i c_i^2)  (Gaussian propagation of
 * independent errors, GUM JCGM 100:2008 eq. 10).  Because sources are kept,
 * correlated inputs combine correctly: x*x has sigma 2|x|s, x-x has 0.
 * Stored variables keep the unlinearised value (chain rule: linearising later
 * gives the same first-order result).
 *
 * Why in the CAS and not in the app: the correlation of two quantities is
 * the identity of their sources, which must survive variables, ans, user
 * functions, lists and unit conversions -- all CAS data.  The app only sees
 * printed text, where 9.81±0.02 would become a new, uncorrelated measurement
 * every time it is read back.  Units needed no change (units.c only scales
 * the value part, which may be any expression).
 *
 * Commands (name-dispatched from xcmds.c): pm(v, s), nominal(x) (r0),
 * uncert(x) (sigma, exact when the inputs are exact), ucov(x, y)
 * (sum_i c_i d_i, the covariance of two results).
 *
 * Printing (print.c calls unc_print for sums and pm(...) calls):
 *   numeric value and coefficients of one unit:  9.81±0.02, (4.0±0.3)_m/s,
 *       6.0221e23±1.2e20 for magnitudes outside 1e-4 .. 1e6;
 *   symbolic value or coefficients:  value±sqrt(sum c_i^2) unrounded;
 *   anything else (a source inside a function that could not be
 *       linearised, e.g. a complex result): the raw form 1+0.1*_unc1.
 * The text reads back (as new, independent measurements).  cas_save
 * (session.c) sets unc_raw so that saved values keep their sources
 * (9.81+0.02*_unc1) and restored variables stay correlated.
 * x-x and similar results whose inputs had uncertainty but whose value has
 * none are returned as the node pm(0, 0) so that they print as 0±0; it
 * evaluates to 0 when used again.
 *
 * State: the counter for new source names (statics, single-threaded like
 * the rest of the library) and the raw-print flag.  Source symbols are
 * plain user symbols recognised by name, without assumption flags, so the
 * symbol GC drops the ones no stored value refers to.
 */
#include "cas_int.h"
#include <math.h>
#include <string.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

void units_split(E *t, E **val, E **un); /* units.c */

static int unc_raw;            /* printing for cas_save: no ± forms */
static uint32_t unc_next = 1;  /* next source number to try */

/* session.c: print values with their source symbols while saving. */
void unc_set_raw(int on) { unc_raw = on; }

/* Is e a source symbol (_unc followed by a digit 1..9, then digits)? */
static int is_src(E *e) {
    if (e->t != E_SYM || e->s < S__COUNT) return 0;
    const char *n = sym_name(e->s);
    return !memcmp(n, "_unc", 4) && n[4] >= '1' && n[4] <= '9';
}

/* Does e contain a source?  Walk limited by *bud nodes (DAGs). */
static int has_src_b(E *e, int *bud) {
    stack_check();
    if (--*bud < 0) return 0;
    if (e->t == E_SYM) return is_src(e);
    if (e->t < E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_src_b(EK(e)[i], bud)) return 1;
    return 0;
}
static int has_src(E *e) {
    int bud = 20000;
    return has_src_b(e, &bud);
}

/* Any source symbol interned at all? (cheap pre-check for unc_post) */
static int src_in_use(void) {
    for (uint32_t i = S__COUNT; i < K->nsyms; i++) {
        const char *n = K->names + K->syms[i].name;
        if (n[0] == '_' && n[1] == 'u' && !memcmp(n, "_unc", 4)) return 1;
    }
    return 0;
}

/* A fresh source symbol: the first _uncN (N >= unc_next) not interned. */
static E *new_source(void) {
    for (;;) {
        char nm[16] = "_unc", d[12];
        uint32_t k = unc_next++, nd = 0;
        if (!unc_next) unc_next = 1;
        do d[nd++] = (char)('0' + k % 10), k /= 10;
        while (k);
        size_t len = 4;
        while (nd) nm[len++] = d[--nd];
        if (sym_find(nm, len) < 0) return mk_sym(sym_intern(nm, len));
    }
}

/* The symbol id of "pm" (interned on demand). */
static int pm_id(void) { return sym_intern("pm", 2); }

/* Only algebraic results are linearised: sums, products, powers, symbols
 * and the mathematical functions (not relations, commands or user calls). */
static int algebraic(E *e) {
    if (e->t == E_ADD || e->t == E_MUL || e->t == E_POW || e->t == E_SYM) return 1;
    return e->t == E_FN && e->s >= F_SQRT && e->s < S__FIRSTCMD;
}

/* Linear form r0 + sum c_i src_i of r (see the file header); lists and
 * matrices elementwise; non-algebraic values unchanged.  May throw (diff,
 * subst); unc_post catches. */
static E *linearize(E *r) {
    stack_check();
    if (r->t == E_LIST) {
        E **k = salloc(sizeof(E *) * (r->n + 1));
        for (uint32_t i = 0; i < r->n; i++) k[i] = linearize(EK(r)[i]);
        return s_list(r->n, k);
    }
    if (!algebraic(r) || !has_src(r)) return r;
    E *all[64], *src[64], *zero[64];
    uint32_t na = collect_syms(r, all, 64), n = 0;
    for (uint32_t i = 0; i < na; i++)
        if (is_src(all[i])) src[n] = all[i], zero[n++] = K->c0;
    if (!n) return r;
    E **t = salloc(sizeof(E *) * (n + 1));
    uint32_t k = 0;
    t[k++] = subst_many(r, n, src, zero);
    for (uint32_t i = 0; i < n; i++) {
        E *c = subst_many(diff(r, src[i]), n, src, zero);
        if (!is_zero(c)) t[k++] = s_mul2(c, src[i]);
    }
    return s_add(k, t);
}

/* linearize() with an error handler: a result that cannot be linearised
 * (e.g. a derivative that is undefined at the nominal value) is kept. */
static E *linearize_safe(E *r) {
    char *m = smark();
    tryf tf;
    try_enter(&tf);
    if (setjmp(tf.jb)) {
        try_leave(&tf);
        if (K->err == CAS_ERR_INTERRUPTED) rethrow();
        srelease(m);
        K->err = CAS_OK;
        K->errmsg[0] = 0;
        return r;
    }
    E *l = linearize(r);
    try_leave(&tf);
    return l;
}

/* Did the raw input refer to an uncertain quantity?  A ± / pm call, a
 * source, a variable whose value has a source, or ans with one. */
static int input_unc(E *e, int depth, int *bud) {
    stack_check();
    if (--*bud < 0 || depth > 3 /* variable hops */) return 0;
    if (e->t == E_SYM) {
        if (is_src(e)) return 1;
        E *v = e->s == S_ANS ? K->ans : K->syms[e->s].val;
        return v && input_unc(v, depth + 1, bud);
    }
    if (e->t < E_SYM || e->t == E_STR) return 0;
    if (e->t == E_FN && e->s == pm_id()) return 1;
    for (uint32_t i = 0; i < e->n; i++)
        if (input_unc(EK(e)[i], depth, bud)) return 1;
    return 0;
}

/* A number, or a number times units (no other symbols)?  *v gets the value,
 * *un the unit part (1 if none). */
static int qty(E *t, double *v, E **un) {
    E *val;
    units_split(t, &val, un);
    return to_double(val, v) && isfinite(*v);
}

/* Replace the display nodes pm(v, 0) that unc_post returned earlier (they
 * reach new results through ans or variables, which are substituted without
 * re-evaluation) by v.  Budgeted walk; unchanged subtrees are shared. */
static E *strip_pm(E *e, int *bud) {
    stack_check();
    if (--*bud < 0 || e->t <= E_SYM || e->t == E_STR) return e;
    if (e->t == E_FN && e->n == 2 && e->s == pm_id() && is_zero(K1(e))) return strip_pm(K0(e), bud);
    E **k = salloc(sizeof(E *) * (e->n + 1));
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) ch |= (k[i] = strip_pm(EK(e)[i], bud)) != EK(e)[i];
    return ch ? rebuild(e, k) : e;
}

/* Post-processing of a top-level result r of input e (ext_post): linearise;
 * return pm(r, 0) when the inputs were uncertain but the (numeric) result is
 * exact, so it prints 0±0 -- except for nominal / uncert / ucov, whose
 * results are plain numbers by design. */
E *unc_post(E *e, E *r) {
    if (!src_in_use()) return r;
    int sb = 20000;
    r = strip_pm(r, &sb);
    r = linearize_safe(r);
    if (has_src(r)) return r;
    E *f = IFN(e, F_ASSIGN) ? EK(e)[1] : e;
    if (f->t == E_FN) {
        const char *nm = sym_name(f->s);
        if ((f->s > S__FIRSTCMD && f->s < S__COUNT) || !strcmp(nm, "nominal") || !strcmp(nm, "uncert") ||
            !strcmp(nm, "ucov"))
            return r;
    }
    double v;
    E *un;
    int bud = 20000;
    if (r->t != E_LIST && qty(r, &v, &un) && input_unc(f, 0, &bud)) return mk_fn2(pm_id(), r, K->c0);
    return r;
}

/* Split a linear form into its value (terms without sources) and the
 * coefficients c[] of its sources (at most cap).  Returns the number of
 * sources, 0 if e has none, -1 if e is not linear in them (a source inside
 * a function or a product of two sources). */
static int split_linear(E *e, E **val, E **c, int cap) {
    uint32_t n = e->t == E_ADD ? e->n : 1;
    E **ts = e->t == E_ADD ? EK(e) : &e;
    E **vt = salloc(sizeof(E *) * (n + 1));
    uint32_t nv = 0;
    int nc = 0;
    for (uint32_t i = 0; i < n; i++) {
        E *t = ts[i];
        if (!has_src(t)) {
            vt[nv++] = t;
            continue;
        }
        if (nc >= cap) return -1;
        if (is_src(t)) {
            c[nc++] = K->c1;
            continue;
        }
        if (t->t != E_MUL) return -1;
        E **o = salloc(sizeof(E *) * t->n);
        uint32_t no = 0, ns = 0;
        for (uint32_t j = 0; j < t->n; j++) {
            if (is_src(EK(t)[j])) ns++;
            else if (has_src(EK(t)[j])) return -1;
            else o[no++] = EK(t)[j];
        }
        if (ns != 1) return -1;
        c[nc++] = s_mul(no, o);
    }
    *val = s_add(nv, vt);
    return nc;
}

/* ---------------------------------------------------------- formatting --- */
/* 10^k as a double (exact for |k| <= 22). */
static double p10(int k) {
    double r = 1;
    for (int i = 0; i < (k < 0 ? -k : k); i++) r *= 10;
    return k < 0 ? 1 / r : r;
}

/* Decimal digits of |n| into d (most significant first); returns the count. */
static int digits_of(long long n, char *d) {
    char t[24];
    int k = 0;
    unsigned long long u = n < 0 ? 0ull - (unsigned long long)n : (unsigned long long)n;
    do t[k++] = (char)('0' + u % 10), u /= 10;
    while (u);
    for (int i = 0; i < k; i++) d[i] = t[k - 1 - i];
    return k;
}

/* The number n * 10^q in fixed notation: 12300 (q = 2), 9.81 (q = -2),
 * 0.020 (n = 20, q = -3). */
static void put_fix(sbuf *b, long long n, int q) {
    char d[24];
    int k = digits_of(n, d);
    if (n < 0) sb_putc(b, '-');
    if (q >= 0) {
        sb_putn(b, d, (size_t)k);
        if (n)
            for (int i = 0; i < q; i++) sb_putc(b, '0');
        return;
    }
    int dp = -q;
    if (k <= dp) {
        sb_puts(b, "0.");
        for (int i = 0; i < dp - k; i++) sb_putc(b, '0');
        sb_putn(b, d, (size_t)k);
        return;
    }
    sb_putn(b, d, (size_t)(k - dp));
    sb_putc(b, '.');
    sb_putn(b, d + k - dp, (size_t)dp);
}

/* The number n * 10^q in scientific notation as the parser reads it:
 * 6.0221e23, 1.2e-7, 5e3. */
static void put_sci(sbuf *b, long long n, int q) {
    char d[24];
    int k = digits_of(n, d);
    if (n < 0) sb_putc(b, '-');
    sb_putc(b, d[0]);
    if (k > 1) {
        sb_putc(b, '.');
        sb_putn(b, d + 1, (size_t)(k - 1));
    }
    sb_putc(b, 'e');
    sb_int(b, q + k - 1);
}

/* Round x to an integer (halves away from zero). */
static long long iround(double x) { return (long long)(x < 0 ? -floor(-x + 0.5) : floor(x + 0.5)); }

/* "v±s" for numbers: s rounded to 1 significant figure (2 if its leading
 * digit is 1), v to the same decimal place; scientific when the magnitude
 * is outside [1e-4, 1e6) or the digits would not fit in 17.  Returns 0 if
 * the numbers are not finite. */
static int put_pm_num(sbuf *b, double v, double s) {
    if (!isfinite(v) || !isfinite(s)) return 0;
    if (s <= 0) return 0;
    int e = (int)floor(log10(s));
    if (s / p10(e) >= 10) e++;
    if (s / p10(e) < 1) e--;
    long long t = iround(s / p10(e - 1)); /* two leading digits, 10..100 */
    if (t >= 100) t = 10, e++;
    long long S;
    int q;
    if (t < 20) /* leading 1: two figures (0.014 -> 0.014, 0.0196 -> 0.020) */
        S = t, q = e - 1;
    else /* one figure; 0.096 rounds to 0.10 */
        S = iround(s / p10(e)), q = e;
    double vq = v / p10(q);
    if (fabs(vq) >= 1e17) return 0;
    long long V = iround(vq);
    double mag = fabs(v) > s ? fabs(v) : s;
    int me = mag > 0 ? (int)floor(log10(mag)) : 0;
    if (me >= 6 || me < -4) {
        put_sci(b, V, q);
        sb_puts(b, "\xc2\xb1");
        put_sci(b, S, q);
    } else {
        put_fix(b, V, q);
        sb_puts(b, "\xc2\xb1");
        put_fix(b, S, q);
    }
    return 1;
}

/* Unit tail "_m*kg/s^2" of a unit product (factors u or u^k); "*" + the
 * default printer when it has no positive power. */
static void put_units(sbuf *b, E *un) {
    uint32_t n = un->t == E_MUL ? un->n : 1, npos = 0;
    E **fs = un->t == E_MUL ? EK(un) : &un;
    for (uint32_t i = 0; i < n; i++) npos += fs[i]->t == E_SYM;
    for (uint32_t i = 0; i < n; i++) {
        long k = 0;
        if (fs[i]->t == E_POW) num_is_small_int(K1(fs[i]), &k);
        npos += k > 0;
    }
    if (!npos) {
        sb_putc(b, '*');
        print_expr(b, un);
        return;
    }
    int first = 1;
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t i = 0; i < n; i++) {
            E *f = fs[i], *u = f;
            long k = 1;
            if (f->t == E_POW) u = K0(f), num_is_small_int(K1(f), &k);
            if (u->t != E_SYM || (pass == 0) != (k > 0)) continue;
            sb_putc(b, pass ? '/' : first ? '_' : '*');
            sb_puts(b, sym_name(u->s) + 1);
            if (k < 0) k = -k;
            if (k != 1) {
                sb_putc(b, '^');
                sb_int(b, k);
            }
            first = 0;
        }
}

/* print.c hook: a sum with sources or a call pm(v, s) printed as v±s (see
 * the file header).  ctx is print.c's precedence (P_SUM = 2): the form is
 * parenthesised inside products and powers.  Returns 0 to let the default
 * printer run. */
int unc_print(sbuf *b, E *e, int ctx) {
    if (unc_raw) return 0;
    E *val, *c[64];
    int nc;
    if (e->t == E_FN) {
        if (e->n != 2 || e->s != pm_id()) return 0;
        val = K0(e), c[0] = K1(e), nc = 1;
    } else {
        if (e->t != E_ADD || !has_src(e)) return 0;
        nc = split_linear(e, &val, c, 64);
        if (nc <= 0) return 0;
    }
    char *mark = smark();
    /* numeric case: every part a number times the same units */
    double v, s2 = 0, ci;
    E *un = NULL, *u;
    int numeric = 1;
    if (is_zero(val)) v = 0;
    else if (!qty(val, &v, &un)) numeric = 0;
    for (int i = 0; i < nc && numeric; i++) {
        if (!qty(c[i], &ci, &u)) numeric = 0;
        else if (is_zero(c[i])) continue;
        else if (un && !expr_eq(un, u)) numeric = 0;
        else un = u, s2 += ci * ci;
    }
    int units = numeric && un && !is_one(un);
    int paren = units || ctx > 2;
    if (paren) sb_putc(b, '(');
    int done = 0;
    if (numeric) {
        size_t len0 = b->len;
        done = put_pm_num(b, v, sqrt(s2));
        if (!done) b->len = len0, b->buf[len0] = 0;
        if (!done && s2 == 0) {
            E *vv;
            units_split(val, &vv, &u);
            print_expr(b, vv);
            sb_puts(b, "\xc2\xb1" "0");
            done = 1;
        }
    }
    if (!done) {
        /* symbolic: value ± sqrt(sum c_i^2) */
        E **sq = salloc(sizeof(E *) * (size_t)nc);
        for (int i = 0; i < nc; i++) sq[i] = s_mul2(c[i], c[i]);
        print_expr(b, val);
        sb_puts(b, "\xc2\xb1");
        E *sg = s_sqrt(s_add((uint32_t)nc, sq));
        if (sg->t == E_ADD) {
            sb_putc(b, '(');
            print_expr(b, sg);
            sb_putc(b, ')');
        } else
            print_expr(b, sg);
        units = 0;
    }
    if (paren) sb_putc(b, ')');
    if (units) put_units(b, un);
    srelease(mark);
    return 1;
}

/* ------------------------------------------------------------ commands --- */
/* pm(v, s): v + |s| * (new source); lists elementwise (a scalar s applies
 * to every element, each with its own source). */
static E *pm1(E *v, E *s) {
    stack_check();
    if (v->t == E_LIST) {
        if (s->t == E_LIST && s->n != v->n) cas_throw(CAS_ERR_DOMAIN, "pm: list lengths differ");
        E **k = salloc(sizeof(E *) * (v->n + 1));
        for (uint32_t i = 0; i < v->n; i++) k[i] = pm1(EK(v)[i], s->t == E_LIST ? EK(s)[i] : s);
        return s_list(v->n, k);
    }
    if (s->t == E_LIST) cas_throw(CAS_ERR_DOMAIN, "pm: expected a value and an uncertainty");
    if (is_num(s) && num_sgn(s) < 0) s = num_neg(s);
    if (is_zero(s)) return v;
    return s_add2(v, s_mul2(s, new_source()));
}

/* Coefficients of the sources in the linear form of x: fills src/c (up to
 * cap), returns the count; *r0 gets the value part. */
static int coefs(E *x, E **r0, E **src, E **c, int cap) {
    x = linearize(x);
    E *all[64];
    uint32_t na = has_src(x) ? collect_syms(x, all, 64) : 0;
    int n = 0;
    E *zero[64];
    for (uint32_t i = 0; i < na && n < cap; i++)
        if (is_src(all[i])) src[n] = all[i], zero[n++] = K->c0;
    *r0 = n ? subst_many(x, (uint32_t)n, src, zero) : x;
    for (int i = 0; i < n; i++) c[i] = subst_many(diff(x, src[i]), (uint32_t)n, src, zero);
    return n;
}

/* op: 0 pm(v, s), 1 nominal(x), 2 uncert(x), 3 ucov(x, y).  Arguments are
 * evaluated; lists are mapped elementwise for nominal and uncert. */
E *unc_cmd(int op, uint32_t n, E **a) {
    stack_check();
    if (op == 0) return pm1(a[0], a[1]);
    if (a[0]->t == E_LIST && op < 3) {
        E **k = salloc(sizeof(E *) * (a[0]->n + 1));
        for (uint32_t i = 0; i < a[0]->n; i++) k[i] = unc_cmd(op, 1, &EK(a[0])[i]);
        return s_list(a[0]->n, k);
    }
    E *r0, *src[64], *c[64];
    int nx = coefs(a[0], &r0, src, c, 64);
    if (op == 1) return r0;
    E **t = salloc(sizeof(E *) * 65);
    int nt = 0;
    if (op == 2) {
        for (int i = 0; i < nx; i++) t[nt++] = s_mul2(c[i], c[i]);
        return nt ? s_sqrt(s_add((uint32_t)nt, t)) : K->c0;
    }
    E *r1, *src2[64], *d[64];
    int ny = coefs(a[1], &r1, src2, d, 64);
    for (int i = 0; i < nx; i++)
        for (int j = 0; j < ny; j++)
            if (src[i]->s == src2[j]->s) t[nt++] = s_mul2(c[i], d[j]);
    return s_add((uint32_t)nt, t);
}
