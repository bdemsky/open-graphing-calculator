/*
 * Unit tests of the statistics app: distributions (cas/src/statdist.c),
 * statistics / regressions / tests / intervals (app/stat_math.c), the FAT
 * file layer on a RAM card (core/fat.c, including a card pulled mid-write),
 * CSV export/import of lists (app/stat_csv.c) and the CAS commands.
 *
 * Reference values were computed offline with SciPy 1.18 / NumPy 2.5 and
 * mpmath (40 digits) and pasted here as constants.
 *
 * NEAR() compares with a relative tolerance chosen per value: ~1e-14 where
 * the algorithm should be accurate to a few ulps, looser for iterative
 * fits (logistic and sinusoidal regressions converge to ~1e-7) and for
 * p-values computed from tail integrals of extreme statistics.
 *
 * The FAT and CSV tests use the simulator's RAM microSD card
 * (hal_sd_sim_insert) and its fault injection: sim_sd_writes_left >= 0 makes
 * every block write after that many fail, like a card pulled mid-write.
 * The CAS commands are only checked with the real CAS (skipped for
 * CAS=stub). Built and run by `make test` (tests/unit/test_*.c, linked with
 * the simulator library and the configured CAS).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../platform/sim/sim.h"
#include "cas.h"
#include "cas_stat.h"
#include "fat.h"
#include "hal.h"
#include "session.h"
#include "stat.h"
#include "stat_math.h"
#include "ui.h"

static int fails, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if (!(c)) {                                                               \
            fails++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

/* Relative-error check behind NEAR(); `want` near 0 is compared against an
 * absolute floor of 1e-300 so exact zeros still need a tiny result. */
static void near_(double got, double want, double tol, const char *what, int line)
{
    checks++;
    double err = fabs(got - want) / fmax(fabs(want), 1e-300);
    if (!(err <= tol)) {
        fails++;
        fprintf(stderr, "test_stat.c:%d: %s = %.17g, want %.17g (rel err %.2e > %.0e)\n", line, what, got, want, err,
                tol);
    }
}
#define NEAR(got, want, tol) near_((got), (want), (tol), #got, __LINE__)

/* ------------------------------------------------------------ distributions */
/* Densities, cumulative probabilities (lower, upper bounds; +-1e99 stands for
 * infinity as on the calculator), and inverses of the normal, t, chi-square,
 * F, binomial, Poisson and geometric distributions, plus the regularized
 * incomplete beta and gamma functions they are built on. The cases include
 * far tails (normcdf below -30), large parameters (n = 1000, df = 300,
 * lambda = 1000) and reversed bounds; invalid parameters give NaN and a
 * non-integer binomial k gives 0. Finally inverse(cdf) round trips. */
