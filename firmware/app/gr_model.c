/* Graph modes and entries: FUNC (Y1..Y6 in g.f), PAR, POL, SEQ, IMPL, DE; compilation
 * with slider parameters, evaluation, and the saved state (persist_graphx). */
/*
 * The model behind every graphing screen (Y=, WINDOW, GRAPH, TABLE, CALC):
 *
 * Storage. FUNC entries are the six editors g.f[] of app_t (ui.h), shared with
 * the home screen's Y-VARS; the five other modes keep their six entries in
 * gx.ent[mode - 1][] as compact token strings (gtext_t: editor token ids, at
 * most GTOK tokens) so the inactive modes cost little RAM. PAR uses ex[0] =
 * x(t), ex[1] = y(t); SEQ ex[0] = u(n), ex[1] = initial values; DE ex[0] =
 * f(x, y) of y' = f(x, y), ex[1] = initial points; POL and IMPL only ex[0].
 * DE curves added on the graph are gx.dtap (gr_de.c). Colour, style and enable flag are per entry (FUNC: g.f[i] and
 * gx.fstyle[i]). The WINDOW ranges of the parametric modes (Tmin.., θmin..,
 * nMin..) live in gx; Xmin..Ymax in g.
 *
 * Compilation. Entries are compiled lazily by gx_compile() (FUNC by
 * funcs_compile()) into CAS compiled functions (cas_compile_vars): the CAS
 * parses the expression once and evaluates it in double precision quickly
 * (cas_fn_eval_vars) with an array of variable values. The variables are the
 * mode's independent variables followed by the active slider parameters, so
 * dragging a slider only changes an array element and never recompiles.
 * Any change to entries, mode or settings calls funcs_invalidate(), which
 * frees the compiled code and marks everything for recompilation.
 *   FUNC  f(x)                             vars: x, params
 *   PAR   x(t), y(t)                       vars: t, params
 *   POL   r(θ)                             vars: theta, params
 *   IMPL  "lhs REL rhs" -> (lhs)-(rhs)     vars: x, y, params (relation in run[i].rel)
 *   DE    f(x, y) of y' = f(x, y)              vars: x, y, params (initial points in run[i].ic)
 *   SEQ   u(n) with uK(n-1), uK(n-2) rewritten to variables useqK1, useqK2
 *                                          vars: n, useq11, useq12, ..., useq62, params
 *
 * Slider parameters. Free single-letter names in the entries (a*x^2+b) that
 * are not the mode's variables, e or i become parameters (gx.prm, at most
 * GNPARAM). Their values follow the CAS variable of the same name when it is
 * defined; a slider writes back to the CAS with gx_param_commit (a:=v).
 *
 * Evaluation. gx_f, gx_par, gx_pol_r, gx_impl and gx_seq* evaluate entry i of
 * the current mode, NAN where undefined. They write the shared static arrays
 * v1 / v2 / vs, so they are not reentrant (single-threaded main loop only).
 *
 * State: gx (graphx_t, saved by persist_graphx), run[] (compiled code and
 * per-entry run-time info, not saved), the parameter index (np, pidx) and the
 * evaluation arrays. The compiled functions belong to the CAS context g.cas.
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "anim.h" /* gx_scan_letters */
#include "graph.h"
#include "registry.h"
#include "session.h"

graphx_t gx;
editor_t gx_tmp_ed;

const char *const gmode_names[GM_NMODES] = {"FUNC", "PAR", "POL", "SEQ", "IMPL", "DE"};
const char *const gstyle_names[GS_NSTYLES] = {"line", "thick", "thin", "dotted", "above", "below"};

/* run-time state of the current mode's entries (not saved) */
typedef struct {
    cas_fn *fn[2];   /* compiled ex[0] / ex[1] (only PAR uses fn[1]); NULL if empty or in error */
    bool compiled;   /* compilation was attempted (successfully or not) since the last invalidation */
    char err[48];    /* compile error shown in Y= / on the graph ("" if none) */
    uint8_t rel;     /* IMPL */
    double init[2];  /* SEQ initial values */
    uint8_t ninit, depth; /* SEQ: number of initial values given; deepest lag used (0..2) */
    bool selfonly;   /* SEQ: uses only its own u(n-1) (web plot) */
    double ic[DE_NIC][2]; /* DE: the initial points (x0, y0) of the IC line */
    uint8_t nic;
} grun_t;
static grun_t run[GNENT];

/* active parameters (compile order) and the evaluation arrays: the independent
 * variables first, then the parameter values */
static int np, pidx[GNPARAM]; /* pidx[k] = index in gx.prm of the k-th compiled parameter */
static bool params_valid;     /* params_scan() ran since the last invalidation */
#define SEQ_NV (1 + 2 * GNENT) /* SEQ variables: n, then u_j(n-1), u_j(n-2) for each sequence j */
/* v1: one variable (x, t or θ) + params; v2: x, y + params (IMPL, DE); vs: SEQ */
static double v1[1 + GNPARAM], v2[2 + GNPARAM], vs[SEQ_NV + GNPARAM];

/* ------------------------------------------------------------ defaults */
static void params_reset(void)
{
    memset(gx.prm, 0, sizeof gx.prm);
    gx.sel_param = 0;
}

/* Factory settings of the graphing state (not Xmin..Ymax, which are in g):
 * FUNC mode, every entry empty and enabled, colours cycling through the
 * palette, line style (dots for sequences), T and θ from 0 to 2π in steps of
 * π/24, n from 1 to 10, no parameters. Does not touch compiled code. */
void graphx_defaults(void)
{
    gx.mode = GM_FUNC;
    gx.seqplot = SEQ_TIME;
    for (int i = 0; i < NFUNC; i++)
        gx.fstyle[i] = GS_LINE;
    for (int m = 0; m < GM_NMODES - 1; m++)
        for (int i = 0; i < GNENT; i++) {
            gentry_t *e = &gx.ent[m][i];
            memset(e, 0, sizeof *e);
            e->enabled = true;
            e->color = (uint8_t)(i % NCOLORS);
            e->style = m + 1 == GM_SEQ ? GS_DOT : GS_LINE;
        }
    gx.tmin = 0, gx.tmax = 2 * M_PI, gx.tstep = M_PI / 24;
    gx.thmin = 0, gx.thmax = 2 * M_PI, gx.thstep = M_PI / 24;
    gx.nmin = 1, gx.nmax = 10, gx.plotstart = 1, gx.plotstep = 1;
    gx.ndtap = 0;
    params_reset();
}

