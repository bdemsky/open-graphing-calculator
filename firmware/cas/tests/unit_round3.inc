/*
 * unit_round3.inc -- CAS round 3 (cas/ext/adv): vector calculus in
 * curvilinear coordinates, potentials, line and surface integrals (veccalc.c);
 * included by unit_tests.c.
 */
/* the value of `in` (a number) within tol of `want` */
static void expect_near(const char *in, double want, double tol) {
    const char *got = ev(in);
    char *end;
    double v = strtod(got, &end);
    CHECK(end != got && fabs(v - want) <= tol, "%s -> %s (want %.9g +- %g)", in, got, want, tol);
}

/* the closed form (at x0, t0) and the finite-difference grid (gridval) agree within tol */
static void expect_cross(const char *closed, const char *num, const char *rx, const char *rt, double x0, double t0, double tol) {
    char buf[1400];
    snprintf(buf, sizeof buf, "approx(subst(subst(%s,x,%.17g),t,%.17g))", closed, x0, t0);
    const char *a = ev(buf);
    char *ea;
    double va = strtod(a, &ea);
    CHECK(ea != a, "closed form %s is not a number: %s", closed, a);
    snprintf(buf, sizeof buf, "gridval(%s,%s,%s,%.17g,%.17g)", num, rx, rt, x0, t0);
    const char *b = ev(buf);
    char *end;
    double vb = strtod(b, &end);
    CHECK(end != b && fabs(va - vb) <= tol, "closed %s = %.9g, numeric %s = %s (tol %g)", closed, va, num, b, tol);
}

