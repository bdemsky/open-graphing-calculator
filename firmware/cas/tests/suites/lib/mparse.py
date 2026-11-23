"""
Infix math parser shared by the suite translators (Maxima, Rubi-in-Maxima-
syntax, Giac).  Parses a dialect into a small AST and emits
  * our CAS syntax  (to_our)     -- raises Untranslatable for unknown names
  * a SymPy object  (to_sympy)   -- raises NoRef when there is no SymPy meaning

AST nodes are tuples:
  ('num', text) ('sym', name) ('str', s) ('list', [items])
  ('call', name, [args])          f(a, b)
  ('op', op, [a, b])              binary + - * / ^ = # < <= > >= and or : :=
  ('neg', a) ('not', a) ('fact', a) ('fact2', a) ('quote', a)
  ('idx', base, [args])           a[1]
  ('if', c, a, b)
"""
import re

import sympy as sp


class ParseError(Exception):
    pass


class Untranslatable(Exception):
    pass


class NoRef(Exception):
    pass


TOKEN = re.compile(r"""
   (?P<ws>\s+)
 | (?P<num>(?:\d+\.?\d*|\.\d+)(?:[eEbBdD][+-]?\d+)?)
 | (?P<str>"(?:[^"\\]|\\.)*")
 | (?P<id>[%A-Za-z_?][A-Za-z_0-9%]*)
 | (?P<op>::=|:=|::|\*\*|\^\^|<=|>=|!=|==|'\(|''|!!|[-+*/^=#<>!:,;$()\[\]{}.'|@&~])
""", re.X)


def tokenize(s):
    pos, out = 0, []
    while pos < len(s):
        m = TOKEN.match(s, pos)
        if not m:
            raise ParseError("bad character %r" % s[pos])
        pos = m.end()
        k = m.lastgroup
        if k == 'ws':
            continue
        out.append((k, m.group(k)))
    out.append(('end', None))
    return out


# binding powers (left, right) for binary operators
BIN_MAXIMA = {
    ':': (20, 19), ':=': (20, 19), '::': (20, 19), '::=': (20, 19),
    'or': (30, 31), 'and': (40, 41),
    '=': (60, 61), '#': (60, 61), '<': (60, 61), '<=': (60, 61), '>': (60, 61), '>=': (60, 61),
    '!=': (60, 61), '==': (60, 61),
    '+': (100, 101), '-': (100, 101),
    '*': (120, 121), '/': (120, 121), '.': (125, 126),
    '^': (141, 140), '**': (141, 140), '^^': (141, 140),
}
PREFIX_NEG = 130      # -x^2 = -(x^2); -a*b = (-a)*b, both fine
PREFIX_NOT = 50


