/* Host unit tests for 2D math (app/m2d_*.c) and themes (app/theme.c).
 *
 * Covers the pieces of textbook display and entry that work without a
 * screen: repeating-decimal input rewriting and output detection
 * (m2d_repdec.c), Textbook templates in the token editor and their Classic /
 * CAS text forms (m2d_edit.c), the linear-text -> box-tree parser and box
 * layout (m2d_parse.c, m2d_box.c), and the dark-theme colour map and large
 * font (theme.c). main() creates a CAS in an 8 MB malloc'd arena and
 * initialises the token table and the 2D-math marker tokens (m2d_init).
 * Built and run by `make test` (tests/unit/test_*.c, linked with the
 * simulator library and the configured CAS); with CAS=stub the exact
 * results are skipped (CHECK_EVAL). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include "gfx.h"
#include "hal.h"
#include "m2d.h"
#include "m2d_box.h"
#include "theme.h"

static int fails, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if (!(c)) {                                                               \
            fails++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)
#define CHECK_STR(a, b)                                                             \
    do {                                                                            \
        checks++;                                                                   \
        if (strcmp((a), (b))) {                                                     \
            fails++;                                                                \
            fprintf(stderr, "%s:%d: '%s' != '%s'\n", __FILE__, __LINE__, (a), (b)); \
        }                                                                           \
    } while (0)

static cas_ctx *cas;
/* exact results only with the real CAS (the stub is numeric) */
#define CHECK_EVAL(in, want)               \
    do {                                   \
        if (strcmp(CAS_IMPL, "stub"))      \
            CHECK_STR(eval(in), want);     \
    } while (0)

/* Evaluate with the test's CAS; returns a static buffer ("ERR" on any
 * error), overwritten by the next call. */
static const char *eval(const char *in)
{
    static char out[512];
    if (cas_eval(cas, in, out, sizeof out) != CAS_OK)
        snprintf(out, sizeof out, "ERR");
    return out;
}

/* ------------------------------------------------------------ repeating decimals */
/* Input: repdec_rewrite() turns a repeating decimal into an exact fraction
 * using the classic rule
 *   I.N(R) = (INR - IN) / (99...9 00...0)   (|R| nines, |N| zeros)
 * where INR and IN are the digit strings without the point, e.g.
 * 1.2(34) = (1234 - 12) / 990 = 611/495. Digits after a letter (x.(3),
 * a2.(3)) or a call such as 2(3) are not decimals and are left alone.
 * Output: repdec_format() recognises a full-precision (12 significant
 * digits here) decimal that is a short repeating fraction, allowing the
 * rounded last digit (0.166666666667 = 0.1(6)); terminating decimals,
 * irrationals, short user-typed decimals and exponent forms are refused.
 * repdec_apply() rewrites every such number inside an expression. */
