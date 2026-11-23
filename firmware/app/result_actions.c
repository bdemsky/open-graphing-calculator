/* Result actions and automatic simplification of displayed results.
 *
 * Two features that make the home screen (and RPN) simplify without typing
 * simplify(...):
 *
 * 1. Result actions: one key applies a rewriting command to the last result,
 *    the selected history item or the line being typed. Home screen:
 *    SHIFT F3 (simplify) applies simplify, SHIFT F4 (rewrite) opens the
 *    REWRITE menu below (forms = the alternate forms browser of
 *    app/forms_ui.c, factor, expand, ≈ / exact, partfrac,
 *    texpand, tlin, tcollect, collect in x, and for a result with trig or
 *    exp also exp→trig / trig→exp (exp2trig, trig2exp); for a matrix x⁻¹, det,
 *    transpose; for a value with a unit convert to... (UNITS menu) and
 *    usimplify; digits 1..9 choose). F5 (≈) on an
 *    empty line toggles the last result between approx and exact. RPN: SHIFT
 *    F4 (simplify) and SHIFT F5 (rewrite) act on level 1 (a line being typed
 *    is pushed first). The result is a new history entry (a new level 1)
 *    whose input reads like the command, e.g. "simplify(Ans)", so the history
 *    stays honest. The screens do the work (home_result_action in
 *    app/home.c, rpn_result_action in app/rpn.c); this file holds the menu
 *    and the dispatch.
 *    Why these keys: the unshifted home soft keys (y= frac func mtrx ≈) are
 *    the templates and the decimal key users press most; SHIFT F3 / F4 used
 *    to insert factor( and expand( templates, which the REWRITE menu now
 *    covers (and applies at once). So simplify is two presses, everything
 *    else three, and nothing printed on the keyboard changes. In RPN, SHIFT
 *    F4 / F5 were lastarg and stack, which keep their own keys (SHIFT (-)
 *    and the , key).
 *
 * 2. MODE > SIMPLIFY AUTO / OFF (default AUTO): every displayed home or RPN
 *    result goes through the CAS's cheap autosimplify pass (rational normal
 *    form when it is a clear win, Pythagorean identities: cos(x)^2+sin(x)^2
 *    is 1; Euler's formula between matched exponentials and trig functions:
 *    (e^(ix)+e^(-ix))/2 is cos(x); cas/src/simplify.c simplify_auto) before
 *    it is shown. It is
 *    skipped when the input already asks for a particular form (simplify,
 *    expand, factor, partfrac, the trig and log rewritings, approx/exact,
 *    series ...), for assignments, for results without a letter (numbers)
 *    and for long results; it runs with a time budget (AUTO_BUDGET_MS) and
 *    the original result is kept whenever it fails or is interrupted.
 *    Graphing, the solver, tables, compiled functions and the other apps
 *    never see it: only app_eval_result() (home, preview) and the RPN levels
 *    call autosimp_*. Host measurements (1014 results from the help
 *    examples, CAS unit tests, benchmarks): median 2 µs, 99th percentile
 *    35 µs, worst short result 0.2 ms, scratch peak 0.6 MB; on the MCU
 *    (10-20x slower) about 4 ms worst case, well inside the budget. The
 *    Euler pass left these unchanged (same corpus, re-measured: median 3 µs,
 *    p99 24 µs); results that trigger it take 10-60 µs, a 12-exponential
 *    sum 0.15 ms.
 *
 * Saved state: the "simplify" persist blob (the MODE row). Console tests use
 * `setmode SIMPLIFY OFF` and the `state` key autosimp. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "app.h" /* app_cas_poll */
#include "forms_ui.h"
#include "hal.h"
#include "registry.h"
#include "rpn.h"
#include "session.h"
#include "ui.h"

#define AUTO_BUDGET_MS 100 /* time box of one autosimplify pass */
#define AUTO_MAX_TEXT 300  /* longer results are shown as they are */

