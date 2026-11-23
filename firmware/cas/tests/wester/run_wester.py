#!/usr/bin/env python3
"""
Wester benchmark runner for the CAS (build/cas_repl, batch mode).

Problem set: tests/wester/problems.txt (see the header of that file for the
format and provenance).  Every problem runs in a fresh cas_repl process with a
per-line timeout; the output of the last input line is graded against the
expected answer with SymPy (mathematical equivalence: numeric spot checks at
random points, falling back to simplify(diff) == 0) -- never by string match,
except for the explicit 'str'/'bool' checks.

Categories
  PASS         equivalent to the expected answer (and in the requested form
               for simp/form/factor checks)
  WRONG        a definite answer that is mathematically different
  UNEVAL       returned unevaluated, partially evaluated, only numerically,
               or not simplified to the requested form; also ERROR 6
               ("valid input the engine cannot handle yet")
  UNSUPPORTED  the function/feature does not exist (echoed unknown function,
               ERROR 3, or no possible translation: 'unsupported:' key)
  ERROR        any other error status, or output SymPy cannot parse
  TIMEOUT      interrupted by the per-line timeout (or wall-clock timeout)
  CRASH        the process died (signal, sanitizer report, missing output)
  NA           not applicable to this CAS (code generation, noncommutative
               algebra, ...); excluded from the score
  UNGRADED     the grader could not decide (listed for manual review)

Usage: run_wester.py [--repl PATH] [--asan] [--only ID,ID|SECTION] [-j N]
                     [--timeout MS] [--mem MB] [--out BASE] [--verbose]
Writes tests/wester/results.json and tests/wester/results.md.
"""
import argparse
import json
import os
import random
import re
import signal
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

import sympy as sp
from sympy.parsing.sympy_parser import (parse_expr, standard_transformations,
                                        factorial_notation)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))          # firmware/cas
FIRMWARE = os.path.dirname(ROOT)
REPL = os.path.join(ROOT, "build", "cas_repl")
ASAN_REPL = os.path.join(ROOT, "build", "cas_repl_asan")
PROBLEMS = os.path.join(HERE, "problems.txt")

CATS = ["PASS", "WRONG", "UNEVAL", "UNSUPPORTED", "ERROR", "TIMEOUT", "CRASH", "UNGRADED", "NA"]

# ------------------------------------------------------------------ names ---
def builtin_names():
    """Every function/command name the CAS knows, scraped from the sources and
    help pages (used to tell an echoed unknown function from a real result)."""
    names = set()
    try:
        src = open(os.path.join(ROOT, "src", "cas_int.h")).read()
        names |= set(re.findall(r'"([A-Za-z_][A-Za-z_0-9]*)"', src))
    except OSError:
        pass
    for f in ("xcmds.c", "statdist.c", "stats.c", "units.c", "unc.c", "eval.c"):
        try:
            names |= set(re.findall(r'"([A-Za-z_][A-Za-z_0-9]*)"', open(os.path.join(ROOT, "src", f)).read()))
        except OSError:
            pass
    # the casx extension (cas/ext/*/*.c): its functions and commands are named by string literals
    for d in sorted(os.listdir(os.path.join(ROOT, "ext"))):
        dd = os.path.join(ROOT, "ext", d)
        if os.path.isdir(dd):
            for f in sorted(os.listdir(dd)):
                if f.endswith(".c"):  # also NUL-separated name lists ("a\\0b\\0")
                    for lit in re.findall(r'"((?:[A-Za-z_][A-Za-z_0-9]*(?:\\0)?)+)"', open(os.path.join(dd, f)).read()):
                        names |= set(n for n in lit.split('\\0') if n)
    hd = os.path.join(FIRMWARE, "help")
    if os.path.isdir(hd):
        for f in os.listdir(hd):
            if f.endswith(".txt"):
                names |= set(re.findall(r"^@cmd\s+(\S+)", open(os.path.join(hd, f)).read(), re.M))
    return names


BUILTINS = builtin_names()
# Commands whose appearance in an output means "not evaluated".
UNEVAL_NAMES = {"integrate", "int", "sum", "product", "limit", "diff", "solve", "csolve", "dsolve",
                "desolve", "series", "taylor", "laplace", "ilaplace", "derivative", "integral",
                "sum_", "limit_", "simplify", "factor", "expand", "eigenvals", "eigenvects", "rootof",
                "partfrac", "trigsimp", "normal", "nint", "residue"}

# ---------------------------------------------------------------- parsing ---
x, y, z, t, s, n, k, a, b, c, m, w, p, q, r, u, v = sp.symbols('x y z t s n k a b c m w p q r u v')


def _log(u_, base=None):
    return sp.log(u_, 10) if base is None else sp.log(u_, base)


def _piecewise(*args):
    args = list(args)
    pairs = []
    while len(args) >= 2:
        cnd_ = args.pop(0)
        pairs.append((args.pop(0), cnd_))  # piecewise(cond, value, ...) -> (value, cond)
    pairs.append((args[0] if args else sp.nan, True))
    return sp.Piecewise(*pairs)


def _round(u_):
    return sp.sign(u_) * sp.floor(sp.Abs(u_) + sp.Rational(1, 2))


