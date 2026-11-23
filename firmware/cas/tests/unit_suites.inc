/*
 * unit_suites.inc -- regression tests for CAS bugs found by the external test
 * suites (tests/suites: Rubi, SymPy, Maxima; see tests/suites/REPORT.md).
 * One block per root cause (R1 ...).  Included by unit_tests.c.
 */
static void test_suites(void) {
    cas_reset(ctx);
    /* R1 integ.c try_sub: for u = x^2 the inverse x = u^(1/2) holds only for
     * x > 0; the substitution is accepted only if both branches agree */
    expect("integrate(sqrt(x^2),x)", "x*sqrt(x^2)/2");
    expect("integrate((x^2)^p,x)", "x*(x^2)^p/(2*p+1)");
    expect("integrate(sqrt(ln(x)^2)/x,x)", "ln(x)*sqrt(ln(x)^2)/2");
    expect("integrate(x^3/(x^2+1),x)", "x^2/2-ln(x^2+1)/2");
    /* R2 integ.c sqrt_pos: sqrt((a-b)^2) is not a-b */
    expect("integrate(1/sqrt((b-x)*(x-a)),x)", "-asin((a-2*x+b)/sqrt((a-b)^2))");
    expect("integrate(1/sqrt(4-x^2),x)", "asin(x/2)");
    /* R3 integ.c: equal bounds */
    expect("integrate(x,x,inf,inf)", "0");
    /* R4 integ.c eval_at / series.c try_direct: Ci(0) - Ci(0) is not 0 */
    expect("limit(Ci(3*x)-Ci(x),x,0,1)", "ln(3)");
    expect("integrate((cos(3*x)-cos(x))/x,x,0,inf)", "-ln(3)");
    /* R5 integ.c: a parameter result must agree with numeric quadrature at
     * sample parameter values (discontinuous Weierstrass antiderivative) */
    expect("assume(a>0)", "a>0");
    expect("integrate(1/(1+a*cos(x)),x,0,2*pi)", "integrate(1/(a*cos(x)+1),x,0,2*pi)");
    expect("integrate(exp(-a*x),x,0,inf)", "1/a");
    cas_reset(ctx);
    /* R6 sum.c: sum of 0 over an infinite range */
    expect("sum(0,n,0,inf)", "0");
    /* R7 solve.c: poles of the equation as written, assumptions, inf */
    expect("solve(x+1/x=1/x,x)", "[]");
    expect("solve((3+x/(1+x))^(-2)=0,x)", "[]");
    expect("solve(-a*x+2*x*ln(x)=0,x)", "[exp(a/2)]");
    expect("csolve(2^x+4^x=0,x)", "[i*pi/ln(2)]");
    expect("assume(x>0)", "x>0");
    expect("solve(x^2=4,x)", "[2]");
    expect("solve([x+y=0,2-y^2=0],[x,y])", "[[x=sqrt(2),y=-sqrt(2)]]");
    cas_reset(ctx);
    /* R8 solve.c: 0^x and negative bases in real mode */
    expect("solve(0^x=100,x)", "[]");
    expect("solve((-2)^x=4,x)", "[2]");
    expect("solve((-3)^(x-3)=-4052555153018976267,x)", "[42]");
    /* R9 solve.c numeric scan: rounding noise and tiny complex values are not roots */
    expect("solve(x^x=0,x)", "ERROR 6: cannot solve");
    expect("solve(tanh(x-3)*tanh(x+3)=1,x)", "ERROR 6: cannot solve");
    /* R10 solve.c: perfect powers with complex coefficients; all three
     * Cardano roots in complex mode */
    expect("csolve((1-i-x)^4=0,x)", "[1-i]");
    expect("length(csolve(x^3+x^2/2+i+1/2=0,x))", "3");
    /* R11 series.c: limits on a branch cut are one-sided values */
    expect("limit(ln(-2+i*x),x,0)", "ERROR 6: limit could not be determined");
    expect("limit(ln(-1+i*x),x,0,1)", "i*pi");
    expect("limit(atan2(y,-1),y,0,-1)", "ERROR 6: limit could not be determined");
    expect("limit(atan2(y,-1),y,0,1)", "pi");
    /* R12 series.c: ln of a negative infinitesimal/infinity is complex infinity */
    expect("limit(ln(1-exp(x)),x,0,1)", "undef");
    expect("limit(ln(x),x,-inf)", "undef");
    expect("limit(ln(x),x,0)", "-inf");
    expect("limit(abs(ln(x)),x,0)", "inf");
    expect("limit(ln(sign(x)),x,0)", "undef");
    /* R13 series.c: an unevaluable left side does not make the two-sided limit the right one */
    expect("limit(exp(-1/x)*sin(1/x),x,0)", "ERROR 6: limit could not be determined");
    expect("limit(sqrt(x),x,0)", "0");
    /* R14 series.c: denominators that vanish without rat_normal noticing */
    expect("limit(z/(1-sqrt(1+z)*sin(a)^2-sqrt(1-z)*cos(a)^2),z,0,1)", "-2/(sin(a)^2-cos(a)^2)");
    expect("limit((1/2+x-sqrt(5)/2)/(x^2+x-1),x,sqrt(5)/2-1/2,1)", "sqrt(5)/5");
    /* R15 series.c: Ei(u) = gamma + ln|u| + ... for u < 0 */
    expect("limit(Ei(-x)-ln(x),x,0,1)", "euler_gamma");
    /* R17 solve.c: an empty result of a nonlinear system with parameters is
     * reported as a failure, not as "no solution" */
    expect("csolve([(x-1)^2+(y-1)^2-r^2=0,(x-2)^2+(y-2)^2-r^2=0],[x,y])", "ERROR 6: cannot solve this system");
    expect("solve([x+y=a,x+y=a+1],[x,y])", "[]");
    /* R16 series.c: e^(c) with c not linear in ln h is not a coefficient */
    {
        const char *in = "limit(exp(exp(2*ln(x^5+x)*ln(ln(x))))/exp(exp(10*ln(x)*ln(ln(x)))),x,inf)";
        const char *got = ev(in);
        CHECK(strcmp(got, "1") != 0, "%s -> %s (true limit: inf)", in, got);
    }
}
