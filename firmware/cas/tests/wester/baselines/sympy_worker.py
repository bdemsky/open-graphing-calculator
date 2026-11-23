#!/usr/bin/env python3
"""Evaluates one translated Wester problem with SymPy (one process per
problem; run_baselines.py applies the wall-clock timeout).

stdin: JSON {"lines": [...]} -- lines produced by engine_sympy.translate():
  'assume(<our assume args>)'       symbol assumptions (see assume_)
  'NAME := EXPR' / 'NAME(a,b) := EXPR'   definitions
  'EXPR'                            Python/SymPy expression text, evaluated
                                    with parse_expr in the command namespace
stdout: JSON {"status": ok|error|uneval|unsupported, "out": <our syntax>, "msg"}
The command namespace (CMDS) is the SymPy side of the translation table: each
of our commands is mapped to the SymPy function a SymPy user would call.
"""
import functools
import itertools
import json
import re
import sys
import traceback

import sympy as sp
import sympy.stats  # noqa: F401  (sp.stats)
from sympy.parsing.sympy_parser import parse_expr, standard_transformations
from sympy.printing.str import StrPrinter


class Unsupported(Exception):
    pass


# --------------------------------------------------------------- symbols ---
SYMS = {}          # name -> Symbol with the current assumptions
FACTS = []         # assumptions SymPy symbols cannot carry (relations): for ask()


def sym(name):
    if name not in SYMS:
        SYMS[name] = sp.Symbol(name, real=True)      # our CAS's symbols are real
    return SYMS[name]


def _replace_symbol(name, **assm):
    old = sym(name)
    new = sp.Symbol(name, **assm)
    SYMS[name] = new
    for k, v in list(NS.items()):
        if isinstance(v, sp.Basic) and old in getattr(v, 'free_symbols', ()):
            NS[k] = v.subs(old, new)


def assume_(text):
    """assume(n,integer) / assume(x>0) / assume(x<0) / assume(y>=0): symbol
    assumptions; relations between symbols, abs() and intervals go to FACTS
    (used only by the ask() of a final truth-value question)."""
    a = [p.strip() for p in split_top(text)]
    if len(a) == 2 and a[1] in ('integer', 'real', 'complex', 'positive', 'negative'):
        if a[1] == 'complex':
            _replace_symbol(a[0])
        elif a[1] == 'integer':
            _replace_symbol(a[0], integer=True)
        else:
            _replace_symbol(a[0], **{a[1]: True})
        return
    rel = ev(a[0])
    for r in (rel.args if isinstance(rel, sp.And) else [rel]):
        m = None
        if isinstance(r, sp.core.relational.Relational):
            lhs, rhs = r.lhs, r.rhs
            if rhs == 0 and lhs.is_Symbol:
                m = {sp.StrictGreaterThan: 'positive', sp.StrictLessThan: 'negative',
                     sp.GreaterThan: 'nonnegative', sp.LessThan: 'nonpositive'}.get(type(r))
                if m:
                    _replace_symbol(str(lhs), real=True, **{m: True})
                    continue
        FACTS.append(r)


def q_of(r):
    """relation -> Q predicate"""
    if isinstance(r, sp.And):
        return sp.And(*[q_of(x) for x in r.args])
    if isinstance(r, sp.Or):
        return sp.Or(*[q_of(x) for x in r.args])
    d = r.lhs - r.rhs
    return {sp.StrictGreaterThan: sp.Q.positive(d), sp.StrictLessThan: sp.Q.negative(d),
            sp.GreaterThan: sp.Q.nonnegative(d), sp.LessThan: sp.Q.nonpositive(d),
            sp.Equality: sp.Q.zero(d)}[type(r)]


# -------------------------------------------------------------- commands ---
def _M(m):
    return m if isinstance(m, sp.MatrixBase) else sp.Matrix(m)


def integrate_(f, x, a=None, b=None):
    return sp.integrate(f, x) if a is None else sp.integrate(f, (x, a, b))


def sum_(f, k, a, b):
    return sp.summation(f, (k, a, b))


def product_(f, k=None, a=None, b=None):
    if k is None:                                 # product of a list
        return sp.Mul(*f)
    return sp.product(f, (k, a, b))


