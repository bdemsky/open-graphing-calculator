/*
 * Finance app (usual time-value-of-money conventions, numeric):
 *   TVM Solver: N, I%, PV, PMT, FV, P/Y, C/Y, PMT: END/BEGIN; ALPHA ENTER
 *     solves the highlighted variable from
 *       0 = PV + (1+i*k)*PMT*(1-(1+i)^-N)/i + FV*(1+i)^-N,
 *       i = (1 + I%/(100*C/Y))^(C/Y / P/Y) - 1,  k = 1 for BEGIN, 0 for END.
 *   Amortization: bal(n), SumPrn(p1,p2), SumInt(p1,p2) and a payment table.
 *   Cash flows: NPV at I% and IRR of CF0, CF1..CFn.
 *   Interest conversion: nominal <-> effective rate for C/Y periods.
 *   Days between dates: dbd of two dates YYYYMMDD.
 * Signs follow the usual cash-flow convention: money received is positive, paid negative.
 *
 * Notation for the TVM equation: N payments, I% the nominal annual rate in
 * percent compounded C/Y times a year, P/Y payments a year. The payment-period
 * rate i converts the compounding rate I%/(100 C/Y) to the payment period:
 * (1 + i) = (1 + I%/(100 C/Y))^(C/Y / P/Y). The equation is the standard
 * present value of an ordinary annuity, PMT (1 - (1+i)^-N) / i, plus the
 * discounted future value FV (1+i)^-N; for payments at the start of each
 * period (annuity due, BEGIN) every payment is discounted one period less,
 * which multiplies the annuity term by (1 + i). With i = 0 the equation
 * becomes PV + PMT N + FV = 0.
 *
 * User interface: APPS > Finance opens a menu of the five screens; the
 * soft keys (tvm amort cashflow interest solve / SHIFT: days) switch between
 * them. All five screens are the same xform_t row form `ff` (app/xform.c)
 * whose callbacks dispatch on `fscr`, the screen being shown; computed rows
 * (amortization results, NPV, IRR, days) are read-only and recomputed every
 * time they are drawn. On the TVM screen F5 ("solve", delivered as KEY_F5)
 * or ALPHA ENTER solves for the highlighted variable; the PMT: row cycles
 * END/BEGIN with ENTER/LEFT/RIGHT. The footer explains the selected field
 * and its sign convention (f_hint); F5 on the other screens says that they
 * compute as values are entered.
 *
 * State: the struct fz (all inputs of all screens), saved as the session blob
 * persist_finance ("finance", version 1). All computations are in double
 * precision, independent of the CAS (fields are evaluated with x_number, so
 * expressions such as 12*30 are accepted).
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

/* TVM variables; the index is also the row of the TVM form (row NV is the
 * END/BEGIN choice). */
enum { V_N, V_I, V_PV, V_PMT, V_FV, V_PY, V_CY, NV };
#define NCF 12 /* cash flows CF1..CF12 after CF0 */
static struct {
    double v[NV];              /* TVM values, indexed by V_* */
    bool begin;                /* payments at the beginning of each period (annuity due) */
    double p1, p2;             /* amortization range */
    double cfrate, cf0;        /* cash flows */
    int ncf;
    double cf[NCF];
    double nom, eff, cy;       /* interest conversion */
    double d1, d2;             /* dates YYYYMMDD */
} fz;
static bool inited;

/* Defaults (also persist_finance's reset): everything 0, P/Y = C/Y = 12
 * (monthly), amortization of payment 1, three cash flows, the dates
 * 2024-01-01 and 2024-12-31. need() installs them on first use when no
 * session blob was loaded. */
static void fz_defaults(void)
{
    memset(&fz, 0, sizeof fz);
    fz.v[V_PY] = fz.v[V_CY] = 12;
    fz.p1 = fz.p2 = 1;
    fz.ncf = 3;
    fz.cy = 12;
    fz.d1 = 20240101;
    fz.d2 = 20241231;
    inited = true;
}
static void need(void)
{
    if (!inited)
        fz_defaults();
}