INERT = {nm: sp.Function(nm) for nm in UNEVAL_NAMES}
LOCAL = dict(INERT)
# diff() only survives classification when the problem allows it (derivatives
# of unknown functions); then it means a SymPy Derivative.
LOCAL['diff'] = lambda e_, *a_: sp.Derivative(e_, *a_)
LOCAL.update({nm: sp.Symbol(nm) for nm in "xyztsnkabcmwpqruv"})
LOCAL.update({
    'e': sp.E, 'i': sp.I, 'pi': sp.pi, 'inf': sp.oo, 'undef': sp.nan, 'oo': sp.oo, 'I': sp.I, 'E': sp.E,
    'ln': sp.log, 'log': _log, 'log10': lambda u_: sp.log(u_, 10), 'log2': lambda u_: sp.log(u_, 2),
    'abs': sp.Abs, 'ceil': sp.ceiling, 'floor': sp.floor, 'round': _round, 'frac': sp.frac,
    'conj': sp.conjugate, 're': sp.re, 'im': sp.im, 'arg': sp.arg, 'sign': sp.sign, 'signum': sp.sign,
    'heaviside': sp.Heaviside, 'dirac': sp.DiracDelta, 'erf': sp.erf, 'gamma': sp.gamma,
    'atan2': sp.atan2, 'binomial': sp.binomial, 'factorial': sp.factorial, 'sqrt': sp.sqrt,
    'exp': sp.exp, 'sin': sp.sin, 'cos': sp.cos, 'tan': sp.tan, 'cot': sp.cot, 'sec': sp.sec,
    'csc': sp.csc, 'asin': sp.asin, 'acos': sp.acos, 'atan': sp.atan, 'acot': sp.acot,
    'asec': sp.asec, 'acsc': sp.acsc,
    'sinh': sp.sinh, 'cosh': sp.cosh, 'tanh': sp.tanh, 'asinh': sp.asinh, 'acosh': sp.acosh,
    'atanh': sp.atanh, 'true': sp.true, 'false': sp.false, 'Si': sp.Si, 'Ci': sp.Ci, 'Ei': sp.Ei,
    'erfc': sp.erfc, 'erfi': sp.erfi, 'euler_gamma': sp.EulerGamma, 'zeta': sp.zeta,
    'max': sp.Max, 'min': sp.Min, 'piecewise': _piecewise,
    'when': lambda cnd, a1, a2: sp.Piecewise((a1, cnd), (a2, True)),
    # names that only appear in expected answers (SymPy spellings)
    'LambertW': sp.LambertW, 'besselj': sp.besselj, 'bessely': sp.bessely, 'besseli': sp.besseli,
    'besselk': sp.besselk, 'lambertw': sp.LambertW, 'fresnels': sp.fresnels, 'fresnelc': sp.fresnelc,
    'Shi': sp.Shi, 'Chi': sp.Chi, 'expint': sp.expint, 'li': sp.li, 'uppergamma': sp.uppergamma,
    'hyp2f1': lambda a_, b_, c_, z_: sp.hyper([a_, b_], [c_], z_), 'polygamma': sp.polygamma, 'digamma': sp.digamma,
    'polylog': sp.polylog, 'Catalan': sp.Catalan, 'fibonacci': sp.fibonacci, 'bernoulli': sp.bernoulli,
    'chebyshevt': sp.chebyshevt, 'legendre': sp.legendre, 'assoc_legendre': sp.assoc_legendre,
    'elliptic_e': sp.elliptic_e, 'elliptic_f': sp.elliptic_f, 'elliptic_pi': sp.elliptic_pi, 'rf': sp.rf, 'factorial2': sp.factorial2,
    'Heaviside': sp.Heaviside, 'DiracDelta': sp.DiracDelta, 'EulerGamma': sp.EulerGamma,
    'GoldenRatio': sp.GoldenRatio, 'csch': sp.csch, 'sech': sp.sech, 'coth': sp.coth,
    'Abs': sp.Abs, 'Max': sp.Max, 'Min': sp.Min,
})
TRANS = standard_transformations + (factorial_notation,)


class Timeout(Exception):
    pass


def _alarm(signum, frame):
    raise Timeout()


def with_timeout(sec, fn, *args):
    """Run fn under a SIGALRM timeout (main thread only; grading is serial)."""
    old = signal.signal(signal.SIGALRM, _alarm)
    signal.setitimer(signal.ITIMER_REAL, sec)
    try:
        return fn(*args)
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, old)


def split_top(sx, sep):
    parts, depth, cur, i = [], 0, '', 0
    while i < len(sx):
        ch = sx[i]
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        if depth == 0 and sx.startswith(sep, i):
            parts.append(cur)
            cur = ''
            i += len(sep)
            continue
        cur += ch
        i += 1
    parts.append(cur)
    return parts


class Eqn:
    """lhs = rhs kept unevaluated (SymPy's Eq would evaluate to a boolean)."""
    def __init__(self, l, r_):
        self.lhs, self.rhs = l, r_

    def __repr__(self):
        return "Eqn(%s, %s)" % (self.lhs, self.rhs)


def outer_parens(text):
    if not (text.startswith('(') and text.endswith(')')):
        return False
    depth = 0
    for i_, ch in enumerate(text):
        depth += ch == '('
        depth -= ch == ')'
        if depth == 0 and i_ < len(text) - 1:
            return False
    return True


# series/taylor print their order term last (x-1/6*x^3+O(x^4)): graded as the
# truncated polynomial, like sympy's removeO()
ORDER_RE = re.compile(r'(^|\+)O\((?:[^()]|\((?:[^()]|\([^()]*\))*\))*\)$')


def strip_order(text):
    if not text.endswith(')') or 'O(' not in text:
        return text
    return ORDER_RE.sub('', text) or '0'


def to_sympy(text, evaluate=True):
    text = text.strip()
    text = strip_order(text)
    while outer_parens(text) and (' and ' in text or ' or ' in text):
        text = text[1:-1].strip()
    if text.startswith('"') and text.endswith('"'):
        return text
    if ' or ' in text and len(split_top(text, ' or ')) > 1:
        return sp.Or(*[to_sympy(p_) for p_ in split_top(text, ' or ')])
    if ' and ' in text and len(split_top(text, ' and ')) > 1:
        return sp.And(*[to_sympy(p_) for p_ in split_top(text, ' and ')])
    if text.startswith('[') and text.endswith(']') and len(split_top(text, ',')) == 1:
        inner = text[1:-1]
        if inner.strip() == '':
            return []
        return [to_sympy(p_, evaluate) for p_ in split_top(inner, ',')]
    depth = 0
    for i_, ch in enumerate(text):
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        elif ch == '=' and depth == 0 and (i_ == 0 or text[i_ - 1] not in '<>!:=') and \
                (i_ + 1 >= len(text) or text[i_ + 1] != '='):
            return Eqn(to_sympy(text[:i_], evaluate), to_sympy(text[i_ + 1:], evaluate))
    py = text.replace('^', '**')
    py = re.sub(r"(?<![<>!=])==(?!=)", "==", py)
    return parse_expr(py, local_dict=dict(LOCAL), transformations=TRANS, evaluate=evaluate)


