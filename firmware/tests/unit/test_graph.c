/* Graphing: numeric methods (gr_num.c), graph modes and parameters (gr_model.c),
 * multi-variable compilation (cas_compile_vars), implicit plotting (gr_plot.c).
 *
 * The numeric part needs no UI: plain C test functions with known roots,
 * extrema, derivatives and integrals. The model part sets up just enough of
 * the application by hand (the global app state `g`, the token table, the
 * graphics layer drawing into a local framebuffer, a CAS in an 8 MB static
 * arena, a [-10,10]x[-10,10] window) instead of running app_init(), then
 * drives the graph model through the same calls the Y= and GRAPH screens use.
 * Built and run by `make test` (tests/unit/test_*.c, linked with the
 * simulator library and the configured CAS). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "graph.h"

static int fails, checks;
#define CHECK(c)                                                              \
    do {                                                                      \
        checks++;                                                             \
        if (!(c)) {                                                           \
            fails++;                                                          \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)
#define NEAR(a, b, tol) CHECK(fabs((a) - (b)) <= (tol))

/* ------------------------------------------------------------ numerics */
/* Test functions (gn_fn signature) with known answers: x^2-2 (root sqrt 2,
 * counts its evaluations), cos x - x (Dottie number 0.739085...), a double
 * root at 1 that touches zero without a sign change, a pole at 0, x^3-3x
 * (roots 0 and +-sqrt 3, minimum at 1, maximum at -1), e^x, sqrt x (infinite
 * slope at 0), and a damped oscillation for the integrator. */
static int nevals;
static double f_sq2(double x, void *u) { (void)u; nevals++; return x * x - 2; }
static double f_cos(double x, void *u) { (void)u; return cos(x) - x; }
static double f_touch(double x, void *u) { (void)u; return (x - 1) * (x - 1); }
static double f_pole(double x, void *u) { (void)u; return 1 / x; }
static double f_poly(double x, void *u) { (void)u; return x * x * x - 3 * x; }
static double f_neg_poly(double x, void *u) { return -f_poly(x, u); }
static double f_exp(double x, void *u) { (void)u; return exp(x); }
static double f_sqrt(double x, void *u) { (void)u; return sqrt(x); }
static double f_osc(double x, void *u) { (void)u; return sin(10 * x) * exp(-x); }

/* Root finding (Brent), nearest-root search, minimisation, numerical
 * derivatives and adaptive Gauss-Kronrod integration (app/gr_num.c). The
 * damped-oscillation reference value is the closed form
 *   int_0^10 e^-x sin(10x) dx = (10 - e^-10 (10 cos 100 + sin 100)) / 101
 * (from int e^(ax) sin(bx) dx = e^(ax) (a sin bx - b cos bx) / (a^2 + b^2)
 * with a = -1, b = 10). Reversed bounds give the negated integral and equal
 * bounds exactly 0. */
static void test_numerics(void)
{
    double r;
    nevals = 0;
    CHECK(gn_root(f_sq2, NULL, 0, 2, &r));
    NEAR(r, sqrt(2), 1e-14);
    CHECK(nevals < 20); /* Brent converges superlinearly */
    CHECK(!gn_root(f_sq2, NULL, 2, 3, &r)); /* no sign change */
    CHECK(gn_root(f_cos, NULL, 0, 1, &r));
    NEAR(r, 0.7390851332151607, 1e-14);
    /* nearest to the guess among several roots */
    CHECK(gn_root_near(f_poly, NULL, -3, 3, 1.5, 64, &r));
    NEAR(r, sqrt(3), 1e-12);
    CHECK(gn_root_near(f_poly, NULL, -3, 3, -0.2, 64, &r));
    NEAR(r, 0, 1e-12);
    /* touching zero (no sign change) */
    CHECK(gn_root_near(f_touch, NULL, -2, 3, 0, 64, &r));
    NEAR(r, 1, 1e-6);
    /* a pole is not a zero */
    CHECK(!gn_root_near(f_pole, NULL, -1, 1.3, 0, 64, &r));
    /* minimum / maximum */
    CHECK(gn_min(f_poly, NULL, 0, 3, 64, &r));
    NEAR(r, 1, 1e-7);
    CHECK(gn_min(f_neg_poly, NULL, -3, 0, 64, &r));
    NEAR(r, -1, 1e-7);
    CHECK(gn_min(f_poly, NULL, 2, 3, 64, &r)); /* at the boundary */
    NEAR(r, 2, 1e-6);
    /* derivatives */
    NEAR(gn_deriv(f_exp, NULL, 1, 1), exp(1), 1e-9); /* last argument: derivative order */
    NEAR(gn_deriv(f_exp, NULL, 1, 2), exp(1), 1e-6);
    NEAR(gn_deriv(f_poly, NULL, 2, 1), 9, 1e-9);
    /* integrals */
    double err;
    NEAR(gn_integrate(f_exp, NULL, 0, 1, 1e-12, &err), exp(1) - 1, 1e-12);
    NEAR(gn_integrate(f_sq2, NULL, 0, 1, 1e-12, NULL), 1.0 / 3 - 2, 1e-12);
    NEAR(gn_integrate(f_sqrt, NULL, 0, 1, 1e-10, NULL), 2.0 / 3, 1e-8); /* endpoint singularity */
    NEAR(gn_integrate(f_osc, NULL, 0, 10, 1e-10, NULL), (10 - exp(-10) * (10 * cos(100) + sin(100))) / 101, 1e-9);
    NEAR(gn_integrate(f_exp, NULL, 1, 0, 1e-12, NULL), 1 - exp(1), 1e-12);
    CHECK(gn_integrate(f_exp, NULL, 2, 2, 1e-12, NULL) == 0);
}