class Parser:
    def __init__(self, text, dialect='maxima'):
        self.toks = tokenize(text)
        self.i = 0
        self.dialect = dialect
        self.bins = dict(BIN_MAXIMA)
        if dialect == 'giac':
            self.bins.pop('#', None)
            self.bins['=='] = (60, 61)
            self.bins['!='] = (60, 61)

    def peek(self):
        return self.toks[self.i]

    def next(self):
        t = self.toks[self.i]
        self.i += 1
        return t

    def expect(self, val):
        t = self.next()
        if t[1] != val:
            raise ParseError("expected %r, got %r" % (val, t[1]))

    def at_end(self):
        return self.peek()[0] == 'end'

    def parse_all(self):
        e = self.expr(0)
        if not self.at_end():
            raise ParseError("trailing input at %r" % (self.peek()[1],))
        return e

    def binop(self, t):
        k, v = t
        if k == 'op' and v in self.bins:
            return v
        if k == 'id' and v in ('and', 'or') and v in self.bins:
            return v
        return None

    def expr(self, rbp):
        left = self.prefix()
        while True:
            t = self.peek()
            if t[0] == 'op' and t[1] == '!':
                self.next()
                left = ('fact', left)
                continue
            if t[0] == 'op' and t[1] == '!!':
                self.next()
                left = ('fact2', left)
                continue
            if t[0] == 'op' and t[1] == '[' and left[0] in ('sym', 'idx', 'call'):
                self.next()
                args = self.arglist(']')
                left = ('idx', left, args)
                continue
            op = self.binop(t)
            if op is None:
                break
            lbp, rbp2 = self.bins[op]
            if lbp <= rbp:
                break
            self.next()
            right = self.expr(rbp2)
            if op == '**':
                op = '^'
            left = ('op', op, [left, right])
        return left

    def arglist(self, close):
        args = []
        if self.peek()[1] == close:
            self.next()
            return args
        while True:
            args.append(self.expr(0))
            t = self.next()
            if t[1] == close:
                return args
            if t[1] != ',':
                raise ParseError("expected , or %s, got %r" % (close, t[1]))

    def prefix(self):
        t = self.next()
        k, v = t
        if k == 'num':
            return ('num', v)
        if k == 'str':
            return ('str', v[1:-1])
        if k == 'id':
            if v == 'not':
                return ('not', self.expr(PREFIX_NOT))
            if v == 'if':
                c = self.expr(0)
                t2 = self.next()
                if t2[1] != 'then':
                    raise ParseError('if without then')
                a = self.expr(0)
                b = ('sym', 'false')
                if self.peek()[1] == 'elseif':
                    raise ParseError('elseif')
                if self.peek()[1] == 'else':
                    self.next()
                    b = self.expr(0)
                return ('if', c, a, b)
            if v in ('then', 'else', 'do', 'for', 'while', 'thru', 'step', 'unless', 'elseif'):
                raise ParseError('keyword ' + v)
            if self.peek()[1] == '(':
                self.next()
                return ('call', v, self.arglist(')'))
            return ('sym', v)
        if k == 'op':
            if v == '(':
                e = self.expr(0)
                if self.peek()[1] == ',':          # (a, b, c) block-like sequence
                    items = [e]
                    while self.peek()[1] == ',':
                        self.next()
                        items.append(self.expr(0))
                    self.expect(')')
                    return ('call', '__seq', items)
                self.expect(')')
                return ('paren', e)
            if v == '[':
                return ('list', self.arglist(']'))
            if v == '{':
                return ('call', 'set', self.arglist('}'))
            if v == '-':
                return ('neg', self.expr(PREFIX_NEG))
            if v == '+':
                return self.expr(PREFIX_NEG)
            if v == "'":
                return ('quote', self.prefix_atom())
            if v == "'(":
                e = self.expr(0)
                self.expect(')')
                return ('quote', e)
            if v == "''":
                return ('qq', self.prefix_atom())
        raise ParseError("unexpected %r" % (v,))

    def prefix_atom(self):
        t = self.next()
        if t[0] == 'id':
            if self.peek()[1] == '(':
                self.next()
                return ('call', t[1], self.arglist(')'))
            return ('sym', t[1])
        if t[1] == '(':
            e = self.expr(0)
            self.expect(')')
            return e
        raise ParseError("bad quote")


def parse(text, dialect='maxima'):
    return strip_parens(Parser(text, dialect).parse_all())


def strip_parens(a):
    if not isinstance(a, tuple):
        return a
    if a[0] == 'paren':
        return strip_parens(a[1])
    out = [a[0]]
    for x in a[1:]:
        if isinstance(x, tuple):
            out.append(strip_parens(x))
        elif isinstance(x, list):
            out.append([strip_parens(y) for y in x])
        else:
            out.append(x)
    return tuple(out)


def walk(a):
    """yield every node"""
    yield a
    for x in a[1:]:
        if isinstance(x, tuple):
            yield from walk(x)
        elif isinstance(x, list):
            for y in x:
                if isinstance(y, tuple):
                    yield from walk(y)


def names_used(a):
    syms, funcs = set(), set()
    for n in walk(a):
        if n[0] == 'sym':
            syms.add(n[1])
        elif n[0] == 'call':
            funcs.add(n[1])
    return syms, funcs


# ------------------------------------------------------------ SymPy side ---
def _sp_log(*a):
    return sp.log(*a)


def _hstep(u):
    return sp.Heaviside(u, sp.Rational(1, 2))


