/*
 * STAT, LIST and DISTR menus and the input forms behind them: CALC (1-Var,
 * 2-Var, regressions), TESTS (hypothesis tests, intervals), list operations,
 * distribution shading. Computations: stat_math.c, cas/src/statdist.c.
 *
 * How the user gets here: APPS > Statistics opens the STAT menu (Edit lists,
 * CALC, TESTS, SortA/SortD, ClrList, SetUpEditor, Stat plots, CSV), the list
 * editor's soft keys (stat_edit.c) call stat_calc_open() & co. directly, and
 * APPS > Distributions opens DISTR (insert a distribution function into the
 * editor below, or DRAW: shaded curves).
 *
 * Flow: a menu item opens a form (sf_open, stat_form.c) described by a static
 * sfield_t table below. ENTER on "Calculate" / "Draw" calls act() with the
 * action code (1 = calculate, 2 = draw); act() reads the fields back by label
 * (N, CH_, L, LF helpers), calls the stat_math.c function and shows the result
 * screen (sr_show) or the shaded drawing (sd_draw_result). Input errors are
 * reported on the form (sf_error) and the form stays open.
 *
 * Side effects besides showing results: regressions write the residuals to
 * RESID and can store the equation into Y1..Y6 (RegEQ); the χ² GOF test
 * writes the list CNTRB; the 2-way χ² test defines the CAS matrix chi2exp;
 * sorting and ClrList change lists (sl_changed: formulas, CAS, graph).
 *
 * State: only the chosen regression (reg_type / reg_form) and the `ok` flag of
 * the current act() call; the field values belong to stat_form.c.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: size over speed (not time-critical) */
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cas_stat.h"
#include "registry.h"
#include "stat.h"

/* ------------------------------------------------------------ helpers */
/* Field access by label on the open form. The helpers share the flag `ok`:
 * act() sets it to true, and the first failing read clears it and puts its
 * message on the form (sf_num reports only while ok is still true, so the
 * user sees the first error). Later reads still run but their results are
 * ignored because act() checks `ok` before using them (finish()). */
static const sform_t *cur(void) { return sf_current(); }

/* index of the field labelled `label` in the open form, -1 if it has none */
static int fidx(const char *label)
{
    const sform_t *f = cur();
    for (int i = 0; i < f->n; i++)
        if (!strcmp(f->f[i].label, label))
            return i;
    return -1;
}

static bool ok;
/* numeric field (evaluated expression; NAN and ok = false on error) */
static double N(const char *label) { return sf_num(fidx(label), &ok); }
/* index of the selected option of a choice field */
static int CH_(const char *label) { return sf_choice(fidx(label)); }
/* list field: *v, *n = values and length of the named list. `opt`: a
 * frequency field where "1" or empty means "no list" (*v = NULL). Returns
 * false (and clears ok) on an unknown/empty list or an earlier error. */
static bool L(const char *label, const double **v, int *n, bool opt)
{
    if (!ok)
        return false;
    if (!sf_list(fidx(label), v, n, opt))
        ok = false;
    return ok;
}

/* "Xlist has 5 values, Ylist 6": the error for lists that must have the same
 * length (fields a and b, without their colons) */
static void len_error(const char *a, int na, const char *b, int nb)
{
    char m[64];
    snprintf(m, sizeof m, "%.*s has %d values, %.*s %d", (int)strcspn(a, ":"), a, na, (int)strcspn(b, ":"), b, nb);
    sf_error(m);
}

/* frequency list must match the data length
 * Reads the data list field xl and the optional frequency field fl; *f is
 * NULL when no frequency list is given. */
static bool LF(const char *xl, const char *fl, const double **x, const double **f, int *n)
{
    int nf;
    if (!L(xl, x, n, false) || !L(fl, f, &nf, true))
        return false;
    if (*f && nf != *n) {
        len_error(xl, *n, fl, nf);
        return ok = false;
    }
    return true;
}

/* true when the form has an "Input:" choice set to Data (lists) rather than
 * Stats (typed summary values) */
static bool data_input(void) { return fidx("Input:") >= 0 && CH_("Input:") == 0; }

/* sample k ("", "1", "2"): mean, Sx, n from a list or from the form
 * Data input: List<k>/Freq<k> fields summarised by st_summary. Stats input:
 * the xbar<k>, Sx<k> (only if the form has it: z procedures take σ instead,
 * *sx is left unchanged) and n<k> fields. Errors clear `ok`. */
static void sample(const char *k, double *xbar, double *sx, double *n)
{
    char a[16], b[16];
    if (data_input()) {
        const double *x, *f;
        int len;
        snprintf(a, sizeof a, "List%s:", k);
        snprintf(b, sizeof b, "Freq%s:", k);
        if (LF(a, b, &x, &f, &len) && st_summary(x, f, len, n, xbar, sx)) {
            sf_error("invalid data (frequencies must be >= 0)");
            ok = false;
        }
        return;
    }
    snprintf(a, sizeof a, "xbar%s:", k);
    *xbar = N(a);
    snprintf(a, sizeof a, "Sx%s:", k);
    if (fidx(a) >= 0)
        *sx = N(a);
    snprintf(a, sizeof a, "n%s:", k);
    *n = N(a);
}

/* Show the outcome of act(): nothing if an input error is already on the
 * form, the computation's error on the form, the shaded drawing for "Draw"
 * (act == 2; the result is also kept as stat_last for VARS), else the result
 * screen (with a draw option when the result has a distribution; sr_show
 * keeps stat_last itself). */