static bool auto_on = true; /* MODE > SIMPLIFY AUTO (saved) */

bool autosimp_on(void) { return auto_on; }

/* Commands whose result has the form the user asked for: an input that
 * calls one of them is never auto-simplified (factor(x^2-1) stays factored,
 * partfrac stays split). Compared ignoring case (FACTOR( is factor(). */
static const char *const shape_cmds[] = {
    "simplify", "autosimplify", "expand",   "factor",   "cfactor",  "ifactor",  "factors",   "sqrfree",
    "partfrac", "apart",        "propfrac", "together", "normal",   "ratsimp",  "collect",   "horner",
    "texpand",  "tcollect",     "tlin",     "trigsimp", "trig2exp", "exp2trig", "lncollect", "lnexpand",
    "expexpand", "powexpand",   "approx",   "evalf",    "float",    "exact",    "series",    "taylor",
    "pade",     "convert",      "usimplify", "numer",   "denom",    "coeffs",   "forms"};

/* true if text s has one of the n names w[] as a whole word (any case) */
static bool has_word(const char *s, const char *const *w, size_t nw)
{
    for (const char *p = s; *p;) {
        if (!isalpha((unsigned char)*p)) {
            p++;
            continue;
        }
        const char *q = p;
        while (isalnum((unsigned char)*p) || *p == '_')
            p++;
        size_t n = (size_t)(p - q);
        for (size_t i = 0; i < nw; i++)
            if (strlen(w[i]) == n && !strncasecmp(q, w[i], n))
                return true;
    }
    return false;
}

/* true if the CAS text calls one of shape_cmds */
static bool asks_for_form(const char *s) { return has_word(s, shape_cmds, sizeof shape_cmds / sizeof shape_cmds[0]); }

/* Should the result `raw` of the CAS input `in` get the autosimplify pass?
 * Only in AUTO mode, never for assignments / definitions or inputs that ask
 * for a form, only for short results that contain a name (numbers are
 * already canonical). */
bool autosimp_wanted(const char *in, const char *raw)
{
    if (!auto_on || strstr(in, ":=") || strchr(in, '\x01') || asks_for_form(in) || strlen(raw) > AUTO_MAX_TEXT)
        return false;
    for (const char *p = raw; *p; p++)
        if (isalpha((unsigned char)*p))
            return true;
    return false;
}

static uint32_t t_start;
/* CAS poll callback: stop the pass when the time budget is spent */
static int budget_cb(void *user)
{
    (void)user;
    hal_watchdog_kick();
    return hal_millis() - t_start > AUTO_BUDGET_MS;
}

/* Evaluate `cmd` (an autosimplify command built by the caller) with the time
 * budget; on success its result replaces raw (n bytes). quiet: leave ans
 * alone. Returns true if raw changed. Any failure (interrupted, out of
 * memory, a stub CAS without the command, a result that does not fit) keeps
 * raw as it was. */
static bool run_pass(const char *cmd, bool quiet, char *raw, size_t n)
{
    char out[AUTO_MAX_TEXT + 64];
    t_start = hal_millis();
    cas_set_poll(g.cas, budget_cb, NULL);
    cas_status st = quiet ? cas_eval_quiet(g.cas, cmd, out, sizeof out) : cas_eval(g.cas, cmd, out, sizeof out);
    cas_set_poll(g.cas, app_cas_poll, NULL); /* the app's default hook (watchdog) */
    if (st != CAS_OK || !out[0] || !strcmp(out, raw) || strlen(out) >= n || strstr(out, "autosimplify"))
        return false;
    strcpy(raw, out);
    return true;
}

/* Home screen: `raw` is the result of cas_eval(in), so `ans` holds it.
 * autosimplify(ans) is evaluated like a user entry, so ans follows what is
 * shown. */
bool autosimp_ans(const char *in, char *raw, size_t n)
{
    return autosimp_wanted(in, raw) && run_pass("autosimplify(ans)", false, raw, n);
}

