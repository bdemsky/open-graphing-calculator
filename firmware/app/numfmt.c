/* Result formatting (MODE: Normal/SCI/ENG, FLOAT/FIX n, complex mode) and the
 * evaluation pipeline shared by the home screen and other entry screens.
 *
 * The CAS computes and prints exact or floating results in its own syntax
 * ("1.5e-7", "sqrt(2)", "3+4*i"). Everything the user sees passes through
 * app_eval() here, which applies the MODE settings that the CAS does not
 * know about:
 *   1. input rewriting: repeating decimals "0.(3)" (app/m2d_repdec.c) and
 *      statistics list names (app/stat_lists.c);
 *   2. the complex mode (REAL: refuse non-real results, r∠θ: polar form;
 *      the CAS itself gets REAL as cas_settings.real, so odd roots of
 *      negative numbers are real there, see app_apply_settings);
 *   3. the number format (numfmt_apply: FIX/SCI/ENG for every float literal
 *      in the output text, exponent mark ᴇ);
 *   4. display prettifying (pretty_from_cas: √, π, θ, ∞).
 * Apps that print plain doubles use numfmt_double() for the same format.
 *
 * Settings are read from the global app state `g` (g.fmt, g.fix, g.cplx,
 * g.cs.digits, g.cs.angle; see app/mode.c). No state of its own. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stat.h"
#include "m2d.h"
#include "ui.h"

#define EE "ᴇ" /* exponent mark (×10ⁿ); the editor turns it into *10^ */

/* Format v in scientific/engineering notation with `dec` decimals of the
 * mantissa (dec < 0: as many as needed for `sig` significant digits, trailing
 * zeros removed). */
static void fmt_exp(double v, bool eng, int dec, int sig, char *out, size_t n)
{
    if (v == 0) {
        if (dec > 0)
            snprintf(out, n, "0.%0*d" EE "0", dec, 0);
        else
            snprintf(out, n, "0" EE "0");
        return;
    }
    int e = (int)floor(log10(fabs(v)));
    double m = v / pow(10, e);
    int d = dec >= 0 ? dec : sig - 1;
    /* rounding can carry into the next power of ten (9.99 -> 10.0) */
    char tmp[48];
    snprintf(tmp, sizeof tmp, "%.*f", d, m);
    if (fabs(atof(tmp)) >= 10) {
        e++;
        m = v / pow(10, e);
    }
    int shift = 0;
    if (eng) {
        shift = ((e % 3) + 3) % 3;
        e -= shift;
        m = v / pow(10, e);
        if (dec < 0)
            d = sig - 1 - shift > 0 ? sig - 1 - shift : 0;
    }
    snprintf(tmp, sizeof tmp, "%.*f", d, m);
    if (dec < 0 && strchr(tmp, '.')) { /* FLOAT: trim trailing zeros */
        char *z = tmp + strlen(tmp) - 1;
        while (*z == '0')
            *z-- = 0;
        if (*z == '.')
            *z = 0;
    }
    snprintf(out, n, "%s" EE "%d", tmp, e);
}

/* Format a double with the MODE settings into out (n bytes):
 *   SCI/ENG   always exponent notation, g.fix decimals (FLOAT: up to the
 *             DIGITS setting, trailing zeros removed);
 *   FIX n     n decimals while |v| < 1e10 and the value does not round to
 *             zero, else scientific with n decimals;
 *   NORMAL    DIGITS significant digits (default 12), scientific outside
 *             [1e-3, 1e10); a ".0" is added to integral values so the
 *             result still reads as approximate, as the CAS prints floats.
 * NaN prints "undef", infinities "∞" / "-∞". */
