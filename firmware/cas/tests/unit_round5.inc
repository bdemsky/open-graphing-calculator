/*
 * unit_round5.inc -- CAS round 5 (cas/ext/int): the special functions of sfn.c (lambertw,
 * fresnels, fresnelc, Shi, Chi, expint, li), Bessel Y and K of real order, the integrals of
 * intsfn.c (by parts, lambertw substitution) and the Fresnel forms of intsf.c; included by
 * unit_tests.c.
 */

static void test_round5_fns(void) {
    cas_reset(ctx);
    /* values (reference values from mpmath) */
    expect_near("lambertw(1.0)", 0.567143290409784, 1e-11);
    expect_near("re(lambertw(-1.0))", -0.318131505204764, 1e-11);
    expect_near("im(lambertw(-1.0))", 1.337235701430689, 1e-11);
    expect_near("re(lambertw(10.0+3*i))", 1.76947134417339, 1e-10);
    expect_near("fresnels(1.0)", 0.438259147390355, 1e-11);
    expect_near("fresnelc(1.0)", 0.779893400376823, 1e-11);
    expect_near("fresnels(3.7)", 0.574980349887473, 1e-11);
    expect_near("fresnelc(5.2)", 0.438887474227742, 1e-11);
    expect_near("re(fresnels(1.0+2*i))", 36.7254648839914, 1e-8);
    expect_near("Shi(1.0)", 1.05725087537573, 1e-11);
    expect_near("Chi(1.0)", 0.837866940980208, 1e-11);
    expect_near("im(Chi(-2.0))", 3.14159265358979, 1e-11);
    expect_near("expint(1,1.0)", 0.219383934395520, 1e-11);
    expect_near("expint(2,3.5)", 0.00580189392089913, 1e-13);
    expect_near("re(expint(3,-2.0))", 1.17511543639219, 1e-10);
    expect_near("im(expint(1,0.5+i))", -0.357493773652163, 1e-11);
    expect_near("li(2.0)", 1.04516378011749, 1e-11);
    expect_near("bessely(1.5,1.0)", -1.10249557516018, 1e-10);
    expect_near("besselk(2.5,1.0)", 3.22747953113526, 1e-10);
    expect_near("im(bessely(-1.8,-1.5))", 0.0857519944018923, 1e-10);
    expect_near("besselj(0,1.0)", 0.765197686557967, 1e-11);
    /* special values and derivatives */
    expect("lambertw(0)", "0");
    expect("lambertw(e)", "1");
    expect("lambertw(2*e^2)", "2");
    expect("lambertw(-1/e)", "-1");
    expect("fresnels(-x)", "-fresnels(x)");
    expect("fresnelc(inf)", "1/2");
    expect("Shi(0)", "0");
    expect("expint(-2,x)", "exp(-x)*(1/x+2/x^2+2/x^3)");
    expect("expint(3,0)", "1/2");
    expect("li(e^x)", "Ei(x)");
    expect("diff(lambertw(x^2),x)", "2*lambertw(x^2)/((lambertw(x^2)+1)*x)");
    expect("diff(fresnels(x),x)", "sin(pi*x^2/2)");
    expect("diff(Chi(3*x),x)", "cosh(3*x)/x");
    expect("diff(expint(3,x^2),x)", "-2*expint(2,x^2)*x");
    expect("diff(li(x),x)", "1/ln(x)");
    /* re / im stay when the value may be complex; real for real arguments */
    expect("re(besselj(n+1/2,-x))", "re(besselj(n+1/2,-x))");
    expect("im(polylog(2,x))", "im(polylog(2,x))");
    expect("re(besselj(2,x))", "besselj(2,x)");
    expect("im(polylog(2,1/2))", "0");
    expect("im(fresnels(x))", "0");
}

