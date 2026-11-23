/*
 * stat_math.h -- statistics computations of the STAT app (app/stat_math.c):
 * one/two-variable statistics, regressions, hypothesis tests and confidence
 * intervals. Pure functions of double arrays (no UI, no CAS), unit-tested in
 * tests/unit/test_stat.c. Distributions: cas/include/cas_stat.h.
 */
#ifndef STAT_MATH_H
#define STAT_MATH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* double constants (NAN / INFINITY are float: -Wdouble-promotion) */
#define DNAN ((double)NAN)
#define DINF ((double)INFINITY)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* scratch for sorting / transformed data: at least 3n doubles (set by stat_lists.c) */
extern double *st_scratch;
extern int st_scratch_cap;

/* A result: up to SR_MAX labelled values (shown in order), plus what the
 * shaded-curve drawing needs. `err` is set instead on failure.
 * Results are plain values (copied freely: stat_last keeps the last one for
 * VARS > Statistics); labels and title point to string literals. */
#define SR_MAX 24
/* distribution of a test statistic under H0 (DIST_NONE: nothing to draw) */
enum { DIST_NONE = 0, DIST_NORM, DIST_T, DIST_CHI2, DIST_F };
/* alternative hypothesis: two-sided (≠), lower tail (<), upper tail (>);
 * the order matches the choice fields of the test forms (stat_menu.c) */
enum { ALT_NE = 0, ALT_LT, ALT_GT };
typedef struct {
    const char *title;          /* screen title, e.g. "T-Test" */
    int n;                      /* number of lines used in lab[] / val[] */
    const char *lab[SR_MAX];    /* line labels ("xbar", "p", "r²", ...) */
    double val[SR_MAX];         /* line values (NAN: shown as undefined) */
    const char *err;   /* NULL = ok */
    /* drawing: distribution of the statistic, its value, the tail(s)
     * (df1/df2: degrees of freedom of t/χ²/F; p: the p-value) */
    int dist, alt;
    double df1, df2, stat, p;
    /* intervals: the estimate and the confidence level as a fraction */
    double est, level;
    /* text of an equation (regressions), CAS syntax */
    char eq[160];
} sres_t;
void sr_add(sres_t *r, const char *lab, double v); /* append a line (dropped past SR_MAX) */
double sr_get(const sres_t *r, const char *lab); /* NAN if absent */

/* data: x[i] with frequency f[i] (f == NULL: all 1)
 * All st_* functions below return 0 on success or -1 with r->err set. */
int st_onevar(const double *x, const double *f, int n, sres_t *r);
int st_twovar(const double *x, const double *y, const double *f, int n, sres_t *r);

/* regression models (the order of the CALC menu and of reg_names[]):
 * y = ax+b, a+bx, quadratic, cubic, quartic, a+b ln x, a b^x, a x^b,
 * c/(1+a e^(-bx)), a sin(bx+c)+d, median-median line ax+b */
enum {
    REG_LINAXB = 0, REG_LINABX, REG_QUAD, REG_CUBIC, REG_QUART, REG_LN, REG_EXP, REG_PWR,
    REG_LOGISTIC, REG_SIN, REG_MEDMED, REG_COUNT
};
/* A fitted model: enough to evaluate it (st_reg_eval) or print it
 * (st_reg_expr). The last fit is kept in stat_last_reg (stat.h). */
typedef struct {
    int type;    /* REG_* */
    double c[5]; /* a, b, c, d, e in the usual naming of each model */
    int nc;      /* number of coefficients used */
    double r, r2; /* r: NAN if not defined for the model */
} sreg_t;
extern const char *const reg_names[REG_COUNT];
/* fit; 0 = ok, else r->err set. Also fills the result lines (a, b, ..., r², r). */
int st_regress(int type, const double *x, const double *y, const double *f, int n, sreg_t *g, sres_t *r);
double st_reg_eval(const sreg_t *g, double x); /* model value at x (NAN outside its domain) */
/* equation in CAS syntax ("0.5*x+2"), full precision */
void st_reg_expr(const sreg_t *g, char *buf, size_t n);

/* number as CAS text: shortest round-trip digits, "(1.5*10^(-7))", negative in parentheses */
void st_num_text(char *b, size_t n, double v);

/* hypothesis tests (alt: ALT_NE / ALT_LT / ALT_GT for mu != / < / > mu0)
 * Sample arguments are summary statistics (mean, Sx, n; n as a double so a
 * frequency-weighted count fits); st_summary turns a list into them. Each
 * fills the statistic, p-value and drawing data of r. Formulas are given at
 * each function in stat_math.c. */
int st_ztest(double mu0, double sigma, double xbar, double n, int alt, sres_t *r);
int st_ttest(double mu0, double xbar, double sx, double n, int alt, sres_t *r);
int st_2sampz(double s1, double s2, double x1, double n1, double x2, double n2, int alt, sres_t *r);
int st_2sampt(double x1, double s1, double n1, double x2, double s2, double n2, int alt, bool pooled, sres_t *r);
int st_1propz(double p0, double x, double n, int alt, sres_t *r);
int st_2propz(double x1, double n1, double x2, double n2, int alt, sres_t *r);
/* contrib (may be NULL): per-category terms (O-E)²/E, n entries */
int st_chi2gof(const double *obs, const double *exp, int n, double df, sres_t *r, double *contrib);
/* observed r x c matrix (row-major, 2..32 each way); expected written to `expected` (may be NULL) */
int st_chi2test(const double *obs, int rows, int cols, sres_t *r, double *expected);
int st_2sampf(double s1, double n1, double s2, double n2, int alt, sres_t *r);
int st_linregt(const double *x, const double *y, const double *f, int n, int alt, sres_t *r);
/* one-way ANOVA of k groups (data[i], len[i]) */
int st_anova(const double *const *data, const int *len, int k, sres_t *r);

/* confidence intervals (level C in (0,1), or a percentage 1..100) */
int st_zint(double sigma, double xbar, double n, double C, sres_t *r);
int st_tint(double xbar, double sx, double n, double C, sres_t *r);
int st_2sampzint(double s1, double s2, double x1, double n1, double x2, double n2, double C, sres_t *r);
int st_2samptint(double x1, double s1, double n1, double x2, double s2, double n2, double C, bool pooled,
                 sres_t *r);
int st_1propzint(double x, double n, double C, sres_t *r);
int st_2propzint(double x1, double n1, double x2, double n2, double C, sres_t *r);

/* sample summary for the "Data" input of the tests: n, mean, Sx */
int st_summary(const double *x, const double *f, int n, double *cnt, double *mean, double *sx);

/* normal probability plot: z-score of the i-th smallest of n (0-based) */
double st_normprob_z(int i, int n);
/* sort ascending in place (heap sort; stable merge sort is not needed: values only) */
void st_sort(double *v, int n);
/* quartiles of sorted data with integer frequencies expanded (medians of the halves, the usual calculator method) */
void st_quartiles(const double *sorted, int n, double *q1, double *med, double *q3);

#endif