# ------------------------------------------------------------ equivalence ---
def free_syms(*es):
    fs = set()
    for e_ in es:
        if isinstance(e_, sp.Basic):
            fs |= e_.free_symbols
    return sorted(fs, key=str)


def _sample(sym, rng, domain):
    d = domain.get(str(sym), 'pos')
    if d == 'int':
        return sp.Integer(rng.randint(1, 9))
    if d == 'neg':
        return -sp.Rational(rng.randint(200, 1700), 1000)
    if d == 'real':
        return sp.Rational(rng.randint(-1700, 1700), 1000)
    if d == 'big':
        return sp.Rational(rng.randint(2000, 9000), 1000)
    if isinstance(d, tuple):
        lo, hi = d
        return sp.Rational(rng.randint(int(lo * 1000) + 1, int(hi * 1000) - 1), 1000)
    return sp.Rational(rng.randint(200, 1700), 1000)


def numeric_equiv(e1, e2, rng, domain, npts=6, tol=1e-7):
    syms = free_syms(e1, e2)
    good = 0
    for _ in range(npts * 4):
        if good >= npts:
            break
        sub = {v_: _sample(v_, rng, domain) for v_ in syms}
        try:
            v1 = complex(sp.N(e1.subs(sub), 30)) if isinstance(e1, sp.Basic) else complex(e1)
            v2 = complex(sp.N(e2.subs(sub), 30)) if isinstance(e2, sp.Basic) else complex(e2)
        except (TypeError, ValueError, ZeroDivisionError, OverflowError, AttributeError):
            continue
        if any(q_ != q_ or abs(q_) == float('inf') for q_ in (v1.real, v1.imag, v2.real, v2.imag)):
            continue
        if abs(v1 - v2) > tol * (1 + abs(v1) + abs(v2)):
            return False
        good += 1
    return True if good >= 2 else None


def equiv(e1, e2, rng, domain, tol=1e-7):
    """True / False / None (undecided)."""
    if isinstance(e1, Eqn) or isinstance(e2, Eqn):
        if not (isinstance(e1, Eqn) and isinstance(e2, Eqn)):
            return False
        r1 = equiv(e1.lhs, e2.lhs, rng, domain, tol)
        r2 = equiv(e1.rhs, e2.rhs, rng, domain, tol)
        if r1 and r2:
            return True
        # a = b  vs  a - b = 0
        return equiv(e1.lhs - e1.rhs, e2.lhs - e2.rhs, rng, domain, tol)
    if isinstance(e1, (list, tuple)) or isinstance(e2, (list, tuple)):
        if not (isinstance(e1, (list, tuple)) and isinstance(e2, (list, tuple))) or len(e1) != len(e2):
            return False
        res = [equiv(p_, q_, rng, domain, tol) for p_, q_ in zip(e1, e2)]
        if any(r_ is False for r_ in res):
            return False
        return True if all(res) else None
    if isinstance(e1, str) or isinstance(e2, str):
        return e1 == e2
    try:
        e1 = sp.sympify(e1)
        e2 = sp.sympify(e2)
        if e1 == e2:
            return True
        boolish = (sp.logic.boolalg.BooleanFunction, sp.logic.boolalg.BooleanAtom,
                   sp.core.relational.Relational)
        if isinstance(e1, boolish) or isinstance(e2, boolish):
            return e1 == e2
        if e1.is_infinite or e2.is_infinite or e1 is sp.nan or e2 is sp.nan or \
                e1 is sp.zoo or e2 is sp.zoo:
            return e1 == e2
        r_ = numeric_equiv(e1, e2, rng, domain, tol=tol)
        if r_ is not None:
            return r_
        return with_timeout(15, lambda: sp.simplify(sp.expand(e1 - e2)) == 0) or None
    except Timeout:
        return None
    except Exception:
        return None


def has_float(e_):
    if isinstance(e_, Eqn):
        return has_float(e_.lhs) or has_float(e_.rhs)
    if isinstance(e_, (list, tuple)):
        return any(has_float(q_) for q_ in e_)
    if not isinstance(e_, sp.Basic):
        return False
    return any(isinstance(a0, sp.Float) for a0 in sp.preorder_traversal(e_))


def ops(e_):
    if isinstance(e_, (list, tuple)):
        return sum(ops(q_) for q_ in e_)
    if isinstance(e_, Eqn):
        return ops(e_.lhs) + ops(e_.rhs)
    try:
        return sp.count_ops(e_, visual=False) + len(str(e_)) / 1000.0
    except Exception:
        return len(str(e_))


def factor_count(e_):
    """non-constant factors (with multiplicity) of an unevaluated product"""
    cnt = 0
    for f_ in sp.Mul.make_args(e_):
        base, ex = f_.as_base_exp()
        if isinstance(base, sp.Mul):
            cnt += factor_count(base) * (abs(int(ex)) if ex.is_Integer else 1)
        elif base.free_symbols:
            cnt += abs(int(ex)) if ex.is_Integer else 1
    return cnt


# ----------------------------------------------------------------- checks ---
class Result:
    def __init__(self, cat, note=""):
        self.cat, self.note = cat, note


R3 = {True: 'PASS', False: 'WRONG', None: 'UNGRADED'}


def num(e_):
    return complex(sp.N(e_, 30))