def limit_(f, x, a, d=None):
    return sp.limit(f, x, a, '+-' if d is None else ('+' if d > 0 else '-'))


def diff_(f, x, n=1):
    if isinstance(f, (list, sp.MatrixBase)):
        return _M(f).applyfunc(lambda e: sp.diff(e, x, n))
    return sp.diff(f, x, n)


def series_(f, x, a, n):
    if isinstance(x, (list, tuple)):
        raise Unsupported('series: SymPy has no multivariate Taylor series')
    return sp.series(f, x, a, n).removeO()


def _eqexpr(e):
    return e.lhs - e.rhs if isinstance(e, sp.Equality) else e


def _isrel(e):
    return isinstance(e, (sp.core.relational.Relational, sp.And, sp.Or)) and not isinstance(e, sp.Equality)


def solve_(eq, x):
    if isinstance(eq, (list, tuple)):
        if any(_isrel(e) for e in eq):
            return sp.reduce_inequalities(eq, x)
        sols = sp.solve([_eqexpr(e) for e in eq], x, dict=True)
        return [[sp.Eq(k, v) for k, v in s.items()] for s in sols]
    if _isrel(eq):
        return sp.solve_univariate_inequality(eq, x, relational=True)
    return sp.solve(_eqexpr(eq), x)


def csolve_(eq, x):
    # the unknowns were parsed as complex symbols (see main: csolve lines)
    eqs = eq if isinstance(eq, (list, tuple)) else [eq]
    eqs = [_eqexpr(e) for e in eqs]
    if isinstance(x, (list, tuple)):
        sols = sp.solve(eqs, x, dict=True)
        return [[sp.Eq(k, v) for k, v in s.items()] for s in sols]
    return sp.solve(eqs[0], x)


def linsolve_(eqs, xs):
    sols = sp.solve([_eqexpr(e) for e in eqs], xs, dict=True)
    return [[sp.Eq(k, v) for k, v in s.items()] for s in sols]


def dsolve_(eqs, ics, funcs, x):
    eqs = [e if isinstance(e, sp.Equality) else sp.Eq(e, 0) for e in eqs]
    if len(funcs) == 1:
        r = sp.dsolve(eqs[0], funcs[0], ics=ics or None)
    else:
        r = sp.dsolve(eqs, funcs, ics=ics or None)
    sub = {f: sp.Symbol(str(f.func)) for f in funcs}
    rs = r if isinstance(r, list) else [r]
    return [e.subs(sub) for e in rs] if len(rs) > 1 or len(funcs) > 1 else rs[0].subs(sub)


def rsolve_(eq, fn, ics):
    d = {e.lhs: e.rhs for e in ics}
    r = sp.rsolve(_eqexpr(eq), fn, d)
    if r is None:
        raise NotImplementedError('rsolve returned None')
    return r


def ifactor_(n):
    return '*'.join(str(p) if e == 1 else '%d^%d' % (p, e) for p, e in sorted(sp.factorint(n).items()))


def gcd_(*a):
    return functools.reduce(sp.gcd, a)


def approx_(e, n=15):
    if isinstance(e, (list, sp.MatrixBase)):
        return _M(e).evalf(n)
    return sp.N(e, n)


def ichinrem_(*pairs):
    from sympy.ntheory.modular import crt
    r, m = crt([p[1] for p in pairs], [p[0] for p in pairs])
    return [r, m]


def contfrac_(x, n):
    from sympy.ntheory.continued_fraction import continued_fraction_iterator
    return list(itertools.islice(continued_fraction_iterator(x), int(n)))


def digits_(n, b):
    from sympy.ntheory import digits
    return digits(n, b)[1:]


def hex_(n):
    return '"0x%X"' % int(n)


def _rv(L):
    from sympy.stats import DiscreteUniform
    return DiscreteUniform('X', list(L))


def median_(L):
    from sympy.stats import median
    m = median(_rv(L))
    return list(m)[0] if len(m) == 1 else m


def quartile1_(L):
    from sympy.stats import quantile
    return quantile(_rv(L))(sp.Rational(1, 4))


def binompdf_(n, p, k):
    from sympy.stats import Binomial, density
    return density(Binomial('X', n, p))(k)


def binomcdf_(n, p, k):
    from sympy.stats import Binomial, P
    X = Binomial('X', n, p)
    return P(X <= k)


