#!/usr/bin/env python3
"""
SymPy's own unit tests -> cases.jsonl for our CAS.

Walks the installed SymPy test modules with the `ast` module (the modules are
never imported or executed).  Simple statements of the form

    assert CALL(args) == EXPECTED

are kept when CALL is a command or function we have (integrate, diff, limit,
series, solve, simplify, factor, apart, gcd, summation, Matrix.det, sin, ...).
The arguments and EXPECTED are evaluated with SymPy in a restricted namespace
(SymPy names, the symbols and simple assignments of the enclosing module and
test function, no Python builtins beyond a few constructors); the evaluated
arguments are printed in our syntax by OurPrinter.  Symbols with SymPy
assumptions become assume(...) setup lines (positive, negative, nonnegative,
integer); others that our CAS cannot express mark the case `uncertain`.

Usage: extract.py OUT.jsonl [SYMPY_DIR]
Source: SymPy 1.14.0 (BSD-3-Clause), the copy installed in the project venv.
"""
import ast
import json
import os
import re
import signal
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'lib'))
sys.path.insert(0, HERE)
import sympy as sp                       # noqa: E402
from sympy.printing.str import StrPrinter  # noqa: E402
import suitelib as S                     # noqa: E402
from mparse import RESERVED_OUR          # noqa: E402
from overrides import OVERRIDES          # noqa: E402

FILES = [
    'integrals/tests/test_integrals.py', 'integrals/tests/test_heurisch.py',
    'integrals/tests/test_manual.py', 'integrals/tests/test_risch.py',
    'integrals/tests/test_rationaltools.py', 'integrals/tests/test_trigonometry.py',
    'integrals/tests/test_meijerint.py', 'integrals/tests/test_failing_integrals.py',
    'integrals/tests/test_laplace.py',
    'series/tests/test_limits.py', 'series/tests/test_gruntz.py', 'series/tests/test_demidovich.py',
    'series/tests/test_series.py', 'series/tests/test_nseries.py', 'series/tests/test_limitseq.py',
    'series/tests/test_residues.py',
    'solvers/tests/test_solvers.py', 'solvers/tests/test_solveset.py', 'solvers/tests/test_polysys.py',
    'simplify/tests/test_simplify.py', 'simplify/tests/test_trigsimp.py', 'simplify/tests/test_ratsimp.py',
    'simplify/tests/test_radsimp.py', 'simplify/tests/test_powsimp.py', 'simplify/tests/test_sqrtdenest.py',
    'simplify/tests/test_combsimp.py', 'simplify/tests/test_gammasimp.py', 'simplify/tests/test_fu.py',
    'polys/tests/test_polytools.py', 'polys/tests/test_partfrac.py', 'polys/tests/test_polyroots.py',
    'polys/tests/test_rationaltools.py', 'polys/tests/test_polyfuncs.py',
    'matrices/tests/test_matrices.py', 'matrices/tests/test_determinant.py', 'matrices/tests/test_eigen.py',
    'matrices/tests/test_commonmatrix.py', 'matrices/tests/test_matrixbase.py',
    'matrices/tests/test_reductions.py',
    'concrete/tests/test_sums_products.py', 'concrete/tests/test_products.py',
    'concrete/tests/test_gosper.py',
    'functions/elementary/tests/test_complexes.py', 'functions/elementary/tests/test_exponential.py',
    'functions/elementary/tests/test_hyperbolic.py', 'functions/elementary/tests/test_integers.py',
    'functions/elementary/tests/test_miscellaneous.py', 'functions/elementary/tests/test_piecewise.py',
    'functions/elementary/tests/test_trigonometric.py',
    'functions/combinatorial/tests/test_comb_factorials.py',
    'functions/special/tests/test_gamma_functions.py', 'functions/special/tests/test_error_functions.py',
    'functions/special/tests/test_zeta_functions.py', 'functions/special/tests/test_delta_functions.py',
    'core/tests/test_arit.py', 'core/tests/test_power.py', 'core/tests/test_expand.py',
    'core/tests/test_evalf.py', 'core/tests/test_numbers.py', 'core/tests/test_function.py',
    'core/tests/test_diff.py', 'core/tests/test_subs.py', 'core/tests/test_complex.py',
    'core/tests/test_eval.py', 'core/tests/test_exprtools.py',
    'ntheory/tests/test_primetest.py', 'ntheory/tests/test_generate.py', 'ntheory/tests/test_factor_.py',
]


class Untr(Exception):
    """no translation (reason = capability for the summary)"""


class NA(Exception):
    """SymPy-internal API / not a math test"""


class Timeout(Exception):
    pass


def _alarm(sig, frm):
    raise Timeout()


def timed(sec, fn, *a):
    old = signal.signal(signal.SIGALRM, _alarm)
    signal.setitimer(signal.ITIMER_REAL, sec)
    try:
        return fn(*a)
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, old)


# ------------------------------------------------------------ namespace ---
SAFE_BUILTINS = {k: __builtins__[k] if isinstance(__builtins__, dict) else getattr(__builtins__, k)
                 for k in ('range', 'len', 'list', 'tuple', 'dict', 'int', 'abs', 'sum', 'min', 'max',
                           'set', 'zip', 'pow', 'float', 'complex', 'True', 'False', 'None', 'map',
                           'sorted', 'reversed', 'enumerate', 'frozenset', 'bool', 'str', 'any', 'all')}
BASE_NS = {k: getattr(sp, k) for k in dir(sp) if not k.startswith('_')}
import sympy.abc  # noqa: E402
for _mod in ('sympy.simplify.fu', 'sympy.integrals.manualintegrate', 'sympy.integrals.risch',
             'sympy.integrals.rationaltools', 'sympy.integrals.trigonometry', 'sympy.series.gruntz',
             'sympy.series.limitseq', 'sympy.concrete.gosper', 'sympy.simplify.sqrtdenest',
             'sympy.simplify.radsimp', 'sympy.polys.polyfuncs', 'sympy.solvers.polysys',
             'sympy.integrals.laplace', 'sympy.series.residues', 'sympy.simplify.combsimp',
             'sympy.simplify.gammasimp', 'sympy.ntheory', 'sympy.solvers.solveset',
             'sympy.integrals.heurisch', 'sympy.integrals.integrals'):
    try:
        __import__(_mod)
        m = sys.modules[_mod]
        for k in dir(m):
            if not k.startswith('_') and k not in BASE_NS:
                BASE_NS[k] = getattr(m, k)
    except Exception:
        pass
BASE_NS['__builtins__'] = SAFE_BUILTINS


def seval(node, ns, sec=5):
    code = compile(ast.Expression(body=node), '<expr>', 'eval')
    return timed(sec, eval, code, ns)