/* Preview / other displays: simplify the result text itself, quietly. */
bool autosimp_text(const char *in, char *raw, size_t n)
{
    char cmd[AUTO_MAX_TEXT + 24];
    if (!autosimp_wanted(in, raw))
        return false;
    snprintf(cmd, sizeof cmd, "autosimplify((%s))", raw);
    return run_pass(cmd, true, raw, n);
}

/* RPN: the level's value is the hidden variable `var` (_rpnN); store the
 * simplified value back into it so the level holds what it shows. */
bool autosimp_var(const char *in, const char *var, char *raw, size_t n)
{
    char cmd[64];
    if (!autosimp_wanted(in, raw))
        return false;
    snprintf(cmd, sizeof cmd, "%s:=autosimplify(%s)", var, var);
    return run_pass(cmd, false, raw, n);
}

/* true if result text is a decimal approximation (has a float literal:
 * digits with a '.', or the exponent mark ᴇ), so ≈ ↔ exact should go
 * back to exact */
bool result_is_approx(const char *s)
{
    if (strstr(s, "ᴇ"))
        return true;
    for (const char *p = s; *p; p++)
        if (*p == '.' && isdigit((unsigned char)p[1]) && (p == s || !isalpha((unsigned char)p[-1])))
            return true;
    return false;
}

/* ------------------------------------------------------------ REWRITE menu */
/* Items: label, CAS command (insert), text after the argument (suffix:
 * ",x" for collect), action code, help topic. NULL insert = ≈ / exact
 * (approx or exact, decided from the argument); RA_CONVERT = convert to a
 * unit (a template: the unit is chosen from the UNITS menu). The menu shows
 * one of three lists, chosen by rewrite_open() from the value it acts on:
 * a matrix gets x⁻¹, det and transpose, a value with a unit gets
 * convert, anything else the algebra and trig rewritings. */
enum { RA_CMD, RA_CONVERT, RA_FORMS };
static const menu_item_t items_expr[] = {
#if defined(CONFIG_CAS_FORMS)
    /* item 1: the alternate forms browser (app/forms_ui.c); simplify itself
     * is SHIFT F3 (RPN: SHIFT F4), so the other items keep their digits */
    {"forms (alternate forms)", NULL, "", RA_FORMS, "forms-browser"},
#else
    {"simplify", "simplify", "", RA_CMD, "simplify"}, /* core tier: no forms */
#endif
    {"factor", "factor", "", RA_CMD, "factor"},
    {"expand", "expand", "", RA_CMD, "expand"},
    {"≈ / exact", NULL, "", RA_CMD, "approx"},
    {"partfrac (partial fractions)", "partfrac", "", RA_CMD, "partfrac"},
    {"texpand (trig expand)", "texpand", "", RA_CMD, "texpand"},
    {"tlin (trig reduce)", "tlin", "", RA_CMD, "tlin"},
    {"tcollect (trig combine)", "tcollect", "", RA_CMD, "tcollect"},
    {"collect (in x)", "collect", ",x", RA_CMD, "collect"},
    /* only for results with trig or exp (rewrite_open) */
    {"exp→trig (Euler)", "exp2trig", "", RA_CMD, "exp2trig"},
    {"trig→exp (Euler)", "trig2exp", "", RA_CMD, "trig2exp"},
};
static const char *const trig_exp_names[] = {"sin",  "cos",  "tan",  "cot", "sec",
                                             "csc",  "sinh", "cosh", "tanh", "exp"};