SP_FUNCS = {
    'sqrt': sp.sqrt, 'exp': sp.exp, 'log': sp.log, 'ln': sp.log,
    'sin': sp.sin, 'cos': sp.cos, 'tan': sp.tan, 'cot': sp.cot, 'sec': sp.sec, 'csc': sp.csc,
    'asin': sp.asin, 'acos': sp.acos, 'atan': sp.atan, 'acot': sp.acot, 'asec': sp.asec, 'acsc': sp.acsc,
    'sinh': sp.sinh, 'cosh': sp.cosh, 'tanh': sp.tanh, 'coth': sp.coth, 'sech': sp.sech, 'csch': sp.csch,
    'asinh': sp.asinh, 'acosh': sp.acosh, 'atanh': sp.atanh, 'acoth': sp.acoth, 'asech': sp.asech,
    'acsch': sp.acsch,
    'abs': sp.Abs, 'signum': sp.sign, 'sign': sp.sign, 'floor': sp.floor, 'ceiling': sp.ceiling,
    'ceil': sp.ceiling,
    'realpart': sp.re, 'imagpart': sp.im, 're': sp.re, 'im': sp.im, 'conjugate': sp.conjugate,
    'conj': sp.conjugate, 'carg': sp.arg, 'arg': sp.arg, 'atan2': sp.atan2,
    'gamma': sp.gamma, 'factorial': sp.factorial, 'binomial': sp.binomial, 'beta': sp.beta,
    'erf': sp.erf, 'erfc': sp.erfc, 'erfi': sp.erfi, 'zeta': sp.zeta,
    'expintegral_si': sp.Si, 'expintegral_ci': sp.Ci, 'expintegral_ei': sp.Ei, 'Si': sp.Si,
    'Ci': sp.Ci, 'expintegral_shi': sp.Shi, 'expintegral_chi': sp.Chi,
    'max': sp.Max, 'min': sp.Min, 'mod': sp.Mod, 'psi': sp.polygamma,
    'lambert_w': sp.LambertW, 'li': sp.polylog, 'polylog': sp.polylog,
    'fresnel_s': lambda z: sp.fresnels(z), 'fresnel_c': lambda z: sp.fresnelc(z),
    'bessel_j': sp.besselj, 'bessel_y': sp.bessely, 'bessel_i': sp.besseli, 'bessel_k': sp.besselk,
    'hstep': _hstep, 'unit_step': lambda u: sp.Heaviside(u, 0), 'delta': sp.DiracDelta,
    'kron_delta': sp.KroneckerDelta, 'double_factorial': sp.factorial2, 'genfact': None,
    'pochhammer': sp.rf, 'poch': sp.rf,
    'round': lambda u: sp.sign(u) * sp.floor(sp.Abs(u) + sp.Rational(1, 2)),
    'truncate': lambda u: sp.sign(u) * sp.floor(sp.Abs(u)),
    'gamma_incomplete': sp.uppergamma, 'elliptic_f': sp.elliptic_f, 'elliptic_e': sp.elliptic_e,
    'elliptic_kc': sp.elliptic_k, 'elliptic_ec': sp.elliptic_e, 'elliptic_pi': sp.elliptic_pi,
    'dilog': lambda u: sp.polylog(2, u),
    'GAMMA': lambda *a: sp.gamma(a[0]) if len(a) == 1 else sp.uppergamma(a[0], a[1]),
    'hypergeometric': lambda p_, q_, z_: sp.hyper(list(p_), list(q_), z_),
    'ProductLog': sp.LambertW, 'FresnelS': sp.fresnels, 'FresnelC': sp.fresnelc, 'Shi': sp.Shi, 'Chi': sp.Chi,
    'Ei': lambda *a: sp.Ei(a[0]) if len(a) == 1 else sp.expint(a[0], a[1]), 'Li': sp.li,
    'expintegral_e': sp.expint, 'expintegral_li': sp.li, 'expintegral_e1': lambda u: sp.expint(1, u),
    'csign': None,
}
SP_CONST = {
    '%pi': sp.pi, '%e': sp.E, '%i': sp.I, '%gamma': sp.EulerGamma, '%phi': sp.GoldenRatio,
    'inf': sp.oo, 'minf': -sp.oo, 'infinity': sp.zoo, 'true': sp.true, 'false': sp.false,
    '%catalan': sp.Catalan,
}
GIAC_CONST = {'pi': sp.pi, 'e': sp.E, 'i': sp.I, 'inf': sp.oo, 'infinity': sp.zoo,
              'euler_gamma': sp.EulerGamma, 'true': sp.true, 'false': sp.false}