void graphx_init(void)
{
    /* the CAS was (re)created: compiled functions are gone, do not free them */
    memset(run, 0, sizeof run);
    params_valid = false;
    np = 0;
    graphx_defaults();
}

/* ------------------------------------------------------------ entries */
/* Accessors for entry i of the current mode; in FUNC mode they map to g.f[i]
 * and gx.fstyle[i]. gx_enabled/gx_color/gx_style expect a valid index. */
gentry_t *gx_entry(int i)
{
    if (gx.mode == GM_FUNC || i < 0 || i >= GNENT)
        return NULL;
    return &gx.ent[gx.mode - 1][i];
}

bool gx_enabled(int i) { return gx.mode == GM_FUNC ? g.f[i].enabled : gx_entry(i)->enabled; }
uint8_t gx_color(int i) { return gx.mode == GM_FUNC ? g.f[i].color : gx_entry(i)->color; }
uint8_t gx_style(int i) { return gx.mode == GM_FUNC ? gx.fstyle[i] : gx_entry(i)->style; }

/* set the line style of entry i (wraps around, so callers can cycle with +1 / -1) */
void gx_set_style(int i, int style)
{
    style = ((style % GS_NSTYLES) + GS_NSTYLES) % GS_NSTYLES;
    if (gx.mode == GM_FUNC)
        gx.fstyle[i] = (uint8_t)style;
    else
        gx_entry(i)->style = (uint8_t)style;
    g.graph_valid = false;
}

bool gtext_empty(const gtext_t *t) { return t->len == 0; }

/* Load a stored expression into an editor (cursor at the end). */
void gtext_to_editor(const gtext_t *t, editor_t *e)
{
    ed_clear(e);
    for (int k = 0; k < t->len; k++)
        e->t[k] = t->t[k];
    e->len = t->len;
    e->cur = e->len;
    e->scroll = 0;
}

/* Store an editor's tokens; false (t unchanged) if longer than GTOK tokens. */
bool gtext_from_editor(gtext_t *t, const editor_t *e)
{
    if (e->len > GTOK)
        return false;
    for (int k = 0; k < e->len; k++)
        t->t[k] = e->t[k];
    t->len = (uint8_t)e->len;
    return true;
}

/* Entry i has no expression (PAR: neither x(t) nor y(t); SEQ: no u(n), the
 * initial values do not count). */
bool gx_entry_empty(int i)
{
    if (gx.mode == GM_FUNC)
        return ed_empty(&g.f[i].ed);
    gentry_t *e = gx_entry(i);
    return gtext_empty(&e->ex[0]) && (gx.mode != GM_PAR || gtext_empty(&e->ex[1]));
}

/* Display name of part `part` (0 or 1) of entry i, written into buf (Y= labels,
 * error lines, trace labels). Returns buf. */
const char *gx_entry_label(int i, int part, char *buf, size_t n)
{
    switch (gx.mode) {
    case GM_PAR: snprintf(buf, n, part ? "Y%dT" : "X%dT", i + 1); break;
    case GM_POL: snprintf(buf, n, "r%d", i + 1); break;
    case GM_SEQ: snprintf(buf, n, part ? "u%d(nMin)" : "u%d(n)", i + 1); break;
    case GM_IMPL: snprintf(buf, n, "I%d", i + 1); break;
    case GM_DE: snprintf(buf, n, part ? "IC%d" : "y%d'", i + 1); break;
    default: snprintf(buf, n, "Y%d", i + 1); break;
    }
    return buf;
}

const char *gx_indep_name(void)
{
    static const char *const n[GM_NMODES] = {"x", "T", "θ", "n", "x", "x"};
    return n[gx.mode];
}

double gx_angle_unit(void) { return g.cs.angle == CAS_ANGLE_DEG ? M_PI / 180 : 1.0; }

/* Switch the graph mode (Y= header, MODE row GRAPH). The compiled code of the
 * old mode is freed before the switch (run[] belongs to the old mode's
 * entries); the same invalidation makes the new mode recompile and rescan its
 * parameters on the next gx_compile(). */
void gx_set_mode(int m)
{
    if (m < 0 || m >= GM_NMODES || m == gx.mode)
        return;
    funcs_invalidate();
    gx.mode = (uint8_t)m;
}

/* ------------------------------------------------------------ compilation */
/* gx_seq's resumable run (see gx_seq): valid while the compiled sequences,
 * nMin and the parameter values are those it was computed with */
static struct {
    bool valid;
    long nmin, n;                    /* run from nMin, terms up to n computed */
    double hist[GNENT][2], cur[GNENT]; /* u_j(n-1), u_j(n-2) before term n; u_j(n) */
} sq;

/* free the compiled code of the non-FUNC entries and clear their run-time state */
static void free_run(void)
{
    sq.valid = false;
    for (int i = 0; i < GNENT; i++) {
        for (int k = 0; k < 2; k++)
            if (run[i].fn[k])
                cas_fn_free(g.cas, run[i].fn[k]);
        memset(&run[i], 0, sizeof run[i]);
    }
}

/* Something that affects the graph changed (an entry, the mode, a setting such
 * as the angle unit, the CAS variables): free all compiled functions, force a
 * parameter rescan and mark the graph cache stale. Recompilation happens
 * lazily at the next gx_compile()/funcs_compile(). */
void funcs_invalidate(void)
{
    for (int i = 0; i < NFUNC; i++) {
        if (g.f[i].fn)
            cas_fn_free(g.cas, g.f[i].fn);
        g.f[i].fn = NULL;
        g.f[i].compiled = false;
        g.f[i].err[0] = 0;
    }
    free_run();
    params_valid = false;
    g.graph_valid = false;
}


