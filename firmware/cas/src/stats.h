/*
 * stats.h -- statistics commands of the CAS (src/stats.c): probability
 * distributions callable from expressions (normalcdf(...), binompdf(...), ...).
 * The numeric work is in src/statdist.c (include/cas_stat.h).
 *
 * Registration: STATS_SYMS is expanded inside BUILTIN_SYMS (cas_int.h), so
 * the names are builtin command symbols; eval_cmd (cmds.c) forwards ids in
 * [C_NORMALPDF, C_GEOMETCDF] to stats_cmd(); the numeric compiler and the
 * complex evaluator (numeric.c) call stats_num() for graphing.
 *
 * Adding a distribution means: append X(...) to STATS_SYMS (keeping
 * C_NORMALPDF first and C_GEOMETCDF last, or updating STATS_IS_CMD), add a
 * row to the arity table in stats.c in the same order, and a case in
 * stats_num.  Because the ids live in the command range of BUILTIN_SYMS,
 * inserting names shifts the ids of everything after them; nothing stores
 * builtin ids across sessions (session.c saves names), so that is safe.
 */
#ifndef CAS_STATS_H
#define CAS_STATS_H

/* X-macro list of the distribution command names, expanded inside
 * BUILTIN_SYMS(X) in cas_int.h (so each becomes a C_* builtin id).  Grouped
 * as pdf / cdf / inverse for each continuous distribution, then pdf / cdf for
 * the discrete ones; stats.c's arity table relies on this order. */
#define STATS_SYMS(X) \
    X(C_NORMALPDF, "normalpdf") X(C_NORMALCDF, "normalcdf") X(C_INVNORM, "invNorm") \
    X(C_TPDF, "tpdf") X(C_TCDF, "tcdf") X(C_INVT, "invT") \
    X(C_CHI2PDF, "chi2pdf") X(C_CHI2CDF, "chi2cdf") X(C_INVCHI2, "invChi2") \
    X(C_FPDF, "Fpdf") X(C_FCDF, "Fcdf") X(C_INVF, "invF") \
    X(C_BINOMPDF, "binompdf") X(C_BINOMCDF, "binomcdf") X(C_POISSONPDF, "poissonpdf") \
    X(C_POISSONCDF, "poissoncdf") X(C_GEOMETPDF, "geometpdf") X(C_GEOMETCDF, "geometcdf")

/* 1 if the symbol id is one of the distribution commands above (they form a
 * contiguous id range). */
#define STATS_IS_CMD(id) ((id) >= C_NORMALPDF && (id) <= C_GEOMETCDF)

struct E;
/* Evaluate distribution command `id` on n already-evaluated arguments a[]
 * (called from eval_cmd).  Returns a float (or an exact rational for
 * binomial / geometric probabilities in exact mode), a list when an argument
 * is a list or for binompdf(n,p) / binomcdf(n,p), or the call itself
 * (unevaluated) when an argument is symbolic.  Throws CAS_ERR_DOMAIN on a
 * wrong argument count, non-real constant arguments or parameters outside
 * the distribution's domain.  Result in scratch. */
struct E *stats_cmd(int id, uint32_t n, struct E **a);
/* numeric value for n real arguments (NAN outside the domain / bad arity) */
double stats_num(int id, int n, const double *x);

#endif