def _cdf_between(X, a, b):
    from sympy.stats import cdf
    F = cdf(X)
    lo = 0 if a == -sp.oo else F(a)
    hi = 1 if b == sp.oo else F(b)
    return hi - lo


def normalcdf_(a, b, mu=0, s=1):
    from sympy.stats import Normal
    return _cdf_between(Normal('X', mu, s), a, b)


def tcdf_(a, b, df):
    from sympy.stats import StudentT
    return _cdf_between(StudentT('T', df), a, b)


def chi2cdf_(a, b, df):
    from sympy.stats import ChiSquared
    return _cdf_between(ChiSquared('C', df), a, b)


def cartprod_(A, B):
    return [[a, b] for a in A for b in B]


def norm_(v, p=None):
    M = _M(v)
    return M.norm() if p is None else M.norm(p)


def cross_(a, b):
    return list(_M(a).cross(_M(b)))


def _coords(vs):
    from sympy.vector import CoordSys3D
    R = CoordSys3D('R')
    to = dict(zip(vs, [R.x, R.y, R.z]))
    back = {v: k for k, v in to.items()}
    return R, to, back


def curl_(F, vs):
    from sympy.vector import curl
    R, to, back = _coords(vs)
    V = sum((sp.sympify(c).subs(to) * e for c, e in zip(F, [R.i, R.j, R.k])), sp.vector.Vector.zero)
    C = curl(V)
    return [C.dot(e).subs(back) for e in (R.i, R.j, R.k)]


def potential_(F, vs):
    from sympy.vector import scalar_potential
    R, to, back = _coords(vs)
    V = sum((sp.sympify(c).subs(to) * e for c, e in zip(F, [R.i, R.j, R.k])), sp.vector.Vector.zero)
    return scalar_potential(V, R).subs(back)


def gramschmidt_(vs):
    rows = _M(vs).tolist()
    return [list(v) for v in sp.GramSchmidt([sp.Matrix(r) for r in rows])]


def lu_(M):
    L, U, perm = _M(M).LUdecomposition()
    P = sp.eye(L.rows)
    for i, j in perm:
        P.row_swap(i, j)
    return [L, U, P]


def eigenvals_(M):
    return list(_M(M).eigenvals().keys())


def eigenvects_(M):
    return [[v, [list(x) for x in vecs]] for v, m, vecs in _M(M).eigenvects()]


def smith_(M):
    from sympy.matrices.normalforms import smith_normal_form
    M = _M(M)
    gens = sorted(M.free_symbols, key=str)
    return smith_normal_form(M, domain=sp.QQ[tuple(gens)] if gens else sp.ZZ)


def sinm_(M):
    t = sp.Dummy('t')
    return _M(M).analytic_func(sp.sin(t), t)


def grad_(f, vs):
    return list(sp.derive_by_array(f, vs))


def map_(L, f):
    return [f(v) for v in L]


def implicitdiff_(eq, y, x):
    return sp.idiff(_eqexpr(eq), y, x)


def minimize_(f, x):
    from sympy.calculus.util import minimum
    return minimum(f, x)


def _fourier(f, x, T, a):
    return sp.fourier_series(f, (x, a, a + T))


def fourier_an_(f, x, T, n, a):
    fs = _fourier(f, x, T, a)
    if n == 0:
        return fs.a0
    k = fs.an.variables[0]
    c = sp.simplify(fs.an.formula / sp.cos(2 * sp.pi * k * x / T))
    return c.subs(k, n)


def fourier_bn_(f, x, T, n, a):
    fs = _fourier(f, x, T, a)
    k = fs.bn.variables[0]
    c = sp.simplify(fs.bn.formula / sp.sin(2 * sp.pi * k * x / T))
    return c.subs(k, n)


def laplace_(f, t, s):
    return sp.laplace_transform(f, t, s, noconds=True)


def ilaplace_(F, s, t):
    return sp.inverse_laplace_transform(F, s, t)


def fourier_(f, x, k):
    return sp.fourier_transform(f, x, k)


def hypergeom_(a, b, z):
    return sp.hyperexpand(sp.hyper(a, b, z))


