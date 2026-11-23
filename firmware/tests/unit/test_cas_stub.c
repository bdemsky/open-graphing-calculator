/* Unit tests for the numeric CAS stub (cas_stub/cas_stub.c).
 *
 * The stub implements the cas.h interface with plain double arithmetic so the
 * firmware, simulators and UI tests can be built without the symbolic CAS
 * library (CAS=stub). This test pins down its behaviour: parsing and
 * precedence, implicit multiplication, UTF-8 operators (π √ × −), numeric
 * versions of diff/integrate/solve/limit, error statuses and messages,
 * angle/exact/digit settings, variables and `ans`, and compiled functions
 * (cas_compile*: what the graph screens evaluate per pixel).
 * The Makefile always links this test with the stub (STUB_LIB_OBJ), whatever
 * CAS the rest of the build uses; it runs in `make test` like every
 * tests/unit/test_*.c. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cas.h"

static int failures, checks;
static unsigned char arena[1u << 20]; /* used by test_init(), then reused by main()'s context */

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

/* One cas_eval() case: input, expected status, and the expected output
 * text (CAS_OK) or a substring of cas_last_error() (errors). */
typedef struct {
    const char *in;
    cas_status st;
    const char *out; /* expected text (for CAS_OK) or substring of the error message */
} eval_case;

/* Evaluate every case in order (later cases may depend on earlier ones,
 * e.g. variables and ans) and report each mismatch. */
static void run_cases(cas_ctx *c, const eval_case *cases, size_t n)
{
    char out[128];
    for (size_t i = 0; i < n; i++) {
        cas_status st = cas_eval(c, cases[i].in, out, sizeof out);
        if (cases[i].st == CAS_OK) {
            CHECK(st == CAS_OK && strcmp(out, cases[i].out) == 0, "eval(\"%s\") = [%d] \"%s\" (%s), want \"%s\"",
                  cases[i].in, st, out, st ? cas_last_error(c) : "", cases[i].out);
        } else {
            CHECK(st == cases[i].st && strstr(cas_last_error(c), cases[i].out) != NULL,
                  "eval(\"%s\") = [%d] \"%s\" msg \"%s\", want status %d msg ~\"%s\"", cases[i].in, st, out,
                  cas_last_error(c), cases[i].st, cases[i].out);
        }
    }
}

/* Angle mode, exact (1) or approximate (0) results, significant digits. */
static void set_mode(cas_ctx *c, cas_angle_mode angle, int exact, int digits)
{
    cas_settings s = {angle, exact, digits};
    cas_set_settings(c, &s);
}

/* Approximate mode, radians, 12 digits: arithmetic (^ is right
 * associative, unary minus binds looser than ^), number syntax, implicit
 * multiplication, functions, exponent formatting (1E25, 1E-5), syntax and
 * domain errors, and the symbolic commands the stub cannot do (it returns
 * CAS_ERR_UNSUPPORTED, or the numeric value when the argument is a number). */