/* ------------------------------------------------------------ model */
static uint8_t arena[8u << 20];
static uint16_t fb[LCD_WIDTH * LCD_HEIGHT];

/* Set Y= function i (the FUNC-mode editors in g.f[]) and mark the
 * compiled functions stale. */
static void set_func(int i, const char *text)
{
    ed_set_text(&g.f[i].ed, text);
    funcs_invalidate();
}

/* Set expression `part` of graph entry i in the current graph mode (e.g.
 * PAR: 0 = x(t), 1 = y(t); SEQ: 0 = u(n), 1 = initial values) by going
 * through an editor, as the Y= screen does. */
static void set_entry(int i, int part, const char *text)
{
    static editor_t e;
    ed_set_text(&e, text);
    CHECK(gtext_from_editor(&gx_entry(i)->ex[part], &e));
    funcs_invalidate();
}

/* Number of framebuffer pixels of colour c. */
static int count_color(uint16_t c)
{
    int n = 0;
    for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++)
        n += fb[i] == c;
    return n;
}

/* Graph modes FUNC (with slider parameters), PAR, POL, SEQ and IMPL: what
 * compiles, error messages for incomplete entries, evaluated values, and an
 * implicit circle actually plotted at the right radius. */
static void test_model(void)
{
    memset(&g, 0, sizeof g);
    tok_init();
    gfx_init(fb);
    g.cas = cas_init(arena, sizeof arena);
    CHECK(g.cas != NULL);
    cas_get_settings(g.cas, &g.cs);
    g.xmin = -10, g.xmax = 10, g.ymin = -10, g.ymax = 10, g.xscl = g.yscl = 1;
    g.tbldelta = 1;
    yvars_invalidate();
    graphx_init();
    for (int i = 0; i < NFUNC; i++)
        g.f[i].enabled = true;

    /* FUNC with parameters: a and b become inputs of the compiled function */
    set_func(0, "a*x^2+b");
    gx_compile();
    CHECK(gx_plottable(0));
    CHECK(gx_nparams_shown() == 2);
    NEAR(gx_f(0, 2), 5, 1e-12); /* defaults a = b = 1 */
    int pa = gx_param_shown(0);
    CHECK(gx.prm[pa].name[0] == 'a');
    gx_param_set(pa, 3);
    NEAR(gx_f(0, 2), 13, 1e-12); /* no recompilation needed */
    gx_param_set(pa, 3.04); /* snapped to the step 0.1 (default range -10..10) */
    NEAR(gx.prm[pa].val, 3.0, 1e-12);
    gx_param_set(pa, 99); /* clamped to max */
    NEAR(gx.prm[pa].val, 10, 1e-12);
    /* a defined CAS variable gives the start value, and follows changes */
    char out[64];
    cas_eval(g.cas, "b:=4", out, sizeof out);
    funcs_invalidate();
    gx_compile();
    NEAR(gx_f(0, 0), 4, 1e-12);
    cas_eval(g.cas, "b:=-2", out, sizeof out);
    CHECK(gx_params_sync());
    NEAR(gx_f(0, 0), -2, 1e-12);
    gx_param_set(pa, 2);
    gx_param_commit(pa);
    cas_eval(g.cas, "a", out, sizeof out);
    CHECK(!strcmp(out, "2"));
    /* x, e and i are not parameters; f(x) calls are not either */
    set_func(0, "e^x+x");
    gx_compile();
    CHECK(gx_nparams_shown() == 0);

    /* PAR */
    gx_set_mode(GM_PAR);
    set_entry(0, 0, "3cos(t)");
    set_entry(0, 1, "2sin(t)");
    set_entry(1, 0, "t"); /* y missing */
    gx_compile();
    CHECK(gx_plottable(0));
    CHECK(!gx_plottable(1));
    CHECK(strstr(gx_error(1), "both") != NULL);
    double x, y;
    CHECK(gx_par(0, M_PI / 2, &x, &y));
    NEAR(x, 0, 1e-12);
    NEAR(y, 2, 1e-12);

    /* POL: r(θ) */
    gx_set_mode(GM_POL);
    set_entry(0, 0, "2");
    gx_compile();
    CHECK(gx_par(0, M_PI, &x, &y));
    NEAR(x, -2, 1e-12);
    NEAR(y, 0, 1e-12);

    /* SEQ: geometric, Fibonacci, explicit; missing initial values.
     * Sequences start at nMin = 1: u1 = 2, 3, 4.5, ...; u2 = 1, 1, 2, ...,
     * u2(10) = 55; u3(7) = 49; u4 needs two initial values but has one;
     * u5(n) = u1(n-1) + u5(n-1) = 0, 2, 5. gx_seq_step(0, 4) applies u1's
     * recursion once to the value 4 (1.5 * 4, used by web plots). */
    gx_set_mode(GM_SEQ);
    set_entry(0, 0, "1.5u1(n-1)");
    set_entry(0, 1, "2");
    set_entry(1, 0, "u2(n-1)+u2(n-2)");
    set_entry(1, 1, "{1,1}");
    set_entry(2, 0, "n^2");
    set_entry(3, 0, "u4(n-2)");
    set_entry(3, 1, "1");
    set_entry(4, 0, "u1(n-1)+u5(n-1)"); /* cross reference */
    set_entry(4, 1, "0");
    gx_compile();
    CHECK(gx_plottable(0) && gx_plottable(1) && gx_plottable(2) && gx_plottable(4));
    CHECK(!gx_plottable(3));
    CHECK(strstr(gx_error(3), "2 initial values") != NULL);
    NEAR(gx_seq(0, 1), 2, 1e-12);
    NEAR(gx_seq(0, 3), 4.5, 1e-12);
    NEAR(gx_seq(1, 10), 55, 1e-12);
    NEAR(gx_seq(2, 7), 49, 1e-12);
    NEAR(gx_seq(4, 3), 2 + 3, 1e-12); /* 0, 0+2, 2+3 */
    CHECK(gx_seq_depth(1) == 2 && gx_seq_selfonly(0) && !gx_seq_selfonly(1));
    NEAR(gx_seq_step(0, 4), 6, 1e-12);
    /* gx_seq resumes its run for increasing n and restarts for a smaller n,
     * another nMin or a recompilation */
    NEAR(gx_seq(1, 11), 89, 1e-12);
    NEAR(gx_seq(1, 12), 144, 1e-12);
    NEAR(gx_seq(1, 5), 5, 1e-12);
    NEAR(gx_seq(0, 2), 3, 1e-12);
    CHECK(isnan(gx_seq(1, 0)));
    gx.nmin = 2; /* u2 starts over at n = 2: 1, 1, 2, ... */
    NEAR(gx_seq(1, 4), 2, 1e-12);
    gx.nmin = 1;
    NEAR(gx_seq(1, 4), 3, 1e-12);
    set_entry(1, 0, "u2(n-1)+u2(n-2)+1");
    gx_compile();
    NEAR(gx_seq(1, 4), 5, 1e-12); /* 1, 1, 3, 5 */

    /* IMPL: relations, compiled in x and y; gx_impl() returns lhs - rhs
     * (an expression without a relation, x*y, means x*y = 0) */
    gx_set_mode(GM_IMPL);
    set_entry(0, 0, "x^2+y^2=16");
    set_entry(1, 0, "y<x");
    set_entry(2, 0, "x*y");
    set_entry(3, 0, "x==y"); /* "==" is one relation */
    gx_compile();
    CHECK(gx_plottable(0) && gx_plottable(1) && gx_plottable(2) && gx_plottable(3));
    CHECK(gx_rel(0) == REL_EQ && gx_rel(1) == REL_LT && gx_rel(2) == REL_EQ && gx_rel(3) == REL_EQ);
    NEAR(gx_impl(3, 2, 5), -3, 1e-12);
    gx_entry(3)->enabled = false;
    NEAR(gx_impl(0, 4, 0), 0, 1e-12);
    NEAR(gx_impl(0, 1, 1), -14, 1e-12);
    NEAR(gx_impl(1, 1, 3), 2, 1e-12);
    /* the circle is drawn: pixels of entry 0's colour lie on radius 4
     * (marching squares; 0.25 graph units is a few pixels at this window) */
    gx_entry(1)->enabled = gx_entry(2)->enabled = false;
    gfx_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, C_WHITE);
    gr_plot_all();
    uint16_t c = func_colors[gx_entry(0)->color];
    CHECK(count_color(c) > 500);
    int bad = 0;
    for (int py = 0; py < GH; py++)
        for (int px = 0; px < GW; px++)
            if (fb[(GY0 + py) * LCD_WIDTH + GX0 + px] == c) {
                double r = hypot(gv_px_to_x(px), gv_py_to_y(py));
                bad += fabs(r - 4) > 0.25;
            }
    CHECK(bad == 0);

    /* DE: y' = f(x, y) compiled in x and y, with initial points (a bare y0 means
     * x0 = 0). The RK4 solutions against closed forms: y' = y from (0, 1) is
     * e^x; y' = x - y from (0, -2) is x - 1 - e^-x and from (2, 1) the line
     * x - 1; y' = -x/y from (0, 3) is the circle of radius 3, which ends at its
     * vertical tangent x = 3 (no jump across y = 0). Curves are numbered typed
     * points first, then added ones; a bad initial point is the entry's error. */
    gx_set_mode(GM_DE);
    set_entry(0, 0, "y");
    set_entry(0, 1, "1");
    set_entry(1, 0, "x-y");
    set_entry(1, 1, "(0,-2),[2,1]");
    set_entry(2, 0, "-x/y");
    set_entry(2, 1, "{(0,3)}");
    set_entry(3, 0, "x");
    set_entry(3, 1, "(0,1");
    gx_compile();
    CHECK(gx_plottable(0) && gx_plottable(1) && gx_plottable(2) && !gx_plottable(3));
    CHECK(gx_error(3)[0] != 0);
    CHECK(gx_de_ncurves() == 4);
    NEAR(gx_impl(1, 3, 1), 2, 1e-12);
    NEAR(gx_de_solve(0, 1), M_E, 1e-5);
    NEAR(gx_de_solve(0, -2), exp(-2), 1e-5);
    NEAR(gx_de_solve(1, 2), 1 - exp(-2), 1e-5);
    NEAR(gx_de_solve(2, 5), 4, 1e-5);
    NEAR(gx_de_solve(3, 2), sqrt(5), 1e-4);
    NEAR(gx_de_solve(3, -2.9), sqrt(9 - 2.9 * 2.9), 1e-3);
    CHECK(isnan(gx_de_solve(3, 3.05)) && isnan(gx_de_solve(3, -3.05)));
    int k = gx_de_add(0, 0, 2); /* y' = y through (0, 2): 2e^x */
    CHECK(k == 4 && gx.ndtap == 1 && gx_de_ncurves() == 5);
    NEAR(gx_de_solve(k, 1), 2 * M_E, 1e-5);
    gx_entry(0)->enabled = false; /* an added curve of a switched-off entry is hidden, not removed */
    funcs_invalidate();
    gx_compile();
    CHECK(gx_de_ncurves() == 3 && gx.ndtap == 1);
    gx_de_clear_taps(-1);
    CHECK(gx.ndtap == 0);
    gfx_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, C_WHITE);
    gr_plot_all();
    CHECK(count_color(func_colors[gx_entry(2)->color]) > 300);
    gx_entry(0)->enabled = true;

    /* overlay helpers map graph coordinates like the plot */
    CHECK(gov_px(g.xmin) == GX0 && gov_px(g.xmax) == GX0 + GW - 1);
    CHECK(gov_py(g.ymax) == GY0 && gov_py(g.ymin) == GY0 + GH - 1);
}

int main(void)
{
    test_numerics();
    test_model();
    printf("test_graph: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