static bool is_idch(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* single-letter names in CAS text that are not followed by '(' */
/* Calls cb for each such name (possibly several times for the same letter).
 * Longer identifiers (sin, theta, useq11) and numbers are skipped as a whole,
 * so the digits and exponent letter of 2.5E3 are not mistaken for names, and
 * one-letter function calls such as f(2) are not parameters. */
void gx_scan_letters(const char *s, void (*cb)(char c, void *u), void *u)
{
    for (const char *p = s; *p;) {
        if (isalpha((unsigned char)*p)) {
            const char *q = p;
            while (is_idch(*q))
                q++;
            const char *r = q;
            while (*r == ' ')
                r++;
            if (q - p == 1 && *r != '(')
                cb(*p, u);
            p = q;
        } else if (isdigit((unsigned char)*p)) { /* 2.5E3 etc.: skip the number */
            while (isdigit((unsigned char)*p) || *p == '.')
                p++;
        } else {
            p++;
        }
    }
}

/* Letters that are never parameters in `mode`: the constants e and i, and
 * the mode's own variables (θ is "theta" in CAS text, so POL excludes none). */
static bool excluded(char c, int mode)
{
    if (c == 'e' || c == 'i')
        return true;
    switch (mode) {
    case GM_FUNC: return c == 'x';
    case GM_PAR: return c == 't';
    case GM_POL: return false;
    case GM_SEQ: return c == 'n';
    case GM_IMPL:
    case GM_DE: return c == 'x' || c == 'y';
    }
    return false;
}

static int scan_mode; /* mode whose entries params_scan is reading (for excluded()) */
/* scan_letters callback: mark parameter c active, creating it if needed.
 * Slot choice for a new name: a free slot, else one whose parameter is no
 * longer referenced (its slider settings are forgotten). New parameters
 * start at 1 with range -10..10 and step 0.1. Names beyond GNPARAM active
 * parameters are ignored: they stay undefined and the compile reports them. */
static void add_param(char c, void *u)
{
    (void)u;
    if (excluded(c, scan_mode))
        return;
    /* a variable holding a list or matrix (a pdenum grid U in gridval(U,...,x,t)) is data, not a parameter */
    char q[16] = "type(?)", out[16];
    q[5] = c;
    if (g.cas && cas_eval_quiet(g.cas, q, out, sizeof out) == CAS_OK && (!strcmp(out, "\"matrix\"") || !strcmp(out, "\"vector\"")))
        return;
    for (int k = 0; k < GNPARAM; k++)
        if (gx.prm[k].used && gx.prm[k].name[0] == c) {
            gx.prm[k].active = true;
            return;
        }
    int slot = -1;
    for (int k = 0; k < GNPARAM && slot < 0; k++)
        if (!gx.prm[k].used)
            slot = k;
    for (int k = 0; k < GNPARAM && slot < 0; k++)
        if (!gx.prm[k].active)
            slot = k;
    if (slot < 0)
        return; /* too many: the name stays undefined */
    gparam_t *p = &gx.prm[slot];
    memset(p, 0, sizeof *p);
    p->name[0] = c;
    p->used = p->active = true;
    p->val = 1, p->min = -10, p->max = 10, p->step = 0.1;
}

static bool cur_shown; /* value mark_shown assigns (enabled state of the entry being scanned) */
/* scan_letters callback: show (or not) the slider of active parameter c */
static void mark_shown(char c, void *u)
{
    (void)u;
    if (excluded(c, gx.mode))
        return;
    for (int k = 0; k < GNPARAM; k++)
        if (gx.prm[k].used && gx.prm[k].active && gx.prm[k].name[0] == c)
            gx.prm[k].shown = cur_shown;
}

/* CAS input text of part `part` of entry i of `mode` (via the editor's
 * ed_cas conversion; non-FUNC entries go through the scratch gx_tmp_ed). */
static void entry_cas(int mode, int i, int part, char *out, size_t n)
{
    editor_t *e = &gx_tmp_ed;
    if (mode == GM_FUNC)
        e = &g.f[i].ed;
    else
        gtext_to_editor(&gx.ent[mode - 1][i].ex[part], e);
    ed_cas(e, out, n);
}

/* value of a CAS variable if it is defined and numeric */
static bool cas_value(const char *name, double *v)
{
    char err[64];
    return eval_number(name, v, err, sizeof err) == 0 && isfinite(*v);
}

/* Find the parameters: single letters in the FUNC entries and in the current
 * mode's entries. Values follow the CAS variables when they are defined. */
/* Steps: (1) mark every name referenced by any FUNC entry or any entry of the
 * current mode active (FUNC entries are always scanned because gx_compile
 * always compiles them, with the same parameter list); (2) mark as shown (slider visible) those referenced
 * by an enabled entry of the current mode; (3) build the compile order pidx
 * and take the values of defined CAS variables, widening the slider range to
 * include them; (4) keep the keyboard-selected slider on a shown one; (5) fill
 * the evaluation arrays. Evaluates CAS variables (eval_number), so it must
 * run with a live CAS; does not change ans. */
static void params_scan(void)
{
    char cas[ED_MAX * 4];
    for (int k = 0; k < GNPARAM; k++)
        gx.prm[k].active = gx.prm[k].shown = false;
    int modes[2] = {GM_FUNC, gx.mode};
    for (int mi = 0; mi < (gx.mode == GM_FUNC ? 1 : 2); mi++) {
        scan_mode = modes[mi];
        for (int i = 0; i < GNENT; i++)
            for (int part = 0; part < (scan_mode == GM_PAR || scan_mode == GM_SEQ ? 2 : 1); part++) {
                if (scan_mode == GM_SEQ && part == 1)
                    continue; /* initial values are numbers */
                entry_cas(scan_mode, i, part, cas, sizeof cas);
                gx_scan_letters(cas, add_param, NULL);
            }
    }
    /* sliders: parameters of the enabled entries of the current mode */
    for (int i = 0; i < GNENT; i++) {
        cur_shown = gx_enabled(i);
        if (!cur_shown)
            continue;
        for (int part = 0; part < 2; part++) {
            if (part == 1 && gx.mode != GM_PAR)
                break;
            entry_cas(gx.mode, i, part, cas, sizeof cas);
            gx_scan_letters(cas, mark_shown, NULL);
        }
    }
    np = 0;
    for (int k = 0; k < GNPARAM; k++)
        if (gx.prm[k].used && gx.prm[k].active) {
            gparam_t *p = &gx.prm[k];
            double v;
            if (cas_value(p->name, &v) && v != p->val) {
                p->val = v;
                if (v < p->min)
                    p->min = v;
                if (v > p->max)
                    p->max = v;
            }
            pidx[np++] = k;
        }
    if (gx.sel_param >= GNPARAM || !gx.prm[gx.sel_param].shown) {
        gx.sel_param = 0;
        for (int k = 0; k < GNPARAM; k++)
            if (gx.prm[k].shown) {
                gx.sel_param = (uint8_t)k;
                break;
            }
    }
    params_valid = true;
    gx_params_changed();
}

/* Copy the parameter values into the tail of each evaluation array (after the
 * independent variables) and mark the graph stale. No recompilation. */
void gx_params_changed(void)
{
    sq.valid = false;
    for (int k = 0; k < np; k++) {
        double v = gx.prm[pidx[k]].val;
        v1[1 + k] = v;
        v2[2 + k] = v;
        vs[SEQ_NV + k] = v;
    }
    g.graph_valid = false;
}

/* The user may have stored a parameter on the home screen (a:=3): pick up
 * changed CAS values of the active parameters (graph screen entry). Returns
 * true if any value changed (the graph must be re-plotted). Called when the
 * graph screen is entered. */
bool gx_params_sync(void)
{
    gx_params_flush(); /* values stepped with + / - win over the CAS's older ones */
    if (!params_valid)
        return false;
    bool changed = false;
    for (int k = 0; k < np; k++) {
        gparam_t *p = &gx.prm[pidx[k]];
        double v;
        if (cas_value(p->name, &v) && v != p->val) {
            p->val = v;
            if (v < p->min)
                p->min = v;
            if (v > p->max)
                p->max = v;
            changed = true;
        }
    }
    if (changed)
        gx_params_changed();
    return changed;
}

/* compile `expr` in the given independent variables plus the parameters */
/* The variable order must match the evaluation arrays (v1, v2, vs). On error
 * *out is NULL and err gets the CAS message. */
static cas_status compile(const char *expr, const char *const *ind, int nind, cas_fn **out, char *err, size_t errn)
{
    const char *vars[SEQ_NV + GNPARAM];
    for (int k = 0; k < nind; k++)
        vars[k] = ind[k];
    for (int k = 0; k < np; k++)
        vars[nind + k] = gx.prm[pidx[k]].name;
    cas_status st = cas_compile_vars(g.cas, expr, vars, nind + np, out);
    if (st != CAS_OK) {
        *out = NULL;
        const char *m = cas_last_error(g.cas);
        snprintf(err, errn, "%s", m && *m ? m : "error");
    }
    return st;
}

/* Compile the FUNC entries Y1..Y6 that are not compiled yet (cheap when
 * nothing changed). Defines the Y-VARS in the CAS first (yvars_sync) because
 * an entry may call another one (Y2 = Y1(x)^2). */
void funcs_compile(void)
{
    if (!params_valid)
        params_scan();
    static const char *const ind[] = {"x"};
    char cas[ED_MAX * 4];
    bool synced = false;
    for (int i = 0; i < NFUNC; i++) {
        func_t *f = &g.f[i];
        if (f->compiled)
            continue;
        f->compiled = true;
        f->err[0] = 0;
        if (ed_empty(&f->ed))
            continue;
        if (!synced) { /* Y1(x) in another function */
            yvars_sync();
            synced = true;
        }
        ed_cas(&f->ed, cas, sizeof cas);
        compile(cas, ind, 1, &f->fn, f->err, sizeof f->err);
    }
}

/* Yi+1(x) in double precision (NAN if not compiled or undefined at x). */
double func_eval(int i, double x)
{
    if (i < 0 || i >= NFUNC || !g.f[i].fn)
        return NAN;
    v1[0] = x;
    return cas_fn_eval_vars(g.f[i].fn, v1);
}

/* split "lhs REL rhs" at a top-level relation; returns REL_* and "(lhs)-(rhs)" */
/* Only the first relation outside parentheses/brackets counts (=, ==, <, <=,
 * >, >=). Without a relation the text is taken as "expr = 0". The curve is the
 * zero set of lhs - rhs and the inequality's region is where the sign of
 * lhs - rhs satisfies the relation (gr_plot.c). */
static int split_relation(const char *s, char *out, size_t n)
{
    int depth = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '(' || *p == '[')
            depth++;
        else if (*p == ')' || *p == ']')
            depth--;
        else if (!depth && (*p == '=' || *p == '<' || *p == '>')) {
            int rel = REL_EQ, len = p[0] == '=' && p[1] == '=' ? 2 : 1;
            if (*p == '<')
                rel = p[1] == '=' ? (len = 2, REL_LE) : REL_LT;
            else if (*p == '>')
                rel = p[1] == '=' ? (len = 2, REL_GE) : REL_GT;
            snprintf(out, n, "(%.*s)-(%s)", (int)(p - s), s, p + len);
            return rel;
        }
    }
    snprintf(out, n, "%s", s);
    return REL_EQ;
}