static void test_repdec(void)
{
    char b[256];
    CHECK(repdec_rewrite("0.(3)", b, sizeof b));
    CHECK_STR(b, "((3-0)/9)");
    CHECK_EVAL(b, "1/3");
    CHECK(repdec_rewrite("1.2(34)+x", b, sizeof b));
    CHECK_STR(b, "((1234-12)/990)+x");
    CHECK_EVAL(b, "x+611/495");
    CHECK(repdec_rewrite("f(0.(142857))*.(9)", b, sizeof b));
    CHECK_STR(b, "f(((142857-0)/999999))*((9-0)/9)");
    CHECK(!repdec_rewrite("x.(3)+2(3)+sin(0.5)", b, sizeof b));
    CHECK(!repdec_rewrite("a2.(3)", b, sizeof b));

    CHECK(repdec_format("0.142857142857", 12, b, sizeof b));
    CHECK_STR(b, "0.(142857)");
    CHECK(repdec_format("0.333333333333", 12, b, sizeof b));
    CHECK_STR(b, "0.(3)");
    CHECK(repdec_format("0.166666666667", 12, b, sizeof b));
    CHECK_STR(b, "0.1(6)");
    CHECK(repdec_format("2.33333333333", 12, b, sizeof b));
    CHECK_STR(b, "2.(3)");
    CHECK(repdec_format("0.0909090909091", 12, b, sizeof b));
    CHECK_STR(b, "0.(09)");
    CHECK(repdec_format("12.4166666667", 12, b, sizeof b));
    CHECK_STR(b, "12.41(6)");
    CHECK(!repdec_format("1.41421356237", 12, b, sizeof b));
    CHECK(!repdec_format("3.14159265359", 12, b, sizeof b));
    CHECK(!repdec_format("0.5", 12, b, sizeof b));
    CHECK(!repdec_format("0.125", 12, b, sizeof b));
    CHECK(!repdec_format("0.333", 12, b, sizeof b)); /* not full precision: user's own decimal */
    CHECK(!repdec_format("1.5e-7", 12, b, sizeof b));
    /* a long period: either refused or at least a bounded result */
    CHECK(!repdec_format("0.0000999900009999", 12, b, sizeof b) || strlen(b) < 32);

    repdec_apply("x+0.333333333333*y-0.142857142857", b, sizeof b, 12);
    CHECK_STR(b, "x+0.(3)*y-0.(142857)");
    repdec_apply("1.5ᴇ-7+x2+0.25", b, sizeof b, 12);
    CHECK_STR(b, "1.5ᴇ-7+x2+0.25");
}

/* ------------------------------------------------------------ templates */
/* Classic (linear, re-enterable) text of an editor line. */
static void disp(const editor_t *e, char *b, size_t n) { ed_display(e, b, n); }

/* Textbook templates: marker tokens in the ordinary token editor
 * (OPEN slot SEP slot ... CLOSE). ed_right() moves to the next slot or out
 * of the box. Checked: the Classic display text and the CAS text of each
 * template, explicit multiplication around boxes, session serialisation,
 * that the linear form evaluates the same and parses back to the same boxes
 * (m2d_set_linear), DEL on a box, the key bindings that open boxes (x^y,
 * sqrt, SHIFT sqrt, SHIFT -, SHIFT LOG) with Classic typing precedence, and
 * m2d_flatten() for switching to Classic. */
