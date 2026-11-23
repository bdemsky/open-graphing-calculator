"""Second-tier (harder) curated problems for the differential harness.
Each entry: (category, input, check, extra-spec-dict).  References are
computed with SymPy inside difftest.py where needed."""
import sympy as sp

x, y, z, a, b, c, n, k, t, s = sp.symbols('x y z a b c n k t s')

HARD_INTEGRALS = [
    # Stewart-style techniques
    "x^2*sqrt(1-x^2)", "1/(x^2*sqrt(4-x^2))", "sqrt(9-x^2)/x^2", "x^3/sqrt(x^2+4)", "1/((x^2+1)^(3/2))",
    "x/sqrt(1-x^4)", "exp(x)*sqrt(1-exp(2*x))", "1/(x*sqrt(x^2-4))", "sqrt(x^2-9)/x", "1/(1+sqrt(x))",
    "sqrt(x)/(1+x)", "1/(x^(1/2)+x^(1/3))", "x^5*exp(-x^2)", "ln(x)/x^2", "ln(1+x^2)", "x*atan(x)^2",
    "exp(2*x)*cos(3*x)", "sin(ln(x))", "cos(ln(x))", "x^2*sin(2*x)", "sec(x)^3", "tan(x)^3", "tan(x)^4",
    "sec(x)^4", "sin(x)^4", "cos(x)^6", "sin(3*x)*sin(5*x)", "cos(2*x)*cos(4*x)", "sin(x)^3*cos(x)^3",
    "1/(sin(x)*cos(x))", "1/(1+sin(x)+cos(x))", "1/(3+2*cos(x))", "sin(x)/(1+cos(x)^2)", "cos(x)/(1+sin(x)^2)",
    "(x^3+x)/(x^4+2*x^2+1)", "(x+2)/(x^2+4*x+8)", "1/(x^3+x)", "(5*x^2+3*x-2)/(x^3+2*x^2)",
    "(x^2+1)/((x-1)*(x-2)*(x-3))", "x^4/(x^4-1)", "1/(x^4+x^2+1)", "(2*x^2-x+4)/(x^3+4*x)",
    "1/((x-1)^2*(x+1)^2)", "x/(x^4+1)", "x^2/(x^6+1)", "(x^3+2)/(x^2+1)^2", "1/(x^5+1)",
    "exp(x)/(exp(2*x)+1)", "exp(x)/(exp(x)+1)^2", "1/(exp(x)-1)", "1/(1+exp(-x))", "sqrt(1+exp(x))",
    "x*sinh(x)", "cosh(x)^3", "1/cosh(x)", "tanh(x)^2", "asin(x)/sqrt(1-x^2)", "acos(x)", "x*acos(x)",
    "atan(sqrt(x))", "ln(x+sqrt(x^2+1))", "x*exp(x)*sin(x)", "x^2*exp(-x)*cos(x)", "sqrt(x)*exp(sqrt(x))",
    "1/(x*ln(x)*ln(ln(x)))", "ln(ln(x))/x", "x^x*(1+ln(x))", "exp(x)*(1/x-1/x^2)", "(1-x^2)/(1+x^2)^2",
    "1/(x^2-2*x+5)^2", "x^7/(x^4+1)", "cos(x)^2*sin(x)^2", "sin(x)*exp(-2*x)", "abs(x)", "sign(x)*x",
    "1/sqrt(2*x-x^2)", "1/sqrt(x^2+4*x+13)", "sqrt(x^2+4*x+13)", "x/sqrt(x^2+2*x+2)", "(3*x-1)/sqrt(x^2+1)",
]

