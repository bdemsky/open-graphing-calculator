/*
 * The `calc` library: the calculator's CAS from Python (help page
 * "python-calc", docs/MANUAL.md).
 *
 *   calc.Expr        a CAS expression (its text: the CAS's linear form, as
 *                    str() and repr() show it). + - * / ** with ints, floats,
 *                    complex numbers and other Exprs build CAS expressions,
 *                    evaluated by the CAS; == compares mathematically (the
 *                    difference simplifies to 0); float(), int(), complex()
 *                    work when the value is a number.
 *   calc.eval(s)     evaluate CAS input -> Expr
 *   calc.sym('x'), calc.syms('x y') and `from calc import x, y, t`
 *   solve(eq, x) -> list, integrate(f, x[, a, b]), diff(f, x[, n]),
 *   limit(f, x, a[, dir]), factor, expand, simplify, exact, approx(e[, digits])
 *   (a float or complex when numeric), latex(e) -> str, eq(a, b) -> a=b,
 *   subs(e, x, value), plot(e, xmin, xmax) (FUNC graph, shown afterwards)
 *   calc.CASError    raised when the CAS reports an error
 *
 * Conversion to CAS text: Python ints exactly (any size), floats with 17
 * significant digits, complex as a+b*i, str as CAS input, objects with
 * numerator / denominator (fractions) as n/d, lists and tuples as [..].
 * The CAS is called with cas_eval_quiet: `ans` on the home screen is kept.
 */
#include <math.h>
#include <string.h>
#include "py_mod.h"
#include "nb.h"
#include "py/formatfloat.h"
#include "py/objstr.h"
#include "py/parsenum.h"
#include "py/runtime.h"

MP_DEFINE_EXCEPTION(CASError, Exception)

typedef struct {
    mp_obj_base_t base;
    mp_obj_t text; /* str */
} expr_t;

bool py_is_expr(mp_obj_t o) { return mp_obj_is_type(o, &py_type_expr); }
const char *py_expr_text(mp_obj_t o) { return mp_obj_str_get_str(((expr_t *)MP_OBJ_TO_PTR(o))->text); }

static mp_obj_t expr_new_str(mp_obj_t s)
{
    expr_t *e = mp_obj_malloc(expr_t, &py_type_expr);
    e->text = s;
    return MP_OBJ_FROM_PTR(e);
}
static mp_obj_t expr_new(const char *s) { return expr_new_str(mp_obj_new_str(s, strlen(s))); }
mp_obj_t py_expr_new(const char *cas) { return expr_new(cas); }

/* ------------------------------------------------------------ the CAS */
static NORETURN void cas_fail(cas_status s)
{
    if (s == CAS_ERR_INTERRUPTED) {
        py_break_pending(); /* the press that stopped it: no second KeyboardInterrupt */
        mp_raise_type(&mp_type_KeyboardInterrupt);
    }
    mp_raise_msg_varg(&mp_type_CASError, MP_ERROR_TEXT("%s"), py_pc->error());
}

/* Evaluate CAS input; the result text as a str object. */
static mp_obj_t cas_str(const char *in)
{
    for (size_t n = 1024;; n *= 4) {
        char *buf = m_new(char, n);
        cas_status s = py_pc->eval(in, buf, n, py_stack_used());
        if (s == CAS_OK) {
            mp_obj_t r = mp_obj_new_str(buf, strlen(buf));
            m_del(char, buf, n);
            return r;
        }
        m_del(char, buf, n);
        if (s != CAS_ERR_OUTPUT_TRUNC || n >= (256u << 10))
            cas_fail(s);
    }
}
static mp_obj_t cas_expr(const char *in) { return expr_new_str(cas_str(in)); }

/* ------------------------------------------------------------ Python -> CAS text */
static void put_float(vstr_t *v, mp_float_t f)
{
    if (isnan(f))
        mp_raise_ValueError(MP_ERROR_TEXT("nan has no CAS value"));
    if (isinf(f)) {
        vstr_add_str(v, f > 0 ? "inf" : "(-inf)");
        return;
    }
    char b[40];
    mp_format_float(f, b, sizeof b, 'g', 17, '\0');
    char *e = strchr(b, 'e');
    bool neg = b[0] == '-';
    if (neg)
        vstr_add_char(v, '(');
    if (e) {
        *e = 0;
        vstr_add_str(v, b);
        vstr_add_str(v, "*10^(");
        vstr_add_str(v, e[1] == '+' ? e + 2 : e + 1);
        vstr_add_char(v, ')');
    } else {
        vstr_add_str(v, b);
        if (!strchr(b, '.'))
            vstr_add_str(v, ".0"); /* a float stays a float in the CAS */
    }
    if (neg)
        vstr_add_char(v, ')');
}