# --------------------------------------------------------------- printer ---
FUNC_MAP = {
    'sin': 'sin', 'cos': 'cos', 'tan': 'tan', 'cot': 'cot', 'sec': 'sec', 'csc': 'csc',
    'asin': 'asin', 'acos': 'acos', 'atan': 'atan', 'acot': 'acot', 'asec': 'asec', 'acsc': 'acsc',
    'sinh': 'sinh', 'cosh': 'cosh', 'tanh': 'tanh', 'asinh': 'asinh', 'acosh': 'acosh', 'atanh': 'atanh',
    'exp': 'exp', 'Abs': 'abs', 'sign': 'sign', 'floor': 'floor', 'ceiling': 'ceil', 're': 're',
    'im': 'im', 'conjugate': 'conj', 'arg': 'arg', 'atan2': 'atan2', 'gamma': 'gamma',
    'factorial': 'factorial', 'binomial': 'binomial', 'erf': 'erf', 'erfc': 'erfc', 'erfi': 'erfi',
    'Si': 'Si', 'Ci': 'Ci', 'Ei': 'Ei', 'zeta': 'zeta', 'DiracDelta': 'dirac', 'Max': 'max',
    'Min': 'min', 'Mod': 'mod', 'frac': 'frac',
}
RECIP = {'coth': 'tanh', 'sech': 'cosh', 'csch': 'sinh'}
INV_RECIP = {'acoth': 'atanh', 'asech': 'acosh', 'acsch': 'asinh'}
# names run_wester.to_sympy maps to functions/constants: symbols with these names cannot round-trip
BAD_SYMNAMES = {'gamma', 'beta', 'zeta', 'sign', 'binomial', 'factorial', 'diff', 'max', 'min', 'abs',
                'ln', 'log', 'exp', 'sqrt', 're', 'im', 'arg', 'erf', 'Si', 'Ci', 'Ei', 'when', 'round',
                'floor', 'frac', 'conj', 'piecewise', 'heaviside', 'dirac', 'sum', 'limit', 'series',
                'integrate', 'solve', 'factor', 'expand', 'simplify', 'normal', 'product', 'oo', 'E', 'I',
                'N', 'S', 'O', 'Q', 'C', 'D'}


def rename(name):
    if name in ('e', 'i', 'E', 'I'):
        return 'sym_' + name
    if name in RESERVED_OUR:
        return name + '_'
    return name


class OurPrinter(StrPrinter):
    printmethod = '_our_cas_str'      # do not let objects' own _sympystr bypass us
    def _print_Symbol(self, s):
        n = s.name
        if not re.match(r'^[A-Za-z][A-Za-z0-9_]*$', n):
            raise Untr('symbol name %r' % n)
        n2 = rename(n)
        if n2 in BAD_SYMNAMES:
            raise Untr('symbol name %r' % n)
        return n2

    _print_Dummy = _print_Symbol

    def _print_Exp1(self, e):
        return 'e'

    def _print_ImaginaryUnit(self, e):
        return 'i'

    def _print_Infinity(self, e):
        return 'inf'

    def _print_NegativeInfinity(self, e):
        return '-inf'

    def _print_NaN(self, e):
        return 'undef'

    def _print_ComplexInfinity(self, e):
        raise Untr('zoo')

    def _print_EulerGamma(self, e):
        return 'euler_gamma'

    def _print_GoldenRatio(self, e):
        return '((1+sqrt(5))/2)'

    def _print_Pi(self, e):
        return 'pi'

    def _print_BooleanTrue(self, e):
        return 'true'

    def _print_BooleanFalse(self, e):
        return 'false'

    def _print_Float(self, e):
        return sp.Float(e, 17).__str__() if e._prec > 60 else repr(float(e))

    def _print_exp(self, e):
        return 'exp(%s)' % self._print(e.args[0])

    def _print_log(self, e):
        if len(e.args) == 2:
            return '(ln(%s)/ln(%s))' % (self._print(e.args[0]), self._print(e.args[1]))
        return 'ln(%s)' % self._print(e.args[0])

    def _print_Heaviside(self, e):
        if len(e.args) > 1 and e.args[1] != sp.S.Half:
            raise Untr('Heaviside with H0')
        return 'heaviside(%s)' % self._print(e.args[0])

    def _print_And(self, e):
        raise Untr('boolean expression')

    _print_Or = _print_And
    _print_Not = _print_And
    _print_Xor = _print_And

    def _print_Function(self, e):
        n = e.func.__name__
        if n == 'DiracDelta' and len(e.args) > 1:
            raise Untr('DiracDelta derivative')
        if n == 'zeta' and len(e.args) > 1:
            raise Untr('Hurwitz zeta')
        args = ','.join(self._print(a) for a in e.args)
        if n in FUNC_MAP:
            return '%s(%s)' % (FUNC_MAP[n], args)
        if n in RECIP:
            return '(1/%s(%s))' % (RECIP[n], args)
        if n in INV_RECIP:
            return '%s(1/(%s))' % (INV_RECIP[n], args)
        if isinstance(e, sp.core.function.AppliedUndef):
            raise Untr('undefined function')
        raise Untr(n)

    def _print_Piecewise(self, e):
        parts = []
        for v, c in e.args:
            if c is sp.true:
                parts.append(self._print(v))
            else:
                parts.append(self._print_cond(c))
                parts.append(self._print(v))
        return 'piecewise(%s)' % ','.join(parts)

    def _print_cond(self, c):
        if isinstance(c, (sp.StrictLessThan, sp.LessThan, sp.StrictGreaterThan, sp.GreaterThan)):
            return '%s%s%s' % (self._print(c.lhs), c.rel_op, self._print(c.rhs))
        if isinstance(c, sp.And):
            return '(' + ') and ('.join(self._print_cond(a) for a in c.args) + ')'
        if isinstance(c, sp.Or):
            return '(' + ') or ('.join(self._print_cond(a) for a in c.args) + ')'
        raise Untr('piecewise condition %s' % type(c).__name__)

    def _print_Equality(self, e):
        return '%s=%s' % (self._print(e.lhs), self._print(e.rhs))

    def _print_Relational(self, e):
        if e.rel_op in ('<', '<=', '>', '>=', '!='):
            return '%s%s%s' % (self._print(e.lhs), e.rel_op, self._print(e.rhs))
        raise Untr('relational')

    def _print_MatrixBase(self, m):
        if m.rows == 0 or m.cols == 0:
            raise Untr('empty matrix')
        return '[' + ','.join('[' + ','.join(self._print(x) for x in m.row(i)) + ']'
                              for i in range(m.rows)) + ']'

    _print_ImmutableDenseMatrix = _print_MatrixBase
    _print_MutableDenseMatrix = _print_MatrixBase
    _print_ImmutableSparseMatrix = _print_MatrixBase
    _print_MutableSparseMatrix = _print_MatrixBase

    def _print_list(self, L):
        return '[' + ','.join(self._print(x) for x in L) + ']'

    _print_tuple = _print_list
    _print_Tuple = _print_list

    def emptyPrinter(self, e):
        raise Untr(type(e).__name__)

    def _print_Basic(self, e):
        raise Untr(type(e).__name__)

    def _print_Indexed(self, e):
        raise Untr('Indexed')

    _print_IndexedBase = _print_Indexed
    _print_Idx = _print_Indexed

    def _print_MatrixSymbol(self, e):
        raise Untr('MatrixSymbol')

    _print_MatrixElement = _print_MatrixSymbol
    _print_MatAdd = _print_MatrixSymbol
    _print_MatMul = _print_MatrixSymbol
    _print_MatPow = _print_MatrixSymbol
    _print_Identity = _print_MatrixSymbol
    _print_ZeroMatrix = _print_MatrixSymbol

    def _print_Integral(self, e):
        raise Untr('Integral object in argument')

    def _print_Derivative(self, e):
        raise Untr('Derivative object in argument')

    def _print_Sum(self, e):
        raise Untr('Sum object in argument')

    def _print_Product(self, e):
        raise Untr('Product object in argument')

    def _print_Limit(self, e):
        raise Untr('Limit object in argument')

    def _print_Order(self, e):
        raise Untr('Order term')

    def _print_Poly(self, e):
        raise NA('Poly object')

    _print_PurePoly = _print_Poly

    def _print_AccumulationBounds(self, e):
        raise Untr('AccumBounds')

    def _print_Catalan(self, e):
        raise Untr('Catalan')

    def _print_Lambda(self, e):
        raise Untr('Lambda')

    def _print_Interval(self, e):
        raise Untr('Interval')

    def _print_CRootOf(self, e):
        raise Untr('CRootOf')

    def _print_Pow(self, e, rational=False):
        b, x = e.as_base_exp()
        if x == sp.S.Half:
            return 'sqrt(%s)' % self._print(b)
        if x == -sp.S.Half:
            return '1/sqrt(%s)' % self._print(b)
        if x == -1:
            return '1/%s' % self.parenthesize(b, 50)
        bs = self.parenthesize(b, 61)
        if isinstance(b, sp.Rational) and not b.is_Integer or (b.is_Number and b < 0):
            bs = '(%s)' % self._print(b)
        xs = self._print(x)
        if not (x.is_Integer and x >= 0) and not (x.is_Symbol):
            xs = '(%s)' % xs
        return '%s^%s' % (bs, xs)

    def _print_Rational(self, e):
        if e.q == 1:
            return str(e.p)
        return '%d/%d' % (e.p, e.q)