HARD_LIMITS = [
    ("(1+x)^(1/x)", "0", sp.E), ("(1-2/x)^x", "inf", sp.exp(-2)), ("x^2*sin(1/x)", "0", 0),
    ("(tan(x)-x)/x^3", "0", sp.Rational(1, 3)), ("(x*cos(x)-sin(x))/x^3", "0", -sp.Rational(1, 3)),
    ("(exp(x)-exp(-x))/x", "0", 2), ("(1-cos(x)^2)/x^2", "0", 1), ("(sqrt(1+x)-sqrt(1-x))/x", "0", 1),
    ("(x^(1/3)-1)/(x-1)", "1", sp.Rational(1, 3)), ("(2^x-1)/x", "0", sp.log(2)), ("x*ln(1+1/x)", "inf", 1),
    ("ln(x)*ln(1-x)", "1", 0), ("(ln(x))^2/x", "inf", 0), ("x^(1/ln(x))", "inf", sp.E),
    ("(sin(x)/x)^(1/x^2)", "0", sp.exp(-sp.Rational(1, 6))), ("(1/x^2-1/sin(x)^2)", "0", -sp.Rational(1, 3)),
    ("(x-sin(x))/(x-tan(x))", "0", -sp.Rational(1, 2)), ("(cos(x)-1)/(exp(x^2)-1)", "0", -sp.Rational(1, 2)),
    ("x^2*exp(-x^2)", "inf", 0), ("(3*x^3-2*x)/(x^3+5)", "-inf", 3), ("sqrt(x^2+2*x)-x", "inf", 1),
    ("sqrt(x^2+2*x)+x", "-inf", -1), ("exp(x)/x^x", "inf", 0), ("(x+sin(x))/x", "inf", 1),
    ("atan(1/x)", "0", None), ("x*exp(1/x)-x", "inf", 1), ("(ln(1+x)-x)/x^2", "0", -sp.Rational(1, 2)),
    ("(x^x-1)/(x*ln(x))", "1", 1), ("(asin(x)-x)/x^3", "0", sp.Rational(1, 6)), ("x*(exp(1/x)-1)", "inf", 1),
    ("(1+1/x)^x-exp(1)", "inf", 0), ("x*(sqrt(x^2+1)-x)", "inf", sp.Rational(1, 2)),
    ("(cosh(x)-1)/x^2", "0", sp.Rational(1, 2)), ("tanh(x)", "-inf", -1), ("ln(sin(x))/ln(x)", "0", 1),
]

HARD_SOLVE = [
    ("x^4-4*x^2+3=0", 4), ("x^3-7*x+6=0", 3), ("2*x^3-3*x^2-11*x+6=0", 3), ("x^6-7*x^3-8=0", 2),
    ("sqrt(x+3)+sqrt(x)=3", 1), ("sqrt(3*x+1)-sqrt(x-1)=2", 2), ("ln(x)+ln(x-2)=ln(3)", 1),
    ("exp(x)-6*exp(-x)=1", 1), ("2^(2*x)-5*2^x+4=0", 2), ("log(x)+log(x-3)=1", 1), ("3^x=2^(x+1)", 1),
    ("x^(2/3)=4", None), ("abs(2*x-3)=5", 2), ("abs(x^2-4)=3", 4), ("1/(x-1)+1/(x+1)=1", 2),
    ("(x+1)/(x-2)=(x-3)/(x+4)", 1), ("x^4+x^3-x-1=0", 2), ("tan(x)=sqrt(3)", 1), ("2*sin(x)^2-1=0", 1),
    ("cos(x)^2-sin(x)=1", None), ("x*ln(x)=1", 1), ("exp(-x)=x", 1), ("x^5+x^4-2*x^3-2*x^2+x+1=0", 2),
    ("x^4-2*x^3-x^2+2*x=0", 4), ("6*x^4-5*x^3-38*x^2-5*x+6=0", 4),
]

HARD_FACTOR = [
    "x^4+64", "x^6-64", "x^8+x^4+1", "a^3-b^3+a-b", "x^3+3*x^2*y+3*x*y^2+y^3-1",
    "x^4-2*x^2*y^2+y^4", "x^5-x^4-x+1", "4*x^4+1", "x^4-x^2-2*x-1", "x^2*y-y^3+x^2*z-y^2*z",
    "x^6+2*x^5+3*x^4+4*x^3+3*x^2+2*x+1", "x^8-256", "a*c+a*d+b*c+b*d", "x^3*y-x*y^3",
    "x^9-1", "x^2-2", "x^2+1", "36*x^4-97*x^2+36", "x^3-x^2*y-x*y^2+y^3", "x^4+x^3+x^2+x+1",
    "2*x^5-3*x^4-4*x^3+6*x^2+2*x-3", "(x^2+3*x+2)*(x^2-9)", "x^16-1", "x^4+4*y^4", "x^24-1",
]