def grade(prob, out, rng):
    chk = prob.get('check', 'exact').split()
    kind = chk[0]
    domain = prob['domain']
    tol = float(prob.get('tol', '1e-7'))
    if kind == 'manual':
        return Result('UNGRADED', 'manual review')
    if kind == 'str':
        want = prob['expect'].replace(' ', '')
        return Result('PASS' if out.replace(' ', '') == want else 'WRONG')
    if kind == 'bool':
        if out not in ('true', 'false'):
            return Result('UNEVAL', 'no truth value')
        return Result('PASS' if out == prob['expect'] else 'WRONG')
    try:
        ours = to_sympy(out)
    except Exception:
        if '([[' in out.replace(' ', ''):
            return Result('UNEVAL', 'function of a matrix left unevaluated')
        raise
    if kind == 'ineq':
        return grade_ineq(prob, ours, rng)
    if kind == 'ode':
        return grade_ode(prob, ours, rng, chk)
    if kind == 'odesys':
        return grade_odesys(prob, ours, rng, chk)
    if kind in ('linsys', 'nlsys'):
        return grade_system(prob, ours, rng, chk)
    if kind == 'eigvecs':
        return grade_eigvecs(prob, ours, rng, chk)
    if kind == 'satisfy':
        return grade_satisfy(prob, ours, rng)
    if kind == 'antideriv':
        var = sp.Symbol(prob.get('var', 'x'))
        f_ = to_sympy(prob['expect'])
        if isinstance(ours, (list, Eqn)):
            return Result('WRONG', 'not an expression')
        vr = sp.Symbol(str(var) + '_r', real=True)
        dF = sp.diff(ours.subs(var, vr), vr).subs(vr, var)
        r_ = equiv(dF, f_, rng, domain, tol)
        return Result(R3[r_], '' if r_ else 'derivative check')
    exp_ = to_sympy(prob['expect'])
    if kind == 'curlof':
        if not isinstance(ours, (list, tuple)) or len(ours) != 3:
            return Result('WRONG', 'not a 3-vector')
        x_, y_, z_ = sp.symbols('x y z')
        A = ours
        c = [sp.diff(A[2], y_) - sp.diff(A[1], z_), sp.diff(A[0], z_) - sp.diff(A[2], x_),
             sp.diff(A[1], x_) - sp.diff(A[0], y_)]
        r_ = equiv(c, exp_, rng, domain, tol)
        return Result(R3[r_], '' if r_ else 'curl of the result is not the field')
    if kind == 'set':
        distinct = len(chk) > 1 and chk[1] == 'distinct'
        return grade_set(ours, exp_, rng, domain, tol, distinct)
    if kind == 'oneof':
        res = [equiv(ours, alt, rng, domain, tol) for alt in exp_]
        if any(res):
            return Result('PASS')
        return Result('UNGRADED' if None in res else 'WRONG')
    if kind == 'rel':
        try:
            v1, v2 = sp.N(ours, 60), sp.N(exp_, 60)
            ok = abs(v1 - v2) <= tol * abs(v2)
        except Exception:
            return Result('UNGRADED', 'not numeric')
        return Result('PASS' if ok else 'WRONG', '' if ok else 'relative error %.3g' % float(abs(v1 - v2) / abs(v2)))
    if kind == 'asym':
        return grade_asym(prob, ours, exp_, float(chk[1]))
    if kind == 'matsq':
        M = sp.Matrix(exp_)
        try:
            S = sp.Matrix(ours)
        except Exception:
            return Result('UNEVAL', 'matrix function not evaluated')
        if S.shape != M.shape:
            return Result('WRONG', 'shape')
        r_ = equiv((S * S).tolist(), M.tolist(), rng, domain, tol)
        return Result(R3[r_], '' if r_ else 'square of the result is not the matrix')
    if kind == 'span':
        try:
            A, B = sp.Matrix(ours), sp.Matrix(exp_)
        except Exception:
            return Result('WRONG', 'not a list of vectors')
        if A.shape[1] != B.shape[1]:
            return Result('WRONG', 'vector length')
        rk = sp.Matrix.vstack(A, B).rank(simplify=True)
        ok = A.rank(simplify=True) == A.shape[0] == B.rank(simplify=True) == rk
        return Result('PASS' if ok else 'WRONG', '' if ok else 'different span or dependent vectors')
    if kind == 'logic':
        try:
            same = not sp.satisfiable(sp.Xor(ours, exp_))
        except Exception:
            return Result('UNGRADED', 'not boolean')
        if not same:
            return Result('WRONG')
        if sp.count_ops(ours) > sp.count_ops(exp_):
            return Result('UNEVAL', 'equivalent but not simplified')
        return Result('PASS')
    if kind in ('exact', 'simp', 'form', 'factor', 'value', 'rect', 'terms'):
        r_ = equiv(ours, exp_, rng, domain, tol)
        if r_ is None:
            return Result('UNGRADED', 'equivalence undecided')
        if r_ is False:
            return Result('WRONG')
        if kind in ('exact', 'simp', 'form', 'factor', 'rect', 'terms') and has_float(ours) and not has_float(exp_):
            return Result('UNEVAL', 'numeric instead of exact')
        if kind == 'simp':
            slack = float(prob.get('slack', '0'))
            # count on the unevaluated parse: SymPy's own evaluation (Abs of a
            # number, ...) must not do the simplification for us
            o_ops = ops(to_sympy(out, evaluate=False))
            e_ops = ops(to_sympy(prob['expect'], evaluate=False))
            if o_ops > e_ops + slack:
                return Result('UNEVAL', 'equivalent but not simplified (ops %d > %d)' % (o_ops, e_ops))
        if kind == 'form':
            ok = sp.srepr(ours) == sp.srepr(exp_) if not isinstance(ours, (list, Eqn)) else str(ours) == str(exp_)
            if not ok:
                return Result('UNEVAL', 'equivalent but not in the requested form')
        if kind == 'factor':
            o2 = to_sympy(out, evaluate=False)
            e2 = to_sympy(prob['expect'], evaluate=False)
            if factor_count(o2) != factor_count(e2):
                return Result('UNEVAL', 'factor count %d, expected %d' % (factor_count(o2), factor_count(e2)))
        if kind == 'terms':
            if len(sp.Add.make_args(to_sympy(out, evaluate=False))) != int(chk[1]):
                return Result('UNEVAL', 'not split into %s terms' % chk[1])
        if kind == 'rect':
            for node in sp.preorder_traversal(ours):
                if isinstance(node, sp.Function) or (isinstance(node, sp.Pow) and not node.exp.is_Integer):
                    if any(sp.I in sp.preorder_traversal(a0) for a0 in node.args):
                        return Result('UNEVAL', 'not in rectangular form')
        return Result('PASS')
    raise ValueError("unknown check " + kind)