static void test_templates(void)
{
    editor_t e;
    char d[1024], c[1024];
    ed_clear(&e);
    /* (1+x)/2 + 3^2 */
    CHECK(m2d_insert_template(&e, TPL_FRAC, 0, 0));
    CHECK(m2d_has_templates(&e));
    ed_insert_text(&e, "1+x", NULL);
    ed_right(&e); /* to the denominator */
    ed_insert_text(&e, "2", NULL);
    ed_right(&e); /* out */
    ed_insert_text(&e, "+3", NULL);
    m2d_insert_template(&e, TPL_POW, 0, 0);
    ed_insert_text(&e, "2", NULL);
    disp(&e, d, sizeof d);
    ed_cas(&e, c, sizeof c);
    CHECK_STR(d, "(1+x)/2+3^2");
    CHECK_STR(c, "((1+x)/(2))+3^(2)");
    CHECK_EVAL(c, "x/2+19/2");

    /* session save/restore keeps the boxes */
    char ser[2048];
    ed_serialize(&e, ser, sizeof ser);
    editor_t r;
    ed_deserialize(&r, ser);
    ed_cas(&r, c, sizeof c);
    CHECK_STR(c, "((1+x)/(2))+3^(2)");
    CHECK(r.len == e.len);

    /* implicit multiplication around boxes */
    ed_clear(&e);
    ed_insert_text(&e, "x", NULL);
    m2d_insert_template(&e, TPL_SQRT, 0, 0);
    ed_insert_text(&e, "2", NULL);
    ed_right(&e);
    ed_insert_text(&e, "y", NULL);
    disp(&e, d, sizeof d);
    ed_cas(&e, c, sizeof c);
    CHECK_STR(d, "x*√(2)y");
    CHECK_STR(c, "x*sqrt(2)*y");
    CHECK_EVAL(c, "sqrt(2)*x*y");

    ed_clear(&e);
    ed_insert_text(&e, "2", NULL);
    m2d_insert_template(&e, TPL_FRAC, 0, 0);
    ed_insert_text(&e, "1", NULL);
    ed_right(&e);
    ed_insert_text(&e, "3", NULL);
    ed_right(&e);
    ed_insert_text(&e, "x", NULL);
    disp(&e, d, sizeof d);
    ed_cas(&e, c, sizeof c);
    CHECK_STR(d, "2*(1/3)x");
    CHECK_EVAL(c, "2*x/3");

    /* del: the CAS menu's ∇ templates are single tokens; ∇² before a
     * fraction is an operator, not a factor (no "*") */
    ed_clear(&e);
    ed_insert_text(&e, "∇²", NULL);
    CHECK(e.len == 1);
    m2d_insert_template(&e, TPL_FRAC, 0, 0);
    ed_insert_text(&e, "1", NULL);
    ed_right(&e);
    ed_insert_text(&e, "x", NULL);
    disp(&e, d, sizeof d);
    ed_cas(&e, c, sizeof c);
    CHECK_STR(d, "∇²(1/x)");
    CHECK_STR(c, "∇²((1)/(x))"); /* (laplacian is in the advanced module, not linked here) */
    ed_clear(&e);
    ed_insert_text(&e, "∇", NULL);
    m2d_insert_template(&e, TPL_FRAC, 0, 0);
    ed_insert_text(&e, "1", NULL);
    ed_right(&e);
    ed_insert_text(&e, "x", NULL);
    ed_cas(&e, c, sizeof c);
    CHECK_STR(c, "∇((1)/(x))");
    CHECK_EVAL(c, "[-1/x^2]");
    ed_clear(&e);
    ed_insert_text(&e, "∇·[", "]");
    ed_insert_text(&e, "x,y", NULL);
    ed_cas(&e, c, sizeof c);
    CHECK(e.len == 6);
    CHECK_STR(c, "∇·[x,y]");
    CHECK_EVAL(c, "2");

    /* every template evaluates: slot texts in the order the cursor visits
     * them (e.g. integral: lower, upper, integrand, variable; matrix: row
     * by row), r/cl are the matrix dimensions */
    static const struct {
        int type, r, cl;
        const char *slots[9];
        const char *cas_result;
    } t[] = {
        {TPL_NROOT, 0, 0, {"3", "8"}, "2"},
        {TPL_ABS, 0, 0, {"-5"}, "5"},
        {TPL_LOGB, 0, 0, {"2", "8"}, "3"},
        {TPL_DERIV, 0, 0, {"x", "x^3"}, "3*x^2"},
        {TPL_INTEG, 0, 0, {"0", "1", "x^2", "x"}, "1/3"},
        {TPL_INDEF, 0, 0, {"2*x", "x"}, "x^2"},
        {TPL_SUM, 0, 0, {"k", "1", "10", "k"}, "55"},
        {TPL_PROD, 0, 0, {"k", "1", "5", "k"}, "120"},
        {TPL_MATRIX, 2, 2, {"1", "2", "3", "4"}, "[[1,2],[3,4]]"},
    };
    for (size_t k = 0; k < sizeof t / sizeof t[0]; k++) {
        ed_clear(&e);
        CHECK(m2d_insert_template(&e, t[k].type, t[k].r, t[k].cl));
        for (int s = 0; s < 9 && t[k].slots[s]; s++) {
            if (s)
                ed_right(&e);
            ed_insert_text(&e, t[k].slots[s], NULL);
        }
        ed_cas(&e, c, sizeof c);
        CHECK_EVAL(c, t[k].cas_result);
        /* the linear form evaluates to the same */
        disp(&e, d, sizeof d);
        editor_t lin;
        ed_set_text(&lin, d);
        ed_cas(&lin, c, sizeof c);
        CHECK_EVAL(c, t[k].cas_result);
        /* and comes back as the same boxes */
        editor_t back;
        m2d_set_linear(&back, d);
        char d2[1024];
        disp(&back, d2, sizeof d2);
        CHECK_STR(d2, d);
    }

    /* DEL: an empty box disappears, a filled one becomes linear text */
    ed_clear(&e);
    m2d_insert_template(&e, TPL_SQRT, 0, 0);
    e.cur = 0;
    CHECK(m2d_edit_key(&e, KEY_DEL, MOD_NONE));
    CHECK(e.len == 0);
    m2d_insert_template(&e, TPL_FRAC, 0, 0);
    ed_insert_text(&e, "1", NULL);
    ed_right(&e);
    ed_insert_text(&e, "x+1", NULL);
    e.cur = 0;
    CHECK(m2d_edit_key(&e, KEY_DEL, MOD_NONE));
    CHECK(!m2d_has_templates(&e));
    disp(&e, d, sizeof d);
    CHECK_STR(d, "1/(x+1)");

    /* Classic precedence while typing an exponent: 2^3+1 */
    ed_clear(&e);
    ed_insert_text(&e, "2", NULL);
    CHECK(m2d_edit_key(&e, KEY_POW, MOD_NONE));
    ed_insert_text(&e, "3", NULL);
    CHECK(!m2d_edit_key(&e, KEY_ADD, MOD_NONE)); /* leaves the box, the caller inserts + */
    ed_insert_text(&e, "+1", NULL);
    disp(&e, d, sizeof d);
    CHECK_STR(d, "2^3+1");

    /* ")" closes √( like Classic typing */
    ed_clear(&e);
    CHECK(m2d_edit_key(&e, KEY_SQRT, MOD_NONE));
    ed_insert_text(&e, "2", NULL);
    CHECK(m2d_edit_key(&e, KEY_RPAREN, MOD_NONE));
    CHECK(e.cur == e.len);
    disp(&e, d, sizeof d);
    CHECK_STR(d, "√(2)");

    /* SHIFT ∛: 3rd root box, the cursor in the radicand */
    ed_clear(&e);
    CHECK(m2d_edit_key(&e, KEY_SQRT, MOD_SHIFT));
    ed_insert_text(&e, "8", NULL);
    disp(&e, d, sizeof d);
    CHECK_STR(d, "8^(1/3)");

    /* SHIFT −: |x| box */
    ed_clear(&e);
    CHECK(m2d_edit_key(&e, KEY_SUB, MOD_SHIFT));
    ed_insert_text(&e, "-2", NULL);
    disp(&e, d, sizeof d);
    CHECK_STR(d, "abs(-2)");

    /* SHIFT LOG: 10 with an exponent box that ')' closes, linear form as in Classic */
    ed_clear(&e);
    CHECK(m2d_edit_key(&e, KEY_LOG, MOD_SHIFT));
    ed_insert_text(&e, "3", NULL);
    CHECK(m2d_edit_key(&e, KEY_RPAREN, MOD_NONE));
    disp(&e, d, sizeof d);
    CHECK_STR(d, "10^(3)");
    CHECK_EVAL("10^(3)", "1000");

    /* Classic: boxes flatten to text */
    ed_clear(&e);
    m2d_insert_template(&e, TPL_LOGB, 0, 0);
    ed_insert_text(&e, "2", NULL);
    ed_right(&e);
    ed_insert_text(&e, "8", NULL);
    m2d_flatten(&e);
    CHECK(!m2d_has_templates(&e));
    disp(&e, d, sizeof d);
    CHECK_STR(d, "log(8,2)");

    /* linear -> boxes: the CAS text of the boxed editor evaluates the same
     * as the CAS text of the plain linear editor (with the stub CAS this
     * compares numeric results) */
    static const char *const lin[] = {"(1+x)/2+3^(2√(5))", "x^2-2", "√(x+1)/(2x)", "-1/2", "a/b/c", "sin(x)/2",
                                      "e^(-x^2)", "log(8,2)+diff(x^3,x)", "2(3)+[1,2]"};
    for (size_t k = 0; k < sizeof lin / sizeof lin[0]; k++) {
        editor_t a;
        m2d_set_linear(&a, lin[k]);
        ed_cas(&a, c, sizeof c);
        char ref[1024];
        editor_t b2;
        ed_set_text(&b2, lin[k]);
        ed_cas(&b2, ref, sizeof ref);
        char r1[512];
        snprintf(r1, sizeof r1, "%s", eval(c));
        CHECK_STR(r1, eval(ref));
    }
}