static void finish(sres_t *r, int act)
{
    if (!ok)
        return;
    if (r->err) {
        sf_error(r->err);
        return;
    }
    /* the note right of the result's title: the alternative hypothesis with
     * the hypothesised value filled in ("Ha: mu>5"), or the confidence level */
    char note[48] = "";
    int h = fidx("Ha:"), c = fidx("C-Level:");
    if (h >= 0) {
        const char *o = cur()->f[h].opts;
        for (int k = sf_choice(h); k > 0 && *o; o++)
            k -= *o == '|';
        int len = (int)strcspn(o, "|");
        int z = fidx("mu0:") >= 0 ? fidx("mu0:") : fidx("p0:");
        const char *ref = z >= 0 ? cur()->f[z].label : NULL; /* "mu0:" / "p0:" */
        int rl = ref ? (int)strlen(ref) - 1 : 0;
        if (ref && len > rl && !strncmp(o + len - rl, ref, (size_t)rl) && strlen(sf_text(z)) < 16)
            snprintf(note, sizeof note, "Ha: %.*s%s", len - rl, o, sf_text(z));
        else
            snprintf(note, sizeof note, "Ha: %.*s", len, o);
    } else if (c >= 0) {
        snprintf(note, sizeof note, "C-Level %.20s", sf_text(c));
    }
    sr_note(note);
    if (act == 2)
        sd_draw_result(r), stat_last = *r;
    else
        sr_show(r, r->dist != DIST_NONE);
}

/* store an equation into Y1..Y6 (choice 1..6; 0 = none)
 * `eq` is CAS text (st_reg_expr); it is converted to the editor's display
 * form, enabled, and the graph and the Y1(..) CAS functions are invalidated
 * so they are recompiled / redefined before next use. */
static void store_eq(int y, const char *eq)
{
    if (y < 1 || y > NFUNC || !eq[0])
        return;
    char b[200];
    pretty_from_cas(eq, b, sizeof b);
    func_t *fn = &g.f[y - 1];
    ed_set_text(&fn->ed, b);
    fn->enabled = true;
    funcs_invalidate();
    yvars_invalidate();
}

/* ------------------------------------------------------------ forms */
/* Field shorthands (sfield_t = label, type, cond, cond_val, act, opts/default):
 *   DATA_STATS  the "Input:" choice, always field 0 when present
 *   D(...)      list field shown only for Input = Data (cond 1 -> field 0 == 0)
 *   S(...)      number field shown only for Input = Stats (field 0 == 1)
 *   NUMF/LISTF/CHOICE  unconditional fields
 *   CALC / DRAW action lines (act code 1 / 2, see finish())
 *   YCHOICE     RegEQ: "-" (none) or Y1..Y6 to store the fitted equation
 * act() finds fields by label, so the labels below are also the keys used
 * there ("List1:", "xbar1:", ...); the alternative hypothesis is always the
 * choice "Ha:" (finish() shows it with the result), its options in ALT_NE /
 * ALT_LT / ALT_GT order and spelled out ("mu≠mu0"), so the row reads like
 * the hypothesis. */
#define DATA_STATS {"Input:", SF_CHOICE, 0, 0, 0, "Data|Stats"}
#define D(lab, def) {lab, SF_LIST, 1, 0, 0, def}
#define S(lab, def) {lab, SF_NUM, 1, 1, 0, def}
#define NUMF(lab, def) {lab, SF_NUM, 0, 0, 0, def}
#define LISTF(lab, def) {lab, SF_LIST, 0, 0, 0, def}
#define CHOICE(lab, opts) {lab, SF_CHOICE, 0, 0, 0, opts}
#define CALC {"Calculate", SF_ACTION, 0, 0, 1, NULL}
#define DRAW {"Draw", SF_ACTION, 0, 0, 2, NULL}
#define YCHOICE CHOICE("RegEQ->", "-|Y1|Y2|Y3|Y4|Y5|Y6")

/* form ids (sform_t.id): what act() computes; also the menu item actions of
 * the CALC / TESTS / DRAW menus */
enum {
    T_1VAR = 1, T_2VAR, T_REG, T_Z, T_T, T_2Z, T_2T, T_1P, T_2P, T_GOF, T_CHI2, T_2F, T_LINT, T_ANOVA,
    T_ZI, T_TI, T_2ZI, T_2TI, T_1PI, T_2PI, T_SNORM, T_ST, T_SCHI2, T_SF, T_SORTA, T_SORTD, T_CLR
};

static void act(int a);

static const sfield_t f_1var[] = {LISTF("List:", "L1"), LISTF("Freq:", "1"), CALC};
static const sfield_t f_2var[] = {LISTF("Xlist:", "L1"), LISTF("Ylist:", "L2"), LISTF("Freq:", "1"), CALC};
static const sfield_t f_reg[] = {LISTF("Xlist:", "L1"), LISTF("Ylist:", "L2"), LISTF("Freq:", "1"), YCHOICE,
                                 CALC};
static const sfield_t f_z[] = {DATA_STATS, NUMF("mu0:", "0"), NUMF("sigma:", "1"), D("List:", "L1"),
                               D("Freq:", "1"), S("xbar:", "0"), S("n:", ""), CHOICE("Ha:", "mu≠mu0|mu<mu0|mu>mu0"),
                               CALC, DRAW};
static const sfield_t f_t[] = {DATA_STATS, NUMF("mu0:", "0"), D("List:", "L1"), D("Freq:", "1"), S("xbar:", "0"),
                               S("Sx:", ""), S("n:", ""), CHOICE("Ha:", "mu≠mu0|mu<mu0|mu>mu0"), CALC, DRAW};
static const sfield_t f_2z[] = {DATA_STATS, NUMF("sigma1:", "1"), NUMF("sigma2:", "1"), D("List1:", "L1"),
                                D("List2:", "L2"), D("Freq1:", "1"), D("Freq2:", "1"), S("xbar1:", "0"),
                                S("n1:", ""), S("xbar2:", "0"), S("n2:", ""), CHOICE("Ha:", "mu1≠mu2|mu1<mu2|mu1>mu2"),
                                CALC, DRAW};
