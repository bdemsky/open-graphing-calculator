/*
 * unit_round6.inc -- CAS round 6 (cas/ext/int/intalg.c, cas/ext/alg/hyper.c, cas/ext/alg/odex.c):
 * elliptic integrals in Legendre form, hyp2f1 for non-integer trig powers, Gosper and Zeilberger
 * summation, Riccati / Bessel / power series ODEs, normalised general solutions and wronskian;
 * included by unit_tests.c.
 */

static void test_round6(void) {
    cas_reset(ctx);
    expect("integrate(1/sqrt(1-x^4),x)",
           "elliptic_f(asin(x),-1)");
    expect("integrate(x^2/sqrt(1-x^4),x)",
           "elliptic_e(asin(x),-1)-elliptic_f(asin(x),-1)");
    expect("integrate(sqrt(1-x^4),x)",
           "x*sqrt(-x^4+1)/3+2*elliptic_f(asin(x),-1)/3");
    expect("integrate(1/sqrt(1+x^4),x)",
           "elliptic_f(2*atan(x),1/2)/2");
    expect("integrate(x^2/sqrt(1+x^4),x)",
           "x*sqrt(x^4+1)/(x^2+1)-elliptic_e(2*atan(x),1/2)+elliptic_f(2*atan(x),1/2)/2");
    expect("integrate(1/sqrt(x^4-1),x)",
           "sqrt(2)*elliptic_f(acos(1/x),1/2)/2");
    expect("integrate(1/sqrt(2+5*x^2-3*x^4),x)",
           "elliptic_f(asin(sqrt(2)*x/2),-6)");
    expect("integrate(1/((1+x^2)*sqrt(1-x^4)),x)",
           "x*sqrt(-x^4+1)/(2*(x^2+1))+elliptic_e(asin(x),-1)/2");
    expect("integrate(sqrt(1-x^2)*sqrt(1-2*x^2),x)",
           "x*sqrt(-2*x^2+1)*sqrt(-x^2+1)/3+sqrt(2)*(elliptic_e(asin(sqrt(2)*x),1/2)-elliptic_f(asin(sqrt(2)*x),1/2)/3)/2");
    expect("integrate(1/((1-3*x^2)*sqrt(1-x^2)*sqrt(1-2*x^2)),x)",
           "sqrt(2)*elliptic_pi(3/2,asin(sqrt(2)*x),1/2)/2");
    expect("integrate(1/sqrt(1+x^3),x)",
           "3^(3/4)*elliptic_f(acos((-x+sqrt(3)-1)/(x+sqrt(3)+1)),sqrt(3)/4+1/2)/3");
    expect("integrate(sqrt(1+x^3),x)",
           "2*x*sqrt(x^3+1)/5+3^(3/4)*elliptic_f(acos((-x+sqrt(3)-1)/(x+sqrt(3)+1)),sqrt(3)/4+1/2)/5");
    expect("integrate(1/sqrt(1-x^3),x)",
           "-3^(3/4)*elliptic_f(acos((x+sqrt(3)-1)/(-x+sqrt(3)+1)),sqrt(3)/4+1/2)/3");
    expect("integrate(sqrt(x)/sqrt(1+x^2),x)",
           "2*sqrt(x)*sqrt(x^2+1)/(x+1)-2*elliptic_e(acos((-x+1)/(x+1)),1/2)+elliptic_f(acos((-x+1)/(x+1)),1/2)");
    expect("integrate(1/sqrt(a+b*x^4),x)",
           "elliptic_f(2*atan((b/a)^(1/4)*x),1/2)/(2*sqrt(a*sqrt(b/a)))");
    expect("integrate(1/sqrt(1+cos(x)^2),x)",
           "sqrt(cos(x)^2/2+1/2)*elliptic_f(x,1/2)/sqrt(cos(x)^2+1)");
    expect("integrate(sqrt(2-sin(x)^2),x)",
           "sqrt(cos(x)^2+1)*elliptic_e(x,1/2)/sqrt(cos(x)^2/2+1/2)");
    expect("integrate(sin(x)^(1/3),x)",
           "3*cos(x)*hyp2f1(1/2,2/3,5/3,sin(x)^2)*sin(x)^(4/3)/(4*sqrt(cos(x)^2))");
    expect("integrate(sinh(x)^(1/3),x)",
           "3*hyp2f1(1/2,2/3,5/3,-sinh(x)^2)*sinh(x)^(4/3)/4");
    expect("sum(k*k!,k,0,n)",
           "(n+1)!-1");
    expect("sum(k!/(k+2)!,k,0,n)",
           "(n+1)/(n+2)");
    expect("sum(binomial(2*k,k)/4^k,k,0,n)",
           "(n+1)*binomial(2*n+2,n+1)/(2*4^n)");
    expect("sum(binomial(n,k)^2,k,0,n)",
           "binomial(2*n,n)");
    expect("sum(binomial(n,k)*binomial(m,n-k),k,0,n)",
           "binomial(m+n,m)");
    expect("sum(binomial(n,k)/binomial(2*n,k),k,0,n)",
           "(2*n+1)/(n+1)");
    expect("sum((-1)^k*binomial(n,k)^2,k,0,n)",
           "(-1)^(n/2)*((-1)^n/2+1/2)*binomial(n,n/2)");
    expect("dsolve(y'+2*y=exp(x),y,x)",
           "c1*exp(-2*x)+exp(x)/3");
    expect("dsolve(y'=y^2-x^2+1,y,x)",
           "x+exp(x^2)/(c1-erfi(x)*sqrt(pi)/2)");
    expect("dsolve(y'=-y^2+x^2+1,y,x)",
           "x+exp(-x^2)/(c1+erf(x)*sqrt(pi)/2)");
    expect("dsolve(y'=y^2-2/x^2,y,x)",
           "-2/x+1/((c1+1/(3*x^3))*x^4)");
    expect("dsolve(x^2*y''+x*y'+(4*x^2-1)*y=0,y,x)",
           "besselj(1,2*x)*c1+bessely(1,2*x)*c2");
    expect("dsolve(x^2*y''+x*y'-(x^2+1)*y=0,y,x)",
           "besseli(1,x)*c1+besselk(1,x)*c2");
    expect("dsolve(y''+x*y=0,y,x,0,8)",
           "c1*(x^6/180-x^3/6+1)+c2*(x^7/504-x^4/12+x)");
    expect("dsolve([y''+x*y=0,y(0)=1,y'(0)=0],y,x,0,8)",
           "x^6/180-x^3/6+1");
    expect("dsolve(y''-2*x*y'+4*y=0,y,x,0,6)",
           "c1*(-2*x^2+1)+c2*(-x^5/30-x^3/3+x)");
    expect("dsolve(x^2*y''+x*y'+x^2*y=0,y,x,0,6)",
           "c1*(x^4/64-x^2/4+1)+c2*(-3*x^4/128+x^2/4+ln(x)*(x^4/64-x^2/4+1))");
    expect("dsolve(4*x*y''+2*y'+y=0,y,x,0,4)",
           "c1*sqrt(x)*(-x^3/5040+x^2/120-x/6+1)+c2*(-x^3/720+x^2/24-x/2+1)");
    expect("wronskian([sin(x),cos(x)],x)",
           "-1");
    expect("wronskian([exp(x),x*exp(x),x^2*exp(x)],x)",
           "2*exp(3*x)");
    expect("wronskian([1,x,x^2],x)",
           "2");
    expect("integrate(polylog(n,a*x)/x,x)",
           "polylog(n+1,a*x)");
    expect("integrate(x*polylog(2,exp(x)),x)",
           "polylog(3,exp(x))*x-polylog(4,exp(x))");
    expect("integrate(ln(x)*ln(1-x),x)",
           "2*x-ln(x)*x+ln(-x+1)-ln(x)*ln(-x+1)-polylog(2,x)+ln(-x+1)*(ln(x)*x-x)");
    expect("diff(polylog(n,x),x)",
           "polylog(n-1,x)/x");
    expect("integrate(1/(sqrt(1-x)*sqrt(2-x)*sqrt(3-x)*sqrt(4-x)),x)",
           "elliptic_f(asin(sqrt((2*x/3-8/3)/(x-3))),3/4)*sqrt((2*x/3-8/3)/(x-3))*sqrt((x-2)/(2*x-6))*sqrt((x-1)/(3*x-9))*(3*x^2-18*x+27)/(sqrt(-x+1)*sqrt(-x+2)*sqrt(-x+3)*sqrt(-x+4))");
    /* general solutions as sum c_i y_i + y_p */
    expect("dsolve(y'-y=exp(2*x)*x,y,x)", "c1*exp(x)+(x-1)*exp(2*x)");
    expect("dsolve(x*y'+y=x^3,y,x)", "x^3/4+c1/x");
    expect("dsolve(y'+y*tan(x)=sec(x),y,x)", "c1*cos(x)+sin(x)");
    expect("wronskianmat([cos(t),sin(t)],t)", "[[cos(t),sin(t)],[-sin(t),cos(t)]]");
    expect("sum(binomial(n,k)*binomial(m,r-k),k,0,r)", "binomial(m+n,r)");
    expect("dsolve([y'=y^2-x^2+1,y(0)=2],y,x)", "x+exp(x^2)/(1/2-erfi(x)*sqrt(pi)/2)");
    expect("dsolve([y'=y^2+1,y(0)=1],y,x)", "tan(x+pi/4)");
    /* ODE systems: unknowns inferred from the primes (t when x is one), conditions on any
     * single-letter unknown (x(0) is a call when x' occurs in the same input), and solve()
     * refuses equations with primes instead of reading x' as diff(x,x) = 1 */
    expect("dsolve([x'=y,y'=-x])", "[x=c1*cos(t)+c2*sin(t),y=c2*cos(t)-c1*sin(t)]");
    expect("dsolve([x'=y,y'=-x,x(0)=1,y(0)=0])", "[x=cos(t),y=-sin(t)]");
    expect("dsolve([p'=q,q'=-p,p(0)=1,q(0)=0],[p,q],t)", "[p=cos(t),q=-sin(t)]");
    expect("dsolve([y'=z,z'=-y,y(0)=1,z(0)=0])", "[y=cos(x),z=-sin(x)]");
    expect("p(2)", "2*p");
    CHECK(cas_eval_quiet(ctx, "solve([x'=y,y'=-x])", out, sizeof out) != CAS_OK, "solve with primes: %s", out);
    /* sqrt of a perfect square: the root's sign from assumptions decides (|p| = p or -p);
     * without an assumption it stays, like sqrt(x^2) */
    expect("sqrt(x^2+2*x+1)", "sqrt(x^2+2*x+1)");
    expect("assume(x>0)", "x>0");
    expect("sqrt(x^2+2*x+1)", "x+1");
    expect("sqrt(4*x^2+4*x+1)", "2*x+1");
    expect("sqrt(x^4+2*x^2+1)", "x^2+1");
    expect("sqrt(x^2+1)", "sqrt(x^2+1)");
    expect("assume(x<-1)", "x<-1");
    expect("sqrt(x^2+2*x+1)", "-x-1");
    expect("unassume(x)", "true");
    /* logarithms of whole numbers that cancel collapse in any sum (no simplify needed) */
    expect("ln(10)-ln(2)-ln(5)", "0");
    expect("ln(6)-ln(2)", "ln(3)");
    expect("ln(12)-ln(3)", "2*ln(2)");
    expect("ln(2)-ln(6)", "-ln(3)");
    expect("ln(2)+ln(3)", "ln(2)+ln(3)");
    expect("ln(2)-ln(3)", "ln(2)-ln(3)");
    expect("x+ln(10)-ln(5)+1", "x+ln(2)+1");
    expect("log(10)-log(2)-log(5)", "0");
    expect("log(20)-log(2)", "1");
    expect("a*ln(10)-a*ln(2)", "a*ln(5)");
    /* no closed form: unevaluated, a clean error */
    expect("integrate(sqrt(x)*sqrt(1+x)*sqrt(2+x)*sqrt(3+x)*sqrt(4+x),x)", "integrate(sqrt(x)*sqrt(x+1)*sqrt(x+2)*sqrt(x+3)*sqrt(x+4),x)");
    expect("wronskian(x,x)", "ERROR 2: wronskian: expected [f1, ..., fn], x");
    cas_reset(ctx);
}
