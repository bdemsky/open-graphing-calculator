/*
 * applied.c -- the applied-mathematics part of the casx module (applied.h): its command
 * table and shared numeric helpers.
 *
 *   svd(A), singvals(A), pinv(A), norm(A, 2), rank(A, tol), cond(A, p)      nla.c
 *   fourier(f, x, w), ifourier(F, w, x), ztrans(f, n, z), iztrans(F, z, n)   xform.c
 *   fft(list), ifft(list)                                                     fft.c
 *   linprog(...), minimize / maximize(f, constraints, vars)                   lp.c
 *   truthtable, boolsimplify / logicsimplify, cnf, dnf, satisfiable,
 *   implies, iff                                                               logic.c
 *   spline(xs, ys [, x])                                                       spline.c
 *
 * norm, rank and cond are core commands: the core forwards norm(A, 2), rank(A, tol) and
 * cond(A, p) here (casx_dispatch); eigenvals / eigenvects / rank of matrices with floats come
 * through the slot CASX_FN_NUMLA.  fourier replaces the one of cas/ext/int/fourier.c (this table
 * is registered first); minimize / maximize with three arguments sit beside the two-argument
 * ones of cas/ext/int/spfn.c (casx.c picks the entry by argument count).
 */
#include "applied.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

void ap_sincos(double t, double *s, double *c) {
    const double PIO2_1 = 1.5707963267341256e+00, PIO2_1T = 6.0771005065061922e-11;
    double q = t * 0.63661977236758134308; /* 2/pi */
    long k = (long)(q + (q >= 0 ? 0.5 : -0.5));
    double r = (t - (double)k * PIO2_1) - (double)k * PIO2_1T, r2 = r * r;
    double sn = 0, cs = 0, ts = r, tc = 1;
    for (int i = 1; i <= 19; i += 2) { /* r^i/i!, r^(i-1)/(i-1)! with alternating signs */
        sn += ts, cs += tc;
        ts *= -r2 / (double)((i + 1) * (i + 2));
        tc *= -r2 / (double)(i * (i + 1));
    }
    switch ((unsigned long)k & 3) {
    case 0: *s = sn, *c = cs; break;
    case 1: *s = cs, *c = -sn; break;
    case 2: *s = -sn, *c = -cs; break;
    default: *s = -cs, *c = sn; break;
    }
}

E *ap_cnum(XP, double re, double im) {
    if (im == 0) return mk_flt(re);
    E *b = s_mul2(mk_flt(im), K->i_);
    return re == 0 ? b : s_add2(mk_flt(re), b);
}

double *ap_dmat(XP, E *A, uint32_t *m, uint32_t *n) {
    if (!is_matrix(A, m, n)) cas_throw(CAS_ERR_DOMAIN, "expected a matrix");
    if ((uint64_t)*m * *n > 40000) cas_throw(CAS_ERR_DOMAIN, "matrix too large");
    double *a = salloc(sizeof(double) * *m * *n);
    for (uint32_t i = 0; i < *m; i++)
        for (uint32_t j = 0; j < *n; j++) {
            double re, im;
            E *e = EK(EK(A)[i])[j];
            if (!is_const_expr(e) || !X->eval_complex(e, &re, &im)) cas_throw(CAS_ERR_DOMAIN, "expected a numeric matrix");
            if (im != 0) cas_throw(CAS_ERR_DOMAIN, "expected a real matrix");
            a[i * *n + j] = re;
        }
    return a;
}

E *ap_mat(XP, const double *a, uint32_t m, uint32_t n, double tiny) {
    E **rows = salloc(sizeof(E *) * m);
    for (uint32_t i = 0; i < m; i++) {
        E **r = salloc(sizeof(E *) * n);
        for (uint32_t j = 0; j < n; j++) r[j] = mk_flt(AFABS(a[i * n + j]) <= tiny ? 0.0 : a[i * n + j]);
        rows[i] = s_list(n, r);
    }
    return s_list(m, rows);
}

E *ap_sym(XP, const char *nm) { return mk_sym_name(nm); }

enum { A_SVD, A_SINGVALS, A_PINV, A_NORM, A_RANK, A_COND, A_FOURIER, A_IFOURIER, A_ZTRANS, A_IZTRANS, A_FFT,
       A_IFFT, A_LINPROG, A_MIN, A_MAX, A_TT, A_BSIMP, A_LSIMP, A_CNF, A_DNF, A_SAT, A_IMPL, A_IFF, A_SPLINE };
static const casx_cmd_t atab[] = {
    {"svd", 1, 1},          {"singvals", 1, 1},      {"pinv", 1, 1},    {"norm", 2, 2},    {"rank", 2, 2},
    {"cond", 2, 2},         {"fourier", 3, 3},       {"ifourier", 3, 3}, {"ztrans", 3, 3},  {"iztrans", 3, 3},
    {"fft", 1, 1},          {"ifft", 1, 1},          {"linprog", 3, 5}, {"minimize", 3, 3}, {"maximize", 3, 3},
    {"truthtable", 1, 2},   {"boolsimplify", 1, 1},  {"logicsimplify", 1, 1}, {"cnf", 1, 1}, {"dnf", 1, 1},
    {"satisfiable", 1, 1},  {"implies", 2, 2},       {"iff", 2, 2},     {"spline", 2, 3},
};

static E *acmd(XP, int op, uint32_t n, E **a) {
    switch (op) {
    case A_SVD: return ap_svd(X, a[0]);
    case A_SINGVALS: return ap_singvals(X, a[0]);
    case A_PINV: return ap_pinv(X, a[0]);
    case A_NORM: return ap_norm2(X, a[0]);
    case A_RANK: return ap_rank(X, a[0], a[1]);
    case A_COND: return ap_cond(X, a[0], a[1]);
    case A_FOURIER: case A_IFOURIER: return ap_fourier(X, a[0], a[1], a[2], op == A_IFOURIER);
    case A_ZTRANS: return ap_ztrans(X, a[0], a[1], a[2]);
    case A_IZTRANS: return ap_iztrans(X, a[0], a[1], a[2]);
    case A_FFT: case A_IFFT: return ap_fft(X, a[0], op == A_IFFT);
    case A_LINPROG: return ap_linprog(X, n, a);
    case A_MIN: case A_MAX: return ap_extremum(X, a[0], a[1], a[2], op == A_MAX);
    case A_TT: return ap_logic(X, AP_TT, n, a);
    case A_BSIMP: case A_LSIMP: return ap_logic(X, AP_BSIMP, n, a);
    case A_CNF: return ap_logic(X, AP_CNF, n, a);
    case A_DNF: return ap_logic(X, AP_DNF, n, a);
    case A_SAT: return ap_logic(X, AP_SAT, n, a);
    case A_IMPL: return ap_logic(X, AP_IMPL, n, a);
    case A_IFF: return ap_logic(X, AP_IFF, n, a);
    case A_SPLINE: return ap_spline(X, a[0], a[1], n > 2 ? a[2] : mk_sym(S_X));
    }
    return NULL;
}

int casx_applied_init(XP) {
    return X->reg("casx", atab, (int)(sizeof atab / sizeof atab[0]), acmd) | ap_nla_register(X);
}