class SympyConv:
    def __init__(self, dialect='maxima', funcs=None, consts=None):
        self.dialect = dialect
        self.funcs = dict(SP_FUNCS)
        if funcs:
            self.funcs.update(funcs)
        self.consts = dict(GIAC_CONST if dialect == 'giac' else SP_CONST)
        if consts:
            self.consts.update(consts)
        self.symcache = {}

    def sym(self, n):
        if n in self.consts:
            return self.consts[n]
        if n.startswith('%') or n in ('und', 'ind', 'zeroa', 'zerob', 'undef'):
            raise NoRef('constant ' + n)
        if n in ('e', 'i', 'E', 'I'):
            n = 'sym_' + n
        elif n in RESERVED_OUR:
            n = n + '_'
        if n not in self.symcache:
            self.symcache[n] = sp.Symbol(n)
        return self.symcache[n]

    def conv(self, a):
        k = a[0]
        if k == 'num':
            t = a[1]
            if re.search(r'[.eEbBdD]', t):
                return sp.Float(re.sub(r'[bBdD]', 'e', t), 30)
            return sp.Integer(t)
        if k == 'sym':
            return self.sym(a[1])
        if k == 'str':
            return a[1]
        if k == 'list':
            if a[1] and all(x[0] == 'list' for x in a[1]) and False:
                pass
            return [self.conv(x) for x in a[1]]
        if k == 'neg':
            v = self.conv(a[1])
            if isinstance(v, list):
                raise NoRef('negated list')
            return -v
        if k == 'not':
            return sp.Not(self.conv(a[1]))
        if k == 'fact':
            return sp.factorial(self.conv(a[1]))
        if k == 'fact2':
            return sp.factorial2(self.conv(a[1]))
        if k == 'op':
            op = a[1]
            x, y = (self.conv(a[2][0]), self.conv(a[2][1]))
            if op == '=':
                return ('Eq', x, y)
            if isinstance(x, (list, tuple, str)) or isinstance(y, (list, tuple, str)):
                raise NoRef('operator on list')
            if op == '+':
                return x + y
            if op == '-':
                return x - y
            if op == '*':
                return x * y
            if op == '/':
                return x / y
            if op == '^':
                return x ** y
            if op == '#' or op == '!=':
                return sp.Ne(x, y)
            if op == '<':
                return sp.Lt(x, y)
            if op == '<=':
                return sp.Le(x, y)
            if op == '>':
                return sp.Gt(x, y)
            if op == '>=':
                return sp.Ge(x, y)
            if op == 'and':
                return sp.And(x, y)
            if op == 'or':
                return sp.Or(x, y)
            raise NoRef('operator ' + op)
        if k == 'call':
            name, args = a[1], a[2]
            if name == 'matrix':
                return ('Matrix', [self.conv(r) for r in args])
            if name in ('log', 'ln') and len(args) == 1:
                return sp.log(self.conv(args[0]))
            if self.dialect == 'giac' and name == 'log' and len(args) == 1:
                return sp.log(self.conv(args[0]))
            f = self.funcs.get(name)
            if f is None:
                raise NoRef('function ' + name)
            cargs = [self.conv(x) for x in args]
            if name == 'hypergeometric' and len(cargs) == 3 and all(isinstance(c, list) for c in cargs[:2]):
                return sp.hyper(cargs[0], cargs[1], cargs[2])
            if any(isinstance(c, (list, tuple, str)) for c in cargs):
                raise NoRef('list argument to ' + name)
            if name == 'psi':
                # maxima psi[n](x) is idx; psi(x) is digamma
                return sp.digamma(*cargs)
            try:
                return f(*cargs)
            except TypeError as ex:
                raise NoRef('arity %s: %s' % (name, ex))
        if k == 'idx':
            b = a[1]
            if b[0] == 'sym' and b[1] == 'psi' and len(a[2]) == 1:
                return ('psi', self.conv(a[2][0]))
            raise NoRef('subscript')
        if k == 'quote':
            raise NoRef('noun form')
        raise NoRef('node ' + k)

    def full(self, a):
        """convert, resolving psi[n](x) and the Eq/Matrix markers"""
        if a[0] == 'call' and False:
            pass
        v = self.conv(a)
        return v


# ---------------------------------------------------------------- emitter ---
PREC = {'or': 30, 'and': 40, '=': 60, '!=': 60, '<': 60, '<=': 60, '>': 60, '>=': 60, '==': 60,
        '+': 100, '-': 100, '*': 120, '/': 120, '^': 140}