PR = OurPrinter({'order': 'none'})


def our(e):
    if isinstance(e, bool):
        return 'true' if e else 'false'
    if isinstance(e, (int,)):
        return str(e)
    if isinstance(e, float):
        return repr(e)
    if isinstance(e, (list, tuple)):
        return '[' + ','.join(our(x) for x in e) + ']'
    if isinstance(e, dict):
        raise Untr('dict argument')
    if isinstance(e, str):
        raise NA('string argument')
    if not isinstance(e, (sp.Basic, sp.MatrixBase)):
        raise NA('argument of type %s' % type(e).__name__)
    s = PR.doprint(e)
    return s.replace('**', '^')


# ----------------------------------------------------------- expected ---
def plain_map(objs):
    """symbols -> renamed plain symbols; assumption info for assume lines"""
    syms = set()
    for o in objs:
        _collect(o, syms)
    return syms


def _collect(o, syms):
    if isinstance(o, (sp.Basic, sp.MatrixBase)):
        syms |= {s for s in o.free_symbols if isinstance(s, sp.Symbol)}
    elif isinstance(o, (list, tuple, set, frozenset)):
        for x in o:
            _collect(x, syms)
    elif isinstance(o, dict):
        for k, v in o.items():
            _collect(k, syms)
            _collect(v, syms)


def plainify(o, rep):
    if isinstance(o, sp.MatrixBase):
        return [[plainify(x, rep) for x in o.row(i)] for i in range(o.rows)]
    if isinstance(o, sp.Basic):
        return o.xreplace(rep)
    if isinstance(o, (list, tuple)):
        return [plainify(x, rep) for x in o]
    return o


UNEVAL_TYPES = (sp.Integral, sp.Sum, sp.Product, sp.Limit, sp.Derivative, sp.Order,
                sp.AccumBounds, sp.RootOf, sp.Subs)


def has_uneval(o):
    if isinstance(o, sp.Basic):
        return any(isinstance(n, UNEVAL_TYPES) or isinstance(n, sp.core.function.AppliedUndef)
                   for n in sp.preorder_traversal(o))
    if isinstance(o, sp.MatrixBase):
        return any(has_uneval(x) for x in o)
    if isinstance(o, (list, tuple)):
        return any(has_uneval(x) for x in o)
    return False


def assume_lines(syms):
    lines, uncertain, domain = [], [], {}
    for s in sorted(syms, key=lambda q: q.name):
        a = s.assumptions0
        n = rename(s.name)
        if not a or a.get('commutative') is False:
            if a.get('commutative') is False:
                raise NA('noncommutative symbol')
            continue
        if a.get('integer'):
            lines.append('assume(%s,integer)' % n)
            domain[n] = 'int'
        if a.get('positive'):
            lines.append('assume(%s>0)' % n)
        elif a.get('negative'):
            lines.append('assume(%s<0)' % n)
            domain[n] = 'neg'
            if a.get('integer'):
                domain[n] = (-9.0, -0.5)
        elif a.get('nonnegative'):
            lines.append('assume(%s>=0)' % n)
        elif a.get('nonpositive') or a.get('extended_negative') or a.get('extended_nonpositive'):
            uncertain.append('%s nonpositive' % n)
            domain[n] = 'neg'
        known = {'integer', 'positive', 'negative', 'nonnegative', 'real', 'commutative', 'rational',
                 'extended_real', 'finite', 'infinite', 'complex', 'hermitian', 'imaginary', 'zero',
                 'nonzero', 'extended_positive', 'extended_nonnegative', 'extended_nonzero',
                 'algebraic', 'transcendental', 'irrational', 'noninteger', 'nonpositive',
                 'extended_negative', 'extended_nonpositive', 'antihermitian', 'composite', 'prime',
                 'even', 'odd'}
        if a.get('even') or a.get('odd') or a.get('prime') or a.get('composite'):
            raise NA('parity/prime symbol (not expressible)')
        if a.get('complex') and not a.get('real') and not a.get('extended_real') is True:
            if a.get('real') is not True and 'real' not in a:
                pass
        if a.get('imaginary') or a.get('noninteger') or a.get('irrational'):
            raise NA('imaginary/noninteger symbol (not expressible)')
        if a.get('zero'):
            raise NA('zero symbol')
        _ = known
    return lines, uncertain, domain


# ------------------------------------------------------------ commands ---
SIMPLE_CMDS = {
    'simplify': 'simplify', 'trigsimp': 'trigsimp', 'ratsimp': 'ratsimp', 'factor': 'factor',
    'expand': 'expand', 'cancel': 'normal', 'together': 'together', 'powsimp': 'simplify',
    'powdenest': 'simplify', 'radsimp': 'simplify', 'combsimp': 'simplify', 'gammasimp': 'simplify',
    'sqrtdenest': 'simplify', 'fu': 'trigsimp', 'expand_log': 'lnexpand', 'expand_trig': 'texpand',
    'logcombine': 'lncollect', 'nsimplify': None, 'exptrigsimp': 'simplify', 'sqf': 'sqrfree',
    'horner': None, 'expand_mul': 'expand', 'expand_power_base': 'powexpand',
    'factor_terms': None, 'collect': None,
}
TWO_ARG = {'gcd': 'gcd', 'lcm': 'lcm', 'quo': 'quo', 'rem': 'rem', 'div': 'divide', 'resultant': 'resultant',
           'prem': None, 'pquo': None}
NT = {'isprime': 'isprime', 'nextprime': 'nextprime', 'prevprime': 'prevprime', 'totient': 'euler',
      'divisors': 'divisors', 'igcd': 'gcd', 'ilcm': 'lcm', 'primerange': None, 'factorint': None,
      'mod_inverse': 'invmod', 'isqrt': None, 'integer_nthroot': None}
INTEG_ALIASES = {'integrate', 'manualintegrate', 'risch_integrate', 'ratint', 'trigintegrate',
                 'heurisch', 'heurisch_wrapper'}
LIMIT_ALIASES = {'limit', 'gruntz', 'limit_seq'}


class Case(Exception):
    pass


