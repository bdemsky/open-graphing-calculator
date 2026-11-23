# The calc library (modules/python/py_calc.c): the CAS from Python.
# Run by tests/run_python.py in the simulator's Python module; the output must
# equal test_calc.py.exp (each check prints its result, asserts stop at the
# first wrong value with a traceback).
import calc
from calc import x, y, Expr, CASError

# symbols and arithmetic: + - * / ** with ints, floats, complex and Exprs build CAS expressions
print(x + 1, 2 * x, x - y, x / 2, x ** 2, 1 - x, 2 / x, 2 ** x)
print(-x, +x, abs(x - 3))
print((x + 1) ** 2, calc.expand((x + 1) ** 2))
e = x
e += 1
e *= 3
print(e)
print(type(x), isinstance(x, Expr), calc.sym("b2"), calc.syms("p q"))

# Python numbers become exact CAS numbers: ints of any size, fractions-like objects
print(Expr(2) ** 100 + 1)
print(Expr(1) / 3 + Expr(1) / 6)
print(Expr(10 ** 30) / 7)
class Frac:
    numerator = 3
    denominator = 4
print(Expr(Frac()) + Expr(1) / 4)
print(x + 0.5, Expr(1.25), Expr(2 + 3j))
print(Expr([1, 2, 3]), Expr((x, 2)))

# == compares mathematically, hashes follow the text
print(x * 2 == 2 * x, (x + 1) ** 2 == x ** 2 + 2 * x + 1, x == y, x != y)
print(Expr(1) / 2 == 0.5, len({x, calc.sym("x"), y}))
print(Expr(2) < 3, calc.eval("pi") > 3, calc.sqrt(Expr(2)) <= 1.5)

# conversions back to Python
print(float(calc.eval("sqrt(2)")), int(Expr(12) / 4), complex(calc.eval("2+3*i")))
print(calc.approx(calc.eval("pi")), calc.approx(Expr(1) / 3, 20))
print(bool(Expr(0)), bool(x))

# the CAS functions
print(calc.solve(x ** 2 - 4, x), calc.solve(calc.eq(x ** 2, 9)), calc.solve("x^2=2", "x"))
print(calc.integrate(x ** 2, x), calc.integrate(x ** 2, x, 0, 3), calc.integrate(calc.sin(x)))
print(calc.diff(x ** 3), calc.diff(calc.sin(x), x, 2), calc.diff(x * y, y))
print(calc.limit(calc.sin(x) / x, x, 0), calc.limit(1 / x, x, 0, 1))
print(calc.factor(x ** 2 - 1), calc.simplify(calc.sin(x) ** 2 + calc.cos(x) ** 2), calc.exact(0.25))
print(calc.subs(x ** 2 + y, x, 3), (x ** 2).subs(x, 2), calc.eval("2+3"))
print(calc.sin(calc.pi / 6), calc.sin(0.5), calc.log(x, 2), calc.sqrt(8), calc.sqrt(Expr(8)))
print([r for r in calc.solve(x ** 3 - x, x)], calc.solve(x ** 3 - x, x)[2])
print(calc.latex(calc.sqrt(x) / 2), calc.latex(Expr(-1) / 2), (x ** Expr(1) / 3).latex())

# errors: the CAS's become CASError, Python's stay Python's
try:
    calc.eval("1/0")
except CASError as err:
    print("CASError:", err)
try:
    calc.eval("((")
except CASError as err:
    print("CASError:", type(err).__name__, isinstance(err, Exception))
try:
    x + "a"
except TypeError:
    print("TypeError")
try:
    Expr(object())
except TypeError as err:
    print("TypeError:", err)
try:
    float(x)
except TypeError:
    print("TypeError: not a number")
try:
    calc.plot(lambda t: t * t)
except TypeError:
    print("TypeError: plot takes an expression")
try:
    calc.sym("2x")
except ValueError:
    print("ValueError")
try:
    calc.nosuchthing
except AttributeError:
    print("AttributeError")
print("ok")