static void to_cas(vstr_t *v, mp_obj_t o)
{
    if (py_is_expr(o)) {
        vstr_add_char(v, '(');
        vstr_add_str(v, py_expr_text(o));
        vstr_add_char(v, ')');
    } else if (o == mp_const_true || o == mp_const_false) {
        vstr_add_char(v, o == mp_const_true ? '1' : '0');
    } else if (mp_obj_is_int(o)) {
        mp_print_t pr;
        vstr_t t;
        vstr_init_print(&t, 16, &pr);
        mp_obj_print_helper(&pr, o, PRINT_STR);
        bool neg = t.len && t.buf[0] == '-';
        if (neg)
            vstr_add_char(v, '(');
        vstr_add_strn(v, t.buf, t.len);
        if (neg)
            vstr_add_char(v, ')');
        vstr_clear(&t);
    } else if (mp_obj_is_float(o)) {
        put_float(v, mp_obj_float_get(o));
    } else if (mp_obj_is_type(o, &mp_type_complex)) {
        mp_float_t re, im;
        mp_obj_complex_get(o, &re, &im);
        vstr_add_char(v, '(');
        put_float(v, re);
        vstr_add_char(v, '+');
        put_float(v, im);
        vstr_add_str(v, "*i)");
    } else if (mp_obj_is_str(o)) {
        vstr_add_char(v, '(');
        vstr_add_str(v, mp_obj_str_get_str(o));
        vstr_add_char(v, ')');
    } else if (mp_obj_is_type(o, &mp_type_list) || mp_obj_is_type(o, &mp_type_tuple)) {
        size_t n;
        mp_obj_t *items;
        mp_obj_get_array(o, &n, &items);
        vstr_add_char(v, '[');
        for (size_t i = 0; i < n; i++) {
            if (i)
                vstr_add_char(v, ',');
            to_cas(v, items[i]);
        }
        vstr_add_char(v, ']');
    } else {
        mp_obj_t num[2], den[2];
        mp_load_method_maybe(o, MP_QSTR_numerator, num);
        mp_load_method_maybe(o, MP_QSTR_denominator, den);
        if (num[0] == MP_OBJ_NULL || den[0] == MP_OBJ_NULL || num[1] != MP_OBJ_NULL || den[1] != MP_OBJ_NULL)
            mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("can't convert %s to a CAS expression"),
                              mp_obj_get_type_str(o));
        vstr_add_char(v, '(');
        to_cas(v, num[0]);
        vstr_add_char(v, '/');
        to_cas(v, den[0]);
        vstr_add_char(v, ')');
    }
}

/* "cmd(a1,a2,...)" from up to 4 Python arguments (MP_OBJ_NULL ends the list) */
static mp_obj_t call_cmd(const char *cmd, mp_obj_t a, mp_obj_t b, mp_obj_t c, mp_obj_t d)
{
    vstr_t v;
    vstr_init(&v, 64);
    vstr_add_str(&v, cmd);
    vstr_add_char(&v, '(');
    mp_obj_t args[4] = {a, b, c, d};
    for (int i = 0; i < 4 && args[i] != MP_OBJ_NULL; i++) {
        if (i)
            vstr_add_char(&v, ',');
        to_cas(&v, args[i]);
    }
    vstr_add_char(&v, ')');
    mp_obj_t r = cas_expr(vstr_null_terminated_str(&v));
    vstr_clear(&v);
    return r;
}

/* ------------------------------------------------------------ CAS text -> numbers */
/* A real number in the CAS's output form: 1.5, -2, 3.0*10^(-7), 1.2E-5; true if all of s is one */
static bool parse_real(const char *s, size_t n, mp_float_t *out)
{
    char b[64];
    if (!n || n >= sizeof b)
        return false;
    memcpy(b, s, n);
    b[n] = 0;
    char *t = strstr(b, "*10^");
    if (t) { /* m*10^(e) -> me+e */
        char *e = t + 4;
        if (*e == '(')
            e++;
        size_t k = strlen(e);
        if (k && e[k - 1] == ')')
            e[--k] = 0;
        *t = 'e';
        memmove(t + 1, e, k + 1);
    }
    if (!strcmp(b, "inf") || !strcmp(b, "-inf")) {
        *out = b[0] == '-' ? -INFINITY : INFINITY;
        return true;
    }
    for (char *p = b; *p; p++)
        if (!strchr("0123456789.eE+-", *p))
            return false;
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t f = mp_parse_num_float(b, strlen(b), false, NULL);
        nlr_pop();
        *out = mp_obj_get_float(f);
        return true;
    }
    return false;
}