HARD_SIMPLIFY = [
    ("sin(2*x)/sin(x)", "2*cos(x)"), ("(1-cos(2*x))/sin(2*x)", "tan(x)"), ("cos(x)/(1-sin(x))-tan(x)", "sec(x)"),
    ("sin(x)^4+2*sin(x)^2*cos(x)^2+cos(x)^4", "1"), ("tan(x)+1/tan(x)", "2/sin(2*x)"),
    ("(x^2-4)/(x^2+4*x+4)", "(x-2)/(x+2)"), ("(x^3+y^3)/(x+y)", "x^2-x*y+y^2"),
    ("1/(1+1/(1+1/x))", "(2*x+1)/(x+1)*0+(x+1)/(2*x+1)"), ("exp(ln(x)+ln(y))", "x*y"),
    ("sqrt(8)+sqrt(18)", "5*sqrt(2)"), ("(sqrt(5)+1)*(sqrt(5)-1)", "4"), ("(a/b-b/a)/(1/a+1/b)", "a-b"),
    ("cos(x)^2*(1+tan(x)^2)", "1"), ("sin(x)*cos(x)*tan(x)+cos(x)^2", "1"), ("2*cos(x)^2-1", "cos(2*x)"),
    ("exp(x)^2/exp(2*x-1)", "e"), ("(x^(1/2))^4", "x^2"), ("ln(exp(2))", "2"), ("cosh(x)+sinh(x)", "exp(x)"),
    ("x/(x-y)+y/(y-x)", "1"),
]

HARD_SERIES = [
    ("1/(1-x-x^2)", 0, 8), ("exp(x)/(1-x)", 0, 6), ("sqrt(1-2*x)", 0, 6), ("atan(x)/x", 0, 8),
    ("ln((1+x)/(1-x))", 0, 8), ("cos(sqrt(x))", 0, 5), ("x^2/sin(x)^2", 0, 6), ("exp(x^2)*cos(x)", 0, 8),
    ("tan(x)", sp.pi / 4, 4), ("1/(x^2+x)", 1, 4), ("exp(1/(1-x))", 0, 4), ("asinh(x)", 0, 8),
    ("ln(1+sin(x))", 0, 6), ("sec(x)", 0, 8), ("(1+x)^x", 0, 5),
]

HARD_SUMS = [
    ("sum(k*(k+1),k,1,n)", sp.summation(k * (k + 1), (k, 1, n))), ("sum(1/(k*(k+2)),k,1,inf)", sp.Rational(3, 4)),
    ("sum(k^2/2^k,k,1,inf)", 6), ("sum((2/3)^k,k,1,inf)", 2), ("sum(1/(k^2+3*k+2),k,0,n)", sp.summation(1 / ((k + 1) * (k + 2)), (k, 0, n))),
    ("sum(binomial(10,k),k,0,10)", 1024), ("sum(k*binomial(n,k),k,0,n)", None), ("sum(1/k^6,k,1,inf)", sp.pi**6 / 945),
    ("sum(k^5,k,1,n)", sp.summation(k**5, (k, 1, n))), ("product(1+1/k,k,1,n)", n + 1),
    ("sum((-1)^(k+1)/k,k,1,inf)", sp.log(2)), ("sum(1/(2*k-1)^2,k,1,inf)", sp.pi**2 / 8),
]