void numfmt_double(double v, char *out, size_t n)
{
    int sig = g.cs.digits > 0 && g.cs.digits <= 15 ? g.cs.digits : 12;
    if (isnan(v)) {
        snprintf(out, n, "undef");
        return;
    }
    if (isinf(v)) {
        snprintf(out, n, v > 0 ? "∞" : "-∞");
        return;
    }
    if (g.fmt == FMT_SCI || g.fmt == FMT_ENG) {
        fmt_exp(v, g.fmt == FMT_ENG, g.fix, sig, out, n);
        return;
    }
    double a = fabs(v);
    if (g.fix >= 0) {
        /* FIX: fixed decimals while the number fits, else scientific */
        if (a != 0 && (a >= 1e10 || a < 0.5 * pow(10, -g.fix)))
            fmt_exp(v, false, g.fix, sig, out, n);
        else
            snprintf(out, n, "%.*f", g.fix, v);
        return;
    }
    /* NORMAL FLOAT: `sig` significant digits, scientific outside 1e-3..1e10 */
    if (a != 0 && (a >= 1e10 || a < 1e-3)) {
        fmt_exp(v, false, -1, sig, out, n);
        return;
    }
    snprintf(out, n, "%.*g", sig, v);
    if (!strchr(out, '.') && !strchr(out, 'e') && strlen(out) + 2 < n)
        strcat(out, ".0"); /* still marks an approximate value, like the CAS */
}

