/*
 * unit_nabla.inc -- the del operator (parse.c p_nabla, cmds.c nabla_vars): ∇f, ∇·F, ∇×F, ∇²f,
 * the ASCII spelling nabla, inferred and explicit variables, precedence and errors; included by
 * unit_tests.c.
 */

static void test_nabla(void) {
    cas_reset(ctx);
    /* the four operators, with ∇ and with nabla */
    expect("∇(x^2*y)", "[2*x*y,x^2]");
    expect("∇·[x,y,z]", "3");
    expect("∇×[y,-x,0]", "[0,0,-2]");
    expect("∇²(x^2*y+z^3)", "2*y+6*z");
    expect("nabla(x*y)", "[y,x]");
    expect("nabla.[x^2,x*y]", "3*x");
    expect("nabla·[x,y]", "2");
    expect("nabla×[-y,x]", "2");
    expect("nabla^2 x^3", "6*x");
    expect("nabla²(x^2+y^2)", "4");
    expect("∇.[x,y]", "2");
    expect("∇^2(x^2+y^2)", "4");
    expect("∇ · [x,y]", "2");
    expect("∇ (x*y)", "[y,x]");
    /* the one-argument commands the notation stands for */
    expect("grad(x*y)", "[y,x]");
    expect("divergence([x*y,y*z,z*x])", "x+y+z");
    expect("curl([y*z,x*z,x*y])", "[0,0,0]");
    expect("laplacian(x^2*y^2)", "2*(x^2+y^2)");
    /* inferred variables: a scalar up to the last of x, y, z in it; a field by its size */
    expect("∇x^2", "[2*x]");
    expect("∇(x^2*y)", "[2*x*y,x^2]");
    expect("∇(x*z)", "[z,0,x]");
    expect("∇5", "[0]");
    expect("∇·[x^2]", "2*x");
    expect("∇·[x,y,z]", "3");
    expect("∇×[-y,x]", "2");
    expect("∇²[x^2,y^2,z^2]", "[2,2,2]");
    expect("∇·[1,2,3,4]", "ERROR 2: nabla: give the variables");
    /* explicit variables */
    expect("∇_[r,t](r^2*t)", "[2*r*t,r^2]");
    expect("∇_[r,t]·[r^2,t]", "2*r+1");
    expect("∇·_[r,t][r^2,t]", "2*r+1");
    expect("∇_[x,y,z]x", "[1,0,0]");
    expect("∇(r^2*cos(t),[r,t],polar)", "[2*cos(t)*r,-r*sin(t)]");
    expect("∇·([r^2,0,0],[r,t,p],spherical)", "4*r");
    expect("∇_[r,t](r,t)", "ERROR 1: variables given twice");
    /* precedence: the following factor (a unary, like sin x) */
    expect("∇x*y", "[y]");
    expect("∇(x*y)*2", "[2*y,2*x]");
    expect("2∇(x*y)", "[2*y,2*x]");
    expect("-∇(x*y)", "[-y,-x]");
    expect("∇x^2*y", "[2*x*y]");
    expect("∇·[x,y]^2", "2*x+2*y");
    expect("∇·∇(x^2+y^2)", "4");
    expect("∇×∇(x*y*z)", "[0,0,0]");
    expect("∇·∇×[x*y,y*z,z*x]", "0");
    /* · and × elsewhere still multiply */
    expect("x·y", "x*y");
    expect("2×3", "6");
    expect("[1,2]·3", "[3,6]");
    /* a stored field */
    expect("f:=x^2+y^2+z^2", "x^2+y^2+z^2");
    expect("∇f", "[2*x,2*y,2*z]");
    expect("∇²f", "6");
    expect("h(x,y):=x^2*y", "x^2*y");
    expect("∇h(x,y)", "[2*x*y,x^2]");
    /* errors */
    expect("∇", "ERROR 1: unexpected end of input");
    expect("nabla", "ERROR 1: unexpected end of input");
    expect("∇·", "ERROR 1: unexpected end of input");
    expect("∇()", "ERROR 1: syntax error");
    expect("∇_[x", "ERROR 1: missing ']'");
    cas_reset(ctx);
}