/* a number a, b*i, a+b*i, a-b*i (also i, -i); false if s is anything else */
static bool parse_number(const char *s, mp_float_t *re, mp_float_t *im)
{
    size_t n = strlen(s);
    *re = 0, *im = 0;
    if (parse_real(s, n, re))
        return true;
    if (!n || s[n - 1] != 'i')
        return false;
    /* the imaginary part starts at the last + or - that is not an exponent's sign */
    size_t k = 0;
    for (size_t j = 1; j + 1 < n; j++)
        if ((s[j] == '+' || s[j] == '-') && s[j - 1] != 'e' && s[j - 1] != 'E' && s[j - 1] != '(')
            k = j;
    if (k && !parse_real(s, k, re))
        return false;
    const char *ip = s + k;
    size_t il = n - k - 1; /* without the i */
    if (il && ip[il - 1] == '*')
        il--;
    if (il == 0 || (il == 1 && (ip[0] == '+' || ip[0] == '-'))) {
        *im = il && ip[0] == '-' ? -1 : 1;
        return true;
    }
    return parse_real(ip[0] == '+' ? ip + 1 : ip, ip[0] == '+' ? il - 1 : il, im);
}

/* approx(e): a float, a complex, or (not a number) MP_OBJ_NULL */
static mp_obj_t approx_number(mp_obj_t e)
{
    mp_obj_t r = call_cmd("approx", e, MP_OBJ_NEW_SMALL_INT(17), MP_OBJ_NULL, MP_OBJ_NULL);
    mp_float_t re, im;
    if (!parse_number(py_expr_text(r), &re, &im))
        return MP_OBJ_NULL;
    return im == 0 ? mp_obj_new_float(re) : mp_obj_new_complex(re, im);
}

/* ------------------------------------------------------------ calc.Expr */
static void expr_print(const mp_print_t *print, mp_obj_t self, mp_print_kind_t kind)
{
    (void)kind;
    mp_print_str(print, py_expr_text(self));
}

static mp_obj_t expr_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args)
{
    (void)type;
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    if (py_is_expr(args[0]))
        return args[0];
    vstr_t v;
    vstr_init(&v, 32);
    to_cas(&v, args[0]);
    mp_obj_t r = cas_expr(vstr_null_terminated_str(&v));
    vstr_clear(&v);
    return r;
}

static mp_obj_t binop_text(const char *op, mp_obj_t a, mp_obj_t b)
{
    vstr_t v;
    vstr_init(&v, 64);
    to_cas(&v, a);
    vstr_add_str(&v, op);
    to_cas(&v, b);
    mp_obj_t r = cas_expr(vstr_null_terminated_str(&v));
    vstr_clear(&v);
    return r;
}

static bool convertible(mp_obj_t o)
{
    return py_is_expr(o) || mp_obj_is_int(o) || mp_obj_is_float(o) || mp_obj_is_type(o, &mp_type_complex) ||
           mp_obj_is_bool(o);
}

static mp_obj_t expr_binary_op(mp_binary_op_t op, mp_obj_t lhs, mp_obj_t rhs)
{
    if (!convertible(rhs))
        return MP_OBJ_NULL;
    static const struct {
        uint8_t op;
        char s[2];
        bool rev;
    } ops[] = {
        {MP_BINARY_OP_ADD, "+", 0}, {MP_BINARY_OP_SUBTRACT, "-", 0}, {MP_BINARY_OP_MULTIPLY, "*", 0},
        {MP_BINARY_OP_TRUE_DIVIDE, "/", 0}, {MP_BINARY_OP_POWER, "^", 0},
        {MP_BINARY_OP_INPLACE_ADD, "+", 0}, {MP_BINARY_OP_INPLACE_SUBTRACT, "-", 0},
        {MP_BINARY_OP_INPLACE_MULTIPLY, "*", 0}, {MP_BINARY_OP_INPLACE_TRUE_DIVIDE, "/", 0},
        {MP_BINARY_OP_INPLACE_POWER, "^", 0},
        {MP_BINARY_OP_REVERSE_ADD, "+", 1}, {MP_BINARY_OP_REVERSE_SUBTRACT, "-", 1},
        {MP_BINARY_OP_REVERSE_MULTIPLY, "*", 1}, {MP_BINARY_OP_REVERSE_TRUE_DIVIDE, "/", 1},
        {MP_BINARY_OP_REVERSE_POWER, "^", 1},
    };
    for (size_t i = 0; i < MP_ARRAY_SIZE(ops); i++)
        if (ops[i].op == op)
            return ops[i].rev ? binop_text(ops[i].s, rhs, lhs) : binop_text(ops[i].s, lhs, rhs);
    if (op == MP_BINARY_OP_EQUAL || op == MP_BINARY_OP_NOT_EQUAL) {
        mp_obj_t d = binop_text("-", lhs, rhs);
        mp_obj_t z = call_cmd("simplify", d, MP_OBJ_NULL, MP_OBJ_NULL, MP_OBJ_NULL);
        bool eq = !strcmp(py_expr_text(z), "0");
        return mp_obj_new_bool(op == MP_BINARY_OP_EQUAL ? eq : !eq);
    }
    if (op == MP_BINARY_OP_LESS || op == MP_BINARY_OP_MORE || op == MP_BINARY_OP_LESS_EQUAL ||
        op == MP_BINARY_OP_MORE_EQUAL) {
        mp_float_t a, b;
        mp_obj_t x = py_is_expr(lhs) ? approx_number(lhs) : lhs, y = py_is_expr(rhs) ? approx_number(rhs) : rhs;
        if (x == MP_OBJ_NULL || y == MP_OBJ_NULL || !mp_obj_get_float_maybe(x, &a) || !mp_obj_get_float_maybe(y, &b))
            mp_raise_TypeError(MP_ERROR_TEXT("only real numbers can be compared"));
        bool r = op == MP_BINARY_OP_LESS ? a < b : op == MP_BINARY_OP_MORE ? a > b : op == MP_BINARY_OP_LESS_EQUAL ? a <= b : a >= b;
        return mp_obj_new_bool(r);
    }
    return MP_OBJ_NULL;
}