/* ------------------------------------------------------------ 2D layout */
/* m2d_parse() on typical CAS outputs and entries must succeed and produce a
 * box tree that measures to a non-empty size and draws (text log off, as the
 * home screen does); malformed text must be refused (the caller then shows
 * plain text). Then a few tree shapes (the root is a row whose child is the
 * structure: x^(1/3) is a cube-root box, a matrix box keeps its column
 * count in aux), heights, pool overflow, and m2d_layout_editor(), which
 * lays out a Textbook editor line and also returns its slot rows (for a
 * fraction: the main line, numerator and denominator). */
static void test_layout(void)
{
    static bx_t store[400];
    bxpool_t p;
    static const char *const ok[] = {
        "(x-1)*(x+1)", "x/2+1/3", "√(2)/2", "[-√(2),√(2)]", "[[-2,1],[3/2,-1/2]]", "1/10+7*i/10", "abs(x)",
        "exp(2*x)", "x^(1/3)", "log(8,2)", "n*(n+1)/2", "diff(f(x),x)", "1.5ᴇ-7", "integrate(x^2,x,0,1)",
        "sum(1/k^2,k,1,n)", "limit(sin(x)/x,x,0)", "x_1+2", "0.(142857)", "5∠0.9273", "a:=5", "5→a",
        "sin⁻¹(x)+x²+x⁻¹+3!", "2π", "{1,2,3}", "x≠2", "±√(2)", "10^(3)", "-x^2", "Ans*2",
        "∇(x^2*y)", "∇·[x,y,z]", "∇×[y,-x,0]", "∇²f+1", "2∇f", "∇·∇f", "grad(x*y)", "laplacian(f)",
        "divergence([x,y])", "curl([y,-x,0])",
    };
    for (size_t k = 0; k < sizeof ok / sizeof ok[0]; k++) {
        bx_init(&p, store, 400);
        int r = m2d_parse(&p, ok[k], 0);
        CHECK(r >= 0);
        if (r < 0) {
            fprintf(stderr, "  parse failed: %s\n", ok[k]);
            continue;
        }
        bx_measure(&p, r);
        CHECK(p.b[r].w > 0 && p.b[r].asc > 0 && p.b[r].desc >= 0 && !p.overflow);
        bx_place(&p, r, 10, 100);
        gfx_textlog_enable(false);
        bx_draw(&p, r, C_BLACK);
        gfx_textlog_enable(true);
    }
    static const char *const bad[] = {"", "2+", "((", "ERR:SYNTAX", "x$y", "f(,)", "∇", "∇·)"};
    for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) {
        bx_init(&p, store, 400);
        CHECK(m2d_parse(&p, bad[k], 0) < 0);
    }
    /* structure */
    bx_init(&p, store, 400);
    int r = m2d_parse(&p, "1/2", 0);
    CHECK(r >= 0 && p.b[p.b[r].child].kind == BX_FRAC);
    bx_init(&p, store, 400);
    r = m2d_parse(&p, "x^(1/3)", 0);
    CHECK(r >= 0 && p.b[p.b[r].child].kind == BX_ROOT && bx_nchildren(&p, p.b[r].child) == 2);
    bx_init(&p, store, 400);
    r = m2d_parse(&p, "[[1,2,3],[4,5,6]]", 0);
    CHECK(r >= 0 && p.b[p.b[r].child].kind == BX_MATRIX && p.b[p.b[r].child].aux == 3);
    /* ∇ notation: grad(x*y) is the text ∇ then (x·y) in parentheses; laplacian(f) is ∇, a
     * superscript 2 and f; ∇·[x,y] as typed keeps its operator text */
    bx_init(&p, store, 400);
    r = m2d_parse(&p, "grad(x*y)", 0);
    int c0 = r >= 0 ? p.b[r].child : -1; /* (m2d_parse returns the nabla row itself) */
    CHECK(c0 >= 0 && p.b[c0].kind == BX_TEXT && p.b[c0].len == 3 && !memcmp(p.b[c0].s, "∇", 3));
    CHECK(c0 >= 0 && p.b[p.b[c0].next].kind == BX_FENCE);
    bx_init(&p, store, 400);
    r = m2d_parse(&p, "laplacian(f)", 0);
    c0 = r >= 0 ? p.b[r].child : -1;
    CHECK(c0 >= 0 && p.b[p.b[c0].next].kind == BX_SUP && p.b[p.b[p.b[c0].next].next].kind == BX_TEXT);
    bx_init(&p, store, 400);
    r = m2d_parse(&p, "∇·[x,y]", 0);
    c0 = r >= 0 ? p.b[r].child : -1;
    CHECK(c0 >= 0 && p.b[c0].len == 5 && !memcmp(p.b[c0].s, "∇·", 5));
    /* a fraction is taller than a line, an exponent raises */
    bx_init(&p, store, 400);
    r = m2d_parse(&p, "1/2", 0);
    bx_measure(&p, r);
    CHECK(p.b[r].asc + p.b[r].desc > bx_asc(0) + bx_desc(0) + 10);
    /* pool overflow is reported, not a crash */
    bx_init(&p, store, 8);
    CHECK(m2d_parse(&p, "1/2+3/4+5/6+7/8", 0) < 0);
    /* diff(f,x,2): the order is repeated in the denominator, d²/dx² (a
     * number order is a row holding one text box, not a text box) */
    bx_init(&p, store, 400);
    r = m2d_parse(&p, "diff(x^3,x,2)", 0);
    int twos = 0, ns = 0;
    for (int i = 0; r >= 0 && i < p.n; i++)
        if (p.b[i].kind == BX_TEXT && p.b[i].len == 1)
            twos += p.b[i].s[0] == '2', ns += p.b[i].s[0] == 'n';
    CHECK(r >= 0 && twos == 2 && ns == 0);
    bx_init(&p, store, 400);
    r = m2d_parse(&p, "diff(f(x),x,k)", 0);
    int ks = 0;
    for (int i = 0; r >= 0 && i < p.n; i++)
        ks += p.b[i].kind == BX_TEXT && p.b[i].len == 1 && p.b[i].s[0] == 'k';
    CHECK(r >= 0 && ks == 2);

    /* editor layout: a caret row per box */
    editor_t e;
    ed_clear(&e);
    /* unknown template types and matrix sizes outside 1..3 are refused */
    CHECK(!m2d_insert_template(&e, -1, 0, 0) && !m2d_insert_template(&e, TPL_COUNT, 0, 0));
    CHECK(!m2d_insert_template(&e, TPL_MATRIX, 9, 1) && !m2d_insert_template(&e, TPL_MATRIX, 1, 4));
    CHECK(e.len == 0 && !m2d_has_templates(&e));
    m2d_insert_template(&e, TPL_FRAC, 0, 0);
    ed_insert_text(&e, "1", NULL);
    int16_t tokbox[ED_MAX], rows[16];
    int nrows;
    bx_init(&p, store, 400);
    r = m2d_layout_editor(&p, &e, 0, tokbox, rows, &nrows, 16);
    CHECK(nrows == 3); /* line, numerator, denominator */
    bx_measure(&p, r);
    CHECK(!p.overflow && p.b[r].w > 0);
}