def split_call(node):
    """-> (kind, name, receiver_node, args, kwargs) for Call nodes"""
    if not isinstance(node, ast.Call):
        return None
    f = node.func
    if isinstance(f, ast.Name):
        return ('func', f.id, None, node.args, node.keywords)
    if isinstance(f, ast.Attribute):
        return ('meth', f.attr, f.value, node.args, node.keywords)
    return None


def kw(kws, ns):
    return {k.arg: seval(k.value, ns) for k in kws if k.arg}


def as_syms(v):
    if isinstance(v, sp.Symbol):
        return v
    raise Untr('non-symbol variable')


COMMAND_NAMES = {'limit', 'integrate', 'diff', 'solve', 'solveset', 'summation', 'product', 'series',
                 'simplify', 'trigsimp', 'ratsimp', 'factor', 'expand', 'cancel', 'together', 'apart', 'gcd',
                 'lcm', 'roots', 'nsimplify', 'powsimp', 'radsimp', 'gruntz', 'manualintegrate',
                 'risch_integrate', 'ratint', 'heurisch', 'Integral', 'Sum', 'Product', 'Limit', 'Derivative',
                 'N', 'nsolve', 'resultant', 'discriminant', 'sqrtdenest', 'combsimp', 'gammasimp', 'fu',
                 'expand_log', 'expand_trig', 'logcombine', 'collect', 'residue', 'laplace_transform',
                 'inverse_laplace_transform', 'nseries', 'limit_seq'}
COMMAND_METHODS = {'doit', 'limit', 'integrate', 'diff', 'series', 'subs', 'expand', 'simplify', 'evalf', 'n',
                   'factor', 'rewrite', 'nseries', 'cancel', 'together', 'trigsimp', 'det', 'inv', 'eigenvals',
                   'removeO', 'as_leading_term', 'apart', 'replace', 'xreplace', 'match', 'coeff'}


def nested_command(nodes):
    for nd in nodes:
        for sub in ast.walk(nd):
            if isinstance(sub, ast.Call):
                f = sub.func
                if isinstance(f, ast.Name) and f.id in COMMAND_NAMES:
                    return f.id
                if isinstance(f, ast.Attribute) and f.attr in COMMAND_METHODS:
                    return f.attr
    return None