static void test_round3_pde(void) {
    cas_reset(ctx);
    /* first order: characteristics */
    expect("pdesolve(u_t(x,t)+2*u_x(x,t)=0,u(x,t),u(x,0)=sin(x))", "sin(x-2*t)");
    expect("pdesolve(x*diff(u(x,y),x)+y*diff(u(x,y),y)=u(x,y),u(x,y))", "F(y/x)*x");
    expect("pdesolve(diff(u(x,y),x)+diff(u(x,y),y)+u(x,y)=x,u(x,y))", "x+F(y-x)*exp(-x)-1");
    expect("pdesolve(diff(u(x,t),t)+c*diff(u(x,t),x)=0,u(x,t))", "F(-x/c+t)");
    /* separation of variables */
    expect("pdesolve(diff(u(x,t),t)=diff(u(x,t),x,2),u(x,t),[u(0,t)=0,u(pi,t)=0,u(x,0)=sin(x)+3*sin(2*x)])",
           "exp(-t)*sin(x)+3*exp(-4*t)*sin(2*x)");
    expect("pdesolve(diff(u(x,t),t)=k*diff(u(x,t),x,2),u(x,t),[u(0,t)=0,u(1,t)=0,u(x,0)=x*(1-x)])",
           "sum(-4*((-1)^n-1)*exp(-k*n^2*pi^2*t)*sin(n*pi*x)/(n^3*pi^3),n,1,inf)");
    expect("pdesolve(u_t(x,t)=2*u_xx(x,t),u(x,t),[u_x(0,t)=0,u_x(L,t)=0,u(x,0)=x])",
           "L/2+sum(2*((-1)^n-1)*L*cos(n*pi*x/L)*exp(-2*n^2*pi^2*t/L^2)/(n^2*pi^2),n,1,inf)");
    expect("pdesolve(u_t(x,t)=u_xx(x,t),u(x,t),[u(0,t)=1,u(1,t)=3,u(x,0)=1+2*x+sin(pi*x)])", "2*x+exp(-pi^2*t)*sin(pi*x)+1");
    expect("pdesolve(u_t(x,t)=u_xx(x,t),u(x,t),[u(0,t)=0,u(1,t)=0,u(x,0)=sin(pi*x)^3])",
           "3*exp(-pi^2*t)*sin(pi*x)/4-exp(-9*pi^2*t)*sin(3*pi*x)/4");
    expect("pdesolve(u_t(x,t)=u_xx(x,t),u(x,t),[u(0,t)=0,u(1,t)=0,u(x,0)=1],3)",
           "4*exp(-pi^2*t)*sin(pi*x)/pi+4*exp(-9*pi^2*t)*sin(3*pi*x)/(3*pi)");
    expect("pdesolve(u_t(x,t)=u_xx(x,t),u(x,t),[u(0,t)=0,u(pi,t)=0,u(x,0)=x*sin(x)])",
           "exp(-t)*pi*sin(x)/2+sum(-4*((-1)^n+1)*exp(-n^2*t)*n*sin(n*x)/((n-1)^2*(n+1)^2*pi),n,2,inf)");
    expect("pdesolve(u_tt(x,t)=4*u_xx(x,t),u(x,t),[u(0,t)=0,u(1,t)=0,u(x,0)=sin(pi*x),u_t(x,0)=0])", "cos(2*pi*t)*sin(pi*x)");
    expect("pdesolve(u_tt(x,t)=u_xx(x,t),u(x,t),[u(0,t)=0,u(pi,t)=0,u(x,0)=0,u_t(x,0)=x])",
           "sum(-2*(-1)^n*sin(n*t)*sin(n*x)/n^2,n,1,inf)");
    expect("pdesolve(u_xx(x,y)+u_yy(x,y)=0,u(x,y),[u(0,y)=0,u(1,y)=0,u(x,0)=0,u(x,1)=sin(pi*x)])", "sin(pi*x)*sinh(pi*y)/sinh(pi)");
    /* the line: d'Alembert, Gauss-Weierstrass */
    expect("pdesolve(u_tt(x,t)=c^2*u_xx(x,t),u(x,t),[u(x,0)=exp(-x^2),u_t(x,0)=0])", "exp(-(x+c*t)^2)/2+exp(-(x-c*t)^2)/2");
    expect("pdesolve(u_tt(x,t)=u_xx(x,t),u(x,t),[u(x,0)=0,u_t(x,0)=cos(x)])", "cos(x)*sin(t)");
    expect("pdesolve(u_t(x,t)=k*u_xx(x,t),u(x,t),[u(x,0)=x^2])", "x^2+2*k*t");
    /* a high mode (simplify no longer expands sin(50 pi x) into a polynomial: trig.c big_multiple) */
    expect("simplify(sin(50*pi*x))", "sin(50*pi*x)");
    expect("pdesolve(u_t(x,t)=u_xx(x,t),u(x,t),[u(0,t)=0,u(1,t)=0,u(x,0)=sin(50*pi*x)],200)", "exp(-2500*pi^2*t)*sin(50*pi*x)");
    /* not handled: unevaluated */
    expect("pdesolve(u_t(x,t)=u_xx(x,t)+u(x,t)^2,u(x,t),[u(x,0)=1])", "pdesolve(u_t(x,t)=u(x,t)^2+u_xx(x,t),u(x,t),[u(x,0)=1])");
    expect("pdesolve(u_t(x,t)=u_xx(x,t),u(x,t),[u(0,t)=t,u(1,t)=0,u(x,0)=0])", "pdesolve(u_t(x,t)=u_xx(x,t),u(x,t),[u(0,t)=t,u(1,t)=0,u(x,0)=0])");

    /* finite differences against the exact solutions */
    ev("U:=pdenum(u_t(x,t)=u_xx(x,t),u(x,t),[u(0,t)=0,u(1,t)=0,u(x,0)=sin(pi*x)],[x,0,1,10],[t,0,0.1,50])");
    expect_near("gridval(U,[0,1],[0,0.1],0.5,0.1)", 0.372707838853, 1e-3);
    expect_near("size(U)", 51, 0);
    expect_near("size(U[1])", 11, 0);
    expect("pdenum(u_t(x,t)=u_xx(x,t),u(x,t),[u(0,t)=0,u(1,t)=0,u(x,0)=sin(pi*x)],[x,0,1,20],[t,0,0.1,10],explicit)",
           "ERROR 2: pdenum: explicit scheme unstable (k dt/dx^2 > 1/2): more time steps or cn");
    expect("pdenum(u_tt(x,t)=u_xx(x,t),u(x,t),[u(0,t)=0,u(1,t)=0,u(x,0)=0,u_t(x,0)=1],[x,0,1,20],[t,0,1,10])",
           "ERROR 2: pdenum: unstable (c dt/dx > 1): more time steps");
    expect("pdenum(u_t(x,t)=u_xx(x,t),u(x,t),[u(0,t)=0,u(1,t)=0,u(x,0)=1],[x,0,1,1000],[t,0,1,1000])",
           "ERROR 4: pdenum: too many grid points (at most 250000)");
    expect_near("gridval([[1,2],[3,4]],[0,1],[0,1],0.5,0.5)", 2.5, 1e-12);
    expect("gridval([[1,2],[3,4]],[0,1],[0,1],2,0.5)", "undef");
    /* heat D-D: series (40 terms) against Crank-Nicolson */
    ev("S:=pdesolve(u_t(x,t)=u_xx(x,t),u(x,t),[u(0,t)=0,u(1,t)=0,u(x,0)=x*(1-x)],40)");
    ev("V:=pdenum(u_t(x,t)=u_xx(x,t),u(x,t),[u(0,t)=0,u(1,t)=0,u(x,0)=x*(1-x)],[x,0,1,50],[t,0,0.1,200],cn)");
    expect_cross("S", "V", "[0,1]", "[0,0.1]", 0.3, 0.05, 2e-4);
    expect_cross("S", "V", "[0,1]", "[0,0.1]", 0.5, 0.1, 2e-4);
    /* heat N-N (explicit) */
    ev("S:=pdesolve(u_t(x,t)=u_xx(x,t),u(x,t),[u_x(0,t)=0,u_x(1,t)=0,u(x,0)=x],40)");
    ev("V:=pdenum(u_t(x,t)=u_xx(x,t),u(x,t),[u_x(0,t)=0,u_x(1,t)=0,u(x,0)=x],[x,0,1,40],[t,0,0.1,400])");
    expect_cross("S", "V", "[0,1]", "[0,0.1]", 0.2, 0.1, 1e-3);
    /* heat with boundary values (D-N) */
    ev("S:=pdesolve(u_t(x,t)=u_xx(x,t),u(x,t),[u(0,t)=1,u_x(1,t)=0,u(x,0)=0],40)");
    ev("V:=pdenum(u_t(x,t)=u_xx(x,t),u(x,t),[u(0,t)=1,u_x(1,t)=0,u(x,0)=0],[x,0,1,40],[t,0,0.2,200],cn)");
    expect_cross("S", "V", "[0,1]", "[0,0.2]", 0.7, 0.2, 2e-3);
    /* wave D-D (leapfrog) */
    ev("S:=pdesolve(u_tt(x,t)=u_xx(x,t),u(x,t),[u(0,t)=0,u(1,t)=0,u(x,0)=x*(1-x),u_t(x,0)=0],40)");
    ev("V:=pdenum(u_tt(x,t)=u_xx(x,t),u(x,t),[u(0,t)=0,u(1,t)=0,u(x,0)=x*(1-x),u_t(x,0)=0],[x,0,1,100],[t,0,0.5,100])");
    expect_cross("S", "V", "[0,1]", "[0,0.5]", 0.3, 0.2, 1e-3);
    ev("S:=pdesolve(u_tt(x,t)=4*u_xx(x,t),u(x,t),[u(0,t)=0,u(1,t)=0,u(x,0)=0,u_t(x,0)=sin(pi*x)])");
    ev("V:=pdenum(u_tt(x,t)=4*u_xx(x,t),u(x,t),[u(0,t)=0,u(1,t)=0,u(x,0)=0,u_t(x,0)=sin(pi*x)],[x,0,1,50],[t,0,0.5,100])");
    expect_cross("S", "V", "[0,1]", "[0,0.5]", 0.4, 0.3, 1e-3);
    /* Laplace on the unit square (SOR); here t is y */
    ev("S:=subst(pdesolve(u_xx(x,y)+u_yy(x,y)=0,u(x,y),[u(0,y)=0,u(1,y)=y,u(x,0)=0,u(x,1)=x*(1-x)],40),y,t)");
    ev("V:=pdenum(u_xx(x,y)+u_yy(x,y)=0,u(x,y),[u(0,y)=0,u(1,y)=y,u(x,0)=0,u(x,1)=x*(1-x)],[x,0,1,40],[y,0,1,40])");
    expect_cross("S", "V", "[0,1]", "[0,1]", 0.5, 0.5, 1e-3);
    expect_cross("S", "V", "[0,1]", "[0,1]", 0.75, 0.25, 1e-3);
    /* Poisson: u_xx + u_yy = -2 pi^2 sin(pi x) sin(pi y) has u = sin(pi x) sin(pi y) */
    ev("V:=pdenum(u_xx(x,y)+u_yy(x,y)=-2*pi^2*sin(pi*x)*sin(pi*y),u(x,y),[u(0,y)=0,u(1,y)=0,u(x,0)=0,u(x,1)=0],[x,0,1,40],[y,0,1,40])");
    expect_near("gridval(V,[0,1],[0,1],0.5,0.5)", 1, 2e-3);
    /* the graph compiler: gridval of a stored grid becomes a table lookup (OP_GRID) */
    {
        cas_fn *fn = NULL;
        const char *xt[2] = {"x", "t"};
        cas_status st = cas_compile_vars(ctx, "gridval(U,[0,1],[0,0.1],x,t)", xt, 2, &fn);
        CHECK(st == CAS_OK && fn, "compile gridval: %s", cas_last_error(ctx));
        if (fn) {
            double v[2] = {0.5, 0.1}, w[2] = {1.5, 0.1};
            double a = cas_fn_eval_vars(fn, v), b = strtod(ev("gridval(U,[0,1],[0,0.1],0.5,0.1)"), NULL);
            CHECK(fabs(a - b) < 1e-12, "compiled gridval %.12g vs %.12g", a, b);
            CHECK(isnan(cas_fn_eval_vars(fn, w)), "compiled gridval outside the grid is NaN");
            cas_fn_free(ctx, fn);
        }
    }
    expect("purge(U)", "true");
    expect("purge(V)", "true");
    expect("purge(S)", "true");
}