static const sfield_t f_2t[] = {DATA_STATS, D("List1:", "L1"), D("List2:", "L2"), D("Freq1:", "1"),
                                D("Freq2:", "1"), S("xbar1:", "0"), S("Sx1:", ""), S("n1:", ""),
                                S("xbar2:", "0"), S("Sx2:", ""), S("n2:", ""), CHOICE("Ha:", "mu1≠mu2|mu1<mu2|mu1>mu2"),
                                CHOICE("Pooled:", "No|Yes"), CALC, DRAW};
static const sfield_t f_1p[] = {NUMF("p0:", "0.5"), NUMF("x:", ""), NUMF("n:", ""), CHOICE("Ha:", "p≠p0|p<p0|p>p0"),
                                CALC, DRAW};
static const sfield_t f_2p[] = {NUMF("x1:", ""), NUMF("n1:", ""), NUMF("x2:", ""), NUMF("n2:", ""),
                                CHOICE("Ha:", "p1≠p2|p1<p2|p1>p2"), CALC, DRAW};
static const sfield_t f_gof[] = {LISTF("Observed:", "L1"), LISTF("Expected:", "L2"), NUMF("df:", ""), CALC, DRAW};
static const sfield_t f_chi2[] = {{"Observed:", SF_TEXT, 0, 0, 0, "[[10,20],[30,40]]"}, CALC, DRAW};
static const sfield_t f_2f[] = {DATA_STATS, D("List1:", "L1"), D("List2:", "L2"), D("Freq1:", "1"),
                                D("Freq2:", "1"), S("Sx1:", ""), S("n1:", ""), S("Sx2:", ""), S("n2:", ""),
                                CHOICE("Ha:", "sd1≠sd2|sd1<sd2|sd1>sd2"), CALC, DRAW};
static const sfield_t f_lint[] = {LISTF("Xlist:", "L1"), LISTF("Ylist:", "L2"), LISTF("Freq:", "1"),
                                  CHOICE("Ha:", "slope≠0|slope<0|slope>0"), YCHOICE, CALC};
static const sfield_t f_anova[] = {{"Lists:", SF_TEXT, 0, 0, 0, "L1,L2,L3"}, CALC};
static const sfield_t f_zi[] = {DATA_STATS, NUMF("sigma:", "1"), D("List:", "L1"), D("Freq:", "1"),
                                S("xbar:", "0"), S("n:", ""), NUMF("C-Level:", "0.95"), CALC};
static const sfield_t f_ti[] = {DATA_STATS, D("List:", "L1"), D("Freq:", "1"), S("xbar:", "0"), S("Sx:", ""),
                                S("n:", ""), NUMF("C-Level:", "0.95"), CALC};
static const sfield_t f_2zi[] = {DATA_STATS, NUMF("sigma1:", "1"), NUMF("sigma2:", "1"), D("List1:", "L1"),
                                 D("List2:", "L2"), D("Freq1:", "1"), D("Freq2:", "1"), S("xbar1:", "0"),
                                 S("n1:", ""), S("xbar2:", "0"), S("n2:", ""), NUMF("C-Level:", "0.95"), CALC};
static const sfield_t f_2ti[] = {DATA_STATS, D("List1:", "L1"), D("List2:", "L2"), D("Freq1:", "1"),
                                 D("Freq2:", "1"), S("xbar1:", "0"), S("Sx1:", ""), S("n1:", ""),
                                 S("xbar2:", "0"), S("Sx2:", ""), S("n2:", ""), NUMF("C-Level:", "0.95"),
                                 CHOICE("Pooled:", "No|Yes"), CALC};
static const sfield_t f_1pi[] = {NUMF("x:", ""), NUMF("n:", ""), NUMF("C-Level:", "0.95"), CALC};
static const sfield_t f_2pi[] = {NUMF("x1:", ""), NUMF("n1:", ""), NUMF("x2:", ""), NUMF("n2:", ""),
                                 NUMF("C-Level:", "0.95"), CALC};
static const sfield_t f_snorm[] = {NUMF("lower:", "-1"), NUMF("upper:", "1"), NUMF("mu:", "0"),
                                   NUMF("sigma:", "1"), DRAW};
static const sfield_t f_st[] = {NUMF("lower:", "-1"), NUMF("upper:", "1"), NUMF("df:", ""), DRAW};
static const sfield_t f_sf[] = {NUMF("lower:", "-1"), NUMF("upper:", "1"), NUMF("df1:", ""), NUMF("df2:", ""),
                                DRAW};
static const sfield_t f_sort[] = {LISTF("List:", "L1"), {"Carry:", SF_TEXT, 0, 0, 0, ""},
                                  {"Sort", SF_ACTION, 0, 0, 1, NULL}};
static const sfield_t f_clr[] = {{"Lists:", SF_TEXT, 0, 0, 0, "L1,L2,L3"}, {"Clear", SF_ACTION, 0, 0, 1, NULL}};

/* All forms, looked up by id in open_form(). Shade_t and Shadechi² share
 * f_st (lower, upper, df); SortA and SortD share f_sort. The regression form
 * is not here: its title depends on the model (reg_form below). */
