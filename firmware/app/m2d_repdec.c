/*
 * Repeating decimals: input 0.(3) / 1.2(34) is the exact fraction (rewritten
 * before the CAS sees it), and decimal results that are a fraction with a
 * short period are shown as 0.(142857) (2D: a bar over the period).
 *
 * Input uses the textbook identity for a number with integer part I,
 * non-repeating decimals A (a digits) and repeating block R (r digits):
 *
 *   I.A(R) = (IAR - IA) / (9...9 0...0)      r nines followed by a zeros
 *
 * where IAR and IA are the digit strings read as integers; e.g.
 * 0.1(6) = (16 - 1)/90 = 1/6 and 1.2(34) = (1234 - 12)/990. The CAS then
 * reduces the fraction exactly.
 *
 * Display works backwards from a rounded decimal: find the simplest fraction
 * p/q (q <= MAX_Q) that agrees with it to the shown precision (continued
 * fractions), then compute the exact decimal expansion of p/q. Its
 * pre-period length is max(e2, e5) for q = 2^e2 5^e5 q', and its period is
 * the multiplicative order of 10 modulo q' (the smallest k with
 * 10^k = 1 mod q'). Only short periods are shown; display only, the history
 * keeps the CAS text.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m2d.h"

static bool dig(char c) { return c >= '0' && c <= '9'; }
/* character of a name or number: a digit glued to one is not a literal */
static bool idc(char c) { return dig(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

/* ------------------------------------------------------------ input */
/* Rewrite every repeating decimal "I.A(R)" of CAS input `in` as
 * "((IAR-IA)/99..00)" (leading zeros of both integers stripped) into out
 * (n bytes). Numbers that are part of a name (x2.(3)) are left alone, as are
 * groups longer than 40 digits. Returns true if anything was rewritten; on
 * false (nothing to do, or out too small) the caller uses `in`, and out is
 * then not necessarily terminated. */
bool repdec_rewrite(const char *in, char *out, size_t n)
{
    size_t o = 0;
    bool any = false;
    const char *s = in;
    while (*s) {
        if ((dig(*s) || *s == '.') && (s == in || (!idc(s[-1]) && s[-1] != '.'))) {
            const char *ip = s, *p = s;
            while (dig(*p))
                p++;
            const char *ipe = p;
            if (*p == '.') {
                const char *a = ++p;
                while (dig(*p))
                    p++;
                const char *ae = p;
                if (*p == '(' && dig(p[1])) {
                    const char *r = ++p;
                    while (dig(*p))
                        p++;
                    if (*p == ')' && p - r <= 40 && ae - ip <= 40) {
                        const char *re = p++;
                        /* (IAR - IA) / (9..9 0..0) */
                        char iar[96], ia[64], den[96];
                        int k = 0;
                        for (const char *q = ip; q < ipe; q++)
                            iar[k++] = *q;
                        for (const char *q = a; q < ae; q++)
                            iar[k++] = *q;
                        memcpy(ia, iar, (size_t)k);
                        ia[k] = 0;
                        for (const char *q = r; q < re; q++)
                            iar[k++] = *q;
                        iar[k] = 0;
                        int d = 0;
                        for (const char *q = r; q < re; q++)
                            den[d++] = '9';
                        for (const char *q = a; q < ae; q++)
                            den[d++] = '0';
                        den[d] = 0;
                        const char *x = iar, *y = ia;
                        while (*x == '0' && x[1])
                            x++;
                        while (*y == '0' && y[1])
                            y++;
                        char tmp[256];
                        int l = snprintf(tmp, sizeof tmp, "((%s-%s)/%s)", x, *y ? y : "0", den);
                        if (o + (size_t)l + 1 >= n)
                            return false;
                        memcpy(out + o, tmp, (size_t)l);
                        o += (size_t)l;
                        s = p;
                        any = true;
                        continue;
                    }
                }
            }
            /* not a repeating decimal: copy the number */
            while (s < p) {
                if (o + 1 >= n)
                    return false;
                out[o++] = *s++;
            }
            continue;
        }
        if (o + 1 >= n)
            return false;
        out[o++] = *s++;
    }
    out[o] = 0;
    return any;
}

/* ------------------------------------------------------------ display */
#define MAX_Q 10000   /* largest denominator recognised */
#define MAX_PERIOD 16 /* longest period shown */

/* One decimal literal `lit` (as printed with `sig` significant digits) ->
 * "I.A(R)" in out if it is p/q with q <= MAX_Q, a period of at most
 * MAX_PERIOD digits and a pre-period of at most 8. Only literals that use
 * (nearly) all significant digits qualify, i.e. results that were rounded:
 * 0.5 or 0.25 stay as they are, and so do terminating fractions. Requires
 * sig >= 6, 0 < value < 1e9. */
bool repdec_format(const char *lit, int sig, char *out, size_t n)
{
    /* plain decimal only: digits '.' digits */
    int sigd = 0;
    bool lead = true, dot = false;
    for (const char *p = lit; *p; p++) {
        if (*p == '.') {
            if (dot)
                return false;
            dot = true;
            continue;
        }
        if (!dig(*p))
            return false;
        if (lead && *p == '0')
            continue;
        lead = false;
        sigd++;
    }
    if (!dot || sig < 6 || sigd < sig - 1)
        return false;
    double v = strtod(lit, NULL);
    if (!(v > 0) || v >= 1e9)
        return false;
    int e10 = (int)floor(log10(v));
    /* accept a fraction within 0.6 units of the last shown digit (rounding
     * error is at most 0.5 units; a little slack for the binary double) */
    double tol = 0.6 * pow(10, e10 - sig + 1);
    /* best rational approximation by continued fractions: the convergents
     * p/q of v = a0 + 1/(a1 + 1/(a2 + ...)) follow p_k = a_k p_{k-1} + p_{k-2}
     * (same for q); the first one within tol is the simplest fraction that
     * explains the digits */
    long long p0 = 0, q0 = 1, p1 = 1, q1 = 0;
    double x = v;
    long long P = 0, Q = 0;
    for (int it = 0; it < 40; it++) {
        double a = floor(x);
        long long ai = (long long)a;
        long long p2 = ai * p1 + p0, q2 = ai * q1 + q0;
        if (q2 > MAX_Q)
            break;
        p0 = p1, q0 = q1, p1 = p2, q1 = q2;
        if (fabs((double)p2 / (double)q2 - v) <= tol) {
            P = p2, Q = q2;
            break;
        }
        double f = x - a;
        if (f < 1e-15)
            break;
        x = 1 / f;
    }
    if (Q <= 1)
        return false;
    /* preperiod (powers of 2 and 5) and period (order of 10 mod the rest) */
    long long q = Q;
    int twos = 0, fives = 0;
    while (q % 2 == 0)
        q /= 2, twos++;
    while (q % 5 == 0)
        q /= 5, fives++;
    if (q == 1)
        return false; /* terminating: the decimal is exact already */
    int pre = twos > fives ? twos : fives;
    int per = 1;
    long long r = 10 % q;
    while (r != 1 && per <= MAX_PERIOD) {
        r = r * 10 % q;
        per++;
    }
    if (per > MAX_PERIOD || pre > 8)
        return false;
    /* long division of P/Q for the pre-period and one period */
    long long ip = P / Q, rem = P % Q;
    char digits[32];
    for (int k = 0; k < pre + per; k++) {
        rem *= 10;
        digits[k] = (char)('0' + rem / Q);
        rem %= Q;
    }
    /* ip < 1e9 fits a long: newlib-nano prints no long long */
    int l = snprintf(out, n, "%ld.%.*s(%.*s)", (long)ip, pre, digits, per, digits + pre);
    return l > 0 && (size_t)l < n;
}

/* Replace every plain decimal literal in result text `in` by its repeating
 * form where repdec_format() finds one (out: n bytes, always terminated).
 * Literals glued to a name, a "(" or an exponent mark ᴇ are left alone. */
void repdec_apply(const char *in, char *out, size_t n, int sig)
{
    size_t o = 0;
    const char *s = in;
    while (*s && o + 1 < n) {
        if ((dig(*s) || (*s == '.' && dig(s[1]))) && (s == in || !idc(s[-1]))) {
            const char *p = s;
            while (dig(*p) || *p == '.')
                p++;
            bool glued = idc(*p) || *p == '(' || !strncmp(p, "ᴇ", 3);
            char lit[48], rep[64];
            size_t l = (size_t)(p - s);
            if (!glued && l < sizeof lit) {
                memcpy(lit, s, l);
                lit[l] = 0;
                if (repdec_format(lit, sig, rep, sizeof rep)) {
                    size_t rl = strlen(rep);
                    if (o + rl >= n)
                        break;
                    memcpy(out + o, rep, rl);
                    o += rl;
                    s = p;
                    continue;
                }
            }
            while (s < p && o + 1 < n)
                out[o++] = *s++;
            continue;
        }
        out[o++] = *s++;
    }
    out[o] = 0;
}