/* true if p continues an identifier (a letter before it in the same alphanumeric run) */
static bool in_ident(const char *s, const char *p)
{
    const char *q = p;
    while (q > s && is_idch(q[-1]))
        q--;
    return q < p && !isdigit((unsigned char)*q);
}

/* SEQ: uK(n-1) -> useqK1, uK(n-2) -> useqK2 (spaces removed first) */
static const char *const seq_vars[SEQ_NV] = {"n",      "useq11", "useq12", "useq21", "useq22", "useq31", "useq32",
                                             "useq41", "useq42", "useq51", "useq52", "useq61", "useq62"};
/* Rewrite sequence `self`'s formula so it can be compiled as a plain function
 * of the variables seq_vars: every uK(n-1) / uK(n-2) (K = 1..6, not inside a
 * longer identifier) becomes the variable useqK1 / useqK2. Outputs: *depth =
 * the largest lag used (how many initial values are needed), *selfonly =
 * the formula refers only to its own u(n-1) (the condition for a web plot). */
static void seq_rewrite(const char *in, char *out, size_t n, int self, uint8_t *depth, bool *selfonly)
{
    char s[ED_MAX * 4];
    size_t k = 0;
    for (const char *p = in; *p && k + 1 < sizeof s; p++)
        if (*p != ' ')
            s[k++] = *p;
    s[k] = 0;
    size_t o = 0;
    *depth = 0;
    *selfonly = true;
    bool any = false;
    for (const char *p = s; *p && o + 8 < n;) {
        if (p[0] == 'u' && p[1] >= '1' && p[1] <= '0' + GNENT && !in_ident(s, p) &&
            !strncmp(p + 2, "(n-", 3) && (p[5] == '1' || p[5] == '2') && p[6] == ')') {
            int which = p[1] - '1', lag = p[5] - '0';
            o += (size_t)snprintf(out + o, n - o, "useq%d%d", which + 1, lag);
            if (lag > *depth)
                *depth = (uint8_t)lag;
            if (which != self || lag != 1)
                *selfonly = false;
            any = true;
            p += 7;
            continue;
        }
        out[o++] = *p++;
    }
    out[o] = 0;
    if (!any)
        *selfonly = false;
}

