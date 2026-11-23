/*
 * spline.c -- spline(xs, ys [, x]) of the casx module (applied.h): the natural cubic spline
 * through the points (xs_i, ys_i), xs strictly increasing, as
 *   piecewise(x < xs_2, S_1, x < xs_3, S_2, ..., S_(n-1))
 * with S_i the expanded cubic on [xs_i, xs_(i+1)] (the end pieces extend beyond the data).  The
 * second derivatives M_i solve the tridiagonal system (M_1 = M_n = 0)
 *   h_(i-1) M_(i-1) + 2 (h_(i-1) + h_i) M_i + h_i M_(i+1) = 6 (d_i - d_(i-1)),  d_i = (y_(i+1) - y_i)/h_i
 * by elimination in exact arithmetic (rational data give rational coefficients; floats stay
 * floats), and S_i(x) = M_i (x_(i+1) - x)^3/(6 h_i) + M_(i+1) (x - x_i)^3/(6 h_i)
 *   + (y_i/h_i - M_i h_i/6) (x_(i+1) - x) + (y_(i+1)/h_i - M_(i+1) h_i/6) (x - x_i).
 * Two points give the line.  An x that is not a variable (a number, an expression) is
 * substituted into the piecewise.  At most 200 points.
 */
#include "applied.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

E *ap_spline(XP, E *xs, E *ys, E *x) {
    if (xs->t != E_LIST || ys->t != E_LIST || xs->n != ys->n || xs->n < 2)
        cas_throw(CAS_ERR_DOMAIN, "spline: two lists of the same length >= 2");
    uint32_t n = xs->n;
    if (n > 200) cas_throw(CAS_ERR_DOMAIN, "spline: at most 200 points");
    E **px = EK(xs), **py = EK(ys);
    double prev = 0;
    for (uint32_t i = 0; i < n; i++) {
        double v;
        if (!is_const_expr(px[i]) || !to_double(px[i], &v)) cas_throw(CAS_ERR_DOMAIN, "spline: the xs must be numbers");
        if (i && !(v > prev)) cas_throw(CAS_ERR_DOMAIN, "spline: the xs must increase");
        prev = v;
    }
    E *t = x->t == E_SYM ? x : mk_sym_name("_sx");
    E **h = salloc(sizeof(E *) * n), **M = salloc(sizeof(E *) * n), **c = salloc(sizeof(E *) * n),
      **r = salloc(sizeof(E *) * n);
    for (uint32_t i = 0; i + 1 < n; i++) h[i] = s_sub(px[i + 1], px[i]);
    for (uint32_t i = 0; i < n; i++) M[i] = K->c0;
    /* Thomas algorithm on rows 1..n-2: diagonal b_i, off-diagonals h_(i-1), h_i */
    for (uint32_t i = 1; i + 1 < n; i++) {
        poll_intr();
        E *b = s_mul2(K->c2, s_add2(h[i - 1], h[i]));
        E *d = s_mul2(mk_int(6), s_sub(s_div(s_sub(py[i + 1], py[i]), h[i]), s_div(s_sub(py[i], py[i - 1]), h[i - 1])));
        if (i > 1) { /* eliminate h_(i-1) M_(i-1) with row i-1 (normalised: M_(i-1) + c_(i-1) M_i = r_(i-1)) */
            b = s_sub(b, s_mul2(h[i - 1], c[i - 1]));
            d = s_sub(d, s_mul2(h[i - 1], r[i - 1]));
        }
        c[i] = s_div(h[i], b), r[i] = s_div(d, b);
    }
    for (uint32_t i = n - 1; i-- > 1;) M[i] = i + 2 < n ? s_sub(r[i], s_mul2(c[i], M[i + 1])) : r[i];
    E **pw = salloc(sizeof(E *) * (2 * n));
    uint32_t np = 0;
    E *six = mk_int(6);
    for (uint32_t i = 0; i + 1 < n; i++) {
        E *a = s_sub(px[i + 1], t), *b = s_sub(t, px[i]), *hi = h[i];
        E *S = s_add(4, (E *[]){s_div(s_mul2(M[i], s_powi(a, 3)), s_mul2(six, hi)),
                                s_div(s_mul2(M[i + 1], s_powi(b, 3)), s_mul2(six, hi)),
                                s_mul2(s_sub(s_div(py[i], hi), s_div(s_mul2(M[i], hi), six)), a),
                                s_mul2(s_sub(s_div(py[i + 1], hi), s_div(s_mul2(M[i + 1], hi), six)), b)});
        S = expand(S);
        if (i + 2 < n) pw[np++] = s_fn2(F_LT, t, px[i + 1]);
        pw[np++] = S;
    }
    E *res = np == 1 ? pw[0] : mk_fn(F_PIECEWISE, np, pw);
    if (t == x) return res;
    if (x->t != E_LIST) return subst(res, t, x);
    E **v = salloc(sizeof(E *) * (x->n + 1)); /* a list of points: the value at each */
    for (uint32_t i = 0; i < x->n; i++) v[i] = subst(res, t, EK(x)[i]);
    return s_list(x->n, v);
}