static const eval_case approx_cases[] = {
    {"2+3", CAS_OK, "5"},
    {"2+3*4", CAS_OK, "14"},
    {"(2+3)*4", CAS_OK, "20"},
    {"2^3^2", CAS_OK, "512"},
    {"-2^2", CAS_OK, "-4"},
    {"(-2)^2", CAS_OK, "4"},
    {"2*-3", CAS_OK, "-6"},
    {"2^-1", CAS_OK, "0.5"},
    {"--3", CAS_OK, "3"},
    {"1/3", CAS_OK, "0.333333333333"},
    {"10/4", CAS_OK, "2.5"},
    {"1.5E-3*2", CAS_OK, "0.003"},
    {"1.5e3", CAS_OK, "1500"},
    {".5+.25", CAS_OK, "0.75"},
    {"2pi", CAS_OK, "6.28318530718"},
    {"2(3)", CAS_OK, "6"},
    {"(1+1)(2+2)", CAS_OK, "8"},
    {"3sin(0)", CAS_OK, "0"},
    {"2e", CAS_OK, "5.43656365692"},
    {"\xCF\x80", CAS_OK, "3.14159265359"},           /* π */
    {"\xE2\x88\x9A" "16", CAS_OK, "4"},               /* √16 */
    {"\xE2\x88\x9A(2)^2", CAS_OK, "2"},
    {"2\xC3\x97" "3\xE2\x88\x92" "1", CAS_OK, "5"},    /* 2×3−1 */
    {"5!", CAS_OK, "120"},
    {"sqrt(2)", CAS_OK, "1.41421356237"},
    {"sin(pi/2)", CAS_OK, "1"},
    {"sin(pi)", CAS_OK, "0"},
    {"cos(0)+tan(0)", CAS_OK, "1"},
    {"atan(1)*4", CAS_OK, "3.14159265359"},
    {"ln(e)", CAS_OK, "1"},
    {"log(1000)", CAS_OK, "3"},
    {"log(8,2)", CAS_OK, "3"},
    {"exp(0)", CAS_OK, "1"},
    {"abs(-3.5)", CAS_OK, "3.5"},
    {"floor(-2.5)+ceil(2.1)", CAS_OK, "0"},
    {"round(2.567,2)", CAS_OK, "2.57"},
    {"min(3,1,2)+max(4,9)", CAS_OK, "10"},
    {"sign(-7)", CAS_OK, "-1"},
    {"mod(7,3)", CAS_OK, "1"},
    {"mod(-1,3)", CAS_OK, "2"},
    {"nthroot(-27,3)", CAS_OK, "-3"},
    {"cbrt(8)", CAS_OK, "2"},
    {"sinh(0)+cosh(0)", CAS_OK, "1"},
    {"1E20*1E5", CAS_OK, "1E25"},
    {"1/100000", CAS_OK, "1E-5"},
    {"0*-1", CAS_OK, "0"},
    {"2 3", CAS_OK, "6"},
    /* errors */
    {"", CAS_ERR_SYNTAX, "empty"},
    {"2+", CAS_ERR_SYNTAX, "end of input"},
    {"(2+3", CAS_ERR_SYNTAX, "missing ')'"},
    {"2+3)", CAS_ERR_SYNTAX, "unmatched"},
    {"2$3", CAS_ERR_SYNTAX, "unexpected character"},
    {"1/0", CAS_ERR_DOMAIN, "division by zero"},
    {"sqrt(-1)", CAS_ERR_DOMAIN, "domain"},
    {"ln(0)", CAS_ERR_DOMAIN, "domain"},
    {"q+1", CAS_ERR_UNDEFINED, "undefined: q"},
    {"sin(1,2)", CAS_ERR_SYNTAX, "number of arguments"},
    {"sin 1", CAS_ERR_SYNTAX, "missing '('"},
    {"2=3", CAS_ERR_SYNTAX, "only allowed inside solve"},
    {"pi:=3", CAS_ERR_SYNTAX, "cannot assign"},
    {"factor(x^2-1)", CAS_ERR_UNSUPPORTED, "factor needs the CAS library"},
    {"expand((x+1)^2)", CAS_ERR_UNSUPPORTED, "expand needs the CAS library"},
    {"diff(x^2,x)", CAS_ERR_UNSUPPORTED, "symbolic diff"},
    {"integrate(x,x)", CAS_ERR_UNSUPPORTED, "symbolic integrate"},
    {"factor(12)", CAS_OK, "12"},
    {"simplify(2+2)", CAS_OK, "4"},
    {"approx(1/4)", CAS_OK, "0.25"},
};

/* Numeric commands: diff at a point (diff(f,x,x0)), definite integrals,
 * real roots of solve (a double root is found although f does not change
 * sign; a pole is not a root), limits (finite, removable singularities,
 * infinity, a two-sided limit that does not exist); a command must be the
 * whole input. */