def tr_call(node, ns):
    nc = nested_command(list(node.args) + [k.value for k in node.keywords] +
                        ([node.func.value] if isinstance(node.func, ast.Attribute) else []))
    if nc and not (isinstance(node.func, ast.Attribute) and node.func.attr == 'doit' and nc in
                   ('Sum', 'Product', 'Integral', 'Derivative', 'Limit') and
                   nested_command(list(node.func.value.args)) is None):
        raise NA('SymPy command %s evaluated inside the arguments' % nc)
    """-> dict(line=..., check=..., integrand=..., post=callable(expected)->expected,
               objs=[evaluated sympy args])"""
    sc = split_call(node)
    if sc is None:
        raise NA('lhs not a call')
    kind, name, recv, args, kws = sc
    if any(isinstance(a, ast.Starred) for a in args) or any(k.arg is None for k in kws):
        raise NA('starred args')
    if kind == 'meth':
        robj = seval(recv, ns)
        if name == 'doit' and not args:
            if isinstance(robj, sp.Sum):
                return tr_sum('sum', robj.function, robj.limits)
            if isinstance(robj, sp.Product):
                return tr_sum('product', robj.function, robj.limits)
            if isinstance(robj, sp.Integral):
                return tr_integral(robj.function, list(robj.limits))
            if isinstance(robj, sp.Derivative):
                return tr_diff(robj.expr, [v for v, c in robj.variable_count for _ in range(int(c))])
            if isinstance(robj, sp.Limit):
                e, z, z0, d = robj.args
                return tr_limit(e, z, z0, str(d))
            raise NA('doit on %s' % type(robj).__name__)
        a = [seval(x, ns) for x in args]
        k = kw(kws, ns)
        if isinstance(robj, sp.MatrixBase):
            if k:
                raise NA('matrix method kwargs')
            mm = {'det': 'det', 'inv': 'inv', 'rank': 'rank', 'transpose': 'transpose', 'trace': 'trace',
                  'eigenvals': 'eigenvals', 'adjugate': 'adjoint', 'inverse': 'inv'}
            if name in mm and not a:
                d = {'line': '%s(%s)' % (mm[name], our(robj)), 'objs': [robj]}
                if name == 'eigenvals':
                    d['eig'] = True
                return d
            raise NA('Matrix.%s' % name)
        if not isinstance(robj, sp.Basic):
            raise NA('method on %s' % type(robj).__name__)
        if name == 'diff':
            return tr_diff(robj, a, k)
        if name == 'integrate':
            return tr_integral(robj, a, k)
        if name in ('series',):
            return tr_series(robj, a, k)
        if name == 'limit':
            return tr_limit(robj, *a, **k)
        if name == 'subs':
            if k:
                raise NA('subs kwargs')
            pairs = [tuple(a)] if len(a) == 2 else list(a[0].items()) if len(a) == 1 and isinstance(a[0], dict) else []
            for p_, q_ in pairs:
                if not isinstance(p_, sp.Symbol):
                    raise NA('subs of a non-symbol (SymPy pattern semantics)')
                if not isinstance(q_, sp.Expr):
                    raise NA('subs by non-expression')
                if any(q_.has(p2) for p2, _ in pairs):
                    raise NA('sequential subs')
            if not isinstance(robj, sp.Expr):
                raise NA('subs on non-expression')
            if len(a) == 2:
                return {'line': 'subst(%s,%s,%s)' % (our(robj), our(a[0]), our(a[1])), 'objs': [robj] + a}
            if len(a) == 1 and isinstance(a[0], dict):
                eqs = ','.join('%s=%s' % (our(p), our(q)) for p, q in a[0].items())
                return {'line': 'subst(%s,[%s])' % (our(robj), eqs), 'objs': [robj] + list(a[0].items())}
            raise NA('subs form')
        if name in ('evalf', 'n'):
            return tr_evalf(robj, a, k)
        if name == 'expand' and not a and k in ({'trig': True}, {'log': True}, {'log': True, 'force': True}):
            cmd = 'texpand' if 'trig' in k else 'lnexpand'
            return {'line': '%s(%s)' % (cmd, our(robj)), 'objs': [robj]}
        if name in ('expand', 'factor', 'simplify', 'cancel', 'together', 'trigsimp', 'ratsimp') and not a and not k:
            return {'line': '%s(%s)' % (SIMPLE_CMDS[name], our(robj)), 'objs': [robj]}
        if name == 'apart' and len(a) <= 1 and not k:
            return {'line': 'partfrac(%s)' % ','.join(our(x) for x in [robj] + a), 'objs': [robj] + a}
        raise NA('method .%s' % name)
    # plain function call
    if name not in ns:
        raise NA('unknown name %s' % name)
    k = kw(kws, ns)
    if name in ('Sum', 'Product', 'Integral', 'Derivative', 'Limit'):
        raise NA('unevaluated object constructor')
    a = [seval(x, ns) for x in args]
    if name in INTEG_ALIASES:
        if name != 'integrate' and len(a) != 2:
            raise NA(name + ' arguments')
        k.pop('risch', None) if name == 'integrate' and k.get('risch') is None else None
        if name in ('heurisch', 'heurisch_wrapper', 'manualintegrate', 'risch_integrate', 'ratint',
                    'trigintegrate'):
            k = {}
        return tr_integral(a[0], a[1:], k)
    if name == 'diff':
        return tr_diff(a[0], a[1:], k)
    if name in LIMIT_ALIASES:
        if name == 'limit_seq':
            if len(a) != 2 or k:
                raise NA('limit_seq args')
            return tr_limit(a[0], a[1], sp.oo, '+')
        return tr_limit(*a, **k)
    if name == 'series':
        return tr_series(a[0], a[1:], k)
    if name == 'residue':
        if len(a) != 3 or k:
            raise NA('residue args')
        return {'line': 'residue(%s,%s,%s)' % (our(a[0]), our(a[1]), our(a[2])), 'objs': a}
    if name in ('solve', 'solveset', 'solve_poly_system', 'roots'):
        return tr_solve(name, a, k)
    if name in ('summation', 'product', 'gosper_sum'):
        if k:
            raise NA('kwargs')
        if name == 'product' and len(a) >= 2 and not isinstance(a[1], (tuple, sp.Tuple)):
            raise NA('product of terms')
        return tr_sum('product' if name == 'product' else 'sum', a[0], a[1:])
    if name in SIMPLE_CMDS:
        if SIMPLE_CMDS[name] is None:
            raise Untr(name)
        extra = dict(k)
        if name in ('expand_log', 'logcombine') and extra.pop('force', False):
            pass
        if extra:
            raise Untr('%s(%s=...)' % (name, ','.join(sorted(extra))))
        if not a or isinstance(a[0], (tuple, list, sp.Tuple)):
            raise NA('%s of a tuple' % name)
        gens = a[1:]
        if gens and name not in ('factor', 'horner', 'sqf', 'cancel', 'together', 'simplify'):
            raise NA('%s extra args' % name)
        if name == 'factor' and gens:
            gens = []
        return {'line': '%s(%s)' % (SIMPLE_CMDS[name], our(a[0])), 'objs': a[:1]}
    if name == 'apart':
        if k:
            raise Untr('apart(%s=...)' % ','.join(sorted(k)))
        return {'line': 'partfrac(%s)' % ','.join(our(x) for x in a), 'objs': a}
    if name in TWO_ARG:
        if TWO_ARG[name] is None:
            raise Untr(name)
        if k or len(a) < 2:
            raise NA('%s args' % name)
        if isinstance(a[0], (list, tuple)):
            raise NA('list gcd')
        rest = [r for r in a[2:]]
        if len(rest) == 1 and isinstance(rest[0], (list, tuple)) and len(rest[0]) == 1:
            rest = [rest[0][0]]
        d0 = {}
        if name in ('gcd', 'lcm') and any(isinstance(q, sp.Float) or (isinstance(q, sp.Rational) and not q.is_Integer)
                                          for t in a[:2] if isinstance(t, sp.Basic) for q in t.atoms(sp.Number)):
            d0['uncertain'] = 'gcd/lcm content convention (rational or float coefficients)'
        if name == 'resultant':
            if len(rest) != 1:
                raise NA('resultant without var')
            return {'line': 'resultant(%s,%s,%s)' % (our(a[0]), our(a[1]), our(rest[0])), 'objs': a}
        d0.update({'line': '%s(%s,%s)' % (TWO_ARG[name], our(a[0]), our(a[1])), 'objs': a[:2],
                   'tuple_out': name == 'div'})
        return d0
    if name in ('discriminant', 'degree', 'horner'):
        if len(a) == 2 and isinstance(a[1], (list, tuple)):
            if len(a[1]) != 1:
                raise NA('%s with several generators' % name)
            a = [a[0], a[1][0]]
        if k or len(a) not in (1, 2):
            raise NA('%s args' % name)
        if len(a) == 1:
            fs = a[0].free_symbols if isinstance(a[0], sp.Basic) else set()
            if len(fs) != 1:
                raise NA('%s without generator' % name)
            a = a + [list(fs)[0]]
        return {'line': '%s(%s)' % (name, ','.join(our(x) for x in a)), 'objs': a}
    if name in ('det', 'Determinant'):
        return {'line': 'det(%s)' % our(a[0]), 'objs': a}
    if name in NT:
        if NT[name] is None:
            raise Untr(name)
        if k or (name in ('nextprime', 'prevprime') and len(a) != 1):
            raise NA('kwargs / extra arguments')
        if name == 'divisors' and a and a[0] == 0:
            raise NA('divisors(0) convention')
        return {'line': '%s(%s)' % (NT[name], ','.join(our(x) for x in a)), 'objs': a}
    if name in ('N',):
        return tr_evalf(a[0], a[1:], k)
    if name in ('laplace_transform', 'inverse_laplace_transform'):
        if k.pop('noconds', None) is None and False:
            pass
        if k:
            raise NA('laplace kwargs')
        nm = 'laplace' if name == 'laplace_transform' else 'ilaplace'
        return {'line': '%s(%s)' % (nm, ','.join(our(x) for x in a[:3])), 'objs': a, 'laplace': True}
    # elementary / special function evaluation: f(args) == value
    fobj = ns[name]
    if name == 'transpose' and not (a and isinstance(a[0], sp.MatrixBase)):
        raise NA('transpose of a scalar')
    if name in ('Max', 'Min', 'transpose') and not k:
        nm = {'Max': 'max', 'Min': 'min', 'transpose': 'transpose'}[name]
        return {'line': '%s(%s)' % (nm, ','.join(our(x) for x in a)), 'objs': a}
    if isinstance(fobj, type) and issubclass(fobj, sp.Function) or name in ('sqrt', 'root', 'exp', 'log'):
        if k:
            raise NA('function kwargs')
        try:
            timed(5, fobj, *a)
        except Timeout:
            raise
        except Exception:
            raise NA('call %s with these arguments fails in SymPy' % name)
        if name == 'DiracDelta' and len(a) > 1:
            raise Untr('DiracDelta derivative')
        if name == 'zeta' and len(a) > 1:
            raise Untr('Hurwitz zeta')
        if name in INV_RECIP and a[0] == 0:
            raise NA('inverse reciprocal function at 0')
        if name == 'transpose' and not isinstance(a[0], sp.MatrixBase):
            raise NA('transpose of a scalar')
        if name == 'acot':
            return {'line': 'acot(%s)' % our(a[0]), 'objs': a, 'uncertain': 'acot branch convention (ours: range (0,pi))'}
        if name == 'sqrt':
            return {'line': 'sqrt(%s)' % our(a[0]), 'objs': a}
        if name == 'log':
            if len(a) == 2:
                return {'line': 'ln(%s)/ln(%s)' % (our(a[0]), our(a[1])), 'objs': a}
            return {'line': 'ln(%s)' % our(a[0]), 'objs': a}
        if name == 'root':
            raise Untr('root')
        if name == 'Heaviside':
            if len(a) > 1:
                raise Untr('Heaviside with H0')
            return {'line': 'heaviside(%s)' % our(a[0]), 'objs': a}
        if name in FUNC_MAP:
            return {'line': '%s(%s)' % (FUNC_MAP[name], ','.join(our(x) for x in a)), 'objs': a}
        if name in RECIP:
            return {'line': '1/%s(%s)' % (RECIP[name], our(a[0])), 'objs': a}
        if name in INV_RECIP:
            return {'line': '%s(1/(%s))' % (INV_RECIP[name], our(a[0])), 'objs': a}
        raise Untr(name)
    raise NA('call %s' % name)