/* ------------------------------------------------------------ themes */
/* Dark mode is a colour map installed in the graphics layer: white fills
 * become dark (red channel of RGB565 below 8 of 31), black becomes light,
 * the selection and status-bar colours are kept, and ink is chosen against
 * the colour underneath (gfx role INK with the background colour). Light
 * mode removes the map. Large font: 14x28 font_xlarge for UI_FONT and
 * nesting level 0 of the 2D math fonts. */
static void test_theme(void)
{
    gfx_init(hal_framebuffer());
    theme_set(THEME_LIGHT);
    gfx_fill_rect(0, 0, 4, 4, C_WHITE);
    CHECK(gfx_get_pixel(1, 1) == C_WHITE);
    theme_set(THEME_DARK);
    gfx_fill_rect(0, 0, 4, 4, C_WHITE);
    uint16_t bg = gfx_get_pixel(1, 1);
    CHECK(bg != C_WHITE && (bg >> 11) < 8);
    CHECK(theme_dark_map(C_BLACK, GFX_ROLE_FILL, 0) != C_BLACK);
    CHECK(theme_dark_map(C_SEL_BG, GFX_ROLE_FILL, 0) == C_SEL_BG);
    CHECK(theme_dark_map(C_STATUS_BG, GFX_ROLE_FILL, 0) == C_STATUS_BG);
    CHECK(theme_dark_map(C_WHITE, GFX_ROLE_INK, C_SEL_BG) == C_WHITE);     /* on a selection */
    CHECK(theme_dark_map(C_BLACK, GFX_ROLE_INK, C_YELLOW) == C_BLACK);     /* on a light badge */
    CHECK(theme_dark_map(C_BLACK, GFX_ROLE_INK, bg) != C_BLACK);           /* on the background */
    /* text drawn on the dark background is light */
    gfx_fill_rect(0, 0, 40, 30, C_WHITE);
    gfx_text(&font_large, 0, 0, "8", C_BLACK, GFX_TRANSPARENT);
    int light = 0;
    for (int y = 0; y < 20; y++)
        for (int x = 0; x < 10; x++)
            light += (gfx_get_pixel(x, y) >> 11) > 20;
    CHECK(light > 10);
    theme_set(THEME_LIGHT);
    CHECK(gfx_color_map == NULL);
    theme_set_large(true);
    CHECK(ui_font == &font_xlarge && UI_CH == 28 && theme_math_font(0) == &font_xlarge);
    theme_set_large(false);
    CHECK(ui_font == &font_large && theme_math_font(1) == &font_medium);
    CHECK(font_xlarge.w == 14 && font_xlarge.h == 28);
    /* the large-font mode font has every glyph of the normal 10x20 font
     * (tools/gen_font.py fills gaps from fallback fonts): each code point of
     * font_large is in font_xlarge's sorted table */
    int missing = 0;
    for (int i = 0; i < font_large.nglyphs; i++) {
        uint32_t cp = font_large.cp[i];
        int lo = 0, hi = font_xlarge.nglyphs - 1;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (font_xlarge.cp[mid] < cp)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (font_xlarge.cp[lo] != cp) {
            fprintf(stderr, "  font_xlarge lacks U+%04X\n", (unsigned)cp);
            missing++;
        }
    }
    CHECK(missing == 0);
}

int main(void)
{
    hal_init();
    size_t n = 8u << 20;
    cas = cas_init(malloc(n), n);
    g.cas = cas;
    tok_init();
    m2d_init();
    gfx_init(hal_framebuffer());
    test_repdec();
    test_templates();
    test_layout();
    test_theme();
    printf("test_m2d: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