HARD_LINALG = [
    ("det([[1,2,3],[4,5,6],[7,8,9]])", 0), ("rank([[1,2,3],[4,5,6],[7,8,9]])", 2),
    ("inv([[1,1,1],[1,2,4],[1,3,9]])", sp.Matrix([[1, 1, 1], [1, 2, 4], [1, 3, 9]]).inv().tolist()),
    ("det([[x,1,1],[1,x,1],[1,1,x]])", sp.expand((x - 1)**2 * (x + 2))),
    ("eigenvals([[4,1],[2,3]])", [2, 5]), ("eigenvals([[2,0,0],[0,3,4],[0,4,9]])", [1, 2, 11]),
    ("charpoly([[2,1],[1,2]],x)", x**2 - 4 * x + 3), ("rref([[2,4,6,8],[1,3,5,7],[1,1,1,1]])",
                                                       sp.Matrix([[2, 4, 6, 8], [1, 3, 5, 7], [1, 1, 1, 1]]).rref()[0].tolist()),
    ("[[1,2],[3,4]]^10", (sp.Matrix([[1, 2], [3, 4]])**10).tolist()),
    ("det([[cos(t),-sin(t)],[sin(t),cos(t)]])", 1),
]

# ---- tier 3: harder classics ----
T3_INTEGRALS = [
    "x/(x^3+1)", "ln(x^2+1)/x^2", "x^2*asin(x)", "asin(x)^2", "sec(x)^5", "1/(sin(x)+cos(x))",
    "sqrt(1+x^2)/x", "x*sqrt(x+1)", "1/(x*sqrt(1+x))", "(1+x)^(1/3)", "ln(x)/(1+x)^2", "x*ln(1+x)",
    "exp(2*x)*sin(exp(x))", "1/(1+tan(x))", "tan(x)^5", "cot(x)^3", "csc(x)^3", "sin(2*x)*exp(sin(x))",
    "x*cos(x)^2", "x^2*exp(x)*sin(x)", "1/(x^4-x^2)", "x^5/(x^2+1)^3", "(x^2-1)/(x^4+x^2+1)",
    "1/(x*(x^4+1))", "x^3/sqrt(1-x^2)", "1/(x^2*sqrt(x^2-1))", "sqrt(x)/(1+x^(1/3))", "1/(1+x^(1/3))",
    "exp(sqrt(x))/sqrt(x)", "sin(sqrt(x))", "x*exp(-x)*sin(x)", "atan(x)/x^2", "ln(x+1)/sqrt(x+1)",
    "ln(sin(x))*cos(x)", "cos(x)*ln(cos(x))*0+sin(x)*cos(x)/(1+sin(x)^2)", "1/(2+cos(x))^2",
    "(x+1)*exp(x)/x^2*0+exp(x)*(x-1)/x^2", "x*sec(x)*tan(x)", "sinh(x)*sin(x)", "cosh(x)^2*sinh(x)",
    "1/(exp(x)+exp(2*x))", "x^2/(x+1)^10", "(x^3+1)/(x^4+4*x)", "1/(x^3-x^2-x+1)", "x*2^(x^2)",
    "3^x*2^x", "ln(x)^4", "x^7*exp(x^4)", "exp(x)*ln(1+exp(x))", "1/(x*(1+ln(x)^2))",
]

