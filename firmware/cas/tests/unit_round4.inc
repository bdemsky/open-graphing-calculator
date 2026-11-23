/*
 * unit_round4.inc -- CAS round 4 (cas/ext/int): the casx integrator behind the core's
 * integrate (slot CASX_FN_INTEGRATE); included by unit_tests.c.
 */

/* integrate(f, x) has a closed form, and its derivative is f at a sample point (the other
 * symbols a..d, n set to numbers) */
static void expect_antideriv(const char *f) {
    char buf[1600], F[1200];
    snprintf(buf, sizeof buf, "integrate(%s,x)", f);
    const char *r = ev(buf);
    CHECK(!strstr(r, "integrate") && !strstr(r, "ERROR"), "%s -> %s", buf, r);
    if (strstr(r, "integrate") || strstr(r, "ERROR")) return;
    snprintf(F, sizeof F, "%s", r);
    snprintf(buf, sizeof buf,
             "abs(approx(subst(subst(subst(subst(subst(subst(diff(%s,x)-(%s),x,0.613),a,0.917),b,1.231),c,0.442),"
             "d,1.377),n,0.705)))", F, f);
    const char *v = ev(buf);
    char *end;
    double re = strtod(v, &end);
    CHECK(end != v && fabs(re) < 1e-7 && !strchr(end, 'i'), "d/dx of integral of %s at a point: %s", f, v);
}

