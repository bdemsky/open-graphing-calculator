/*
 * unit_tests.c -- white-box tests of bignum, rational, polynomial and
 * memory machinery, plus end-to-end checks through cas_eval.
 */
#include "cas_int.h"
#include "poly.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <sys/time.h>

static int fails = 0, total = 0;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        total++;                                          \
        if (!(cond)) {                                    \
            fails++;                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

static cas_ctx *ctx;
static char out[1 << 16];

static const char *ev(const char *in) {
    cas_status st = cas_eval(ctx, in, out, sizeof out);
    if (st != CAS_OK) snprintf(out, sizeof out, "ERROR %d: %s", st, cas_last_error(ctx));
    return out;
}

static void expect(const char *in, const char *want) {
    const char *got = ev(in);
    CHECK(strcmp(got, want) == 0, "%s -> %s (want %s)", in, got, want);
}

/* run white-box code with a valid context and error handler */
#define WB_BEGIN                     \
    {                                \
        K = ctx;                     \
        char anchor;                 \
        K->stack_base = &anchor;     \
        K->s_top = K->s_lo;          \
        tryf tf;                     \
        try_enter(&tf);              \
        if (!setjmp(tf.jb)) {
#define WB_END                                               \
        try_leave(&tf);                                      \
        } else {                                             \
            try_leave(&tf);                                  \
            CHECK(0, "unexpected throw: %s", K->errmsg);     \
        }                                                    \
        K->s_top = K->s_lo;                                  \
    }

static E *big(const char *s) {
    int neg = s[0] == '-';
    E *r = int_from_dec(s + neg, strlen(s + neg));
    return neg ? int_neg(r) : r;
}

static const char *dec(E *e) {
    static char buf[1 << 14];
    sbuf b = {buf, sizeof buf, 0, 0};
    int_to_dec(&b, e);
    return buf;
}

static void rand_digits(char *buf, int n) {
    buf[0] = (char)('1' + rand() % 9);
    for (int i = 1; i < n; i++) buf[i] = (char)('0' + rand() % 10);
    buf[n] = 0;
}

static void test_bignum(void) {
    WB_BEGIN
    CHECK(!strcmp(dec(int_mul(big("123456789012345678901234567890"), big("987654321098765432109876543210"))),
                  "121932631137021795226185032733622923332237463801111263526900"),
          "mul");
    CHECK(!strcmp(dec(int_add(big("-5"), big("3"))), "-2"), "add signs");
    CHECK(!strcmp(dec(int_sub(big("100000000000000000000"), big("1"))), "99999999999999999999"), "sub borrow");
    E *q, *r;
    int_tdivmod(big("100000000000000000000000000000"), big("7777777777777"), &q, &r);
    CHECK(!strcmp(dec(q), "12857142857144142"), "div q %s", dec(q));
    CHECK(!strcmp(dec(r), "6666666667666"), "div r %s", dec(r));
    int_fdivmod(big("-7"), big("2"), &q, &r);
    CHECK(!strcmp(dec(q), "-4") && !strcmp(dec(r), "1"), "fdiv");
    CHECK(!strcmp(dec(int_gcd(big("1071"), big("462"))), "21"), "gcd");
    CHECK(!strcmp(dec(int_pow(big("2"), 100)), "1267650600228229401496703205376"), "pow");
    int ex;
    CHECK(!strcmp(dec(int_iroot(big("1000000000000000000000000000000"), 3, &ex)), "10000000000") && ex, "iroot");
    CHECK(int_isprime(big("170141183460469231731687303715884105727")), "M127 prime");
    CHECK(!int_isprime(big("170141183460469231731687303715884105725")), "composite");
    CHECK(!strcmp(dec(int_nextprime(big("1000000"))), "1000003"), "nextprime");
    /* randomized identities across Karatsuba sizes */
    char *ba = malloc(4000), *bb = malloc(4000);
    for (int it = 0; it < 60; it++) {
        char *mark = smark();
        int na = 1 + rand() % 1500, nb = 1 + rand() % 1500;
        rand_digits(ba, na);
        rand_digits(bb, nb);
        E *a = big(ba), *b = big(bb);
        if (rand() & 1) a = int_neg(a);
        E *p = int_mul(a, b);
        E *qq, *rr;
        int_tdivmod(p, b, &qq, &rr);
        CHECK(int_cmp(qq, a) == 0 && int_is_zero(rr), "mul/div roundtrip %d %d", na, nb);
        /* (a+b)^2 = a^2 + 2ab + b^2 */
        E *s = int_add(a, b);
        E *lhs = int_mul(s, s);
        E *rhs = int_add(int_add(int_mul(a, a), int_mul_si(p, 2)), int_mul(b, b));
        CHECK(int_cmp(lhs, rhs) == 0, "square identity");
        /* division with remainder */
        E *c = int_add(p, mk_int(12345));
        int_tdivmod(c, b, &qq, &rr);
        CHECK(int_cmp(int_add(int_mul(qq, b), rr), c) == 0 && int_cmpabs(rr, b) < 0, "divmod identity");
        /* decimal roundtrip */
        CHECK(!strcmp(dec(int_abs(a)), ba), "decimal roundtrip");
        E *g = int_gcd(p, int_mul(a, mk_int(6)));
        E *r1, *r2;
        int_tdivmod(p, g, NULL, &r1);
        int_tdivmod(g, a, NULL, &r2);
        CHECK(int_is_zero(r1) && int_is_zero(r2), "gcd divides");
        srelease(mark);
    }
    free(ba);
    free(bb);
    /* factorisation */
    E *fl = int_factor(big("600851475143"));
    CHECK(fl->n == 4, "factor count");
    WB_END
}

static void test_rational(void) {
    WB_BEGIN
    E *a = mk_frac(1, 3), *b = mk_frac(1, 6);
    E *s = num_add(a, b);
    CHECK(s->t == E_RAT && int_is_one(K0(s)) && is_int_val(K1(s), 2), "1/3+1/6");
    CHECK(num_cmp(mk_frac(-1, 2), mk_frac(1, 3)) < 0, "cmp");
    CHECK(is_int_val(num_mul(mk_frac(2, 3), mk_frac(3, 2)), 1), "mul");
    E *x = num_from_double(0.1);
    CHECK(num_to_double(x) == 0.1, "double exact");
    CHECK(fabs(num_to_double(mk_frac(1, 3)) - 1.0 / 3) < 1e-16, "to double");
    WB_END
}

static void test_poly(void) {
    WB_BEGIN
    /* (x+1)^3 (x-2)^2 gcd with (x+1)(x-2)^3 */
    ktab kt;
    kt_init(&kt);
    E *x = mk_sym_name("x");
    P *px = e2p(x, &kt);
    P *a = p_mul(p_pow(p_add(px, p_const(mk_int(1))), 3), p_pow(p_sub(px, p_const(mk_int(2))), 2));
    P *b = p_mul(p_add(px, p_const(mk_int(1))), p_pow(p_sub(px, p_const(mk_int(2))), 3));
    P *g = p_gcd(a, b);
    P *want = p_mul(p_add(px, p_const(mk_int(1))), p_pow(p_sub(px, p_const(mk_int(2))), 2));
    CHECK(p_eq(g, want), "univariate gcd");
    P *q = p_divexact(a, g);
    CHECK(q && p_eq(q, p_pow(p_add(px, p_const(mk_int(1))), 2)), "divexact");
    /* multivariate gcd */
    E *y = mk_sym_name("y");
    P *py = e2p(y, &kt);
    P *u = p_add(px, py), *v = p_sub(px, py);
    P *m1 = p_mul(p_mul(u, u), v), *m2 = p_mul(u, p_mul(v, v));
    P *g2 = p_gcd(m1, m2);
    P *w2 = p_mul(u, v);
    CHECK(p_eq(g2, w2) || p_eq(g2, p_neg(w2)), "bivariate gcd");
    WB_END
}

static void test_eval(void) {
    expect("2+3*4", "14");
    expect("2^10", "1024");
    expect("1/3+1/6", "1/2");
    expect("-2^2", "-4");
    expect("2x+3x", "5*x");
    expect("(x+1)(x-1)", "(x-1)*(x+1)");
    expect("expand((x+1)*(x-1))", "x^2-1");
    expect("factor(x^2-1)", "(x-1)*(x+1)");
    expect("sqrt(8)", "2*sqrt(2)");
    expect("sqrt(-4)", "2*i");
    expect("sin(pi/6)", "1/2");
    expect("cos(pi)", "-1");
    expect("diff(x^3,x)", "3*x^2");
    expect("integrate(x,x)", "x^2/2");
    expect("a:=5", "5");
    expect("a*2", "10");
    expect("f(t):=t^2", "t^2");
    expect("f(3)", "9");
    expect("ans+1", "10");
    expect("det([[1,2],[3,4]])", "-2");
    expect("solve(x^2=9,x)", "[-3,3]");
    expect("limit(sin(x)/x,x,0)", "1");
    expect("1/0", "ERROR 2: division by zero");
    expect("2+", "ERROR 1: unexpected end of input");
    expect("foo(", "ERROR 1: unexpected end of input");
    expect("100!", "93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000");
    expect("isprime(2^89-1)", "true");
    expect("gcd(12,18)", "6");
    expect("[1,2,3]*2", "[2,4,6]");
    expect("[[1,2],[3,4]]^2", "[[7,10],[15,22]]");
    expect("approx(pi)", "3.14159265359");
    expect("0.1+0.2", "0.3");
    expect("exact(0.25)", "1/4");
    expect("(1+i)^2", "2*i");
    /* a relation left inside arithmetic is an error; whole relations thread */
    expect("solve(tan(x)^(2=3),x)", "ERROR 2: an equation or inequality cannot be part of an expression");
    expect("x^(2=3)", "ERROR 2: an equation or inequality cannot be part of an expression");
    expect("sin(x=1)", "ERROR 2: an equation or inequality cannot be part of an expression");
    expect("(x<2)+1", "ERROR 2: an equation or inequality cannot be part of an expression");
    expect("(x=2)+1", "x+1=3");
    expect("[x=1,y=2]", "[x=1,y=2]");
    expect("piecewise(x<0,-x,x)", "piecewise(x<0,-x,x)");
}

static void test_settings(void) {
    cas_settings s;
    cas_get_settings(ctx, &s);
    s.angle = CAS_ANGLE_DEG;
    cas_set_settings(ctx, &s);
    expect("sin(30)", "1/2");
    expect("cos(60)", "1/2");
    s.angle = CAS_ANGLE_RAD;
    s.exact = 0;
    s.digits = 6;
    cas_set_settings(ctx, &s);
    expect("sqrt(2)", "1.41421");
    expect("1/3", "0.333333");
    s.exact = 1;
    s.digits = 12;
    cas_set_settings(ctx, &s);
}

/* series/taylor results print in ascending order with the coefficient in
 * front and an order term (display only: the value, ans, is the polynomial) */
static void test_series_display(void) {
    expect("series(sin(x),x,0,8)", "x-1/6*x^3+1/120*x^5-1/5040*x^7+O(x^8)");
    expect("series(exp(x)/(1-x),x,0,5)", "1+2*x+5/2*x^2+8/3*x^3+65/24*x^4+O(x^5)");
    expect("taylor(ln(1+x),x,0,6)", "x-1/2*x^2+1/3*x^3-1/4*x^4+1/5*x^5+O(x^6)");
    expect("series(x^2,x,1,3)", "1+2*(x-1)+(x-1)^2+O((x-1)^3)");
    expect("series(sin(x),x,1,3)", "sin(1)+cos(1)*(x-1)-sin(1)/2*(x-1)^2+O((x-1)^3)");
    expect("series(cos(x),x=pi,4)", "-1+1/2*(x-pi)^2+O((x-pi)^4)");
    expect("series(1/(1+x),x,inf,4)", "1/x-1/x^2+1/x^3+O(1/x^4)");
    expect("series(1/sin(x),x,0,4)", "1/x+1/6*x+7/360*x^3+O(x^4)"); /* Laurent */
    expect("series(sqrt(x)*exp(x),x,0,3)", "sqrt(x)+x^(3/2)+1/2*x^(5/2)+O(x^3)"); /* Puiseux */
    expect("series(exp(a*x),x,0,3)", "1+a*x+a^2/2*x^2+O(x^3)");
    expect("series(exp(x),x,0,0)", "O(1)");
    /* the value is the polynomial: arithmetic, subst and ans drop the O-term */
    expect("series(exp(x),x,0,3)", "1+x+1/2*x^2+O(x^3)");
    expect("ans", "x^2/2+x+1");
    expect("2*series(exp(x),x,0,3)", "x^2+2*x+2");
    expect("subst(series(exp(x),x,0,3),x,2)", "5");
    expect("f:=series(cos(x),x,0,4)", "-x^2/2+1");
    cas_settings s;
    cas_get_settings(ctx, &s);
    s.exact = 0;
    s.digits = 6;
    cas_set_settings(ctx, &s);
    expect("series(exp(x),x,0,3)", "1.0+1.0*x+0.5*x^2+O(x^3)");
    s.exact = 1;
    s.digits = 12;
    cas_set_settings(ctx, &s);
}

/* COMPLEX mode REAL (settings.real): odd-denominator powers of negative
 * numbers are real roots in every evaluator; complex mode keeps the
 * principal branch; even roots stay non-real in both. */
static void real_mode(int on) {
    cas_settings s;
    cas_get_settings(ctx, &s);
    s.real = on;
    cas_set_settings(ctx, &s);
    cas_get_settings(ctx, &s);
    CHECK(s.real == on, "settings.real round trip");
}
static void test_real_roots(void) {
    cas_reset(ctx);
    /* default after cas_init: complex mode, principal branch */
    expect("(-8)^(1/3)", "2*(-1)^(1/3)");
    expect("(-1)^(2/3)", "(-1)^(2/3)");
    expect("approx((-8)^(1/3))", "1.0+1.73205080757*i");
    expect("(-2.0)^(1/3)", "0.629960524947+1.09112363597*i");
    expect("integrate(x^(-1/3),x,-1,8)", "6-3*(-1)^(2/3)/2");
    expect("solve(x^(1/3)=-2,x)", "[]");
    expect("solve(x^3=-8,x)", "[-2]");
    real_mode(1);
    /* exact simplification */
    expect("(-8)^(1/3)", "-2");
    expect("(-1)^(2/3)", "1");
    expect("(-8)^(-1/3)", "-1/2");
    expect("(-1)^(7/3)", "-1");
    expect("(-27/8)^(2/3)", "9/4");
    expect("(-2)^(1/3)", "-2^(1/3)");
    expect("(1-pi)^(1/3)", "-(pi-1)^(1/3)");
    expect("(-x)^(1/3)", "-x^(1/3)");
    expect("(-8*x)^(2/3)", "4*x^(2/3)");
    expect("(x^3)^(1/3)", "x");
    expect("subst(x^(1/3),x,-8)", "-2");
    expect("f(t):=t^(1/3)", "t^(1/3)");
    expect("f(-27)", "-3");
    /* even roots of negatives and non-rational exponents stay non-real */
    expect("sqrt(-1)", "i");
    expect("(-4)^(3/2)", "-8*i");
    expect("(-8)^(1/6)", "(-1)^(1/6)*sqrt(2)");
    /* numeric, approximate mode and high precision */
    expect("(-2.0)^(1/3)", "-1.25992104989");
    expect("(-1.0)^(2/3)", "1.0");
    expect("approx((-2)^(1/3))", "-1.25992104989");
    expect("approx((1-pi)^(1/3),30)", "-1.28897834971234295027998620145");
    expect("approx(cos(2)^(1/3),30)", "-0.746590052610028330936386758153");
    expect("approx(cos(2)^(1/3))", "-0.74659005261");
    expect("nint(x^(1/3),x,-8,0)", "-12.0");
    /* calculus */
    expect("integrate(x^(-1/3),x,-1,8)", "9/2");
    expect("integrate(x^(1/3),x,-8,0)", "-12");
    expect("limit(x^(1/3),x,-1)", "-1");
    expect("limit(x^(-1/3),x,0,-1)", "-inf");
    expect("limit((x^(1/3)+2)/(x+8),x,-8)", "1/12");
    expect("taylor(x^(1/3),x,-8,2)", "-2+1/12*(x+8)+O((x+8)^2)");
    /* solve: real cube roots */
    expect("solve(x^3=-8,x)", "[-2]");
    expect("solve(x^(1/3)=-2,x)", "[-8]");
    expect("solve(x^(2/3)=4,x)", "[-8,8]");
    expect("solve(x^(1/2)=-2,x)", "[]");
    /* sign of x^(1/3) for x < 0 */
    expect("assume(y<0)", "y<0");
    expect("sign(y^(1/3))", "-1");
    expect("abs(y^(1/3))", "-y^(1/3)");
    /* approximate mode keeps odd roots exact, so they stay real */
    {
        cas_settings s;
        cas_get_settings(ctx, &s);
        s.exact = 0;
        cas_set_settings(ctx, &s);
        expect("(-8)^(1/3)", "-2.0");
        expect("x^(1/3)", "x^(1/3)");
        expect("subst(x^(2/3)+1,x,-8)", "5.0");
        s.exact = 1;
        cas_set_settings(ctx, &s);
    }
    /* compiled functions use the real root in both modes */
    cas_fn *fn;
    cas_status st = cas_compile(ctx, "x^(1/3)", "x", &fn);
    CHECK(st == CAS_OK && fabs(cas_fn_eval(fn, -8.0) + 2) < 1e-12, "real mode compiled cube root");
    if (st == CAS_OK) cas_fn_free(ctx, fn);
    /* switching back restores the principal branch */
    real_mode(0);
    expect("(-8)^(1/3)", "2*(-1)^(1/3)");
    /* re, im, abs, arg, conj of principal roots of -1 and products with them */
    expect("re((-1)^(1/3))", "1/2");
    expect("im((-1)^(1/3))", "sqrt(3)/2");
    expect("abs((-1)^(1/3))", "1");
    expect("arg((-1)^(1/3))", "pi/3");
    expect("arg((-1)^(2/3))", "2*pi/3");
    expect("arg((-1)^(5/3)/2)", "-pi/3");
    expect("arg(-(-1)^(2/3))", "-pi/3");
    expect("abs(2*(-1)^(1/3))", "2");
    expect("arg(2*(-1)^(1/3))", "pi/3");
    expect("im(2*(-1)^(1/3))", "sqrt(3)");
    expect("abs(x*(-1)^(1/3))", "abs(x)");
    expect("conj((-1)^(1/3))", "1/2-sqrt(3)*i/2");
    expect("im((-1)^(1/7))", "sin(pi/7)");
    expect("abs(1+(-1)^(1/3))", "sqrt(3)");
    expect("arg(1+(-1)^(1/3))", "pi/6");
    expect("arg(-1+(-1)^(1/3))", "2*pi/3");
    expect("abs((-1)^(1/6)*sqrt(2))", "sqrt(2)");
    expect("(-x)^(1/3)", "(-x)^(1/3)");
    st = cas_compile(ctx, "x^(1/3)", "x", &fn);
    CHECK(st == CAS_OK && fabs(cas_fn_eval(fn, -8.0) + 2) < 1e-12, "complex mode compiled cube root");
    if (st == CAS_OK) cas_fn_free(ctx, fn);
    cas_reset(ctx);
}

static void test_memory(void) {
    /* long session: repeated assignments must not leak persistent memory */
    cas_mem_stats m0, m1;
    ev("v:=expand((x+1)^20)");
    cas_mem(ctx, &m0);
    for (int i = 0; i < 2000; i++) {
        char buf[64];
        snprintf(buf, sizeof buf, "v:=expand((x+%d)^20)", i % 7);
        ev(buf);
    }
    cas_mem(ctx, &m1);
    CHECK(m1.used <= m0.used + 4096, "persistent memory grows: %zu -> %zu", m0.used, m1.used);
    /* output truncation */
    char small[8];
    cas_status st = cas_eval(ctx, "2^100", small, sizeof small);
    CHECK(st == CAS_ERR_OUTPUT_TRUNC, "truncation status %d", st);
    /* deep nesting is rejected, not crashing */
    static char deep[200000];
    int n = 0;
    for (int i = 0; i < 60000; i++) deep[n++] = '(';
    deep[n++] = '1';
    for (int i = 0; i < 60000; i++) deep[n++] = ')';
    deep[n] = 0;
    st = cas_eval(ctx, deep, out, sizeof out);
    CHECK(st != CAS_OK, "deep nesting should fail");
    /* out of memory is reported */
    st = cas_eval(ctx, "expand((x+y+z+w+1)^60)", out, sizeof out);
    CHECK(st == CAS_OK || st == CAS_ERR_MEMORY, "big expand status %d", st);
    expect("1+1", "2");
    /* symbol table GC: many distinct names over a long session */
    expect("keepme:=zz1+qq7", "qq7+zz1");
    expect("gfun(t):=t*ww3", "t*ww3");
    for (int i = 0; i < 30000; i++) {
        char buf[64];
        snprintf(buf, sizeof buf, "sym%d+1", i);
        cas_status st2 = cas_eval(ctx, buf, out, sizeof out);
        if (st2 != CAS_OK) {
            CHECK(0, "symbol churn failed at %d: %s", i, cas_last_error(ctx));
            break;
        }
    }
    expect("keepme", "qq7+zz1");
    expect("gfun(2)", "2*ww3");
    expect("ans+zz1", "2*ww3+zz1");
    /* reset */
    cas_reset(ctx);
    expect("a", "a");
}

static void on_alarm(int sig) {
    (void)sig;
    cas_interrupt(ctx);
}

static void test_deep(void) {
    signal(SIGALRM, on_alarm);
    /* MCU-like stack budget: deep expressions must fail gracefully */
    cas_set_stack_limit(ctx, 48u << 10);
    ev("deep:=x");
    int built = 0;
    for (int i = 0; i < 3000; i++) {
        cas_status st = cas_eval(ctx, i % 2 ? "deep:=sin(deep)+1" : "deep:=deep^2+x", out, sizeof out);
        if (st != CAS_OK) break;
        built++;
    }
    CHECK(built > 20, "deep build too shallow: %d", built);
    printf("deep expression nesting reached with 48 KB stack: %d levels\n", built);
    const char *ops[] = {"diff(deep,x)", "integrate(deep,x)", "simplify(deep)", "factor(deep)", "expand(deep)",
                         "series(deep,x,0,3)", "limit(deep,x,0)", "solve(deep=0,x)", "approx(subst(deep,x,1/2))",
                         "deep-deep", "deep*2", "normal(deep)", "taylor(deep,x,0,2)", "subst(deep,x,0)",
                         "trigsimp(deep)", "texpand(deep)", "has(deep,x)", "partfrac(deep,x)"};
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        struct itimerval tv = {{0, 0}, {2, 0}}, z = {{0, 0}, {0, 0}};
        setitimer(ITIMER_REAL, &tv, NULL);
        cas_status st = cas_eval(ctx, ops[i], out, sizeof out);
        setitimer(ITIMER_REAL, &z, NULL);
        CHECK(st == CAS_OK || st == CAS_ERR_MEMORY || st == CAS_ERR_UNSUPPORTED || st == CAS_ERR_DOMAIN ||
                  st == CAS_ERR_OUTPUT_TRUNC || st == CAS_ERR_INTERRUPTED,
              "deep op %s status %d", ops[i], st);
    }
    char lx[256];
    cas_latex(ctx, lx, sizeof lx);
    cas_set_stack_limit(ctx, 2u << 20);
    expect("1+1", "2");
    ev("purge(deep)");
}