T3_LIMITS = [
    ("x^7/exp(x)", "inf", 0), ("(3^x+5^x)^(1/x)", "inf", 5), ("sin(x)^x", "0", 1), ("(1-cos(x))^x", "0", 1),
    ("x^(1/x^2)", "inf", 1), ("(x+1)*ln(x+1)-x*ln(x)", "inf", None), ("exp(x)*(exp(1/x)-1)/x^0-exp(x)/x", "inf", None),
    ("x*(exp(1/x)-1)", "inf", 1), ("ln(x)^ln(x)/x^2", "inf", None), ("(2^x-x^2)/(x-2)", "2", 4 * sp.log(2) - 4),
    ("(tan(x))^(tan(2*x))", "pi/4", sp.exp(-1)), ("(1/x)^tan(x)", "0", 1), ("(sin(x)-sinh(x))/x^3", "0", -sp.Rational(1, 3)),
    ("(exp(x)-exp(sin(x)))/x^3", "0", sp.Rational(1, 6)), ("(1+x)^(1/x)-exp(1)", "0", None),
    ("x/sqrt(x^2+1)", "-inf", -1), ("(x^2+1)/(x+1)-x", "inf", -1), ("ln(x+1)-ln(x)", "inf", 0),
    ("x^2*(cos(1/x)-1)", "inf", -sp.Rational(1, 2)), ("(cos(x)-1+x^2/2)/x^4", "0", sp.Rational(1, 24)),
    ("(2^x+3^x)/(3^x+x^9)", "inf", 1), ("(x^2+exp(x))/(exp(x)+x^3)", "inf", 1), ("(5^x-2^x)/(5^x+x)", "inf", 1),
    ("(exp(x)+x)/(exp(2*x)+1)", "inf", 0), ("(2*exp(x)+x)/(3*exp(x)-x^2)", "inf", sp.Rational(2, 3)),
    ("x/(x+sin(x))", "inf", 1), ("(x^2+1)/(x+1)^2", "inf", 1), ("ln(2^x+x^5)/x", "inf", sp.log(2)),
    ("x-ln(exp(x)+1)", "inf", 0), ("ln(x)^ln(x)/x^2", "inf", sp.oo), ("x^ln(x)/exp(x)", "inf", 0),
    ("exp(ln(x)^2)/x^5", "inf", sp.oo), ("(3^x+5^x)^(1/x)", "inf", 5), ("exp(x)/(exp(x)+exp(x/2))", "inf", 1),
    ("(exp(2*x)-exp(x))/(exp(2*x)+x)", "inf", 1), ("x^3*exp(-x)+exp(-x^2)*x^10", "inf", 0),
    ("(x^x)/(2^x)", "inf", sp.oo), ("(2^x)/(x^x)", "inf", 0), ("ln(ln(x))/ln(x)", "inf", 0),
    ("x^(1/ln(ln(x)))", "inf", sp.oo),
]

T3_SOLVE = [
    ("ln(x)=x-2", 2), ("sin(x)=cos(2*x)", None), ("x^x=2", 1), ("exp(x)=x+2", 2), ("x^4+4=0", 0),
    ("2*cos(x)^2+3*cos(x)+1=0", None), ("tan(x)^2=3", None), ("x^(1/3)+x^(1/2)=2", 1), ("(x^2-1)*exp(x)=0", 2),
    ("x^3+x^2+x+1=0", 1), ("sqrt(x+5)=x+3", 1), ("abs(x)=abs(x-4)", 1), ("x^5=32", 1), ("1/(x-1)=1/(x^2-1)", 1),
]

T3_SIMPLIFY = [
    ("(sin(x)+cos(x))^2-sin(2*x)", "1"), ("tan(2*x)*(1-tan(x)^2)/tan(x)", "2"), ("sec(x)^2-tan(x)^2", "1"),
    ("(1-cos(x))*(1+cos(x))/sin(x)", "sin(x)"), ("exp(x+ln(2))", "2*exp(x)"), ("sqrt(12)-sqrt(27)+sqrt(75)", "4*sqrt(3)"),
    ("(x^2+x-6)/(x^2-4)", "(x+3)/(x+2)"), ("1/(1/x+1/y)", "x*y/(x+y)"), ("sin(3*x)-3*sin(x)+4*sin(x)^3", "0"),
    ("cos(x+y)+cos(x-y)", "2*cos(x)*cos(y)"),
]