/* ------------------------------------------------------------ math */
/* Interest rate per payment period for the annual percentage ipct, with the
 * current P/Y and C/Y: i = (1 + ipct/(100 C/Y))^(C/Y / P/Y) - 1. */
static double rate_per_pmt(double ipct)
{
    double py = fz.v[V_PY], cy = fz.v[V_CY];
    return pow(1 + ipct / (100 * cy), cy / py) - 1;
}

/* Residual of the TVM equation (zero when the values are consistent) for the
 * per-period rate i, using the END/BEGIN setting fz.begin:
 *   PV + (1 + i k) PMT (1 - (1+i)^-N) / i + FV (1+i)^-N,   k = BEGIN ? 1 : 0.
 * |i| < 1e-14 uses the limit i -> 0, PV + PMT N + FV, avoiding 0/0. */
static double tvm_f(double N, double i, double pv, double pmt, double fv)
{
    double k = fz.begin ? 1 : 0;
    if (fabs(i) < 1e-14)
        return pv + pmt * N + fv;
    double d = pow(1 + i, -N);
    return pv + (1 + i * k) * pmt * (1 - d) / i + fv * d;
}

/* solve variable `which`; returns false (msg set) if impossible
 *
 * which: a V_* index (the highlighted row); P/Y, C/Y and the END/BEGIN row
 * cannot be solved for. On success the solution is stored in fz.v[which].
 * PV, PMT and FV appear linearly and are solved in closed form; N is solved
 * by taking logarithms; I% has no closed form and is found numerically. */
static bool tvm_solve(int which, char *msg, size_t n)
{
    double N = fz.v[V_N], pv = fz.v[V_PV], pmt = fz.v[V_PMT], fv = fz.v[V_FV], k = fz.begin ? 1 : 0;
    if (fz.v[V_PY] <= 0 || fz.v[V_CY] <= 0) {
        snprintf(msg, n, "ERR: P/Y and C/Y must be > 0");
        return false;
    }
    double i = rate_per_pmt(fz.v[V_I]), r = NAN;
    switch (which) {
    case V_N:
        if (fabs(i) < 1e-14)
            r = pmt != 0 ? -(pv + fv) / pmt : (double)NAN;
        else {
            /* with a = PMT (1 + i k) and d = (1+i)^-N the equation is
             * PV + a/i - (a/i) d + FV d = 0, so d = (a + PV i) / (a - FV i)
             * and N = ln((a - FV i) / (a + PV i)) / ln(1 + i); a negative
             * log argument (no solution) gives NaN, reported below */
            double a = pmt * (1 + i * k);
            r = log((a - fv * i) / (a + pv * i)) / log(1 + i);
        }
        break;
    case V_PV: r = -(tvm_f(N, i, 0, pmt, fv)); break;
    case V_FV: {
        double d = pow(1 + i, -N);
        r = fabs(i) < 1e-14 ? -(pv + pmt * N) : -(tvm_f(N, i, pv, pmt, 0)) / d;
        break;
    }
    case V_PMT: {
        double c = fabs(i) < 1e-14 ? N : (1 + i * k) * (1 - pow(1 + i, -N)) / i;
        r = c != 0 ? -(tvm_f(N, i, pv, 0, fv)) / c : (double)NAN;
        break;
    }
    case V_I: {
        /* secant / Newton on the periodic rate, then convert to I%
         * Secant iteration on the per-period rate x, started from 1 % and
         * 2 %; it converges when |f| is below 1e-12 of the size of the
         * cash flows. A step that is not finite (f == f0) or that would go
         * to a rate <= -100 % (where (1+x)^-N is undefined) is replaced by
         * the point halfway between x and -1. The result is converted back
         * with the inverse of rate_per_pmt: I% = 100 C/Y ((1+x)^(P/Y / C/Y) - 1).
         * Only one root is found; with several sign changes in the cash
         * flows the equation can have more. */
        double x = 0.01, x0 = 0.02, f0 = tvm_f(N, x0, pv, pmt, fv);
        bool ok = false;
        for (int it = 0; it < 200; it++) {
            double f = tvm_f(N, x, pv, pmt, fv);
            if (isnan(f))
                break;
            if (fabs(f) < 1e-12 * (fabs(pv) + fabs(pmt) * fabs(N) + fabs(fv) + 1e-30)) {
                ok = true;
                break;
            }
            double nx = x - f * (x - x0) / (f - f0);
            if (!isfinite(nx) || nx <= -1)
                nx = (x - 1) / 2;
            x0 = x, f0 = f, x = nx;
        }
        if (ok) {
            double py = fz.v[V_PY], cy = fz.v[V_CY];
            r = 100 * cy * (pow(1 + x, py / cy) - 1);
        }
        break;
    }
    default: snprintf(msg, n, "Select N, I%%, PV, PMT or FV, then F5"); return false;
    }
    if (!isfinite(r)) {
        snprintf(msg, n, "ERR: no solution: check the signs (paid < 0)");
        return false;
    }
    fz.v[which] = r;
    return true;
}

