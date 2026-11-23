/*
 * unit_trigsolve.inc -- general solutions of periodic equations and solve on an interval
 * (cas/ext/alg/trigsolve.c, slot CASX_FN_SOLVE); included by unit_tests.c.
 */
#include "casx.h"

static void test_trigsolve(void) {
    cas_reset(ctx);
    /* families with the integer parameter n_1 */
    expect("solve(sin(x)=1/2,x)", "[2*n_1*pi+pi/6,2*n_1*pi+5*pi/6]");
    expect("about(n_1)", "integer");
    expect("solve(cos(x)=0,x)", "[n_1*pi+pi/2]");
    expect("solve(sin(x)=0,x)", "[n_1*pi]");
    expect("solve(cos(x)=-1,x)", "[2*n_1*pi+pi]");
    expect("solve(tan(x)=1,x)", "[n_1*pi+pi/4]");
    expect("solve(tan(x)^2=3,x)", "[n_1*pi-pi/3,n_1*pi+pi/3]");
    expect("solve(sin(x)=cos(x),x)", "[n_1*pi+pi/4]");
    expect("solve(sin(x)*cos(x)=0,x)", "[n_1*pi/2]");
    expect("solve(2*sin(x)^2-sin(x)-1=0,x)", "[2*n_1*pi/3-pi/6]");
    expect("solve(sin(x)+cos(x)=-1,x)", "[2*n_1*pi-pi/2,2*n_1*pi+pi]");
    expect("solve(sin(2*x+1)=1/3,x)", "[n_1*pi+asin(1/3)/2-1/2,n_1*pi-asin(1/3)/2+pi/2-1/2]");
    expect("solve(sin(x/2)=1,x)", "[4*n_1*pi+pi]");
    expect("solve(cos(3*x)=1,x)", "[2*n_1*pi/3]");
    expect("solve(sec(x)=2,x)", "[2*n_1*pi-pi/3,2*n_1*pi+pi/3]");
    expect("solve(csc(x)=2,x)", "[2*n_1*pi+pi/6,2*n_1*pi+5*pi/6]");
    expect("solve(cot(x)=1,x)", "[n_1*pi+pi/4]");
    expect("solve(sin(x)=2,x)", "[]");
    expect("solve(sin(x)=a,x)", "[2*n_1*pi+asin(a),2*n_1*pi-asin(a)+pi]");
    /* not periodic, inequalities, systems: as before */
    expect("solve(sin(x)=x,x)", "[0]");
    expect("solve(exp(x)=2,x)", "[ln(2)]");
    expect("solve([x+y=3,x-y=1],[x,y])", "[[x=2,y=1]]");
    /* n_1 taken: the next name */
    expect("solve(sin(x)=n_1,x)", "[2*n_2*pi+asin(n_1),2*n_2*pi-asin(n_1)+pi]");
    /* the parameter is an integer: members simplify */
    expect("subst(sin(x),x,2*n_1*pi+pi/6)", "1/2");
    expect("tan(n_1*pi+pi/4)", "1");
    /* a variable n is not touched */
    expect("solve(n^2=2,n)", "[-sqrt(2),sqrt(2)]");
    /* on an interval: sorted values */
    expect("solve(sin(x)=1/2,x,0,10)", "[pi/6,5*pi/6,13*pi/6,17*pi/6]");
    expect("solve(sin(x)*cos(x)=0,x,0,2*pi)", "[0,pi/2,pi,3*pi/2,2*pi]");
    expect("solve(tan(x)=1,x,-pi,pi)", "[-3*pi/4,pi/4]");
    expect("solve(sin(x)=1/2,x,10,0)", "[pi/6,5*pi/6,13*pi/6,17*pi/6]");
    expect("solve(x^2=4,x,0,5)", "[2]");
    expect("solve(x^2=4,x,-inf,inf)", "[-2,2]");
    expect("solve(sin(x)=1/2,x,0,inf)", "ERROR 2: solve: more than 100 solutions in the interval");
    expect("solve(sin(x)=a,x,0,1)", "ERROR 6: solve: the solutions are not numeric, no interval");
    expect("solve(sin(x)=1/2,x,1)", "ERROR 2: wrong number of arguments");
    expect("csolve(sin(x)=1/2,x,0,1)", "ERROR 2: wrong number of arguments");
    /* degree mode: period 360 */
    cas_settings s;
    cas_get_settings(ctx, &s);
    s.angle = CAS_ANGLE_DEG;
    cas_set_settings(ctx, &s);
    expect("solve(sin(x)=1/2,x)", "[360*n_1+30,360*n_1+150]");
    expect("solve(cos(x)=0,x,0,360)", "[90,270]");
    s.angle = CAS_ANGLE_RAD;
    cas_set_settings(ctx, &s);
    /* without the module's slot: the principal solutions, as before */
    casx_api.reg_fn(CASX_FN_SOLVE, NULL);
    expect("solve(sin(x)=1/2,x)", "[pi/6,5*pi/6]");
    expect("solve(cos(x)=0,x)", "[-pi/2,pi/2]");
    expect("solve(sin(x)=1/2,x,0,10)", "ERROR 6: solve on an interval needs the advanced CAS module");
    cas_ext_unload(); /* the linked-in extensions come back at the next use */
    expect("solve(cos(x)=0,x)", "[n_1*pi+pi/2]");
}