static void test_round5_int(void) {
    cas_reset(ctx);
    /* lambertw: u = W */
    expect_antideriv("lambertw(a+b*x)");
    expect_antideriv("lambertw(a*x)^2");
    expect_antideriv("1/lambertw(a+b*x)");
    expect_antideriv("x^2/(1+lambertw(a*x))");
    expect_antideriv("lambertw(a*x^2)/x^2");
    expect_antideriv("sqrt(c*lambertw(a*x))");
    expect_antideriv("1/(x*(d+d*lambertw(a*x)))");
    /* by parts */
    expect_antideriv("x^7*fresnels(b*x)");
    expect_antideriv("fresnelc(b*x)/x^6");
    expect_antideriv("x*fresnels(b*x)*cos(pi*b^2*x^2/2)");
    expect_antideriv("x*Shi(b*x)^2");
    expect_antideriv("Chi(b*x)");
    expect_antideriv("x^2*expint(1,b*x)");
    expect_antideriv("expint(2,b*x)/x^3");
    expect_antideriv("li(b*x)");
    expect_antideriv("x^3*polylog(3,a*x)");
    expect_antideriv("polylog(3,a*x^2)/x^4");
    expect_antideriv("(a+b*ln(c*x))^2*polylog(2,d*x)/x");
    expect("integrate(polylog(2,a*x)/x,x)", "polylog(3,a*x)");
    /* Fresnel forms for sin, cos of a quadratic */
    expect("integrate(sin(x^2),x)", "sqrt(2)*fresnels(sqrt(2)*x/sqrt(pi))*sqrt(pi)/2");
    expect_antideriv("x^2*cos(3*x^2+x+1)");
    expect_antideriv("sin(x^2)/x^4");
    expect_antideriv("cos(3*x^2+1)/x^3");
    expect_antideriv("sin(a*x^2)^2");
}

/* tidy.c: the final antiderivative in a smaller form (still checked by differentiation) */
static void test_round5_tidy(void) {
    cas_reset(ctx);
    expect("integrate(sqrt(A^2+B^2*sin(x)^2)/sin(x),x)",
           "A*ln(A*cos(x)/sqrt(A^2+B^2*sin(x)^2)-1)/2-A*ln(A*cos(x)/sqrt(A^2+B^2*sin(x)^2)+1)/2-"
           "B*atan(B*cos(x)/sqrt(A^2+B^2*sin(x)^2))");
    expect("integrate(sin(x)^4/(a+a*cos(x)),x)", "(x/2-sin(x)/3-cos(x)*sin(x)/2+cos(x)^2*sin(x)/3)/a");
    expect("integrate(lambertw(a*x)^2,x)", "4*x-4*x/lambertw(a*x)-2*lambertw(a*x)*x+lambertw(a*x)^2*x");
    expect_has("integrate(x^7/((a+b*x^4)^2*sqrt(c+d*x^4)),x)", "sqrt(d*x^4+c)");
    expect_antideriv("x^7/((a+b*x^4)^2*sqrt(c+d*x^4))");
    expect_antideriv("cos(c+d*x)^6*(a+b/cos(c+d*x)+n/cos(c+d*x)^2)");
}

/* ellip.c, intell.c: elliptic integrals of half-integer powers of a + b cos u (sin u, cosh u) */
static void test_round5_ell(void) {
    cas_reset(ctx);
    expect_near("elliptic_f(0.5,0.3)", 0.506140211962355, 1e-11);
    expect_near("elliptic_e(0.5,0.3)", 0.493991144728968, 1e-11);
    expect_near("elliptic_pi(0.2,0.5,0.3)", 0.514475085488809, 1e-11);
    expect_near("elliptic_e(2.5,0.7)", 1.87132943038386, 1e-10);
    expect_near("im(elliptic_e(1.2,2))", 0.245533848629467, 1e-10);
    expect("elliptic_f(0,m)", "0");
    expect("elliptic_e(phi,0)", "phi");
    expect("diff(elliptic_e(x,m),x)", "sqrt(-m*sin(x)^2+1)");
    expect("integrate(sqrt(cos(x)),x)", "2*elliptic_e(x/2,2)");
    expect("integrate(1/sqrt(cos(x)),x)", "2*elliptic_f(x/2,2)");
    expect("integrate(sqrt(a+b*cos(x)),x)", "2*sqrt(a+b*cos(x))*elliptic_e(x/2,2*b/(a+b))/sqrt((a+b*cos(x))/(a+b))");
    expect("integrate(sqrt(sin(x)),x)", "2*elliptic_e(x/2-pi/4,2)");
    expect("integrate(1/(sqrt(cos(x))*(2+3*cos(x))),x)", "2*elliptic_pi(6/5,x/2,2)/5");
    expect_antideriv("(a+b*cos(c+d*x))*(n+b*cos(c+d*x)+cos(c+d*x)^2)/sqrt(cos(c+d*x))");
    expect_antideriv("1/cosh(a+b*x)^(7/2)");
    expect_antideriv("(a+a*sin(c+d*x))^2/cos(c+d*x)^(9/2)");
    expect_antideriv("sqrt(cos(x))/(2+cos(x))^2");
    expect_antideriv("1/(a+b*sin(x)^2)^(3/2)");
}

