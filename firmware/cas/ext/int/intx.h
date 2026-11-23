/* intx.h -- the casx integrator (cas/ext/int): shared helpers of intx.c, intrad.c, intrat.c */
#ifndef INTX_H
#define INTX_H
#include "casx_mod.h"

E *ix_int(XP, E *g, E *x);               /* the core's integrate; NULL unless complete */
int ix_verify(XP, E *f, E *F, E *x);     /* F' = f at sample points */
int ix_verify_neg(XP, E *f, E *F, E *x); /* and at negative x where f is real */
int ix_verify_safe(XP, E *f, E *F, E *x); /* ix_verify; an error in F' gives 0 */
E *ix_powsub(XP, E *f, E *x);
E *ix_trigsub(XP, E *f, E *x);           /* inttrig.c: u = sin w, cos w, tan w (hyperbolic too) */            /* intrad.c: u = x^n, then radicals */
int ix_has_fn(XP, E *e, int id);
void ix_syms(XP, E *e, E *x, E **out, uint32_t *n, uint32_t cap);
int ix_ratx(XP, E *e, E *x);             /* e is a rational function of x */
E *ix_radsub(XP, E *f, E *x);            /* intrad.c: radicals of <= 2 linear forms */
E *ix_ratbin(XP, E *f, E *x);            /* intrat.c: binomial / trinomial denominators */
E *ix_special(XP, E *f, E *x);           /* intsf.c: Ei, Si, Ci, erf, erfi */
E *ix_invsub(XP, E *f, E *x);            /* intinv.c: u = asin(c x + d), ... */
E *ix_fresh(XP, E *f, int *i);           /* a symbol _xt<nn> not in f, from *i on */

/* sfn.c: more special functions (lambertw, fresnels, fresnelc, Shi, Chi, expint, li) */
enum { SF_LAMBERTW, SF_FRESNELS, SF_FRESNELC, SF_SHI, SF_CHI, SF_EXPINT, SF_LI, SF_N };
int sf_which(XP, E *e);         /* SF_* of a call with the right arity, else -1 */
E *sf_val(XP, E *e);            /* the value of f(numbers), NULL if none */
E *sf_diff(XP, E *e, E *x);     /* d/dx f(args), NULL if not known */
E *sf_simp(XP, E *e);           /* special values, NULL: kept */
int sf_real(XP, E *e);          /* f(args) is real for real symbols */
/* ellip.c: elliptic_f(phi, m), elliptic_e(phi, m), elliptic_pi(n, phi, m) */
int el_which(XP, E *e);         /* 0 F, 1 E, 2 Pi, else -1 */
E *el_val(XP, E *e);
E *el_diff(XP, E *e, E *x);
E *el_simp(XP, E *e);
/* hyp.c: uppergamma(a, z), hyp2f1(a, b, c, z) */
int hy_which(XP, E *e);         /* 0 uppergamma, 1 hyp2f1, else -1 */
E *hy_val(XP, E *e);
E *hy_diff(XP, E *e, E *x);
E *hy_simp(XP, E *e);
E *ix_hyp(XP, E *f, E *x);      /* inthyp.c: x^q e^(b x), x^m (a + b x^n)^p, two linear forms */
E *ix_ibp(XP, E *f, E *x);      /* intsfn.c: the by-parts recursion alone */
E *ix_polylog(XP, E *f, E *x);  /* intpl.c: ln(t)^j R(t), P(x) R(e^(c x)), R(x) ln(a+b x)^k */
E *ix_ellip(XP, E *f, E *x);    /* intell.c: R(cos u, sin u) sqrt(cos u + r) and the like */
E *ix_algell(XP, E *f, E *x);
int ix_lsolve(XP, uint32_t ne, E **eqs, uint32_t nv, E **vs, E **out); /* intalg.c: linear eqs = 0, free unknowns 0 */
E *ix_trigalg(XP, E *f, E *x);
E *ix_trighyp(XP, E *f, E *x);
E *ix_plog2(XP, E *f, E *x);    /* intalg.c: polylog of symbolic order, ln(u) ln(v) (verified) */  /* intalg.c: sin^p cos^q, p or q not an integer: hyp2f1 (verified) */  /* intalg.c: trigonometric ones through C = cos u (verified) */   /* intalg.c: R(x) sqrt(cubic or quartic) in F, E, Pi (verified) */
E *ix_sfn(XP, E *f, E *x);      /* intsfn.c: by parts, lambertw substitution (verified) */
E *ix_tidy(XP, E *f, E *r, E *x); /* tidy.c: a smaller form of the antiderivative r of f */
E *ix_tidy_rad(XP, E *f, E *t, E *x, int *real); /* tidyrad.c: logarithms, radicals, fractions */
int ix_kred(XP, E *g, E *S, E *Q, E **A, E **B); /* tidyrad.c: g = A + B S where S^2 = Q */
E *ix_tidy_trig(XP, E *t, E *x, E *f, int *pref); /* tidytrig.c: sin / cos / tan powers (pref NULL: not an antiderivative) */
E *ix_tidy_lncomb(XP, E *t, E *x, E *f); /* tidytrig.c: logarithms combined */
E *ix_tidy_fin(XP, E *e, E *x); /* tidyrad.c: logarithms merged, (a D + b)/D -> a + b/D */

#ifdef IX_DEBUG /* host only: cc -DIX_DEBUG */
#include <stdio.h>
void print_expr(sbuf *b, E *e);
static inline void ix_dbg(const char *w, E *e) {
    char buf[2000];
    sbuf b = {buf, sizeof buf, 0, 0};
    buf[0] = 0;
    if (e) print_expr(&b, e);
    fprintf(stderr, "[ix] %s: %s\n", w, e ? buf : "NULL");
}
#define IXD(w, e) ix_dbg(w, e)
#else
#define IXD(w, e) ((void)0)
#endif
#endif
