/*
 * unit_gaps.inc -- capabilities added to close known gaps against the other
 * CAS engines (Wester COMPARISON.md, suites REPORT.md); included by
 * unit_tests.c.
 */
static void test_gaps(void) {
    cas_reset(ctx);
    /* the extension commands (casx.c, cas/ext/adv) are registered here (CASX_BUILTIN) and all
     * listed as known, so a missing module reports itself instead of an unknown function */
    {
        extern const char *casx_unlisted(void);
        const char *u = casx_unlisted();
        CHECK(!u, "extension command %s is not in casx.c known_names", u ? u : "");
        expect("jordan([[1,1],[0,1]])", "[[1,1],[0,1]]");
        expect("factorial2(7)", "105");
    }
    /* exact roots (solve.c): roots of unity, shifted / Ferrari quartics,
     * exact snaps of numeric roots */
    expect("csolve(x^4+x^3+x^2+x+1=0,x)",
           "[-sqrt(5)/4-sqrt(10-2*sqrt(5))*i/4-1/4,sqrt(10-2*sqrt(5))*i/4-sqrt(5)/4-1/4,"
           "sqrt(5)/4-sqrt(2*sqrt(5)+10)*i/4-1/4,sqrt(5)/4+sqrt(2*sqrt(5)+10)*i/4-1/4]");
    expect("csolve(x^4-x^2+1=0,x)", "[-sqrt(3)/2-i/2,sqrt(3)/2-i/2,i/2-sqrt(3)/2,sqrt(3)/2+i/2]");
    expect("solve(x^4-4*x-1=0,x)", "[sqrt(2)/2-sqrt(4*sqrt(2)-2)/2,sqrt(2)/2+sqrt(4*sqrt(2)-2)/2]");
    expect("solve(x^4-4*x^3+6*x^2-4*x-3=0,x)", "[1-sqrt(2),sqrt(2)+1]");
    expect("solve(abs(2*x+5)=abs(x-2),x)", "[-7,-1]");
    expect("solve(1-abs(x)=max(-x-2,x-2),x)", "[-3/2,3/2]");
    expect("solve(sqrt(ln(x))=ln(sqrt(x)),x)", "[1,exp(4)]");
    expect("csolve(x^2+t^2*w^2=0,x)", "[i*t*w,-i*t*w]");

    /* matrix functions and structure (matfn.c, cmds.c norm, parse.c L[i]) */
    expect("expm([[1,-2],[2,1]])", "[[cos(2)*e,-e*sin(2)],[e*sin(2),cos(2)*e]]");
    expect("expm([[0,1],[0,0]])", "[[1,1],[0,1]]");
    expect("expm([[0,t],[-t,0]])", "[[cos(t),sin(t)],[-sin(t),cos(t)]]");
    expect("expm([[0,1,0,0],[0,0,0,2*w],[0,0,0,1],[0,-2*w,3*w^2,0]]*t)",
           "[[1,(-3*t*w+4*sin(t*w))/w,6*t*w-6*sin(t*w),(2-2*cos(t*w))/w],[0,4*cos(t*w)-3,6*w-6*cos(t*w)*w,"
           "2*sin(t*w)],[0,(2*cos(t*w)-2)/w,4-3*cos(t*w),sin(t*w)/w],[0,-2*sin(t*w),3*sin(t*w)*w,cos(t*w)]]");
    expect("sinm(pi/2*[[2,1,1],[2,3,2],[1,1,2]])", "[[1,0,0],[0,1,0],[0,0,1]]");
    expect("sinm([[a,1,0],[0,a,0],[0,0,b]])", "[[sin(a),cos(a),0],[0,sin(a),0],[0,0,sin(b)]]");
    expect("cosm([[0,1],[-1,0]])", "[[cos(i),0],[0,cos(i)]]");
    expect("[[1,1,0],[0,1,0],[0,0,1]]^(1/2)", "[[1,1/2,0],[0,1,0],[0,0,1]]");
    expect("sqrtm([[4,0],[0,9]])", "[[2,0],[0,3]]");
    expect("[[0,1,0],[0,0,0],[0,0,0]]^(1/2)", "sqrt([[0,1,0],[0,0,0],[0,0,0]])");
    expect("expm([[1.5,0],[0,1]])", "expm([[1.5,0],[0,1]])");
    expect("jordan([[1,0,0,1,-1],[0,1,-2,3,-3],[0,0,-1,2,-2],[1,-1,1,0,1],[1,-1,1,-1,2]])",
           "[[-1,0,0,0,0],[0,1,1,0,0],[0,0,1,0,0],[0,0,0,1,1],[0,0,0,0,1]]");
    expect("jordan([[2,1,0],[0,2,1],[0,0,2]])", "[[2,1,0],[0,2,1],[0,0,2]]");
    expect("jordan([[2,0],[0,2]])", "[[2,0],[0,2]]");
    expect("minpoly([[17,-8,-12,14],[46,-22,-35,41],[-2,1,4,-4],[4,-2,-2,3]],x)", "(x-1)^2*(x+1)");
    expect("minpoly([[2,0],[0,2]])", "x-2");
    expect("diag([[1,2,3],[4,5,6],[7,8,9]],1)", "[2,6]");
    expect("diag([[1,2,3],[4,5,6],[7,8,9]],-2)", "[7]");
    expect("diag([1,2])", "[[1,0],[0,2]]");
    expect("minor([[1,2,3],[4,5,6],[7,8,9]],2,3)", "[[1,2],[7,8]]");
    expect("submatrix([[1,2,3],[4,5,6],[7,8,9]],2,3,1,2)", "[[4,5],[7,8]]");
    expect("blockdiag([[a,1],[0,a]],b)", "[[a,1,0],[0,a,0],[0,0,b]]");
    expect("mod([[7,11],[3,8]],2)", "[[1,1],[1,0]]");
    expect("norm([[1,-2*i],[-3*i,4]],inf)", "7");
    expect("norm([[1,-2],[-3,4]],1)", "6");
    expect("norm([3,-4],inf)", "4");
    expect("norm([3,-4],1)", "7");
    expect("norm([3,4],2)", "5");
    expect("norm([[1,2],[3,4]],3)", "ERROR 2: norm: p must be 1, 2, inf or a number");
    expect("L:=[5,6,7]", "[5,6,7]");
    expect("L[2]", "6");
    expect("L[-1]", "7");
    expect("L[4]", "ERROR 2: index out of range");
    expect("M:=[[1,2],[3,4]]", "[[1,2],[3,4]]");
    expect("M[2,1]", "3");
    expect("M[1][2]", "2");
    expect("M[2]", "[3,4]");
    expect("2*L [2]", "ERROR 2: vector sizes do not match");
    expect("purge(L,M)", "true");

    /* linear ODE systems via expm, rsolve (matfn.c) */
    expect("dsolve([u'=u-v,v'=u+v],[u,v],t)", "[u=-exp(t)*(c2*sin(t)-c1*cos(t)),v=exp(t)*(c1*sin(t)+c2*cos(t))]");
    expect("dsolve([u'=3*u-w,v'=2*v,w'=u+w],[u,v,w],t)",
           "[u=exp(2*t)*(c1*t+c1-c3*t),v=c2*exp(2*t),w=exp(2*t)*(c1*t-c3*t+c3)]");
    expect("dsolve([u'=v,v'=-u,u(0)=1,v(0)=0],[u,v],t)", "[u=cos(t),v=-sin(t)]");
    expect("dsolve([u'=v,v'=-u+1],[u,v],t)", "[u=c1*cos(t)+c2*sin(t)+1,v=c2*cos(t)-c1*sin(t)]");
    expect("dsolve([u'=u*v,v'=u],[u,v],t)", "ERROR 6: dsolve: only linear systems with constant coefficients");
    expect("dsolve([u'=t*v,v'=u],[u,v],t)", "ERROR 6: dsolve: only linear systems with constant coefficients");
    expect("dsolve([u'=v,v'=u,u(0)=1,u(0)=2],[u,v],t)", "ERROR 2: conditions cannot be satisfied");
    expect("rsolve(f(n+2)-2*f(n+1)+f(n)=2,f(n),[f(0)=1,f(1)=m])", "m*n+n^2-2*n+1");
    expect("rsolve(f(n)=5*f(n-1)-6*f(n-2),f(n),[f(0)=0,f(1)=1])", "3^n-2^n");
    expect("rsolve(f(n+1)=2*f(n)+1,f(n),[f(0)=0])", "2^n-1");
    expect("rsolve(f(n+1)=2*f(n)+2^n,f(n),[f(0)=0])", "2^n*n/2");
    expect("rsolve(f(n+1)-f(n)=n,f(n),[f(0)=0])", "n^2/2-n/2");
    expect("rsolve(f(n+1)=3*f(n),f(n))", "3^n*c1");
    expect("rsolve(f(n+1)=n*f(n),f(n))", "rsolve(f(n+1)=f(n)*n,f(n))");
    /* integration (integ.c): irreducible cubics split over the reals, heaviside /
     * piecewise / sign / abs of a linear argument with continuous antiderivatives */
    expect("integrate(1/(x^3+2),x)", "2^(1/3)*sqrt(3)*atan(2^(2/3)*sqrt(3)*x/3-sqrt(3)/3)/6+2^(1/3)*ln(x+2^(1/3))/6-"
                                     "2^(1/3)*ln(x^2-2^(1/3)*x+2^(2/3))/12");
    expect("simplify(diff(integrate(x/(x^3-3),x),x)-x/(x^3-3))", "0");
    ev("integrate(1/(x^3+x+1),x,0,1)");
    {
        static char in[2000];
        snprintf(in, sizeof in, "approx(%s)", out);
        expect(in, "0.630319322412"); /* mpmath quad: 0.630319322412408 */
    }
    expect("integrate(piecewise(x<0,-x,x),x)", "heaviside(x)*x^2-x^2/2");
    expect("integrate(x*heaviside(x)-x*heaviside(-x),x)", "heaviside(x)*x^2-x^2/2");
    expect("integrate(x*heaviside(2-x),x)", "x^2/2-heaviside(x-2)*(x^2/2-2)");
    expect("integrate(piecewise(x<1,x^2,2*x),x,0,2)", "10/3");
    expect("integrate(exp(x)*heaviside(x-1),x,0,2)", "e*(e-1)");
    expect("integrate((heaviside(t-1)-heaviside(t-2))*cos(t),t,0,u)",
           "heaviside(u-1)*(sin(u)-sin(1))-heaviside(u-2)*(sin(u)-sin(2))");
    expect("assume(p>0)", "p>0");
    expect("heaviside(p)+heaviside(-p)+heaviside(pi-3)", "2");
    expect("purge(p)", "true");
    expect("heaviside(p)", "heaviside(p)");
    /* integer, combinatorial and list-statistics commands (ntcmds.c, parse.c n!!) */
    expect("[10!!,9!!,0!!,(-1)!!]", "[3840,945,1,1]");
    expect("n!!", "factorial2(n)");
    expect("(3!)!", "720");
    expect("pochhammer(a,3)", "a*(a+1)*(a+2)");
    expect("bernoulli(16)", "-3617/510");
    expect("[fibonacci(10),fibonacci(-6),fibonacci(-7)]", "[55,-8,13]");
    expect("fibonacci(100)", "354224848179261915075");
    expect("[stirling1(5,2),stirling2(5,2),stirling1(4,4),stirling2(3,5)]", "[-50,15,1,0]");
    expect("[partition(4),partition(100)]", "[5,190569292]");
    expect("[primroot(191),primroot(2),primroot(7)]", "[19,1,3]");
    expect("primroot(15)", "ERROR 2: primroot: expected a prime");
    expect("contfrac(pi,5)", "[3,7,15,1,292]");
    expect("contfrac(sqrt(23),5)", "[4,1,3,1,8]");
    expect("contfrac((1+sqrt(5))/2,5)", "[1,1,1,1,1]");
    expect("contfrac(415/93)", "[4,2,6,7]");
    expect("contfrac(-7/3)", "[-3,1,2]");
    expect("contfrac(e,10)", "[2,1,2,1,1,4,1,1,6,1]");
    expect("contfrac(sqrt(2)^2,3)", "[2]");
    expect("[quartile1([1,2,3,4,5,6,7,8]),quartile3([1,2,3,4,5,6,7,8]),quartile1([5,1,4,2,3])]", "[5/2,13/2,3/2]");
    expect("mode([3,7,11,7,3,5,7])", "7");
    expect("mode([1,2,2,1])", "[1,2]");
    expect("[stddevp([1,2,3,4,5]),variancep([1,2,3,4,5])]", "[sqrt(10)/2,5/2]");
    expect("linreg([1,2,3],[2,4,7])", "5*x/2-2/3");
    expect("[covariance([1,2,3,4],[2,4,5,9]),covariancep([1,2,3,4],[2,4,5,9])]", "[11/4,11/3]");
    expect("correlation([1,2,3,4],[2,4,5,9])", "11*sqrt(130)/130");
    expect("ttest([5.1,4.9,5.6,5.8,6.0,5.3],5)", "[t=2.60532329994,df=5,p=0.0479399246663,mean=5.45,sx=0.423083916026,reject=true]");
    expect("ttest2([5.1,4.9,5.6,5.8,6.0,5.3],[4.2,4.8,5.0,4.4,4.9])", "[t=3.41759784743,df=8.99971981045,p=0.00765732889136,mean1=5.45,mean2=4.66,reject=true]");
    expect("ttest2([5.1,4.9,5.6,5.8,6.0,5.3],[4.2,4.8,5.0,4.4,4.9],1,0.001)", "[t=3.34756002833,df=9,p=0.00855667212692,mean1=5.45,mean2=4.66,reject=false]");
    expect("pairedttest([10,12,9,11],[9,11,9,10])", "[t=3.0,df=3,p=0.0576688856224,mean=0.75,sx=0.5,reject=false]");
    expect("tinterval([5.1,4.9,5.6,5.8,6.0,5.3])", "[5.00600065986,5.89399934014]");
    expect("ztest([5.1,4.9,5.6,5.8,6.0,5.3],5,0.5)", "[z=2.2045407685,p=0.0274863361115,mean=5.45,reject=true]");
    expect("zinterval([5.1,4.9,5.6,5.8,6.0,5.3],0.5)", "[5.04992402697,5.85007597303]");
    expect("propinterval(52,100,0.9)", "[0.437823139136,0.602176860864]");
    expect("chi2gof([18,22,20,40],[1,1,1,1],0.001)", "[chi2=12.32,df=3,p=0.0063636299952,reject=false]");
    expect("chi2test([[10,20,30],[6,9,17]])", "[chi2=0.271574651504,df=2,p=0.87302828338,reject=false]");
    expect("anova([6,8,4,5,3,4],[8,12,9,11,6,8],0.01)", "[F=12.0,df1=1,df2=10,p=0.00608084792709,reject=true]");
    expect("[expcdf(0,1,2),invExp(1/2,2),expcdf(-inf,1.5,2)]", "[1-exp(-2),ln(2)/2,0.950212931632]");
    expect("[uniformpdf(5,0,4),uniformcdf(1,3,0,4),invUniform(1/4,2,6)]", "[0,1/2,3]");
    expect("[gammapdf(2,3,2),gammacdf(0,2,3,2)]", "[1/(4*e),1-5/(2*e)]");
    expect("[gammacdf(0,2,2.5,2),invGamma(0.5,3,2)]", "[0.150854963915,5.34812062745]");
    expect("[betapdf(1/2,2,3),betacdf(0,1/2,2,3),betacdf(0,0.3,2.5,3),invBeta(0.5,2,3)]", "[3/2,11/16,0.241237553921,0.385727568132]");
    expect("[weibullcdf(0,1,2,3),invWeibull(0.5,2,3)]", "[1-exp(-1/9),2.49766383347]");
    expect("[lognormpdf(2.0,0,1),lognormcdf(0,2,0,1),invLognorm(0.5,1,2)]", "[0.156874019279,0.755891404214,2.71828182846]");
    expect("[invBinom(0.5,10,0.3),invPoisson(0.9,4)]", "[3,7]");
    expect("exppdf(x,2)", "exppdf(x,2)");
    expect("polyreg([0,1,2,3,4],[1,2,5,11,16],2)", "[11*x^2/14+53*x/70+27/35,R2=2813/2835]");
    expect("expreg([1,2,3,4,5],[3,7,15,30,65])", "[1.46752938968*2.13973870156^x,R2=0.998903576409]");
    expect("powerreg([1,2,3,4,5],[2,5.6,10.4,16,22.4])", "[1.99232611711*x^1.50242660586,R2=0.99997680252]");
    expect("logreg([1,2,3,4,5],[1,2.4,3.1,3.8,4.2])", "[1.9889229234*ln(x)+0.995609585441,R2=0.998527279665]");
    expect("multireg([[1,2,3,4,5],[2,1,4,3,5]],[3,4,8,8,11])", "[4*x1/3+5*x2/6+3/10,R2=425/428]");
    expect("nlfit(a*sin(b*t),[a,b],[2,1],[0,1,2,3],[0,1.7,1.8,0.3],t)", "[1.99859609774*sin(0.998512768622*t),R2=0.999667150754]");
    expect("factormod(x^4-3*x^2+1,5)", "(x-2)^2*(x+2)^2");
    expect("factormod(x^11+x+1,65537)", "(x^2+x+1)*(x^9-x^8+x^6-x^5+x^3-x^2+1)");
    expect("factormod(x^9-x,3)", "x*(x-1)*(x+1)*(x^2+1)*(x^2+x-1)*(x^2-x-1)");
    expect("factormod(x^4+1,2)", "(x+1)^4");
    expect("factormod(x^8+x^4+x^3+x+1,2)", "x^8+x^4+x^3+x+1");
    expect("factormod((x+1)^5*(x^2+1)^3*(x+3),5)", "(x-2)^4*(x+1)^5*(x+2)^3");
    expect("factormod(x^2+1,4)", "ERROR 2: factormod: modulus must be prime");
    expect("[msolve(9*x=15,x,21),msolve(x^2+x+4=0,x,8),msolve(x^2=-1,x,7)]", "[[4,11,18],[3,4],[]]");
    /* sets, Gram-Schmidt (ntcmds.c); absorbing false / true in and / or (eval.c) */
    expect("union([a,b,b,c,c,c],[d,c,b],[b,e,b])", "[a,b,c,d,e]");
    expect("intersect([a,b,b,c,c,c],[d,c,b],[b,e,b])", "[b]");
    expect("setdiff([a,b,c,d,e],[b])", "[a,c,d,e]");
    expect("gramschmidt([[0,1,2,1],[0,1,3,1],[1,1,1,0],[1,3,6,2]])", "[[0,1,2,1],[0,-1/3,1/3,-1/3],[1,1/2,0,-1/2]]");
    expect("[x and 1>2,x or 1<2,x and 1<2]", "[false,true,x and true]");
    ev("rsolve(f(n)=f(n-1)+f(n-2),f(n),[f(1)=1,f(2)=2])");
    {
        static char in[600];
        snprintf(in, sizeof in, "approx(subst(%s,n,10))", out);
        expect(in, "89.0");
    }
}

