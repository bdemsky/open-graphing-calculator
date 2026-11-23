/*
 * Solver app: numeric equation solver (enter the equation, fill in the knowns, solve for one),
 * polynomial root finder and linear-system solver. All numeric (doubles) via
 * cas_compile, so it works with either CAS build.
 *
 *   Equation: type an equation in several variables, fill in the known ones,
 *   highlight the unknown and press F5 (solve; or ALPHA ENTER): the
 *   root nearest the current value (the guess) within the bounds; every sign
 *   change found by a scan of the bounds is listed as well, one row per
 *   root. The footer hint of each row says what it is for (sf_hint, pf_hint).
 *   Polynomial: degree and coefficients -> all complex roots (Aberth).
 *   Linear system: m equations, n unknowns (augmented matrix) -> unique
 *   solution, "no solution", or a parametric family in t1, t2, ...
 *
 * Framework wiring:
 *   - app_solver (APPS > Solver, registered in app/registry.c) opens a small
 *     menu that chooses one of the three screens; the soft-key bar
 *     solver_soft (equation / poly / linear / - / solve) switches between them.
 *     The "solve" soft key has no fn, so it reaches the screen as KEY_F5.
 *   - The equation and polynomial screens are xform_t row forms and the linear
 *     system is an xgrid_t cell grid (app/xform.c); this file only supplies the
 *     row callbacks and the math.
 *   - persist_solver (blob "solver", version 1) saves the equation text, the
 *     variable values, the bounds, the polynomial coefficients and the
 *     augmented matrix. Found roots and the linear-system result are not saved
 *     (they are recomputed on demand).
 *
 * State lives in three file-scope structs (sv: equation, pr: polynomial,
 * ls: linear system) plus their form widgets (sf, pf, lg/lf). No arena or CAS
 * variables are used; the CAS is only asked to compile the equation into a
 * one-variable numeric function (cas_compile / cas_fn_eval), so everything
 * runs in double precision and works with the stub CAS as well.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "registry.h"
#include "session.h"
#include "ui.h"
#include "xform.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

/* The defaults are normally installed by persist_solver's load (or reset) at
 * boot. When no "solver" blob exists, load is never called, so every screen's
 * enter() calls need_defaults() to install them lazily on first use. */
static bool inited;
static void sv_defaults(void);
static void need_defaults(void)
{
    if (!inited)
        sv_defaults();
}

/* ================================================================ equation */
/*
 * Equation solver. The form rows are:
 *   0            eq:        the equation (display text, e.g. "a*x^2+b=c")
 *   1..nv        name=      one row per variable found in the equation
 *   nv+1, nv+2   bound lo=, bound hi=   the search interval
 *   nv+3 ...     read-only: "roots:" (nothing solved yet) or one row
 *                "root1=", "root2=", ... per root found by the last solve
 * Committing the equation re-extracts the variable list (sv_vars), keeping the
 * values of variables whose names survive. F5 on a variable row solves for it;
 * on the eq: line (or a bound) it solves for the only variable when there is
 * one; on a root row it puts that root into the variable last solved for. The
 * footer hint (sf_hint) says what the selected row is for.
 */
#define SV_MAXV 8  /* variables in one equation */
#define SV_MAXR 12 /* roots kept by one solve */
static struct {
    char eq[160];                 /* display text */
    int nv;                       /* number of variables in name[] / val[] */
    char name[SV_MAXV][12];       /* variable names in order of first appearance */
    double val[SV_MAXV];          /* known values; the unknown's value is the initial guess */
    double lo, hi;                /* search bounds for the unknown */
    int nroots;                   /* roots found by the last solve (sorted ascending) */
    double roots[SV_MAXR];
    double leftrt;                /* residual lhs - rhs at the chosen root */
    int last;                     /* variable the roots belong to (index into name[]), -1: none */
} sv;

static xform_t sf;