static mp_obj_t expr_unary_op(mp_unary_op_t op, mp_obj_t self)
{
    const char *t = py_expr_text(self);
    switch (op) {
    case MP_UNARY_OP_POSITIVE:
        return self;
    case MP_UNARY_OP_NEGATIVE:
        return binop_text("-", MP_OBJ_NEW_SMALL_INT(0), self);
    case MP_UNARY_OP_ABS:
        return call_cmd("abs", self, MP_OBJ_NULL, MP_OBJ_NULL, MP_OBJ_NULL);
    case MP_UNARY_OP_BOOL:
        return mp_obj_new_bool(strcmp(t, "0") && strcmp(t, "false"));
    case MP_UNARY_OP_HASH:
        return mp_unary_op(MP_UNARY_OP_HASH, ((expr_t *)MP_OBJ_TO_PTR(self))->text);
    case MP_UNARY_OP_FLOAT_MAYBE: {
        mp_obj_t r = approx_number(self);
        return r != MP_OBJ_NULL && mp_obj_is_float(r) ? r : MP_OBJ_NULL;
    }
    case MP_UNARY_OP_COMPLEX_MAYBE: {
        mp_obj_t r = approx_number(self);
        if (r == MP_OBJ_NULL)
            return MP_OBJ_NULL;
        return mp_obj_is_float(r) ? mp_obj_new_complex(mp_obj_float_get(r), 0) : r;
    }
    case MP_UNARY_OP_INT_MAYBE: {
        size_t n = strlen(t), k = t[0] == '-';
        if (n == k)
            return MP_OBJ_NULL;
        for (size_t i = k; i < n; i++)
            if (t[i] < '0' || t[i] > '9')
                return MP_OBJ_NULL;
        return mp_parse_num_integer(t, n, 10, NULL);
    }
    default:
        return MP_OBJ_NULL;
    }
}

/* e[i]: the items of a list result ([a,b,...]; nested lists stay together) */
static size_t list_items(const char *t, size_t *starts, size_t *lens, size_t max)
{
    size_t n = strlen(t);
    if (n < 2 || t[0] != '[' || t[n - 1] != ']')
        return 0;
    size_t k = 0, depth = 0, st = 1;
    for (size_t i = 1; i < n - 1; i++) {
        char c = t[i];
        if (c == '(' || c == '[' || c == '{')
            depth++;
        else if (c == ')' || c == ']' || c == '}')
            depth--;
        else if (c == ',' && !depth) {
            if (k < max)
                starts[k] = st, lens[k] = i - st;
            k++;
            st = i + 1;
        }
    }
    if (n > 2) {
        if (k < max)
            starts[k] = st, lens[k] = n - 1 - st;
        k++;
    }
    return k;
}

/* A list result as a Python list of Expr (anything else: [e]) */
static mp_obj_t to_list(mp_obj_t e)
{
    const char *t = py_expr_text(e);
    size_t n = strlen(t);
    size_t cnt = list_items(t, NULL, NULL, 0);
    if (!cnt && n == 2 && t[0] == '[')
        return mp_obj_new_list(0, NULL);
    if (!cnt)
        return mp_obj_new_list(1, &e);
    size_t *st = m_new(size_t, cnt), *ln = m_new(size_t, cnt);
    list_items(t, st, ln, cnt);
    mp_obj_t l = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < cnt; i++)
        mp_obj_list_append(l, expr_new_str(mp_obj_new_str(t + st[i], ln[i])));
    m_del(size_t, st, cnt);
    m_del(size_t, ln, cnt);
    return l;
}