/* initial values "1", "1,2", "{1,2}" */
/* Strips braces/brackets, splits at top-level commas and evaluates up to two
 * values numerically (expressions such as 1/2 are allowed). v[0] is
 * u(nMin), v[1] is u(nMin+1). Returns the count, or -1 with err set. */
static int parse_inits(const char *s, double *v, char *err, size_t errn)
{
    char b[ED_MAX * 4];
    size_t k = 0;
    for (const char *p = s; *p && k + 1 < sizeof b; p++)
        if (*p != '{' && *p != '}' && *p != '[' && *p != ']')
            b[k++] = *p;
    b[k] = 0;
    int nv = 0;
    char *p = b;
    while (*p && nv < 2) {
        char *c = p;
        int depth = 0;
        while (*c && !(*c == ',' && !depth)) {
            if (*c == '(')
                depth++;
            else if (*c == ')')
                depth--;
            c++;
        }
        char save = *c;
        *c = 0;
        if (eval_number(p, &v[nv], err, errn))
            return -1;
        nv++;
        if (!save)
            break;
        p = c + 1;
    }
    return nv;
}

/* DE initial points: "(0,1),(2,-1)", "{(0,1)}", or a bare y0 for the point (0, y0) */
/* Splits at top-level commas; an item in parentheses or brackets with a comma inside is a point
 * (x0, y0), any other item a value y0 at x0 = 0. Each number may be an expression (π/2, a CAS
 * variable). Up to DE_NIC points; returns the count, or -1 with err set. */
static int parse_ics(const char *s, double (*ic)[2], char *err, size_t errn)
{
    char b[ED_MAX * 4];
    size_t k = 0;
    for (const char *p = s; *p && k + 1 < sizeof b; p++)
        if (*p != ' ')
            b[k++] = *p;
    b[k] = 0;
    char *p = b;
    if (*p == '{' && k > 1 && b[k - 1] == '}')
        p++, b[k - 1] = 0;
    int n = 0;
    while (*p) {
        char *c = p, *mid = NULL;
        int depth = 0;
        for (; *c && !(*c == ',' && !depth); c++) {
            if (*c == '(' || *c == '[' || *c == '{')
                depth++;
            else if (*c == ')' || *c == ']' || *c == '}')
                depth--;
            else if (*c == ',' && depth == 1 && !mid)
                mid = c;
        }
        char save = *c;
        *c = 0;
        if (n == DE_NIC) {
            snprintf(err, errn, "at most %d initial points", DE_NIC);
            return -1;
        }
        size_t len = strlen(p);
        if (mid && (*p == '(' || *p == '[') && len > 2 && (p[len - 1] == ')' || p[len - 1] == ']')) {
            *mid = 0, p[len - 1] = 0;
            if (eval_number(p + 1, &ic[n][0], err, errn) || eval_number(mid + 1, &ic[n][1], err, errn))
                return -1;
        } else {
            ic[n][0] = 0;
            if (eval_number(p, &ic[n][1], err, errn))
                return -1;
        }
        if (!isfinite(ic[n][0]) || !isfinite(ic[n][1])) {
            snprintf(err, errn, "initial points must be numbers");
            return -1;
        }
        n++;
        if (!save)
            break;
        p = c + 1;
    }
    return n;
}

/* Compile everything the current mode needs: the FUNC entries (always; other
 * modes may use Y1(..)) and the current mode's entries that are not compiled
 * yet. Empty entries are not errors. Errors are stored per entry
 * (gx_error); an entry with an error has fn[0] == NULL and is not plottable. */
void gx_compile(void)
{
    funcs_compile();
    if (gx.mode == GM_FUNC)
        return;
    char cas[ED_MAX * 4], ex[ED_MAX * 4 + 16];
    for (int i = 0; i < GNENT; i++) {
        grun_t *r = &run[i];
        if (r->compiled)
            continue;
        r->compiled = true;
        r->err[0] = 0;
        gentry_t *e = gx_entry(i);
        switch (gx.mode) {
        case GM_PAR: {
            static const char *const ind[] = {"t"};
            bool e0 = gtext_empty(&e->ex[0]), e1 = gtext_empty(&e->ex[1]);
            if (e0 && e1)
                break;
            if (e0 || e1) {
                snprintf(r->err, sizeof r->err, "both x(t) and y(t) are needed");
                break;
            }
            for (int k = 0; k < 2; k++) {
                entry_cas(GM_PAR, i, k, cas, sizeof cas);
                if (compile(cas, ind, 1, &r->fn[k], r->err, sizeof r->err) != CAS_OK)
                    break;
            }
            break;
        }
        case GM_POL: {
            static const char *const ind[] = {"theta"};
            if (gtext_empty(&e->ex[0]))
                break;
            entry_cas(GM_POL, i, 0, cas, sizeof cas);
            compile(cas, ind, 1, &r->fn[0], r->err, sizeof r->err);
            break;
        }
        case GM_IMPL: {
            static const char *const ind[] = {"x", "y"};
            if (gtext_empty(&e->ex[0]))
                break;
            entry_cas(GM_IMPL, i, 0, cas, sizeof cas);
            r->rel = (uint8_t)split_relation(cas, ex, sizeof ex);
            compile(ex, ind, 2, &r->fn[0], r->err, sizeof r->err);
            break;
        }
        case GM_DE: { /* y' = f(x, y), then the initial points */
            static const char *const ind[] = {"x", "y"};
            if (gtext_empty(&e->ex[0]))
                break;
            entry_cas(GM_DE, i, 0, cas, sizeof cas);
            if (compile(cas, ind, 2, &r->fn[0], r->err, sizeof r->err) != CAS_OK)
                break;
            entry_cas(GM_DE, i, 1, cas, sizeof cas);
            int n = cas[0] ? parse_ics(cas, r->ic, r->err, sizeof r->err) : 0;
            if (n < 0) {
                cas_fn_free(g.cas, r->fn[0]);
                r->fn[0] = NULL;
                break;
            }
            r->nic = (uint8_t)n;
            break;
        }
        case GM_SEQ: {
            if (gtext_empty(&e->ex[0]))
                break;
            entry_cas(GM_SEQ, i, 0, cas, sizeof cas);
            seq_rewrite(cas, ex, sizeof ex, i, &r->depth, &r->selfonly);
            if (compile(ex, seq_vars, SEQ_NV, &r->fn[0], r->err, sizeof r->err) != CAS_OK)
                break;
            entry_cas(GM_SEQ, i, 1, cas, sizeof cas);
            /* a recursive formula needs as many initial values as its deepest lag */
            int n = cas[0] ? parse_inits(cas, r->init, r->err, sizeof r->err) : 0;
            if (n < 0) {
                cas_fn_free(g.cas, r->fn[0]);
                r->fn[0] = NULL;
                break;
            }
            r->ninit = (uint8_t)n;
            if (r->ninit < r->depth) {
                snprintf(r->err, sizeof r->err, "u%d(nMin) needs %d initial value%s", i + 1, r->depth,
                         r->depth > 1 ? "s" : "");
                cas_fn_free(g.cas, r->fn[0]);
                r->fn[0] = NULL;
            }
            break;
        }
        }
    }
}