/* Identifier characters: first (letter or _) and following (also digits). */
static bool ident0(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool ident1(char c) { return ident0(c) || (c >= '0' && c <= '9'); }

/* True for names that are never solver variables: CAS constants and keywords,
 * and anything starting with '_' (units and physical constants such as _m). */
static bool reserved(const char *w)
{
    static const char *const r[] = {"pi", "e", "i", "inf", "and", "or", "not", "mod", "ans", "true", "false", "undef"};
    if (w[0] == '_')
        return true; /* units and constants */
    for (size_t k = 0; k < sizeof r / sizeof r[0]; k++)
        if (!strcmp(w, r[k]))
            return true;
    return false;
}

/* Walk the identifiers of CAS text that are variables (not calls, not
 * reserved); cb returns replacement text or NULL to keep the name.
 *
 * s:   CAS text to scan.  out/n: receives a copy of s with every variable
 *      replaced by cb's text (truncated at n - 1 bytes, always terminated).
 * This is a lightweight lexer, not a parser: an identifier directly followed
 * by '(' is a function call and left alone; number literals are copied as a
 * unit so that the 'e' of 1e5 or the digits of 0x1F are not taken for
 * variables. Identifiers of 24 characters or more are copied unchanged (never
 * variables: names longer than 11 characters are not collected). Used twice:
 * with collect_cb to list the variables, with subst_cb to substitute the
 * known values. */
static void walk_vars(const char *s, char *out, size_t n, const char *(*cb)(const char *name))
{
    size_t o = 0;
    while (*s && o + 1 < n) {
        if (ident0(*s)) {
            char w[24];
            size_t l = 0;
            while (ident1(s[l]))
                l++;
            if (l >= sizeof w) { /* too long for a variable: copy it as it is */
                if (o + l >= n)
                    break;
                memcpy(out + o, s, l);
                o += l, s += l;
                continue;
            }
            memcpy(w, s, l);
            w[l] = 0;
            const char *rep = NULL;
            if (s[l] != '(' && !reserved(w))
                rep = cb(w);
            const char *src = rep ? rep : w;
            size_t sl = strlen(src);
            if (o + sl >= n)
                break;
            memcpy(out + o, src, sl);
            o += sl;
            s += l;
            continue;
        }
        if (*s >= '0' && *s <= '9') { /* numbers, including 1e5 and 0x1F */
            bool hex = s[0] == '0' && (s[1] == 'x' || s[1] == 'b' || s[1] == 'o');
            if (hex && o + 3 > n)
                break;
            if (hex)
                out[o++] = *s++, out[o++] = *s++;
            while (o + 2 < n && ((*s >= '0' && *s <= '9') || *s == '.' || (hex && ident1(*s)) ||
                                 ((*s == 'e' || *s == 'E') && (s[1] == '-' || (s[1] >= '0' && s[1] <= '9'))))) {
                if (!hex && (*s == 'e' || *s == 'E'))
                    out[o++] = *s++;
                out[o++] = *s++;
            }
            continue;
        }
        out[o++] = *s++;
    }
    out[o] = 0;
}

/* walk_vars callback: append each new variable name to sv.name[] (value 0),
 * up to SV_MAXV names of < 12 characters. Never replaces anything. */
static const char *collect_cb(const char *w)
{
    for (int i = 0; i < sv.nv; i++)
        if (!strcmp(sv.name[i], w))
            return NULL;
    if (sv.nv < SV_MAXV && strlen(w) < sizeof sv.name[0]) {
        snprintf(sv.name[sv.nv], sizeof sv.name[0], "%s", w);
        sv.val[sv.nv++] = 0;
    }
    return NULL;
}

/* Rebuild the variable list after the equation changed: convert the display
 * text to CAS text, collect its variables, and carry over the values of
 * variables that existed before (matched by name). Clears the root list. */
static void sv_vars(void)
{
    char cas[400], tmp[400];
    char oldn[SV_MAXV][12];
    double oldv[SV_MAXV];
    int on = sv.nv;
    memcpy(oldn, sv.name, sizeof oldn);
    memcpy(oldv, sv.val, sizeof oldv);
    x_cas_of(sv.eq, cas, sizeof cas);
    sv.nv = 0;
    walk_vars(cas, tmp, sizeof tmp, collect_cb);
    for (int i = 0; i < sv.nv; i++)
        for (int k = 0; k < on; k++)
            if (!strcmp(sv.name[i], oldn[k]))
                sv.val[i] = oldv[k];
    sv.nroots = 0;
    sv.last = -1;
}

/* walk_vars callback for sv_compile: replace every known variable (all but
 * sv.name[solve_for]) by its value in parentheses, printed with %.17g so the
 * double round-trips exactly. Returns a static buffer (valid until the next
 * call; walk_vars copies it immediately). */
static int solve_for;
static const char *subst_cb(const char *w)
{
    static char b[40];
    for (int i = 0; i < sv.nv; i++)
        if (i != solve_for && !strcmp(sv.name[i], w)) {
            snprintf(b, sizeof b, "(%.17g)", sv.val[i]);
            return b;
        }
    return NULL;
}

/* f(x) = lhs - rhs, other variables substituted
 *
 * k: index of the unknown in sv.name[]. Returns a compiled one-variable
 * function owned by the caller (free it with cas_fn_free), or NULL with an
 * "ERR: ..." message in err. The first '=' that is not part of ==, <=, >=,
 * != or := splits the equation; without one the text itself is taken as the
 * expression to set to zero. */
static cas_fn *sv_compile(int k, char *err, size_t errn)
{
    char cas[400], body[600], expr[640];
    x_cas_of(sv.eq, cas, sizeof cas);
    solve_for = k;
    walk_vars(cas, body, sizeof body, subst_cb);
    char *eq = strchr(body, '=');
    if (eq && eq[1] != '=' && (eq == body || (eq[-1] != '<' && eq[-1] != '>' && eq[-1] != '!' && eq[-1] != ':'))) {
        *eq = 0;
        snprintf(expr, sizeof expr, "(%s)-(%s)", body, eq + 1);
    } else
        snprintf(expr, sizeof expr, "%s", body);
    cas_fn *fn = NULL;
    if (cas_compile(g.cas, expr, sv.name[k], &fn) != CAS_OK || !fn) {
        snprintf(err, errn, "ERR: %s", cas_last_error(g.cas));
        return NULL;
    }
    return fn;
}

static double fe(const cas_fn *fn, double x) { return cas_fn_eval(fn, x); }

/* bracketed root (false position alternating with bisection)
 *
 * Precondition: fa = f(a) and fb = f(b) have opposite signs. The loop
 * alternates a regula-falsi (secant through the bracket ends) step on even
 * iterations with a bisection step on odd ones; the bisection steps guarantee
 * that the bracket at least halves every two iterations even when false
 * position stalls on one side, so the method cannot fail to converge. A
 * secant point outside the open bracket (rounding) is replaced by the
 * midpoint. Stops after 200 iterations, when the bracket is ~1e-15 relative,
 * on an exact zero, or when f becomes NaN; returns the end with the smaller
 * |f|. */
static double refine(const cas_fn *fn, double a, double b, double fa, double fb)
{
    for (int it = 0; it < 200 && fabs(b - a) > 1e-15 * (fabs(a) + fabs(b)) + 1e-300; it++) {
        double m = (it & 1) ? 0.5 * (a + b) : b - fb * (b - a) / (fb - fa);
        if (!(m > fmin(a, b) && m < fmax(a, b)))
            m = 0.5 * (a + b);
        double fm = fe(fn, m);
        if (isnan(fm))
            break;
        if (fm == 0)
            return m;
        if ((fm < 0) == (fa < 0))
            a = m, fa = fm;
        else
            b = m, fb = fm;
    }
    return fabs(fa) < fabs(fb) ? a : b;
}

/* Add a candidate root to sv.roots[] if it is a genuine root inside the
 * bounds and not a duplicate (relative distance 1e-9). A sign change is also
 * found across a pole (1/x), so candidates whose |f| is not small compared
 * with |f| at r +- h are rejected. Silently ignores roots beyond SV_MAXR. */
static void add_root(double r, const cas_fn *fn)
{
    double fr = fe(fn, r);
    if (isnan(fr) || r < sv.lo || r > sv.hi)
        return;
    /* reject poles: a true root has a small residual relative to nearby values */
    double h = 1e-6 * (fabs(r) + 1e-3), s = fabs(fe(fn, r + h)) + fabs(fe(fn, r - h));
    if (fabs(fr) > 1e-6 * (s + 1e-9) && fabs(fr) > 1e-9)
        return;
    for (int i = 0; i < sv.nroots; i++)
        if (fabs(sv.roots[i] - r) <= 1e-9 * (fabs(r) + 1e-9))
            return;
    if (sv.nroots < SV_MAXR)
        sv.roots[sv.nroots++] = r;
}

/* Sample f at n + 1 equally spaced points of [a, b]; every interval with a
 * sign change is refined to a root, and exact zeros at sample points are
 * taken as they are. Intervals with a NaN end (outside the domain) are
 * skipped. Roots of even multiplicity (no sign change) are not seen here;
 * only the Newton step in sv_solve can find them. */
static void scan(const cas_fn *fn, double a, double b, int n)
{
    double x0 = a, f0 = fe(fn, a);
    for (int i = 1; i <= n; i++) {
        double x1 = a + (b - a) * i / n, f1 = fe(fn, x1);
        if (!isnan(f0) && !isnan(f1)) {
            if (f0 == 0)
                add_root(x0, fn);
            else if ((f0 < 0) != (f1 < 0))
                add_root(refine(fn, x0, x1, f0, f1), fn);
        }
        x0 = x1, f0 = f1;
    }
    if (f0 == 0)
        add_root(x0, fn);
}

/* xform action (F5 / ALPHA ENTER): solve for the variable on form row `row`.
 *
 * 1. Compile f(x) = lhs - rhs with the other variables substituted.
 * 2. Newton's method from the current value (the guess), with a central
 *    difference derivative, h = 1e-7 * (|x| + 1e-7); up to 60 steps. This
 *    finds the root nearest the guess in the common case, including double
 *    roots that have no sign change.
 * 3. Scan the bounds for sign changes (scan()): 2000 equal steps when the
 *    interval is at most 1e6 wide; otherwise 400 steps on [-1, 1] plus a
 *    geometric grid (ratio 1.25, 4 samples per cell) out to the larger bound
 *    (at most 1e300), each cell cut to [lo, hi], so a huge default interval
 *    (+-1e99) still gets sensible resolution near 0 and the cells at the
 *    bounds are scanned too (about 8000 evaluations for +-1e99).
 * 4. Sort the roots, store the one nearest the guess in the variable and show
 *    the residual ("left-rt", lhs - rhs) and the number of roots; every root
 *    appears in the "roots:" row.
 * Rows other than a variable: a root row copies that root into the variable
 * the roots belong to (sv.last) and selects it; the eq: line and the bounds
 * solve for the only variable if there is exactly one, else ask for the
 * unknown to be selected.
 * Side effects: sv.val[k], sv.roots/nroots, sv.last, sv.leftrt, the form
 * footer and the selected row (sf.sel: the variable solved for). */
static void sv_solve(int row)
{
    int k = row - 1;
    if (row >= sv.nv + 3 && sv.nroots && sv.last >= 0 && sv.last < sv.nv) { /* a root row */
        int r = row - sv.nv - 3;
        char b[24];
        sv.val[sv.last] = sv.roots[r];
        sf.sel = sv.last + 1;
        x_fmt(sv.roots[r], b, sizeof b);
        xform_msg(&sf, false, "%s=%s (root %d of %d)", sv.name[sv.last], b, r + 1, sv.nroots);
        return;
    }
    if ((k < 0 || k >= sv.nv) && sv.nv == 1)
        k = 0; /* only one variable: no need to select it */
    if (k < 0 || k >= sv.nv) {
        xform_msg(&sf, true, sv.nv ? "Select the unknown with ▲▼, then F5"
                                   : "Type an equation on the eq: line first");
        return;
    }
    sf.sel = k + 1;
    char err[64];
    cas_fn *fn = sv_compile(k, err, sizeof err);
    if (!fn) {
        xform_msg(&sf, true, "%s", err);
        return;
    }
    double guess = sv.val[k];
    sv.nroots = 0;
    /* Newton / secant from the guess */
    double x = guess, best = NAN;
    for (int it = 0; it < 60; it++) {
        double f = fe(fn, x), h = 1e-7 * (fabs(x) + 1e-7), d = (fe(fn, x + h) - fe(fn, x - h)) / (2 * h);
        if (isnan(f) || isnan(d) || d == 0)
            break;
        double nx = x - f / d;
        if (fabs(nx - x) <= 1e-14 * (fabs(nx) + 1e-14)) {
            best = nx;
            break;
        }
        x = nx;
        if (fabs(x) > 1e300)
            break;
    }
    if (!isnan(best))
        add_root(best, fn);
    /* scan the bounds (geometric outside +-1 when they are wide) */
    double lo = sv.lo, hi = sv.hi;
    if (hi - lo <= 1e6)
        scan(fn, lo, hi, 2000);
    else {
        double a = fmax(lo, -1), b = fmin(hi, 1);
        if (a < b)
            scan(fn, a, b, 400);
        double top = fmin(fmax(fabs(lo), fabs(hi)), 1e300);
        for (int sgn = -1; sgn <= 1; sgn += 2)
            for (double m = 1; m < top; m *= 1.25) {
                double p = fmax(fmin(sgn * m, sgn * m * 1.25), lo), q = fmin(fmax(sgn * m, sgn * m * 1.25), hi);
                if (p < q)
                    scan(fn, p, q, 4);
            }
    }
    /* sort; pick the root nearest the guess */
    for (int i = 1; i < sv.nroots; i++)
        for (int j = i; j > 0 && sv.roots[j - 1] > sv.roots[j]; j--) {
            double t = sv.roots[j];
            sv.roots[j] = sv.roots[j - 1];
            sv.roots[j - 1] = t;
        }
    if (!sv.nroots) {
        cas_fn_free(g.cas, fn);
        sv.last = -1;
        xform_msg(&sf, true, "ERR: no root found: try another guess or bounds");
        return;
    }
    int bi = 0;
    for (int i = 1; i < sv.nroots; i++)
        if (fabs(sv.roots[i] - guess) < fabs(sv.roots[bi] - guess))
            bi = i;
    sv.val[k] = sv.roots[bi];
    sv.last = k;
    sv.leftrt = fe(fn, sv.val[k]);
    cas_fn_free(g.cas, fn);
    char b[32];
    x_fmt(sv.leftrt, b, sizeof b);
    xform_msg(&sf, false, "%s solved: %d root%s, left-right=%s", sv.name[k], sv.nroots, sv.nroots == 1 ? "" : "s", b);
}

/* xform_ops_t callbacks of the equation form (row layout described above sv). */
static int sf_count(void) { return sv.nv + 3 + (sv.nroots ? sv.nroots : 1); }
static int sf_kind(int i) { return i >= sv.nv + 3 ? XR_INFO : XR_EDIT; }
static void sf_label(int i, char *b, size_t n)
{
    if (i == 0)
        snprintf(b, n, "eq:");
    else if (i <= sv.nv)
        snprintf(b, n, "%s=", sv.name[i - 1]);
    else if (i <= sv.nv + 2)
        snprintf(b, n, "%s", i == sv.nv + 1 ? "bound lo=" : "bound hi=");
    else if (sv.nroots)
        snprintf(b, n, "root%d=", i - sv.nv - 2);
    else
        snprintf(b, n, "roots:");
}
static void sf_get(int i, char *b, size_t n)
{
    if (i == 0)
        snprintf(b, n, "%s", sv.eq);
    else if (i <= sv.nv)
        x_fmt(sv.val[i - 1], b, n);
    else if (i <= sv.nv + 2)
        x_fmt(i == sv.nv + 1 ? sv.lo : sv.hi, b, n);
    else if (sv.nroots)
        x_fmt(sv.roots[i - sv.nv - 3], b, n);
    else
        b[0] = 0;
}

/* Footer hint for the selected row (xform_ops_t.hint): what the row is and
 * what F5 does there. Returns a static buffer. */
static const char *sf_hint(int i)
{
    static char b[64];
    if (i == 0)
        snprintf(b, sizeof b, "%s", sv.nv ? "the equation; ▼ to the variables, F5 solves"
                                         : "type an equation, e.g. x^3-2x=1, then ENTER");
    else if (i <= sv.nv)
        snprintf(b, sizeof b, "F5: solve for %s (its value is the guess)", sv.name[i - 1]);
    else if (i <= sv.nv + 2)
        snprintf(b, sizeof b, "search range for the unknown");
    else if (sv.nroots && sv.last >= 0)
        snprintf(b, sizeof b, "F5: use this root as %s", sv.name[sv.last]);
    else
        snprintf(b, sizeof b, "F5 on a variable lists every root here");
    return b;
}
/* Commit an edited row. Row 0 stores the display text (so it can be edited
 * again) and re-extracts the variables; numeric rows are evaluated with
 * x_number (expressions such as 2*pi are allowed). Returns false, which keeps
 * the row in edit mode, when the value does not evaluate to a number. */
static bool sf_set(int i, const char *cas, const char *disp)
{
    if (i == 0) {
        snprintf(sv.eq, sizeof sv.eq, "%s", disp);
        sv_vars();
        return true;
    }
    double v;
    char err[64];
    if (!x_number(cas, &v, err, sizeof err)) {
        xform_msg(&sf, true, "ERR: %s", err);
        return false;
    }
    if (i <= sv.nv)
        sv.val[i - 1] = v;
    else if (i == sv.nv + 1)
        sv.lo = v;
    else
        sv.hi = v;
    return true;
}

static const xform_ops_t sf_ops = {sf_count, sf_kind, sf_label, sf_get, sf_set, NULL, sv_solve, sf_hint};

/* Screen callbacks: the xform widget does the drawing, editing, scrolling and
 * touch; CLEAR on a row that is not being edited returns home. enter() keeps
 * the selection and message when returning from a menu (ui_returning). */
static void sf_enter(void)
{
    need_defaults();
    sf.o = &sf_ops;
    sf.hint = NULL; /* sf_hint */
    if (!ui_returning) {
        sf.msg[0] = 0;
        xform_reset(&sf);
    }
}
static void sf_draw(void) { xform_draw(&sf); }
static void sf_key(int key, int mod)
{
    if (!xform_key(&sf, key, mod) && key == KEY_CLEAR)
        ui_switch(ui_home());
}
static editor_t *sf_editor(void) { return xform_editor(&sf); }
static void sf_touch(const touch_event_t *ev) { xform_touch(&sf, ev); }
extern const softkeys_t solver_soft;
const screen_t scr_eqsolver = {"eqsolver", "SOLVER", sf_enter, sf_draw, sf_key, NULL, sf_editor, sf_touch, "solver",
                               &solver_soft};

/* ============================================================ polynomial */
/* Polynomial roots: form rows are degree=, one row per coefficient from the
 * highest power down, then one read-only row per (complex) root. Roots are
 * recomputed whenever a coefficient or the degree is committed. */
#define PMAX 10 /* maximum degree */
static struct {
    int deg;
    double a[PMAX + 1]; /* a[0] x^deg + ... + a[deg] */
    int nr;             /* roots in re[]/im[] (deg minus leading zero coefficients) */
    double re[PMAX], im[PMAX];
    bool ok;            /* false: all coefficients of degree >= 1 are zero */
} pr;

/* Complex division (a + bi) / (c + di) -> *re + *im i (textbook formula; no
 * overflow scaling, which is fine for the magnitudes seen here). */
static void cdiv(double a, double b, double c, double d, double *re, double *im)
{
    double den = c * c + d * d;
    *re = (a * c + b * d) / den;
    *im = (b * c - a * d) / den;
}

/* Aberth-Ehrlich iteration on the normalized polynomial
 *
 * Fills pr.re/pr.im/pr.nr and sets pr.ok. Steps:
 * 1. Skip leading zero coefficients (the effective degree n drops) and divide
 *    by the leading coefficient so the polynomial is monic: c[0] = 1.
 * 2. Trailing zero coefficients are roots at 0: count them (z) and deflate.
 * 3. Start the n approximations on a circle of radius max_i |c_i|^(1/i) + 0.1
 *    (a bound in the spirit of Fujiwara's root bound), at equally spaced
 *    angles offset by 0.4 rad so no start is on the real axis (a conjugate-
 *    symmetric start could never leave it).
 * 4. Aberth step for every z_i (Gauss-Seidel style: updated values are used
 *    at once):  q = p(z_i) / p'(z_i),  s = sum_{j != i} 1 / (z_i - z_j),
 *    w = q / (1 - q s),  z_i -= w.  This is Newton's step corrected for the
 *    other roots; it converges cubically for simple roots and finds all roots
 *    simultaneously without deflation (O. Aberth, "Iteration methods for
 *    finding all zeros of a polynomial simultaneously", Math. Comp. 27, 1973).
 *    p and p' are evaluated together by Horner's scheme in complex
 *    arithmetic. If 1 - q s is 0 the plain Newton step is used.
 * 5. Stop when the largest relative step is < 1e-15 or after 500 sweeps.
 * 6. Snap tiny real / imaginary parts to 0 (relative 1e-13 and 1e-11: the
 *    imaginary part of a real root is noisier) and sort by real part, then
 *    imaginary part. */
static void poly_roots(void)
{
    int lead = 0, n = pr.deg;
    pr.nr = 0;
    pr.ok = false;
    while (lead <= pr.deg && pr.a[lead] == 0)
        lead++, n--;
    if (n < 1)
        return;
    double c[PMAX + 1];
    for (int i = 0; i <= n; i++)
        c[i] = pr.a[lead + i] / pr.a[lead];
    int z = 0; /* zero roots */
    while (n > 0 && c[n] == 0)
        n--, z++;
    double zr[PMAX], zi[PMAX];
    double rad = 0;
    for (int i = 1; i <= n; i++)
        rad = fmax(rad, pow(fabs(c[i]), 1.0 / i));
    for (int i = 0; i < n; i++) {
        double ang = 2 * M_PI * i / n + 0.4;
        zr[i] = (rad + 0.1) * cos(ang);
        zi[i] = (rad + 0.1) * sin(ang);
    }
    for (int it = 0; it < 500; it++) {
        double maxstep = 0;
        for (int i = 0; i < n; i++) {
            /* p and p' by Horner */
            double pr_ = 1, pi_ = 0, dr = 0, di = 0;
            for (int k = 1; k <= n; k++) {
                double ndr = dr * zr[i] - di * zi[i] + pr_, ndi = dr * zi[i] + di * zr[i] + pi_;
                dr = ndr, di = ndi;
                double npr = pr_ * zr[i] - pi_ * zi[i] + c[k], npi = pr_ * zi[i] + pi_ * zr[i];
                pr_ = npr, pi_ = npi;
            }
            if (pr_ == 0 && pi_ == 0)
                continue;
            double qr, qi; /* p/p' */
            cdiv(pr_, pi_, dr, di, &qr, &qi);
            double sr = 0, si = 0;
            for (int j = 0; j < n; j++)
                if (j != i) {
                    double r, im;
                    cdiv(1, 0, zr[i] - zr[j], zi[i] - zi[j], &r, &im);
                    sr += r, si += im;
                }
            /* w = q / (1 - q*s) */
            double den_r = 1 - (qr * sr - qi * si), den_i = -(qr * si + qi * sr), wr, wi;
            cdiv(qr, qi, den_r, den_i, &wr, &wi);
            if (!isfinite(wr) || !isfinite(wi))
                wr = qr, wi = qi;
            zr[i] -= wr, zi[i] -= wi;
            maxstep = fmax(maxstep, hypot(wr, wi) / (hypot(zr[i], zi[i]) + 1e-300));
        }
        if (maxstep < 1e-15)
            break;
    }
    for (int i = 0; i < z; i++)
        pr.re[pr.nr] = 0, pr.im[pr.nr++] = 0;
    for (int i = 0; i < n; i++) {
        double m = hypot(zr[i], zi[i]) + 1e-300;
        pr.re[pr.nr] = fabs(zr[i]) < 1e-13 * m ? 0 : zr[i];
        pr.im[pr.nr++] = fabs(zi[i]) < 1e-11 * m ? 0 : zi[i];
    }
    /* sort: real part, then imaginary */
    for (int i = 1; i < pr.nr; i++)
        for (int j = i; j > 0 && (pr.re[j - 1] > pr.re[j] + 1e-12 ||
                                  (fabs(pr.re[j - 1] - pr.re[j]) <= 1e-12 && pr.im[j - 1] > pr.im[j]));
             j--) {
            double t = pr.re[j];
            pr.re[j] = pr.re[j - 1], pr.re[j - 1] = t;
            t = pr.im[j];
            pr.im[j] = pr.im[j - 1], pr.im[j - 1] = t;
        }
    pr.ok = true;
}

/* Format a complex root as "a", "bi", "a+bi" or "a-bi" (x_fmt numbers). */
static void cfmt(double re, double im, char *b, size_t n)
{
    char r[24], i[24];
    x_fmt(re, r, sizeof r);
    if (im == 0) {
        snprintf(b, n, "%s", r);
        return;
    }
    x_fmt(fabs(im), i, sizeof i);
    if (re == 0)
        snprintf(b, n, "%s%si", im < 0 ? "-" : "", i);
    else
        snprintf(b, n, "%s%s%si", r, im < 0 ? "-" : "+", i);
}

/* xform_ops_t callbacks of the polynomial form: row 0 degree, rows
 * 1..deg+1 coefficients a[0..deg] (labelled x^p:, x:, 1:), then the roots
 * x1=, x2=, ... (read-only). */
static xform_t pf;
static int pf_count(void) { return 1 + (pr.deg + 1) + pr.nr; }
static int pf_kind(int i) { return i <= pr.deg + 1 ? XR_EDIT : XR_INFO; }
static void pf_label(int i, char *b, size_t n)
{
    if (i == 0)
        snprintf(b, n, "degree=");
    else if (i <= pr.deg + 1) {
        int p = pr.deg - (i - 1);
        if (p > 1)
            snprintf(b, n, "x^%d:", p);
        else
            snprintf(b, n, "%s", p ? "x:  " : "1:  ");
    } else
        snprintf(b, n, "x%d=", i - pr.deg - 1);
}
static void pf_get(int i, char *b, size_t n)
{
    if (i == 0)
        snprintf(b, n, "%d", pr.deg);
    else if (i <= pr.deg + 1)
        x_fmt(pr.a[i - 1], b, n);
    else {
        int k = i - pr.deg - 2;
        cfmt(pr.re[k], pr.im[k], b, n);
    }
}
/* Commit a row and recompute the roots. A new degree (integer 1..PMAX) keeps
 * the low-order coefficients aligned to their powers; when the degree grows
 * the new leading coefficient is 1 so the polynomial keeps that degree. */
static bool pf_set(int i, const char *cas, const char *disp)
{
    double v;
    char err[64];
    if (!x_number(cas, &v, err, sizeof err)) {
        xform_msg(&pf, true, "ERR: %s", err);
        return false;
    }
    if (i == 0) {
        int d = (int)v;
        if (d != v || d < 1 || d > PMAX) {
            xform_msg(&pf, true, "ERR: degree 1..%d", PMAX);
            return false;
        }
        /* keep the low-order coefficients */
        double a[PMAX + 1] = {0};
        for (int k = 0; k <= d && k <= pr.deg; k++)
            a[d - k] = pr.a[pr.deg - k];
        if (d > pr.deg)
            a[0] = 1;
        memcpy(pr.a, a, sizeof a);
        pr.deg = d;
    } else
        pr.a[i - 1] = v;
    poly_roots();
    if (!pr.ok)
        xform_msg(&pf, true, "ERR: all coefficients but the constant are 0");
    else
        pf.msg[0] = 0;
    return true;
}

/* Footer hint (xform_ops_t.hint): on the degree row what to enter, else the
 * polynomial itself as the coefficients make it ("p(x)=x^3-2x-1"), so the
 * order of the coefficients is visible while they are typed. */
static const char *pf_hint(int i)
{
    static char b[160];
    if (i == 0)
        return "degree 1..10, then the coefficients from x^n down";
    size_t o = (size_t)snprintf(b, sizeof b, "p(x)=");
    bool any = false;
    for (int k = 0; k <= pr.deg && o + 24 < sizeof b; k++) {
        double c = pr.a[k];
        int p = pr.deg - k;
        if (c == 0)
            continue;
        char num[24];
        x_fmt(fabs(c), num, sizeof num);
        if (any || c < 0)
            b[o++] = c < 0 ? '-' : '+';
        if (strcmp(num, "1") || !p)
            o += (size_t)snprintf(b + o, sizeof b - o, "%s", num);
        if (p)
            o += (size_t)snprintf(b + o, sizeof b - o, p > 1 ? "x^%d" : "x", p);
        any = true;
    }
    if (!any)
        snprintf(b + o, sizeof b - o, "0");
    else
        b[o] = 0;
    return b;
}
static const xform_ops_t pf_ops = {pf_count, pf_kind, pf_label, pf_get, pf_set, NULL, NULL, pf_hint};

/* Screen callbacks (same pattern as the equation form); entering recomputes
 * the roots, which are not saved with the session. */
static void pf_enter(void)
{
    need_defaults();
    pf.o = &pf_ops;
    pf.hint = NULL; /* pf_hint */
    if (!ui_returning) {
        poly_roots();
        xform_reset(&pf);
    }
}
static void pf_draw(void) { xform_draw(&pf); }
static void pf_key(int key, int mod)
{
    if (!xform_key(&pf, key, mod) && key == KEY_CLEAR)
        ui_switch(ui_home());
}
static editor_t *pf_editor(void) { return xform_editor(&pf); }
static void pf_touch(const touch_event_t *ev) { xform_touch(&pf, ev); }
const screen_t scr_polyroot = {"polyroot", "POLY ROOTS", pf_enter, pf_draw, pf_key, NULL, pf_editor, pf_touch,
                               "polyroot", &solver_soft};

/* =========================================================== linear system */
/* Linear system: the coefficients are edited as an m x (n+1) grid [A|b]
 * (scr_linsys); F5 or ALPHA/SHIFT ENTER solves and shows the result lines on
 * a read-only form (scr_linres); CLEAR there returns to the grid unchanged. */
#define LMAX 6 /* maximum equations and unknowns */
static struct {
    int m, n;                       /* equations, unknowns */
    double a[LMAX][LMAX + 1];       /* augmented matrix, column n = right-hand side b */
} ls;
static char lres[LMAX + 2][48];     /* result lines: status line + one line per unknown */
static int nlres;

/* Solve ls by Gauss-Jordan elimination with partial pivoting into reduced row
 * echelon form, and write the result lines to lres[].
 *
 * - Pivots smaller than tol = 1e-11 * max|a_ij| count as zero, so the rank
 *   decision is relative to the size of the data.
 * - Each pivot row is scaled to a leading 1 and the column is eliminated from
 *   every other row (above and below), so the solution can be read off
 *   directly: piv[r] is the pivot column of row r.
 * - A zero row with a non-zero right-hand side means the system is
 *   inconsistent ("no solution").
 * - Non-pivot columns are free variables, named t1, t2, ...; each pivot
 *   variable is printed as x_j = rhs - sum(M[r][k] * t_k) over the free k,
 *   dropping a zero constant and a coefficient of 1. */
static void ls_solve(void)
{
    double M[LMAX][LMAX + 1];
    int m = ls.m, n = ls.n, piv[LMAX], rank = 0;
    memcpy(M, ls.a, sizeof M);
    double scale = 0;
    for (int i = 0; i < m; i++)
        for (int j = 0; j <= n; j++)
            scale = fmax(scale, fabs(M[i][j]));
    double tol = 1e-11 * (scale > 0 ? scale : 1);
    for (int c = 0; c < n && rank < m; c++) {
        int p = rank;
        for (int i = rank + 1; i < m; i++)
            if (fabs(M[i][c]) > fabs(M[p][c]))
                p = i;
        if (fabs(M[p][c]) <= tol)
            continue;
        for (int j = 0; j <= n; j++) {
            double t = M[p][j];
            M[p][j] = M[rank][j], M[rank][j] = t;
        }
        double d = M[rank][c];
        for (int j = 0; j <= n; j++)
            M[rank][j] /= d;
        for (int i = 0; i < m; i++)
            if (i != rank && M[i][c] != 0) {
                double f = M[i][c];
                for (int j = 0; j <= n; j++)
                    M[i][j] -= f * M[rank][j];
            }
        piv[rank++] = c;
    }
    nlres = 0;
    for (int i = rank; i < m; i++)
        if (fabs(M[i][n]) > tol) {
            snprintf(lres[nlres++], sizeof lres[0], "no solution (inconsistent)");
            return;
        }
    /* free variables get parameters t1, t2, ... */
    int par[LMAX], np = 0;
    for (int j = 0; j < n; j++) {
        par[j] = 0;
        bool isp = false;
        for (int r = 0; r < rank; r++)
            isp |= piv[r] == j;
        if (!isp)
            par[j] = ++np;
    }
    snprintf(lres[nlres++], sizeof lres[0], np ? "infinitely many (%d parameter%s)" : "unique solution", np,
             np == 1 ? "" : "s");
    for (int j = 0; j < n; j++) {
        char *b = lres[nlres++], num[24];
        size_t sz = sizeof lres[0];
        if (par[j]) {
            snprintf(b, sz, "x%d=t%d", j + 1, par[j]);
            continue;
        }
        int r = 0;
        while (piv[r] != j)
            r++;
        bool c0 = fabs(M[r][n]) <= tol, first = true;
        x_fmt(c0 ? 0 : M[r][n], num, sizeof num);
        snprintf(b, sz, "x%d=%s", j + 1, num);
        for (int k = 0; k < n; k++)
            if (par[k] && fabs(M[r][k]) > tol) {
                if (c0 && first)
                    b[strlen(b) - 1] = 0; /* drop the constant 0 */
                x_fmt(fabs(M[r][k]), num, sizeof num);
                size_t l = strlen(b);
                snprintf(b + l, sz - l, "%s%st%d", M[r][k] > 0 ? "-" : (c0 && first) ? "" : "+",
                         strcmp(num, "1") ? num : "", par[k]);
                first = false;
            }
    }
}

/* Result screen: a read-only xform listing lres[] (filled by ls_solve before
 * the switch; no enter callback, so the list is left as lg_key set it up). */
static xform_t lf;
static int lf_count(void) { return nlres; }
static int lf_kind(int i) { return XR_INFO; }
static void lf_label(int i, char *b, size_t n) { b[0] = 0; }
static void lf_get(int i, char *b, size_t n) { snprintf(b, n, "%s", lres[i]); }
static bool lf_set(int i, const char *cas, const char *disp) { return true; }
static const xform_ops_t lf_ops = {lf_count, lf_kind, lf_label, lf_get, lf_set};
static void lr_draw(void) { xform_draw(&lf); }
static void lr_key(int key, int mod);
extern const screen_t scr_linsys;
static void lr_touch(const touch_event_t *ev) { xform_touch(&lf, ev); }
const screen_t scr_linres = {"linres", "SOLUTION", NULL, lr_draw, lr_key, NULL, NULL, lr_touch, "linsys", &solver_soft};

/* Coefficient grid callbacks: cell text, commit of an edited cell (any
 * numeric expression), and resizing from the dimensions line (rows =
 * equations, columns = unknowns + 1 for b). */
static xgrid_t lg;
static void lg_cell(int r, int c, char *b, size_t n) { x_fmt(ls.a[r][c], b, n); }
/* Column header: the unknowns x1..xn, then b (the right-hand side). */
static void lg_colname(int c, char *b, size_t n)
{
    if (c < ls.n)
        snprintf(b, n, "x%d", c + 1);
    else
        snprintf(b, n, "= b");
}
/* The size in words next to "[A|b] m×(n+1)". */
static void lg_note(void)
{
    snprintf(lg.note, sizeof lg.note, "%d equation%s, %d unknown%s", ls.m, ls.m == 1 ? "" : "s", ls.n,
             ls.n == 1 ? "" : "s");
}
static bool lg_set(int r, int c, const char *cas, char *err, size_t errn)
{
    double v;
    char e[56];
    if (!x_number(cas, &v, e, sizeof e)) {
        snprintf(err, errn, "ERR: %s", e);
        return false;
    }
    ls.a[r][c] = v;
    return true;
}
static bool lg_resize(int r, int c, char *err, size_t errn)
{
    if (c < 2) {
        snprintf(err, errn, "ERR: at least 1 unknown");
        return false;
    }
    ls.m = r;
    ls.n = c - 1;
    lg_note();
    return true;
}
/* (Re)configure the grid for ls when the screen is opened fresh; keep the
 * cursor and any edit in progress when returning from a menu or the result. */
static void lg_enter(void)
{
    need_defaults();
    if (ui_returning)
        return;
    lg.cell = lg_cell;
    lg.set = lg_set;
    lg.resize = lg_resize;
    lg.colname = lg_colname;
    lg.maxr = LMAX;
    lg.maxc = LMAX + 1;
    lg.rows = ls.m;
    lg.cols = ls.n + 1;
    lg.sr = 0;
    lg.sc = 0;
    lg.editing = false;
    snprintf(lg.title, sizeof lg.title, "[A|b]");
    lg_note();
    snprintf(lg.msg, sizeof lg.msg, "one row per equation; F5 solves");
    lg.err = false;
}
static void lg_draw(void) { xgrid_draw(&lg); }
/* Grid keys. Solve on F5 or a modified ENTER (SHIFT, or ALPHA pending: plain
 * ENTER edits cells): first commit a cell being edited (stay if it does not
 * evaluate), then solve and switch to the result screen. Everything else goes
 * to the grid widget; an unused CLEAR returns home. */
static void lg_key(int key, int mod)
{
    if ((key == KEY_ENTER && (mod != MOD_NONE || g.alpha_snapshot_for_key)) || key == KEY_F5) {
        if (lg.editing && !xgrid_key(&lg, KEY_ENTER, MOD_NONE))
            return;
        if (lg.err)
            return;
        ls_solve();
        lf.sel = lf.top = 0;
        lf.o = &lf_ops;
        lf.hint = "CLEAR: back to the coefficients";
        ui_switch(&scr_linres);
        return;
    }
    if (!xgrid_key(&lg, key, mod) && key == KEY_CLEAR)
        ui_switch(ui_home());
}
/* Result screen keys: CLEAR goes back to the grid with ui_returning set so
 * lg_enter keeps the grid state; other keys scroll the result list. */
static void lr_key(int key, int mod)
{
    if (key == KEY_CLEAR) {
        ui_returning = true;
        ui_switch(&scr_linsys);
        return;
    }
    xform_key(&lf, key, mod);
}
static editor_t *lg_editor(void) { return xgrid_editor(&lg); }
static void lg_touch(const touch_event_t *ev) { xgrid_touch(&lg, ev); }
const screen_t scr_linsys = {"linsys", "LINEAR SYSTEM", lg_enter, lg_draw, lg_key, NULL, lg_editor, lg_touch, "linsys",
                             &solver_soft};

/* ================================================================== app */
/* APPS > Solver opens this menu; the action selects the screen. ui_returning
 * is cleared so the chosen screen starts fresh (menus normally return with it
 * set). */
static const menu_item_t solver_items[] = {
    {"Equation solver", NULL, NULL, 0, "solver"},
    {"Polynomial roots", NULL, NULL, 1, "polyroot"},
    {"Linear system", NULL, NULL, 2, "linsys"},
};
static void solver_select(const menu_item_t *it)
{
    ui_returning = false;
    ui_switch(it->action == 0 ? &scr_eqsolver : it->action == 1 ? &scr_polyroot : &scr_linsys);
}
static const menu_def_t menu_solver = {"SOLVER", solver_items, 3, solver_select, false, "solver"};

/* Soft keys shared by all solver screens: F1..F3 switch screens, F5 "solve"
 * has no action and reaches the current screen's key() as KEY_F5. */
static void sk_eqn(void) { ui_switch(&scr_eqsolver); }
static void sk_poly(void) { ui_switch(&scr_polyroot); }
static void sk_lin(void) { ui_switch(&scr_linsys); }
const softkeys_t solver_soft = {{{"equation", sk_eqn}, {"poly", sk_poly}, {"linear", sk_lin}, {NULL}, {"solve"}}};
static void solver_open(void) { menu_open(&menu_solver); }
const app_def_t app_solver = {"solver", "Solver", solver_open, {{0}}, "solver"};

/* ================================================================ persist */
/* Defaults (also persist_solver's reset for MEM > Reset all): empty equation,
 * bounds +-1e99, the polynomial x^2 - 1, and the 2x2 identity system with
 * b = 0. Clears the forms' selection and forces them to reload their rows. */
static void sv_defaults(void)
{
    inited = true;
    memset(&sv, 0, sizeof sv);
    sv.last = -1;
    sv.lo = -1e99;
    sv.hi = 1e99;
    memset(&pr, 0, sizeof pr);
    pr.deg = 2;
    pr.a[0] = 1;
    pr.a[2] = -1;
    memset(&ls, 0, sizeof ls);
    ls.m = ls.n = 2;
    ls.a[0][0] = ls.a[1][1] = 1;
    sf.sel = pf.sel = 0;
    sf.loaded = pf.loaded = false;
}

/* Blob layout (version 1):
 *   str eq; u8 nv; nv x (str name, f64 value); f64 lo, hi;
 *   u8 deg; (deg + 1) x f64 coefficient;
 *   u8 m, n; m x (n + 1) x f64 augmented matrix. */
static void svp_save(pw_t *w)
{
    pw_str(w, sv.eq);
    pw_u8(w, (uint32_t)sv.nv);
    for (int i = 0; i < sv.nv; i++) {
        pw_str(w, sv.name[i]);
        pw_f64(w, sv.val[i]);
    }
    pw_f64(w, sv.lo);
    pw_f64(w, sv.hi);
    pw_u8(w, (uint32_t)pr.deg);
    for (int i = 0; i <= pr.deg; i++)
        pw_f64(w, pr.a[i]);
    pw_u8(w, (uint32_t)ls.m);
    pw_u8(w, (uint32_t)ls.n);
    for (int i = 0; i < ls.m; i++)
        for (int j = 0; j <= ls.n; j++)
            pw_f64(w, ls.a[i][j]);
}

/* Restore the blob over the defaults. Out-of-range sizes keep the defaults
 * for that part, but their entries are still read past, so the later fields
 * are read from the right place; a truncated or corrupt blob (r->err) resets
 * everything. */
static void svp_load(pr_t *r, unsigned version)
{
    sv_defaults();
    pr_str(r, sv.eq, sizeof sv.eq);
    unsigned nv = pr_u8(r);
    for (unsigned i = 0; i < nv; i++) {
        char name[sizeof sv.name[0]];
        double v;
        pr_str(r, name, sizeof name);
        v = pr_f64(r);
        if (nv <= SV_MAXV)
            snprintf(sv.name[i], sizeof sv.name[i], "%s", name), sv.val[i] = v;
    }
    sv.nv = nv <= SV_MAXV ? (int)nv : 0;
    sv.lo = pr_f64(r);
    sv.hi = pr_f64(r);
    unsigned d = pr_u8(r);
    for (unsigned i = 0; i <= d; i++) {
        double v = pr_f64(r);
        if (d >= 1 && d <= PMAX)
            pr.a[i] = v;
    }
    if (d >= 1 && d <= PMAX)
        pr.deg = (int)d;
    unsigned m = pr_u8(r), n = pr_u8(r);
    if (m >= 1 && m <= LMAX && n >= 1 && n <= LMAX) {
        ls.m = (int)m, ls.n = (int)n;
        for (unsigned i = 0; i < m; i++)
            for (unsigned j = 0; j <= n; j++)
                ls.a[i][j] = pr_f64(r);
    }
    if (r->err)
        sv_defaults();
}
const persist_def_t persist_solver = {"solver", 1, svp_save, svp_load, sv_defaults};