/* round 2: exact definite integrals (integ.c def_split, def_special, eval_by_sign) */
static void test_gaps2(void) {
    cas_reset(ctx);
    /* |u| and sign(u) split at the exact zeros of u; square roots that hide |u| */
    expect("integrate(sqrt(x+1/x-2),x,0,1)", "4/3");
    expect("integrate(sqrt(x+1/x-2),x,0,2)", "8/3-2*sqrt(2)/3");
    expect("integrate(sqrt(2-2*cos(2*x))/2,x,-3*pi/4,-pi/4)", "sqrt(2)");
    expect("integrate(integrate(abs(y-x^2),y,0,2),x,-1,1)", "46/15");
    expect("integrate(abs(x^2-2),x,-1,2)", "8*sqrt(2)/3+1/3");
    expect("integrate(sign(x^3-x),x,-2,2)", "0");
    expect("integrate(sqrt(1-cos(x)),x,0,4*pi)", "8*sqrt(2)");
    expect("integrate(abs(x^3-2),x,0,2)", "3*2^(1/3)");
    /* no exact zero: stays numeric */
    expect("integrate(abs(x^5-x-1),x,0,2)", "9.52057234899");
    /* Gaussians with a parameter, Euler's constant, Dirichlet and Frullani */
    expect("assume(p>0)", "p>0");
    expect("integrate(x*exp(-p*x^2+2*q*x),x,-inf,inf)", "exp(q^2/p)*sqrt(pi)*q/p^(3/2)");
    expect("integrate(x^2*exp(-p*x^2),x,0,inf)", "sqrt(pi)/(4*p^(3/2))");
    expect("purge(p)", "true");
    expect("integrate(1/ln(t)+1/(1-t)-ln(ln(1/t)),t,0,1)", "2*euler_gamma");
    expect("integrate(sin(t)/t*exp(2*i*t),t,-inf,inf)", "0");
    expect("integrate((cos(t)-cos(3*t))/t,t,0,inf)", "ln(3)");
    expect("integrate(sin(2*t)*cos(t)/t,t,0,inf)", "pi/2");
    /* limits that depend on the sign of a parameter */
    expect("integrate(x/(x^2+y^2),y,-inf,inf)", "pi*sign(x)");
    /* radicals and algebraic simplification (cas/ext/adv/radsimp.c through the simplify hooks) */
    expect("simplify(sqrt(-2+sqrt(-5))*sqrt(-2-sqrt(-5)))", "3");
    expect("simplify((90+34*sqrt(7))^(1/3))", "sqrt(7)+3");
    expect("simplify(((135+78*sqrt(3))^(2/3)+3)*sqrt(3)/(135+78*sqrt(3))^(1/3))", "12");
    expect("simplify((41+29*sqrt(2))^(1/5))", "sqrt(2)+1");
    expect("simplify((7+5*sqrt(2))^(1/3))", "sqrt(2)+1");
    expect("simplify((7-5*sqrt(2))^(1/3))", "(7-5*sqrt(2))^(1/3)"); /* negative: the principal root is not real */
    expect("simplify((3+2*sqrt(2))^(1/3))", "(2*sqrt(2)+3)^(1/3)");
    expect("simplify((exp(x)-1)/(exp(x/2)+1))", "exp(x/2)-1");
    expect("simplify((x^3+(sqrt(2)-2)*x^2-(2*sqrt(2)+3)*x-3*sqrt(2))/(x^2-2))", "(x^2-2*x-3)/(x-sqrt(2))");
    expect("simplify(ln((2*sqrt(r)+1)/sqrt(4*r+4*sqrt(r)+1)))", "0");
    expect("simplify((4*r+4*sqrt(r)+1)^(sqrt(r)/(2*sqrt(r)+1))*(2*sqrt(r)+1)^(1/(2*sqrt(r)+1))-2*sqrt(r)-1)", "0");
    expect("simplify(sqrt((1+cos(6))/2))", "-cos(3)");
    expect("simplify(sqrt((1-cos(2))/2))", "sin(1)");
    /* not a square of a + b sqrt(r) with a > 0: left alone */
    expect("simplify(sqrt(r-2*sqrt(r)+1))", "sqrt(r-2*sqrt(r)+1)");
}