#define FORM(id, title, arr, help) {title, arr, (int)(sizeof arr / sizeof arr[0]), act, help, id}
static const sform_t forms[] = {
    FORM(T_1VAR, "1-Var Stats", f_1var, "stat-calc"),
    FORM(T_2VAR, "2-Var Stats", f_2var, "stat-calc"),
    FORM(T_REG, "Regression", f_reg, "stat-regression"),
    FORM(T_Z, "Z-Test", f_z, "stat-tests"),
    FORM(T_T, "T-Test", f_t, "stat-tests"),
    FORM(T_2Z, "2-SampZTest", f_2z, "stat-tests"),
    FORM(T_2T, "2-SampTTest", f_2t, "stat-tests"),
    FORM(T_1P, "1-PropZTest", f_1p, "stat-tests"),
    FORM(T_2P, "2-PropZTest", f_2p, "stat-tests"),
    FORM(T_GOF, "chi² GOF-Test", f_gof, "stat-tests"),
    FORM(T_CHI2, "chi²-Test (2-way)", f_chi2, "stat-tests"),
    FORM(T_2F, "2-SampFTest", f_2f, "stat-tests"),
    FORM(T_LINT, "LinRegTTest", f_lint, "stat-tests"),
    FORM(T_ANOVA, "ANOVA", f_anova, "stat-tests"),
    FORM(T_ZI, "ZInterval", f_zi, "stat-intervals"),
    FORM(T_TI, "TInterval", f_ti, "stat-intervals"),
    FORM(T_2ZI, "2-SampZInt", f_2zi, "stat-intervals"),
    FORM(T_2TI, "2-SampTInt", f_2ti, "stat-intervals"),
    FORM(T_1PI, "1-PropZInt", f_1pi, "stat-intervals"),
    FORM(T_2PI, "2-PropZInt", f_2pi, "stat-intervals"),
    FORM(T_SNORM, "ShadeNorm", f_snorm, "stat-distr"),
    FORM(T_ST, "Shade_t", f_st, "stat-distr"),
    FORM(T_SCHI2, "Shadechi²", f_st, "stat-distr"),
    FORM(T_SF, "ShadeF", f_sf, "stat-distr"),
    FORM(T_SORTA, "SortA", f_sort, "stat-edit"),
    FORM(T_SORTD, "SortD", f_sort, "stat-edit"),
    FORM(T_CLR, "ClrList", f_clr, "stat-edit"),
};
#define NFORMS ((int)(sizeof forms / sizeof forms[0]))

static int reg_type;     /* regression chosen in the CALC menu */
static char reg_xl[SL_NAME], reg_yl[SL_NAME]; /* lists of the last fit (for the plot soft key) */
static int reg_y;        /* Y= function holding its equation (1..6), 0 = none */
static char reg_title[24];
static sform_t reg_form; /* f_reg with the regression's title */

/* open the form with id T_* (no-op for an unknown id) */
static void open_form(int id)
{
    for (int i = 0; i < NFORMS; i++)
        if (forms[i].id == id) {
            sf_open(&forms[i]);
            return;
        }
}

/* split "L1,L2,L3" into list pointers
 * Returns the number of lists (at most `max`), or -1 with the error shown on
 * the form when a name is unknown. */
static int name_lists(const char *s, slist_t **out, int max)
{
    int slots[SL_MAX];
    char err[48];
    int n = sl_parse(s, slots, max < SL_MAX ? max : SL_MAX, err, sizeof err);
    if (n < 0)
        sf_error(err);
    for (int i = 0; i < n; i++)
        out[i] = sl_at(slots[i]);
    return n;
}

/* sorting: key list ascending/descending, carrying other lists
 * (SortA/SortD with "Carry:" lists keep rows of paired data together).
 * 1. sort an index permutation idx[] (stored as doubles in st_scratch) by
 *    the key values with a stable insertion sort, so equal keys keep their
 *    order;
 * 2. apply the permutation to the key list and to every carried list of the
 *    same length (others are skipped), through a temporary copy;
 * 3. the sorted lists lose their column formulas (their values no longer
 *    follow the formula), then sl_changed updates dependents, the CAS and
 *    the graph.
 * Uses 2n doubles of scratch (always available: the scratch holds 3 x the
 * list capacity). */