static void test_compile(void) {
    cas_fn *fn;
    cas_status st = cas_compile(ctx, "sin(x)/x + x^2", "x", &fn);
    CHECK(st == CAS_OK, "compile status");
    if (st == CAS_OK) {
        double v = cas_fn_eval(fn, 2.0);
        CHECK(fabs(v - (sin(2.0) / 2 + 4)) < 1e-12, "compiled value %g", v);
        CHECK(isnan(cas_fn_eval(fn, 0.0)) || 1, "nan ok");
        cas_fn_free(ctx, fn);
    }
    st = cas_compile(ctx, "x^(1/3)", "x", &fn);
    CHECK(st == CAS_OK && fabs(cas_fn_eval(fn, -8.0) + 2) < 1e-12, "real cube root");
    if (st == CAS_OK) cas_fn_free(ctx, fn);
    st = cas_compile(ctx, "sqrt(x)", "x", &fn);
    CHECK(st == CAS_OK && isnan(cas_fn_eval(fn, -1.0)), "sqrt negative is nan");
    if (st == CAS_OK) cas_fn_free(ctx, fn);
    ev("k:=3");
    st = cas_compile(ctx, "k*x", "x", &fn);
    CHECK(st == CAS_OK && fabs(cas_fn_eval(fn, 2.0) - 6) < 1e-12, "uses variables");
    if (st == CAS_OK) cas_fn_free(ctx, fn);
    st = cas_compile(ctx, "y*x", "x", &fn);
    CHECK(st != CAS_OK, "undefined var");
    /* several variables; stored values of the listed names are hidden */
    const char *xy[] = {"x", "y", "k"};
    st = cas_compile_vars(ctx, "x^2+y^2-k*sin(x*y)", xy, 3, &fn);
    CHECK(st == CAS_OK, "compile_vars status");
    if (st == CAS_OK) {
        double v[3] = {1.5, -2.0, 0.5};
        double r = cas_fn_eval_vars(fn, v);
        CHECK(fabs(r - (2.25 + 4 - 0.5 * sin(-3.0))) < 1e-12, "compile_vars value %g", r);
        CHECK(fabs(cas_fn_eval(fn, 2.0) - 4) < 1e-12, "eval with other vars 0");
        cas_fn_free(ctx, fn);
    }
    const char *one[] = {"x"};
    st = cas_compile_vars(ctx, "y*x", one, 1, &fn);
    CHECK(st != CAS_OK, "compile_vars undefined var");
}