/* Entry i is enabled and compiled without error (call gx_compile first). */
bool gx_plottable(int i)
{
    if (i < 0 || i >= GNENT)
        return false;
    if (gx.mode == GM_FUNC)
        return g.f[i].enabled && g.f[i].fn;
    return gx_entry(i)->enabled && run[i].fn[0] && (gx.mode != GM_PAR || run[i].fn[1]);
}

const char *gx_error(int i)
{
    if (gx.mode == GM_FUNC)
        return g.f[i].compiled ? g.f[i].err : "";
    return run[i].compiled ? run[i].err : "";
}

int gx_rel(int i) { return run[i].rel; }

/* ------------------------------------------------------------ evaluation */
double gx_f(int i, double x) { return func_eval(i, x); }

/* POL: r(θ) of entry i (θ in the MODE angle unit, as typed by the user) */
double gx_pol_r(int i, double th)
{
    if (!run[i].fn[0])
        return NAN;
    v1[0] = th;
    return cas_fn_eval_vars(run[i].fn[0], v1);
}

/* Point of curve i at parameter t: PAR (x(t), y(t)); POL (r cos θ, r sin θ)
 * with θ converted to radians for DEG mode. Returns false if the point is
 * undefined (*x, *y may then be NAN). */
bool gx_par(int i, double t, double *x, double *y)
{
    if (gx.mode == GM_POL) {
        double r = gx_pol_r(i, t), a = t * gx_angle_unit();
        *x = r * cos(a);
        *y = r * sin(a);
    } else {
        if (!run[i].fn[0] || !run[i].fn[1])
            return false;
        v1[0] = t;
        *x = cas_fn_eval_vars(run[i].fn[0], v1);
        *y = cas_fn_eval_vars(run[i].fn[1], v1);
    }
    return isfinite(*x) && isfinite(*y);
}

/* IMPL: lhs - rhs of relation i at (x, y); DE: the slope f(x, y) of y' = f(x, y);
 * caller must check gx_plottable(i) */
double gx_impl(int i, double x, double y)
{
    v2[0] = x;
    v2[1] = y;
    return cas_fn_eval_vars(run[i].fn[0], v2);
}

int gx_de_nic(int i) { return run[i].fn[0] ? run[i].nic : 0; }
void gx_de_ic(int i, int k, double *x, double *y)
{
    *x = run[i].ic[k][0];
    *y = run[i].ic[k][1];
}

int gx_seq_depth(int i) { return run[i].depth; }
bool gx_seq_selfonly(int i) { return run[i].selfonly; }

#define SEQ_MAX_STEPS 200000L /* refuse runs longer than this (time bound) */
/* One step of a run (gx_seq_run, gx_seq): term n (k = n - nMin) of every
 * sequence into cur[] from the history, then shift the history. */
static void seq_step(long n, long k, double hist[GNENT][2], double cur[GNENT])
{
    vs[0] = (double)n;
    for (int j = 0; j < GNENT; j++) {
        vs[1 + 2 * j] = hist[j][0];
        vs[2 + 2 * j] = hist[j][1];
    }
    for (int j = 0; j < GNENT; j++) {
        grun_t *r = &run[j];
        if (!r->fn[0])
            cur[j] = NAN;
        else if (k < r->depth)
            cur[j] = k < r->ninit ? r->init[k] : (double)NAN;
        else
            cur[j] = cas_fn_eval_vars(r->fn[0], vs);
    }
    for (int j = 0; j < GNENT; j++) {
        hist[j][1] = hist[j][0];
        hist[j][0] = cur[j];
    }
}

/* Iterate all six sequences together from nMin to n1 and call cb(n, u) for
 * n >= n0, with u[j] = u_j(n) (NAN for empty/erroneous sequences). Running
 * them together lets a sequence use another's previous terms (u2(n-1) in u1).
 * hist[j][0] / [1] hold u_j(n-1) / u_j(n-2) and are copied into vs before
 * each step; the first `depth` terms of a recursive sequence come from its
 * initial values. Cost is O(n1 - nMin) evaluations per call. Nothing is
 * called if n1 < nMin or the run would exceed SEQ_MAX_STEPS. */
void gx_seq_run(long n0, long n1, void (*cb)(long n, const double *u, void *user), void *user)
{
    long nmin = (long)gx.nmin;
    if (n1 < nmin || n1 - nmin > SEQ_MAX_STEPS)
        return;
    double hist[GNENT][2], cur[GNENT];
    for (int j = 0; j < GNENT; j++)
        hist[j][0] = hist[j][1] = NAN;
    for (long n = nmin; n <= n1; n++) {
        seq_step(n, n - nmin, hist, cur);
        if (n >= n0 && cb)
            cb(n, cur, user);
    }
}

