/*
 * unit_forms.inc -- tests of forms() (src/forms.c): alternate forms,
 * conditions of partial equivalence, ranking and the work budget
 * (included by unit_tests.c).
 */

/* the output of `in` contains `want` */
static void expect_has(const char *in, const char *want) {
    const char *got = ev(in);
    CHECK(strstr(got, want) != NULL, "%s -> %s (want it to contain %s)", in, got, want);
}

static void test_forms(void) {
    cas_reset(ctx);
    /* several forms, the simplest first */
    expect_has("forms(sin(2*x)*(1+tan(x)^2))", "[2*tan(x),");
    expect_has("forms(sin(2*x)*(1+tan(x)^2))", "sin(2*x)/cos(x)^2");
    expect_has("forms(sin(2*x)*(1+tan(x)^2))", "sin(2*x)*(tan(x)^2+1)");
    expect_has("forms(sin(2*x)*(1+tan(x)^2),1)", "[[2*tan(x),6,1,\"\"],");
    /* a cancelled common factor: equal except at x = 1 */
    expect_has("forms((x^2-1)/(x-1),1)", "[[x+1,3,1,\"x≠1\"],");
    expect_has("forms((x^2-1)/(x-1),1)", "[(x^2-1)/(x-1),11,0,\"\"]");
    expect_has("forms(x/(x^2+x),1)", "[1/(x+1),5,1,\"x≠0\"]");
    /* polynomials: expanded and factored */
    expect("forms(x^2-2*x+1)", "[(x-1)^2,x^2-2*x+1]");
    expect("forms((x+2)*(x-3))", "[(x-3)*(x+2),x^2-x-6]");
    expect_has("forms((x+y)^3)", "x^3+3*x^2*y+3*x*y^2+y^3");
    /* already minimal: itself */
    expect("forms(x+1)", "[x+1]");
    expect("forms(x)", "[x]");
    expect("forms(3)", "[3]");
    expect("forms([1,x])", "[[1,x]]");
    /* real vs complex: ln(x*y) = ln(x)+ln(y) only for x, y > 0 */
    expect("forms(ln(x*y),1)", "[[ln(x*y),6,1,\"\"],[ln(x)+ln(y),9,0,\"x>0, y>0\"]]");
    expect_has("forms(ln(x^2),1)", "[2*ln(x),6,1,\"x>0\"]");
    /* trig identities, hyperbolic via exponentials, partial fractions */
    expect_has("forms(sin(x)^4-cos(x)^4)", "[-cos(2*x),");
    expect_has("forms(cosh(x)^2-sinh(x)^2)", "[1,");
    expect_has("forms(1/(x-1)-1/(x+1))", "2/(x^2-1)");
    expect_has("forms((a+b)^2-(a-b)^2)", "[4*a*b,");
    /* a wrong rewriting never gets in: every listed form passed the check */
    expect_has("forms(sqrt(x^2),1)", "[[sqrt(x^2),");
    /* the budget: none gives the input alone, the default stays bounded */
    expect("forms(sin(2*x)*(1+tan(x)^2),0,0)", "[sin(2*x)*(tan(x)^2+1)]");
    expect("forms(x,1,2,3)", "ERROR 2: wrong number of arguments");
    {
        struct timeval t0, t1;
        gettimeofday(&t0, NULL);
        const char *r = ev("forms((a*x^2+b*x+c)^3/(x^2-1)^2+sin(3*x)*cos(2*x)^2)");
        gettimeofday(&t1, NULL);
        double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_usec - t0.tv_usec) / 1e3;
        CHECK(r[0] == '[' && ms < 400, "forms time box: %.1f ms, %s", ms, r);
        /* the budget does not leak: the next evaluation is unaffected */
        expect("expand((x+1)^2)", "x^2+2*x+1");
    }
}