/* Round to the MODE DECIMAL setting (FIX 0..9), unchanged in FLOAT. The
 * amortization works with values rounded like the displayed money amounts,
 * as a bank statement would, so the table adds up to what is shown.
 * scale = 10^FIX (from rnd_scale(), computed once per amortization run). */
static double rnd_scale(void) { return g.fix < 0 ? 0 : pow(10, g.fix); }
static double rnd(double x, double scale) { return scale ? round(x * scale) / scale : x; }

/* amortization from TVM values: balance after payment n, sums over [p1,p2]
 *
 * Simulates the loan payment by payment from PV with the payment PMT (both
 * rounded with rnd). For payment k the interest is -i * balance (for BEGIN
 * the payment is made first, so interest accrues on balance + PMT), also
 * rounded; the principal part is PMT - interest, and the balance moves by the
 * principal. With the sign convention (PV > 0 received, PMT < 0 paid) the
 * interest and principal parts come out negative and the balance shrinks.
 *
 * Outputs: *bal = balance after payment n (always). If both intp and prn are
 * given they receive the interest and principal sums over payments
 * fz.p1..n (ΣInt, ΣPrn; callers pass n = Pmt2). If only intp is given it
 * receives the interest part of payment n alone (table rows).
 * The run is resumable: `am` keeps the state after the last payment computed
 * and the inputs it was computed for, so a call for a later payment with the
 * same inputs continues from there. The table asks for payments k - 1 and k
 * row by row, so a redraw costs O(Pmt2) in all instead of O(Pmt2) per row. */
static struct {
    bool valid;
    double i, pv, pmt, scale, p1; /* inputs of the run */
    bool begin;
    int k;                        /* payments done */
    double b, in, si, sp;         /* balance, interest of payment k, sums over p1..k */
} am;

static void amort(int n, double *bal, double *intp, double *prn)
{
    double scale = rnd_scale(), i = rate_per_pmt(fz.v[V_I]), pv = rnd(fz.v[V_PV], scale),
           pmt = rnd(fz.v[V_PMT], scale);
    if (!(am.valid && am.i == i && am.pv == pv && am.pmt == pmt && am.scale == scale && am.p1 == fz.p1 &&
          am.begin == fz.begin && am.k <= n)) { /* new inputs or an earlier payment: start over */
        am.valid = true, am.i = i, am.pv = pv, am.pmt = pmt, am.scale = scale, am.p1 = fz.p1;
        am.begin = fz.begin, am.k = 0, am.b = pv, am.in = am.si = am.sp = 0;
    }
    for (int k = am.k + 1; k <= n; k++) {
        double in = rnd(-i * (fz.begin ? am.b + pmt : am.b), scale), pr = pmt - in;
        am.b += pr;
        if (k >= (int)fz.p1)
            am.si += in, am.sp += pr;
        am.in = in;
        am.k = k;
    }
    *bal = am.b;
    if (intp && prn)
        *intp = am.si, *prn = am.sp;
    else if (intp && n > 0)
        *intp = am.in;
}