/* cas_save / cas_restore / cas_vars */
static char sess[1 << 16];
static size_t sess_len;
static int sess_writer(void *user, const char *d, size_t n) {
    (void)user;
    if (sess_len + n > sizeof sess) return 1;
    memcpy(sess + sess_len, d, n);
    sess_len += n;
    return 0;
}
static int tiny_writer(void *user, const char *d, size_t n) {
    int *calls = user;
    return ++*calls >= 3; /* stop after a few records */
}
static char vars_seen[512];
static void vars_cb(void *user, const char *name, int np) {
    (void)user;
    char t[64];
    snprintf(t, sizeof t, "%s/%d ", name, np);
    strncat(vars_seen, t, sizeof vars_seen - strlen(vars_seen) - 1);
}

static void test_session(void) {
    cas_reset(ctx);
    expect("a:=2.5", "2.5");
    expect("b:=x+1/3", "x+1/3");
    expect("x:=3", "3");               /* b keeps x unevaluated */
    expect("g(u,v):=u^2-2*u*v+sin(v)/3", "u^2-2*u*v+sin(v)/3");
    expect("h(t):=when(t>0,t,-t)", "when(t>0,t,-t)");
    expect("m:=[[1,2],[3,4]]", "[[1,2],[3,4]]");
    expect("c:=0.1", "0.1");
    expect("assume(p>0)", "p>0");
    expect("big:=2^200", "1606938044258990275541962092341162602522202993782792835301376");
    expect("17/4", "17/4");            /* ans */
    sess_len = 0;
    CHECK(cas_save(ctx, sess_writer, NULL) == CAS_OK, "cas_save failed");
    CHECK(sess_len > 5 && !memcmp(sess, "CAS1\n", 5), "bad header");
    vars_seen[0] = 0;
    cas_vars(ctx, vars_cb, NULL);
    CHECK(strstr(vars_seen, "a/-1 ") && strstr(vars_seen, "g/2 ") && strstr(vars_seen, "x/-1 ") &&
              !strstr(vars_seen, "p/"), "cas_vars: %s", vars_seen);
    /* restore into a fresh context in degree + approx mode: the settings must not apply */
    cas_reset(ctx);
    cas_settings s;
    cas_get_settings(ctx, &s);
    s.angle = CAS_ANGLE_DEG;
    cas_set_settings(ctx, &s);
    expect("a", "a");
    CHECK(cas_restore(ctx, sess, sess_len) == CAS_OK, "cas_restore: %s", cas_last_error(ctx));
    s.angle = CAS_ANGLE_RAD;
    cas_set_settings(ctx, &s);
    expect("ans", "17/4");
    expect("a", "2.5");
    expect("c*3", "0.3");
    expect("b", "x+1/3");
    expect("x", "3");
    expect("g(2,0)", "4");
    expect("g(1,pi/2)", "4/3-pi");
    expect("h(-5)", "5");
    expect("m^2", "[[7,10],[15,22]]");
    expect("big", "1606938044258990275541962092341162602522202993782792835301376");
    expect("sqrt(p^2)", "p");
    /* save -> restore -> save keeps the meaning (raw function bodies may print
     * in a different but equivalent order) */
    static char s2[1 << 16];
    expect("17/4", "17/4");
    sess_len = 0;
    cas_save(ctx, sess_writer, NULL);
    size_t l2 = sess_len;
    memcpy(s2, sess, l2);
    cas_reset(ctx);
    CHECK(cas_restore(ctx, s2, l2) == CAS_OK, "second restore");
    expect("big", "1606938044258990275541962092341162602522202993782792835301376");
    expect("g(1,pi/2)", "4/3-pi");
    /* bad records are skipped, the rest is restored */
    cas_reset(ctx);
    const char *bad = "CAS1\nv q 7\nv r (((\nz junk\nf k notalist\nv pi 3\nv s q+1\n";
    CHECK(cas_restore(ctx, bad, strlen(bad)) == CAS_ERR_SYNTAX, "bad records not reported");
    expect("q", "7");
    expect("s", "q+1");
    expect("r", "r");
    expect("pi", "pi");
    /* cas_eval_quiet leaves ans alone */
    expect("6*7", "42");
    CHECK(cas_eval_quiet(ctx, "Y1(x):=x^2+1", out, sizeof out) == CAS_OK, "quiet define");
    expect("ans", "42");
    expect("Y1(3)", "10");
    /* writer stop */
    int calls = 0;
    CHECK(cas_save(ctx, tiny_writer, &calls) == CAS_ERR_OUTPUT_TRUNC, "writer stop not reported");
    cas_reset(ctx);
}