/* round 2: relational assumptions (assume.c) and residues (integ.c def_residue) */
static void test_gaps3(void) {
    cas_reset(ctx);
    expect("assume(x>-3*pi and x<-5*pi/2)", "x>-3*pi and x<-5*pi/2");
    expect("[abs(cos(x)),abs(sin(x))]", "[-cos(x),-sin(x)]");
    expect("abs(tan(x))", "tan(x)"); /* the pole -5 pi/2 is an excluded end */
    expect("about(x)", "[x<0,x+3*pi>0,-x-5*pi/2>0]");
    expect("unassume(x)", "true");
    expect("abs(cos(x))", "abs(cos(x))");
    expect("assume(z>-pi/2 and z<pi/2)", "z>-pi/2 and z<pi/2");
    expect("simplify(atan(tan(z)))", "z");
    expect("assume(z>-pi/2 and z<=pi/2)", "z>-pi/2 and z<=pi/2");
    expect("atan(tan(10))", "10-3*pi");
    expect("atan(tan(-2))", "pi-2");
    expect("x^4-x+1>0", "true");
    expect("x^2-2>0", "x^2-2>0");
    expect("x^2+1<0", "false");
    expect("assume(abs(w)<1)", "abs(w)<1");
    expect("-1<w and w<1", "true");
    expect("w>1", "false");
    expect("assume(p>q)", "p>q");
    expect("assume(q>0)", "q>0");
    expect("assume(k>0)", "k>0");
    expect("assume(n>0)", "n>0");
    expect("k*p^n>k*q^n", "true");
    expect("assume(u>1)", "u>1");
    expect("assume(v>=u-1)", "v>=u-1");
    expect("v>0", "true");
    expect("v<0", "false");
    expect("v>1", "v>1");
    /* the facts are saved with the session */
    sess_len = 0;
    CHECK(cas_save(ctx, sess_writer, NULL) == CAS_OK, "save with facts");
    cas_reset(ctx);
    expect("v>0", "v>0");
    CHECK(cas_restore(ctx, sess, sess_len) == CAS_OK, "restore facts: %s", cas_last_error(ctx));
    expect("v>0", "true");
    expect("purge(v)", "true");
    expect("v>0", "v>0");
    cas_reset(ctx);
    expect("assume(a>0)", "a>0");
    expect("integrate(cos(x)/(x^2+a^2),x,-inf,inf)", "exp(-a)*pi/a");
    expect("integrate(x*sin(x)/(x^2+1),x,-inf,inf)", "pi/e");
    expect("integrate(cos(3*x)/((x^2+1)*(x^2+4)),x,-inf,inf)", "pi*(2*exp(3)-1)/(6*exp(6))");
    cas_reset(ctx);
    /* transforms (ode.c): hyperbolic products, exponential shifts, repeated quadratics */
    expect("laplace(sinh(w*t)*cosh(w*t),t,s)", "-w/(4*w^2-s^2)");
    expect("laplace(sinh(t)^2,t,s)", "2/(s^3-4*s)");
    expect("ilaplace(s/(s^2+1)+4*(1/s-s/(s^2+1))*(exp(-s)-exp(-2*s)),s,t)",
           "cos(t)+(4*cos(t-2)-4)*heaviside(t-2)+(4-4*cos(t-1))*heaviside(t-1)");
    expect("ilaplace(2/(s^2+4)^2,s,t)", "-cos(2*t)*t/4+sin(2*t)/8");
    expect("ilaplace(s/(s^2+1)^2,s,t)", "sin(t)*t/2");
    expect("ilaplace(1/(s^2+2*s+5)^2,s,t)", "exp(-t)*(-cos(2*t)*t/8+sin(2*t)/16)");
    expect("laplace(ilaplace(1/(s^2+1)^3,s,t),t,s)", "1/(s^2+1)^3");
    /* sparse multivariate gcd: coprimality from modular images, no dense PRS (poly.c) */
    expect("gcd(24*x*y^19*z^8-47*x^17*y^5*z^8+6*x^15*y^9*z^2-3*x^22+5,"
           "34*x^5*y^8*z^13+20*x^7*y^7*z^7+12*x^9*y^16*z^4+80*y^14*z)", "1");
    expect("gcd(6*x*y+6,4*x*y+4)", "2*x*y+2");
    expect("gcd(x^2*y-y,x*y+y)", "x*y+y");
    expect("gcd(6*x^2*y^3,10*x*y^5)", "2*x*y^3");
}
