/*
 * unit_regress.inc -- regression tests for fixed CAS engine bugs (numeric,
 * linalg, series/limit, diff, solve, integ, bf, eval, parser) listed in
 * firmware/docs/KNOWN_ISSUES.md (included by unit_tests.c).
 */
E *ln_collect_pos(E *e);

static void test_regress(void) {
    cas_reset(ctx);
    cas_fn *fn;
    cas_status st;
    /* numeric.c: Ei(x) for x < -1 via E1 continued fraction (no cancellation) */
    expect("Ei(-30.0)", "-3.02155201069e-15");
    expect("Ei(-2.0)", "-0.0489005107081");
    /* numeric.c: principal branch of acosh for z < -1 */
    expect("approx(acosh(-2))", "1.31695789692+3.14159265359*i");
    /* numeric.c: compiled x^(p/q) with q >= 65536 must not wrap the packed q */
    st = cas_compile(ctx, "x^(1/65537)", "x", &fn);
    CHECK(st == CAS_OK && fabs(cas_fn_eval(fn, 2.0) - pow(2.0, 1.0 / 65537)) < 1e-12, "x^(1/65537)");
    if (st == CAS_OK) cas_fn_free(ctx, fn);
    /* linalg.c: float matrices may pivot on symbolic entries; the pivot
     * threshold is relative to the matrix scale */
    expect("det([[x,0.5],[0,1]])", "x");
    expect("charpoly([[1.5,0],[0,2.5]],x)", "x^2-4.0*x+3.75");
    expect("rank([[1e-14]])", "1");
    expect("rank([[1.0,2],[2,4]])", "1");
    /* series.c: a bounded oscillating argument of atan/tanh has no limit
     * (the _bounded marker must not leak) */
    expect("limit(atan(sin(x)),x,inf)", "ERROR 6: limit could not be determined");
    /* series.c: floor/ceil/frac at a jump with an argument of unknown sign */
    expect("series(floor(a*x),x,0,3)", "ERROR 6: series expansion failed");
    expect("series(floor(-x),x,0,3)", "-1+O(x^3)");
    /* series.c: two-sided limit with a real but undetermined left side */
    expect("limit(floor(x)*sin(1/x),x,0)", "ERROR 6: limit could not be determined");
    expect("limit(sqrt(x)*ln(x),x,0)", "0");
    /* diff.c: a constant list differentiates to a list of zeros */
    expect("diff([1,2],x)", "[0,0]");
    /* solve.c: Cardano with a double root and float coefficients */
    expect("solve(x^3-3*x+2.0=0,x)", "[-2.0,1.0]");
    /* solve.c: fsolve systems check convergence */
    expect("fsolve([x^2+1=0,y=0],[x,y],[0.3,1])", "ERROR 2: fsolve: no convergence");
    expect("fsolve([x^2+y^2=4e10,x=y],[x,y],[1,1])", "[x=141421.356237,y=141421.356237]");
    /* solve.c: resultant elimination re-checks the other equations */
    expect("solve([a-x,b-y,c-z,x^2+y^2-2,x^2-y^2,x*y-z^2],[a,b,c,x,y,z])",
           "[[a=-1,b=-1,c=-1,x=-1,y=-1,z=-1],[a=-1,b=-1,c=1,x=-1,y=-1,z=1],"
           "[a=1,b=1,c=-1,x=1,y=1,z=-1],[a=1,b=1,c=1,x=1,y=1,z=1]]");
    /* integ.c: a strategy error means "not found" (unevaluated), not an error */
    expect("integrate(x^1000000*exp(x),x)", "integrate(exp(x)*x^1000000,x)");
    expect("integrate(x*exp(x)+x^1000000*exp(x),x)", "integrate(exp(x)*x^1000000,x)+exp(x)*(x-1)");
    /* integ.c: interior poles with trig periods shorter than pi (2pi/3 here) */
    expect("integrate(1/sin(3*x),x,2,2.2)", "ERROR 2: integral diverges (pole inside the interval)");
    expect("integrate(1/cos(4*x),x,1.1,1.25)", "ERROR 2: integral diverges (pole inside the interval)");
    expect("integrate(1/(2+sin(3*x)),x,0,10)", "5.69434997343");
    /* bf.c: guard precision under cancellation */
    expect("approx(ln(1+10^-50),30)", "1.0e-50");
    expect("approx(sin(10^40),30)", "-0.569633400953636327308034181574");
    expect("approx(sinh(10^-30),30)", "1.0e-30");
    expect("approx(pi,50)", "3.1415926535897932384626433832795028841971693993751");
    /* trig.c: ln_collect_pos restores its branch-safety flag when it throws */
    WB_BEGIN
    E *lx = s_add2(s_ln(mk_sym_name("x")), s_ln(mk_sym_name("y")));
    tryf t2;
    try_enter(&t2);
    size_t lim = K->stack_limit;
    K->stack_limit = 1;
    if (!setjmp(t2.jb)) {
        ln_collect_pos(lx);
        try_leave(&t2);
        CHECK(0, "ln_collect_pos did not throw");
    } else
        try_leave(&t2);
    K->stack_limit = lim;
    WB_END
    expect("lncollect(ln(x)+ln(y))", "ln(x*y)");
    /* xcmds.c: fourier's temporary integer assumption on n is undone on error */
    expect("fourier_an(1/(x-1/2),x,1,n)", "ERROR 2: integral diverges (pole inside the interval)");
    expect("sin(2*pi*n)", "sin(2*n*pi)");
    /* eval.c/cas.c: cas_compile resets the user-function call depth left
     * high by an earlier "recursion too deep" error */
    ev("rf(x):=rf(x)+1");
    ev("g(x):=x^2");
    const char *got = ev("rf(1)");
    CHECK(strstr(got, "ERROR") != NULL, "rf(1) should fail: %s", got);
    st = cas_compile(ctx, "g(x)", "x", &fn);
    CHECK(st == CAS_OK && fabs(cas_fn_eval(fn, 3.0) - 9) < 1e-12, "compile after deep recursion (%d)", st);
    if (st == CAS_OK) cas_fn_free(ctx, fn);
    /* eval.c: cas_compile_vars hides all (up to 24) variables */
    {
        static const char *vs[20] = {"v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9",
                                     "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19"};
        double vals[20] = {0};
        ev("v19:=5");
        st = cas_compile_vars(ctx, "v19*2", vs, 20, &fn);
        vals[19] = 1;
        CHECK(st == CAS_OK && fabs(cas_fn_eval_vars(fn, vals) - 2) < 1e-12, "20th variable hidden");
        if (st == CAS_OK) cas_fn_free(ctx, fn);
        expect("v19", "5");
    }
    /* eval.c: an assignment to a hidden name during the hidden evaluation
     * is kept (not overwritten by the restore) */
    st = cas_compile(ctx, "x:=5", "x", &fn);
    if (st == CAS_OK) cas_fn_free(ctx, fn);
    expect("x", "5");
    /* cas.c: cas_latex clears a stale interrupt request */
    expect("6*7", "42");
    cas_interrupt(ctx);
    char lx[64];
    CHECK(cas_latex(ctx, lx, sizeof lx) == CAS_OK && !strcmp(lx, "42"), "latex after stale interrupt: %s", lx);
    cas_reset(ctx);
    /* parse.c: d/dNAME is a derivative only if d and dNAME are unassigned
     * and an operand follows */
    expect("d/dt(t^2)", "2*t");
    expect("d/dx x^3", "3*x^2");
    expect("d/dt", "d/dt");
    ev("d:=6");
    ev("dx:=2");
    expect("d/dx", "3");
    /* parse.c: "->" is not an operator (it was lexed but never accepted) */
    expect("x->2", "ERROR 1: syntax error");
    cas_reset(ctx);
    /* integ.c: a narrow peak at an endpoint is not missed by the quadrature
     * (int_0^1 x^1000000 e^x = 2.71827639191e-6, was 0.0) */
    expect("integrate(x^1000000*exp(x),x,0,1)", "2.71827639191e-6");
    expect("nint(sqrt(x),x,0,1)", "0.666666666667");
    /* integ.c: x^(-1/3) is finite (principal branch, complex) left of 0, so
     * the integral converges, consistent with (-1)^(1/3) = 1/2+sqrt(3)/2 i */
    expect("integrate(x^(-1/3),x,-1,8)", "6-3*(-1)^(2/3)/2");
    expect("approx(integrate(x^(-1/3),x,-1,8))", "6.75-1.29903810568*i");
    expect("integrate(1/x^2,x,-1,1)", "ERROR 2: integral diverges (pole inside the interval)");
    /* session.c: cas_restore reports the first failing record's real status */
    {
        const char *rec = "v a [1,2]*[1,2,3]\nv b 5\n";
        cas_status rs = cas_restore(ctx, rec, strlen(rec));
        CHECK(rs == CAS_ERR_DOMAIN, "restore status %d (want domain error)", rs);
        expect("b", "5");
        rec = "v c 1+\n";
        CHECK(cas_restore(ctx, rec, strlen(rec)) == CAS_ERR_SYNTAX, "restore syntax status");
    }
    cas_reset(ctx);
    /* solve.c: roots multiplicities with non-rational coefficients, numeric
     * clusters, and the same root from several factors over Q */
    expect("roots((x-i)^3,x)", "[[i,3]]");
    expect("roots(expand((x-i)^2*(x+1)),x)", "[[-1,1],[i,2]]");
    expect("roots(expand((x-1-i)^2*(x-2)^3),x)", "[[2,3],[1+i,2]]");
    expect("roots(expand((x-i)^3*(x+i)^2),x)", "[[-i,2],[i,3]]");
    expect("roots(expand((x-sqrt(2))^2*(x+1)),x)", "[[-1,1],[sqrt(2),2]]");
    expect("roots(x^4-2*x^2+1.0,x)", "[[-1.0,2],[1.0,2]]");
    expect("roots(expand((x-1.5)^2*(x^3+x+1)),x)",
           "[[-0.682327803828,1],[0.341163901914-1.1615414*i,1],[0.341163901914+1.1615414*i,1],[1.5,2]]");
    expect("roots((x-1)^2*(x+2),x)", "[[-2,1],[1,2]]");
    cas_reset(ctx);
    /* eval.c: degree mode does not rescale a trig argument with an angle unit */
    {
        cas_settings s0, s1;
        cas_get_settings(ctx, &s0);
        s1 = s0;
        s1.angle = CAS_ANGLE_DEG;
        cas_set_settings(ctx, &s1);
        expect("sin(30_deg)", "1/2");
        expect("cos(pi/3_rad)", "1/2");
        expect("sin(30)", "1/2");
        expect("asin(1/2)", "30");
        cas_set_settings(ctx, &s0);
    }
    expect("sin(30_deg)", "1/2");
    /* parse.c: a number with its unit tail is one primary */
    expect("1/2_s", "1/(2*_s)");
    expect("(1/2_s)*4_s", "2");
    expect("6_m/2_s/3_s", "_m/s^2");
    expect("1/2_m/s", "0.5_s/m");
    expect("9.8_m/s^2*3_s", "29.4_m/s");
    expect("2_s^2", "2_s^2");
    expect("5_m/(2_ft)", "8.20209973753");
    cas_reset(ctx);
}