static const eval_case command_cases[] = {
    {"diff(x^3,x,2)", CAS_OK, "12"},
    {"diff(sin(x),x,0)", CAS_OK, "1"},
    {"integrate(x^2,x,0,3)", CAS_OK, "9"},
    {"integrate(sin(x),x,0,pi)", CAS_OK, "2"},
    {"solve(x^2=2,x)", CAS_OK, "x=-1.41421356237 or x=1.41421356237"},
    {"solve(x^2-4,x)", CAS_OK, "x=-2 or x=2"},
    {"solve(2t+1=0,t)", CAS_OK, "t=-0.5"},
    {"solve(x^2,x)", CAS_OK, "x=0"},                    /* tangent zero */
    {"solve((x-1)^2,x)", CAS_OK, "x=1"},
    {"solve(1/x,x)", CAS_ERR_UNSUPPORTED, "no solution"}, /* pole is not a root */
    {"solve(x^2+1=0,x)", CAS_ERR_UNSUPPORTED, "no solution"},
    /* non-finite integrand values end the quadrature at once (no 2^40 recursion) */
    {"integrate(1/x,x,-1,1)", CAS_ERR_DOMAIN, "division by zero"},
    {"integrate(sqrt(-1-x^2),x,0,1)", CAS_ERR_DOMAIN, "domain error"},
    {"limit(sin(x)/x,x,0)", CAS_OK, "1"},
    {"limit((x^2-1)/(x-1),x,1)", CAS_OK, "2"},
    {"limit(1/x,x,0)", CAS_ERR_DOMAIN, "limit does not exist"},
    {"limit(1/x,x,inf)", CAS_OK, "0"},
    {"2*solve(x=1,x)", CAS_ERR_UNSUPPORTED, "whole input"},
};

static void test_eval(cas_ctx *c)
{
    set_mode(c, CAS_ANGLE_RAD, 0, 12);
    run_cases(c, approx_cases, sizeof approx_cases / sizeof approx_cases[0]);
    run_cases(c, command_cases, sizeof command_cases / sizeof command_cases[0]);
}

/* Degree mode (exact values at multiples of 30/45/90 degrees, tan 90 is a
 * domain error), exact mode (the stub computes in double and shows a result
 * as p/q when a continued-fraction approximation with q <= 1000 matches it
 * to 1e-12 relative, so 0.1+0.2 gives 3/10 while 1/1001 and sqrt(2) stay
 * decimals), and 6-digit rounding.
 * Restores RAD / approximate / 12 digits at the end. */
static void test_modes(cas_ctx *c)
{
    static const eval_case deg[] = {
        {"sin(90)", CAS_OK, "1"},
        {"cos(180)", CAS_OK, "-1"},
        {"sin(180)", CAS_OK, "0"},
        {"sin(30)", CAS_OK, "0.5"},
        {"tan(45)", CAS_OK, "1"},
        {"asin(1)", CAS_OK, "90"},
        {"atan(1)", CAS_OK, "45"},
        {"tan(90)", CAS_ERR_DOMAIN, "domain"},
    };
    set_mode(c, CAS_ANGLE_DEG, 0, 12);
    run_cases(c, deg, sizeof deg / sizeof deg[0]);

    static const eval_case exact[] = {
        {"1/3", CAS_OK, "1/3"},
        {"-7/4", CAS_OK, "-7/4"},
        {"2/4+1", CAS_OK, "3/2"},
        {"6/3", CAS_OK, "2"},
        {"sqrt(2)", CAS_OK, "1.41421356237"},
        {"1/1001", CAS_OK, "0.000999000999001"},
        {"1/100001", CAS_OK, "9.999900001E-6"},
        {"0.1+0.2", CAS_OK, "3/10"},
    };
    set_mode(c, CAS_ANGLE_RAD, 1, 12);
    run_cases(c, exact, sizeof exact / sizeof exact[0]);

    static const eval_case digits6[] = {
        {"pi", CAS_OK, "3.14159"},
        {"1/3", CAS_OK, "0.333333"},
        {"123456789", CAS_OK, "1.23457E8"},
    };
    set_mode(c, CAS_ANGLE_RAD, 0, 6);
    run_cases(c, digits6, sizeof digits6 / sizeof digits6[0]);

    cas_settings s;
    cas_get_settings(c, &s);
    CHECK(s.digits == 6 && s.exact == 0 && s.angle == CAS_ANGLE_RAD, "get_settings roundtrip");

    /* settings.real (COMPLEX = REAL): real odd roots of negative numbers;
     * otherwise no real value, and even roots never have one */
    static const eval_case real[] = {
        {"(-8)^(1/3)", CAS_OK, "-2"},
        {"(-1)^(2/3)", CAS_OK, "1"},
        {"(-8)^(-1/3)", CAS_OK, "-0.5"},
        {"(-32)^(3/5)", CAS_OK, "-8"},
        {"(-8)^(1/6)", CAS_ERR_DOMAIN, "domain"},
    };
    s = (cas_settings){CAS_ANGLE_RAD, 0, 12, 1};
    cas_set_settings(c, &s);
    run_cases(c, real, sizeof real / sizeof real[0]);
    static const eval_case principal[] = {
        {"(-8)^(1/3)", CAS_ERR_DOMAIN, "domain"},
    };
    set_mode(c, CAS_ANGLE_RAD, 0, 12);
    run_cases(c, principal, sizeof principal / sizeof principal[0]);
}