static const menu_item_t items_matrix[] = {
    {"simplify", "simplify", "", RA_CMD, "simplify"},
    {"x⁻¹ (inverse)", "inv", "", RA_CMD, "inv"},
    {"det", "det", "", RA_CMD, "det"},
    {"transpose", "transpose", "", RA_CMD, "transpose"},
    {"≈ / exact", NULL, "", RA_CMD, "approx"},
    {"factor", "factor", "", RA_CMD, "factor"},
    {"expand", "expand", "", RA_CMD, "expand"},
};
static const menu_item_t items_unit[] = {
    {"simplify", "simplify", "", RA_CMD, "simplify"},
    {"convert to (unit)", "convert", "", RA_CONVERT, "convert"},
    {"usimplify (to SI units)", "usimplify", "", RA_CMD, "usimplify"},
    {"≈ / exact", NULL, "", RA_CMD, "approx"},
    {"factor", "factor", "", RA_CMD, "factor"},
    {"expand", "expand", "", RA_CMD, "expand"},
};
#define NITEMS(a) ((int)(sizeof a / sizeof a[0]))
static const menu_item_t *rw_items = items_expr; /* the list rewrite_open chose */
static int rw_n = NITEMS(items_expr);

static int rw_count(void) { return rw_n; }
static void rw_get(int i, menu_item_t *out) { *out = rw_items[i]; }

/* true if result text is a matrix ([[...],[...]]) */
static bool is_matrix(const char *s) { return s[0] == '[' && s[1] == '['; }

/* true if result text has a unit (_m, _km/_h: '_' then a letter, not glued
 * to a name such as a hidden variable _rpn1) */
static bool has_unit(const char *s)
{
    for (const char *p = s; *p; p++)
        if (*p == '_' && isalpha((unsigned char)p[1]) && (p == s || !isalpha((unsigned char)p[-1])) &&
            strncmp(p, "_rpn", 4))
            return true;
    return false;
}

/* Apply a rewriting command to the home screen's or RPN's current result
 * (fn NULL: ≈ / exact). */
void result_action(const char *fn, const char *extra)
{
    if (rpn_active())
        rpn_result_action(fn, extra);
    else
        home_result_action(fn, extra);
}

/* REWRITE item chosen: back to the home (or RPN) screen with its state kept,
 * then apply the command there (convert: the screen's convert template). */
static void rewrite_select(const menu_item_t *it)
{
    ui_returning = true;
    ui_switch(ui_home());
    ui_returning = false;
    if (it->action == RA_CONVERT)
        rpn_active() ? rpn_result_convert() : home_result_convert();
    else if (it->action == RA_FORMS)
        rpn_active() ? rpn_result_forms() : home_result_forms();
    else
        result_action(it->insert, it->suffix);
}

static const menu_def_t menu_rewrite = {"REWRITE", NULL, 0, rewrite_select, false, "result-actions", rw_count, rw_get};

/* Open the REWRITE menu with the items that fit `value` (the result text the
 * action will act on: a matrix, a value with a unit, or anything else). */
void rewrite_open(const char *value)
{
    if (is_matrix(value))
        rw_items = items_matrix, rw_n = NITEMS(items_matrix);
    else if (has_unit(value))
        rw_items = items_unit, rw_n = NITEMS(items_unit);
    else
        rw_items = items_expr,
        rw_n = NITEMS(items_expr) - (has_word(value, trig_exp_names, NITEMS(trig_exp_names)) ? 0 : 2);
    menu_open(&menu_rewrite);
}

/* ------------------------------------------------------------ MODE row */
static int simp_get(void) { return auto_on ? 0 : 1; }
static void simp_set(int c) { auto_on = c == 0; }
const mode_row_t mode_simplify = {"SIMPLIFY", {"AUTO", "OFF"}, 2, simp_get, simp_set, "mode-simplify"};

/* ------------------------------------------------------------ persistence */
/* "simplify" blob, version 1: u8 1 = AUTO, 0 = OFF. */
static void simp_save(pw_t *w) { pw_u8(w, auto_on); }
static void simp_load(pr_t *r, unsigned version)
{
    (void)version;
    unsigned v = pr_u8(r);
    if (!r->err)
        auto_on = v != 0;
}
static void simp_reset(void) { auto_on = true; }
const persist_def_t persist_simplify = {"simplify", 1, simp_save, simp_load, simp_reset};