def grade_set(ours, exp_, rng, domain, tol, distinct=False):
    if not isinstance(ours, list):
        ours = [ours]
    if not isinstance(exp_, list):
        exp_ = [exp_]
    if distinct:
        ded = []
        for o in ours:
            if not any(equiv(o, d, rng, domain, tol) for d in ded):
                ded.append(o)
        ours = ded
    rest = list(exp_)
    undecided = False
    extra = []
    for o in ours:
        for j, r0 in enumerate(rest):
            rr = equiv(o, r0, rng, domain, tol)
            if rr:
                rest.pop(j)
                break
            if rr is None:
                undecided = True
        else:
            extra.append(o)
    if not extra and not rest:
        if has_float(ours) and not has_float(exp_):
            return Result('UNEVAL', 'numeric instead of exact')
        return Result('PASS')
    if undecided:
        return Result('UNGRADED', 'set comparison undecided')
    if not extra and rest:
        return Result('WRONG', 'missing %d solution(s)' % len(rest))
    return Result('WRONG', '%d unexpected / %d missing' % (len(extra), len(rest)))


def grade_satisfy(prob, ours, rng):
    """every returned solution is a root of `eq`; all `need` values present"""
    var = sp.Symbol(prob.get('var', 'x'))
    eq_ = to_sympy(prob['eq'])
    need = to_sympy(prob.get('need', '[]'))
    sols = ours if isinstance(ours, list) else [ours]
    sols = [s_.rhs if isinstance(s_, Eqn) else s_ for s_ in sols]
    nfree = []
    for s_ in sols:
        fs = [f_ for f_ in s_.free_symbols if f_ != var]
        if fs:   # general solution with an integer parameter, e.g. pi/4 + n pi
            nfree.append(s_)
            for kk in range(-2, 3):
                v_ = num(eq_.subs(var, s_.subs({f_: kk for f_ in fs})))
                if abs(v_) > 1e-9:
                    return Result('WRONG', 'solution %s does not satisfy the equation' % s_)
        else:
            v_ = num(eq_.subs(var, s_))
            if abs(v_) > 1e-9:
                return Result('WRONG', 'solution %s does not satisfy the equation' % s_)
    for nd in need:
        found = False
        for s_ in sols:
            fs = [f_ for f_ in s_.free_symbols if f_ != var]
            cands = [s_.subs({f_: kk for f_ in fs}) for kk in range(-3, 4)] if fs else [s_]
            if any(abs(num(c0 - nd)) < 1e-9 for c0 in cands):
                found = True
        if not found:
            return Result('WRONG', 'missing solution %s' % nd)
    if has_float(sols):
        return Result('UNEVAL', 'numeric instead of exact')
    return Result('PASS', 'general solution' if nfree else 'particular solutions')


def truth(cond, sub):
    if isinstance(cond, list):
        return any(truth(c_, sub) for c_ in cond) if cond else False
    if cond is sp.true or cond is True:
        return True
    if cond is sp.false or cond is False:
        return False
    if isinstance(cond, Eqn):
        return abs(complex(sp.N((cond.lhs - cond.rhs).subs(sub)))) < 1e-12
    if isinstance(cond, (sp.And, sp.Or, sp.Not)):
        vals = [truth(a0, sub) for a0 in cond.args]
        return all(vals) if isinstance(cond, sp.And) else any(vals) if isinstance(cond, sp.Or) else not vals[0]
    if isinstance(cond, sp.core.relational.Relational):
        lhs, rhs = sp.N(cond.lhs.subs(sub), 30), sp.N(cond.rhs.subs(sub), 30)
        if not (lhs.is_real and rhs.is_real):
            return False
        return bool(cond.func(lhs, rhs))
    v_ = cond.subs(sub)
    if v_ in (sp.true, sp.false):
        return bool(v_)
    return bool(sp.N(v_))


def grade_ineq(prob, ours, rng):
    var = sp.Symbol(prob.get('var', 'x'))
    exp_ = to_sympy(prob['expect'])
    others = {s_ for s_ in free_syms(exp_) if s_ != var}
    fixed = {s_: _sample(s_, rng, prob['domain']) for s_ in others}
    pts = [sp.Rational(rng.randint(-8000, 8000), 1000) for _ in range(150)]
    pts += [sp.Integer(i_) for i_ in range(-8, 9)] + [sp.Rational(2 * i_ + 1, 4) for i_ in range(-30, 30)]
    if 'points' in prob:
        pts += to_sympy(prob['points'])
    bad = 0
    for pt in pts:
        sub = dict(fixed)
        sub[var] = pt
        try:
            t1 = truth(ours, sub)
            t2 = truth(exp_, sub)
        except Exception:
            continue
        if t1 != t2:
            bad += 1
    if bad:
        return Result('WRONG', 'solution sets differ at %d sample points' % bad)
    if has_float(ours) and not has_float(exp_):
        return Result('UNEVAL', 'numeric boundaries')
    return Result('PASS')


def ode_env(Y, Y1, Y2, var):
    env = {'Y': Y, 'Y1': Y1, 'Y2': Y2, 'x': var, 't': var, 'sp': sp}
    env.update({nm: sp.Symbol(nm) for nm in 'abcknmwqv'})
    return env