def piecewise_(*a):
    a = list(a)
    pairs = []
    while len(a) >= 2:
        c, v = a.pop(0), a.pop(0)
        pairs.append((v, c))
    pairs.append((a[0] if a else sp.nan, True))
    return sp.Piecewise(*pairs)


def tcollect_(e):
    from sympy.simplify.fu import TR7, TR8
    return sp.expand(TR8(TR7(e)))


def nint_(f, x, a, b):
    return sp.Integral(f, (x, a, b)).evalf()


def setop(op):
    def f(*sets):
        r = sp.FiniteSet(*sets[0])
        for s in sets[1:]:
            r = op(r, sp.FiniteSet(*s))
        return r
    return f


def numbers():
    from sympy.functions.combinatorial.numbers import partition, stirling
    return {'partition': partition, 'stirling1': lambda n, k: stirling(n, k, kind=1, signed=True)}


CMDS = {
    'integrate': integrate_, 'sum': sum_, 'product': product_, 'limit': limit_, 'diff': diff_,
    'series': series_, 'solve': solve_, 'csolve': csolve_, 'linsolve': linsolve_, 'dsolve_': dsolve_,
    'rsolve': rsolve_, 'simplify': sp.simplify, 'normal': lambda e: sp.cancel(sp.together(e)),
    'factor': sp.factor, 'expand': sp.expand, 'cfactor': lambda e: sp.factor(e, gaussian=True),
    'partfrac': lambda f, x: sp.apart(f, x), 'texpand': sp.expand_trig, 'tcollect': tcollect_,
    'trigsimp': sp.trigsimp, 'ifactor': ifactor_, 'gcd': gcd_, 'resultant': sp.resultant,
    'rem': lambda p, q, x: sp.rem(p, q, x), 'digits': digits_, 'hex': hex_,
    'log': lambda a, b=None: sp.log(a, 10) if b is None else sp.log(a, b), 'ln': sp.log,
    'invmod': sp.mod_inverse, 'approx': approx_, 'ichinrem': ichinrem_, 'prevprime': sp.prevprime,
    'nextprime': sp.nextprime, 'primroot': sp.primitive_root, 'euler': sp.totient,
    'pochhammer': sp.rf, 'factorial2': sp.factorial2, 'bernoulli': sp.bernoulli, 'fibonacci': sp.fibonacci,
    'binomial': sp.binomial, 'factorial': sp.factorial, 'contfrac': contfrac_,
    'mean': lambda L: sp.stats.E(_rv(L)), 'median': median_, 'quartile1': quartile1_,
    'stddev': lambda L: sp.stats.std(_rv(L)), 'binompdf': binompdf_, 'binomcdf': binomcdf_,
    'normalcdf': normalcdf_, 'tcdf': tcdf_, 'chi2cdf': chi2cdf_,
    'union': setop(lambda a, b: a | b), 'intersect': setop(lambda a, b: a & b),
    'setdiff': setop(lambda a, b: a - b), 'cartprod': cartprod_, 'horner': lambda p, x: sp.horner(p, wrt=x),
    'xor': sp.Xor, 'subst': lambda e, x, a: e.subs(x, a), 'map': map_, 'norm': norm_, 'cross': cross_,
    'curl': curl_, 'potential': potential_, 'gramschmidt': gramschmidt_,
    'diag': lambda M, k=0: list(_M(M).diagonal(k)), 'minor': lambda M, i, j: _M(M).minor_submatrix(i - 1, j - 1),
    'blockdiag': lambda *bs: sp.diag(*[_M(b) if isinstance(b, (list, sp.MatrixBase)) else b for b in bs]),
    'mod': lambda M, n: _M(M).applyfunc(lambda e: e % n), 'transpose': lambda M: _M(M).T,
    'conj': lambda e: _M(e).conjugate() if isinstance(e, (list, sp.MatrixBase)) else sp.conjugate(e),
    'inv': lambda M: _M(M).inv(), 'lu': lu_, 'rref': lambda M: _M(M).rref()[0], 'rank': lambda M: _M(M).rank(),
    'ker': lambda M: [list(v) for v in _M(M).nullspace()], 'det': lambda M: _M(M).det(),
    'charpoly': lambda M, x: _M(M).charpoly(x).as_expr(x), 'eigenvals': eigenvals_, 'eigenvects': eigenvects_,
    'identity': sp.eye, 'jordan': lambda M: _M(M).jordan_form()[1], 'smith': smith_,
    'expm': lambda M: _M(M).exp(), 'sinm': sinm_, 'svd': lambda M: list(_M(M).singular_value_decomposition()),
    'jacobian': lambda F, v: _M(F).jacobian(v), 'hessian': lambda f, v: sp.hessian(f, v), 'grad': grad_,
    'implicitdiff': implicitdiff_, 'residue': sp.residue, 'minimize': minimize_,
    'fourier_an': fourier_an_, 'fourier_bn': fourier_bn_, 'laplace': laplace_, 'ilaplace': ilaplace_,
    'fourier': fourier_, 'legendrep': sp.assoc_legendre, 'legendre': sp.legendre,
    'chebyshevt': sp.chebyshevt, 'besselj': sp.besselj, 'hypergeom': hypergeom_, 'zeta': sp.zeta,
    'gamma': sp.gamma, 'digamma': sp.digamma, 'ellipticE': sp.elliptic_e, 'expand_complex': sp.expand_complex, 'polylog': sp.polylog,
    'Ci': sp.Ci, 'erf': sp.erf, 'heaviside': sp.Heaviside, 'dirac': sp.DiracDelta,
    'piecewise': piecewise_, 'max': sp.Max, 'min': sp.Min, 'abs': sp.Abs, 'sign': sp.sign,
    'floor': sp.floor, 'ceil': sp.ceiling, 're': sp.re, 'im': sp.im, 'nint': nint_,
    'sqrt': sp.sqrt, 'exp': sp.exp, 'sin': sp.sin, 'cos': sp.cos, 'tan': sp.tan, 'sec': sp.sec,
    'acot': sp.acot, 'atan': sp.atan, 'asin': sp.asin, 'acos': sp.acos, 'asinh': sp.asinh,
    'sinh': sp.sinh, 'cosh': sp.cosh, 'And': sp.And, 'Or': sp.Or, 'Not': sp.Not, 'Eq': sp.Eq,
    'Matrix': sp.Matrix, 'Derivative': sp.Derivative, 'Rational': sp.Rational,
    'i': sp.I, 'e': sp.E, 'pi': sp.pi, 'inf': sp.oo, 'euler_gamma': sp.EulerGamma,
    'true': sp.true, 'false': sp.false, 'undef': sp.nan,
}
CMDS.update(numbers())
CMDS.update({'diophantine': lambda e: [list(t) for t in sp.diophantine(e)],
             'polynomial_congruence': lambda e, m: __import__('sympy.ntheory.residue_ntheory', fromlist=['x'])
             .polynomial_congruence(e, m)})