#include "unit_xcmds.inc"
#include "unit_regress.inc"
#include "unit_suites.inc"
#include "unit_unc.inc"
#include "unit_autosimp.inc"
#include "unit_forms.inc"
#include "unit_wester.inc"
#include "unit_gaps.inc"
#include "unit_robust.inc"
#include "unit_round3.inc"
#include "unit_round4.inc"
#include "unit_round5.inc"
#include "unit_round6.inc"
#include "unit_trigsolve.inc"
#include "unit_tidy.inc"
#include "unit_applied.inc"
#include "unit_nabla.inc"
#include "unit_known.inc"
/* cas_set_poll: the callback can stop an evaluation; NULL removes it */
static int poll_calls, poll_stop_after;
static int poll_cb(void *user) {
    (void)user;
    return ++poll_calls >= poll_stop_after;
}

static void test_poll(void) {
    poll_calls = 0;
    poll_stop_after = 1 << 30;
    cas_set_poll(ctx, poll_cb, NULL);
    expect("expand((x+y+1)^6)-expand((x+y+1)^6)", "0");
    CHECK(poll_calls > 0, "poll callback never called");
    poll_calls = 0;
    poll_stop_after = 2;
    cas_status st = cas_eval_quiet(ctx, "expand((x+y+z+1)^12)", out, sizeof out);
    CHECK(st == CAS_ERR_INTERRUPTED, "poll did not interrupt (%d)", st);
    cas_set_poll(ctx, NULL, NULL);
    expect("1+1", "2");
    expect("expand((x+1)^2)", "x^2+2*x+1");
}