# ---- tier 4: breadth ----
T4_EXACT = [
    ("det([[a,b,c],[b,c,a],[c,a,b]])", sp.Matrix([[a, b, c], [b, c, a], [c, a, b]]).det()),
    ("inv([[1,a],[0,1]])", [[1, -a], [0, 1]]),
    ("[[cos(t),-sin(t)],[sin(t),cos(t)]]^2", (sp.Matrix([[sp.cos(t), -sp.sin(t)], [sp.sin(t), sp.cos(t)]])**2).tolist()),
    ("rank([[1,a],[a,1]])", 2), ("trace([[a,1],[2,b]])", a + b),
    ("charpoly([[a,1],[1,a]],x)", x**2 - 2 * a * x + a**2 - 1),
    ("eigenvals([[1,2,0],[2,1,0],[0,0,5]])", [-1, 3, 5]),
    ("det(matrix(5,5,1)+identity(5))", 6),
    ("transpose([[1,2],[3,4]])*[[1,2],[3,4]]", [[10, 14], [14, 20]]),
    ("solve(a*x^2-b=0,x)", [-sp.sqrt(b / a), sp.sqrt(b / a)]),
    ("solve(x^2-2*a*x+a^2=0,x)", [a]),
    ("solve(exp(a*x)=b,x)", [sp.log(b) / a]),
    ("gcd(x^3-a^3,x^2-a^2)", x - a), ("lcm(x^2-1,x^2-2*x+1)", sp.expand((x - 1)**2 * (x + 1))),
    ("quo(x^4+1,x^2+1,x)", x**2 - 1), ("rem(x^4+1,x^2+1,x)", 2),
    ("degree(x^5*y^2+x^3,x)", 5), ("coeff(3*x^4+2*x^2+7,x,2)", 2), ("coeffs(x^3-2*x+1,x)", [1, 0, -2, 1]),
    ("numer((x+1)/(x-1)+1)", 2 * x), ("denom((x+1)/(x-1)+1)", x - 1),
    ("subst(x^2+y,x=2)", 4 + y), ("subst(sin(x)+x,x,pi)", sp.pi),
    ("collect(a*x+b*x+c,x)", (a + b) * x + c),
    ("tangent(x^2,x=1)", 2 * x - 1),
    ("ifactor(2^32-1)", 2**32 - 1), ("divisors(100)", [1, 2, 4, 5, 10, 20, 25, 50, 100]),
    ("euler(97)", 96), ("iegcd(240,46)", [-9, 47, 2]), ("ichinrem([2,3],[3,5])", [8, 15]),
    ("sqrt(50)+sqrt(18)-sqrt(8)", 6 * sp.sqrt(2)), ("(sqrt(3)+sqrt(2))*(sqrt(3)-sqrt(2))", 1),
    ("simplify(2/(sqrt(5)-1))", (sp.sqrt(5) + 1) / 2), ("simplify(sqrt(7+4*sqrt(3)))", 2 + sp.sqrt(3)),
    ("laplace(t^2*exp(3*t),t,s)", 2 / (s - 3)**3), ("ilaplace(s/(s^2+4*s+5),s,t)", sp.exp(-2 * t) * (sp.cos(t) - 2 * sp.sin(t))),
    ("lagrange([0,1,2],[1,3,7],x)", x**2 + x + 1),
    ("diff(f(x)*g(x),x)", None), ("integrate(diff(sin(x)*x,x),x)", sp.sin(x) * x),
    ("sum(1/(k*(k+1)*(k+2)),k,1,inf)", sp.Rational(1, 4)), ("sum(k^2/2^k,k,1,inf)", 6),
    ("limit(((x+1)/(x-1))^x,x,inf)", sp.exp(2)), ("limit(x*(pi/2-atan(x)),x,inf)", 1),
    ("limit((1+1/x^2)^(x^3),x,inf)", sp.oo), ("limit(ln(x)/ln(ln(x)),x,inf)", sp.oo),
]

T4_SERIES = [
    ("sqrt(x^2+1)", sp.oo, 3), ("1/(x-1)", sp.oo, 4), ("x/(x+1)", sp.oo, 4), ("exp(1/x)", sp.oo, 4),
    ("ln(1+1/x)", sp.oo, 4), ("atan(x)", sp.oo, 4), ("sin(x)/x^2", 0, 3), ("cot(x)", 0, 5),
    ("1/(1-cos(x))", 0, 3), ("ln(x)", 2, 4), ("exp(x)", -1, 3), ("x^x", 1, 3), ("sqrt(x)", 1, 4),
]