static void test_round4(void) {
    cas_reset(ctx);
    /* radicals of one or two linear forms */
    expect_antideriv("1/((b*x+a)^(3/2)*sqrt(d*x+c))");
    expect_antideriv("sqrt(b*x+a)/(x^2*sqrt(d*x+c))");
    expect_antideriv("(d*x+c)^(1/3)/(b*x+a)^(1/3)");
    expect_antideriv("(b*x+a)^(n-2)*(d*x+c)^(-n)");
    expect_antideriv("(a+b*x)^(3/2)/x^(3/2)");
    expect_antideriv("x*sqrt(c-c/(a*x))");
    /* exponentials of inverse functions */
    expect_antideriv("exp(3*atanh(a*x))*(c-a^2*c*x^2)");
    expect_antideriv("exp(-2*atanh(a*x))*sqrt(c-c/(a*x))*x");
    /* u = x^n (Chebyshev binomials), three linear forms, trigonometric substitutions */
    expect_antideriv("(a+b*x^4)^(1/4)/x^14");
    expect_antideriv("1/(x^11*(a+b*x^3)^(2/3))");
    expect_antideriv("x^5/(a+b/x^3)^(3/2)");
    expect_antideriv("exp(3*atanh(1/(a*x)))*(c-a*c*x)^(3/2)");
    expect_antideriv("exp(atanh(1/(a*x)))*sqrt(c-a^2*c*x^2)/x^2");
    expect_antideriv("tan(c+d*x)/(a+b*sin(c+d*x)^2)^(3/2)");
    expect_antideriv("cos(c+d*x)^3/(a+b*sin(c+d*x)^2)^(5/2)");
    expect_antideriv("cos(x)^2/(a+b*sin(x)^2)^2");
    expect("integrate(sin(x)/sqrt(1+sin(x)^2),x)", "-asin(sqrt(2)*cos(x)/2)");
    expect_antideriv("(a+a*sin(c+d*x))^(3/2)/(b+n*sin(c+d*x))^(3/2)");
    expect_antideriv("sec(c+d*x)^4/sqrt(a+a*cos(c+d*x))");
    expect_antideriv("x^(-1+7/2*n)/sqrt(a+b*x^n)");
    /* binomial and trinomial denominators */
    expect_antideriv("1/(x^3-b)");
    expect_antideriv("1/(a+b*x^2+c*x^4)");
    /* Ei, Si, Ci, erf, erfi */
    expect_antideriv("exp(a+b*x)/x^2");
    expect_antideriv("sin(a+b*x)/x");
    expect_antideriv("cos(b*x)/x^3");
    expect_antideriv("exp(a+b*x)/(c+d*x)");
    expect_antideriv("exp(-b*x^2)*x^2");
    expect_antideriv("exp(a+b*x-c*x^2)");
    expect_antideriv("x^3/ln(x)");
    expect_antideriv("x^2/(a+b*ln(c*x))");
    /* inverse trigonometric and hyperbolic functions */
    expect_antideriv("x^2*asin(a*x)^2");
    expect_antideriv("x*(1-c^2*x^2)^(5/2)/(a+b*asin(c*x))");
    expect_antideriv("x/asinh(a*x)^2");
    expect_antideriv("atan(a*x)^2/(c+a^2*c*x^2)");
    expect_antideriv("2^x/sqrt(1+4^x)");
    /* special functions and analysis commands (spfn.c) */
    expect("chebyshevt(5,x)", "16*x^5-20*x^3+5*x");
    expect("chebyshevu(3,x)", "8*x^3-4*x");
    expect("legendre(3,x)", "5*x^3/2-3*x/2");
    expect("legendrep(3,1,x)", "-sqrt(-x^2+1)*(15*x^2/2-3/2)");
    expect("hermite(3,x)", "8*x^3-12*x");
    expect("laguerre(2,x)", "x^2/2-2*x+1");
    expect("chebyshevt(n,-1)", "(-1)^n");
    expect("chebyshevt(n,x)", "chebyshevt(n,x)");
    expect("normal(chebyshevt(300,x)-2*x*chebyshevt(299,x)+chebyshevt(298,x))", "0");
    expect("integrate((1+x)^3*legendre(1,x)*legendre(2,x),x,-1,1)", "36/35");
    expect("digamma(1/3)", "-euler_gamma-ln(6)-ln(sqrt(3)/2)-sqrt(3)*pi/6");
    expect("digamma(4)", "11/6-euler_gamma");
    expect_near("digamma(2.5)", 0.7031566406452432, 1e-9);
    expect_near("approx(digamma(7/4))", 0.247472453546861, 1e-9);
    expect("residue((z^3+5)/((z^4-1)*(z+1)),z,-1)", "-9/4");
    expect("residue(exp(z)/z^3,z,0)", "1/2");
    expect("residue(exp(1/z),z,0)", "residue(exp(1/z),z,0)");
    expect("minimize(x^4-x+1,x)", "1-3*2^(1/3)/8");
    expect("maximize(2*x-x^2,x)", "1");
    expect("minimize(x^3,x)", "-inf");
    expect("cartprod([a,b],[c,d])", "[[a,c],[a,d],[b,c],[b,d]]");
    expect("sum(g(k+1)-g(k),k,0,n)", "g(n+1)-g(0)");
    /* eigenvalues of reducible matrices by blocks (eigblk.c) */
    expect("eigenvals((2-a)*identity(100))", "[-a+2]");
    expect("eigenvals([[1,0,0,0,0,0],[0,2,0,0,0,0],[0,0,3,1,0,0],[0,0,1,3,0,0],[0,0,0,0,b,0],[7,0,0,0,0,b]])",
           "[1,2,4,b]");
    /* sparse 3-variable factorisation (Wester H11: lifting to total degree 52, compacted) */
    expect("factor(expand((24*x*y^19*z^8-47*x^17*y^5*z^8+6*x^15*y^9*z^2-3*x^22+5)*(34*x^5*y^8*z^13+20*x^7*y^7*z^7+"
           "12*x^9*y^16*z^4+80*y^14*z)))",
           "-2*y^7*z*(6*x^9*y^9*z^3+10*x^7*z^6+17*x^5*y*z^12+40*y^7)*(3*x^22+47*x^17*y^5*z^8-6*x^15*y^9*z^2-24*x*y^19*z^8-5)");
    /* functions of the module: derivatives and values (fnx.c), dilogarithm integrals */
    expect("diff(polylog(2,x^2),x)", "-2*ln(-x^2+1)/x");
    expect("diff(polylog(3,x),x)", "polylog(2,x)/x");
    expect("diff(besselj(0,z),z)", "-besselj(1,z)");
    expect("diff(besseli(2,z),z)", "besseli(1,z)/2+besseli(3,z)/2");
    expect_near("approx(polylog(2,1/2))", 0.582240526465012, 1e-10);
    expect_near("approx(polylog(2,-3))", -1.93937542076671, 1e-10);
    expect_near("approx(polylog(3,0.9))", 1.04965895018644, 1e-10);
    expect_near("approx(polylog(4,-0.8))", -0.765102872066312, 1e-10);
    expect_near("approx(re(polylog(3,2+i)))", 1.89018684728999, 1e-9);
    expect_near("approx(bessely(1,2.5))", 0.145918137966786, 1e-10);
    expect_near("approx(besselk(0,1.5))", 0.213805562647526, 1e-10);
    expect_near("approx(besseli(1,2))", 1.59063685463733, 1e-10);
    expect_near("approx(besselj(1/2,2))", 0.513016136561828, 1e-10);
    expect_near("approx(im(besselj(2,1+i)))", 0.247397641513306, 1e-10);
    expect("integrate(ln(1+x)/x,x)", "-polylog(2,-x)");
    expect("besselj(-5/2,pi/2)", "12/pi^2");
    expect("besseli(1/2,z)", "sqrt(2)*sinh(z)*sqrt(1/z)/sqrt(pi)");
    expect_near("approx(besselj(7/2,13/10))", 0.0173149868485839, 1e-12);
    expect_near("approx(besseli(-5/2,7/10))", 5.42515345906194, 1e-10);
    expect("polylog(1,x)", "-ln(-x+1)");
    expect("polylog(2,1)", "pi^2/6");
    expect("polylog(3,-1)", "-3*zeta(3)/4");
    expect("polylog(2,1/2)", "pi^2/12-ln(2)^2/2");
    expect("f(x,y)", "f(x,y)");
    expect_antideriv("ln(a+b*x)/(c+d*x)");
    expect_antideriv("x*ln(1+x)/(1+2*x)^2");
    /* derivative of piecewise, almost everywhere (pieces on an equation dropped) */
    expect("diff(piecewise(x<0,-x,x),x)", "piecewise(x<0,-1,1)");
    expect("diff(piecewise(x<0,x^2,x<1,x,x^3),x)", "piecewise(x<0,2*x,x<1,1,3*x^2)");
    expect("subst(diff(piecewise(x=1,x^2-1,x^3),x),x,1)", "3");
    /* ln(e^c), principal branch */
    expect("ln(exp(10*i))", "10*i-4*i*pi");
    expect("ln(exp(3+5*i))", "5*i-2*i*pi+3");
    expect("ln(exp(2*i))", "2*i");
    /* nothing to find: stays unevaluated */
    expect("integrate(exp(x^3),x)", "integrate(exp(x^3),x)");
    expect("integrate(sqrt(x)*sqrt(1+x)*sqrt(2+x)*sqrt(3+x)*sqrt(4+x),x)", "integrate(sqrt(x)*sqrt(x+1)*sqrt(x+2)*sqrt(x+3)*sqrt(x+4),x)");
    cas_reset(ctx);
}