NS = {}


# ----------------------------------------------------------------- parse ---
IDENT = re.compile(r'[A-Za-z_][A-Za-z_0-9]*')
TRANS = standard_transformations


def split_top(s, sep=','):
    parts, depth, cur, i = [], 0, '', 0
    while i < len(s):
        ch = s[i]
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
        if depth == 0 and s.startswith(sep, i):
            parts.append(cur)
            cur = ''
            i += len(sep)
            continue
        cur += ch
        i += 1
    parts.append(cur)
    return parts


def ev(code):
    loc = dict(CMDS)
    loc.update(NS)
    for m in IDENT.finditer(code):
        nm = m.group(0)
        nxt = code[m.end():m.end() + 1]
        prev = code[m.start() - 1:m.start()] if m.start() else ''
        if nm in loc or prev == '.' or nxt == '(' or nm in ('lambda',):
            continue
        loc[nm] = sym(nm)
    return parse_expr(code, local_dict=loc, transformations=TRANS)


# ----------------------------------------------------------------- print ---
class OurPrinter(StrPrinter):
    def _print_ImaginaryUnit(self, e):
        return 'i'

    def _print_Exp1(self, e):
        return 'e'

    def _print_Infinity(self, e):
        return 'inf'

    def _print_NegativeInfinity(self, e):
        return '-inf'

    def _print_ComplexInfinity(self, e):
        return 'undef'

    def _print_NaN(self, e):
        return 'undef'

    def _print_EulerGamma(self, e):
        return 'euler_gamma'

    def _print_log(self, e):
        return 'ln(%s)' % self._print(e.args[0])

    def _print_Abs(self, e):
        return 'abs(%s)' % self._print(e.args[0])

    def _print_conjugate(self, e):
        return 'conj(%s)' % self._print(e.args[0])

    def _print_ceiling(self, e):
        return 'ceil(%s)' % self._print(e.args[0])

    def _print_Heaviside(self, e):
        return 'heaviside(%s)' % self._print(e.args[0])

    def _print_DiracDelta(self, e):
        if len(e.args) == 1:
            return 'dirac(%s)' % self._print(e.args[0])
        return 'diff(dirac(%s),%s)' % (self._print(e.args[0]), self._print(e.args[1]))

    def _print_Piecewise(self, e):
        parts = []
        for ex, c in e.args:
            if c is sp.true:
                parts.append(self._print(ex))
                return 'piecewise(%s)' % ','.join(parts)
            parts += [self._cond(c), self._print(ex)]
        parts.append('undef')
        return 'piecewise(%s)' % ','.join(parts)

    def _lims(self, e, name):
        f = self._print(e.function)
        out = [f]
        for lim in e.limits:
            out = ['%s(%s)' % (name, ','.join([out[0]] + [self._print(x) for x in lim]))]
        return out[0]

    def _print_Integral(self, e):
        return self._lims(e, 'integrate')

    def _print_Sum(self, e):
        return self._lims(e, 'sum')

    def _print_Product(self, e):
        return self._lims(e, 'product')

    def _print_Limit(self, e):
        return 'limit(%s,%s,%s)' % tuple(self._print(x) for x in e.args[:3])

    def _print_Derivative(self, e):
        s = self._print(e.expr)
        for v, n in e.variable_count:
            s = 'diff(%s,%s%s)' % (s, self._print(v), '' if n == 1 else ',%s' % n)
        return s

    def _print_Subs(self, e):
        ex, old, new = e.args
        return 'subst(%s,%s,%s)' % (self._print(ex), self._print(old[0]), self._print(new[0]))

    def _print_Equality(self, e):
        return '%s=%s' % (self._print(e.lhs), self._print(e.rhs))

    def _print_Unequality(self, e):
        return 'Ne(%s,%s)' % (self._print(e.lhs), self._print(e.rhs))

    def _cond(self, c):
        """a condition inside a call: functional And/Or/Eq so that it parses"""
        if isinstance(c, sp.Equality):
            return 'Eq(%s,%s)' % (self._print(c.lhs), self._print(c.rhs))
        if isinstance(c, (sp.And, sp.Or, sp.Not)):
            return '%s(%s)' % (type(c).__name__, ','.join(self._cond(a) for a in c.args))
        return self._print(c)

    def _print_And(self, e):
        return self._cond(e)

    _print_Or = _print_Not = _print_And

    def _print_exp_polar(self, e):
        return 'exp(%s)' % self._print(e.args[0])

    def _print_Intersection(self, e):
        return 'intersect(%s)' % ','.join(self._print(a) for a in e.args)

    def _print_Union(self, e):
        return 'union(%s)' % ','.join(self._print(a) for a in e.args)

    def _print_Complement(self, e):
        return 'setdiff(%s)' % ','.join(self._print(a) for a in e.args)

    def _print_BooleanTrue(self, e):
        return 'true'

    def _print_BooleanFalse(self, e):
        return 'false'

    def _print_MatrixBase(self, e):
        return '[' + ','.join('[' + ','.join(self._print(x) for x in row) + ']' for row in e.tolist()) + ']'

    _print_ImmutableMatrix = _print_MutableDenseMatrix = _print_ImmutableDenseMatrix = _print_MatrixBase

    def _print_list(self, e):
        return '[' + ','.join(self._print(x) for x in e) + ']'

    _print_tuple = _print_Tuple = _print_list

    def _print_FiniteSet(self, e):
        return '[' + ','.join(self._print(x) for x in e.args) + ']'

    def _print_EmptySet(self, e):
        return '[]'

    def _print_Interval(self, e):
        return 'solve(%s)' % StrPrinter._print_Interval(self, e)

    def _print_RisingFactorial(self, e):
        return 'rf(%s,%s)' % (self._print(e.args[0]), self._print(e.args[1]))

    def _print_hyper(self, e):
        return 'hypergeom(%s,%s,%s)' % (self._print(list(e.ap)), self._print(list(e.bq)), self._print(e.argument))

    def _print_ComplexRootOf(self, e):
        return 'rootof(%s,%s)' % (self._print(e.expr), e.index)

    def _print_ConditionSet(self, e):
        return 'solve(%s)' % self._print(e.condition)

    def _print_ImageSet(self, e):
        return 'solve(%s)' % StrPrinter._print_ImageSet(self, e)

    def _print_Order(self, e):
        return '0'

    def _print_str(self, e):
        return e


