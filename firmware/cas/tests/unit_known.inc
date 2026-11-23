/*
 * unit_known.inc -- second batch of closed gaps: exp(q*i*pi) values, minimize on a domain
 * from ln, elliptic special values, initial conditions in dsolve; included by unit_tests.c.
 */

static void test_known(void) {
    cas_reset(ctx);
    /* exp(q i pi) for denominators 1, 2, 3, 4, 6 (simp.c exp_ipi) */
    expect("exp(i*pi)", "-1");
    expect("e^(i*pi)", "-1");
    expect("exp(2*i*pi)", "1");
    expect("exp(i*pi/2)", "i");
    expect("exp(-i*pi/2)", "-i");
    expect("exp(i*pi/3)", "sqrt(3)*i/2+1/2");
    expect("exp(-i*pi/4)", "sqrt(2)/2-sqrt(2)*i/2");
    expect("exp(5*i*pi/6)", "i/2-sqrt(3)/2");
    expect("exp(1+i*pi)", "-e");
    expect("exp(i*pi/5)", "exp(i*pi/5)");
    expect("exp2trig(exp(i*x))", "cos(x)+i*sin(x)");
    expect("trig2exp(cos(x))", "exp(i*x)/2+exp(-i*x)/2");
    expect("exp2trig(trig2exp(sin(x)))", "sin(x)");

    /* minimize / maximize over the real domain (spfn.c dom_ends: ends from ln and roots) */
    expect("minimize(x*ln(x),x)", "-1/e");
    expect("maximize(x*ln(x),x)", "inf");
    expect("minimize(x-ln(x),x)", "1");
    expect("maximize(sqrt(x)-x,x)", "1/4");
    expect("minimize(sqrt(1-x^2),x)", "0");
    expect("maximize(sqrt(1-x^2),x)", "1");
    expect("minimize(ln(x^2-1),x)", "-inf");
    expect("minimize(x^2-2*x,x)", "-1");

    /* elliptic special values, complete integrals K(m), E(m) with m = k^2 (ellip.c) */
    expect("elliptic_e(pi/2,1)", "1");
    expect("elliptic_e(pi/6,1)", "1/2");
    expect("elliptic_f(pi/2,1)", "inf");
    expect("elliptic_f(x,0)", "x");
    expect("elliptic_e(x,1)", "elliptic_e(x,1)");
    expect("elliptic_k(0)", "pi/2");
    expect("elliptic_e(0)", "pi/2");
    expect("elliptic_k(1)", "inf");
    expect("elliptic_e(1)", "1");
    expect("elliptic_k(1/2)", "gamma(1/4)^2/(4*sqrt(pi))");
    expect("approx(elliptic_e(1/2))", "1.35064388105");
    expect("elliptic_k(0.3)", "1.71388944818");
    expect("elliptic_e(0.3)", "1.44536306441");
    expect("elliptic_k(2.0)", "1.31102877715-1.31102877715*i");
    expect("diff(elliptic_e(m),m)", "(elliptic_e(m)-elliptic_k(m))/(2*m)");

    /* fourier with a parameter of unknown sign: the result with its condition (xform.c ft_cond) */
    expect("fourier(exp(-a*abs(x)),x,w)", "when(a>0,a*sqrt(2)/(sqrt(pi)*(a^2+w^2)),undef)");
    expect("fourier(exp(a*abs(x)),x,w)", "when(a<0,-a*sqrt(2)/(sqrt(pi)*(a^2+w^2)),undef)");
    expect("fourier(exp(-a*x^2),x,w)", "when(a>0,sqrt(2)*exp(-w^2/(4*a))/(2*sqrt(a)),undef)");
    expect("about(a)", "real");
    expect("assume(a>0)", "a>0");
    expect("fourier(exp(-a*abs(x)),x,w)", "a*sqrt(2)/(sqrt(pi)*(a^2+w^2))");
    cas_reset(ctx);

    /* compiled functions (graphs): piecewise, when, relations, and/or/not, splines (OP_CMP, OP_SEL) */
    {
        static const struct {
            const char *e;
            double x, y;
        } cs[] = {
            {"piecewise(x<0,-x,x)", -2, 2},
            {"piecewise(x<0,-x,x)", 3, 3},
            {"piecewise(x<0,-x,x<1,x^2,1)", 0.5, 0.25},
            {"piecewise(x<0,-x,x<1,x^2,1)", 5, 1},
            {"when(x>0 and x<1,x,0)", 0.5, 0.5},
            {"when(x>0 and x<1,x,0)", 2, 0},
            {"when(not(x>0) or x>=3,1,2)", 4, 1},
            {"spline([0,1,2],[0,1,0],x)", 1, 1},
            {"spline([0,1,2],[0,1,0],x)", 0.5, 0.6875},
            {"spline([0,1,2],[0,1,0],x)", 1.5, 0.6875},
        };
        for (unsigned i = 0; i < sizeof cs / sizeof cs[0]; i++) {
            cas_fn *fn;
            cas_status st = cas_compile(ctx, cs[i].e, "x", &fn);
            CHECK(st == CAS_OK, "compile %s: %d", cs[i].e, st);
            if (st != CAS_OK) continue;
            double y = cas_fn_eval(fn, cs[i].x);
            CHECK(fabs(y - cs[i].y) < 1e-12, "%s at %g = %g, want %g", cs[i].e, cs[i].x, y, cs[i].y);
            cas_fn_free(ctx, fn);
        }
        cas_fn *fn;
        cas_status st = cas_compile(ctx, "when(x>0,x,undef)", "x", &fn);
        CHECK(st == CAS_OK && isnan(cas_fn_eval(fn, -1)) && cas_fn_eval(fn, 2) == 2, "when with undef");
        if (st == CAS_OK) cas_fn_free(ctx, fn);
    }
}