def grade_ode(prob, ours, rng, chk):
    """explicit y = f(x, c1..) or implicit F(x, y, c1..) = G; the residual is
    an expression in Y, Y1 (= y'), Y2 (= y'') and x."""
    var = sp.Symbol(prob.get('var', 'x'))
    ysym = sp.Symbol('y')
    nconst = int(chk[1]) if len(chk) > 1 else 0
    sols = ours if isinstance(ours, list) else [ours]
    if not sols:
        return Result('WRONG', 'no solution returned')
    for so in sols:
        implicit = isinstance(so, Eqn) and not (so.lhs == ysym and ysym not in so.rhs.free_symbols)
        if isinstance(so, Eqn) and not implicit:
            so = so.rhs
        if implicit:
            F = so.lhs - so.rhs
            consts = {str(s_) for s_ in F.free_symbols if re.match(r'^c\d+$', str(s_))}
            P = -sp.diff(F, var) / sp.diff(F, ysym)
            P2 = sp.diff(P, var) + sp.diff(P, ysym) * P
            res = eval(prob['expect'], ode_env(ysym, P, P2, var))
            if 'ic' in prob:
                return Result('UNGRADED', 'implicit solution of an IVP')
            dom = dict(prob['domain'])
            dom.setdefault('y', 'pos')
            # for second order the implicit relation has the constants inside;
            # eliminate nothing: check the residual on the solution curve by
            # solving F = 0 for one constant
            if len(consts) and order_of(prob) == 2:
                cc = sorted(consts)[-1]
                csol = sp.solve(F, sp.Symbol(cc))
                if len(csol) != 1:
                    return Result('UNGRADED', 'cannot eliminate a constant')
                res = res.subs(sp.Symbol(cc), csol[0])
        else:
            if not isinstance(so, sp.Basic):
                return Result('WRONG', 'not an expression')
            consts = {str(s_) for s_ in so.free_symbols if re.match(r'^c\d+$', str(s_))}
            res = eval(prob['expect'], ode_env(so, sp.diff(so, var), sp.diff(so, var, 2), var))
            dom = dict(prob['domain'])
        for cc in consts:
            dom.setdefault(cc, 'pos')
        r_ = equiv(sp.sympify(res), sp.Integer(0), rng, dom)
        if r_ is not True:
            return Result(R3[r_] if r_ is None else 'WRONG', 'does not satisfy the ODE')
        if 'ic' in prob:
            ics = eval(prob['ic'], ode_env(so, None, None, var))
            for icv in ics:
                if equiv(sp.sympify(icv), sp.Integer(0), rng, dom) is not True:
                    return Result('WRONG', 'initial condition not satisfied')
        if len(consts) < nconst:
            return Result('WRONG', 'only %d of %d constants' % (len(consts), nconst))
    return Result('PASS', 'implicit solution' if implicit else '')


def order_of(prob):
    return 2 if 'Y2' in prob['expect'] else 1


def sol_dict(ours):
    """[u=.., v=..] or [[u=.., v=..]] -> {'u': expr, ...}"""
    if isinstance(ours, list) and len(ours) == 1 and isinstance(ours[0], list):
        ours = ours[0]
    d = {}
    for e_ in ours if isinstance(ours, list) else [ours]:
        if not isinstance(e_, Eqn):
            raise ValueError('not an equation list')
        d[str(e_.lhs)] = e_.rhs
    return d


def grade_odesys(prob, ours, rng, chk):
    var = sp.Symbol(prob.get('var', 't'))
    d = sol_dict(ours)
    env = {'sp': sp, 't': var, 'q': sp.Symbol('q')}
    for nm in 'uvw':
        env[nm.upper()] = d.get(nm, sp.Integer(0))
    res = eval(prob['expect'], env)
    # constants of the solution (a residual that cancels identically shows none)
    allsyms = set().union(*[sp.sympify(v_).free_symbols for v_ in d.values()])
    dom = dict(prob['domain'])
    consts = {str(s_) for s_ in allsyms if re.match(r'^c\d+$', str(s_))}
    for r_ in res:
        if equiv(sp.sympify(r_), sp.Integer(0), rng, dom) is not True:
            return Result('WRONG', 'does not satisfy the system')
    if len(consts) < int(chk[1]):
        return Result('WRONG', 'only %d of %s constants' % (len(consts), chk[1]))
    return Result('PASS')


def parse_system(inp):
    m_ = re.match(r'^\w+\((\[.*\]),\s*(\[[^\[\]]*\])\)$', inp)
    eqs = to_sympy(m_.group(1))
    vars_ = to_sympy(m_.group(2))
    return [e_.lhs - e_.rhs if isinstance(e_, Eqn) else e_ for e_ in eqs], vars_


def grade_system(prob, ours, rng, chk):
    eqs, vars_ = parse_system(prob['in'])
    want = int(chk[1])
    sols = ours if isinstance(ours, list) else [ours]
    if sols and isinstance(sols[0], Eqn):
        sols = [sols]
    if chk[0] == 'linsys':
        if len(sols) != 1:
            return Result('WRONG', '%d solution sets' % len(sols))
        d = {sp.Symbol(str(e_.lhs)): e_.rhs for e_ in sols[0]}
        free = [v_ for v_ in vars_ if v_ not in d]
        for e_ in eqs:
            r_ = sp.simplify(sp.together(e_.subs(d)))
            if r_ != 0:
                return Result('WRONG', 'equation not satisfied: %s' % r_)
        if len(free) != want:
            return Result('WRONG', '%d free variables, expected %d' % (len(free), want))
        return Result('PASS')
    seen = []
    for so in sols:
        d = {sp.Symbol(str(e_.lhs)): e_.rhs for e_ in so}
        if len(d) != len(vars_):
            return Result('WRONG', 'incomplete solution %s' % so)
        for e_ in eqs:
            if abs(num(e_.subs(d))) > 1e-8:
                return Result('WRONG', 'solution does not satisfy the system')
        key = tuple(num(d[v_]) for v_ in vars_)
        if not any(all(abs(a0 - b0) < 1e-8 for a0, b0 in zip(key, k2)) for k2 in seen):
            seen.append(key)
    if len(seen) != want:
        return Result('WRONG', '%d distinct solutions, expected %d' % (len(seen), want))
    if has_float([e_.rhs for so in sols for e_ in so]) and prob.get('floats') != 'ok':
        return Result('UNEVAL', 'numeric instead of exact')
    return Result('PASS')