static bool ident_char(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* Reformat the floating-point literals in CAS output text with the MODE
 * number format; everything else (integers, exact fractions, names, operators)
 * is copied unchanged. A literal is a float if it has a '.' or an exponent;
 * digits glued to an identifier ("x2", "e1" in a name) are not numbers. In
 * Normal/FLOAT mode the CAS digits are kept and only "e" becomes "ᴇ", so the
 * output matches the CAS precision. A formatted number that would not fit
 * ends the output there (no partial number). */
void numfmt_apply(const char *in, char *out, size_t n)
{
    size_t o = 0;
    bool plain = g.fmt == FMT_NORMAL && g.fix < 0;
    const char *s = in;
    while (*s && o + 1 < n) {
        /* a number literal not glued to an identifier (x2, e1) */
        if ((isdigit((unsigned char)*s) || (*s == '.' && isdigit((unsigned char)s[1]))) &&
            (s == in || !ident_char(s[-1]))) {
            const char *p = s;
            bool isfloat = false;
            while (isdigit((unsigned char)*p))
                p++;
            if (*p == '.') {
                isfloat = true;
                p++;
                while (isdigit((unsigned char)*p))
                    p++;
            }
            if ((*p == 'e' || *p == 'E') &&
                (isdigit((unsigned char)p[1]) || ((p[1] == '-' || p[1] == '+') && isdigit((unsigned char)p[2])))) {
                isfloat = true;
                p += 2;
                while (isdigit((unsigned char)*p))
                    p++;
            }
            /* the two numbers of value±unc keep the CAS's significant-figure
             * rounding (cas/src/unc.c), whatever the MODE format */
            bool pm = (s - in >= 2 && s[-2] == '\xc2' && s[-1] == '\xb1') || (p[0] == '\xc2' && p[1] == '\xb1');
            if (isfloat && !pm && !ident_char(*p)) {
                char num[64], f[96];
                size_t l = (size_t)(p - s) < sizeof num - 1 ? (size_t)(p - s) : sizeof num - 1;
                memcpy(num, s, l);
                num[l] = 0;
                if (plain) {
                    /* only the exponent mark: 1.5e-7 -> 1.5ᴇ-7 */
                    char *e = strpbrk(num, "eE");
                    if (e) {
                        *e = 0;
                        snprintf(f, sizeof f, "%s" EE "%s", num, e + 1);
                    } else {
                        snprintf(f, sizeof f, "%s", num);
                    }
                } else {
                    numfmt_double(strtod(num, NULL), f, sizeof f);
                }
                size_t fl = strlen(f);
                if (o + fl >= n)
                    break;
                memcpy(out + o, f, fl);
                o += fl;
                s = p;
                continue;
            }
            /* integer or part of something else: copy as is */
            while (s < p && o + 1 < n)
                out[o++] = *s++;
            continue;
        }
        out[o++] = *s++;
    }
    out[o] = 0;
}

/* true if the CAS text contains the imaginary unit `i` as a word */
static bool has_i(const char *s)
{
    for (const char *p = s; *p; p++)
        if (*p == 'i' && (p == s || !ident_char(p[-1])) && !ident_char(p[1]))
            return true;
    return false;
}

/* true if a CAS result is not real: it contains the word i, or a root of -1
 * with a numeric exponent, "(-1)^(1/6)" (the principal branch keeps even
 * roots of negatives in that form, e.g. (-8)^(1/6) = (-1)^(1/6)*sqrt(2); in
 * COMPLEX mode REAL odd roots are real and never print like this). A
 * symbolic exponent such as (-1)^n is not counted. See ui.h. */
bool numfmt_nonreal(const char *s)
{
    if (has_i(s))
        return true;
    for (const char *p = strstr(s, "(-1)^("); p; p = strstr(p + 1, "(-1)^(")) {
        const char *q = p + 6;
        if (*q == '-')
            q++;
        const char *d = q;
        while (*q >= '0' && *q <= '9')
            q++;
        if (q == d || *q++ != '/')
            continue;
        d = q;
        while (*q >= '0' && *q <= '9')
            q++;
        if (q > d && *q == ')')
            return true;
    }
    return false;
}

/* Error names for "ERR:..." messages, indexed by cas_status (cas.h). */
static const char *const status_names[] = {"OK", "SYNTAX", "DOMAIN", "UNDEFINED", "MEMORY",
                                           "BREAK", "UNSUPPORTED", "OVERFLOW"};

/* Evaluate CAS input like the home screen (see the header comment and ui.h).
 * Sets the CAS `ans` (cas_eval). Returns the CAS status; a truncated result
 * (CAS_ERR_OUTPUT_TRUNC) is still shown. On errors `out` is "ERR:NAME detail".
 * A non-real result (numfmt_nonreal) in REAL mode is an error unless the
 * input itself used i.
 * Polar mode: abs(ans) and arg(ans) (converted to degrees in DEG mode) are
 * evaluated quietly and shown as "r∠θ" if both simplify to real expressions;
 * otherwise the a+bi form is kept. autos: run the SIMPLIFY AUTO pass first
 * (app_eval_result). */
static cas_status eval_core(const char *cas_text, char *out, size_t n, bool autos)
{
    char raw[512], tmp[512], rw[512];
    if (repdec_rewrite(cas_text, rw, sizeof rw)) /* 0.(3) is 1/3 (app/m2d_repdec.c) */
        cas_text = rw;
    stat_pre_eval(cas_text); /* statistics lists named in the input -> CAS (app/stat_lists.c) */
    cas_status st = cas_eval(g.cas, cas_text, raw, sizeof raw);
    stat_post_eval(cas_text); /* ... and back after assignments */
    if (st != CAS_OK && st != CAS_ERR_OUTPUT_TRUNC) {
        const char *msg = cas_last_error(g.cas);
        snprintf(out, n, "ERR:%s %s", (unsigned)st < 8 ? status_names[st] : "?", msg ? msg : "");
        return st;
    }
    if (autos && st == CAS_OK) /* MODE SIMPLIFY AUTO: cos(x)^2+sin(x)^2 -> 1 (app/result_actions.c) */
        autosimp_ans(cas_text, raw, sizeof raw);
    if (numfmt_nonreal(raw)) {
        if (g.cplx == CPLX_REAL && !has_i(cas_text)) {
            snprintf(out, n, "ERR:NONREAL ANS (MODE: a+bi)");
            return CAS_ERR_DOMAIN;
        }
        if (g.cplx == CPLX_POLAR) {
            char r[192], th[192];
            if (cas_eval_quiet(g.cas, "abs(ans)", r, sizeof r) == CAS_OK &&
                cas_eval_quiet(g.cas, g.cs.angle == CAS_ANGLE_DEG ? "arg(ans)*180/pi" : "arg(ans)", th,
                               sizeof th) == CAS_OK &&
                !has_i(r) && !has_i(th) && !strstr(r, "abs(") && !strstr(th, "arg(")) {
                snprintf(raw, sizeof raw, "%s∠%s", r, th);
            }
        }
    }
    numfmt_apply(raw, tmp, sizeof tmp);
    pretty_from_cas(tmp, out, n);
    return st;
}

/* app_eval without the auto-simplification: helper evaluations of other
 * screens (see ui.h). */
cas_status app_eval(const char *cas_text, char *out, size_t n) { return eval_core(cas_text, out, n, false); }

/* app_eval of a displayed home-screen result: after the evaluation, the MODE
 * SIMPLIFY AUTO pass (autosimp_ans) replaces the result and ans by the
 * cheap autosimplify form when that applies. */
cas_status app_eval_result(const char *cas_text, char *out, size_t n) { return eval_core(cas_text, out, n, true); }