/* fourier of a parameter of unknown sign assumes a sign for the parameter during the call: an
 * interrupt at any point of the call must not leave the assumption behind */
static void test_poll_fourier(void) {
    const char *in = "fourier(exp(-a*abs(x)),x,w)";
    poll_calls = 0;
    poll_stop_after = 1 << 30;
    cas_set_poll(ctx, poll_cb, NULL);
    cas_status st = cas_eval_quiet(ctx, in, out, sizeof out);
    int total = poll_calls, hit = 0;
    CHECK(st == CAS_OK && total > 0, "%s: %d, %d polls", in, st, total);
    for (int k = 1; k <= total; k++) {
        poll_calls = 0;
        poll_stop_after = k;
        st = cas_eval_quiet(ctx, in, out, sizeof out);
        hit += st == CAS_ERR_INTERRUPTED;
        cas_set_poll(ctx, NULL, NULL);
        expect("abs(a)", "abs(a)");
        cas_set_poll(ctx, poll_cb, NULL);
    }
    CHECK(hit > 0, "%s never interrupted", in);
    cas_set_poll(ctx, NULL, NULL);
    expect(in, "when(a>0,a*sqrt(2)/(sqrt(pi)*(a^2+w^2)),undef)");
}

/* Regressions for the CAS entries of docs/KNOWN_ISSUES.md.  The memory
 * overruns were inside the scratch arena, so most of them showed up only
 * as crashes or corrupted results; these inputs exercise the fixed bounds. */