/* modular gcd (cas/ext/alg/mgcd.c), Hensel-lifting factorisation (mfactor.c), memory */
static void test_round3_alg(void) {
    cas_reset(ctx);
    expect("gcd(expand((24*x*y^19*z^8-47*x^17*y^5*z^8+6*x^15*y^9*z^2-3*x^22+5)*(11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8)),"
           "expand((34*x^5*y^8*z^13+20*x^7*y^7*z^7+12*x^9*y^16*z^4+80*y^14*z)*(11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8)))",
           "47*x^17*y^5*z^8+11*x^12*y^7*z^13-23*x^2*y^8*z^10");
    expect("gcd(expand(y^2*(x+y)*(x^2+y)),expand(y^2*(x+y)*(x-1)))", "x*y^2+y^3");
    expect("gcd(expand((x*y^2*z-y^3*z+x^3)*(x+1)*(x^5+y^4*z+1)),expand((x*y^2*z-y^3*z+x^3)*(y+2)*(x^5+y^4*z+1)))",
           "-x^8-x^6*y^2*z+x^5*y^3*z-x^3*y^4*z-x^3-x*y^6*z^2-x*y^2*z+y^7*z^2+y^3*z"); /* same as the PRS */
    expect("factor(expand((x-2*y^2+3*z^3)^20))", "(x-2*y^2+3*z^3)^20");
    expect("factor(expand((sin(x)-2*cos(y)^2+3*tan(z)^3)^20))", "(sin(x)-2*cos(y)^2+3*tan(z)^3)^20");
    expect("factor(expand((64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81)*(72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)))",
           "-(16*x^60+21*x^47-64*x^34+126*x^8+46*x^5+81)*(72*x^60-83*x^52-22*x^39-25*x^25-19*x^23+54*x^10+81)");
    expect("resultant(expand((64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81)*(34*x^19-25*x^16+70*x^7+20*x^3-91*x-86)),"
           "expand((72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)*(34*x^19-25*x^16+70*x^7+20*x^3-91*x-86)),x)", "0");
    expect("factor(det([[1,1,1,1],[w,x,y,z],[w^2,x^2,y^2,z^2],[w^3,x^3,y^3,z^3]]))", "-(x-w)*(w-y)*(x-y)*(w-z)*(x-z)*(y-z)");
    expect("factor(expand((x^2+y*z+1)*(x*y-z^2+3)))", "(x^2+y*z+1)*(x*y-z^2+3)");
    expect("factor(expand((x*y+1)*(x+y)*(x^2-y)))", "(x+y)*(x*y+1)*(x^2-y)");
    expect("factor(x^2*y^2-1)", "(x*y-1)*(x*y+1)");
    expect("factor(x^4+y^4)", "x^4+y^4");
    expect("factor(-6*x^2+6)", "-6*(x-1)*(x+1)");
    expect("factormod(4*x^2-21*x*y+20*y^2,3)", "(x+y)*(x-y)");
    expect("factormod(3*x^2+x*y,3)", "x*y");
    /* incomplete eigenvalues (a general quintic factor) stay unevaluated */
    expect("eigenvals([[0,0,0,0,1],[1,0,0,0,1],[0,1,0,0,0],[0,0,1,0,0],[0,0,0,1,0]])",
           "[1.16730397826,0.18123244447-1.08395410132*i,-0.764884433601-0.352471546032*i,0.352471546032*i-0.764884433601,0.18123244447+1.08395410132*i]");
    expect("eigenvals([[0,0,0,0,a,0],[1,0,0,0,1,0],[0,1,0,0,0,0],[0,0,1,0,0,0],[0,0,0,1,0,0],[0,0,0,0,0,2]])",
           "eigenvals([[0,0,0,0,a,0],[1,0,0,0,1,0],[0,1,0,0,0,0],[0,0,1,0,0,0],[0,0,0,1,0,0],[0,0,0,0,0,2]])");
    expect("simplify(sin(50*x)^2+cos(50*x)^2)", "1");
}