def tr_integral(f, lims, k=None):
    if k:
        k = {q: v for q, v in k.items() if q not in ('conds', 'meijerg', 'risch', 'manual', 'heurisch')}
        if k:
            raise NA('integrate kwargs')
    if isinstance(f, (tuple, list, sp.Tuple)):
        raise NA('integrand given as a tuple')
    if not lims and isinstance(f, sp.Basic) and len(f.free_symbols) == 1:
        lims = [list(f.free_symbols)[0]]
    if len(lims) != 1:
        raise Untr('multiple integral' if lims else 'integrate without variable')
    L = lims[0]
    if isinstance(L, (tuple, sp.Tuple)) and len(L) == 1:
        L = L[0]
    if isinstance(L, sp.Symbol):
        d = {'line': 'integrate(%s,%s)' % (our(f), our(L)), 'objs': [f, L]}
        if isinstance(f, sp.Expr):
            d['antideriv'] = (f, L)
        return d
    if isinstance(L, (tuple, sp.Tuple)) and len(L) == 3:
        return {'line': 'integrate(%s,%s,%s,%s)' % (our(f), our(L[0]), our(L[1]), our(L[2])),
                'objs': [f] + list(L)}
    raise NA('integration limits')


def tr_diff(f, vs, k=None):
    if k:
        raise NA('diff kwargs')
    if not vs:
        raise NA('diff without variable')
    out = []
    for v in vs:
        if isinstance(v, (tuple, sp.Tuple)) and len(v) == 2:
            out += [our(v[0])] * int(v[1])
        elif isinstance(v, (int, sp.Integer)):
            if not out:
                raise NA('diff count first')
            last = out.pop()
            out += [last] * int(v)
        elif isinstance(v, sp.Symbol):
            out.append(our(v))
        else:
            raise Untr('derivative w.r.t. non-symbol')
    if not out:
        raise NA('zeroth derivative')
    if len(set(out)) == 1 and len(out) > 1:
        line = 'diff(%s,%s,%d)' % (our(f), out[0], len(out))
    else:
        line = 'diff(%s,%s)' % (our(f), ','.join(out))
    return {'line': line, 'objs': [f] + vs}


def tr_limit(e, z, z0, dir='+'):
    dir = str(dir)
    if z0 in (sp.oo, -sp.oo) or dir == '+-':
        return {'line': 'limit(%s,%s,%s)' % (our(e), our(z), our(z0)), 'objs': [e, z, z0]}
    d = {'+': '1', '-': '-1'}.get(dir)
    if d is None:
        raise NA('limit dir')
    return {'line': 'limit(%s,%s,%s,%s)' % (our(e), our(z), our(z0), d), 'objs': [e, z, z0]}


def tr_series(f, a, k):
    a = list(a)
    names = ['x', 'x0', 'n', 'dir']
    p = dict(zip(names, a))
    p.update(k)
    if set(p) - set(names):
        raise NA('series kwargs')
    x = p.get('x')
    if x is None:
        fs = f.free_symbols
        if len(fs) != 1:
            raise NA('series without x')
        x = list(fs)[0]
    if not isinstance(x, sp.Symbol):
        raise NA('series in a non-symbol')
    x0 = p.get('x0', 0)
    n = p.get('n', 6)
    if p.get('dir', '+') != '+':
        raise Untr('series from the left')
    if n is None or not isinstance(n, (int, sp.Integer)):
        raise NA('series n')
    if not (isinstance(x0, sp.Basic) and x0.is_finite) and x0 != 0:
        raise Untr('series at infinity')
    return {'line': 'series(%s,%s,%s,%d)' % (our(f), our(x), our(x0), int(n)), 'objs': [f, x, x0],
            'series': True}


def tr_sum(nm, f, lims):
    if len(lims) != 1:
        raise Untr('multiple %s' % nm)
    L = lims[0]
    if not (isinstance(L, (tuple, sp.Tuple)) and len(L) == 3):
        raise NA('%s limits' % nm)
    try:
        if bool(sp.sympify(L[1]) > sp.sympify(L[2]) + 1):
            raise NA('reversed limits (Karr convention)')
    except TypeError:
        pass
    return {'line': '%s(%s,%s,%s,%s)' % (nm, our(f), our(L[0]), our(L[1]), our(L[2])), 'objs': [f] + list(L)}


def tr_solve(name, a, k):
    if name == 'roots':
        if k or not a:
            raise NA('roots kwargs')
        p = a[0]
        if len(a) > 1:
            x = a[1]
        else:
            fs = p.free_symbols if isinstance(p, sp.Basic) else set()
            if len(fs) != 1:
                raise NA('roots gens')
            x = list(fs)[0]
        return {'line': 'SOLVE(%s=0,%s)' % (our(p), our(x)), 'objs': [p, x], 'solve': 'roots'}
    if name == 'solveset':
        dom = k.pop('domain', None)
        if len(a) >= 3:
            dom = a[2]
        if k or len(a) < 2 or not isinstance(a[1], sp.Symbol):
            raise NA('solveset args')
        if dom not in (None, sp.S.Reals, sp.S.Complexes):
            raise NA('solveset domain')
        eq = a[0]
        line = 'SOLVE(%s,%s)' % (our(eq if isinstance(eq, sp.Equality) else sp.Eq(eq, 0)), our(a[1]))
        return {'line': line, 'objs': a[:2], 'solve': 'solveset',
                'real': dom == sp.S.Reals}
    if name == 'solve_poly_system':
        eqs, vs = a[0], a[1:]
        if len(vs) == 1 and isinstance(vs[0], (list, tuple)):
            vs = list(vs[0])
        if not vs or not all(isinstance(v_, sp.Symbol) for v_ in vs):
            raise NA('solve for non-symbols')
        return {'line': 'SOLVE(%s,%s)' % (our([sp.Eq(q, 0) for q in eqs]), our(list(vs))),
                'objs': [eqs] + list(vs), 'solve': 'system', 'vars': list(vs)}
    # solve
    if k:
        raise Untr('solve(%s=...)' % ','.join(sorted(k)))
    eq = a[0]
    vs = a[1:]
    if not vs:
        fs = set()
        for q in (eq if isinstance(eq, (list, tuple, set)) else [eq]):
            if isinstance(q, sp.Basic):
                fs |= q.free_symbols
        if len(fs) != 1:
            raise NA('solve without variables')
        vs = [list(fs)[0]]
    if len(vs) == 1 and isinstance(vs[0], (list, tuple)):
        vs = list(vs[0])
    if not vs or not all(isinstance(v_, sp.Symbol) for v_ in vs):
        raise NA('solve for non-symbols')
    if isinstance(eq, (list, tuple, set)):
        eqs = [q if isinstance(q, sp.Equality) else sp.Eq(q, 0) for q in eq]
        if len(vs) == 1 and len(eqs) == 1:
            eq = eqs[0]
        else:
            if any(q in (sp.true, sp.false) for q in eqs):
                raise NA('equation evaluated to a boolean')
            return {'line': 'SOLVE(%s,%s)' % (our(eqs), our(list(vs))), 'objs': [eqs] + list(vs),
                    'solve': 'system', 'vars': list(vs)}
    if len(vs) != 1:
        raise Untr('solve for several variables')
    if not isinstance(eq, sp.Basic) or isinstance(eq, sp.Rel) and not isinstance(eq, sp.Equality):
        raise Untr('solve inequality')
    eqs = eq if isinstance(eq, sp.Equality) else sp.Eq(eq, 0)
    if eqs is sp.true or eqs is sp.false:
        raise NA('trivial equation')
    if eqs.has(sp.oo, -sp.oo, sp.zoo):
        raise NA('infinity in the equation')
    return {'line': 'SOLVE(%s,%s)' % (our(eqs), our(vs[0])), 'objs': [eqs, vs[0]], 'solve': 'solve'}