static void test_distributions(void)
{
    /* mpmath (40 digits) unless noted */
    NEAR(sd_normpdf(1.5, 0, 1), 0.12951759566589174, 1e-14);
    NEAR(sd_normcdf(-1, 1, 0, 1), 0.68268949213708585, 1e-14);
    NEAR(sd_normcdf(5, 1e99, 0, 1), 2.8665157187919391e-07, 1e-13);
    NEAR(sd_normcdf(-1e99, -30, 0, 1), 4.9067139271481870595e-198, 1e-12);
    NEAR(sd_normcdf(70, 80, 75, 4), 0.78870045266628952, 1e-14);
    NEAR(sd_normcdf(1, 0, 0, 1), -0.34134474606854293, 1e-13); /* reversed bounds: negative (usual convention) */
    NEAR(sd_invnorm(0.975, 0, 1), 1.9599639845400543, 1e-14);
    NEAR(sd_invnorm(1e-10, 0, 1), -6.3613409024040566, 1e-14);
    NEAR(sd_invnorm(0.3, 10, 2), 8.9511989745839191, 1e-14);
    NEAR(sd_tpdf(0.5, 3), 0.31318091100882861, 1e-13);
    NEAR(sd_tcdf(-2, 2, 5), 0.89806052117014179, 1e-13);
    NEAR(sd_tcdf(3, 1e99, 10), 0.0066718275112847862, 1e-12);
    NEAR(sd_tcdf(-1e99, -8, 4), 0.00066194845460858362, 1e-12);
    NEAR(sd_tcdf(1.2, 1e99, 2.5), 0.16571941883293884, 1e-12);
    NEAR(sd_tcdf(-1e99, 0.3, 300), 0.61780754058592935, 1e-12);
    NEAR(sd_invt(0.975, 10), 2.2281388519862739, 1e-13);
    NEAR(sd_invt(0.001, 3), -10.214531852407385, 1e-13);
    NEAR(sd_invt(0.6, 1000), 0.25341451583949876477, 1e-12);
    NEAR(sd_invt(0.9, 1), 3.0776835371752536, 1e-13);
    NEAR(sd_chi2pdf(3, 4), 0.1673476201113224, 1e-13);
    NEAR(sd_chi2pdf(0.5, 1), 0.43939128946772243, 1e-13);
    NEAR(sd_chi2cdf(0, 3.84, 1), 0.94995647875129496, 1e-13);
    NEAR(sd_chi2cdf(20, 1e99, 5), 0.0012497305630313773, 1e-12);
    NEAR(sd_chi2cdf(900, 1100, 1000), 0.97466835378241501, 1e-12);
    NEAR(sd_invchi2(0.95, 7), 14.067140449340167, 1e-13);
    NEAR(sd_fpdf(1.5, 3, 10), 0.24338778073276723, 1e-13);
    NEAR(sd_fcdf(0, 2.5, 4, 12), 0.90183818882689548, 1e-13);
    NEAR(sd_fcdf(5, 1e99, 2, 30), 0.013363461010158065, 1e-12);
    NEAR(sd_invf(0.95, 3, 20), 3.0983912121407813, 1e-12);
    NEAR(sd_binompdf(20, 0.3, 5), 0.17886305056987978, 1e-13);
    NEAR(sd_binompdf(1000, 0.5, 500), 0.025225018178360797, 1e-13);
    NEAR(sd_binomcdf(20, 0.3, 5), 0.41637082944748088, 1e-13);
    NEAR(sd_binomcdf(1000, 0.01, 3), 0.010072654772014380352, 1e-12);
    NEAR(sd_poissonpdf(3.5, 2), 0.18495897346170082, 1e-13);
    NEAR(sd_poissonpdf(1000, 1000), 0.012614611348721499718, 1e-13);
    NEAR(sd_poissoncdf(3.5, 2), 0.32084719886213409, 1e-13);
    NEAR(sd_poissoncdf(50, 30), 0.0015940273186063065, 1e-12);
    NEAR(sd_geometpdf(0.2, 4), 0.1024, 1e-14);
    NEAR(sd_geometcdf(0.2, 4), 0.5904, 1e-14);
    NEAR(sd_betainc(2.5, 3.5, 0.4), 0.48690419152611741, 1e-13);
    NEAR(sd_betainc(200, 300, 0.41), 0.67762816477217891, 1e-12);
    NEAR(sd_gammp(3, 2), 0.32332358381693654, 1e-14);
    NEAR(sd_gammq(100, 120), 0.027863739890520663, 1e-12);
    NEAR(sd_gammp(0.5, 0.01), 0.1124629160182849, 1e-14);
    CHECK(isnan(sd_normpdf(0, 0, -1)));
    CHECK(isnan(sd_invnorm(1.5, 0, 1)));
    CHECK(sd_binompdf(10, 0.5, 2.5) == 0);
    /* inverse round trips */
    for (double p = 0.01; p < 1; p += 0.07) {
        NEAR(sd_tcdf(-1e99, sd_invt(p, 7.5), 7.5), p, 1e-12);
        NEAR(sd_chi2cdf(0, sd_invchi2(p, 3), 3), p, 1e-12);
        NEAR(sd_fcdf(0, sd_invf(p, 4, 9), 4, 9), p, 1e-12);
    }
}

/* ------------------------------------------------------------ statistics */
/* Work area for stat_math.c (sorting copies etc.), installed in main(). */
static double scratch[3000];

/* 1-Var Stats with frequencies (11 data points in total), then the quartile
 * convention without frequencies: Q1/Q3 are the medians of the lower and
 * upper halves, the median excluded when n is odd (1..9: Q1 = 2.5,
 * Q3 = 7.5). The mode is NaN when no value repeats. */