/* sums, products and limits of the extension slots (cas/ext/alg/sums.c) */
static void test_round3_sums(void) {
    cas_reset(ctx);
    expect("sum(k^2*binomial(n,k),k,1,n)", "n*2^n*(n+1)/4");
    expect("sum(binomial(n,k)/(k+1),k,0,n)", "(2^(n+1)-1)/(n+1)");
    expect("sum(sin(k*x),k,1,n)", "sin(n*x/2)*sin((n+1)*x/2)/sin(x/2)");
    expect("sum(sin((2*k-1)*x),k,1,n)", "sin(n*x)^2/sin(x)");
    expect("sum(cos(k*x),k,0,n)", "cos(n*x/2)*sin((n+1)*x/2)/sin(x/2)");
    expect("sum(k^3*binomial(n,k),k,2,n)", "2^(n-3)*n^2*(n+3)-n");
    expect("product((2*k-1)/(2*k),k,1,n)", "gamma(n+1/2)/(gamma(n+1)*sqrt(pi))");
    expect("product((k+1/3)/k,k,1,n)", "gamma(n+4/3)/(gamma(4/3)*gamma(n+1))");
    expect("product(k-5,k,1,n)", "product(k-5,k,1,n)"); /* a zero factor in the range: no closed form */
    expect("limit(n!^(1/n)/n,n,inf)", "exp(-1)");
    expect("limit(gamma(n+1)^(1/n)/n,n,inf)", "exp(-1)");
    expect("limit(n!*exp(n)/n^(n+1/2),n,inf)", "sqrt(2)*sqrt(pi)");
    expect("limit((2*n)!/(n!)^2/4^n*sqrt(n),n,inf)", "1/sqrt(pi)");
    expect("assume(a>0)", "a>0");
    expect("limit(gamma(z+a)/gamma(z)*exp(-a*ln(z)),z,inf)", "1");
    expect("unassume(a)", "true");
    expect("limit((exp(x*exp(-x)/(exp(-x)+exp(-2*x^2/(x+1))))-exp(x))/x,x,inf)", "-exp(2)"); /* Wester T5 */
    /* a closed form is checked against the explicit sum */
    expect("sum(k*binomial(n,k)/(k+1),k,0,n)", "2^n-(2^(n+1)-1)/(n+1)");
    /* Laplace transforms moved into the module (cas/ext/adv/lapl.c) */
    expect("laplace(t*exp(2*t))", "1/(s-2)^2");
    expect("ilaplace(1/(s^2+4),s,t)", "sin(2*t)/2");
}