/* Net present value at ipct percent per period of CF0 (at time 0) and
 * CF1..CFn (at the end of periods 1..n): NPV = sum_k CF_k / (1 + i)^k. */
static double npv(double ipct)
{
    double i = ipct / 100, s = fz.cf0, d = 1;
    for (int k = 0; k < fz.ncf; k++) {
        d /= 1 + i;
        s += fz.cf[k] * d;
    }
    return s;
}

/* Internal rate of return in percent: the rate at which npv() is zero.
 * Returns false when no sign change is found between -99 % and 10000 %.
 * The scan goes upwards (steps of 5 % up to 100 %, then a factor of 1.5), so
 * with several IRRs (cash flows that change sign more than once) the lowest
 * one in the scanned range is returned. 200 bisection steps reach the limit
 * of double precision. */
static bool irr(double *out)
{
    /* bracket a sign change of NPV on a grid of rates, then bisect */
    double a = -99, fa = npv(a);
    for (double b = -95; b <= 10000; b = b < 100 ? b + 5 : b * 1.5) {
        double fb = npv(b);
        if (!isnan(fa) && !isnan(fb) && (fa == 0 || (fa < 0) != (fb < 0))) {
            for (int it = 0; it < 200; it++) {
                double m = 0.5 * (a + b), fm = npv(m);
                if ((fm < 0) == (fa < 0))
                    a = m, fa = fm;
                else
                    b = m;
            }
            *out = 0.5 * (a + b);
            return true;
        }
        a = b, fa = fb;
    }
    return false;
}

/* Days from 1970-01-01 to the proleptic Gregorian date y-m-d (negative
 * before 1970). H. Hinnant's days_from_civil algorithm
 * (http://howardhinnant.github.io/date_algorithms.html): years are shifted to
 * start in March so the leap day is the last day of the year; era = 400-year
 * cycle (146097 days), yoe = year of era, doy = day of the March-based year
 * ((153 m' + 2) / 5 counts the days before month m'), doe = day of era;
 * 719468 is the day number of 1970-01-01 from 0000-03-01. */