static void test_onevar(void)
{
    static const double x[] = {5, 1, 3, 2, 4, 7, 6}, f[] = {1, 2, 3, 1, 2, 1, 1};
    sres_t r;
    CHECK(st_onevar(x, f, 7, &r) == 0);
    NEAR(sr_get(&r, "xbar"), 3.5454545454545454, 1e-14);
    NEAR(sr_get(&r, "Sx"), 1.916436086262016, 1e-13);
    NEAR(sr_get(&r, "sigmax"), 1.827250112931071, 1e-13);
    NEAR(sr_get(&r, "n"), 11, 0);
    NEAR(sr_get(&r, "Med"), 3, 0);
    NEAR(sr_get(&r, "Q1"), 2, 0);
    NEAR(sr_get(&r, "Q3"), 5, 0);
    NEAR(sr_get(&r, "minX"), 1, 0);
    NEAR(sr_get(&r, "maxX"), 7, 0);
    NEAR(sr_get(&r, "mode"), 3, 0);
    NEAR(sr_get(&r, "Σx²"), 175, 1e-15);
    /* quartiles (medians of the halves), odd and even n, no frequencies */
    static const double a[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    st_onevar(a, NULL, 9, &r);
    NEAR(sr_get(&r, "Q1"), 2.5, 0);
    NEAR(sr_get(&r, "Q3"), 7.5, 0);
    CHECK(isnan(sr_get(&r, "mode"))); /* every value once: no mode */
    st_onevar(a, NULL, 8, &r);
    NEAR(sr_get(&r, "Q1"), 2.5, 0);
    NEAR(sr_get(&r, "Med"), 4.5, 0);
    NEAR(sr_get(&r, "Q3"), 6.5, 0);
    static const double b[] = {3, 1, 2, 3, 3, 2};
    st_onevar(b, NULL, 6, &r);
    NEAR(sr_get(&r, "mode"), 3, 0);
}

/* Nearly linear data (y ~ 2x + 0.3) shared by the regression and LinRegTTest
 * cases. */
static const double X[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
static const double Y[] = {2.3, 4.1, 6.2, 8.4, 9.9, 12.5, 13.8, 16.4, 18.1, 19.9};

/* Every regression model of st_regress(): coefficients gr.c[] in the order
 * defined in app/stat_math.h, r or r^2, the regression equation text
 * (r.eq must equal st_reg_expr()), frequencies acting as repeated points,
 * and refusal of a fit with too few points. Polynomial fits get looser
 * tolerances because the normal equations lose a few digits; the logistic
 * and sinusoidal fits are iterative and are compared with the least-squares
 * optimum from scipy.optimize.curve_fit. The Med-Med data replaces one point
 * so the medians of the three groups give a simple exact line. */
static void test_regressions(void)
{
    sres_t r;
    sreg_t gr;
    CHECK(st_regress(REG_LINAXB, X, Y, NULL, 10, &gr, &r) == 0);
    NEAR(gr.c[0], 1.9769696969696968, 1e-13);
    NEAR(gr.c[1], 0.28666666666666707, 1e-12);
    NEAR(gr.r, 0.99925613904898147, 1e-14);
    CHECK(st_regress(REG_LINABX, X, Y, NULL, 10, &gr, &r) == 0);
    NEAR(gr.c[1], 1.9769696969696968, 1e-13);
    static const double qc[] = {-0.004545454545454769, 2.0269696969696991, 0.18666666666666282};
    CHECK(st_regress(REG_QUAD, X, Y, NULL, 10, &gr, &r) == 0);
    for (int i = 0; i < 3; i++)
        NEAR(gr.c[i], qc[i], 1e-9);
    NEAR(gr.r2, 0.99854661365109643, 1e-13);
    static const double cc[] = {-0.0024087024087023961, 0.03519813519813457, 1.8436674436674472,
                                0.39333333333332759};
    CHECK(st_regress(REG_CUBIC, X, Y, NULL, 10, &gr, &r) == 0);
    for (int i = 0; i < 4; i++)
        NEAR(gr.c[i], cc[i], 1e-9);
    NEAR(gr.r2, 0.99860210889431389, 1e-13);
    static const double rc[] = {-0.00046620046620041969, 0.007847707847706881, -0.039860139860133072,
                                2.0487956487956325, 0.23333333333333875};
    CHECK(st_regress(REG_QUART, X, Y, NULL, 10, &gr, &r) == 0);
    for (int i = 0; i < 5; i++)
        NEAR(gr.c[i], rc[i], 1e-8);
    NEAR(gr.r2, 0.99861319639347901, 1e-12);
    CHECK(st_regress(REG_LN, X, Y, NULL, 10, &gr, &r) == 0);
    NEAR(gr.c[0], -0.58734142528419753, 1e-12);
    NEAR(gr.c[1], 7.7774235631146027, 1e-13);
    NEAR(gr.r, 0.9517545444268507, 1e-13);
    CHECK(st_regress(REG_EXP, X, Y, NULL, 10, &gr, &r) == 0);
    NEAR(gr.c[0], 2.7703889059271702, 1e-13);
    NEAR(gr.c[1], 1.2468392228349845, 1e-13);
    NEAR(gr.r, 0.95653769119212328, 1e-13);
    CHECK(st_regress(REG_PWR, X, Y, NULL, 10, &gr, &r) == 0);
    NEAR(gr.c[0], 2.2135632097335192, 1e-13);
    NEAR(gr.c[1], 0.95187328411257477, 1e-13);
    NEAR(gr.r, 0.99922899020086564, 1e-13);
    /* nonlinear fits: least-squares optimum (scipy curve_fit, tight tolerances) */
    static const double xl[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    static const double yl[] = {1.6, 2.4, 3.6, 4.9, 6.3, 7.5, 8.4, 9.0, 9.5, 9.7, 9.9, 9.95};
    CHECK(st_regress(REG_LOGISTIC, xl, yl, NULL, 12, &gr, &r) == 0);
    NEAR(gr.c[0], 5.4500964199743658, 1e-7);
    NEAR(gr.c[1], 0.5491315214040885, 1e-7);
    NEAR(gr.c[2], 10.097304752050906, 1e-7);
    static const double xs[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    static const double ys[] = {4.6, 4.9, 4.3, 2.9, 1.2, -0.3, -0.9, -0.6, 0.6, 2.4, 4.0, 4.9};
    CHECK(st_regress(REG_SIN, xs, ys, NULL, 12, &gr, &r) == 0);
    NEAR(gr.c[0], 2.9605772091845877, 1e-7);
    NEAR(gr.c[1], 0.59523308420024224, 1e-7);
    NEAR(gr.c[2], 0.4551291849053789, 1e-6);
    NEAR(gr.c[3], 2.0088461031860163, 1e-7);
    /* a logistic fit of pure exponential growth (no saturation) still
     * improves by 0.5 % per step at the 400-iteration cap: not converged */
    static const double xg[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    static const double yg[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048};
    CHECK(st_regress(REG_LOGISTIC, xg, yg, NULL, 12, &gr, &r) != 0 && r.err && strstr(r.err, "converge"));
    double ym[10];
    memcpy(ym, Y, sizeof ym);
    ym[4] = 11.5;
    CHECK(st_regress(REG_MEDMED, X, ym, NULL, 10, &gr, &r) == 0);
    NEAR(gr.c[0], 2.0000000000000004, 1e-14);
    NEAR(gr.c[1], 0.39999999999999858, 1e-13);
    /* the equation text evaluates back to the model */
    char eq[160];
    st_regress(REG_LINAXB, X, Y, NULL, 10, &gr, &r);
    st_reg_expr(&gr, eq, sizeof eq);
    CHECK(!strcmp(eq, r.eq));
    CHECK(strstr(eq, "*x+") != NULL);
    /* frequencies = repeated points */
    static const double xf[] = {1, 2, 3}, yf[] = {1, 3, 2}, ff[] = {2, 1, 3};
    static const double xe[] = {1, 1, 2, 3, 3, 3}, ye[] = {1, 1, 3, 2, 2, 2};
    sreg_t g2;
    st_regress(REG_LINAXB, xf, yf, ff, 3, &gr, &r);
    st_regress(REG_LINAXB, xe, ye, NULL, 6, &g2, &r);
    NEAR(gr.c[0], g2.c[0], 1e-14);
    NEAR(gr.r, g2.r, 1e-14);
    CHECK(st_regress(REG_LN, xf, (const double[]){1, 2, 3}, NULL, 1, &gr, &r) != 0);
}

/* Two samples (n = 8 and 7) for the t, 2-sample and F tests/intervals. */
static const double D1[] = {5.1, 4.9, 5.6, 5.8, 6.0, 5.2, 4.7, 5.5};
static const double D2[] = {4.2, 4.8, 5.0, 4.4, 4.6, 5.1, 4.3};

/* Hypothesis tests and confidence intervals (app/stat_math.c), from
 * summary statistics as entered in the STAT forms: z and t tests (two- and
 * one-sided), 2-sample t (Welch with fractional df, and pooled), 1- and
 * 2-proportion z, chi-square goodness of fit (with per-cell contributions)
 * and two-way (with expected counts), 2-sample F, LinRegTTest, one-way
 * ANOVA; intervals with the confidence level as a fraction or a percent
 * (90 = 0.90); an impossible level and a t test with n = 1 are errors. */
static void test_tests(void)
{
    sres_t r;
    double n1, m1, s1, n2, m2, s2;
    st_summary(D1, NULL, 8, &n1, &m1, &s1);
    st_summary(D2, NULL, 7, &n2, &m2, &s2);
    CHECK(st_ztest(3, 2, 3.8, 5, ALT_NE, &r) == 0);
    NEAR(sr_get(&r, "z"), 0.89442719099991574, 1e-14);
    NEAR(r.p, 0.37109336952269767, 1e-13);
    CHECK(st_ttest(5.0, m1, s1, n1, ALT_NE, &r) == 0);
    NEAR(sr_get(&r, "t"), 2.1979503896796562, 1e-13);
    NEAR(r.p, 0.063924122938865, 1e-12);
    st_ttest(5.0, m1, s1, n1, ALT_GT, &r);
    NEAR(r.p, 0.0319620614694325, 1e-12);
    st_2sampt(m1, s1, n1, m2, s2, n2, ALT_NE, false, &r);
    NEAR(sr_get(&r, "t"), 3.4855227152681518, 1e-13);
    NEAR(sr_get(&r, "df"), 12.852309807101415, 1e-13);
    NEAR(r.p, 0.0040882380379202513, 1e-11);
    st_2sampt(m1, s1, n1, m2, s2, n2, ALT_NE, true, &r);
    NEAR(sr_get(&r, "t"), 3.4242206932850943, 1e-13);
    NEAR(r.p, 0.0045267302138788908, 1e-11);
    st_1propz(0.5, 58, 100, ALT_GT, &r);
    NEAR(sr_get(&r, "z"), 1.6, 1e-14);
    NEAR(r.p, 0.054799291699558071, 1e-13);
    st_2propz(45, 80, 30, 75, ALT_NE, &r);
    NEAR(sr_get(&r, "z"), 2.0231086846731685, 1e-13);
    NEAR(r.p, 0.043061943359532603, 1e-12);
    static const double obs[] = {18, 22, 30, 30}, ex[] = {25, 25, 25, 25};
    double contrib[4];
    st_chi2gof(obs, ex, 4, 3, &r, contrib);
    NEAR(sr_get(&r, "chi²"), 4.32, 1e-14);
    NEAR(r.p, 0.22891886433610481, 1e-12);
    NEAR(contrib[0], 49.0 / 25, 1e-14);
    static const double m[] = {20, 30, 25, 30, 20, 25};
    double expd[6];
    st_chi2test(m, 2, 3, &r, expd);
    NEAR(sr_get(&r, "chi²"), 4, 1e-14);
    NEAR(r.p, 0.1353352832366127, 1e-13);
    NEAR(sr_get(&r, "df"), 2, 0);
    NEAR(expd[0], 25, 1e-14);
    st_2sampf(s1, n1, s2, n2, ALT_NE, &r);
    NEAR(sr_get(&r, "F"), 1.6575875486381326, 1e-13);
    NEAR(r.p, 0.55475741764340947, 1e-12);
    st_linregt(X, Y, NULL, 10, ALT_NE, &r);
    NEAR(sr_get(&r, "t"), 73.289493266914093, 1e-12);
    NEAR(r.p, 1.33831203332978e-12, 1e-6);
    NEAR(sr_get(&r, "s"), 0.2450108222717986, 1e-12);
    static const double g3[] = {6.1, 5.9, 6.5, 6.8};
    const double *d[3] = {D1, D2, g3};
    int len[3] = {8, 7, 4};
    st_anova(d, len, 3, &r);
    NEAR(sr_get(&r, "F"), 22.336736799413703, 1e-12);
    NEAR(r.p, 2.3386702264544239e-05, 1e-10);
    /* intervals */
    st_zint(2, 3.8, 5, 0.95, &r);
    NEAR(sr_get(&r, "lower"), 2.0469549188468368, 1e-13);
    NEAR(sr_get(&r, "upper"), 5.5530450811531633, 1e-13);
    /* the estimate and the level have their own fields (not stat / df2) */
    CHECK(r.est == 3.8 && r.level == 0.95 && r.dist == DIST_NONE && r.df2 == 0 && r.stat == 0);
    st_tint(m1, s1, n1, 90, &r); /* percent accepted */
    NEAR(sr_get(&r, "lower"), 5.0483086993705317, 1e-13);
    NEAR(sr_get(&r, "upper"), 5.6516913006294676, 1e-13);
    st_2samptint(m1, s1, n1, m2, s2, n2, 0.95, false, &r);
    NEAR(sr_get(&r, "lower"), 0.27375562552101312, 1e-11);
    NEAR(sr_get(&r, "upper"), 1.1691015173361299, 1e-12);
    st_1propzint(58, 100, 0.90, &r);
    NEAR(sr_get(&r, "lower"), 0.49881684993684694, 1e-13);
    NEAR(sr_get(&r, "upper"), 0.66118315006315298, 1e-13);
    CHECK(st_zint(2, 3.8, 5, 150, &r) != 0 && r.err);
    CHECK(st_ttest(0, 1, 1, 1, ALT_NE, &r) != 0);
}

/* ------------------------------------------------------------ FAT */
static char rbuf[70000];

/* Read up to max bytes of a file; returns the byte count or -1 if it
 * cannot be opened. */
static int read_file(const char *path, char *buf, int max)
{
    fs_file_t f;
    if (fs_open(&f, path, FS_READ))
        return -1;
    int n = fs_read(&f, buf, (size_t)max);
    fs_close(&f);
    return n;
}

/* Create or replace a file in 1000-byte fs_write() calls; returns 0 or an
 * FS_E* error. */
static int write_file(const char *path, const char *data, size_t n)
{
    fs_file_t f;
    int r = fs_open(&f, path, FS_WRITE);
    if (r)
        return r;
    for (size_t o = 0; o < n; o += 1000) /* several writes */
        if ((r = fs_write(&f, data + o, n - o < 1000 ? n - o : 1000)) < 0)
            return r;
    return fs_close(&f);
}

/* fs_list() callback: counts directory entries. */
static int count_cb(const fs_dirent_t *e, void *user)
{
    (*(int *)user)++;
    return 0;
}

/* FAT layer on a blank RAM card of `mb` MB, formatted by fs_mkfs() (FAT16
 * below 64 MB, FAT32 from 64 MB): multi-cluster files, replacing a file
 * frees its old clusters, case-insensitive 8.3 names, directories growing
 * past one cluster, remove, name/path errors, and the crash-safety
 * guarantee of fs_close() (a new version is published by a single
 * directory-entry write) checked by pulling the card during writes. */
static void test_fat_card(unsigned mb)
{
    CHECK(hal_sd_sim_insert(mb) == 0);
    CHECK(fs_mount() == FS_ENOFS); /* blank card */
    fs_unmount();
    CHECK(fs_mkfs() == 0);
    uint32_t blocks, free0, free1;
    int type;
    CHECK(fs_info(&blocks, &free0, &type) == 0);
    CHECK(type == (mb >= 64 ? 32 : 16));
    CHECK(blocks == mb * 2048u);
    /* a file of 65 KB crosses many clusters */
    static char data[66000];
    for (size_t i = 0; i < sizeof data; i++)
        data[i] = (char)('A' + (i * 7 + i / 13) % 26);
    CHECK(write_file("/big.dat", data, sizeof data) == 0);
    CHECK(read_file("/BIG.DAT", rbuf, sizeof rbuf) == (int)sizeof data);
    CHECK(!memcmp(rbuf, data, sizeof data));
    /* replace with a short version: the old clusters are freed */
    CHECK(write_file("/big.dat", "hello\n", 6) == 0);
    CHECK(read_file("/big.dat", rbuf, sizeof rbuf) == 6 && !memcmp(rbuf, "hello\n", 6));
    CHECK(fs_info(&blocks, &free1, &type) == 0);
    CHECK(free0 - free1 <= 32); /* one cluster in use (<= 32 KB) */
    /* directories, many files (extends a directory past one cluster) */
    CHECK(fs_mkdir("/calc") == 0);
    CHECK(fs_mkdir("/CALC") == 0); /* exists */
    char name[32];
    for (int i = 0; i < 40; i++) {
        snprintf(name, sizeof name, "/calc/f%02d.txt", i);
        CHECK(write_file(name, name, strlen(name)) == 0);
    }
    int n = 0;
    CHECK(fs_list("/calc", count_cb, &n) == 0);
    CHECK(n == 40);
    CHECK(read_file("/calc/F17.TXT", rbuf, 100) == (int)strlen("/calc/f17.txt"));
    CHECK(fs_remove("/calc/f17.txt") == 0);
    CHECK(read_file("/calc/f17.txt", rbuf, 100) < 0);
    n = 0;
    fs_list("/calc", count_cb, &n);
    CHECK(n == 39);
    CHECK(fs_open(&(fs_file_t){0}, "/.hidden.csv", FS_WRITE) == FS_ENAME); /* (long names are written since the notebooks) */
    CHECK(write_file("/nodir/x.txt", "x", 1) == FS_ENOENT);
    /* the card is pulled while a new version is written: the old file survives */
    CHECK(write_file("/keep.csv", data, 5000) == 0);
    fs_unmount();
    fs_file_t f;
    CHECK(fs_open(&f, "/keep.csv", FS_WRITE) == 0);
    sim_sd_writes_left = 3; /* the 4th block write fails, before the file is complete */
    int r = 0;
    for (int i = 0; i < 20 && r >= 0; i++)
        r = fs_write(&f, "0123456789012345678901234567890123456789012345678901234567890123456789", 70);
    if (r >= 0)
        r = fs_close(&f);
    CHECK(r < 0);
    sim_sd_writes_left = -1;
    fs_unmount();
    CHECK(read_file("/keep.csv", rbuf, sizeof rbuf) == 5000 && !memcmp(rbuf, data, 5000));
    /* removal right at the directory update: either version is complete.
     * Cutting after 0..11 block writes covers every write of a small
     * replacement, including the directory-entry update in fs_close(). */
    for (int cut = 0; cut < 12; cut++) {
        CHECK(write_file("/v.txt", "old version", 11) == 0);
        fs_unmount();
        CHECK(fs_open(&f, "/v.txt", FS_WRITE) == 0);
        sim_sd_writes_left = cut;
        fs_write(&f, "new", 3);
        fs_close(&f);
        sim_sd_writes_left = -1;
        fs_unmount();
        int k = read_file("/v.txt", rbuf, 100);
        CHECK((k == 11 && !memcmp(rbuf, "old version", 11)) || (k == 3 && !memcmp(rbuf, "new", 3)));
    }
    fs_unmount();
    hal_sd_sim_insert(0);
    CHECK(fs_mount() == FS_ENOCARD);
}

/* ------------------------------------------------------------ CSV */
/* CSV export/import of statistics lists (app/stat_csv.c) on an 8 MB RAM
 * card. The list pool is a local static array handed to stat_mem_init().
 * Export: a header row of list names, CRLF lines, shorter lists leave empty
 * cells; the slot indices passed (0, 1, 7) select L1, L2 and the first named
 * list. Import: values round-trip exactly (15 significant digits, 17 when
 * 15 do not reproduce the double, e.g. 1/3), and the
 * importer accepts ';' separators with decimal commas, quoted fields, LF or
 * CRLF lines, files without a header (columns go to L1, L2, ...) and header
 * names that create new named lists. */
static void test_csv(void)
{
    static double pool[200000];
    stat_mem_init(pool, true);
    hal_sd_sim_insert(8);
    CHECK(fs_mkfs() == 0);
    static const double a[] = {1, 2.5, -3e-7, 1.0 / 3};
    static const double b[] = {10, 20};
    sl_set(sl_find("L1"), a, 4);
    sl_set(sl_find("L2"), b, 2);
    slist_t *w = sl_create("WT");
    CHECK(w != NULL);
    sl_set(w, b, 2);
    int slots[3] = {0, 1, 7};
    char msg[96];
    CHECK(stat_csv_export("/T.CSV", slots, 3, msg, sizeof msg) == 0);
    CHECK(read_file("/T.CSV", rbuf, sizeof rbuf) > 0);
    CHECK(!strncmp(rbuf, "L1,L2,WT\r\n1,10,10\r\n2.5,20,20\r\n", 30));
    sl_set(sl_find("L1"), NULL, 0);
    sl_set(sl_find("L2"), NULL, 0);
    CHECK(stat_csv_import("/T.CSV", msg, sizeof msg) == 0);
    slist_t *l1 = sl_find("L1"), *l2 = sl_find("L2");
    CHECK(l1->len == 4 && l2->len == 2);
    for (int i = 0; i < 4; i++)
        CHECK(l1->v[i] == a[i]); /* exact round trip */
    /* semicolons, decimal commas, quotes, no header -> L1, L2 */
    static const char csv2[] = "\"1,5\";2\r\n3;4\n5;\n";
    CHECK(write_file("/EU.CSV", csv2, sizeof csv2 - 1) == 0);
    CHECK(stat_csv_import("/EU.CSV", msg, sizeof msg) == 0);
    CHECK(l1->len == 3 && l1->v[0] == 1.5 && l1->v[2] == 5);
    CHECK(l2->len == 2 && l2->v[1] == 4);
    /* header with a new name creates a named list */
    static const char csv3[] = "height,weight\n1.80,75\n1.65,60\n";
    CHECK(write_file("/HW.CSV", csv3, sizeof csv3 - 1) == 0);
    CHECK(stat_csv_import("/HW.CSV", msg, sizeof msg) == 0);
    slist_t *hl = sl_find("height");
    CHECK(hl && hl->len == 2 && hl->v[0] == 1.80);
    hl = sl_find("weight");
    CHECK(hl && hl->len == 2 && hl->v[1] == 60);
    /* a header that is not a list name goes by position, but never into a
     * list another header names: "x y" -> L2 (L1 is named by column 2) */
    static const char csv4[] = "x y,L1\n1,2\n3,4\n";
    CHECK(write_file("/POS.CSV", csv4, sizeof csv4 - 1) == 0);
    CHECK(stat_csv_import("/POS.CSV", msg, sizeof msg) == 0);
    CHECK(l1->len == 2 && l1->v[0] == 2 && l1->v[1] == 4);
    CHECK(l2->len == 2 && l2->v[0] == 1 && l2->v[1] == 3);
    fs_unmount();
    hal_sd_sim_insert(0);
}

/* ------------------------------------------------------------ saved lists */
/* persist_stat_lists: named lists are packed into the first free slots when
 * loading, so a list after a deleted one moves; the editor columns are saved
 * as positions among the saved lists and still name the same lists. */
extern const persist_def_t persist_stat_lists;
static void test_lists_persist(void)
{
    static double pool[200000];
    stat_mem_init(pool, true);
    slist_t *a = sl_create("AA"), *b = sl_create("BB"), *c = sl_create("CC");
    CHECK(a && b && c);
    sl_set(b, (const double[]){1, 2}, 2);
    sl_set(c, (const double[]){3}, 1);
    a->used = false; /* AA deleted: a gap before BB and CC */
    sl_ncols = 0;
    sl_cols[sl_ncols++] = 0;                    /* L1 */
    sl_cols[sl_ncols++] = (int)(c - sl_at(0)); /* CC */
    sl_cols[sl_ncols++] = (int)(b - sl_at(0)); /* BB */
    static uint8_t buf[4096];
    pw_t w = {buf, sizeof buf, 0, false};
    persist_stat_lists.save(&w);
    CHECK(!w.overflow);
    persist_stat_lists.reset();
    pr_t rd = {buf, w.len, 0, false};
    persist_stat_lists.load(&rd, persist_stat_lists.version);
    CHECK(sl_ncols == 3);
    CHECK(sl_at(sl_cols[0]) && !strcmp(sl_at(sl_cols[0])->name, "L1"));
    CHECK(sl_at(sl_cols[1]) && !strcmp(sl_at(sl_cols[1])->name, "CC") && sl_at(sl_cols[1])->v[0] == 3);
    CHECK(sl_at(sl_cols[2]) && !strcmp(sl_at(sl_cols[2])->name, "BB") && sl_at(sl_cols[2])->len == 2);
}

/* ------------------------------------------------------------ CAS */
/* The distribution functions as CAS commands (cas/src/stats.c): exact
 * rational results for discrete distributions with rational arguments,
 * list arguments mapped element-wise (binompdf without k lists every k),
 * symbolic arguments left unevaluated, infinite bounds, a domain error, and
 * the same functions inside compiled (graphing) code. Real CAS only. */
static void test_cas(void)
{
    if (!strcmp(CAS_IMPL, "stub"))
        return;
    size_t size = 8u << 20;
    void *arena = malloc(size);
    cas_ctx *c = cas_init(arena, size);
    char out[512];
    static const struct {
        const char *in, *out;
    } t[] = {
        {"normalcdf(-1,1)", "0.682689492137"},
        {"invNorm(0.975)", "1.95996398454"},
        {"binompdf(3,1/2,1)", "3/8"},
        {"binompdf(3,1/2)", "[1/8,3/8,3/8,1/8]"},
        {"binomcdf(10,1/3,4)", "15488/19683"},
        {"binompdf(3,0.5,1)", "0.375"},
        {"geometcdf(1/6,3)", "91/216"},
        {"tcdf(-2,2,5)", "0.89806052117"},
        {"chi2cdf(0,3.84,1)", "0.949956478751"},
        {"normalpdf([0,1])", "[0.398942280401,0.241970724519]"},
        {"normalpdf(x)", "normalpdf(x)"},
        {"normalcdf(-inf,0)", "0.5"},
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        cas_eval(c, t[i].in, out, sizeof out);
        checks++;
        if (strcmp(out, t[i].out)) {
            fails++;
            fprintf(stderr, "test_stat.c: %s = %s, want %s\n", t[i].in, out, t[i].out);
        }
    }
    CHECK(cas_eval(c, "invNorm(2)", out, sizeof out) == CAS_ERR_DOMAIN);
    /* a list with a complex element: that element is NaN, the others are kept */
    g.cas = c;
    char err[64];
    slist_t *l = sl_find("L3");
    CHECK(sl_eval_into(l, "[1,1+2*i,3]", err, sizeof err) == 0);
    CHECK(l->len == 3 && l->v[0] == 1 && isnan(l->v[1]) && l->v[2] == 3);
    g.cas = NULL;
    /* compiled (graphing) */
    cas_fn *fn = NULL;
    CHECK(cas_compile(c, "normalpdf(x,1,2)+tcdf(-x,x,3)", "x", &fn) == CAS_OK && fn);
    if (fn) {
        NEAR(cas_fn_eval(fn, 1.5), sd_normpdf(1.5, 1, 2) + sd_tcdf(-1.5, 1.5, 3), 1e-15);
        cas_fn_free(c, fn);
    }
    free(arena);
}

/* The FAT tests run twice: 8 MB formats as FAT16, 70 MB as FAT32. */
int main(void)
{
    st_scratch = scratch;
    st_scratch_cap = 3000;
    test_distributions();
    test_onevar();
    test_regressions();
    test_tests();
    test_fat_card(8);   /* FAT16 */
    test_fat_card(70);  /* FAT32 */
    test_csv();
    test_lists_persist();
    test_cas();
    printf("test_stat: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