static void test_round3(void) {
    test_round3_alg();
    test_round3_sums();
    cas_reset(ctx);
    /* curvilinear grad / div / curl / laplacian (the core forwards a coordinate argument) */
    expect("grad(r^2*sin(t)*cos(p),[r,t,p],spherical)", "[2*cos(p)*r*sin(t),cos(p)*cos(t)*r,-r*sin(p)]");
    expect("divergence([r^2,0,0],[r,t,p],spherical)", "4*r");
    expect("divergence([r,0],[r,t],polar)", "2");
    expect("laplacian(1/r,[r,t,p],spherical)", "0");
    expect("laplacian(r^2,[r,t,z],cylindrical)", "4");
    expect("laplacian(ln(r),[r,t],polar)", "0");
    expect("laplacian(x^2*y+z^3,[x,y,z])", "2*y+6*z");
    expect("laplacian([x^2,y^2,z^2],[x,y,z])", "[2,2,2]");
    expect("laplacian([0,0,r^2],[r,t,z],cylindrical)", "[0,0,4]");
    expect("curl([0,0,r^2],[r,t,z],cylindrical)", "[0,-2*r,0]");
    expect("curl([0,r],[r,t],polar)", "2");
    expect("curl([-y,x],[x,y])", "2");
    expect("curl([0,0,0],[r,t,p],spherical)", "[0,0,0]");
    expect("grad(x,[x,y],elliptic)", "ERROR 2: coordinates: cartesian, polar [r,t], cylindrical [r,t,z] or spherical [r,t,p]");
    /* potentials */
    expect("potential([2*x*y,x^2+1,6*z^2],[x,y,z])", "x^2*y+y+2*z^3");
    expect("potential([y,-x],[x,y])", "ERROR 2: potential: the field is not conservative (its curl is not 0)");
    expect("potential([1/r,0],[r,t],polar)", "ln(r)");
    expect("potential([cos(x)*y,sin(x)],[x,y])", "sin(x)*y");
    expect("vpotential([2*y*z^3-2*x^2*y^2*z,x*y,2*x*y^2*z^2-x*z],[x,y,z])", "[x*y*z,x^2*y^2*z^2-y*z^4/2,0]");
    expect("vpotential([0,0,1],[x,y,z])", "[0,x,0]");
    expect("vpotential([x,0,0],[x,y,z])", "ERROR 2: vpotential: the field is not solenoidal (its divergence is not 0)");
    /* line and surface integrals, flux */
    expect("lineint([-y,x],[x,y],[cos(t),sin(t)],t,0,2*pi)", "2*pi");
    expect("lineint(1,[x,y],[cos(t),sin(t)],t,0,2*pi)", "2*pi");
    expect("lineint(x*y,[x,y,z],[cos(t),sin(t),t],t,0,pi/2)", "sqrt(2)/2");
    expect("lineint(sqrt(x^2+y^2),[x,y],[t*cos(t),t*sin(t)],t,0,1)", "2*sqrt(2)/3-1/3");
    expect("surfint(1,[x,y,z],[R*sin(u)*cos(v),R*sin(u)*sin(v),R*cos(u)],u,0,pi,v,0,2*pi)", "4*R^2*pi");
    expect("surfint(z^2,[x,y,z],[sin(u)*cos(v),sin(u)*sin(v),cos(u)],u,0,pi,v,0,2*pi)", "4*pi/3");
    expect("surfint(1,[x,y,z],[u,v,1-u-v],u,0,1-v,v,0,1)", "sqrt(3)/2");
    expect("flux([x,y,z],[x,y,z],[sin(u)*cos(v),sin(u)*sin(v),cos(u)],u,0,pi,v,0,2*pi)", "4*pi");
    expect("flux([x,y],[x,y],[cos(t),sin(t)],t,0,2*pi)", "2*pi");
    test_round3_pde();
}
