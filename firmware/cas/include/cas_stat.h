/*
 * cas_stat.h -- probability distributions in double precision (src/statdist.c).
 *
 * Pure numeric C (no CAS context, no allocation), used by the CAS commands
 * normalcdf(...), tcdf(...), ... (src/stats.c) and directly by the
 * calculator's statistics app (tests, intervals, distribution screens). The
 * stub CAS build compiles the same file (cas_stub/statdist.c), so the app can
 * always call these.
 *
 * Accuracy target: relative error < 1e-12 for moderate parameters (checked
 * against SciPy/mpmath reference values in tests/unit/test_stat.c).
 * Invalid parameters return NAN. Infinite bounds are allowed (and |x| >= 1e99
 * is treated as infinite, the usual calculator convention).
 */
#ifndef CAS_STAT_H
#define CAS_STAT_H

/* building blocks (special functions; see DLMF ch. 8):
 *   I_x(a,b) = B(x; a,b) / B(a,b)       regularized incomplete beta, a, b > 0
 *   P(a,x)   = gamma(a,x) / Gamma(a)    regularized lower incomplete gamma, a > 0
 *   Q(a,x)   = 1 - P(a,x)               computed directly, not as 1 - P
 * x outside the support is clamped (P = 0 for x <= 0, I = 0 for x <= 0 and 1
 * for x >= 1); a or b <= 0 or NaN arguments give NAN. */
double sd_betainc(double a, double b, double x);  /* regularized I_x(a,b) */
double sd_gammp(double a, double x);              /* regularized lower P(a,x) */
double sd_gammq(double a, double x);              /* regularized upper Q(a,x) */
/* ln B(a,b) = lnGamma(a) + lnGamma(b) - lnGamma(a+b) */
double sd_lnbeta(double a, double b);

/* continuous: pdf(x, params), cdf over [lo, hi], inverse of the left tail
 *   ..pdf(x, ...)        density at x (0 outside the support)
 *   ..cdf(lo, hi, ...)   P(lo <= X <= hi); the area is signed, so lo > hi gives
 *                        a negative value; +-inf or |bound| >= 1e99 allowed
 *   inv..(p, ...)        the x with P(X <= x) = p, for 0 < p < 1 (else NAN)
 * Parameters: mu / sigma (> 0) for the normal, df (> 0) degrees of freedom for
 * t and chi-square, d1 / d2 (> 0) numerator / denominator degrees of freedom
 * for F. */
double sd_normpdf(double x, double mu, double sigma);
double sd_normcdf(double lo, double hi, double mu, double sigma);
double sd_invnorm(double p, double mu, double sigma);
double sd_tpdf(double x, double df);
double sd_tcdf(double lo, double hi, double df);
double sd_invt(double p, double df);
double sd_chi2pdf(double x, double df);
double sd_chi2cdf(double lo, double hi, double df);
double sd_invchi2(double p, double df);
double sd_fpdf(double x, double d1, double d2);
double sd_fcdf(double lo, double hi, double d1, double d2);
double sd_invf(double p, double d1, double d2);

/* discrete (x is rounded down for the cdfs, the usual calculator convention)
 *   binomial: n trials (non-negative integer), success probability p in [0,1]
 *   Poisson:  mean mu > 0
 *   geometric: success probability p in (0,1], support x = 1, 2, ...
 * The pdfs return 0 for non-integer or out-of-range x. */
double sd_binompdf(double n, double p, double x);
double sd_binomcdf(double n, double p, double x); /* P(X <= x) */
double sd_poissonpdf(double mu, double x);
double sd_poissoncdf(double mu, double x);
double sd_geometpdf(double p, double x);           /* first success on trial x */
double sd_geometcdf(double p, double x);

/* upper tail areas used by the hypothesis tests: P(Z >= z), P(T >= t), ...
 * Computed directly from the complementary functions, so tiny p-values keep
 * full relative accuracy instead of cancelling in 1 - cdf.  Z is standard
 * normal; T, chi-square and F take their degrees of freedom as above. */
double sd_norm_q(double z);
double sd_t_q(double t, double df);
double sd_chi2_q(double x, double df);
double sd_f_q(double x, double d1, double d2);

#endif