static mp_obj_t expr_subscr(mp_obj_t self, mp_obj_t index, mp_obj_t value)
{
    if (value != MP_OBJ_SENTINEL)
        return MP_OBJ_NULL; /* read-only */
    mp_obj_t l = to_list(self);
    return mp_obj_subscr(l, index, MP_OBJ_SENTINEL);
}

static mp_obj_t expr_getiter(mp_obj_t self, mp_obj_iter_buf_t *iter_buf)
{
    (void)iter_buf;
    return mp_getiter(to_list(self), NULL);
}

/* methods: e.approx(), e.exact(), e.expand(), e.factor(), e.simplify(), e.latex(), e.subs(x, v) */
static mp_obj_t calc_approx(size_t n_args, const mp_obj_t *args);
static mp_obj_t calc_latex(mp_obj_t e);
static mp_obj_t calc_subs(mp_obj_t e, mp_obj_t x, mp_obj_t v);
static mp_obj_t m_exact(mp_obj_t e) { return call_cmd("exact", e, MP_OBJ_NULL, MP_OBJ_NULL, MP_OBJ_NULL); }
static mp_obj_t m_expand(mp_obj_t e) { return call_cmd("expand", e, MP_OBJ_NULL, MP_OBJ_NULL, MP_OBJ_NULL); }
static mp_obj_t m_factor(mp_obj_t e) { return call_cmd("factor", e, MP_OBJ_NULL, MP_OBJ_NULL, MP_OBJ_NULL); }
static mp_obj_t m_simplify(mp_obj_t e) { return call_cmd("simplify", e, MP_OBJ_NULL, MP_OBJ_NULL, MP_OBJ_NULL); }
static MP_DEFINE_CONST_FUN_OBJ_1(calc_exact_obj, m_exact);
static MP_DEFINE_CONST_FUN_OBJ_1(calc_expand_obj, m_expand);
static MP_DEFINE_CONST_FUN_OBJ_1(calc_factor_obj, m_factor);
static MP_DEFINE_CONST_FUN_OBJ_1(calc_simplify_obj, m_simplify);
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(calc_approx_obj, 1, 2, calc_approx);
static MP_DEFINE_CONST_FUN_OBJ_1(calc_latex_obj, calc_latex);
static MP_DEFINE_CONST_FUN_OBJ_3(calc_subs_obj, calc_subs);