static long days_civil(long y, long m, long d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* Validate a date entered as the number YYYYMMDD (integer, year >= 1, real
 * month and day, 29 February only in Gregorian leap years) and convert it to
 * a day number. The days between two dates is the difference of their day
 * numbers (actual/actual count, date2 - date1, negative if date2 is earlier). */
static bool date_ok(double v, long *days)
{
    long x = (long)v, y = x / 10000, m = x / 100 % 100, d = x % 100;
    static const int ml[13] = {0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (v != x || y < 1 || m < 1 || m > 12 || d < 1 || d > ml[m] ||
        (m == 2 && d == 29 && !((y % 4 == 0 && y % 100) || y % 400 == 0)))
        return false;
    *days = days_civil(y, m, d);
    return true;
}

/* ------------------------------------------------------------ screens */
/*
 * Row layout of each screen (E = editable, C = choice, I = read-only info):
 *   S_TVM    0..6 N I% PV PMT FV P/Y C/Y (E), 7 PMT: END/BEGIN (C)
 *   S_AMORT  0 Pmt1, 1 Pmt2 (E); 2 bal, 3 ΣPrn, 4 ΣInt (I);
 *            5 the table header, 6.. one row per payment Pmt1..Pmt2 (at most
 *            360): interest, principal, balance in 9-character columns
 *   S_CF     0 I%, 1 count, 2 CF0, 3..ncf+2 CF1..CFn (E);
 *            then NPV and IRR (I)
 *   S_CONV   0 Nom%, 1 Eff%, 2 C/Y (E): editing one rate recomputes the other
 *   S_DBD    0 date1, 1 date2 (E), 2 days (I)
 */
enum { S_TVM, S_AMORT, S_CF, S_CONV, S_DBD };
static int fscr;  /* screen shown by the shared form (index into scr_fin[]) */
static xform_t ff;

static const char *const tvm_lab[NV + 1] = {"N=", "I%=", "PV=", "PMT=", "FV=", "P/Y=", "C/Y=", "PMT: "};

/* xform_ops_t callbacks, all dispatching on fscr (row layout above). */
static int f_count(void)
{
    switch (fscr) {
    case S_TVM: return NV + 1;
    case S_AMORT: {
        int n = (int)fz.p2 - (int)fz.p1 + 1;
        return 5 + (n > 0 ? 1 + (n < 360 ? n : 360) : 0);
    }
    case S_CF: return 3 + fz.ncf + 2;
    case S_CONV: return 3;
    default: return 3;
    }
}

static int f_kind(int i)
{
    switch (fscr) {
    case S_TVM: return i == NV ? XR_CHOICE : XR_EDIT;
    case S_AMORT: return i < 2 ? XR_EDIT : XR_INFO;
    case S_CF: return i < 3 + fz.ncf ? XR_EDIT : XR_INFO;
    case S_CONV: return XR_EDIT;
    default: return i < 2 ? XR_EDIT : XR_INFO;
    }
}

static void f_label(int i, char *b, size_t n)
{
    switch (fscr) {
    case S_TVM: snprintf(b, n, "%s", tvm_lab[i]); return;
    case S_AMORT: {
        static const char *const l[] = {"Pmt1=", "Pmt2=", "bal=", "ΣPrn=", "ΣInt="};
        if (i < 5)
            snprintf(b, n, "%s", l[i]);
        else if (i == 5)
            snprintf(b, n, "pmt:");
        else
            snprintf(b, n, "%3d:", (int)fz.p1 + i - 6);
        return;
    }
    case S_CF:
        if (i < 3)
            snprintf(b, n, "%s", i == 0 ? "I%=" : i == 1 ? "count=" : "CF0=");
        else if (i < 3 + fz.ncf)
            snprintf(b, n, "CF%d=", i - 2);
        else
            snprintf(b, n, "%s", i == 3 + fz.ncf ? "NPV=" : i == 4 + fz.ncf ? "IRR=" : "");
        return;
    case S_CONV: snprintf(b, n, "%s", i == 0 ? "Nom%=" : i == 1 ? "Eff%=" : "C/Y="); return;
    default: snprintf(b, n, "%s", i == 0 ? "date1=" : i == 1 ? "date2=" : "days="); return;
    }
}

/* The stored value behind editable row i of the current screen, or NULL for
 * computed rows and rows handled specially (CF count, END/BEGIN). */
static double *f_var(int i)
{
    switch (fscr) {
    case S_TVM: return i < NV ? &fz.v[i] : NULL;
    case S_AMORT: return i == 0 ? &fz.p1 : i == 1 ? &fz.p2 : NULL;
    case S_CF: return i == 0 ? &fz.cfrate : i == 2 ? &fz.cf0 : i >= 3 && i < 3 + fz.ncf ? &fz.cf[i - 3] : NULL;
    case S_CONV: return i == 0 ? &fz.nom : i == 1 ? &fz.eff : &fz.cy;
    default: return i == 0 ? &fz.d1 : i == 1 ? &fz.d2 : NULL;
    }
}

/* Number formatting that shows rounding residue (|v| < 5e-13) as 0; tabnum
 * is the narrow (8 character) variant for the three amortization columns. */
static void money(double v, char *b, size_t n) { x_fmt(fabs(v) < 5e-13 ? 0 : v, b, n); }
static void tabnum(double v, char *b, size_t n) { fmt_num(fabs(v) < 5e-13 ? 0 : v, 8, b, n); }

/* Value text of row i. Stored values are formatted directly (dates as plain
 * integers); computed rows are evaluated here, on every draw. */
static void f_get(int i, char *b, size_t n)
{
    double *v = f_var(i);
    if (fscr == S_TVM && i == NV) {
        snprintf(b, n, "%s", fz.begin ? "END [BEGIN]" : "[END] BEGIN");
        return;
    }
    if (fscr == S_CF && i == 1) {
        snprintf(b, n, "%d", fz.ncf);
        return;
    }
    if (fscr == S_DBD && v) {
        snprintf(b, n, "%.0f", *v);
        return;
    }
    if (v) {
        x_fmt(*v, b, n);
        return;
    }
    b[0] = 0;
    if (fscr == S_AMORT) {
        double bal, in, pr;
        if (i == 2) {
            amort((int)fz.p2, &bal, NULL, NULL);
            money(bal, b, n);
        } else if (i < 5) {
            amort((int)fz.p2, &bal, &in, &pr);
            money(i == 3 ? pr : in, b, n);
        } else if (i == 5) {
            snprintf(b, n, "interest princ.   balance"); /* over the columns below */
        } else {
            /* table row for payment k: its interest, its principal (the
             * change of the balance) and the balance after it, in columns
             * of 9 characters under the header */
            int k = (int)fz.p1 + i - 6;
            double b0, b1, in1;
            amort(k - 1, &b0, NULL, NULL);
            amort(k, &b1, &in1, NULL);
            char s1[20], s2[20], s3[20];
            tabnum(in1, s1, sizeof s1);
            tabnum(b1 - b0, s2, sizeof s2);
            tabnum(b1, s3, sizeof s3);
            snprintf(b, n, "%-9s%-9s%s", s1, s2, s3);
        }
    } else if (fscr == S_CF) {
        double r;
        if (i == 3 + fz.ncf)
            money(npv(fz.cfrate), b, n);
        else if (i == 4 + fz.ncf) {
            if (irr(&r))
                x_fmt(r, b, n);
            else
                snprintf(b, n, "no sign change");
        }
    } else if (fscr == S_DBD) {
        long a, c;
        if (date_ok(fz.d1, &a) && date_ok(fz.d2, &c))
            snprintf(b, n, "%ld", c - a);
        else
            snprintf(b, n, "invalid date (YYYYMMDD)");
    }
}

/* Commit an edited row (cas: the field as CAS text). Returns false (row stays
 * in edit mode, error in the footer) for non-numbers and invalid values.
 * Side effects beyond the field itself: P/Y also sets C/Y, Pmt2 is raised to
 * Pmt1 if lower, and the interest conversion recomputes the other rate:
 *   Eff = 100 ((1 + Nom/(100 C/Y))^(C/Y) - 1)       (Nom or C/Y edited)
 *   Nom = 100 C/Y ((1 + Eff/100)^(1/C/Y) - 1)        (Eff edited) */
static bool f_set(int i, const char *cas, const char *disp)
{
    double v;
    char err[64];
    if (!x_number(cas, &v, err, sizeof err)) {
        xform_msg(&ff, true, "ERR: %s", err);
        return false;
    }
    if (fscr == S_CF && i == 1) {
        if (v != (int)v || v < 1 || v > NCF) {
            xform_msg(&ff, true, "ERR: 1..%d cash flows", NCF);
            return false;
        }
        fz.ncf = (int)v;
        return true;
    }
    if (fscr == S_AMORT && (v != floor(v) || v < 1 || v > 100000)) {
        xform_msg(&ff, true, "ERR: payment numbers are 1, 2, ...");
        return false;
    }
    if (fscr == S_CONV && i == 2 && v <= 0) {
        xform_msg(&ff, true, "ERR: C/Y must be > 0");
        return false;
    }
    double *p = f_var(i);
    if (!p)
        return true;
    *p = v;
    if (fscr == S_TVM && i == V_PY)
        fz.v[V_CY] = v; /* the usual convention: C/Y follows P/Y */
    if (fscr == S_AMORT && fz.p2 < fz.p1)
        fz.p2 = fz.p1;
    if (fscr == S_CONV) {
        if (i == 1)
            fz.nom = 100 * fz.cy * (pow(1 + fz.eff / 100, 1 / fz.cy) - 1);
        else
            fz.eff = 100 * (pow(1 + fz.nom / (100 * fz.cy), fz.cy) - 1);
    }
    xform_msg(&ff, false, "%s", "");
    return true;
}

/* The only XR_CHOICE row is TVM's PMT: END/BEGIN; both directions toggle. */
static void f_cycle(int i, int dir) { fz.begin = !fz.begin; }

/* xform action (F5 / ALPHA ENTER on row i): on the TVM screen solve for the
 * highlighted variable and report it in the footer; ignored elsewhere. */
static void f_action(int i)
{
    if (fscr != S_TVM) { /* the other screens compute as values are entered */
        xform_msg(&ff, false, "results update as you enter values");
        return;
    }
    char msg[64];
    if (tvm_solve(i, msg, sizeof msg)) {
        char b[24];
        x_fmt(fz.v[i], b, sizeof b);
        xform_msg(&ff, false, "%s%s solved", tvm_lab[i], b);
    } else
        xform_msg(&ff, true, "%s", msg);
}

/* Footer hint for the selected row (xform_ops_t.hint): what the field
 * means, its sign convention, and on the TVM screen that F5 solves it. */
static const char *f_hint(int i)
{
    static const char *const tvm[NV + 1] = {
        "N: number of payments; F5 solves",
        "I%: yearly interest rate in percent; F5 solves",
        "PV: present value (received +, paid -); F5 solves",
        "PMT: payment each period (paid -); F5 solves",
        "FV: future value (0: paid off); F5 solves",
        "P/Y: payments per year (also sets C/Y)",
        "C/Y: compounding periods per year",
        "payments at the END or BEGIN of a period: ENTER",
    };
    static const char *const amh[] = {"first payment of the range (TVM loan)", "last payment of the range",
                                     "balance after payment Pmt2", "principal paid in Pmt1..Pmt2",
                                     "interest paid in Pmt1..Pmt2"};
    switch (fscr) {
    case S_TVM: return tvm[i];
    case S_AMORT: return i < 5 ? amh[i] : "each payment: interest, principal, balance";
    case S_CF:
        return i == 0              ? "discount rate per period, in percent"
               : i == 1            ? "number of cash flows after CF0 (1..12)"
               : i == 2            ? "cash flow now (an investment is < 0)"
               : i < 3 + fz.ncf    ? "cash flow at the end of that period"
               : i == 3 + fz.ncf   ? "net present value at I%"
                                   : "internal rate of return, in percent";
    case S_CONV:
        return i == 0 ? "nominal yearly rate; Eff% follows" : i == 1 ? "effective yearly rate; Nom% follows"
                                                                     : "compounding periods per year";
    default: return i < 2 ? "a date as YYYYMMDD, e.g. 20240131" : "days from date1 to date2";
    }
}

static const xform_ops_t ff_ops = {f_count, f_kind, f_label, f_get, f_set, f_cycle, f_action, f_hint};

/* Screen callbacks shared by the five scr_fin[] screens. enter() resets the
 * form to the top of the screen chosen in fscr unless returning from a menu,
 * message or help page (ui_returning). CLEAR outside editing returns home. */
static void fs_enter(void)
{
    need();
    ff.o = &ff_ops;
    if (ui_returning)
        return;
    ff.sel = ff.top = 0;
    ff.msg[0] = 0;
    xform_reset(&ff);
}
static void fs_draw(void) { xform_draw(&ff); }
static void fs_key(int key, int mod)
{
    if (!xform_key(&ff, key, mod) && key == KEY_CLEAR)
        ui_switch(ui_home());
}
static editor_t *fs_editor(void) { return xform_editor(&ff); }
static void fs_touch(const touch_event_t *ev) { xform_touch(&ff, ev); }
/* Show finance screen i fresh (soft keys and the FINANCE menu): select it in
 * fscr first, since the shared form callbacks read it. */
static void fin_go(int i)
{
    extern const screen_t scr_fin[5];
    fscr = i;
    ui_returning = false;
    ui_switch(&scr_fin[i]);
}
static void sk_tvm(void) { fin_go(S_TVM); }
static void sk_amort(void) { fin_go(S_AMORT); }
static void sk_cf(void) { fin_go(S_CF); }
static void sk_conv(void) { fin_go(S_CONV); }
static void sk_dbd(void) { fin_go(S_DBD); }
static const softkeys_t fin_soft = {
    {{"tvm", sk_tvm}, {"amort", sk_amort}, {"cashflow", sk_cf}, {"interest", sk_conv}, {"solve"}},
    {{"days", sk_dbd}},
};
/* The five screens differ only in name, title and help topic. */
#define FS(name, title, help) {name, title, fs_enter, fs_draw, fs_key, NULL, fs_editor, fs_touch, help, &fin_soft}
const screen_t scr_fin[5] = {
    FS("tvm", "TVM SOLVER", "tvm"),           FS("amort", "AMORTIZATION", "amortization"),
    FS("cashflow", "CASH FLOWS", "cashflow"), FS("intconv", "INTEREST CONV", "interest-conv"),
    FS("dbd", "DAYS BETWEEN", "dbd"),
};

/* APPS > Finance: menu of the screens (action = S_* index). */
static const menu_item_t fin_items[] = {
    {"TVM Solver", NULL, NULL, S_TVM, "tvm"},
    {"Amortization (bal, ΣPrn, ΣInt)", NULL, NULL, S_AMORT, "amortization"},
    {"Cash flows (NPV, IRR)", NULL, NULL, S_CF, "cashflow"},
    {"Interest conversion (Nom, Eff)", NULL, NULL, S_CONV, "interest-conv"},
    {"Days between dates", NULL, NULL, S_DBD, "dbd"},
};
static void fin_select(const menu_item_t *it)
{
    fscr = it->action;
    ui_returning = false;
    ui_switch(&scr_fin[fscr]);
}
static const menu_def_t menu_fin = {"FINANCE", fin_items, 5, fin_select, false, "finance"};
static void fin_open(void) { menu_open(&menu_fin); }
const app_def_t app_finance = {"finance", "Finance", fin_open, {{0}}, "finance"};

/* ------------------------------------------------------------ persist */
/* Blob layout (version 1): 7 x f64 TVM values; u8 begin; f64 Pmt1, Pmt2;
 * f64 cash-flow I%, CF0; u8 ncf; ncf x f64 CF1..; f64 Nom%, Eff%, C/Y;
 * f64 date1, date2. Load starts from the defaults and falls back to them
 * entirely if the blob is truncated. */
static void fp_save(pw_t *w)
{
    for (int i = 0; i < NV; i++)
        pw_f64(w, fz.v[i]);
    pw_u8(w, fz.begin);
    pw_f64(w, fz.p1);
    pw_f64(w, fz.p2);
    pw_f64(w, fz.cfrate);
    pw_f64(w, fz.cf0);
    pw_u8(w, (uint32_t)fz.ncf);
    for (int i = 0; i < fz.ncf; i++)
        pw_f64(w, fz.cf[i]);
    pw_f64(w, fz.nom);
    pw_f64(w, fz.eff);
    pw_f64(w, fz.cy);
    pw_f64(w, fz.d1);
    pw_f64(w, fz.d2);
}
static void fp_load(pr_t *r, unsigned version)
{
    fz_defaults();
    for (int i = 0; i < NV; i++)
        fz.v[i] = pr_f64(r);
    fz.begin = pr_u8(r) != 0;
    fz.p1 = pr_f64(r);
    fz.p2 = pr_f64(r);
    fz.cfrate = pr_f64(r);
    fz.cf0 = pr_f64(r);
    unsigned n = pr_u8(r);
    fz.ncf = n >= 1 && n <= NCF ? (int)n : 1;
    for (unsigned i = 0; i < n; i++) { /* all n are read, so the fields after them line up */
        double v = pr_f64(r);
        if (i < NCF)
            fz.cf[i] = v;
    }
    fz.nom = pr_f64(r);
    fz.eff = pr_f64(r);
    fz.cy = pr_f64(r);
    fz.d1 = pr_f64(r);
    fz.d2 = pr_f64(r);
    if (r->err)
        fz_defaults();
}
const persist_def_t persist_finance = {"finance", 1, fp_save, fp_load, fz_defaults};
