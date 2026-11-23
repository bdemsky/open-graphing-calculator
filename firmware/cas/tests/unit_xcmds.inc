/*
 * unit_xcmds.inc -- tests of units/constants, base-n, matrix extensions and
 * the commands in src/xcmds.c (included by unit_tests.c).
 */
static void test_xcmds(void) {
    cas_reset(ctx);
    /* units */
    expect("9.8_m/s^2*3_s", "29.4_m/s");
    expect("convert(5_ft,_m)", "1.524_m");
    expect("1_m+1_ft", "1.3048_m");
    expect("2_m+3_m", "5_m");
    expect("5_km*2_m", "10000_m^2");
    expect("5_m/(2_ft)", "8.20209973753");
    expect("1_m+2_s", "ERROR 2: incompatible units");
    expect("1_m+1", "ERROR 2: incompatible units");
    expect("sin(1_m)", "ERROR 2: incompatible units");
    expect("sin(30_deg)", "1/2");
    expect("sqrt(4_m^2)", "2_m");
    expect("(2_m)^2", "4_m^2");
    expect("3_Hz*2_s", "6");
    expect("convert(20_degC,_degF)", "68_degF");
    expect("convert(68_degF,_degC)", "20_degC");
    expect("convert(300_K,_degC)", "26.85_degC");
    expect("deltaconvert(10_degC,_degF)", "18_degF");
    expect("20_degC+5_K", "ERROR 2: incompatible units: use convert for temperatures");
    expect("convert(1_atm,_psi)", "14.6959487755_psi");
    expect("convert(100_km/h,_mph)", "62.1371192237_mph");
    expect("usimplify(3_kg*2_m/_s^2)", "6_N");
    expect("usimplify(3_mA*2_kohm)", "6_V");
    expect("convert(2_N*3_m,\"SI\")", "6_J");
    expect("convert(3_m^2,\"US\")", "32.2917312501_ft^2");
    expect("convert([1_m,2_ft],_in)", "[39.3700787402_in,24_in]");
    expect("convert(1_s,_m)", "ERROR 2: incompatible units");
    expect("uvalue(3_m/s)", "3");
    expect("uunits(3_m/s)", "_m/s");
    expect("solve(x*2_m=6_m,x)", "[3]");
    expect("_kg*m^2/s^2", "_kg*m^2/s^2");
    expect("-2_m/s", "-2_m/s");
    expect("2_m^-1", "2/_m");
    expect("1/(2_s)", "1/(2*_s)");
    /* constants: symbolic when exact, values when approximate */
    expect("_me*_c^2", "_c^2*_me");
    expect("approx(_me*_c^2)", "8.18710578797e-14_kg*m^2/s^2");
    expect("convert(_me*_c^2,_MeV)", "0.510998950692_MeV");
    expect("convert(_c,_km/h)", "1079252848.8_km/h");
    expect("convert(_kB*300_K,_meV)", "25.8519997864_meV");
    expect("convert(_R,_J/mol/K)", "8.31446261815_J/K/mol");
    expect("2.0*_c", "599584916.0_m/s");
    /* output re-parses */
    expect("29.4_m/s*2_s", "58.8_m");
    /* base-n */
    expect("0x1F", "31");
    expect("0b1011+1", "12");
    expect("0o17", "15");
    expect("hex(255)", "\"0xFF\"");
    expect("bin(10)", "\"0b1010\"");
    expect("oct(-1)", "\"0o1777777777777777777777\"");
    expect("5 and 3", "1");
    expect("5 or 3", "7");
    expect("not 5", "-6");
    expect("x>0 and x<1", "x>0 and x<1");
    expect("true and false", "false");
    expect("bitxor(12,10)", "6");
    expect("shl(1,4)", "16");
    expect("rotr(1,1)", "-9223372036854775808");
    expect("wordsize(8,false)", "[8,false]");
    expect("not 5", "250");
    expect("rotr(1,1)", "128");
    expect("shl(255,1)", "254");
    expect("wordsize(64,true)", "[64,true]");
    /* matrices */
    expect("dim([[1,2,3],[4,5,6]])", "[2,3]");
    expect("ref([[1,2,3],[4,5,6]])", "[[1,2,3],[0,1,2]]");
    expect("rowSwap([[1,2],[3,4]],1,2)", "[[3,4],[1,2]]");
    expect("rowAdd([[1,2],[3,4]],1,2)", "[[1,2],[4,6]]");
    expect("mRow(3,[[1,2],[3,4]],2)", "[[1,2],[9,12]]");
    expect("mRowAdd(-3,[[1,2],[3,4]],1,2)", "[[1,2],[0,-2]]");
    expect("augment([[1,2],[3,4]],[5,6])", "[[1,2,5],[3,4,6]]");
    expect("cumSum([[1,2],[3,4]])", "[[1,2],[4,6]]");
    expect("cond([[1,2],[3,4]])", "21");
    expect("cond([[1,2],[2,4]])", "inf");
    expect("fill(7,[[1,2],[3,4]])", "[[7,7],[7,7]]");
    expect("setElem([[1,2],[3,4]],2,1,9)", "[[1,2],[9,4]]");
    expect("redim([[1,2],[3,4]],3,1)", "[[1],[3],[0]]");
    expect("rowSwap([[1,2]],1,2)", "ERROR 2: row index out of range");
    expect("dim(randMat(2,3))", "[2,3]");
    /* fixed / implemented builtins */
    expect("hessian(x^2*y^3,[x,y])", "[[2*y^3,6*x*y^2],[6*x*y^2,6*x^2*y]]");
    expect("expexpand(exp(x+y))", "exp(x)*exp(y)");
    expect("expexpand((exp(x)+1)^2)", "2*exp(x)+exp(2*x)+1");
    expect("digits(1234)", "[1,2,3,4]");
    expect("digits(255,16)", "[15,15]");
    expect("pade(exp(x),x,2,2)", "(x^2/12+x/2+1)/(x^2/12-x/2+1)");
    expect("fourier_an(x^2,x,2*pi,0,-pi)", "pi^2/3");
    expect("fourier_an(x^2,x,2*pi,n,-pi)", "4*(-1)^n/n^2");
    expect("fourier_bn(x,x,2*pi,n,-pi)", "-2*(-1)^n/n");
    expect("egcd(x^2-1,x+3)", "[1/8,-x/8+3/8,1]");
    expect("egcd(12,18)", "[-1,1,6]");
    expect("degree(randpoly(x,4),x)", "4");
    expect("piecewise(x<0,-x,x)", "piecewise(x<0,-x,x)");
    expect("piecewise(3<0,-3,3)", "3");
    expect("piecewise(3<0,-3)", "undef");
    expect("subst(piecewise(x<0,-x,x^2),x,-2)", "2");
    expect("a:=5", "5");
    expect("restart", "true");
    expect("a", "a");
    expect("quo(-17,5)", "-4");
    expect("rem(-17,5)", "3");
    expect("iquo(-17,5)", "-3");
    expect("irem(-17,5)", "-2");
    cas_reset(ctx);
}
