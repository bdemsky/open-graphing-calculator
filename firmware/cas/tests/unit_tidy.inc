/*
 * unit_tidy.inc -- textbook forms of antiderivatives (cas/ext/int/tidyrad.c, tidytrig.c: the
 * casx cleanup after the core's search); included by unit_tests.c.
 */

static void test_tidy_forms(void) {
    cas_reset(ctx);
    /* Euler substitutions rationalised (t = x + sqrt(Q), (sqrt(Q) - sqrt(c))/x) */
    expect("integrate(x/sqrt(x^2+1),x)", "sqrt(x^2+1)");
    expect("integrate(x*sqrt(x^2+1),x)", "(x^2+1)^(3/2)/3");
    expect("integrate(x/sqrt(x^2+2*x+2),x)", "sqrt(x^2+2*x+2)-ln(x+sqrt(x^2+2*x+2)+1)");
    expect("integrate((3*x-1)/sqrt(x^2+1),x)", "3*sqrt(x^2+1)-ln(x+sqrt(x^2+1))");
    expect("integrate(ln(x+sqrt(x^2+1)),x)", "ln(x+sqrt(x^2+1))*x-sqrt(x^2+1)");
    expect("integrate(x^2/sqrt(x^2+1),x)", "x*sqrt(x^2+1)/2-ln(x+sqrt(x^2+1))/2");
    expect("integrate(1/(x^2*sqrt(x^2+1)),x)", "-sqrt(x^2+1)/x");
    expect("integrate(1/(x^2+1)^(3/2),x)", "x/sqrt(x^2+1)");
    /* two radicands; a radicand in cos(x) */
    expect("integrate(1/(sqrt(x)+sqrt(1+x)),x)", "-2*x^(3/2)/3+sqrt(x+1)*(2*x/3+2/3)");
    expect("integrate(atan(sqrt(1+x)-sqrt(x)),x)", "atan(-sqrt(x)+sqrt(x+1))*x+sqrt(x)/2-atan(sqrt(x)+sqrt(x+1))");
    expect("integrate(sin(2*x)*sqrt(1+3*cos(x)^2),x)", "-2*(3*cos(x)^2+1)^(3/2)/9");
    /* k/sqrt(Q) terms as the core's asin form; radicands without square contents */
    expect("integrate(sqrt(9-x^2)/x^2,x)", "-asin(x/3)-sqrt(-x^2+9)/x");
    expect("integrate(x^2/(1-x^2)^(3/2),x)", "x/sqrt(-x^2+1)-asin(x)");
    expect("integrate(x^2/sqrt(4-x^2),x)", "-x*sqrt(-x^2+4)/2+2*asin(x/2)");
    expect("integrate(x*sqrt(4-x^2),x)", "-(-x^2+4)^(3/2)/3");
    /* logarithms: constant factors dropped, conjugates and quotients merged */
    expect("integrate(1/sqrt(x^2+4*x+13),x)", "ln(x+sqrt(x^2+4*x+13)+2)");
    expect("integrate(sqrt(x^2+2*x+5),x)", "2*ln(x+sqrt(x^2+2*x+5)+1)+(x+1)*sqrt(x^2+2*x+5)/2");
    expect("integrate(1/(x*(x^4+1)),x)", "ln(x)-ln(x^4+1)/4");
    expect("integrate(1/(x^4-5*x^2+6),x)", "sqrt(3)*(ln(x-sqrt(3))-ln(x+sqrt(3)))/6-sqrt(2)*(ln(x-sqrt(2))-ln(x+sqrt(2)))/4");
    expect("integrate(1/(x^2-1),x)", "ln(x-1)/2-ln(x+1)/2");
    expect("integrate(1/((x+1)*(x+2)*(x+3)),x)", "ln(x+1)/2-ln(x+2)+ln(x+3)/2");
    /* expanded denominators factored */
    expect("integrate(x^2/(x+1)^10,x)", "1/(4*(x+1)^8)-1/(9*(x+1)^9)-1/(7*(x+1)^7)");
    /* trigonometric powers (also from the half-angle substitution) */
    expect("integrate(sin(x)^3,x)", "cos(x)^3/3-cos(x)");
    expect("integrate(sin(x)^5,x)", "2*cos(x)^3/3-cos(x)-cos(x)^5/5");
    expect("integrate(sin(x)^3*cos(x)^3,x)", "sin(x)^4/4-sin(x)^6/6");
    expect("integrate(sin(x)^2,x)", "x/2-sin(2*x)/4");
    expect("integrate(sec(x)^2*tan(x)^4,x)", "tan(x)^5/5");
    expect("integrate(sec(x)^4*tan(x)^2,x)", "tan(x)^3/3+tan(x)^5/5");
    expect("integrate(tan(x)^3*sec(x)^4,x)", "tan(x)^4/4+tan(x)^6/6");
    expect("integrate(sec(x)^6*tan(x)^3,x)", "1/(8*cos(x)^8)-1/(6*cos(x)^6)");
    expect("integrate(sec(x)^8,x)", "tan(x)+tan(x)^3+3*tan(x)^5/5+tan(x)^7/7");
    expect("integrate(1/(1-sin(x)),x)", "(sin(x)+1)/cos(x)");
    expect("integrate(1/(1-cos(x)),x)", "-1/tan(x/2)");
    expect("integrate(sec(x)^3,x)", "ln(cos(x)/(sin(x)-1))/2+sin(x)/(2*cos(x)^2)");
    /* logarithms of trigonometric and hyperbolic functions combined */
    expect("integrate(1/(sin(x)*cos(x)),x)", "ln(tan(x))");
    expect("integrate((1-sin(x))/cos(x),x)", "ln(sin(x)+1)");
    expect("integrate(cos(x)^2*tan(x)^3,x)", "-ln(cos(x))-sin(x)^2/2");
    expect("integrate(tan(x)^3,x)", "ln(cos(x))+tan(x)^2/2");
    expect("integrate(tanh(x)^2,x)", "x-tanh(x)");
    expect("integrate(tanh(x)^3,x)", "ln(cosh(x))-tanh(x)^2/2");
    expect("integrate(1/tanh(x),x)", "ln(sinh(x))");
    /* real forms: complex exponentials, exponential pairs of hyperbolic integrands */
    expect("integrate(sinh(x)*sin(x),x)", "cosh(x)*sin(x)/2-cos(x)*sinh(x)/2");
    expect("integrate(cosh(x)^3,x)", "3*sinh(x)/4+sinh(3*x)/12");
    expect("integrate(acos(x)^2/x^5,x)",
           "ln(x)/3+(acos(x)*x^3*sqrt(-x^2+1)/3-x^2/12+acos(x)*x*sqrt(-x^2+1)/6-acos(x)^2/4)/x^4");
    /* ln(c u) -> ln(u) only where the logarithm has a constant coefficient */
    expect("integrate(cos(x)*ln(sin(x)/2),x)", "ln(sin(x)/2)*sin(x)-sin(x)");
    expect_antideriv("x^3/sqrt(x^2-4*x+5)");
    expect_antideriv("1/((x+1)*sqrt(x^2+x+1))");
    expect_antideriv("sec(x)^5*tan(x)^3");
    expect_antideriv("1/(2+sin(x))^2");
    expect_antideriv("sinh(x)^3*cosh(x)^2");
}