/* hyp.c, inthyp.c: uppergamma, hyp2f1 and the integrals that need them */
static void test_round5_hyp(void) {
    cas_reset(ctx);
    expect_near("uppergamma(2.5,1.3)", 1.0121136007032, 1e-11);
    expect_near("uppergamma(-1.5,0.7)", 0.333334344096612, 1e-11);
    expect_near("uppergamma(0.3,5.0)", 0.00194846497574874, 1e-13);
    expect_near("uppergamma(-2,1.5)", 0.0252175511868241, 1e-12);
    expect_near("hyp2f1(0.5,1.2,2.3,0.4)", 1.13245399559363, 1e-11);
    expect_near("hyp2f1(0.5,1.2,2.3,-7.0)", 0.505883666965497, 1e-11);
    expect_near("hyp2f1(1,0.7,1.7,-30.0)", 0.17417483365035, 1e-11);
    expect_near("hyp2f1(0.5,1.2,2.3,0.95)", 1.64906227660807, 1e-10);
    expect("uppergamma(3,x)", "2*exp(-x)*(x^2/2+x+1)");
    expect("uppergamma(1/2,x)", "erfc(sqrt(x))*sqrt(pi)");
    expect("uppergamma(a,0)", "gamma(a)");
    expect("diff(hyp2f1(a,b,c,x),x)", "a*b*hyp2f1(a+1,b+1,c+1,x)/c");
    expect("hyp2f1(a,b,c,0)", "1");
    expect("integrate(x^m*exp(b*x),x)", "uppergamma(m+1,-b*x)*x^m*(-b*x)^(-m)/b");
    expect_antideriv("x^n*sinh(b*x)");
    expect_antideriv("(c+d*x)^(3/2)*exp(a+b*x)");
    expect_antideriv("x^2*uppergamma(n,a*x)");
    expect_antideriv("x^n*(a+b*x^3)^(1/3)");
    expect_antideriv("(a+b*x)^n*(c+d*x)^(1/3)");
}

/* intpl.c: polylogarithms for ln(t)^j R(t), P(x) R(e^(c x)), G(ln x) ln(a + b x^q)/x */
static void test_round5_pl(void) {
    cas_reset(ctx);
    expect("integrate(x/(1+exp(x)),x)", "x^2/2-ln(exp(x)+1)*x-polylog(2,-exp(x))");
    expect("integrate(ln(x)*ln(1+x)/x,x)", "polylog(3,-x)-ln(x)*polylog(2,-x)");
    expect("integrate(ln(1+f*x^m)/x,x)", "-polylog(2,-f*x^m)/m");
    expect_antideriv("ln(1+x)^2/x");
    expect_antideriv("x^2/(1+exp(2*x))");
    expect_antideriv("x/tanh(a+b*x)");
    expect_antideriv("x*sec(x)");
    expect_antideriv("ln(a+b*x)^3/x");
    expect_antideriv("ln(c*(d+n*x)^2)^2/(a+b*x)");
    expect_antideriv("x^2*cot(a+b*x)");
    expect_antideriv("atan(c*x)^2/x");
    expect_antideriv("(a+b*ln(c*x^n))^2*ln(1+d*x^2)/x");
}

/* Wester gaps: chain rule for unknown functions, Leibniz rule, ln of Gaussian rationals,
 * c b^u in simplify, Fourier transforms, dense sums in rat_normal (J12) */
static void test_round5_wester(void) {
    cas_reset(ctx);
    expect("diff(f(g(t)),t,2)", "f'(g(t))*diff(diff(g(t),t),t)+f''(g(t))*diff(g(t),t)^2");
    expect("factor(diff(g(x^2+y^2),x)+diff(g(x^2+y^2),y))", "2*(x+y)*g'(x^2+y^2)");
    expect("diff(f(x),x)", "diff(f(x),x)");
    expect("diff(integrate(f(y),y,g(x),h(x)),x)", "diff(h(x),x)*f(h(x))-diff(g(x),x)*f(g(x))");
    expect("simplify(2*2^n)", "2^(n+1)");
    expect("simplify(2^n/8)", "2^(n-3)");
    expect("fourier(1,x,z)", "sqrt(2)*dirac(z)*sqrt(pi)");
    expect("fourier(exp(-9*x^2),x,z)", "sqrt(2)*exp(-z^2/36)/6");
    expect("fourier(abs(x)*exp(-3*abs(x)),x,z)", "-sqrt(2)*(z-3)*(z+3)/(sqrt(pi)*(z^2+9)^2)");
    expect("normal(chebyshevt(1008,x)-2*x*chebyshevt(1007,x)+chebyshevt(1006,x))", "0");
}

static void test_round5(void) {
    test_round5_wester();
    test_round5_pl();
    test_round5_fns();
    test_round5_int();
    test_round5_tidy();
    test_round5_ell();
    test_round5_hyp();
}