static const mp_rom_map_elem_t expr_locals_table[] = {
    {MP_ROM_QSTR(MP_QSTR_approx), MP_ROM_PTR(&calc_approx_obj)},
    {MP_ROM_QSTR(MP_QSTR_exact), MP_ROM_PTR(&calc_exact_obj)},
    {MP_ROM_QSTR(MP_QSTR_expand), MP_ROM_PTR(&calc_expand_obj)},
    {MP_ROM_QSTR(MP_QSTR_factor), MP_ROM_PTR(&calc_factor_obj)},
    {MP_ROM_QSTR(MP_QSTR_simplify), MP_ROM_PTR(&calc_simplify_obj)},
    {MP_ROM_QSTR(MP_QSTR_latex), MP_ROM_PTR(&calc_latex_obj)},
    {MP_ROM_QSTR(MP_QSTR_subs), MP_ROM_PTR(&calc_subs_obj)},
};
static MP_DEFINE_CONST_DICT(expr_locals, expr_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(py_type_expr, MP_QSTR_Expr, MP_TYPE_FLAG_ITER_IS_GETITER, make_new, expr_make_new,
                         print, expr_print, unary_op, expr_unary_op, binary_op, expr_binary_op, subscr, expr_subscr,
                         iter, expr_getiter, locals_dict, &expr_locals);

/* ------------------------------------------------------------ module functions */
static mp_obj_t default_x(void) { return expr_new("x"); }

static mp_obj_t calc_eval(mp_obj_t s)
{
    if (py_is_expr(s))
        return cas_expr(py_expr_text(s));
    return cas_expr(mp_obj_str_get_str(s));
}
static MP_DEFINE_CONST_FUN_OBJ_1(calc_eval_obj, calc_eval);

static bool sym_name_ok(const char *s, size_t n)
{
    if (!n || !((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z')))
        return false;
    for (size_t i = 1; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_'))
            return false;
    return true;
}

static mp_obj_t calc_sym(mp_obj_t name)
{
    size_t n;
    const char *s = mp_obj_str_get_data(name, &n);
    if (!sym_name_ok(s, n))
        mp_raise_ValueError(MP_ERROR_TEXT("a symbol name is a letter followed by letters, digits or _"));
    return expr_new_str(mp_obj_new_str(s, n));
}
static MP_DEFINE_CONST_FUN_OBJ_1(calc_sym_obj, calc_sym);

static mp_obj_t calc_syms(mp_obj_t names)
{
    mp_obj_t parts = mp_call_function_0(mp_load_attr(names, MP_QSTR_split));
    size_t n;
    mp_obj_t *items;
    mp_obj_get_array(parts, &n, &items);
    mp_obj_tuple_t *t = MP_OBJ_TO_PTR(mp_obj_new_tuple(n, NULL));
    for (size_t i = 0; i < n; i++)
        t->items[i] = calc_sym(items[i]);
    return MP_OBJ_FROM_PTR(t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(calc_syms_obj, calc_syms);

/* `from calc import x, y`: a short name (a letter, a letter with digits, a Greek letter's name) is a symbol */
static mp_obj_t calc_getattr(mp_obj_t attr)
{
    qstr q = mp_obj_str_get_qstr(attr);
    size_t n;
    const char *s = (const char *)qstr_data(q, &n);
    static const char greek[][8] = {"alpha", "beta", "gamma", "delta", "theta", "lambda", "mu", "omega", "phi", "tau", "pi"};
    bool ok = sym_name_ok(s, n) && (n == 1 || (n <= 3 && s[1] >= '0' && s[1] <= '9'));
    for (size_t i = 0; !ok && i < MP_ARRAY_SIZE(greek); i++)
        ok = strlen(greek[i]) == n && !memcmp(greek[i], s, n);
    if (!ok)
        return MP_OBJ_NULL; /* not found: the caller raises AttributeError (or, for __init__, goes on) */
    return expr_new_str(mp_obj_new_str(s, n));
}
static MP_DEFINE_CONST_FUN_OBJ_1(calc_getattr_obj, calc_getattr);

static mp_obj_t calc_solve(size_t n_args, const mp_obj_t *args)
{
    mp_obj_t r = call_cmd("solve", args[0], n_args > 1 ? args[1] : default_x(), MP_OBJ_NULL, MP_OBJ_NULL);
    return to_list(r);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(calc_solve_obj, 1, 2, calc_solve);

static mp_obj_t calc_integrate(size_t n_args, const mp_obj_t *args)
{
    if (n_args == 3)
        mp_raise_TypeError(MP_ERROR_TEXT("integrate(f, x) or integrate(f, x, a, b)"));
    return call_cmd("integrate", args[0], n_args > 1 ? args[1] : default_x(), n_args > 2 ? args[2] : MP_OBJ_NULL,
                    n_args > 3 ? args[3] : MP_OBJ_NULL);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(calc_integrate_obj, 1, 4, calc_integrate);

static mp_obj_t calc_diff(size_t n_args, const mp_obj_t *args)
{
    return call_cmd("diff", args[0], n_args > 1 ? args[1] : default_x(), n_args > 2 ? args[2] : MP_OBJ_NULL,
                    MP_OBJ_NULL);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(calc_diff_obj, 1, 3, calc_diff);

static mp_obj_t calc_limit(size_t n_args, const mp_obj_t *args)
{
    return call_cmd("limit", args[0], args[1], args[2], n_args > 3 ? args[3] : MP_OBJ_NULL);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(calc_limit_obj, 3, 4, calc_limit);

static mp_obj_t calc_approx(size_t n_args, const mp_obj_t *args)
{
    if (n_args > 1)
        return call_cmd("approx", args[0], args[1], MP_OBJ_NULL, MP_OBJ_NULL);
    mp_obj_t r = approx_number(args[0]);
    return r != MP_OBJ_NULL ? r : call_cmd("approx", args[0], MP_OBJ_NULL, MP_OBJ_NULL, MP_OBJ_NULL);
}

static mp_obj_t calc_subs(mp_obj_t e, mp_obj_t x, mp_obj_t v)
{
    return call_cmd("subst", e, x, v, MP_OBJ_NULL);
}

void py_calc_latex(mp_obj_t o, char *out, size_t n)
{
    if (!py_is_expr(o)) { /* a number, a string of CAS input: through the CAS first */
        vstr_t v;
        vstr_init(&v, 64);
        to_cas(&v, o);
        o = cas_expr(vstr_null_terminated_str(&v));
        vstr_clear(&v);
    }
    py_latex_text(py_expr_text(o), out, n);
}

static mp_obj_t calc_latex(mp_obj_t e)
{
    size_t n = 4096;
    char *buf = m_new(char, n);
    py_calc_latex(e, buf, n);
    mp_obj_t r = mp_obj_new_str(buf, strlen(buf));
    m_del(char, buf, n);
    return r;
}

static mp_obj_t calc_eq(mp_obj_t a, mp_obj_t b) { return binop_text("=", a, b); }
static MP_DEFINE_CONST_FUN_OBJ_2(calc_eq_obj, calc_eq);

static mp_obj_t calc_plot(size_t n_args, const mp_obj_t *args)
{
    if (!py_is_expr(args[0]) && !mp_obj_is_str(args[0]))
        mp_raise_TypeError(MP_ERROR_TEXT("plot() takes a calc expression or a string in x (not a Python function)"));
    mp_float_t lo = n_args > 1 ? mp_obj_get_float(args[1]) : -10, hi = n_args > 2 ? mp_obj_get_float(args[2]) : 10;
    const char *t = py_is_expr(args[0]) ? py_expr_text(args[0]) : mp_obj_str_get_str(args[0]);
    if (nb_plot_hook(t, lo, hi)) /* a notebook cell: its own plot */
        return mp_const_none;
    char err[80];
    if (py_pc->plot(t, lo, hi, err, sizeof err))
        mp_raise_ValueError(MP_ERROR_TEXT("plot: cannot graph this"));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(calc_plot_obj, 1, 3, calc_plot);

/* sin(x) etc.: symbolic for an Expr argument (CAS), else math's function (floats) */
static mp_obj_t fwd(const char *cas, qstr mathfn, size_t n_args, const mp_obj_t *args)
{
    bool sym = false;
    for (size_t i = 0; i < n_args; i++)
        sym |= py_is_expr(args[i]);
    if (sym) {
        if (n_args == 2) { /* log(x, base) */
            mp_obj_t a = call_cmd(cas, args[0], MP_OBJ_NULL, MP_OBJ_NULL, MP_OBJ_NULL);
            return binop_text("/", a, call_cmd(cas, args[1], MP_OBJ_NULL, MP_OBJ_NULL, MP_OBJ_NULL));
        }
        return call_cmd(cas, args[0], MP_OBJ_NULL, MP_OBJ_NULL, MP_OBJ_NULL);
    }
    mp_obj_t m = mp_import_name(MP_QSTR_math, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
    return mp_call_function_n_kw(mp_load_attr(m, mathfn), n_args, 0, args);
}
#define CALC_FN(nm, cas, maxn)                                                                      \
    static mp_obj_t calc_fn_##nm(size_t n, const mp_obj_t *a) { return fwd(cas, MP_QSTR_##nm, n, a); } \
    static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(calc_fn_##nm##_obj, 1, maxn, calc_fn_##nm);
CALC_FN(sin, "sin", 1)
CALC_FN(cos, "cos", 1)
CALC_FN(tan, "tan", 1)
CALC_FN(asin, "asin", 1)
CALC_FN(acos, "acos", 1)
CALC_FN(atan, "atan", 1)
CALC_FN(sinh, "sinh", 1)
CALC_FN(cosh, "cosh", 1)
CALC_FN(tanh, "tanh", 1)
CALC_FN(exp, "exp", 1)
CALC_FN(log, "ln", 2)
CALC_FN(sqrt, "sqrt", 1)

/* `from calc import *`: everything but eval (Python's own) and the constants (math's floats stay) */
static const mp_rom_obj_tuple_t calc_all = {
    {&mp_type_tuple},
    29,
    {MP_ROM_QSTR(MP_QSTR_Expr), MP_ROM_QSTR(MP_QSTR_CASError), MP_ROM_QSTR(MP_QSTR_sym), MP_ROM_QSTR(MP_QSTR_syms),
     MP_ROM_QSTR(MP_QSTR_solve), MP_ROM_QSTR(MP_QSTR_integrate), MP_ROM_QSTR(MP_QSTR_diff),
     MP_ROM_QSTR(MP_QSTR_limit), MP_ROM_QSTR(MP_QSTR_factor), MP_ROM_QSTR(MP_QSTR_expand),
     MP_ROM_QSTR(MP_QSTR_simplify), MP_ROM_QSTR(MP_QSTR_exact), MP_ROM_QSTR(MP_QSTR_approx),
     MP_ROM_QSTR(MP_QSTR_latex), MP_ROM_QSTR(MP_QSTR_subs), MP_ROM_QSTR(MP_QSTR_eq), MP_ROM_QSTR(MP_QSTR_plot),
     MP_ROM_QSTR(MP_QSTR_sin), MP_ROM_QSTR(MP_QSTR_cos), MP_ROM_QSTR(MP_QSTR_tan), MP_ROM_QSTR(MP_QSTR_asin),
     MP_ROM_QSTR(MP_QSTR_acos), MP_ROM_QSTR(MP_QSTR_atan), MP_ROM_QSTR(MP_QSTR_sinh), MP_ROM_QSTR(MP_QSTR_cosh),
     MP_ROM_QSTR(MP_QSTR_tanh), MP_ROM_QSTR(MP_QSTR_exp), MP_ROM_QSTR(MP_QSTR_log), MP_ROM_QSTR(MP_QSTR_sqrt)},
};


static const mp_rom_map_elem_t calc_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_calc)},
    {MP_ROM_QSTR(MP_QSTR___all__), MP_ROM_PTR(&calc_all)},
    {MP_ROM_QSTR(MP_QSTR_sin), MP_ROM_PTR(&calc_fn_sin_obj)},
    {MP_ROM_QSTR(MP_QSTR_cos), MP_ROM_PTR(&calc_fn_cos_obj)},
    {MP_ROM_QSTR(MP_QSTR_tan), MP_ROM_PTR(&calc_fn_tan_obj)},
    {MP_ROM_QSTR(MP_QSTR_asin), MP_ROM_PTR(&calc_fn_asin_obj)},
    {MP_ROM_QSTR(MP_QSTR_acos), MP_ROM_PTR(&calc_fn_acos_obj)},
    {MP_ROM_QSTR(MP_QSTR_atan), MP_ROM_PTR(&calc_fn_atan_obj)},
    {MP_ROM_QSTR(MP_QSTR_sinh), MP_ROM_PTR(&calc_fn_sinh_obj)},
    {MP_ROM_QSTR(MP_QSTR_cosh), MP_ROM_PTR(&calc_fn_cosh_obj)},
    {MP_ROM_QSTR(MP_QSTR_tanh), MP_ROM_PTR(&calc_fn_tanh_obj)},
    {MP_ROM_QSTR(MP_QSTR_exp), MP_ROM_PTR(&calc_fn_exp_obj)},
    {MP_ROM_QSTR(MP_QSTR_log), MP_ROM_PTR(&calc_fn_log_obj)},
    {MP_ROM_QSTR(MP_QSTR_sqrt), MP_ROM_PTR(&calc_fn_sqrt_obj)},
    {MP_ROM_QSTR(MP_QSTR___getattr__), MP_ROM_PTR(&calc_getattr_obj)},
    {MP_ROM_QSTR(MP_QSTR_Expr), MP_ROM_PTR(&py_type_expr)},
    {MP_ROM_QSTR(MP_QSTR_CASError), MP_ROM_PTR(&mp_type_CASError)},
    {MP_ROM_QSTR(MP_QSTR_eval), MP_ROM_PTR(&calc_eval_obj)},
    {MP_ROM_QSTR(MP_QSTR_sym), MP_ROM_PTR(&calc_sym_obj)},
    {MP_ROM_QSTR(MP_QSTR_syms), MP_ROM_PTR(&calc_syms_obj)},
    {MP_ROM_QSTR(MP_QSTR_solve), MP_ROM_PTR(&calc_solve_obj)},
    {MP_ROM_QSTR(MP_QSTR_integrate), MP_ROM_PTR(&calc_integrate_obj)},
    {MP_ROM_QSTR(MP_QSTR_diff), MP_ROM_PTR(&calc_diff_obj)},
    {MP_ROM_QSTR(MP_QSTR_limit), MP_ROM_PTR(&calc_limit_obj)},
    {MP_ROM_QSTR(MP_QSTR_factor), MP_ROM_PTR(&calc_factor_obj)},
    {MP_ROM_QSTR(MP_QSTR_expand), MP_ROM_PTR(&calc_expand_obj)},
    {MP_ROM_QSTR(MP_QSTR_simplify), MP_ROM_PTR(&calc_simplify_obj)},
    {MP_ROM_QSTR(MP_QSTR_exact), MP_ROM_PTR(&calc_exact_obj)},
    {MP_ROM_QSTR(MP_QSTR_approx), MP_ROM_PTR(&calc_approx_obj)},
    {MP_ROM_QSTR(MP_QSTR_latex), MP_ROM_PTR(&calc_latex_obj)},
    {MP_ROM_QSTR(MP_QSTR_subs), MP_ROM_PTR(&calc_subs_obj)},
    {MP_ROM_QSTR(MP_QSTR_eq), MP_ROM_PTR(&calc_eq_obj)},
    {MP_ROM_QSTR(MP_QSTR_plot), MP_ROM_PTR(&calc_plot_obj)},
};
static MP_DEFINE_CONST_DICT(calc_globals, calc_globals_table);

const mp_obj_module_t py_module_calc = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&calc_globals,
};
MP_REGISTER_MODULE(MP_QSTR_calc, py_module_calc);