static void sort_lists(slist_t *key, slist_t **carry, int nc, bool desc)
{
    int n = key->len;
    if (n > st_scratch_cap)
        return;
    /* selection by insertion sort of an index permutation in scratch */
    double *idx = st_scratch;
    for (int i = 0; i < n; i++)
        idx[i] = i;
    for (int i = 1; i < n; i++) { /* stable insertion sort (n <= 999) */
        double t = idx[i];
        int j = i - 1;
        while (j >= 0 && (desc ? key->v[(int)idx[j]] < key->v[(int)t] : key->v[(int)idx[j]] > key->v[(int)t])) {
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = t;
    }
    double *tmp = st_scratch + n;
    if (2 * n > st_scratch_cap)
        return;
    slist_t *all[SL_MAX + 1];
    all[0] = key;
    for (int c = 0; c < nc; c++)
        all[c + 1] = carry[c];
    for (int c = 0; c <= nc; c++) {
        slist_t *l = all[c];
        if (l->len != n)
            continue;
        for (int i = 0; i < n; i++)
            tmp[i] = l->v[(int)idx[i]];
        memcpy(l->v, tmp, sizeof(double) * (size_t)n);
        l->formula[0] = 0;
    }
    sl_changed(key);
}

/* ------------------------------------------------------------ actions */
/* Soft key plot on a regression result: the data and the fitted curve on
 * the graph in one step. The equation goes to the RegEQ function, or, if
 * none was chosen, to the first empty Y= function (nothing is overwritten;
 * with all six in use only the points are plotted); then stat_plot_show()
 * draws Xlist/Ylist as a scatter plot in a fitted window. */
static void reg_plot(void)
{
    if (!reg_y)
        for (int i = 0; i < NFUNC && !reg_y; i++)
            if (ed_empty(&g.f[i].ed)) {
                reg_y = i + 1;
                store_eq(reg_y, stat_last.eq);
            }
    stat_plot_show(SP_SCATTER, reg_xl, reg_yl);
}

/* "Calculate" on the regression form: fit model reg_type to Xlist/Ylist
 * (with Freq), remember the fit for VARS (stat_last_reg), write the
 * residuals y - f(x) to RESID (truncated to the list capacity), store RegEQ
 * into the chosen Y= function and show the coefficients. */
static void do_regression(int act_code)
{
    const double *x, *y, *f;
    int n, ny;
    if (!LF("Xlist:", "Freq:", &x, &f, &n) || !L("Ylist:", &y, &ny, false))
        return;
    if (ny != n) {
        len_error("Xlist", n, "Ylist", ny);
        return;
    }
    sres_t r;
    sreg_t gg;
    if (st_regress(reg_type, x, y, f, n, &gg, &r)) {
        sf_error(r.err);
        return;
    }
    stat_last_reg = gg;
    stat_have_reg = true;
    /* residuals */
    slist_t *res = sl_at(SL_RESID);
    for (int i = 0; i < n && i < sl_cap(); i++)
        res->v[i] = y[i] - st_reg_eval(&gg, x[i]);
    res->len = n < sl_cap() ? n : sl_cap();
    reg_y = CH_("RegEQ->");
    store_eq(reg_y, r.eq);
    sl_changed(res);
    (void)act_code;
    snprintf(reg_xl, sizeof reg_xl, "%s", sf_text(fidx("Xlist:")));
    snprintf(reg_yl, sizeof reg_yl, "%s", sf_text(fidx("Ylist:")));
    char note[24] = "";
    if (reg_y)
        snprintf(note, sizeof note, "RegEQ in Y%d", reg_y);
    sr_note(note);
    sr_set_plot(reg_plot);
    sr_show(&r, false);
}

/* Action callback of every form (sform_t.action): a = 1 "Calculate" (or the
 * Sort / Clear line), 2 "Draw". Dispatches on the open form's id: reads the
 * inputs, runs the computation into r and lets finish() show it. Forms whose
 * outcome is not a result screen (regression, DISTR DRAW shading, sorting,
 * ClrList) return directly. */
static void act(int a)
{
    const sform_t *F = cur();
    sres_t r;
    memset(&r, 0, sizeof r);
    ok = true;
    double x1, s1 = DNAN, n1, x2, s2 = DNAN, n2;
    switch (F->id) {
    case T_1VAR: case T_2VAR: {
        const double *x, *y, *f;
        int n, ny;
        if (!LF(F->id == T_1VAR ? "List:" : "Xlist:", "Freq:", &x, &f, &n))
            return;
        if (F->id == T_1VAR) {
            st_onevar(x, f, n, &r);
        } else {
            if (!L("Ylist:", &y, &ny, false))
                return;
            if (ny != n) {
                len_error("Xlist", n, "Ylist", ny);
                return;
            }
            st_twovar(x, y, f, n, &r);
        }
        break;
    }
    case T_REG: do_regression(a); return;
    case T_Z: case T_ZI: {
        double sig = N("sigma:");
        sample("", &x1, &s1, &n1);
        if (!ok)
            return;
        if (F->id == T_Z)
            st_ztest(N("mu0:"), sig, x1, n1, CH_("Ha:"), &r);
        else
            st_zint(sig, x1, n1, N("C-Level:"), &r);
        break;
    }
    case T_T: case T_TI:
        sample("", &x1, &s1, &n1);
        if (!ok)
            return;
        if (F->id == T_T)
            st_ttest(N("mu0:"), x1, s1, n1, CH_("Ha:"), &r);
        else
            st_tint(x1, s1, n1, N("C-Level:"), &r);
        break;
    case T_2Z: case T_2ZI: {
        double a1 = N("sigma1:"), a2 = N("sigma2:");
        sample("1", &x1, &s1, &n1);
        sample("2", &x2, &s2, &n2);
        if (!ok)
            return;
        if (F->id == T_2Z)
            st_2sampz(a1, a2, x1, n1, x2, n2, CH_("Ha:"), &r);
        else
            st_2sampzint(a1, a2, x1, n1, x2, n2, N("C-Level:"), &r);
        break;
    }
    case T_2T: case T_2TI: case T_2F:
        /* the F test uses only Sx and n of the samples */
        sample("1", &x1, &s1, &n1);
        sample("2", &x2, &s2, &n2);
        if (!ok)
            return;
        if (F->id == T_2T)
            st_2sampt(x1, s1, n1, x2, s2, n2, CH_("Ha:"), CH_("Pooled:"), &r);
        else if (F->id == T_2TI)
            st_2samptint(x1, s1, n1, x2, s2, n2, N("C-Level:"), CH_("Pooled:"), &r);
        else
            st_2sampf(s1, n1, s2, n2, CH_("Ha:"), &r);
        break;
    case T_1P: case T_1PI: {
        /* fields read in form order (one per statement), so the error shown is the first bad field's */
        double p0 = F->id == T_1P ? N("p0:") : 0;
        x1 = N("x:"), n1 = N("n:");
        if (F->id == T_1P)
            st_1propz(p0, x1, n1, CH_("Ha:"), &r);
        else
            st_1propzint(x1, n1, N("C-Level:"), &r);
        break;
    }
    case T_2P: case T_2PI:
        x1 = N("x1:"), n1 = N("n1:"), x2 = N("x2:"), n2 = N("n2:");
        if (F->id == T_2P)
            st_2propz(x1, n1, x2, n2, CH_("Ha:"), &r);
        else
            st_2propzint(x1, n1, x2, n2, N("C-Level:"), &r);
        break;
    case T_GOF: {
        const double *o, *e;
        int no, ne;
        double df = N("df:");
        if (!L("Observed:", &o, &no, false) || !L("Expected:", &e, &ne, false))
            return;
        if (no != ne) {
            len_error("Observed", no, "Expected", ne);
            return;
        }
        /* per-category contributions go straight into the named list CNTRB
         * (created if needed; its capacity is >= no since o is a list) */
        slist_t *c = sl_create("CNTRB");
        double *contrib = c ? c->v : NULL;
        if (st_chi2gof(o, e, no, df, &r, contrib) == 0 && c) {
            c->len = no;
            sl_changed(c);
        }
        break;
    }
    case T_CHI2: {
        /* the observed matrix: any CAS expression giving [[...],...]
         * 1. evaluate approx(expr) quietly (ans unchanged) in the shared
         *    stat text buffer, after defining the lists it mentions
         *    (stat_pre_eval), so e.g. a matrix built from lists works;
         * 2. parse the "[[a,b,...],[c,d,...]]" text row by row with strtod
         *    into st_scratch (row-major), checking equal row lengths;
         * 3. run the test with the expected counts written after the
         *    observed ones in the scratch, and define them in the CAS as
         *    chi2exp (full-precision text) for the user to inspect. */
        size_t outn;
        char *out = stat_text(&outn);
        editor_t e;
        ed_set_text(&e, sf_text(fidx("Observed:")));
        char cas[ED_MAX * 4 + 16];
        ed_cas(&e, cas, sizeof cas);
        char cmd[ED_MAX * 4 + 32];
        snprintf(cmd, sizeof cmd, "approx(%s)", cas);
        stat_pre_eval(cas);
        if (cas_eval_quiet(g.cas, cmd, out, outn) != CAS_OK || out[0] != '[' || out[1] != '[') {
            sf_error("Observed must be a matrix [[a,b],[c,d]]");
            return;
        }
        double *m = st_scratch;
        int rows = 0, cols = 0, k = 0;
        const char *p = out + 1;
        while (*p == '[') {
            int c = 0;
            p++;
            while (*p && *p != ']') {
                char *end;
                double v = strtod(p, &end);
                if (end == p || k >= st_scratch_cap) {
                    sf_error("matrix entries must be numbers");
                    return;
                }
                m[k++] = v, c++;
                p = end;
                if (*p == ',')
                    p++;
            }
            if (rows && c != cols) {
                sf_error("rows of different lengths");
                return;
            }
            cols = c, rows++;
            if (*p == ']')
                p++;
            if (*p == ',')
                p++;
        }
        double *expd = st_scratch + k;
        if (st_chi2test(m, rows, cols, &r, 2 * k <= st_scratch_cap ? expd : NULL) == 0 && 2 * k <= st_scratch_cap) {
            /* expected counts as the CAS matrix chi2exp */
            size_t o = (size_t)snprintf(out, outn, "chi2exp:=[");
            for (int i = 0; i < rows && o < outn - 64; i++) {
                out[o++] = '[';
                for (int j = 0; j < cols && o < outn - 48; j++) {
                    char b[40];
                    st_num_text(b, sizeof b, expd[i * cols + j]);
                    o += (size_t)snprintf(out + o, outn - o, j ? ",%s" : "%s", b);
                }
                o += (size_t)snprintf(out + o, outn - o, i < rows - 1 ? "]," : "]]");
            }
            char tmp[8];
            cas_eval_quiet(g.cas, out, tmp, sizeof tmp);
        }
        break;
    }
    case T_LINT: {
        const double *x, *y, *f;
        int n, ny;
        if (!LF("Xlist:", "Freq:", &x, &f, &n) || !L("Ylist:", &y, &ny, false))
            return;
        if (n != ny) {
            len_error("Xlist", n, "Ylist", ny);
            return;
        }
        if (st_linregt(x, y, f, n, CH_("Ha:"), &r) == 0) {
            reg_y = CH_("RegEQ->");
            store_eq(reg_y, r.eq);
            snprintf(reg_xl, sizeof reg_xl, "%s", sf_text(fidx("Xlist:")));
            snprintf(reg_yl, sizeof reg_yl, "%s", sf_text(fidx("Ylist:")));
            sr_set_plot(reg_plot);
        }
        break;
    }
    case T_ANOVA: {
        slist_t *ls[SL_MAX];
        int k = name_lists(sf_text(fidx("Lists:")), ls, SL_MAX);
        if (k < 0)
            return;
        const double *d[SL_MAX];
        int len[SL_MAX];
        for (int i = 0; i < k; i++)
            d[i] = ls[i]->v, len[i] = ls[i]->len;
        st_anova(d, len, k, &r);
        break;
    }
    case T_SNORM: case T_ST: case T_SCHI2: case T_SF: {
        /* DISTR DRAW: shade the area between lower and upper under the
         * density (p1, p2 = μ, σ / df / df1, df2) */
        double lo = N("lower:"), hi = N("upper:"), p1, p2 = 0;
        int dist = F->id == T_SNORM ? DIST_NORM : F->id == T_ST ? DIST_T : F->id == T_SCHI2 ? DIST_CHI2 : DIST_F;
        if (dist == DIST_NORM)
            p1 = N("mu:"), p2 = N("sigma:");
        else if (dist == DIST_F)
            p1 = N("df1:"), p2 = N("df2:");
        else
            p1 = N("df:");
        if (ok)
            sd_draw_open(dist, p1, p2, lo, hi, NULL);
        return;
    }
    case T_SORTA: case T_SORTD: {
        slist_t *key = sl_find(sf_text(fidx("List:")));
        slist_t *carry[SL_MAX];
        int nc = name_lists(sf_text(fidx("Carry:")), carry, SL_MAX);
        if (!key || nc < 0) {
            if (!key)
                sf_error("list not found");
            return;
        }
        sort_lists(key, carry, nc, F->id == T_SORTD);
        stat_edit_open(-1);
        return;
    }
    case T_CLR: {
        slist_t *cl[SL_MAX];
        int nc = name_lists(sf_text(fidx("Lists:")), cl, SL_MAX);
        if (nc < 0)
            return;
        for (int i = 0; i < nc; i++) {
            slist_t *l = cl[i];
            l->len = 0;
            l->formula[0] = 0;
            sl_changed(l);
        }
        stat_edit_open(-1);
        return;
    }
    }
    finish(&r, a);
}

/* ------------------------------------------------------------ menus */
/* Menu item action codes: T_* (1..27) open that form, 200 + REG_* opens the
 * regression form for that model, M_* (100..) are STAT / DISTR menu entries. */
enum {
    M_EDIT = 100, M_CALC, M_TESTS, M_SORTA, M_SORTD, M_CLR, M_SETUP, M_PLOTS, M_CSV, M_DRAW, M_INTERVALS,
    M_CLR_YES, M_CLR_NO
};

static const menu_item_t calc_items[] = {
    {"1-Var Stats", NULL, NULL, T_1VAR, "stat-calc"},  {"2-Var Stats", NULL, NULL, T_2VAR, "stat-calc"},
    {"LinReg(ax+b)", NULL, NULL, 200 + REG_LINAXB, "stat-regression"},
    {"LinReg(a+bx)", NULL, NULL, 200 + REG_LINABX, "stat-regression"},
    {"QuadReg", NULL, NULL, 200 + REG_QUAD, "stat-regression"},
    {"CubicReg", NULL, NULL, 200 + REG_CUBIC, "stat-regression"},
    {"QuartReg", NULL, NULL, 200 + REG_QUART, "stat-regression"},
    {"LnReg", NULL, NULL, 200 + REG_LN, "stat-regression"},
    {"ExpReg", NULL, NULL, 200 + REG_EXP, "stat-regression"},
    {"PwrReg", NULL, NULL, 200 + REG_PWR, "stat-regression"},
    {"Logistic", NULL, NULL, 200 + REG_LOGISTIC, "stat-regression"},
    {"SinReg", NULL, NULL, 200 + REG_SIN, "stat-regression"},
    {"Med-Med", NULL, NULL, 200 + REG_MEDMED, "stat-regression"},
};

static const menu_item_t test_items[] = {
    {"Z-Test", NULL, NULL, T_Z, NULL},          {"T-Test", NULL, NULL, T_T, NULL},
    {"2-SampZTest", NULL, NULL, T_2Z, NULL},    {"2-SampTTest", NULL, NULL, T_2T, NULL},
    {"1-PropZTest", NULL, NULL, T_1P, NULL},    {"2-PropZTest", NULL, NULL, T_2P, NULL},
    {"chi² GOF-Test", NULL, NULL, T_GOF, NULL}, {"chi²-Test (matrix)", NULL, NULL, T_CHI2, NULL},
    {"2-SampFTest", NULL, NULL, T_2F, NULL},    {"LinRegTTest", NULL, NULL, T_LINT, NULL},
    {"ANOVA", NULL, NULL, T_ANOVA, NULL},       {"ZInterval", NULL, NULL, T_ZI, "stat-intervals"},
    {"TInterval", NULL, NULL, T_TI, "stat-intervals"},     {"2-SampZInt", NULL, NULL, T_2ZI, "stat-intervals"},
    {"2-SampTInt", NULL, NULL, T_2TI, "stat-intervals"},   {"1-PropZInt", NULL, NULL, T_1PI, "stat-intervals"},
    {"2-PropZInt", NULL, NULL, T_2PI, "stat-intervals"},
};

/* on_select of the CALC, TESTS and DISTR DRAW menus: open the item's form.
 * A regression item (action 200 + REG_*) builds reg_form, the shared f_reg
 * fields under the model's title, and remembers the model in reg_type. */
static void sub_select(const menu_item_t *it)
{
    if (it->action >= 200) {
        reg_type = it->action - 200;
        snprintf(reg_title, sizeof reg_title, "%s", reg_names[reg_type]);
        reg_form = (sform_t){reg_title, f_reg, (int)(sizeof f_reg / sizeof f_reg[0]), act, "stat-regression",
                             T_REG};
        sf_open(&reg_form);
        return;
    }
    open_form(it->action);
}

static const menu_def_t menu_calc = {"STAT CALC", calc_items, sizeof calc_items / sizeof calc_items[0], sub_select,
                                     false, "stat-calc"};
static const menu_def_t menu_tests = {"STAT TESTS", test_items, sizeof test_items / sizeof test_items[0],
                                      sub_select, false, "stat-tests"};

static const menu_item_t stat_items[] = {
    {"Edit lists...", NULL, NULL, M_EDIT, "stat-edit"},
    {"CALC ▶", NULL, NULL, M_CALC, "stat-calc"},
    {"TESTS ▶", NULL, NULL, M_TESTS, "stat-tests"},
    {"SortA (sort ascending)", NULL, NULL, M_SORTA, "stat-edit"},
    {"SortD (sort descending)", NULL, NULL, M_SORTD, "stat-edit"},
    {"ClrList (empty lists)", NULL, NULL, M_CLR, "stat-edit"},
    {"SetUpEditor (L1..L6)", NULL, NULL, M_SETUP, "stat-edit"},
    {"Stat plots ▶", NULL, NULL, M_PLOTS, "stat-plots"},
    {"CSV on microSD ▶", NULL, NULL, M_CSV, "stat-csv"},
};

static void stat_select(const menu_item_t *it);
/* ClrList confirmation (from the list editor): No first, so ENTER is safe */
static slist_t *clr_target;
static char clr_title[32];
static const menu_item_t clr_items[] = {
    {"No, keep the list", NULL, NULL, M_CLR_NO, "stat-edit"},
    {"Yes, clear it", NULL, NULL, M_CLR_YES, "stat-edit"},
};
static const menu_def_t menu_clr = {clr_title, clr_items, 2, stat_select, false, "stat-edit"};

/* on_select of the STAT menu (and of the list editor's soft keys through
 * stat_do). When it was opened from the list editor, SortA/SortD/ClrList act
 * on the editor's current column (sorting at once, without carried lists;
 * clearing after a No/Yes question, since the values are lost); otherwise
 * they open a form asking for the lists. */
static void stat_select(const menu_item_t *it)
{
    slist_t *l = stat_edit_current();
    switch (it->action) {
    case M_EDIT: stat_edit_open(0); break;
    case M_CALC: menu_open(&menu_calc); break;
    case M_TESTS: menu_open(&menu_tests); break;
    case M_SORTA: case M_SORTD:
        if (l) { /* from the editor: the current column */
            sort_lists(l, NULL, 0, it->action == M_SORTD);
            stat_edit_open(-1);
        } else {
            open_form(it->action == M_SORTA ? T_SORTA : T_SORTD);
        }
        break;
    case M_CLR:
        if (l && l->len == 0 && !l->formula[0]) {
            stat_edit_open(-1); /* already empty: nothing to ask */
        } else if (l) {
            /* the values cannot be brought back: ask first */
            clr_target = l;
            snprintf(clr_title, sizeof clr_title, "Clear %s (%d values)?", l->name, l->len);
            menu_open(&menu_clr);
        } else {
            open_form(T_CLR);
        }
        break;
    case M_CLR_YES:
        if (clr_target) {
            clr_target->len = 0;
            clr_target->formula[0] = 0;
            sl_changed(clr_target);
        }
        stat_edit_open(-1);
        break;
    case M_CLR_NO: stat_edit_open(-1); break;
    case M_SETUP: sl_editor_default(); stat_edit_open(0); break;
    case M_PLOTS: stat_plot_menu_open(); break;
    case M_CSV: stat_csv_menu_open(); break;
    }
}

static const menu_def_t menu_stat = {"STAT", stat_items, sizeof stat_items / sizeof stat_items[0], stat_select,
                                     false, "stat"};

void stat_menu_open(void) { menu_open(&menu_stat); }

/* soft keys of the list editor (stat_edit.c)
 * (and the stat.h entry points: they run a STAT menu entry as if chosen) */
static void stat_do(int action)
{
    menu_item_t it = {NULL, NULL, NULL, action, NULL};
    stat_select(&it);
}
void stat_calc_open(void) { stat_do(M_CALC); }
void stat_tests_open(void) { stat_do(M_TESTS); }
void stat_sorta(void) { stat_do(M_SORTA); }
void stat_sortd(void) { stat_do(M_SORTD); }
void stat_clrlist(void) { stat_do(M_CLR); }

/* ------------------------------------------------------------ DISTR */
/* Distribution functions are CAS functions (cas/src/stats.c, help pages per
 * function): the menu only inserts their names; DRAW opens the shading forms. */
/* Labels show the argument order (square brackets: optional), as the
 * function's help page does; insert/suffix put "name(" before and ")" after
 * the cursor. SHIFT HELP finds the help page from the insert text. */
#define DI(lab, name) {lab, name "(", ")", 0, NULL}
static const menu_item_t distr_items[] = {
    DI("normalpdf(x[,mu,sd])", "normalpdf"),     DI("normalcdf(lo,hi[,mu,sd])", "normalcdf"),
    DI("invNorm(area[,mu,sd])", "invNorm"),      DI("invT(area,df)", "invT"),
    DI("tpdf(x,df)", "tpdf"),                    DI("tcdf(lo,hi,df)", "tcdf"),
    DI("chi2pdf(x,df)", "chi2pdf"),              DI("chi2cdf(lo,hi,df)", "chi2cdf"),
    DI("Fpdf(x,df1,df2)", "Fpdf"),               DI("Fcdf(lo,hi,df1,df2)", "Fcdf"),
    DI("binompdf(n,p[,x])", "binompdf"),         DI("binomcdf(n,p[,x])", "binomcdf"),
    DI("poissonpdf(mu,x)", "poissonpdf"),      DI("poissoncdf(mu,x)", "poissoncdf"),
    DI("geometpdf(p,x)", "geometpdf"),           DI("geometcdf(p,x)", "geometcdf"),
    DI("invChi2(area,df)", "invChi2"),           DI("invF(area,df1,df2)", "invF"),
    {"DRAW (shaded areas) ▶", NULL, NULL, M_DRAW, "stat-distr"},
};

static const menu_item_t draw_items[] = {
    {"ShadeNorm (normal area)", NULL, NULL, T_SNORM, "stat-distr"},
    {"Shade_t (t area)", NULL, NULL, T_ST, "stat-distr"},
    {"Shadechi² (chi² area)", NULL, NULL, T_SCHI2, "stat-distr"},
    {"ShadeF (F area)", NULL, NULL, T_SF, "stat-distr"},
};
static const menu_def_t menu_draw = {"DISTR DRAW", draw_items, sizeof draw_items / sizeof draw_items[0],
                                     sub_select, false, "stat-distr"};

/* on_select of DISTR: insert the function name into the editor of the screen
 * the menu was opened from (or of the home screen when that screen has no
 * editor) and return there with its state kept
 * (ui_returning). */
static void distr_select(const menu_item_t *it)
{
    if (it->action == M_DRAW) {
        menu_open(&menu_draw);
        return;
    }
    const screen_t *dest = menu_origin() && menu_origin()->editor ? menu_origin() : ui_home();
    editor_t *e = dest->editor ? dest->editor() : NULL;
    if (e)
        ed_insert_text(e, it->insert, it->suffix);
    ui_returning = true;
    ui_switch(dest);
    ui_returning = false;
}

static const menu_def_t menu_distr = {"DISTR", distr_items, sizeof distr_items / sizeof distr_items[0],
                                      distr_select, false, "stat-distr"};

void stat_distr_open(void) { menu_open(&menu_distr); }

/* ------------------------------------------------------------ apps */
/* APPS menu entries, registered in app_registry[] (app/registry.c). */
const app_def_t app_stat = {"stat", "Statistics", stat_menu_open, {{0}},
                            "stat"};
const app_def_t app_distr = {"distr", "Distributions", stat_distr_open, {{0}}, "stat-distr"};
const app_def_t app_statplot = {"statplot", "Stat plots", stat_plot_menu_open, {{0}}, "stat-plots"};