def grade_eigvecs(prob, ours, rng, chk):
    m_ = re.match(r'^eigenvects\((.*)\)$', prob['in'])
    M = sp.Matrix(to_sympy(m_.group(1)))
    total = 0
    for pair in ours:
        lam, vecs = pair[0], pair[1]
        for v_ in vecs:
            v_ = sp.Matrix(v_)
            if all(c0 == 0 for c0 in v_):
                return Result('WRONG', 'zero eigenvector')
            if sp.simplify(M * v_ - lam * v_) != sp.zeros(*v_.shape):
                return Result('WRONG', 'M v != lambda v for lambda = %s' % lam)
            total += 1
    if total != int(chk[1]):
        return Result('WRONG', '%d eigenvectors, expected %s' % (total, chk[1]))
    return Result('PASS')


def grade_asym(prob, ours, exp_, E):
    var = sp.Symbol(prob.get('var', 'x'))
    try:
        d = [abs(num((ours - exp_).subs(var, sp.Integer(10) ** kk))) * 10.0 ** (kk * E) for kk in (3, 5, 7)]
    except Exception:
        return Result('UNGRADED', 'not numeric at large ' + str(var))
    ok = d[2] < 1e-2 and d[2] <= d[0] + 1e-12
    return Result('PASS' if ok else 'WRONG', '' if ok else 'wrong asymptotics %s' % d)


# ---------------------------------------------------------------- problems ---
def load_problems(path):
    probs, cur, section = [], None, None
    for raw in open(path):
        line = raw.rstrip('\n')
        if line.startswith('#') or not line.strip():
            continue
        m_ = re.match(r'^@section\s+(\S+)\s+(.*)$', line)
        if m_:
            section = (m_.group(1), m_.group(2).strip())
            continue
        m_ = re.match(r'^==\s*(\S+)\s*\|\s*(.*)$', line)
        if m_:
            cur = {'id': m_.group(1), 'title': m_.group(2).strip(), 'section': section[0],
                   'section_name': section[1], 'pre': [], 'in': None, 'domain': {}, 'allow': set(),
                   'alt': []}
            probs.append(cur)
            continue
        m_ = re.match(r'^\s*(\w+):\s?(.*)$', line)
        if not m_ or cur is None:
            raise SystemExit("bad line in problems.txt: " + line)
        key, val = m_.group(1), m_.group(2).strip()
        if key == 'pre':
            cur['pre'].append(val)
        elif key == 'domain':
            for item in val.split():
                nm, d = item.split('=')
                if ':' in d:
                    lo, hi = d.split(':')
                    d = (float(sp.N(to_sympy(lo))), float(sp.N(to_sympy(hi))))
                cur['domain'][nm] = d
        elif key == 'allow':
            cur['allow'] |= set(val.split())
        elif key == 'alt':
            # alt: CATEGORY | value | note   (a known, deliberately classified answer)
            cat, valx, note = [q_.strip() for q_ in val.split('|', 2)]
            cur['alt'].append((cat, valx, note))
        elif key == 'in' and cur['in'] is not None:
            cur['pre'].append(cur['in'])
            cur['in'] = val
        else:
            cur[key] = val
    return probs


def run_one(prob, repl, timeout_ms, mem_mb=None):
    lines = prob['pre'] + [prob['in']]
    inp = "\n".join(lines) + "\n"
    t0 = time.time()
    env = dict(os.environ)
    env.setdefault('ASAN_OPTIONS', 'detect_leaks=0:abort_on_error=0')
    env.setdefault('UBSAN_OPTIONS', 'print_stacktrace=1:halt_on_error=1')
    try:
        argv = [repl, "-b", "-t", str(timeout_ms)] + (["-m", str(mem_mb)] if mem_mb else [])
        p_ = subprocess.run(argv, input=inp, capture_output=True,
                            text=True, timeout=timeout_ms / 1000.0 * len(lines) + 20, env=env,
                            errors='replace')
    except subprocess.TimeoutExpired:
        return {'status': 'walltimeout', 'out': '', 'stderr': '', 'time': time.time() - t0}
    outs = p_.stdout.split("\n")
    st = 'ok'
    if p_.returncode != 0 or len(outs) < len(lines) or 'Sanitizer' in p_.stderr or 'runtime error' in p_.stderr:
        st = 'crash'
    out = outs[len(lines) - 1] if len(outs) >= len(lines) else ''
    return {'status': st, 'out': out, 'pre_out': outs[:len(lines) - 1], 'rc': p_.returncode,
            'stderr': p_.stderr[-3000:], 'time': time.time() - t0}


IDENT = re.compile(r'([A-Za-z_][A-Za-z_0-9]*)\(')


def classify(prob, run, rng):
    if run['status'] == 'walltimeout':
        return Result('TIMEOUT', 'wall-clock timeout')
    if run['status'] == 'crash':
        return Result('CRASH', 'rc=%s %s' % (run.get('rc'), run['stderr'].strip().split('\n')[0][:200] if run['stderr'] else ''))
    out = run['out'].strip()
    for line_, res_ in zip(prob['pre'], run.get('pre_out', [])):
        m_ = re.match(r'^ERROR (\d+): ?(.*)$', res_.strip())
        if m_:
            for cat, val, note in prob['alt']:
                if res_.replace(' ', '') == val.replace(' ', ''):
                    return Result(cat, note)
            if m_.group(1) == '5':
                return Result('TIMEOUT', 'setup line timed out: ' + line_)
            if m_.group(1) == '1':
                return Result('ERROR', 'setup line %s: %s' % (line_, res_))
            return Result('UNSUPPORTED', 'setup line %s failed: %s' % (line_, m_.group(2)))
    for cat, val, note in prob['alt']:
        if out.replace(' ', '') == val.replace(' ', ''):
            return Result(cat, note)
    m_ = re.match(r'^ERROR (\d+): ?(.*)$', out)
    if m_:
        code, msg = int(m_.group(1)), m_.group(2)
        if prob.get('check', '').startswith('error'):
            return Result('PASS', 'error expected: ' + msg)
        if code == 5:
            return Result('TIMEOUT', msg)
        if code == 3:
            return Result('UNSUPPORTED', msg)
        if code == 6:
            return Result('UNEVAL', 'ERROR 6: ' + msg)
        return Result('ERROR', 'ERROR %d: %s' % (code, msg))
    if prob.get('check', '').startswith('error'):
        if out in ('undef', 'inf', '-inf'):
            return Result('PASS', 'divergence reported as ' + out)
        return Result('WRONG', 'expected an error/divergence')
    allowed = prob['allow']
    names = set(IDENT.findall(out))
    unknown = {nm for nm in names if nm not in BUILTINS and nm not in allowed and len(nm) > 1}
    if unknown:
        return Result('UNSUPPORTED', 'unknown function(s) echoed: ' + ','.join(sorted(unknown)))
    unev = {nm for nm in names if nm in UNEVAL_NAMES and nm not in allowed}
    if unev:
        return Result('UNEVAL', 'unevaluated ' + ','.join(sorted(unev)))
    if out.replace(' ', '') == prob['in'].replace(' ', '') and prob.get('check') != 'manual':
        return Result('UNEVAL', 'returned unevaluated (output = input)')
    try:
        return grade(prob, out, rng)
    except Timeout:
        return Result('UNGRADED', 'grader timeout')
    except Exception as ex:   # unparseable output or grader limitation
        return Result('UNGRADED', 'grader exception: %s' % (str(ex)[:150]))