/* Variables and ans: assignment returns the value; an unknown run of
 * letters splits into single-letter products (ab = a*b) but a defined
 * multi-letter name stays whole; function definitions are unsupported.
 * Then robustness: a failed evaluation keeps ans, the nesting limit,
 * output truncation, a stale interrupt request not affecting the next
 * evaluation, and cas_reset() clearing variables and ans. */
static void test_vars(cas_ctx *c)
{
    static const eval_case vars[] = {
        {"a:=3", CAS_OK, "3"},
        {"a*2", CAS_OK, "6"},
        {"2a", CAS_OK, "6"},
        {"ans+1", CAS_OK, "7"},
        {"Ans*2", CAS_OK, "14"},
        {"b:=a+1", CAS_OK, "4"},
        {"ab", CAS_OK, "12"},       /* unknown run splits into a*b */
        {"rate:=2", CAS_OK, "2"},
        {"rate*10", CAS_OK, "20"},  /* defined multi-letter name stays whole */
        {"asin(1)*0+a", CAS_OK, "3"},
        {"x:=2", CAS_OK, "2"},
        {"xsin(0)+x", CAS_OK, "2"}, /* x*sin(0)+x */
        {"x2", CAS_OK, "4"},        /* x*2 */
        {"\xCE\xB8:=1", CAS_OK, "1"}, /* theta */
        {"\xCE\xB8+1", CAS_OK, "2"},
        {"f(x):=x", CAS_ERR_UNSUPPORTED, "assignments"},
    };
    run_cases(c, vars, sizeof vars / sizeof vars[0]);

    /* failed evaluation leaves ans unchanged */
    char out[64];
    cas_eval(c, "41+1", out, sizeof out);
    cas_eval(c, "1/0", out, sizeof out);
    cas_status st = cas_eval(c, "ans", out, sizeof out);
    CHECK(st == CAS_OK && !strcmp(out, "42"), "ans after error = %s", out);

    /* deep nesting: 200 '(' (the closing parentheses are deliberately
     * fewer; the nesting limit must be reported first) */
    char deep[400];
    memset(deep, '(', 200);
    deep[200] = '1';
    memset(deep + 201, ')', 198);
    deep[399] = 0;
    st = cas_eval(c, deep, out, sizeof out);
    CHECK(st == CAS_ERR_SYNTAX && strstr(cas_last_error(c), "nested"), "deep nesting: %d %s", st,
          cas_last_error(c));

    /* output truncation */
    char tiny[4];
    st = cas_eval(c, "1/3", tiny, sizeof tiny);
    CHECK(st == CAS_ERR_OUTPUT_TRUNC, "truncation: %d", st);
    /* a zero-length buffer is never written, also by solve */
    tiny[0] = 'z';
    st = cas_eval(c, "solve(x^2-4,x)", tiny, 0);
    CHECK(st == CAS_ERR_OUTPUT_TRUNC && tiny[0] == 'z', "solve into empty buffer: %d", st);

    /* interrupt flag does not leak into the next evaluation */
    cas_interrupt(c);
    st = cas_eval(c, "integrate(x,x,0,1)", out, sizeof out);
    CHECK(st == CAS_OK && !strcmp(out, "0.5"), "eval after interrupt: %d %s", st, out);

    /* reset clears variables */
    cas_reset(c);
    st = cas_eval(c, "a", out, sizeof out);
    CHECK(st == CAS_ERR_UNDEFINED, "reset clears vars: %d", st);
    st = cas_eval(c, "ans", out, sizeof out);
    CHECK(st == CAS_OK && !strcmp(out, "0"), "reset clears ans: %s", out);
}

