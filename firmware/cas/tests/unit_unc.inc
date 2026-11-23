/*
 * unit_unc.inc -- tests of uncertainty propagation (src/unc.c; included by
 * unit_tests.c): the ± operator and pm(), first-order propagation through
 * arithmetic and functions, correlations (x-x = 0±0, x*x vs x*y), units,
 * the rounding of the printed form, nominal/uncert/ucov, re-parsing of the
 * output and the save/restore of correlated variables.
 *
 * Reference values by hand: for f(x) the uncertainty is |f'(x0)| s, for
 * independent x, y it is sqrt((df/dx sx)^2 + (df/dy sy)^2).
 */
static void test_unc(void) {
    cas_reset(ctx);
    /* input forms and the printed rounding (1 figure, 2 if the first is 1) */
    expect("9.81±0.02", "9.81±0.02");
    expect("pm(9.81,0.02)", "9.81±0.02");
    expect("2±1/10", "2.00±0.10");
    expect("12345.6±0.0012", "12345.6000±0.0012");
    expect("1000±230", "1000±200");
    expect("0.096±0.0961", "0.10±0.10");
    expect("6.02214076e23±1.2e20", "6.0221e23±1.2e20");
    expect("-3±1", "-3.0±1.0");
    expect("9.81±0.02*2", "9.81±0.04");     /* ± binds like + : 9.81±(0.02*2) */
    expect("2±0", "2±0");
    expect("pm(2,-1/10)", "2.00±0.10");      /* |s| */
    /* propagation */
    expect("x:=9.81±0.02", "9.81±0.02");
    expect("x+x", "19.62±0.04");
    expect("2*x", "19.62±0.04");
    expect("x*x", "96.2±0.4");                /* 2*9.81*0.02 = 0.3924 */
    expect("x-x", "0.0±0");                   /* correlated: exactly zero */
    expect("ans*2", "0.0±0");                 /* the 0±0 node is not kept in new results */
    expect("ans+1", "1.0±0");
    expect("x/x", "1±0");
    expect("y:=1±0.5", "1.0±0.5");
    expect("x+y", "10.8±0.5");
    expect("(1±0.01)/(1±0.01)", "1.000±0.014"); /* independent: sqrt(2)*0.01 */
    expect("sin(1±0.1)", "0.84±0.05");        /* cos(1)*0.1 = 0.054 */
    expect("sqrt(4±0.4)", "2.00±0.10");
    expect("exp(x)", "18200±400");
    expect("(2±0.1)^2+1", "5.0±0.4");
    /* exact inputs stay exact in nominal / uncert */
    expect("nominal(x^2)", "96.2361");
    expect("uncert(x^2)", "0.3924");
    expect("uncert(3*(2±1/10))", "3/10");
    expect("nominal(3*(2±1/10))", "6");
    expect("ucov(x,x)", "0.0004");
    expect("ucov(x,y)", "0");
    expect("uncert(5)", "0");
    /* units */
    expect("(2.0±0.1)_m/(0.50±0.02)_s", "(4.0±0.3)_m/s");
    expect("5_m±0.1_m", "(5.00±0.10)_m");
    expect("1_km±1_m", "(1000.0±1.0)_m");
    expect("uncert((2±0.1)_m*3)", "0.3_m");
    /* lists and symbolic values */
    expect("[1±0.1,2±0.2]", "[1.00±0.10,2.0±0.2]");
    expect("pm([1,2],0.5)", "[1.0±0.5,2.0±0.5]");
    expect("t±0.1", "t±0.1");
    expect("approx(2±1/10)", "2.00±0.10");
    /* output reads back (as a new, independent measurement) */
    expect("(4.0±0.3)_m/s*2_s", "(8.0±0.6)_m");
    expect("1.0e3±2e2", "1000±200");
    /* save / restore keeps the correlation */
    expect("z:=2±0.1", "2.00±0.10");
    sess_len = 0;
    CHECK(cas_save(ctx, sess_writer, NULL) == CAS_OK, "unc save");
    CHECK(strstr(sess, "_unc") != NULL && !strstr(sess, "\xc2\xb1"), "raw sources saved: %.200s", sess);
    cas_reset(ctx);
    CHECK(cas_restore(ctx, sess, sess_len) == CAS_OK, "unc restore: %s", cas_last_error(ctx));
    expect("z-z", "0±0");
    expect("z*z", "4.0±0.4");
    expect("z+(2±0.1)", "4.00±0.14");         /* a new source after restore */
    /* cas_save_prefix: hidden '_' variables (the RPN stack) with their sources,
     * restored correlated with a user variable saved by cas_save */
    cas_reset(ctx);
    expect("w:=3±0.2", "3.0±0.2");
    expect("_rpn1:=w*2", "6.0±0.4");
    expect("_rpn2:=1/3", "1/3");
    expect("_other:=5", "5");
    sess_len = 0;
    CHECK(cas_save_prefix(ctx, "_rpn", sess_writer, NULL) == CAS_OK, "save_prefix");
    sess[sess_len] = 0;
    CHECK(!strncmp(sess, "v _rpn1 ", 8) && strstr(sess, "_unc") && strstr(sess, "v _rpn2 1/3\n") &&
              !strstr(sess, "_other") && !strstr(sess, "v w ") && !strstr(sess, "CAS1"),
          "save_prefix records: %.200s", sess);
    char hid[256];
    snprintf(hid, sizeof hid, "%s", sess);
    size_t hid_len = sess_len;
    sess_len = 0;
    CHECK(cas_save(ctx, sess_writer, NULL) == CAS_OK && !strstr(sess, "_rpn"), "cas_save skips _ names");
    cas_reset(ctx);
    CHECK(cas_restore(ctx, sess, sess_len) == CAS_OK && cas_restore(ctx, hid, hid_len) == CAS_OK, "restore both");
    expect("_rpn1-2*w", "0±0");
    expect("_rpn2", "1/3");
    cas_reset(ctx);
}