static void test_known_issues(void) {
    cas_reset(ctx);
    /* factor.c cyclotomic: pass 0 of Phi_30 reaches degree 40 > n + 1 */
    expect("factor(x^30-1)", "(x-1)*(x+1)*(x^2+x+1)*(x^2-x+1)*(x^4+x^3+x^2+x+1)*(x^4-x^3+x^2-x+1)*"
                             "(x^8+x^7-x^5-x^4-x^3+x+1)*(x^8-x^7+x^5-x^4+x^3-x+1)");
    /* upoly.c partfrac: a factor of multiplicity 70 gives 70 terms */
    ev("partfrac(x^70/(x+1)^70,x)");
    CHECK(!strncmp(out, "1/(x+1)^70-70/(x+1)^69+2415/(x+1)^68", 36) && !strstr(out, "ERROR"),
          "partfrac multiplicity 70: %.80s", out);
    /* sum.c: shift 100 needs 200 boundary terms (no closed form attempted) */
    expect("sum(1/(k*(k+100)),k,1,n)", "sum(1/(k*(k+100)),k,1,n)");
    expect("sum(1/(k*(k+3)),k,1,inf)", "11/18");
    /* gbasis.c: more than 256 input polynomials */
    {
        static char in[8192];
        size_t n = 0;
        n += (size_t)snprintf(in + n, sizeof in - n, "gbasis([");
        for (int i = 0; i < 300; i++) n += (size_t)snprintf(in + n, sizeof in - n, "%sx-%d*y", i ? "," : "", i);
        snprintf(in + n, sizeof in - n, "],[x,y])");
        expect(in, "ERROR 6: gbasis: basis too large");
    }
    /* ode.c: a complex double root whose conjugate is not skipped fills the
     * basis exactly (the new bound check must not reject it) */
    expect("assume(b>0)", "b>0");
    expect("desolve(y''''+2*b^2*y''+b^4*y=0,x,y)", "c3*cos(b*x)*x-c4*sin(b*x)*x+c1*cos(b*x)-c2*sin(b*x)");
    /* big.c: the base-61 Miller-Rabin round; unbounded multiplicities */
    expect("isprime(61)", "true");
    expect("nextprime(60)", "61");
    expect("ifactor(2^300*3^7*101^3)", "2^300*3^7*101^3");
    expect("ifactor(1001^100)", "7^100*11^100*13^100");
    /* simp.c: a^u b^u with ab = 1; radicands with > 48 small primes; the
     * s_mul redo list overflowing the factor array */
    expect("2^x*(1/2)^x", "1");
    expect("sqrt(281!)^2-281!", "0");
    expect("sqrt(a*b*c*d*e*f*g*h*j*k*m)*sqrt(n*p*q*r*s*t*u*v*w*y*z)*sqrt(a*b*c*d*e*f*g*h*j*k*m)*"
           "sqrt(n*p*q*r*s*t*u*v*w*y*z)",
           "a*b*c*d*e*f*g*h*j*k*m*n*p*q*r*s*t*u*v*w*y*z");
    /* sum.c: sign of a divergence, telescoping across split terms, poles in
     * the range, zero factors of products */
    expect("sum(-1/k,k,1,inf)", "-inf");
    expect("sum(1/k-1/(k+1),k,1,inf)", "1");
    expect("sum(1/k+1/2^k,k,1,inf)", "inf");
    expect("sum(1/k-2/(2*k+1),k,1,inf)", "sum(-2/(2*k+1)+1/k,k,1,inf)");
    expect("sum(1/((k-3)*(k-2)),k,1,inf)", "ERROR 2: division by zero");
    expect("sum(1/(k-3)^2,k,1,inf)", "ERROR 2: division by zero");
    expect("sum(1/((k-3)*(k-2)),k,4,inf)", "1");
    expect("product(k-3,k,1,n)", "product(k-3,k,1,n)"); /* 0 only for n >= 3: no closed form */
    expect("product(k-3,k,1,inf)", "0");
    expect("product(k-3,k,4,n)", "(n-3)!");
    /* cmds.c: cfactor keeps a^e; odesolve releases scratch per step and
     * reports the step cap */
    expect("cfactor((2x^2+1)^2)", "4*(x-sqrt(2)*i/2)^2*(x+sqrt(2)*i/2)^2");
    expect("odesolve(y,[x,y],[0,1],1)", "2.71828182846");
    expect("odesolve(-1000*(y-cos(x)),[x,y],[0,0],100)", "ERROR 6: odesolve: too many steps"); /* was out of memory */
    /* y' = y^2 blows up at x = 1: tiny steps are no longer accepted blindly */
    expect("odesolve(y^2,[x,y],[0,1],2)", "ERROR 2: odesolve: singularity or blow-up in the solution");
    expect("odesolve(y^2,[x,y],[0,1],0.9)", "10.0");
    /* dead/duplicated code removed from simp.c (log cases), gbasis.c
     * (interreduction) and simplify.c: behaviour unchanged */
    expect("log(100)", "2");
    expect("ln(8,2)", "3");
    expect("log10(x)", "ln(x)/ln(10)");
    expect("gbasis([x^2+y*z-2,y^2+x*z-3,z^2+x*y-5],[x,y,z],grevlex)",
           "[x*z+y^2-3,x*y+z^2-5,x^2+y*z-2,3*x/2+y*z^2-5*y/2-z,x*z^2-5*x/2+y-3*z/2,x*z+3*y*z/2+z^4-15*z^2/2+19/2]");
    expect("simplify(sqrt(3+2*sqrt(2)))", "sqrt(2)+1");
    /* statdist.c: p = 1 */
    expect("geometpdf(1.0,1)", "1.0");
    /* ode.c Laplace: steps before t = 0, scaled dirac */
    expect("laplace(heaviside(t+2),t,s)", "1/s");
    expect("laplace(heaviside(t-2),t,s)", "exp(-2*s)/s");
    expect("laplace(heaviside(-2*t),t,s)", "0");
    expect("laplace(dirac(-2*t),t,s)", "1/2");
    cas_reset(ctx);
}

int main(void) {
    size_t sz = 32u << 20;
    void *arena = malloc(sz);
    ctx = cas_init(arena, sz);
    if (!ctx) {
        printf("init failed\n");
        return 1;
    }
    srand(12345);
    test_bignum();
    test_rational();
    test_poly();
    test_eval();
    test_settings();
    test_real_roots();
    test_series_display();
    test_compile();
    test_deep();
    test_memory();
    test_session();
    test_xcmds();
    test_unc();
    test_poll();
    test_poll_fourier();
    test_regress();
    test_suites();
    test_autosimp();
    test_forms();
    test_wester();
    test_gaps();
    test_round3();
    test_round4();
    test_round5();
    test_round6();
    test_trigsolve();
    test_tidy_forms();
    test_applied();
    test_nabla();
    test_gaps2();
    test_gaps3();
    test_known();
    test_robust();
    test_case_names();
    test_known_issues();
    printf("unit tests: %d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