# ------------------------------------------------------------------- main ---
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repl', default=None)
    ap.add_argument('--asan', action='store_true', help='use build/cas_repl_asan (sanitizer build)')
    ap.add_argument('--only', default=None, help='comma list of ids or section letters')
    ap.add_argument('-j', type=int, default=8)
    ap.add_argument('--timeout', type=int, default=10000, help='per-line timeout in ms')
    ap.add_argument('--verbose', '-v', action='store_true')
    ap.add_argument('--out', default=None, help='output basename (default tests/wester/results)')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--mem', type=int, default=None, help='arena size in MiB (cas_repl -m; default 24)')
    args = ap.parse_args()
    repl = args.repl or (ASAN_REPL if args.asan else REPL)
    probs = load_problems(PROBLEMS)
    if args.only:
        sel = set(args.only.split(','))
        probs = [p_ for p_ in probs if p_['id'] in sel or p_['section'] in sel]
    todo = [p_ for p_ in probs if 'na' not in p_ and 'unsupported' not in p_]
    with ThreadPoolExecutor(max_workers=args.j) as ex:
        runs = dict(zip([p_['id'] for p_ in todo],
                        ex.map(lambda p_: run_one(p_, repl, args.timeout, args.mem), todo)))
    results = []
    for p_ in probs:
        rng = random.Random(args.seed)
        if 'na' in p_:
            res, run = Result('NA', p_['na']), {'out': '', 'time': 0}
        elif 'unsupported' in p_:
            res, run = Result('UNSUPPORTED', p_['unsupported']), {'out': '', 'time': 0}
        else:
            run = runs[p_['id']]
            res = classify(p_, run, rng)
        if p_.get('override'):
            # override: CATEGORY | reason -- manual reclassification after review
            cat, why = [q_.strip() for q_ in p_['override'].split('|', 1)]
            if cat != res.cat:
                res = Result(cat, why + ' (auto: %s %s)' % (res.cat, res.note))
        rec = {'id': p_['id'], 'section': p_['section'], 'section_name': p_['section_name'],
               'title': p_['title'], 'input': p_.get('in', ''), 'pre': p_['pre'],
               'expect': p_.get('expect', ''), 'check': p_.get('check', 'exact'),
               'output': run['out'], 'category': res.cat, 'note': res.note,
               'comment': p_.get('note', ''), 'time': round(run['time'], 3)}
        if run.get('stderr') and res.cat == 'CRASH':
            rec['stderr'] = run['stderr']
        results.append(rec)
        if args.verbose or res.cat not in ('PASS', 'NA'):
            print("%-6s %-11s %s\n       in:  %s\n       out: %s\n       exp: %s   %s" % (
                p_['id'], res.cat, p_['title'], p_.get('in', ''), run['out'][:300], p_.get('expect', ''),
                res.note), flush=True)
    base = args.out or os.path.join(HERE, 'results' + ('_asan' if args.asan else ''))
    json.dump(results, open(base + '.json', 'w'), indent=1)
    write_summary(results, base + '.md', repl)


def write_summary(results, path, repl):
    secs = []
    for r_ in results:
        if (r_['section'], r_['section_name']) not in secs:
            secs.append((r_['section'], r_['section_name']))
    tot = {c_: 0 for c_ in CATS}
    lines = ["# Wester benchmark results", "", "binary: `%s`" % os.path.relpath(repl, ROOT), "",
             "| section | " + " | ".join(CATS) + " | total |", "|---" * (len(CATS) + 2) + "|"]
    for sec, nm in secs:
        cnt = {c_: 0 for c_ in CATS}
        for r_ in results:
            if r_['section'] == sec:
                cnt[r_['category']] += 1
                tot[r_['category']] += 1
        lines.append("| %s %s | " % (sec, nm) + " | ".join(str(cnt[c_]) for c_ in CATS) +
                     " | %d |" % sum(cnt.values()))
    lines.append("| **total** | " + " | ".join("**%d**" % tot[c_] for c_ in CATS) +
                 " | **%d** |" % sum(tot.values()))
    lines.append("")
    for cat in CATS[1:]:
        rs = [r_ for r_ in results if r_['category'] == cat]
        if not rs:
            continue
        lines += ["## %s (%d)" % (cat, len(rs)), ""]
        for r_ in rs:
            lines.append("- **%s** %s -- `%s` -> `%s` (expected `%s`) %s" % (
                r_['id'], r_['title'], r_['input'], r_['output'][:200], r_['expect'], r_['note']))
        lines.append("")
    open(path, 'w').write("\n".join(lines) + "\n")
    print("\n" + "\n".join(lines[:len(secs) + 8]))


if __name__ == '__main__':
    main()