def show(v):
    if isinstance(v, dict):
        v = [sp.Eq(k, w) for k, w in v.items()]
    s = OurPrinter({'full_prec': False}).doprint(v)
    s = s.replace('**', '^').replace(', ', ',')
    s = re.sub(r'\bC(\d+)\b', r'c\1', s)
    return s


# ------------------------------------------------------------------ main ---
def make_function(params, body):
    """f(a,b) := body: re-evaluated with the parameters bound to the call's
    arguments (works for list-valued bodies, unlike Lambda)"""
    def fn(*vals):
        saved = {p: NS.get(p) for p in params}
        for p, v in zip(params, vals):
            NS[p] = v
        try:
            return ev(body)
        finally:
            for p, v in saved.items():
                if v is None:
                    NS.pop(p, None)
                else:
                    NS[p] = v
    return fn


def expand_rootsums(r):
    """RootSum (an integrate result over the roots of a polynomial) -> doit()"""
    if isinstance(r, sp.Basic) and r.has(sp.RootSum):
        try:
            return r.replace(lambda e: isinstance(e, sp.RootSum), lambda e: e.doit())
        except Exception:
            return r
    return r


def ask_truth(r):
    """a final unevaluated relation: decide it with ask() under the assumptions"""
    if not isinstance(r, (sp.core.relational.Relational, sp.And, sp.Or)):
        return r
    facts = [sp.Q.real(s) for s in r.free_symbols] + [q_of(f) for f in FACTS]
    for s in r.free_symbols:
        if s.is_positive:
            facts.append(sp.Q.positive(s))
        if s.is_negative:
            facts.append(sp.Q.negative(s))
    try:
        a = sp.ask(q_of(r), sp.And(*facts))
    except Exception:
        return r
    return r if a is None else sp.sympify(bool(a))