/* Compiled functions: evaluation in double, NaN for undefined points
 * (0/0, sqrt(-1), 1/0), variables and the angle mode fixed at compile time,
 * compile errors, several input variables (cas_compile_vars, inputs in
 * array order), other variable names, and the fixed pool of compiled
 * functions (at least 32, freed functions are reused). */
static void test_compile(cas_ctx *c)
{
    cas_fn *f = NULL, *g = NULL;
    char out[32];
    set_mode(c, CAS_ANGLE_RAD, 0, 12);
    CHECK(cas_compile(c, "sin(x)/x", "x", &f) == CAS_OK && f, "compile sin(x)/x: %s", cas_last_error(c));
    if (f) {
        CHECK(fabs(cas_fn_eval(f, 1.0) - sin(1.0)) < 1e-15, "sin(1)/1");
        CHECK(isnan(cas_fn_eval(f, 0.0)), "0/0 -> NAN");
        cas_fn_free(c, f);
    }
    CHECK(cas_compile(c, "x^2-3x+2", "x", &f) == CAS_OK, "compile poly");
    if (f) {
        CHECK(cas_fn_eval(f, 3.0) == 2.0, "poly(3)");
        CHECK(cas_fn_eval(f, -1.0) == 6.0, "poly(-1)");
    }
    CHECK(cas_compile(c, "sqrt(x)+ln(x)", "x", &g) == CAS_OK, "compile sqrt+ln");
    if (g) {
        CHECK(isnan(cas_fn_eval(g, -1.0)), "sqrt(-1) -> NAN");
        CHECK(fabs(cas_fn_eval(g, 4.0) - (2.0 + log(4.0))) < 1e-14, "sqrt(4)+ln(4)");
        cas_fn_free(c, g);
    }
    if (f)
        cas_fn_free(c, f);

    /* variables are baked in at compile time */
    cas_eval(c, "k:=5", out, sizeof out);
    CHECK(cas_compile(c, "k*x+max(x,1)+2!", "x", &f) == CAS_OK, "compile with k");
    if (f) {
        CHECK(cas_fn_eval(f, 2.0) == 14.0, "k*x+max+2! at 2 = %g", cas_fn_eval(f, 2.0));
        cas_fn_free(c, f);
    }
    CHECK(cas_compile(c, "q*x", "x", &f) == CAS_ERR_UNDEFINED && f == NULL, "undefined in compile");
    CHECK(cas_compile(c, "x+", "x", &f) == CAS_ERR_SYNTAX, "syntax in compile");
    {
        const char *xy[] = {"x", "y", "k"};
        CHECK(cas_compile_vars(c, "x^2+y^2-k*x", xy, 3, &f) == CAS_OK && f, "compile_vars");
        if (f) {
            double v[3] = {2, 3, 1};
            CHECK(cas_fn_eval_vars(f, v) == 11.0, "compile_vars value %g", cas_fn_eval_vars(f, v));
            cas_fn_free(c, f);
        }
    }
    CHECK(isnan(cas_fn_eval(NULL, 1.0)) && isnan(cas_fn_eval_vars(NULL, NULL)), "NULL fn -> NAN");
    CHECK(cas_compile(c, "diff(x,x,1)", "x", &f) == CAS_ERR_UNSUPPORTED, "command not compilable");
    CHECK(cas_compile(c, "1/x", "x", &f) == CAS_OK, "compile 1/x");
    if (f) {
        CHECK(isnan(cas_fn_eval(f, 0.0)), "1/0 -> NAN");
        /* 1/1e-320 (a subnormal divisor) overflows a double: the result must
         * be NAN or finite, never infinite */
        CHECK(isnan(cas_fn_eval(f, 1e-320)) || isfinite(cas_fn_eval(f, 1e-320)), "1/subnormal overflow -> not inf");
        cas_fn_free(c, f);
    }
    /* other variable names */
    CHECK(cas_compile(c, "t^2", "t", &f) == CAS_OK, "compile in t");
    if (f) {
        CHECK(cas_fn_eval(f, 3.0) == 9.0, "t^2(3)");
        cas_fn_free(c, f);
    }

    /* degree mode baked in */
    set_mode(c, CAS_ANGLE_DEG, 0, 12);
    CHECK(cas_compile(c, "sin(x)", "x", &f) == CAS_OK, "compile deg");
    set_mode(c, CAS_ANGLE_RAD, 0, 12);
    if (f) {
        CHECK(cas_fn_eval(f, 90.0) == 1.0, "deg sin(90) = %g", cas_fn_eval(f, 90.0));
        cas_fn_free(c, f);
    }

    /* pool exhaustion and reuse */
    cas_fn *pool[80];
    int n = 0;
    while (n < 80 && cas_compile(c, "x", "x", &pool[n]) == CAS_OK)
        n++;
    CHECK(n >= 32 && n < 80, "fn pool size %d", n);
    for (int i = 0; i < n; i++)
        cas_fn_free(c, pool[i]);
    CHECK(cas_compile(c, "x", "x", &f) == CAS_OK, "compile after free");
    if (f)
        cas_fn_free(c, f);
}