/* u_i(n), NAN outside [nMin, nMin + SEQ_MAX_STEPS]. The run is resumable:
 * sq keeps the state after the last term computed, so increasing n (TABLE
 * rows, TRACE steps) continues from there; a smaller n, another nMin, a
 * recompilation or a parameter change (free_run, gx_params_changed) start it
 * again from nMin. */
double gx_seq(int i, long n)
{
    long nmin = (long)gx.nmin;
    if (n < nmin || n - nmin > SEQ_MAX_STEPS)
        return NAN;
    if (!sq.valid || sq.nmin != nmin || sq.n > n) {
        sq.valid = true, sq.nmin = nmin, sq.n = nmin - 1;
        for (int j = 0; j < GNENT; j++)
            sq.hist[j][0] = sq.hist[j][1] = NAN;
    }
    while (sq.n < n) {
        sq.n++;
        seq_step(sq.n, sq.n - nmin, sq.hist, sq.cur);
    }
    return sq.cur[i];
}

/* Web plot map F with u_i(n) = F(u_i(n-1)): evaluate the formula with its
 * own previous term set to `prev` and every other history variable NAN.
 * Only defined for "self only" sequences (otherwise NAN); n is fixed at
 * nMin+1, so formulas that also depend on n are drawn for that n only. */
double gx_seq_step(int i, double prev)
{
    grun_t *r = &run[i];
    if (!r->fn[0] || !r->selfonly)
        return NAN;
    vs[0] = gx.nmin + 1;
    for (int j = 1; j < SEQ_NV; j++)
        vs[j] = NAN;
    vs[1 + 2 * i] = prev;
    return cas_fn_eval_vars(r->fn[0], vs);
}

/* ------------------------------------------------------------ parameters */
/* number of sliders shown on the graph (parameters of enabled entries) */
int gx_nparams_shown(void)
{
    int n = 0;
    for (int k = 0; k < GNPARAM; k++)
        n += gx.prm[k].used && gx.prm[k].shown;
    return n;
}

/* gx.prm index of the idx-th shown slider (top to bottom), -1 if none */
int gx_param_shown(int idx)
{
    for (int k = 0; k < GNPARAM; k++)
        if (gx.prm[k].used && gx.prm[k].shown && idx-- == 0)
            return k;
    return -1;
}

/* Set parameter p from a slider drag or +/- step: clamp to [min, max], snap
 * to the grid min + k*step (stepping back one if rounding passed max), and
 * clean values within 1e-9 step of max or 0 so the readout shows exact
 * numbers. Updates the evaluation arrays only; the CAS variable is written
 * by gx_param_commit when the drag ends. */
void gx_param_set(int p, double v)
{
    gparam_t *q = &gx.prm[p];
    if (!isfinite(v))
        return;
    if (v < q->min)
        v = q->min;
    if (v > q->max)
        v = q->max;
    if (q->step > 0) {
        double k = round((v - q->min) / q->step), eps = q->step * 1e-9;
        v = q->min + k * q->step;
        if (v > q->max + eps)
            v = q->min + (k - 1) * q->step;
        if (fabs(v - q->max) < eps)
            v = q->max;
        if (fabs(v) < eps)
            v = 0;
    }
    q->val = v;
    gx_params_changed();
}

/* Store parameter p in its CAS variable (a:=v) without changing ans, so the
 * home screen sees the slider's value. */
void gx_param_commit(int p)
{
    char cmd[64], out[64];
    snprintf(cmd, sizeof cmd, "%s:=%.15g", gx.prm[p].name, gx.prm[p].val);
    cas_eval_quiet(g.cas, cmd, out, sizeof out);
    gx.prm[p].dirty = false;
}

/* Store the values the + / - keys changed (slider_step marks them dirty
 * instead of evaluating on every press). Called before anything reads the
 * CAS variables: home/RPN/preview evaluations (yvars_sync), entering the graph
 * (gx_params_sync) and the session save (gx_save, before the CAS blob). */
void gx_params_flush(void)
{
    for (int k = 0; k < GNPARAM; k++)
        if (gx.prm[k].used && gx.prm[k].dirty)
            gx_param_commit(k);
}

/* ------------------------------------------------------------ MODE row */
/* Settings row GRAPH (registered in mode_registry); saved with persist_graphx. */
static int gmode_get(void) { return gx.mode; }
static void gmode_set(int c) { gx_set_mode(c); }
const mode_row_t mode_graphmode = {"GRAPH", {"FUNC", "PAR", "POL", "SEQ", "IMPL", "DE"}, 6, gmode_get, gmode_set,
                                   "graph-modes"};

/* ------------------------------------------------------------ saved state */
/* Expressions are saved as text (ed_serialize), not as token ids, so the
 * session survives firmware updates that renumber tokens. */
static void put_text(pw_t *w, const gtext_t *t)
{
    char b[GTOK * 24];
    gtext_to_editor(t, &gx_tmp_ed);
    ed_serialize(&gx_tmp_ed, b, sizeof b);
    pw_str(w, b);
}

/* read an expression saved by put_text (too long: empty) */
static void get_text(pr_t *r, gtext_t *t)
{
    char b[GTOK * 24];
    pr_str(r, b, sizeof b);
    ed_deserialize(&gx_tmp_ed, b);
    if (!gtext_from_editor(t, &gx_tmp_ed))
        t->len = 0;
}

/* Blob "graphx" version 1 (the FUNC expressions, enables and colours are in
 * the "graph" blob of graph_state.c):
 *   u8 mode, u8 seqplot, u8 NFUNC, NFUNC x u8 FUNC style,
 *   u8 modes (5: PAR POL SEQ IMPL DE; 4 before DE mode), u8 entries per mode (6), then per entry:
 *       u8 enabled, u8 colour, u8 style, str ex[0], str ex[1]
 *   u8 count (10), f64 Tmin Tmax Tstep θmin θmax θstep nMin nMax PlotStart PlotStep
 *   u8 number of parameters, per parameter: u8 name, f64 val min max step
 *   (since DE mode, optional) u8 number of DE curves added on the graph, per curve: u8 entry, f64 x y
 * Every array is preceded by its length so gx_load can read blobs written
 * with other sizes, and the tail is optional: a blob from before DE mode
 * loads with DE's defaults, and older firmware ignores what it does not know
 * (its loader stops after the parameters, mode DE unknown to it reads as FUNC). */