def main():
    job = json.load(sys.stdin)
    res = None
    try:
        for k, line in enumerate(job['lines']):
            last = k == len(job['lines']) - 1
            m = re.match(r'^\s*assume\((.*)\)\s*$', line)
            if m:
                assume_(m.group(1))
                res = sp.true
                continue
            m = re.match(r'^\s*([A-Za-z_]\w*)\s*(\(([^()]*)\))?\s*:=\s*(.*)$', line)
            if m:
                name, args, body = m.group(1), m.group(3), m.group(4)
                if args is not None:
                    NS[name] = make_function([p.strip() for p in args.split(',')], body)
                else:
                    NS[name] = ev(body)
                res = NS[name]
                continue
            m = re.match(r'^\s*csolve\((.*)\)\s*$', line)
            if m:
                xs = split_top(m.group(1))[-1].strip().strip('[]')
                saved = dict(SYMS)
                for v in split_top(xs):
                    SYMS[v.strip()] = sp.Symbol(v.strip())          # complex unknowns
                res = ev(line)
                SYMS.clear()
                SYMS.update(saved)
                continue
            res = ev(line)
        res = expand_rootsums(res)
        if job.get('ask'):
            res = ask_truth(res)
        print(json.dumps({'status': 'ok', 'out': show(res)}))
    except Unsupported as ex:
        print(json.dumps({'status': 'unsupported', 'msg': str(ex)}))
    except NotImplementedError as ex:
        print(json.dumps({'status': 'uneval', 'msg': 'NotImplementedError: %s' % str(ex)[:300]}))
    except Exception as ex:
        tb = traceback.format_exc().strip().split('\n')[-1]
        print(json.dumps({'status': 'error', 'msg': '%s: %s' % (type(ex).__name__, str(ex)[:300]),
                          'tb': tb[:300]}))


if __name__ == '__main__':
    main()
