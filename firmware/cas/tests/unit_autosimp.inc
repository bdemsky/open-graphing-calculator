/*
 * unit_autosimp.inc -- the cheap autosimplify pass (simplify.c simplify_auto,
 * trig.c trig_pyth) and case-insensitive builtin names in the parser
 * (parse.c ci_builtin) (included by unit_tests.c).
 */
static void test_autosimp(void) {
    cas_reset(ctx);
    /* Pythagorean identities */
    expect("autosimplify(cos(x)^2+sin(x)^2)", "1");
    expect("autosimplify(3*cos(y)^2+3*sin(y)^2+z)", "z+3");
    expect("autosimplify(sec(x)^2-tan(x)^2)", "1");
    expect("autosimplify(cos(x)^4-sin(x)^4)", "2*cos(x)^2-1");
    expect("autosimplify(sin(x)^2/(1-cos(x)))", "cos(x)+1");
    expect("autosimplify((cos(x)-1)*(cos(x)+1))", "cos(x)^2-1");
    /* rational normal form when it is a clear win */
    expect("autosimplify((x^2-1)/(x-1))", "x+1");
    expect("autosimplify((x^3-8)/(x-2))", "x^2+2*x+4");
    expect("autosimplify(x*(x+1)-x^2)", "x");
    expect("autosimplify((a+b)^2-(a-b)^2)", "4*a*b");
    expect("autosimplify(1/(x-1)-1/(x+1))", "2/(x^2-1)");
    /* shapes are kept: factored and expanded polynomials, sums of simple
     * fractions, angle multiples, factored trig polynomials */
    expect("autosimplify((x-1)*(x+1))", "(x-1)*(x+1)");
    expect("autosimplify((x-1)*(x+1)+y)", "y+(x-1)*(x+1)");
    expect("autosimplify(x^3+3*x^2+3*x+1)", "x^3+3*x^2+3*x+1");
    expect("autosimplify((x+1)^3)", "(x+1)^3");
    expect("autosimplify(1/x+1/y)", "1/y+1/x");
    expect("autosimplify(2*sin(x)*cos(x))", "2*cos(x)*sin(x)");
    expect("autosimplify(sin(2*x))", "sin(2*x)");
    expect("autosimplify((cos(x)-1)*(cos(x)+2))", "(cos(x)-1)*(cos(x)+2)");
    expect("autosimplify(tan(x)^2+1)", "tan(x)^2+1");
    /* Euler pass: exp(i u) with exp(-i u) or a trig function of u (and e^u
     * with e^-u or sinh/cosh/tanh of u), only when strictly smaller */
    expect("autosimplify(e^(i*x)-(cos(x)+i*sin(x)))", "0");
    expect("autosimplify((e^(i*x)+e^(-i*x))/2)", "cos(x)");
    expect("autosimplify((e^(i*x)-e^(-i*x))/(2*i))", "sin(x)");
    expect("autosimplify(e^(2*i*t)+e^(-2*i*t))", "2*cos(2*t)");
    expect("autosimplify(e^(i*x)-i*sin(x))", "cos(x)");
    expect("autosimplify(tan(x)-(e^(i*x)-e^(-i*x))/(i*(e^(i*x)+e^(-i*x))))", "0");
    expect("autosimplify((e^x+e^-x)/2)", "cosh(x)");
    expect("autosimplify(e^x-e^-x)", "2*sinh(x)");
    expect("autosimplify(cosh(x)-e^x)", "-sinh(x)");
    expect("simplify((e^(i*x)+e^(-i*x))/2)", "cos(x)");
    /* the user's form stays: lone exponentials, lone cos + i sin, partners
     * of other arguments, larger results */
    expect("autosimplify(e^(i*x))", "exp(i*x)");
    expect("autosimplify(cos(x)+i*sin(x))", "cos(x)+i*sin(x)");
    expect("autosimplify(e^x)", "exp(x)");
    expect("autosimplify(cosh(x)+sinh(x))", "cosh(x)+sinh(x)");
    expect("autosimplify(e^(i*x)+e^(-i*x)+e^(i*y))", "2*cos(x)+exp(i*y)");
    expect("autosimplify(e^(i*x)+e^(-i*x)+cos(y)+i*sin(y))", "2*cos(x)+cos(y)+i*sin(y)");
    expect("autosimplify(e^(i*x)*cos(x))", "cos(x)*exp(i*x)");
    expect("autosimplify(e^(i*x)+cos(y))", "cos(y)+exp(i*x)");
    /* degree mode: no exp <-> circular trig (cos(x) would mean degrees) */
    {
        cas_settings s0, s1;
        cas_get_settings(ctx, &s0);
        s1 = s0;
        s1.angle = CAS_ANGLE_DEG;
        cas_set_settings(ctx, &s1);
        expect("autosimplify((e^(i*x)+e^(-i*x))/2)", "exp(i*x)/2+exp(-i*x)/2");
        expect("autosimplify((e^x+e^-x)/2)", "cosh(x)");
        cas_set_settings(ctx, &s0);
    }
    /* COMPLEX = REAL: the same rules (a result never gains an i) */
    real_mode(1);
    expect("autosimplify(e^(i*x)-(cos(x)+i*sin(x)))", "0");
    expect("autosimplify((e^(i*x)+e^(-i*x))/2)", "cos(x)");
    expect("autosimplify(e^(i*x))", "exp(i*x)");
    expect("autosimplify((e^x+e^-x)/2+sin(x))", "cosh(x)+sin(x)");
    real_mode(0);
    /* left alone: numbers, floats, big powers of sums */
    expect("autosimplify(5/3)", "5/3");
    expect("autosimplify(0.5*sin(x)^2+0.5*cos(x)^2)", "0.5*cos(x)^2+0.5*sin(x)^2");
    expect("autosimplify((x+1)^20-x)", "-x+(x+1)^20");
    /* lists, matrices and equations element-wise */
    expect("autosimplify([sin(x)^2+cos(x)^2,(x^2-1)/(x-1)])", "[1,x+1]");
    expect("autosimplify([[sin(x)^2+cos(x)^2,0],[0,1]])", "[[1,0],[0,1]]");
    expect("autosimplify(sin(x)^2+cos(x)^2=y)", "1=y");
    /* updates ans like any command */
    expect("cos(t)^2+sin(t)^2", "cos(t)^2+sin(t)^2");
    expect("autosimplify(ans)", "1");
    expect("ans+1", "2");
    expect("autosimplify(1,2)", "ERROR 2: wrong number of arguments");
}