static void gx_save(pw_t *w)
{
    gx_params_flush(); /* the CAS blob, saved later, then has the slider values */
    pw_u8(w, gx.mode);
    pw_u8(w, gx.seqplot);
    pw_u8(w, NFUNC);
    for (int i = 0; i < NFUNC; i++)
        pw_u8(w, gx.fstyle[i]);
    pw_u8(w, GM_NMODES - 1);
    pw_u8(w, GNENT);
    for (int m = 0; m < GM_NMODES - 1; m++)
        for (int i = 0; i < GNENT; i++) {
            const gentry_t *e = &gx.ent[m][i];
            pw_u8(w, e->enabled);
            pw_u8(w, e->color);
            pw_u8(w, e->style);
            put_text(w, &e->ex[0]);
            put_text(w, &e->ex[1]);
        }
    const double v[] = {gx.tmin, gx.tmax, gx.tstep, gx.thmin, gx.thmax, gx.thstep,
                        gx.nmin, gx.nmax, gx.plotstart, gx.plotstep};
    pw_u8(w, sizeof v / sizeof v[0]);
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++)
        pw_f64(w, v[i]);
    int n = 0;
    for (int k = 0; k < GNPARAM; k++)
        n += gx.prm[k].used;
    pw_u8(w, (uint32_t)n);
    for (int k = 0; k < GNPARAM; k++) {
        const gparam_t *p = &gx.prm[k];
        if (!p->used)
            continue;
        pw_u8(w, (uint8_t)p->name[0]);
        pw_f64(w, p->val);
        pw_f64(w, p->min);
        pw_f64(w, p->max);
        pw_f64(w, p->step);
    }
    pw_u8(w, gx.ndtap);
    for (int k = 0; k < gx.ndtap; k++) {
        pw_u8(w, gx.dtap[k].ent);
        pw_f64(w, gx.dtap[k].x);
        pw_f64(w, gx.dtap[k].y);
    }
}

/* Restore the blob written by gx_save, starting from the defaults. Defensive:
 * values are reduced to their valid ranges, entries/parameters are taken only
 * if they read without error, and the ranges only if they are all finite and
 * consistent (min < max, positive steps, PlotStep >= 1). Extra entries in a
 * larger blob are read and dropped. Ends with funcs_invalidate() so
 * everything recompiles. */
static void gx_load(pr_t *r, unsigned version)
{
    (void)version;
    graphx_defaults();
    unsigned mode = pr_u8(r), seqplot = pr_u8(r);
    unsigned nf = pr_u8(r);
    for (unsigned i = 0; i < nf; i++) {
        unsigned s = pr_u8(r);
        if (i < NFUNC)
            gx.fstyle[i] = (uint8_t)(s % GS_NSTYLES);
    }
    unsigned nm = pr_u8(r), ne = pr_u8(r);
    for (unsigned m = 0; m < nm && !r->err; m++)
        for (unsigned i = 0; i < ne && !r->err; i++) {
            gentry_t tmp;
            memset(&tmp, 0, sizeof tmp);
            tmp.enabled = pr_u8(r) != 0;
            tmp.color = (uint8_t)(pr_u8(r) % NCOLORS);
            tmp.style = (uint8_t)(pr_u8(r) % GS_NSTYLES);
            get_text(r, &tmp.ex[0]);
            get_text(r, &tmp.ex[1]);
            if (!r->err && m < GM_NMODES - 1 && i < GNENT)
                gx.ent[m][i] = tmp;
        }
    unsigned nv = pr_u8(r);
    double v[10];
    for (unsigned i = 0; i < nv; i++) {
        double x = pr_f64(r);
        if (i < 10)
            v[i] = x;
    }
    if (!r->err && nv >= 10) {
        bool ok = true;
        for (int i = 0; i < 10; i++)
            ok &= isfinite(v[i]) != 0;
        if (ok && v[0] < v[1] && v[2] > 0 && v[3] < v[4] && v[5] > 0 && v[6] <= v[7] && v[9] >= 1) {
            gx.tmin = v[0], gx.tmax = v[1], gx.tstep = v[2];
            gx.thmin = v[3], gx.thmax = v[4], gx.thstep = v[5];
            gx.nmin = v[6], gx.nmax = v[7], gx.plotstart = v[8], gx.plotstep = v[9];
        }
    }
    unsigned npm = pr_u8(r);
    for (unsigned k = 0; k < npm && !r->err; k++) {
        gparam_t p;
        memset(&p, 0, sizeof p);
        p.name[0] = (char)pr_u8(r);
        p.val = pr_f64(r), p.min = pr_f64(r), p.max = pr_f64(r), p.step = pr_f64(r);
        p.used = true;
        if (!r->err && k < GNPARAM && isalpha((unsigned char)p.name[0]) && isfinite(p.val) && p.min < p.max &&
            p.step >= 0)
            gx.prm[k] = p;
    }
    unsigned nt = pr_left(r) ? pr_u8(r) : 0; /* (absent before DE mode) */
    for (unsigned k = 0; k < nt && !r->err; k++) {
        unsigned e = pr_u8(r);
        double x = pr_f64(r), y = pr_f64(r);
        if (!r->err && e < GNENT && isfinite(x) && isfinite(y) && gx.ndtap < DE_NTAP) {
            gx.dtap[gx.ndtap].ent = (uint8_t)e;
            gx.dtap[gx.ndtap].x = x, gx.dtap[gx.ndtap].y = y;
            gx.ndtap++;
        }
    }
    if (mode < GM_NMODES)
        gx.mode = (uint8_t)mode;
    gx.seqplot = seqplot == SEQ_WEB ? SEQ_WEB : SEQ_TIME;
    funcs_invalidate();
}

/* MEM > Reset all */
static void gx_reset(void)
{
    graphx_defaults();
    funcs_invalidate();
}

const persist_def_t persist_graphx = {"graphx", 1, gx_save, gx_load, gx_reset};