def tr_evalf(e, a, k):
    if k:
        raise NA('evalf kwargs')
    n = a[0] if a else 15
    if not isinstance(n, (int, sp.Integer)):
        raise NA('evalf n')
    n = int(n)
    if e.free_symbols:
        raise NA('evalf with symbols')
    if n <= 15:
        return {'line': 'approx(%s)' % our(e), 'objs': [e], 'value': 1e-10}
    if n > 200:
        raise NA('huge precision')
    return {'line': 'approx(%s,%d)' % (our(e), n), 'objs': [e], 'value': 10.0 ** (-n + 3)}


def is_real_value(v):
    try:
        if isinstance(v, sp.Basic):
            r = v.is_extended_real
            if r is None:
                c = complex(sp.N(v.subs({s: sp.Rational(7, 10) for s in v.free_symbols})))
                return abs(c.imag) < 1e-12
            return r
    except Exception:
        return False
    return True


def finish(d, expected, rep):
    """build check/expect from the translation dict and the evaluated expected value"""
    if d.get('antideriv'):
        f, x = d['antideriv']
        f2 = plainify(f, rep)
        return {'check': 'antideriv', 'expect': S.ser(f2), 'var': rename(x.name)}
    exp_ = expected
    if d.get('series'):
        if isinstance(exp_, sp.Basic):
            exp_ = exp_.removeO()
    if d.get('laplace') and isinstance(exp_, (tuple, sp.Tuple)):
        exp_ = exp_[0]
    if d.get('tuple_out') and isinstance(exp_, (tuple, sp.Tuple)):
        exp_ = list(exp_)
    if d.get('eig'):
        if not isinstance(exp_, dict):
            raise NA('eigenvals not a dict')
        return {'check': 'set', 'expect': S.ser([plainify(v, rep) for v in exp_]), 'complexsol': True}
    if 'solve' in d:
        kind = d['solve']
        if kind == 'system':
            if isinstance(exp_, dict):
                exp_ = [exp_]
            if isinstance(exp_, list) and exp_ and isinstance(exp_[0], tuple):
                exp_ = [dict(zip(d['vars'], t)) for t in exp_]
            if not isinstance(exp_, list) or not all(isinstance(q, dict) for q in exp_):
                raise NA('system solution format')
            sols = []
            for q in exp_:
                if set(q) != set(d['vars']):
                    raise Untr('parametric system solution')
                sols.append([sp.Eq(plainify(v, rep), plainify(q[v], rep), evaluate=False)
                             for v in d['vars']])
            ser = [[{'t': 'eq', 'l': S.ser(e.lhs), 'r': S.ser(e.rhs)} for e in s] for s in sols]
            return {'check': 'set', 'expect': {'t': 'list', 'v': [{'t': 'list', 'v': s} for s in ser]},
                    'complexsol': any(not is_real_value(e.rhs) for s in sols for e in s)}
        if kind == 'roots':
            if not isinstance(exp_, dict):
                raise NA('roots not a dict')
            vals = list(exp_)
        elif kind == 'solveset':
            if isinstance(exp_, sp.FiniteSet):
                vals = list(exp_)
            elif exp_ is sp.S.EmptySet:
                vals = []
            else:
                raise Untr('solveset result %s' % type(exp_).__name__)
        else:
            if isinstance(exp_, (list, tuple)):
                if exp_ and isinstance(exp_[0], dict):
                    raise NA('solve dict output')
                vals = list(exp_)
            elif isinstance(exp_, (set, frozenset)):
                vals = list(exp_)
            else:
                raise NA('solve result not a list')
        vals = [plainify(v, rep) for v in vals]
        for v in vals:
            if not isinstance(v, sp.Basic) or has_uneval(v):
                raise NA('solution format')
        cplx = any(not is_real_value(v) for v in vals)
        if kind == 'solveset' and not d.get('real'):
            cplx = True
        return {'check': 'set', 'expect': S.ser(vals), 'complexsol': cplx}
    if isinstance(exp_, dict):
        raise NA('dict result')
    if isinstance(exp_, (bool, sp.logic.boolalg.BooleanAtom)):
        return {'check': 'bool', 'expect': S.ser(bool(exp_))}
    if exp_ is sp.nan:
        return {'check': 'undef', 'expect': None}
    if exp_ is sp.zoo:
        return {'check': 'undef', 'expect': None, 'uncertain': 'SymPy zoo (complex infinity)'}
    if has_uneval(exp_):
        return {'check': 'noref', 'expect': None}
    if isinstance(exp_, (sp.Set, sp.Lambda, str)) or not isinstance(exp_, (sp.Basic, list, tuple, int, float,
                                                                          sp.MatrixBase)):
        raise NA('expected of type %s' % type(exp_).__name__)
    if isinstance(exp_, sp.Basic) and not isinstance(exp_, sp.Expr) and not isinstance(exp_, sp.Tuple):
        raise NA('expected of type %s' % type(exp_).__name__)
    e2 = plainify(exp_, rep)
    out = {'check': 'value' if d.get('value') else 'equiv', 'expect': S.ser(e2)}
    if d.get('value'):
        out['tol'] = d['value']
    return out


# ------------------------------------------------------------- walking ---
def is_assign_ok(stmt):
    return isinstance(stmt, ast.Assign) and len(stmt.targets) == 1 and \
        isinstance(stmt.targets[0], (ast.Name, ast.Tuple))


def do_assign(stmt, ns):
    tgt = stmt.targets[0]
    names = [tgt.id] if isinstance(tgt, ast.Name) else \
        [t.id if isinstance(t, ast.Name) else None for t in tgt.elts]
    try:
        v = seval(stmt.value, ns, sec=3)
        if isinstance(tgt, ast.Name):
            ns[tgt.id] = v
        else:
            vals = list(v)
            if len(vals) != len(names):
                raise ValueError
            for nm, val in zip(names, vals):
                if nm:
                    ns[nm] = val
    except BaseException as ex:
        if isinstance(ex, KeyboardInterrupt):
            raise
        for nm in names:
            if nm:
                ns.pop(nm, None)


def do_import(stmt, ns):
    if isinstance(stmt, ast.ImportFrom) and stmt.module == 'sympy.abc':
        for al in stmt.names:
            ns[al.asname or al.name] = getattr(sympy.abc, al.name, sp.Symbol(al.name))


def decorators(fn):
    out = set()
    for d in fn.decorator_list:
        if isinstance(d, ast.Name):
            out.add(d.id)
        elif isinstance(d, ast.Call) and isinstance(d.func, ast.Name):
            out.add(d.func.id)
    return out


def iter_stmts(body):
    for st in body:
        if isinstance(st, ast.With):
            yield from iter_stmts(st.body)
        else:
            yield st