static void test_case_names(void) {
    cas_reset(ctx);
    /* functions and commands in any case */
    expect("COS(x)^2+SIN(x)^2", "cos(x)^2+sin(x)^2");
    expect("simplify(COS(x)^2+Sin(x)^2)", "1");
    expect("LN(1)+Log10(100)+SQRT(4)+ABS(-3)", "7");
    expect("SIN (pi/2)", "1");
    expect("NCR(5,2)", "10");
    expect("FACTOR(x^2-1)", "(x-1)*(x+1)");
    expect("Factorial(4)", "24");
    expect("SI(0)", "0");
    expect("ROWSWAP([[1,2],[3,4]],1,2)", "[[3,4],[1,2]]");
    expect("AutoSimplify(cos(x)^2+sin(x)^2)", "1");
    /* constants */
    expect("PI", "pi");
    expect("2*Pi", "2*pi");
    expect("-INF", "-inf");
    expect("7", "7");
    expect("ANS+1", "8");
    /* e, i, E, I: E and I stay variables (e and i are the constants) */
    expect("E+I", "E+I");
    expect("e^0+i^2", "0");
    /* variables stay case-sensitive */
    expect("X+x", "x+X");
    /* exact spellings of the user win: definitions and values */
    expect("COS(x):=x^2", "x^2");
    expect("COS(3)", "9");
    expect("cos(0)", "1");
    expect("PI:=3", "3");
    expect("PI+1", "4");
    expect("Pi", "pi");
    expect("Ln:=5", "5");
    expect("Ln(2)", "10");
    expect("purge(COS)", "true");
    expect("COS(0)", "1");
    /* unknown names stay undefined functions */
    expect("FOO(1)", "FOO(1)");
}
