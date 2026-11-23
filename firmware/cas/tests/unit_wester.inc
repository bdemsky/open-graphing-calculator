/*
 * unit_wester.inc -- regression tests for the WRONG answers of the Wester
 * benchmark (tests/wester, problem ids in the comments): each one is now
 * right, unevaluated, or an explicit error (included by unit_tests.c).
 */
#define ROSSER "[[611,196,-192,407,-8,-52,-49,29],[196,899,113,-192,-71,-43,-8,-44]," \
               "[-192,113,899,196,61,49,8,52],[407,-192,196,611,8,44,59,-23],"       \
               "[-8,-71,61,8,411,-599,208,208],[-52,-43,49,44,-599,411,208,208],"   \
               "[-49,-8,8,59,208,208,99,-911],[29,-44,52,-23,208,208,-911,99]]"
static void test_wester(void) {
    cas_reset(ctx);
    /* W1, W2 (integ.c): a pole between symbolic bounds diverges */
    expect("integrate(1/(x-a),x,a-1,a+1)", "ERROR 2: integral diverges (pole inside the interval)");
    expect("integrate(1/(x-a)^2,x,a-1,a+1)", "ERROR 2: integral diverges (pole inside the interval)");
    expect("integrate(sin(x-a)/(x-a),x,a-1,a+1)", "2*Si(1)");
    /* K13 (simp.c): (e^z)^p on the principal branch */
    expect("sqrt(exp(6*i))", "-exp(3*i)");
    expect("sqrt(exp(2*i))", "exp(i)");
    expect("sqrt(exp(i*x))", "sqrt(exp(i*x))");
    /* M9, M10, M22, M24, M33 (solve.c, numeric.c, eval.c) */
    expect("csolve(exp(x)=x,x)", "ERROR 6: cannot solve");
    expect("solve(f(x)=0,x)", "ERROR 6: cannot solve");
    expect("solve(x^x=x,x)", "[-1,1]");
    expect("csolve(x=1/sqrt(1+x^2),x)", "[-sqrt(-2*sqrt(5)-2)/2,sqrt(2*sqrt(5)-2)/2]");
    expect("solve(a*b^x=c*d^x,x)", "[(ln(a)-ln(c))/(ln(d)-ln(b))]");
    expect("csolve((1+i)*z+(2-i)*conj(z)=-3*i,z)", "[2+3*i]");
    expect("dsolve(y'=y*(1-y),y,x)", "-exp(x)/(exp(c1)-exp(x))");
    /* N15 (solve.c): a periodic sign chart is not complete */
    expect("solve(sin(x)<1,x)", "ERROR 6: cannot solve");
    expect("solve(sin(x)<2,x)", "[true]");
    /* P17 (linalg.c): trig zero recognition in rank */
    expect("rank([[sin(2*t),cos(2*t)],[2*(1-cos(t)^2)*cos(t),(1-2*sin(t)^2)*sin(t)]])", "1");
    /* P24, P25 (solve.c, cmds.c): no dropped or undef eigenvalues */
    expect("eigenvals(" ROSSER ")",
           "[-10*sqrt(10405),0,510-100*sqrt(26),1000,100*sqrt(26)+510,1020,10*sqrt(10405)]");
    expect("eigenvals(approx(" ROSSER "))",
           "[-1020.04901843,0.0,0.0980486407215,1000.0,1019.90195136,1020.0,1020.04901843]"); /* Jacobi: 510-100 sqrt(26) = 0.09804864072152 */
    /* X24 (integ.c): jumps of sign() in the antiderivative */
    expect("assume(p>0)", "p>0");
    expect("assume(n,integer)", "n");
    expect("fourier_an(abs(x),x,2*p,n,-p)", "2*((-1)^n-1)*p/(n^2*pi^2)");
    expect("integrate(abs(x)*cos(x),x,-1,1)", "2*(cos(1)+sin(1)-1)");
    /* continuous antiderivative with heaviside (integ.c int_heav): checked at q = 3/10, 2, -3, 1 */
    expect("integrate(abs(x-q)*cos(x),x,-1,1)", "2*q*sin(1)-2*heaviside(-q+1)*q*(sin(1)-sin(q))+2*heaviside(-q-1)*q*(-sin(1)-sin(q))-2*heaviside(-q-1)*(-q*sin(q)+cos(1)-cos(q)+sin(1))+2*heaviside(-q+1)*(-q*sin(q)+cos(1)-cos(q)+sin(1))");
    /* U4 (cmds.c): symbolic derivative order */
    expect("diff(x^n,x,n)", "n!");
    expect("diff(x^3,x,n)", "diff(x^3,x,n)");
    /* H12 (cmds.c): gcd with symbolic exponents */
    expect("gcd(2*x^(n+4)-x^(n+2),4*x^(n+1)+3*x^n)", "x^n");
    expect("gcd(x^n+1,x^n-1)", "gcd(x^n+1,x^n-1)");
    /* J16, U3b (simp.c): subst keeps the variable of an unevaluated diff */
    expect("subst(diff(zeta(x),x),x,0)", "subst(diff(zeta(x),x),x,0)");
    expect("subst(diff(g(x),x),x,t)", "diff(g(t),t)");
    /* D2, D2b (bf.c, numeric.c, simp.c): no underflow to 0.0 */
    expect("approx(exp(-1000000),15)", "3.29683147808856e-434295");
    expect("exp(-1000000.0)", "3.29683147809e-434295");
    expect("approx(exp(1000000),20)", "3.0332153968020875451e434294");
    cas_reset(ctx);
}
#undef ROSSER