/* cas_init(): a too-small arena is refused, an unaligned one is accepted,
 * and cas_mem() reports consistent usage. The context is abandoned (the
 * stub needs no teardown). */
static void test_init(void)
{
    static unsigned char small[64];
    CHECK(cas_init(small, sizeof small) == NULL, "too-small arena rejected");
    cas_ctx *c = cas_init(arena + 3, sizeof arena - 3); /* unaligned arena */
    CHECK(c != NULL, "init unaligned");
    cas_mem_stats m;
    cas_mem(c, &m);
    CHECK(m.arena_size == sizeof arena - 3 && m.used > 0 && m.used <= m.arena_size && m.peak >= m.used,
          "mem stats %zu %zu %zu", m.arena_size, m.used, m.peak);
}

/* cas_restore(): the result is the first failing record's real status, not
 * always CAS_ERR_SYNTAX (a full variable table is CAS_ERR_MEMORY). */
static void test_restore_status(void)
{
    static unsigned char ar[sizeof arena];
    cas_ctx *c = cas_init(ar, sizeof ar);
    CHECK(c != NULL, "init for restore");
    if (!c)
        return;
    static char rec[64 * 80];
    size_t n = 0;
    for (int i = 0; i < 70; i++)
        n += (size_t)snprintf(rec + n, sizeof rec - n, "v rv%d %d\n", i, i);
    CHECK(cas_restore(c, rec, n) == CAS_ERR_MEMORY, "restore with full table: memory status");
    cas_reset(c);
    const char *bad = "f g(x) x^2\nv a 1\n";
    CHECK(cas_restore(c, bad, strlen(bad)) == CAS_ERR_SYNTAX, "unknown record: syntax status");
}

int main(void)
{
    test_init();
    test_restore_status();
    cas_ctx *c = cas_init(arena, sizeof arena);
    if (!c) {
        printf("FAIL: cas_init\n");
        return 1;
    }
    test_eval(c);
    test_modes(c);
    test_vars(c);
    test_compile(c);
    printf("test_cas_stub: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