def extract_file(rel, root, cases, stats):
    path = os.path.join(root, rel)
    text = open(path).read()
    tree = ast.parse(text)
    group = os.path.basename(rel)[:-3]
    mns = dict(BASE_NS)
    for st in tree.body:
        if isinstance(st, ast.ImportFrom):
            do_import(st, mns)
        elif is_assign_ok(st):
            do_assign(st, mns)
    for fn in tree.body:
        if not isinstance(fn, ast.FunctionDef) or not fn.name.startswith('test'):
            continue
        decs = decorators(fn)
        if 'SKIP' in decs or 'skip' in decs:
            continue
        ns = dict(mns)
        n = 0
        for st in iter_stmts(fn.body):
            if isinstance(st, ast.ImportFrom):
                do_import(st, ns)
                continue
            if is_assign_ok(st):
                do_assign(st, ns)
                continue
            if not isinstance(st, ast.Assert):
                continue
            t = st.test
            if not (isinstance(t, ast.Compare) and len(t.ops) == 1 and isinstance(t.ops[0], ast.Eq)):
                continue
            lhs, rhs = t.left, t.comparators[0]
            if not isinstance(lhs, ast.Call) and isinstance(rhs, ast.Call):
                # assert 0 == f(...) form is rare; only swap when lhs is a constant
                if isinstance(lhs, (ast.Constant, ast.Name)):
                    lhs, rhs = rhs, lhs
            if not isinstance(lhs, ast.Call):
                continue
            n += 1
            cid = 'sympy/%s/%s/%d' % (group, fn.name, n)
            src = ast.get_source_segment(text, st) or ''
            c = {'id': cid, 'group': group, 'src': src[:1000], 'lines': [''], 'check': 'equiv'}
            if 'XFAIL' in decs:
                c['xfail'] = True
            try:
                c.update(make_case(lhs, rhs, ns, decs))
            except Untr as ex:
                c['unsupported'] = str(ex)[:120]
            except NA as ex:
                c['na'] = str(ex)[:120]
            except Timeout:
                c['na'] = 'sympy evaluation timeout'
            except RecursionError:
                c['na'] = 'recursion'
            except Exception as ex:
                c['na'] = 'sympy evaluation: %s' % type(ex).__name__
            c.update(OVERRIDES.get(cid, {}))
            cases.append(c)


OUR_FUNCS = set(FUNC_MAP) | set(RECIP) | set(INV_RECIP) | {'exp', 'log', 'Piecewise', 'Heaviside', 'sqrt'}
TRIG = (sp.sin, sp.cos, sp.tan, sp.cot, sp.sec, sp.csc)
HYP = (sp.exp, sp.sinh, sp.cosh, sp.tanh, sp.coth, sp.sech, sp.csch)


def _basics(o):
    if isinstance(o, (sp.Basic, sp.MatrixBase)):
        yield o
    elif isinstance(o, (list, tuple, set, frozenset)):
        for x in o:
            yield from _basics(x)
    elif isinstance(o, dict):
        for k_, v_ in o.items():
            yield from _basics(k_)
            yield from _basics(v_)


def any_has(objs, *types):
    return any(b.has(*types) for b in _basics(objs) if isinstance(b, sp.Basic))


def needs_funcs(o):
    out = set()
    for b in _basics(o):
        if not isinstance(b, sp.Basic):
            continue
        for n in sp.preorder_traversal(b):
            if isinstance(n, sp.Function) and not isinstance(n, sp.core.function.AppliedUndef):
                nm = n.func.__name__
                if nm not in OUR_FUNCS:
                    out.add(nm)
    return out


def make_case(lhs, rhs, ns, decs):
    d = tr_call(lhs, ns)
    expected = None
    if not d.get('antideriv'):
        expected = seval(rhs, ns, sec=8)
        if d.get('value') and isinstance(expected, sp.Basic) and expected.has(sp.oo, -sp.oo, sp.zoo):
            raise NA('infinite numeric value')
        nf = needs_funcs(expected)
        if nf:
            raise Untr('answer needs %s' % ','.join(sorted(nf)))
    unc0 = []
    if d.get('uncertain'):
        unc0.append(d['uncertain'])
    if any_has(d['objs'], sp.acot) or (expected is not None and any_has([expected], sp.acot)):
        unc0.append('acot branch convention (ours: range (0,pi))')
    if d['line'].startswith('limit(') and (any_has(d['objs'], sp.I) or (
            isinstance(expected, sp.Basic) and expected.has(sp.I))):
        unc0.append('complex-valued limit (branch cuts)')
    if 'solve' in d:
        vs = d.get('vars') or [o for o in d['objs'][1:2] if isinstance(o, sp.Symbol)]
        eqo = d['objs'][0]
        if any(b.has(*vs) and isinstance(b, TRIG) for e0 in _basics([eqo]) if isinstance(e0, sp.Basic)
               for b in sp.preorder_traversal(e0)):
            unc0.append('periodic equation: solution representatives may differ')
        if any_has([eqo], sp.I):
            d['force_complex'] = True
    syms = plain_map(d['objs'] + ([expected] if expected is not None else []))
    rep = {s: sp.Symbol(rename(s.name)) for s in syms}
    for s in syms:
        if s.name in BAD_SYMNAMES or rename(s.name) in BAD_SYMNAMES:
            raise Untr('symbol name %r' % s.name)
    alines, unc, domain = assume_lines(syms)
    res = finish(d, expected, rep)
    line = d['line']
    if line.startswith('SOLVE('):
        cplx = res.get('complexsol') or d.get('force_complex')
        line = ('csolve(' if cplx else 'solve(') + line[6:]
        if cplx and any(isinstance(b, HYP) and b.has(*[o for o in d['objs'] if isinstance(o, sp.Symbol)])
                        for e0 in _basics(d['objs'][:1]) if isinstance(e0, sp.Basic)
                        for b in sp.preorder_traversal(e0)):
            unc0.append('periodic in the complex plane: solution representatives may differ')
    res.pop('complexsol', None)
    out = {'lines': alines + [line], 'check': res['check'], 'expect': res.get('expect'),
           'src_expect': ast.unparse(rhs)[:500] if not d.get('antideriv') else '(derivative check)'}
    for k in ('var', 'tol'):
        if k in res:
            out[k] = res[k]
    if domain:
        out['domain'] = domain
    u = list(unc) + unc0
    if any(dv == 'neg' or isinstance(dv, tuple) for dv in domain.values()) and \
            any(s_.is_integer for s_ in syms):
        u.append('negative integer symbol')
    if res.get('uncertain'):
        u.append(res['uncertain'])
    if u:
        out['uncertain'] = '; '.join(u)
    if len(line) > 6000:
        raise NA('input too long')
    return out


def main():
    S.self_memguard()           # abort above 2 GB RSS (lib/suitelib.py)
    out = sys.argv[1]
    root = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(sp.__file__)
    sys.setrecursionlimit(5000)
    cases, stats = [], {}
    for rel in FILES:
        if not os.path.exists(os.path.join(root, rel)):
            print('missing', rel, file=sys.stderr)
            continue
        before = len(cases)
        extract_file(rel, root, cases, stats)
        print('%-50s %d' % (rel, len(cases) - before), file=sys.stderr, flush=True)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    S.save_cases(out, cases)
    na = sum(1 for c in cases if c.get('na'))
    un = sum(1 for c in cases if c.get('unsupported'))
    print('sympy %s: %d asserts, %d runnable, %d na, %d unsupported' % (
        sp.__version__, len(cases), len(cases) - na - un, na, un))


if __name__ == '__main__':
    main()