class OurEmitter:
    """AST -> our CAS syntax.  funcs maps dialect name -> our name, or a
    callable(emitter, args) -> string for special handling."""

    def __init__(self, funcs, consts, known_syms=None):
        self.funcs = funcs
        self.consts = consts
        self.notes = []          # translation remarks (uncertain constructs)

    def sym(self, n):
        if n in self.consts:
            v = self.consts[n]
            if v is None:
                raise Untranslatable('constant ' + n)
            return v
        if n.startswith('%') or n.startswith('?'):
            raise Untranslatable('constant ' + n)
        # our reserved single letters: e, i are constants in our CAS
        if n in ('e', 'i', 'E', 'I'):
            return 'sym_' + n
        if n in RESERVED_OUR:
            return n + '_'
        return n

    def prec(self, a):
        if a[0] == 'op':
            return PREC.get(self.opname(a[1]), 10)
        if a[0] == 'neg':
            return 110
        if a[0] == 'not':
            return 50
        if a[0] == 'num' and a[1].startswith('-'):
            return 110
        return 200

    @staticmethod
    def opname(op):
        return {'#': '!=', '**': '^'}.get(op, op)

    def wrap(self, a, minprec):
        s = self.emit(a)
        if self.prec(a) < minprec:
            return '(' + s + ')'
        return s

    def num(self, t):
        if re.search(r'[bBdD]', t):
            t = re.sub(r'[bBdD]', 'e', t)
        if t.endswith('.'):
            t = t + '0'
        if t.startswith('.'):
            t = '0' + t
        return t

    def emit(self, a):
        k = a[0]
        if k == 'num':
            return self.num(a[1])
        if k == 'sym':
            return self.sym(a[1])
        if k == 'str':
            return '"' + a[1] + '"'
        if k == 'list':
            return '[' + ','.join(self.emit(x) for x in a[1]) + ']'
        if k == 'neg':
            return '-' + self.wrap(a[1], 120)
        if k == 'not':
            return 'not(' + self.emit(a[1]) + ')'
        if k == 'fact':
            return 'factorial(' + self.emit(a[1]) + ')'
        if k == 'fact2':
            raise Untranslatable('double factorial')
        if k == 'op':
            op = self.opname(a[1])
            l, r = a[2]
            if op in (':', ':=', '::', '::='):
                raise Untranslatable('assignment inside expression')
            if op == '.':
                raise Untranslatable('noncommutative product')
            if op == '^^':
                raise Untranslatable('noncommutative power')
            p = PREC[op]
            if op == '^':
                ls = self.wrap(l, 141)
                rs = self.wrap(r, 140)
                if rs.startswith('-'):
                    rs = '(' + rs + ')'
                return ls + '^' + rs
            if op in ('and', 'or'):
                return '(' + self.wrap(l, p) + ') ' + op + ' (' + self.wrap(r, p + 1) + ')'
            ls = self.wrap(l, p)
            rs = self.wrap(r, p + 1)
            if op in ('+', '-', '*', '/') and rs.startswith('-'):
                rs = '(' + rs + ')'
            return ls + op + rs
        if k == 'call':
            name, args = a[1], a[2]
            f = self.funcs.get(name)
            if f is None:
                raise Untranslatable('function ' + name)
            if callable(f):
                return f(self, args)
            return f + '(' + ','.join(self.emit(x) for x in args) + ')'
        if k == 'idx':
            raise Untranslatable('subscript')
        if k == 'quote':
            raise Untranslatable('noun form')
        if k == 'qq':
            raise Untranslatable("'' evaluation")
        if k == 'if':
            raise Untranslatable('if')
        raise Untranslatable('node ' + k)


# names that are builtins of our CAS and would change meaning as plain symbols
RESERVED_OUR = {'pi', 'inf', 'undef', 'ans', 'true', 'false', 'euler_gamma', 'oo',
                # our command/function names used as plain variables in test files
                'exp', 'series', 'sum', 'product', 'limit', 'diff', 'integrate', 'solve', 'gamma',
                'beta', 'zeta', 'det', 'rank', 'norm', 'mean', 'max', 'min', 'mod', 'factor',
                'expand', 're', 'im', 'sign', 'abs', 'floor', 'round', 'ln', 'log', 'sqrt', 'sin',
                'cos', 'tan', 'poly', 'seq', 'sort', 'size', 'length', 'map', 'apply', 'trace',
                'dot', 'cross', 'inv', 'lu', 'qr', 'roots', 'degree', 'coeff', 'numer', 'denom',
                'gcd', 'lcm', 'rem', 'quo', 'divide', 'mode', 'median', 'type', 'has', 'lhs',
                'rhs', 'left', 'right', 'dim', 'fill', 'cond', 'matrix', 'identity', 'zeros',
                'concat', 'reverse', 'subst', 'subs', 'normal', 'together', 'collect', 'horner',
                'content', 'perm', 'comb', 'euler', 'digits', 'piecewise', 'when', 'convert',
                'erf', 'heaviside', 'dirac', 'binomial', 'factorial', 'approx', 'exact', 'float',
                'evalf', 'simplify', 'arg', 'conj', 'frac', 'ceil', 'atan2', 'nint', 'taylor',
                'laplace', 'ilaplace', 'dsolve', 'assume', 'about', 'purge', 'restart', 'charpoly',
                'eigenvals', 'eigenvects', 'ker', 'adjoint', 'transpose', 'rref', 'grad', 'curl',
                'divergence', 'hessian', 'jacobian', 'tangent', 'nderiv', 'pade', 'fourier',
                'trn', 'idn', 'ref', 'augment', 'redim', 'pm', 'nominal', 'uncert'}
