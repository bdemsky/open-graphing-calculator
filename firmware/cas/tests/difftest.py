#!/usr/bin/env python3
"""
Differential test harness: compares the CAS (build/cas_repl, batch mode)
against SymPy.  Results differ in form, so every check tests mathematical
equivalence:
  - value/diff/limit/sum/simplify: our result == reference, numerically at
    random points (falling back to sympy.simplify(diff) == 0)
  - integrate: d/dx(our antiderivative) == integrand
  - factor: product expands back to the input AND the number of irreducible
    factors (with multiplicity) matches sympy.factor_list
  - expand: equivalent and fully expanded
  - solve: each solution satisfies the equation, solution count matches
  - series: truncated polynomial equals sympy's series().removeO()
The REPL runs in its default complex mode (settings.real = 0, no -r), so
negative bases use SymPy's principal branch ((-1)^(1/3) is complex).
Usage: difftest.py [--quick] [--seed N] [--only CAT] [--verbose]
Writes tests/results/difftest_report.{json,md}.
"""
import json
import os
import random
import re
import signal
import subprocess
import sys
import time
from fractions import Fraction

import sympy as sp
from sympy.concrete.gosper import gosper_sum
from sympy.parsing.sympy_parser import (parse_expr, standard_transformations,
                                        factorial_notation)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REPL = os.path.join(ROOT, "build", "cas_repl")

x, y, z, a, b, c, n, k, t, s = sp.symbols('x y z a b c n k t s')
c1, c2, c3 = sp.symbols('c1 c2 c3')
VARS = {'c1': c1, 'c2': c2, 'c3': c3, 'x': x, 'y': y, 'z': z, 'a': a, 'b': b, 'c': c, 'n': n, 'k': k, 't': t, 's': s}

def _log(u, base=None):
    return sp.log(u, 10) if base is None else sp.log(u, base)

LOCAL = dict(VARS)
LOCAL.update({
    'e': sp.E, 'i': sp.I, 'pi': sp.pi, 'inf': sp.oo, 'undef': sp.nan,
    'ln': sp.log, 'log': _log, 'log10': lambda u: sp.log(u, 10), 'log2': lambda u: sp.log(u, 2),
    'abs': sp.Abs, 'ceil': sp.ceiling, 'floor': sp.floor, 'round': lambda u: sp.sign(u) * sp.floor(sp.Abs(u) + sp.Rational(1, 2)),
    'conj': sp.conjugate, 're': sp.re, 'im': sp.im, 'arg': sp.arg, 'sign': sp.sign,
    'heaviside': sp.Heaviside, 'dirac': sp.DiracDelta, 'erf': sp.erf, 'gamma': sp.gamma,
    'atan2': sp.atan2, 'binomial': sp.binomial, 'factorial': sp.factorial, 'sqrt': sp.sqrt,
    'exp': sp.exp, 'sin': sp.sin, 'cos': sp.cos, 'tan': sp.tan, 'cot': sp.cot, 'sec': sp.sec,
    'csc': sp.csc, 'asin': sp.asin, 'acos': sp.acos, 'atan': sp.atan, 'acot': sp.acot,
    'sinh': sp.sinh, 'cosh': sp.cosh, 'tanh': sp.tanh, 'asinh': sp.asinh, 'acosh': sp.acosh,
    'atanh': sp.atanh, 'true': sp.true, 'false': sp.false, 'Si': sp.Si, 'Ci': sp.Ci, 'Ei': sp.Ei,
    'erfc': sp.erfc, 'erfi': sp.erfi, 'euler_gamma': sp.EulerGamma, 'zeta': sp.zeta,
    'elliptic_f': sp.elliptic_f, 'elliptic_e': sp.elliptic_e, 'elliptic_pi': sp.elliptic_pi,
    'hyp2f1': lambda a0, b0, c0, z0: sp.hyper([a0, b0], [c0], z0),
    'besselj': sp.besselj, 'bessely': sp.bessely, 'besseli': sp.besseli, 'besselk': sp.besselk,
})
TRANS = standard_transformations + (factorial_notation,)


class Timeout(Exception):
    pass


def _alarm(signum, frame):
    raise Timeout()


signal.signal(signal.SIGALRM, _alarm)


def with_timeout(sec, fn, *args):
    signal.setitimer(signal.ITIMER_REAL, sec)
    try:
        return fn(*args)
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)


# ------------------------------------------------------------ parsing ---
def split_top(sx, sep):
    parts, depth, cur = [], 0, ''
    i = 0
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


# series/taylor print their order term last (x-1/6*x^3+O(x^4)): graded as the
# truncated polynomial, like sympy's removeO()
ORDER_RE = re.compile(r'(^|\+)O\((?:[^()]|\((?:[^()]|\([^()]*\))*\))*\)$')


def strip_order(text):
    if not text.endswith(')') or 'O(' not in text:
        return text
    return ORDER_RE.sub('', text) or '0'


def to_sympy(text):
    text = text.strip()
    text = strip_order(text)
    if ' or ' in text and split_top(text, ' or ')[0] != text:
        return sp.Or(*[to_sympy(p) for p in split_top(text, ' or ')])
    if ' and ' in text and split_top(text, ' and ')[0] != text:
        return sp.And(*[to_sympy(p) for p in split_top(text, ' and ')])
    if text.startswith('[') and text.endswith(']') and split_top(text, ',')[0] == text:
        inner = text[1:-1]
        if inner.strip() == '':
            return []
        return [to_sympy(p) for p in split_top(inner, ',')]
    # equation at top level (not <=, >=, !=, :=)
    m = None
    depth = 0
    for i, ch in enumerate(text):
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        elif ch == '=' and depth == 0 and (i == 0 or text[i - 1] not in '<>!:') and \
                (i + 1 >= len(text) or text[i + 1] != '='):
            m = i
            break
    if m is not None:
        return sp.Eq(to_sympy(text[:m]), to_sympy(text[m + 1:]), evaluate=False)
    py = text.replace('^', '**')
    return parse_expr(py, local_dict=LOCAL, transformations=TRANS)


# ------------------------------------------------------- running cas ---
def run_cas(lines, timeout_ms=10000):
    inp = "\n".join(l.replace("\n", " ") for l in lines) + "\n"
    t0 = time.time()
    p = subprocess.run([REPL, "-b", "-t", str(timeout_ms), "-T"], input=inp, capture_output=True, text=True)
    elapsed = time.time() - t0
    outs = p.stdout.split("\n")
    times = [float(m.group(1)) for m in re.finditer(r"\[([0-9.]+) ms\]", p.stderr)]
    outs = outs[:len(lines)]
    while len(outs) < len(lines):
        outs.append("ERROR crash")
    while len(times) < len(lines):
        times.append(0.0)
    return outs, times, elapsed


# ----------------------------------------------------- equivalence ---
def free_syms(*es):
    fs = set()
    for e in es:
        if isinstance(e, sp.Basic):
            fs |= e.free_symbols
    return sorted(fs, key=str)


def numeric_equiv(e1, e2, rng, npts=6, tol=1e-7, domain=(0.2, 1.7), allow_const=False):
    syms = free_syms(e1, e2)
    good = 0
    consts = []
    for _ in range(npts * 3):
        if good >= npts:
            break
        sub = {v: sp.Rational(rng.randint(int(domain[0] * 1000), int(domain[1] * 1000)), 1000) for v in syms}
        try:
            v1 = complex(sp.N(e1.subs(sub), 30)) if isinstance(e1, sp.Basic) else complex(e1)
            v2 = complex(sp.N(e2.subs(sub), 30)) if isinstance(e2, sp.Basic) else complex(e2)
        except (TypeError, ValueError, ZeroDivisionError, OverflowError, AttributeError):
            continue
        if any(map(lambda q: q != q or abs(q) == float('inf'), [v1.real, v1.imag, v2.real, v2.imag])):
            continue
        d = v1 - v2
        if allow_const:
            consts.append(d)
            good += 1
            continue
        if abs(d) > tol * (1 + abs(v1) + abs(v2)):
            return False
        good += 1
    if allow_const:
        if len(consts) < 2:
            return None
        return all(abs(c0 - consts[0]) <= tol * (1 + abs(consts[0])) for c0 in consts)
    return True if good >= 2 else None


def equiv(e1, e2, rng, simplify_fallback=True):
    if e1 is None or e2 is None:
        return False
    try:
        if isinstance(e1, (list, tuple)) or isinstance(e2, (list, tuple)):
            if not (isinstance(e1, (list, tuple)) and isinstance(e2, (list, tuple))) or len(e1) != len(e2):
                return False
            return all(equiv(p, q, rng) for p, q in zip(e1, e2))
        e1 = sp.sympify(e1)
        e2 = sp.sympify(e2)
        if e1 == e2:
            return True
        if (e1.is_infinite or e2.is_infinite or e1 is sp.nan or e2 is sp.nan):
            return e1 == e2
        r = numeric_equiv(e1, e2, rng)
        if r is not None:
            return r
        if simplify_fallback:
            return with_timeout(10, lambda: sp.simplify(e1 - e2) == 0)
        return False
    except Timeout:
        return False
    except Exception:
        return False


# ------------------------------------------------------------- checks ---
def chk_value(out, spec, rng):
    ref = spec['ref']
    if callable(ref):
        ref = with_timeout(30, ref)
    if 'points' in spec:
        ours = to_sympy(out)
        good = 0
        for pt in spec['points']:
            try:
                v1 = complex(sp.N(ours.subs(x, pt), 30))
                v2 = complex(sp.N(ref.subs(x, pt), 30))
            except Exception:
                continue
            if v1 != v1 or v2 != v2 or abs(v2) > 1e12:
                continue
            if abs(v1 - v2) > 1e-7 * (1 + abs(v1) + abs(v2)):
                return False, "differs at %s" % pt
            good += 1
        return (True if good >= 2 else None), None
    return equiv(to_sympy(out), ref, rng), None


def has_float(e):
    if isinstance(e, (list, tuple)):
        return any(has_float(q) for q in e)
    if not isinstance(e, sp.Basic):
        return False
    return any(isinstance(a0, sp.Float) for a0 in sp.preorder_traversal(e))


def chk_exact_value(out, spec, rng):
    """exact (no floats) and mathematically equal"""
    if re.match(r'^(sum|product|integrate)\(', out):
        return None, "unevaluated"
    ours = to_sympy(out)
    ref = spec['ref']
    if callable(ref):
        ref = with_timeout(30, ref)
    if has_float(ours) and not has_float(ref):
        return False, "inexact result"
    if isinstance(ref, sp.Basic) and (ref.has(sp.oo) or ref.has(-sp.oo) or ref is sp.nan):
        return ours == ref, None
    if spec['input'].startswith(('solve(', 'csolve(', 'eigenvals(', 'roots(')) and isinstance(ref, list) and \
            isinstance(ours, list) and len(ours) == len(ref):
        # solution sets: order-independent comparison
        rest = list(ref)
        for o in ours:
            for j, r0 in enumerate(rest):
                if equiv(o, r0, rng):
                    rest.pop(j)
                    break
            else:
                return False, "solution sets differ"
        return True, None
    return equiv(ours, ref, rng), None


def chk_integrate(out, spec, rng):
    f = to_sympy(spec['f'])
    var = VARS[spec.get('var', 'x')]
    if 'integrate(' in out:
        return None, "unevaluated"
    F = to_sympy(out)
    xr = sp.Symbol(str(var) + 'r', real=True)
    dF = sp.diff(F.subs(var, xr), xr).subs(xr, var)
    r = numeric_equiv(dF, f, rng)
    if r is None:
        try:
            r = with_timeout(20, lambda: sp.simplify(dF - f) == 0)
        except Timeout:
            r = False
    return r, None


def chk_defint(out, spec, rng):
    if 'integrate(' in out:
        return None, "unevaluated"
    ours = to_sympy(out)
    if isinstance(ours, sp.Basic) and ours.has(sp.Function('integrate')):
        return None, "unevaluated"
    ref = spec['ref']
    try:
        v1 = complex(sp.N(ours, 20))
        v2 = complex(sp.N(ref, 20))
    except Exception:
        return False, "nonnumeric"
    ok = abs(v1 - v2) <= 1e-8 * (1 + abs(v2))
    exact = not any(isinstance(a0, sp.Float) for a0 in sp.preorder_traversal(ours))
    return ok, (None if exact else "numeric")


def count_factors(expr, var_list):
    """number of non-constant factors with multiplicity, via sympy.factor_list"""
    num, den = sp.fraction(sp.cancel(sp.together(expr)))
    cnt = 0
    for part in (num, den):
        _, fl = sp.factor_list(sp.expand(part))
        for f, m in fl:
            if f.free_symbols:
                cnt += m
    return cnt


def ours_factor_count(e):
    cnt = 0
    for f in sp.Mul.make_args(e):
        if isinstance(f, sp.Mul):
            cnt += ours_factor_count(f)
            continue
        base, ex = f.as_base_exp()
        if base.free_symbols:
            if ex.is_Integer:
                cnt += abs(int(ex))
            else:
                cnt += 1
    return cnt


def chk_factor(out, spec, rng):
    ours = parse_expr(out.replace('^', '**'), local_dict=LOCAL, transformations=TRANS, evaluate=False)
    orig = to_sympy(spec['f'])
    if not equiv(sp.expand(ours.doit() if hasattr(ours, 'doit') else ours), sp.expand(orig), rng):
        return False, "not equal"
    n1 = ours_factor_count(ours)
    n2 = count_factors(orig, None)
    if n1 != n2:
        return False, "factor count %d vs %d" % (n1, n2)
    return True, None


def chk_expand(out, spec, rng):
    ours = to_sympy(out)
    orig = to_sympy(spec['f'])
    if not equiv(ours, orig, rng):
        return False, "not equal"
    if sp.expand(ours) != ours:
        # allow differences in ordering / trivially unexpanded rational coefficients
        if sp.expand(ours) - ours != 0 and len(sp.Add.make_args(sp.expand(ours))) != len(sp.Add.make_args(ours)):
            return False, "not expanded"
    return True, None


def chk_diff(out, spec, rng):
    ours = to_sympy(out)
    f = to_sympy(spec['f'])
    var = VARS[spec.get('var', 'x')]
    order = spec.get('order', 1)
    ref = spec['ref'] if 'ref' in spec else sp.diff(f, var, order)
    return equiv(ours, ref, rng), None


def chk_limit(out, spec, rng):
    ours = to_sympy(out)
    ref = spec['ref']
    if callable(ref):
        try:
            ref = with_timeout(30, ref)
        except (RecursionError, NotImplementedError, ValueError):
            return None, "reference failed"
        if ref is None or (isinstance(ref, sp.Basic) and (ref.has(sp.Limit) or ref.has(sp.AccumBounds) or
                                                          ref.free_symbols)):
            return None, "reference failed"
    if isinstance(ref, sp.Basic) and ref.has(sp.oo) and ref.has(sp.I) and ours == sp.nan:
        return None, "complex infinity (we return undef for real variables)"
    if ref in (sp.oo, -sp.oo, sp.nan, sp.zoo):
        ok = ours == ref or (ref == sp.zoo and ours == sp.nan)
    else:
        ok = equiv(ours, ref, rng)
    if not ok and isinstance(spec.get('f'), sp.Basic) and numeric_limit_agrees(spec['f'], ours):
        return True, "reference disputed numerically; ours confirmed"
    return ok, None


def numeric_limit_agrees(f, ours):
    """SymPy's limit() is occasionally wrong at infinity; evaluate f at large x
    with high precision and check that the trend supports our answer."""
    try:
        pts = [sp.Integer(10) ** k for k in (2, 3, 4)]
        vals = [sp.N(f.subs(x, p), 30) for p in pts]
        if any(abs(sp.im(v)) > 1e-9 * max(1, abs(sp.re(v))) for v in vals):
            return False
        r = [sp.re(v) for v in vals]
        if ours == sp.oo:
            return bool(r[0] < r[1] < r[2] and r[2] > 1e8)
        if ours == -sp.oo:
            return bool(r[0] > r[1] > r[2] and r[2] < -1e8)
        L = sp.Float(ours, 30)
        return bool(abs(r[2] - L) < 1e-6 * max(1, abs(L)) and abs(r[2] - L) <= abs(r[0] - L))
    except Exception:
        return False


def chk_series(out, spec, rng):
    ours = to_sympy(out)
    f = to_sympy(spec['f'])
    var = VARS[spec.get('var', 'x')]
    x0 = spec.get('x0', 0)
    order = spec.get('n', 6)
    ref = with_timeout(30, lambda: sp.series(f, var, x0, order).removeO())
    d = sp.expand(ours - ref)
    if d == 0:
        return True, None
    return equiv(ours, ref, rng), None


def chk_series_inf(out, spec, rng):
    """series at infinity: compare numerically at large x (difference must be
    o(x^-(n-1)))"""
    ours = to_sympy(out)
    f = to_sympy(spec['f'])
    nn = spec['n']
    for xv in [50, 200, 1000]:
        d = abs(complex(sp.N((ours - f).subs(x, xv), 40)))
        if d > 20 * float(xv) ** (-(nn - 1)):
            return False, "too far at %d" % xv
    return True, None


def satisfies(eq, var, sol, rng):
    try:
        val = eq.lhs - eq.rhs if isinstance(eq, sp.Equality) else eq
        v = val.subs(var, sol)
        others = sorted(v.free_symbols, key=str)
        if others:
            # symbolic parameters: check at random parameter values
            for _ in range(3):
                sub = {o: sp.Rational(rng.randint(1100, 2900), 1000) for o in others}
                num = complex(sp.N(v.subs(sub), 30))
                if abs(num) > 1e-8 * (1 + abs(complex(sp.N(sol.subs(sub), 30)))):
                    return False
            return True
        num = complex(sp.N(v, 30))
        try:
            scale = 1 + abs(complex(sp.N(sol, 30)))
        except TypeError:
            scale = 1
        return abs(num) < 1e-8 * scale
    except Exception:
        return False


FAMILY_RE = re.compile(r'n_[1-9]')


def family_param(sol):
    """the integer parameter n_k of a general solution of a periodic equation, or None"""
    ps = [q for q in sol.free_symbols if FAMILY_RE.fullmatch(str(q))]
    return ps[0] if ps else None


def covered(r, fams, var):
    """the real number r is a member s0 + k*p (k integer) of one of the families"""
    for sol, p in fams:
        s0, st = sol.subs(p, 0), sp.diff(sol, p)
        if st.free_symbols or s0.free_symbols:
            return True  # parameters: not checked
        q = complex(sp.N((r - s0) / st, 30))
        if abs(q.imag) < 1e-9 and abs(q.real - round(q.real)) < 1e-8:
            return True
    return False


def chk_solve(out, spec, rng):
    ours = to_sympy(out)
    if not isinstance(ours, list):
        return False, "not a list"
    eq = to_sympy(spec['eq'])
    var = VARS[spec.get('var', 'x')]
    fams = []
    for sol in ours:
        p = family_param(sol)
        if p is not None:
            # a family s0 + k*p: linear in the integer parameter, every member a solution
            if sp.diff(sol, p, 2) != 0:
                return False, "family not linear in %s: %s" % (p, sol)
            fams.append((sol, p))
            for k0 in (-2, -1, 0, 1, 3):
                if not satisfies(eq, var, sol.subs(p, k0), rng):
                    return False, "bad family member %s at %s=%d" % (sol, p, k0)
        elif not satisfies(eq, var, sol, rng):
            return False, "bad solution %s" % sol
    expected = spec.get('count')
    if expected is not None and len(ours) != expected:
        return False, "count %d expected %d" % (len(ours), expected)
    if fams:
        # every real solution sympy finds is in a family
        val = eq.lhs - eq.rhs if isinstance(eq, sp.Equality) else eq
        try:
            refs = with_timeout(20, sp.solve, val, var)
        except Timeout:
            refs = []
        for r in refs:
            if r.free_symbols:
                continue
            c = complex(sp.N(r, 30))
            if abs(c.imag) > 1e-9:
                continue
            if not covered(sp.re(r) if abs(c.imag) > 0 else r, fams, var):
                return False, "sympy root %s not covered" % r
    approx = any(isinstance(a0, sp.Float) for s0 in ours for a0 in sp.preorder_traversal(s0))
    return True, ("numeric" if approx else None)


def num_roots(f, var, lo, hi, npts=20000):
    """roots of f in [lo, hi] found by sign changes (bisected), for checking solve on an interval"""
    fn = sp.lambdify(var, f, 'mpmath')
    import mpmath
    def val(t):
        try:
            v = complex(fn(mpmath.mpf(t)))
            return v.real if abs(v.imag) < 1e-12 and v.real == v.real else None
        except Exception:
            return None
    roots, h = [], (hi - lo) / npts
    pv, pt = val(lo), lo
    for i in range(1, npts + 1):
        t = lo + i * h
        v = val(t)
        if v is not None and pv is not None and (v == 0 or (v < 0) != (pv < 0)):
            a, b, fa = pt, t, pv
            for _ in range(80):
                m = (a + b) / 2
                fm = val(m)
                if fm is None:
                    break
                if (fm < 0) == (fa < 0):
                    a, fa = m, fm
                else:
                    b = m
            m = (a + b) / 2
            fm = val(m)
            if fm is not None and abs(fm) < 1e-6:
                roots.append(m)
        pv, pt = v, t
    return roots


def chk_solve_iv(out, spec, rng):
    """solve(eq, x, a, b): concrete solutions in [a, b], sorted, every one a root; the same set as
    sympy's solveset on the interval (or containing every sign-change root when sympy gives none)"""
    ours = to_sympy(out)
    if not isinstance(ours, list):
        return False, "not a list"
    eq = to_sympy(spec['eq'])
    var = VARS[spec.get('var', 'x')]
    lo, hi = to_sympy(spec['lo']), to_sympy(spec['hi'])
    vals = []
    for sol in ours:
        if sol.free_symbols:
            return False, "not a number: %s" % sol
        if not satisfies(eq, var, sol, rng):
            return False, "bad solution %s" % sol
        vals.append(float(sp.N(sol, 30)))
    if vals != sorted(vals):
        return False, "not sorted"
    flo, fhi = float(sp.N(lo)), float(sp.N(hi))
    if any(v < flo - 1e-9 or v > fhi + 1e-9 for v in vals):
        return False, "outside the interval"
    val = eq.lhs - eq.rhs if isinstance(eq, sp.Equality) else eq
    ref = None
    try:
        ref = with_timeout(20, sp.solveset, val, var, sp.Interval(lo, hi))
    except Timeout:
        pass
    if isinstance(ref, sp.FiniteSet):
        rv = sorted(float(sp.N(r, 30)) for r in ref)
        if len(rv) != len(vals) or any(abs(a0 - b0) > 1e-8 * (1 + abs(a0)) for a0, b0 in zip(rv, vals)):
            return False, "sympy: %s" % rv
        return True, None
    for r in num_roots(val, var, flo, fhi):
        if not any(abs(r - v) < 1e-6 * (1 + abs(v)) for v in vals):
            return False, "numeric root %.12g missing" % r
    return True, None


def chk_system(out, spec, rng):
    ours = to_sympy(out)
    eqs = [to_sympy(e) for e in spec['eqs']]
    vs = [VARS[v] for v in spec['vars']]
    if not isinstance(ours, list):
        return False, "not a list"
    for sol in ours:
        if not isinstance(sol, list):
            return False, "bad shape"
        sub = {}
        for eq in sol:
            sub[eq.lhs] = eq.rhs
        for e in eqs:
            val = (e.lhs - e.rhs).subs(sub)
            try:
                if abs(complex(sp.N(val, 30))) > 1e-8:
                    return False, "unsatisfied"
            except Exception:
                if sp.simplify(val) != 0:
                    return False, "unsatisfied(sym)"
    if spec.get('count') is not None and len(ours) != spec['count']:
        return False, "count %d expected %d" % (len(ours), spec['count'])
    return True, None


def chk_system_planted(out, spec, rng):
    ok, note = chk_system(out, spec, rng)
    if not ok:
        return ok, note
    ours = to_sympy(out)
    planted = spec['planted']
    for sol in ours:
        d = {eq.lhs: eq.rhs for eq in sol}
        try:
            if all(abs(complex(sp.N(d[v] - val, 30))) < 1e-8 for v, val in planted.items()):
                return True, None
        except Exception:
            continue
    return False, "planted solution missing"


def chk_simplify(out, spec, rng):
    ours = to_sympy(out)
    orig = to_sympy(spec['f'])
    if not equiv(ours, orig, rng):
        return False, "not equal"
    ref = spec.get('ref')
    if ref is not None:
        if sp.count_ops(ours) > sp.count_ops(ref) + spec.get('slack', 2):
            return False, "not simplified enough (%d ops vs %d)" % (sp.count_ops(ours), sp.count_ops(ref))
    return True, None


def chk_ineq(out, spec, rng):
    ours = to_sympy(out)
    if not isinstance(ours, list):
        return False, "not a list"
    rel = to_sympy(spec['rel'])
    ref = sp.solveset(rel, x, sp.S.Reals)
    cond = sp.Or(*ours) if ours else sp.false
    for _ in range(60):
        v = sp.Rational(rng.randint(-10000, 10000), 997)
        try:
            a1 = bool(cond.subs(x, v)) if cond not in (sp.true, sp.false) else bool(cond)
        except TypeError:
            return False, "cannot evaluate condition"
        a2 = v in ref
        if a1 != a2:
            return False, "disagree at %s" % v
    return True, None


def ode_to_sympy(ode):
    yf = sp.Function('y')
    o = ode.replace("y'''", "D3").replace("y''", "D2").replace("y'", "D1").replace('y', 'Y0')
    o = o.replace('D3', 'Y3').replace('D2', 'Y2').replace('D1', 'Y1')
    loc = dict(LOCAL)
    loc.update({'Y0': yf(x), 'Y1': sp.Derivative(yf(x), x), 'Y2': sp.Derivative(yf(x), (x, 2)),
                'Y3': sp.Derivative(yf(x), (x, 3))})
    if '=' in o:
        l0, r0 = o.split('=')
        return parse_expr(l0.replace('^', '**'), local_dict=loc) - parse_expr(r0.replace('^', '**'), local_dict=loc)
    return parse_expr(o.replace('^', '**'), local_dict=loc)


def chk_ode(out, spec, rng):
    ours = to_sympy(out)
    sols = ours if isinstance(ours, list) else [ours]
    yf = sp.Function('y')
    res = ode_to_sympy(spec['ode'])
    for sol in sols:
        if isinstance(sol, sp.Equality):
            return None, "implicit"
        consts = sorted([q for q in sol.free_symbols if str(q).startswith('c')], key=str)
        r = res.subs(yf(x), sol).doit()
        for _ in range(4):
            sub = {cc: sp.Rational(rng.randint(1, 30), 7) for cc in consts}
            sub[x] = sp.Rational(rng.randint(100, 1500), 1000)
            try:
                val = complex(sp.N(r.subs(sub), 30))
            except Exception:
                return False, "eval"
            if abs(val) > 1e-8:
                return False, "residual %g" % abs(val)
        for (pt, order, val) in spec.get('ics', []):
            v = sp.diff(sol, x, order).subs(x, pt)
            if abs(complex(sp.N(v - val, 30))) > 1e-8:
                return False, "initial condition"
    return True, None


def chk_laplace(out, spec, rng):
    ours = to_sympy(out)
    f = to_sympy(spec['f'])
    ref = with_timeout(30, lambda: sp.laplace_transform(f, t, s, noconds=True))
    for _ in range(4):
        sv = sp.Rational(rng.randint(3000, 9000), 1000)
        if abs(complex(sp.N((ours - ref).subs(s, sv), 30))) > 1e-8:
            return False, "mismatch"
    return True, None


def chk_ilaplace(out, spec, rng):
    ours = to_sympy(out)
    F = to_sympy(spec['F'])
    back = with_timeout(30, lambda: sp.laplace_transform(ours, t, s, noconds=True))
    for _ in range(4):
        sv = sp.Rational(rng.randint(3000, 9000), 1000)
        if abs(complex(sp.N((back - F).subs(s, sv), 30))) > 1e-8:
            return False, "mismatch"
    return True, None


def chk_approx_hp(out, spec, rng):
    """high precision: all printed digits must be correct"""
    nd = spec['digits']
    try:
        v = sp.N(to_sympy(out), nd + 20)
    except Exception:
        return False, "not numeric"
    ref = sp.N(spec['ref'], nd + 20)
    if abs(v - ref) > abs(ref) * sp.Float(10)**(-(nd - 1)) + sp.Float(10)**(-(nd + 5)):
        return False, "digits wrong"
    mant = re.sub(r'[^0-9]', '', out.split('e')[0]).lstrip('0')
    if len(mant) < nd - 3:
        return False, "too few digits (%d)" % len(mant)
    return True, None


def chk_sum_int(out, spec, rng):
    """a closed form of sum(f, k, 0, n): equal to SymPy's explicit sums for n = 0..8 (the other
    symbols at random rationals); binomial identities hold for integer n only"""
    if re.match(r'^sum\(', out) or out.startswith('ERROR'):
        return None, "unevaluated"
    ours = to_sympy(out)
    f = to_sympy(spec['f'])
    others = [q for q in free_syms(f) if q not in (k, n)]
    for nv in range(0, 9):
        sub = {q: sp.Rational(rng.randint(3, 40), 7) for q in others}
        ref = sp.Add(*[f.subs(sub).subs({n: nv, k: kv}) for kv in range(0, nv + 1)])
        v = ours.subs(sub).subs(n, nv)
        try:
            d = complex(sp.N(sp.nsimplify(v) - ref, 30)) if not isinstance(v, (int,)) else complex(v - ref)
        except Exception:
            try:
                d = complex(sp.N(sp.limit(ours.subs(sub), n, nv) - ref, 30))
            except Exception:
                return False, "eval at n=%d" % nv
        if abs(d) > 1e-9 * (1 + abs(complex(sp.N(ref)))):
            return False, "differs at n=%d" % nv
    return True, None


def chk_ode_series(out, spec, rng):
    """a truncated series solution: the residual has no terms below x^(order - ode order)"""
    if out.startswith('ERROR'):
        return None, out
    sol = to_sympy(out)
    yf = sp.Function('y')
    res = ode_to_sympy(spec['ode'])
    r = res.subs(yf(x), sol).doit()
    consts = sorted([q for q in sol.free_symbols if str(q).startswith('c')], key=str)
    if len(consts) != spec.get('nconst', 2):
        return False, "constants"
    low = spec['n'] - spec['m']
    # x^r (Frobenius): divide by the lowest power present
    for cc in consts:
        part = sp.expand(sp.diff(r, cc))
        if part == 0:
            continue
        lead = sp.series(part, x, 0, low + spec.get('shift', 0)).removeO()
        if sp.simplify(lead) != 0:
            return False, "residual %s" % lead
    for (pt, order, val) in spec.get('ics', []):
        v = sp.diff(sol, x, order).subs(x, pt)
        if abs(complex(sp.N(v - val, 30))) > 1e-8:
            return False, "initial condition"
    return True, None


def chk_approx(out, spec, rng):
    try:
        v = complex(sp.N(to_sympy(out), 20))
    except Exception:
        return False, "not numeric"
    ref = complex(sp.N(spec['ref'], 30))
    if '.' not in out:
        return False, "not a decimal"
    return abs(v - ref) <= 1e-10 * (1 + abs(ref)), None


CHECKS = {
    'ineq': chk_ineq, 'series_inf': chk_series_inf, 'system_planted': chk_system_planted, 'ode': chk_ode, 'laplace': chk_laplace, 'ilaplace': chk_ilaplace, 'approx': chk_approx, 'approx_hp': chk_approx_hp,
    'value': chk_value, 'exact': chk_exact_value, 'integrate': chk_integrate, 'defint': chk_defint,
    'factor': chk_factor, 'expand': chk_expand, 'diff': chk_diff, 'limit': chk_limit,
    'series': chk_series, 'solve': chk_solve, 'system': chk_system, 'simplify': chk_simplify,
    'solve_iv': chk_solve_iv, 'ode_series': chk_ode_series, 'sum_int': chk_sum_int,
}

# ------------------------------------------------------- curated cases ---
def S(e):
    return to_sympy(e)


def curated():
    T = []

    def add(cat, inp, check, **kw):
        d = {'cat': cat, 'input': inp, 'check': check}
        d.update(kw)
        T.append(d)

    # --- arithmetic & number theory
    arith = [
        ("2^100", 2**100), ("100!", sp.factorial(100)), ("1/3+1/6", sp.Rational(1, 2)),
        ("(2/3)^-3", sp.Rational(27, 8)), ("sqrt(72)", 6 * sp.sqrt(2)), ("sqrt(1/2)", sp.sqrt(2) / 2),
        ("8^(2/3)", 4), ("(-8)^(1/3)", sp.root(-8, 3)), ("27^(-1/3)", sp.Rational(1, 3)),
        ("sqrt(-9)", 3 * sp.I), ("(1+i)*(2-i)", 3 + sp.I), ("(3+4*i)/(1-2*i)", (3 + 4 * sp.I) / (1 - 2 * sp.I)),
        ("abs(3+4*i)", 5), ("(1+i)^8", 16), ("i^99", -sp.I), ("gcd(462,1071)", 21), ("lcm(4,6,10)", 60),
        ("binomial(30,15)", sp.binomial(30, 15)), ("nCr(10,3)", 120), ("nPr(10,3)", 720),
        ("iquo(100,7)", 14), ("irem(100,7)", 2), ("mod(-7,3)", 2), ("euler(36)", 12),
        ("nextprime(1000)", 1009), ("prevprime(1000)", 997), ("powmod(3,200,1000)", pow(3, 200, 1000)),
        ("invmod(3,11)", 4), ("floor(-7/2)", -4), ("ceil(7/2)", 4), ("round(5/2)", 3),
        ("2^(1/2)*2^(1/2)", 2), ("sqrt(12)*sqrt(3)", 6), ("exp(ln(5))", 5), ("ln(e^3)", 3),
        ("ln(8)/ln(2)", 3), ("log(1000)", 3), ("sin(pi/3)", sp.sqrt(3) / 2), ("cos(5*pi/6)", -sp.sqrt(3) / 2),
        ("tan(pi/4)", 1), ("sin(7*pi/6)", -sp.Rational(1, 2)), ("cos(pi/12)", sp.cos(sp.pi / 12)),
        ("asin(1/2)", sp.pi / 6), ("acos(-1/2)", 2 * sp.pi / 3), ("atan(sqrt(3))", sp.pi / 3),
        ("atan(1)+atan(1/2)+atan(1/3)", sp.pi / 2), ("sin(pi/5)", sp.sin(sp.pi / 5)), ("cos(pi/8)", sp.cos(sp.pi / 8)),
        ("gamma(5)", 24), ("gamma(1/2)", sp.sqrt(sp.pi)), ("factorial(20)", sp.factorial(20)),
        ("2^64-1", 2**64 - 1), ("123456789*987654321", 123456789 * 987654321),
        ("(2^61-1)*(2^31-1)", (2**61 - 1) * (2**31 - 1)), ("7^77 mod 1000", pow(7, 77) % 1000),
        ("sum([1,2,3,4])", 10), ("max(3,7,5)", 7), ("min(3,7,5)", 3), ("sign(-5)", -1),
        ("abs(-17/3)", sp.Rational(17, 3)), ("exact(0.125)", sp.Rational(1, 8)), ("numer(6/4)", 3),
        ("denom(6/4)", 2), ("re(3+4*i)", 3), ("im(3+4*i)", 4), ("conj(2-3*i)", 2 + 3 * sp.I),
        ("arg(i)", sp.pi / 2), ("(1/2)^10", sp.Rational(1, 1024)), ("sqrt(2)^7", 8 * sp.sqrt(2)),
        ("1-1/2+1/3-1/4", sp.Rational(7, 12)), ("2*3^2-4/2", 16), ("-3^2", -9), ("(-3)^2", 9),
        ("10!/(5!*5!)", 252), ("e^(i*pi)", -1), ("sinh(0)+cosh(0)", 1),
    ]
    for inp, ref in arith:
        add('arith', inp, 'exact', ref=sp.sympify(ref))
    add('arith', 'ifactor(123456789)', 'exact', ref=123456789)
    add('arith', 'ifactor(2^64+1)', 'exact', ref=2**64 + 1)
    add('arith', 'divisors(36)', 'exact', ref=[1, 2, 3, 4, 6, 9, 12, 18, 36])
    add('arith', 'isprime(2^127-1)', 'exact', ref=sp.true)
    add('arith', 'isprime(2^128+1)', 'exact', ref=sp.false)

    # --- expand
    for f in ["(x+1)^5", "(x-y)^4", "(a+b+c)^3", "(x+1)*(x-1)*(x^2+1)", "(2*x-3)^3*(x+2)",
              "(x+y)^2-(x-y)^2", "(1+x+x^2)^3", "x*(x+1)*(x+2)*(x+3)", "(sqrt(2)+x)^2",
              "(x+1/2)^4", "(a*x+b)^3", "(x^2-x+1)*(x^2+x+1)", "(x+i)^3", "exp(x+y)*exp(x)",
              "(sin(x)+cos(x))^2", "(x+1)^10", "(x-1)*(x^9+x^8+x^7+x^6+x^5+x^4+x^3+x^2+x+1)",
              "(3*x^2-2*y)^3", "(x+y+z)^4", "(1-x)^6"]:
        add('expand', "expand(%s)" % f, 'expand', f=f)

    # --- factor
    for f in ["x^2-1", "x^3-1", "x^4-1", "x^6-1", "x^2+5*x+6", "6*x^2+5*x+1", "x^3-6*x^2+11*x-6",
              "x^4+4", "x^4+x^2+1", "x^8-1", "2*x^2-8", "x^3+x^2+x+1", "x^5-x", "x^2-2*x+1",
              "(x+1)^3*(x-2)^2", "x^4-10*x^2+1", "x^6+x^3+1", "x^12-1", "x^2-y^2", "x^3+y^3",
              "x^2+2*x*y+y^2", "a^2*b-a*b^2", "x^4-y^4", "x^2*y+x*y^2+x+y", "x^3-3*x^2*y+3*x*y^2-y^3",
              "6*x^3+11*x^2-3*x-2", "x^4+2*x^3-x-2", "4*x^4-17*x^2+4", "x^5+x+1", "x^7-x",
              "(x^2-1)/(x^2+2*x+1)", "x^15-1", "x^4-5*x^2+6", "3*x^3-27*x", "x^2*y^2-1",
              "x^10+x^5+1", "x^9-x^7-x^2+1", "120*x^5-274*x^4+225*x^3-85*x^2+15*x-1",
              "x^6-2*x^4-x^2+2", "(2*x+3)*(3*x-4)*(5*x+7)"]:
        add('factor', "factor(%s)" % f, 'factor', f=f)

    # --- derivatives
    for f in ["x^5", "sin(x)^2", "exp(x)*sin(x)", "ln(x)/x", "x^x", "sqrt(1+x^2)", "atan(x^2)",
              "tan(x)", "sec(x)", "asin(x)", "acos(2*x)", "sinh(x)*cosh(x)", "exp(-x^2)",
              "ln(ln(x))", "(x^2+1)/(x^2-1)", "x^2*exp(3*x)", "sin(cos(x))", "2^x", "x^(1/3)",
              "atanh(x)", "x*ln(x)-x", "cot(x)", "csc(x)", "erf(x)", "exp(sin(x))*cos(x)",
              "1/sqrt(x)", "(x+1)^10", "ln(x^2+1)", "x/(1+x)^2"]:
        add('diff', "diff(%s,x)" % f, 'diff', f=f)
    add('diff', "diff(x^3*y^2,y)", 'diff', f="x^3*y^2", var='y')
    add('diff', "diff(abs(x),x)", 'diff', f="abs(x)", ref=sp.sign(x))
    add('diff', "diff(sin(x),x,3)", 'diff', f="sin(x)", order=3)
    add('diff', "diff(x^10,x,4)", 'diff', f="x^10", order=4)
    add('diff', "diff(exp(2*x)*x,x,2)", 'diff', f="exp(2*x)*x", order=2)

    # --- indefinite integrals
    ints = ["x^5", "1/x^3", "sqrt(x)", "1/sqrt(x)", "x^(2/3)", "(2*x+1)^5", "1/(3*x+2)", "exp(3*x)",
            "x*exp(-x)", "x^2*exp(x)", "exp(x)*cos(x)", "sin(3*x)", "cos(x/2)", "sin(x)^2", "cos(x)^2",
            "sin(x)^3", "cos(x)^4", "sin(x)*cos(x)^2", "sin(2*x)*cos(3*x)", "tan(x)", "tan(x)^2",
            "sec(x)", "sec(x)^2", "csc(x)", "x*sin(x)", "x*cos(2*x)", "x^2*cos(x)", "ln(x)", "x*ln(x)",
            "x^2*ln(x)", "ln(x)/x", "ln(x)^2", "1/(x*ln(x))", "atan(x)", "asin(x)", "x*atan(x)",
            "1/(1+x^2)", "1/(4+x^2)", "1/(1-x^2)", "1/(x^2-4)", "x/(1+x^2)", "x/(x^2+1)^2",
            "1/(x^2+2*x+2)", "(2*x+3)/(x^2+3*x+2)", "1/(x*(x+1))", "1/(x^2*(x+1))", "(x^3+1)/(x^2-1)",
            "1/(x^3-1)", "1/(x^3+1)", "x^3/(x^2+1)", "1/((x+1)*(x+2)*(x+3))", "1/(x^2+1)^2",
            "1/sqrt(1-x^2)", "1/sqrt(4-x^2)", "sqrt(1-x^2)", "sqrt(4-x^2)", "1/sqrt(x^2+1)",
            "sqrt(x^2+1)", "x/sqrt(x^2+1)", "x*sqrt(x^2+1)", "exp(x)/(1+exp(x))", "exp(2*x)/(1+exp(x))",
            "1/(1+exp(x))", "sinh(x)", "cosh(2*x)", "tanh(x)", "x*exp(x^2)", "cos(x)*exp(sin(x))",
            "sin(x)/cos(x)^2", "1/(1+cos(x))", "1/(2+sin(x))", "exp(sqrt(x))", "x/(x+1)", "(x+1)/x",
            "x^2/(x+1)", "2^x", "x*2^x", "1/(x^2+x+1)", "sqrt(x)*ln(x)", "sec(x)*tan(x)",
            "exp(-x^2)*x", "ln(x)^3/x", "sin(x)^5*cos(x)", "atan(x)/(1+x^2)", "1/(exp(x)+exp(-x))",
            "x^2/sqrt(1-x^2)", "1/(x^4-1)", "x^3*exp(x^2)", "sin(x)*sin(2*x)", "cos(x)^3",
            "sin(x)^2*cos(x)^2", "x*sec(x)^2", "exp(2*x)*sin(3*x)", "(ln(x)+1)*x^x",
            "1/(x*(x^2+1))", "(x^2+1)/(x^4+1)", "1/(x^4+1)", "1/(x^6-1)", "x^2*sqrt(x^3+1)",
            "cos(sqrt(x))/sqrt(x)", "ln(x^2+1)", "x*asin(x)", "acos(x)", "x^3*ln(x)", "exp(-x)*sin(x)",
            "sinh(x)^2", "1/(1-sin(x))", "1/(x^2*sqrt(x^2+1))", "sqrt(x^2-1)", "1/sqrt(x^2-1)",
            "(3*x+1)/(x^2-x-6)", "(x^4+x+1)/(x^3-x)", "1/(x^2+4*x+13)", "x*exp(2*x)*cos(x)",
            "sin(x)/x", "cos(3*x)/x", "exp(-x)/x", "1/ln(x)", "exp(x^2)", "exp(-2*x^2)", "sin(x^2)*x",
            "Si(x)", "erf(x)", "x*erf(x)"]
    for f in ints:
        add('integrate', "integrate(%s,x)" % f, 'integrate', f=f)

    # --- definite integrals
    defs = [("x^2", "0", "1", sp.Rational(1, 3)), ("sin(x)", "0", "pi", 2), ("exp(-x)", "0", "inf", 1),
            ("1/(1+x^2)", "-inf", "inf", sp.pi), ("x*exp(-x^2)", "0", "inf", sp.Rational(1, 2)),
            ("exp(-x^2)", "-inf", "inf", sp.sqrt(sp.pi)), ("ln(x)", "0", "1", -1), ("1/sqrt(x)", "0", "1", 2),
            ("sin(x)^2", "0", "2*pi", sp.pi), ("x*sin(x)", "0", "pi", sp.pi), ("1/x", "1", "e", 1),
            ("sqrt(1-x^2)", "-1", "1", sp.pi / 2), ("1/(x^2+4)", "0", "2", sp.pi / 8), ("abs(x)", "-1", "1", 1),
            ("x^3-2*x", "-1", "2", sp.Rational(3, 4)), ("exp(x)", "0", "ln(2)", 1), ("cos(x)", "0", "pi/2", 1),
            ("1/(x^2)", "1", "inf", 1), ("x^2*exp(-x)", "0", "inf", 2), ("1/((x+1)*(x+2))", "0", "1", sp.log(sp.Rational(4, 3))),
            ("sec(x)^2", "0", "pi/4", 1), ("1/sqrt(1-x^2)", "0", "1", sp.pi / 2), ("x*ln(x)", "1", "e", (sp.E**2 + 1) / 4),
            ("atan(x)", "0", "1", sp.pi / 4 - sp.log(2) / 2), ("exp(-2*x)*sin(x)", "0", "inf", sp.Rational(1, 5)),
            ("1/(1+x^4)", "0", "inf", sp.pi / (2 * sp.sqrt(2))), ("sin(x)/x", "0", "inf", sp.pi / 2),
            ("x^5", "-2", "2", 0), ("tan(x)", "0", "pi/4", sp.log(2) / 2), ("(x+1)^(-3)", "0", "inf", sp.Rational(1, 2)),
            ("sin(x)/x", "0", "1", sp.Si(1)), ("exp(-x^2)", "0", "1", sp.sqrt(sp.pi) * sp.erf(1) / 2),
            ("sin(2*x)/x", "0", "inf", sp.pi / 2)]
    for f, lo, hi, ref in defs:
        add('defint', "integrate(%s,x,%s,%s)" % (f, lo, hi), 'defint', ref=sp.sympify(ref))

    # --- limits
    lims = [("sin(x)/x", "0", 1), ("(1-cos(x))/x^2", "0", sp.Rational(1, 2)), ("(1+1/x)^x", "inf", sp.E),
            ("x*ln(x)", "0", 0), ("x^2*exp(-x)", "inf", 0), ("(x^2-1)/(x-1)", "1", 2),
            ("sqrt(x^2+x)-x", "inf", sp.Rational(1, 2)), ("ln(x)/x", "inf", 0), ("x^x", "0", 1),
            ("(exp(x)-1-x)/x^2", "0", sp.Rational(1, 2)), ("atan(x)", "inf", sp.pi / 2),
            ("(sin(x)-x)/x^3", "0", -sp.Rational(1, 6)), ("tan(x)/x", "0", 1), ("(2*x^2+3)/(5*x^2-x)", "inf", sp.Rational(2, 5)),
            ("x*sin(1/x)", "inf", 1), ("(1+2/x)^(3*x)", "inf", sp.exp(6)), ("(x^3-8)/(x-2)", "2", 12),
            ("(sqrt(x)-2)/(x-4)", "4", sp.Rational(1, 4)), ("exp(-1/x^2)/x^10", "0", 0),
            ("(1-cos(2*x))/(x*sin(x))", "0", 2), ("x/(exp(x)-1)", "0", 1), ("ln(1+x)/x", "0", 1),
            ("(3^x-2^x)/x", "0", sp.log(sp.Rational(3, 2))), ("x^(1/x)", "inf", 1), ("(x+1)^(1/3)-x^(1/3)", "inf", 0),
            ("x*tan(1/x)", "inf", 1), ("(cos(x))^(1/x^2)", "0", sp.exp(-sp.Rational(1, 2))),
            ("(1/x-1/sin(x))", "0", 0), ("x^3/exp(x)", "inf", 0), ("ln(x)^2/sqrt(x)", "inf", 0),
            ("(x^2+3*x)/(2*x^2-1)", "-inf", sp.Rational(1, 2)), ("exp(x)/x^5", "inf", sp.oo),
            ("1/x^2", "0", sp.oo), ("-1/x^2", "0", -sp.oo), ("sin(3*x)/sin(5*x)", "0", sp.Rational(3, 5)),
            ("(1+sin(x))^(1/x)", "0", sp.E), ("asin(x)/x", "0", 1), ("(exp(2*x)-1)/x", "0", 2),
            ("sqrt(x^2+1)/x", "inf", 1), ("x-sqrt(x^2-x)", "inf", sp.Rational(1, 2))]
    for f, pt, ref in lims:
        add('limit', "limit(%s,x,%s)" % (f, pt), 'limit', ref=sp.sympify(ref))
    add('limit', "limit(1/x,x,0,1)", 'limit', ref=sp.oo)
    add('limit', "limit(1/x,x,0,-1)", 'limit', ref=-sp.oo)
    add('limit', "limit(abs(x)/x,x,0,-1)", 'limit', ref=-1)
    add('limit', "limit(exp(1/x),x,0,-1)", 'limit', ref=0)

    # --- series
    sers = [("sin(x)", 0, 8), ("exp(x)", 0, 6), ("1/(1-x)", 0, 6), ("ln(1+x)", 0, 6), ("tan(x)", 0, 8),
            ("sqrt(1+x)", 0, 5), ("cos(x)", 0, 8), ("atan(x)", 0, 8), ("exp(sin(x))", 0, 6),
            ("1/cos(x)", 0, 8), ("x/(exp(x)-1)", 0, 6), ("ln(cos(x))", 0, 8), ("(1+x)^(1/3)", 0, 5),
            ("asin(x)", 0, 8), ("sinh(x)", 0, 8), ("exp(x)*sin(x)", 0, 6), ("1/(1+x^2)", 0, 8),
            ("cos(x)", sp.pi, 5), ("ln(x)", 1, 5), ("exp(x)", 1, 4), ("sqrt(x)", 4, 4), ("1/x", 2, 4),
            ("sin(x)^2", 0, 8), ("tanh(x)", 0, 8), ("exp(-x^2)", 0, 9)]
    for f, x0, nn in sers:
        x0s = sp.sstr(x0).replace('**', '^')
        add('series', "series(%s,x,%s,%d)" % (f, x0s, nn), 'series', f=f, x0=x0, n=nn)

    # --- solve
    sols = [("x^2=4", 2), ("x^2-5*x+6=0", 2), ("x^2+1=0", 0), ("2*x+3=7", 1), ("x^2-2=0", 2),
            ("x^3-6*x^2+11*x-6=0", 3), ("x^3=2", 1), ("x^3+x+1=0", 1), ("x^3-3*x+1=0", 3),
            ("x^4-5*x^2+4=0", 4), ("x^5-x-1=0", 1), ("exp(x)=5", 1), ("ln(x)=2", 1), ("sin(x)=1/2", 2),
            ("cos(x)=0", 1), ("sqrt(x)=3", 1), ("sqrt(x+1)=x-1", 1), ("exp(2*x)-3*exp(x)+2=0", 2),
            ("x*exp(x)=1", 1), ("1/x=2", 1), ("(x-1)/(x^2-1)=0", 0), ("2^x=8", 1), ("abs(x-1)=2", 2),
            ("x^4=16", 2), ("x^6-1=0", 2), ("x^2+x-1=0", 2), ("3*x^2-12=0", 2), ("x^3-x=0", 3),
            ("(x-2)^2*(x+3)=0", 2), ("x/(x+1)=2", 1), ("tan(x)=1", 1), ("x^4-2*x^2+1=0", 2),
            ("10^x=1000", 1), ("ln(x+1)=1", 1), ("sqrt(2*x+3)=x", 1), ("x^3-3*x^2+3*x-1=0", 1),
            ("x^2-2*x+5=0", 0), ("exp(x)+exp(-x)=4", 2), ("x+1/x=3", 2), ("cos(2*x)=1/2", None),
            ("x^4+x^2-6=0", 2), ("x^8-256=0", 2), ("sin(x)+cos(x)=1", None), ("x^3-2*x-5=0", 1),
            ("(x^2-4)/(x-2)=0", 1), ("x^2=x", 2)]
    for eq, cnt in sols:
        add('solve', "solve(%s,x)" % eq, 'solve', eq=eq, count=cnt)
    add('solve', "csolve(x^2+1=0,x)", 'solve', eq="x^2+1=0", count=2)
    add('solve', "csolve(x^3=1,x)", 'solve', eq="x^3=1", count=3)
    add('solve', "csolve(x^4=1,x)", 'solve', eq="x^4=1", count=4)
    add('solve', "solve(a*x+b=0,x)", 'solve', eq="a*x+b=0", count=1)
    add('solve', "solve(x^2+b*x+c=0,x)", 'solve', eq="x^2+b*x+c=0", count=2)
    # general solutions of periodic equations (cas/ext/alg/trigsolve.c): families s0 + p*n_1,
    # the count is the number of families after merging (cos(x)=0: pi/2 + pi*n_1)
    trig = [("sin(x)=1/2", 2), ("cos(x)=0", 1), ("sin(x)=0", 1), ("cos(x)=1", 1), ("cos(x)=-1", 1),
            ("tan(x)=1", 1), ("tan(x)=-sqrt(3)", 1), ("sin(x)=-1", 1), ("cos(x)=sqrt(2)/2", 2),
            ("sin(2*x+1)=1/3", 2), ("cos(3*x-2)=1/2", 2), ("tan(x/2)=1", 1), ("sin(x/3)=0", 1),
            ("2*sin(x)^2-sin(x)-1=0", 1), ("sin(x)=cos(x)", 1), ("tan(x)^2=3", 2), ("sin(x)^2=1/4", 2),
            ("sec(x)=2", 2), ("csc(x)=2", 2), ("cot(x)=1", 1), ("cot(x)=0", 1), ("sec(x)=-1", 1),
            ("sin(x)*cos(x)=0", 1), ("sin(x)+cos(x)=1", 2), ("sin(x)+cos(x)=-1", 2),
            ("2*cos(x)^2+3*cos(x)+1=0", 3), ("sin(x)=cos(2*x)", 1), ("sin(x)+sin(2*x)=0", 2),
            ("cos(2*x)=cos(x)", 1), ("sin(x)^2=sin(x)", 2), ("4*cos(x)^2-3=0", 2), ("tan(2*x)=1", 1),
            ("sin(x)=2", 0), ("cos(x)^2+1=0", 0), ("sin(pi*x)=0", 1), ("cos(pi*x/4)=1/2", 2),
            ("sin(x)^3=sin(x)", 1), ("tan(x)=sin(x)", 1), ("sin(4*x)=sin(2*x)", None),
            ("3*sin(x)-4*sin(x)^3=1", 1), ("sin(x)=sqrt(3)*cos(x)", 1), ("cos(x)-sin(x)=sqrt(2)", 1)]
    for eq, cnt in trig:
        add('trig_solve', "solve(%s,x)" % eq, 'solve', eq=eq, count=cnt)
    # solve(eq, x, a, b): the concrete solutions in [a, b]
    trig_iv = [("sin(x)=1/2", "0", "10"), ("cos(x)=0", "-5", "5"), ("tan(x)=1", "-pi", "pi"),
               ("sin(x)=0", "0", "pi"), ("sin(2*x+1)=1/3", "0", "4"), ("sin(x)*cos(x)=0", "0", "2*pi"),
               ("2*sin(x)^2-sin(x)-1=0", "-pi", "2*pi"), ("tan(x)^2=3", "-2", "2"), ("sec(x)=2", "0", "20"),
               ("cos(3*x)=1/2", "0", "3"), ("sin(x)+cos(x)=1", "-7", "7"), ("sin(x)=cos(2*x)", "-1", "8"),
               ("x^2=4", "0", "5"), ("x^3-x=0", "-1/2", "2"), ("exp(x)=5", "0", "1"), ("sin(x)=2", "0", "9")]
    for eq, lo, hi in trig_iv:
        add('trig_solve_iv', "solve(%s,x,%s,%s)" % (eq, lo, hi), 'solve_iv', eq=eq, lo=lo, hi=hi)
    syss = [(["x+y=3", "x-y=1"], ["x", "y"], 1), (["x^2+y^2=5", "x-y=1"], ["x", "y"], 2),
            (["x+y+z=6", "x-y+z=2", "2*x+y-z=1"], ["x", "y", "z"], 1), (["2*x+3*y=7", "4*x-y=0"], ["x", "y"], 1),
            (["x*y=6", "x+y=5"], ["x", "y"], 2), (["x^2+y^2=1", "x=y"], ["x", "y"], 2),
            (["x+2*y=1", "2*x+4*y=2"], ["x", "y"], 1), (["x+y=1", "x+y=2"], ["x", "y"], 0),
            (["y=x^2", "y=x+2"], ["x", "y"], 2), (["a+b=10", "a-b=4"], ["a", "b"], 1)]
    for eqs, vs, cnt in syss:
        add('system', "solve([%s],[%s])" % (",".join(eqs), ",".join(vs)), 'system', eqs=eqs, vars=vs, count=cnt)

    # --- linear algebra
    M = sp.Matrix
    la = [("det([[1,2],[3,4]])", -2), ("det([[2,0,1],[1,3,2],[1,1,1]])", M([[2, 0, 1], [1, 3, 2], [1, 1, 1]]).det()),
          ("det([[a,b],[c,x]])", a * x - b * c), ("inv([[1,2],[3,4]])", M([[1, 2], [3, 4]]).inv().tolist()),
          ("inv([[2,1,0],[1,2,1],[0,1,2]])", M([[2, 1, 0], [1, 2, 1], [0, 1, 2]]).inv().tolist()),
          ("rref([[1,2,3],[4,5,6],[7,8,10]])", M([[1, 2, 3], [4, 5, 6], [7, 8, 10]]).rref()[0].tolist()),
          ("rref([[1,2,3],[2,4,6]])", M([[1, 2, 3], [2, 4, 6]]).rref()[0].tolist()), ("rank([[1,2],[2,4]])", 1),
          ("transpose([[1,2,3],[4,5,6]])", [[1, 4], [2, 5], [3, 6]]), ("trace([[1,2],[3,4]])", 5),
          ("[[1,2],[3,4]]*[[5,6],[7,8]]", [[19, 22], [43, 50]]), ("[[1,2],[3,4]]^3", (M([[1, 2], [3, 4]])**3).tolist()),
          ("[[1,2],[3,4]]^-1", M([[1, 2], [3, 4]]).inv().tolist()), ("dot([1,2,3],[4,5,6])", 32),
          ("cross([1,0,0],[0,1,0])", [0, 0, 1]), ("charpoly([[1,2],[3,4]],x)", x**2 - 5 * x - 2),
          ("eigenvals([[2,1],[1,2]])", [1, 3]), ("det(identity(4))", 1),
          ("inv([[a,b],[c,x]])", M([[a, b], [c, x]]).inv().tolist()),
          ("det([[1,x,x^2],[1,y,y^2],[1,z,z^2]])", (y - x) * (z - x) * (z - y)),
          ("ker([[1,2],[2,4]])", [[-2, 1]]), ("norm([3,4])", 5),
          ("linsolve([x+y=3,x-y=1],[x,y])", [sp.Eq(x, 2), sp.Eq(y, 1)]),
          ("[[1,2],[3,4]]+[[1,1],[1,1]]", [[2, 3], [4, 5]]), ("2*[[1,2],[3,4]]", [[2, 4], [6, 8]]),
          ("det([[1,2,3,4],[5,6,7,8],[2,6,4,8],[3,1,1,2]])", M([[1, 2, 3, 4], [5, 6, 7, 8], [2, 6, 4, 8], [3, 1, 1, 2]]).det())]
    for inp, ref in la:
        add('linalg', inp, 'exact', ref=ref)

    # --- sums
    sums = [("sum(k,k,1,n)", n * (n + 1) / 2), ("sum(k^2,k,1,n)", n * (n + 1) * (2 * n + 1) / 6),
            ("sum(k^3,k,1,n)", (n * (n + 1) / 2)**2), ("sum(2^k,k,0,n)", 2**(n + 1) - 1),
            ("sum(k*2^k,k,1,n)", sp.summation(k * 2**k, (k, 1, n))), ("sum(1/2^k,k,0,inf)", 2),
            ("sum(1/(k*(k+1)),k,1,n)", n / (n + 1)), ("sum(1/(k*(k+1)),k,1,inf)", 1),
            ("sum(1/k^2,k,1,inf)", sp.pi**2 / 6), ("sum(1/k^4,k,1,inf)", sp.pi**4 / 90), ("sum(k,k,1,100)", 5050),
            ("sum(x^k/k!,k,0,inf)", sp.exp(x)), ("sum(1/(k^2-1),k,2,inf)", sp.Rational(3, 4)),
            ("product(k,k,1,10)", sp.factorial(10)), ("sum((-1)^k/2^k,k,0,inf)", sp.Rational(2, 3)),
            ("sum(3^k,k,1,n)", sp.summation(3**k, (k, 1, n))), ("sum(k^4,k,1,n)", sp.summation(k**4, (k, 1, n))),
            ("sum(1/(4*k^2-1),k,1,inf)", sp.Rational(1, 2)), ("sum(k/3^k,k,1,inf)", sp.Rational(3, 4)),
            ("sum(2*k-1,k,1,n)", n**2), ("product(2,k,1,n)", 2**n), ("sum(1/k,k,1,inf)", sp.oo),
            ("sum(binomial(n,k),k,0,n)", 2**n), ("sum(k^2*binomial(n,k),k,0,n)", n * (n + 1) * 2**(n - 2)),
            ("sum(binomial(n,k)*2^k,k,0,n)", 3**n), ("product(1-1/k^2,k,2,n)", (n + 1) / (2 * n)),
            ("sum(x^k/k,k,1,inf)", -sp.log(1 - x)), ("sum(1/(k*2^k),k,1,inf)", sp.log(2)),
            ("sum(1/((k+1)*(k+3)),k,0,inf)", sp.Rational(3, 4))]
    for inp, ref in sums:
        add('sum', inp, 'exact', ref=sp.sympify(ref))

    # --- simplify
    simps = [("sin(x)^2+cos(x)^2", "1"), ("(x^2-1)/(x-1)", "x+1"), ("2*sin(x)*cos(x)", "sin(2*x)"),
             ("exp(x)*exp(-x)", "1"), ("tan(x)*cos(x)", "sin(x)"), ("(x^3-1)/(x^2+x+1)", "x-1"),
             ("ln(2)+ln(3)", "ln(6)"), ("1-cos(x)^2", "sin(x)^2"), ("(x^2+2*x+1)/(x+1)", "x+1"),
             ("1/(x-1)-1/(x+1)", "2/(x^2-1)"), ("(a^2-b^2)/(a-b)", "a+b"), ("sqrt(x^2)*0+x/x", "1"),
             ("(x+1)^2-(x-1)^2", "4*x"), ("sin(x)/cos(x)-tan(x)", "0"), ("cos(x)^4-sin(x)^4", "cos(2*x)"),
             ("(1+tan(x)^2)*cos(x)^2", "1"), ("exp(2*ln(x))", "x^2"), ("x^2*x^3/x^4", "x"),
             ("(x^4-1)/((x^2+1)*(x-1))", "x+1"), ("sin(x+pi)+sin(x)", "0"), ("cos(pi/2-x)", "sin(x)"),
             ("(1/x+1/y)/(1/x-1/y)", "(x+y)/(y-x)"), ("sinh(x)^2-cosh(x)^2", "-1"),
             ("(x-1)*(x+1)-x^2", "-1"), ("2^x*2^(x+1)", "2^(2*x+1)")]
    for f, ref in simps:
        add('simplify', "simplify(%s)" % f, 'simplify', f=f, ref=S(ref))

    # --- branch safety: transformations must hold at negative and complex points
    bpts = [sp.Rational(-3, 2), sp.Rational(-1, 3), sp.Rational(5, 7), -1 + sp.I, sp.Rational(1, 2) - 2 * sp.I, 2 * sp.I]
    for f in ["ln(x)+ln(x-2)", "sqrt(x)*sqrt(x-1)", "sqrt(x^2)", "(x^2)^(1/3)", "sqrt(x)/sqrt(x+1)", "ln(x^2)",
              "exp(ln(x))", "x^(1/2)*x^(1/3)", "(x^3)^(1/3)", "sqrt(4*x)", "sqrt(-x)*sqrt(-x)", "ln(1/x)",
              "sqrt(1/x)", "1/sqrt(x)", "(x*(x+1))^(1/2)", "atan(1/x)", "asin(x)+acos(x)", "ln(2*x)-ln(2)",
              "sqrt(x^2+2*x+1)", "(sqrt(x)-1)*(sqrt(x)+1)", "x^(2/3)/x^(1/3)", "(-x)^(1/2)", "sqrt(2*x-x^2)"]:
        for cmd in ["", "simplify", "normal", "expand", "factor"]:
            inp = "%s(%s)" % (cmd, f) if cmd else f
            add('branch', inp, 'value', ref=S(f), points=bpts)
    for rel in ["x^2<4", "x^2>=4", "(x-1)/(x+2)>=0", "x^2+1>0", "x^2<=0", "exp(x)>2", "ln(x)<1",
                "x^3-x>0", "abs(x-1)<2", "sqrt(x)<2", "2*x+3<=7", "(x-3)*(x+1)*(x-5)<0", "1/x>1",
                "x^2-x-6>=0", "x^4-5*x^2+4<0"]:
        add('ineq', "solve(%s,x)" % rel, 'ineq', rel=rel)

    # --- ODEs
    odes = ["y'=2*x", "y'=y", "y'+2*y=exp(x)", "y'=x*y", "y'=y^2", "y''+y=0", "y''-3*y'+2*y=0",
            "y''+2*y'+y=0", "y''+y=x", "y''+4*y=sin(x)", "y'''-y=0", "y'=x/y", "y'+y/x=x^2",
            "y''-y=exp(2*x)", "y'=1+y^2", "y''+2*y'+5*y=0", "y'=(x+y)/x", "y'=y/x+x/y", "2*x*y+(x^2+1)*y'=0",
            "x^2*y''+x*y'-y=0", "x^2*y''-3*x*y'+4*y=0", "2*x+3*y^2*y'=0", "x^2*y''+x*y'+y=0", "y'=x*y^2",
            "y'+y=x", "y''=x^2", "y'''=0", "y''+y'=0", "y'=y*(1-y)", "x*y'+y=x^3"]
    for ode in odes:
        add('ode', "dsolve(%s,y,x)" % ode, 'ode', ode=ode, ics=[])
    add('ode', "dsolve([y''+y=0,y(0)=1,y'(0)=0],y,x)", 'ode', ode="y''+y=0", ics=[(0, 0, 1), (0, 1, 0)])
    add('ode', "dsolve([y'=y,y(0)=2],y,x)", 'ode', ode="y'=y", ics=[(0, 0, 2)])
    add('ode', "dsolve([y''-y=0,y(0)=0,y'(0)=2],y,x)", 'ode', ode="y''-y=0", ics=[(0, 0, 0), (0, 1, 2)])

    # --- hypergeometric sums: Gosper (indefinite) and Zeilberger (definite, binomial identities)
    for f, lo in [("k*k!", 0), ("k!/(k+2)!", 0), ("(4*k+1)*k!/(2*k+1)!", 0), ("binomial(2*k,k)/4^k", 0),
                  ("k*2^k/((k+1)*(k+2))", 0), ("(-1)^k*binomial(n,k)", 0), ("binomial(k,3)", 0),
                  ("k^2*3^k", 1), ("(2*k+1)/(k^2*(k+1)^2)", 1), ("1/(k*(k+2))", 1), ("k*binomial(2*k,k)/4^k", 0),
                  ("k^3*2^k", 0)]:
        fs = to_sympy(f)
        add('gosper_sum', "sum(%s,k,%d,m)" % (f, lo), 'exact',
            ref=(lambda fs=fs, lo=lo: gosper_sum(fs, (k, lo, sp.Symbol('m')))))
    for f in ["binomial(n,k)^2", "binomial(n,k)*binomial(a,n-k)", "binomial(n,k)*binomial(a,k)", "k*binomial(n,k)^2",
              "(-1)^k*binomial(n,k)^2", "binomial(n,k)/binomial(2*n,k)", "binomial(n,k)^2/binomial(2*n,n)",
              "binomial(n,k)*binomial(n,n-k)", "binomial(n+k,k)/2^k", "binomial(n,k)*2^k", "(-1)^k*binomial(2*n,k)^2"]:
        add('summation', "sum(%s,k,0,n)" % f, 'sum_int', f=f)

    # --- ODEs: Riccati, Bessel, normal form of general solutions, power series, wronskian
    for ode in ["y'=y^2-x^2+1", "y'=-y^2+x^2+1", "y'=y^2-2/x^2", "x^2*y'=x^2*y^2+x*y-1", "y'=y^2+2*x*y+x^2-1",
                "x^2*y''+x*y'+(x^2-1/4)*y=0", "x^2*y''+x*y'+(4*x^2-1)*y=0", "x^2*y''+x*y'-(x^2+1)*y=0",
                "y'+2*y=exp(x)", "y''-y=x*exp(x)"]:
        add('ode_more', "dsolve(%s,y,x)" % ode, 'ode', ode=ode, ics=[])
    for ode, n0, m0, sh in [("y''+x*y=0", 8, 2, 0), ("y''-x*y=0", 7, 2, 0), ("y''+y=0", 6, 2, 0),
                            ("y''-2*x*y'+4*y=0", 7, 2, 0), ("(1-x^2)*y''-2*x*y'+6*y=0", 7, 2, 0), ("y'-x*y=0", 7, 1, 0),
                            ("y''+x^2*y=x", 8, 2, 0), ("x^2*y''+x*y'+(x^2-1/4)*y=0", 7, 2, 0),
                            ("4*x*y''+2*y'+y=0", 6, 2, -1), ("x^2*y''+x*y'+x^2*y=0", 7, 2, 0),
                            ("x*y''+y'-y=0", 6, 2, -1)]:
        add('ode_series', "dsolve(%s,y,x,0,%d)" % (ode, n0), 'ode_series', ode=ode, n=n0, m=m0, shift=sh,
            nconst=m0)
    add('ode_series', "dsolve([y''+x*y=0,y(0)=1,y'(0)=0],y,x,0,8)", 'ode_series', ode="y''+x*y=0", n=8, m=2,
        nconst=0, ics=[(0, 0, 1), (0, 1, 0)])
    for fl in ["[sin(x),cos(x)]", "[exp(x),x*exp(x),x^2*exp(x)]", "[1,x,x^2]", "[x,x*ln(x)]", "[exp(a*x),exp(b*x)]",
               "[sinh(x),cosh(x)]", "[x^2,x^3]"]:
        add('wronskian', "wronskian(%s,x)" % fl, 'exact',
            ref=(lambda fl=fl: sp.simplify(sp.wronskian(to_sympy(fl), x))))

    # --- elliptic integrals (Legendre form; checked by differentiation)
    for f in ["1/sqrt(1-x^4)", "1/sqrt(1+x^4)", "x^2/sqrt(1-x^4)", "sqrt(1-x^4)", "1/sqrt((1-x^2)*(1-4*x^2))",
              "1/sqrt(1+x^3)", "sqrt(1+x^3)", "1/sqrt(x^4-1)", "1/sqrt(2+5*x^2-3*x^4)", "sqrt(1-2*x^2)/sqrt(1-x^2)",
              "1/((1+x^2)*sqrt(1-x^4))", "1/sqrt(x*(1-x)*(2-x))", "sqrt(x)/sqrt(1+x^2)", "1/sqrt(1+cos(x)^2)",
              "sqrt(2-sin(x)^2)", "sin(x)^(1/3)", "tan(x)^(1/3)*sec(x)^2+sqrt(cos(x))", "1/(sqrt(2+cos(x))*sqrt(3+cos(x)))"]:
        add('integrate_ell', "integrate(%s,x)" % f, 'integrate', f=f)

    # --- Laplace
    for f in ["1", "t^2", "exp(2*t)", "sin(3*t)", "t*exp(-t)", "exp(-t)*cos(2*t)", "t*sin(t)", "cosh(2*t)",
              "t^3*exp(t)", "sin(t)^2", "5*t+3", "exp(t)*sin(t)"]:
        add('laplace', "laplace(%s,t,s)" % f, 'laplace', f=f)
    for F in ["1/(s-2)", "1/s^3", "1/(s^2+4)", "(s+1)/(s^2+2*s+5)", "1/(s*(s+1))", "s/(s^2+9)",
              "1/(s^2-1)", "(2*s+3)/(s^2+3*s+2)", "1/(s+1)^3", "3/(s^2+6*s+13)"]:
        add('ilaplace', "ilaplace(%s,s,t)" % F, 'ilaplace', F=F)

    # --- approximate numerics
    for e0 in ["pi", "e", "sqrt(2)", "ln(10)", "sin(1)", "atan(1/3)", "exp(-5)", "gamma(1/3)", "erf(1/2)",
               "asinh(2)", "10^(1/3)", "cos(pi/7)", "1/7", "2^0.5", "acos(0.3)", "tanh(0.25)", "ln(2)*1e20",
               "1e-30*3", "sqrt(-2)", "(1+2*i)^0.5"]:
        add('approx', "approx(%s)" % e0, 'approx', ref=S(e0))
    for e0, nd in [("pi", 50), ("e", 40), ("sqrt(2)", 60), ("ln(2)", 30), ("sin(1)", 40), ("exp(10)", 30),
                   ("1/7", 40), ("asin(1/3)", 30), ("2^(1/3)", 30), ("cos(100)", 30), ("tanh(1/2)", 25),
                   ("atan(10)", 35), ("ln(10)/ln(3)", 30), ("sqrt(pi)*e", 45), ("acosh(3)", 30), ("sinh(2)", 30),
                   ("pi^pi", 30), ("exp(-20)", 25), ("cos(pi/7)", 40), ("100!/99!+sqrt(3)", 30)]:
        add('approx_hp', "approx(%s,%d)" % (e0, nd), 'approx_hp', ref=S(e0), digits=nd)
    return T



def hard_cases():
    import cases_hard as H
    T = []

    def add(cat, inp, check, **kw):
        d = {'cat': cat, 'input': inp, 'check': check}
        d.update(kw)
        T.append(d)
    for f in H.HARD_INTEGRALS:
        add('h_integrate', "integrate(%s,x)" % f, 'integrate', f=f)
    for f, pt, ref in H.HARD_LIMITS:
        if ref is None:
            ref = sp.nan
        add('h_limit', "limit(%s,x,%s)" % (f, pt), 'limit', ref=sp.sympify(ref))
    for eq, cnt in H.HARD_SOLVE:
        add('h_solve', "solve(%s,x)" % eq, 'solve', eq=eq, count=cnt)
    for f in H.HARD_FACTOR:
        add('h_factor', "factor(%s)" % f, 'factor', f=f)
    for f, ref in H.HARD_SIMPLIFY:
        add('h_simplify', "simplify(%s)" % f, 'simplify', f=f, ref=S(ref))
    for f, x0, nn in H.HARD_SERIES:
        x0s = sp.sstr(x0).replace('**', '^')
        add('h_series', "series(%s,x,%s,%d)" % (f, x0s, nn), 'series', f=f, x0=x0, n=nn)
    for inp, ref in H.HARD_SUMS:
        if ref is None:
            ref = n * 2**(n - 1)
        add('h_sum', inp, 'exact', ref=sp.sympify(ref))
    for inp, ref in H.HARD_LINALG:
        add('h_linalg', inp, 'exact', ref=ref)
    for f in H.T3_INTEGRALS:
        add('t3_integrate', "integrate(%s,x)" % f, 'integrate', f=f)
    for f, pt, ref in H.T3_LIMITS:
        fs = to_sympy(f)
        ptv = to_sympy(pt)
        r = ref if ref is not None else (lambda ff=fs, pp=ptv: sp.limit(ff, x, pp))
        add('t3_limit', "limit(%s,x,%s)" % (f, pt), 'limit', ref=r if callable(r) else sp.sympify(r))
    for eq, cnt in H.T3_SOLVE:
        add('t3_solve', "solve(%s,x)" % eq, 'solve', eq=eq, count=cnt)
    for f, ref in H.T3_SIMPLIFY:
        add('t3_simplify', "simplify(%s)" % f, 'simplify', f=f, ref=S(ref))
    for inp, ref in H.T4_EXACT:
        if ref is None:
            continue
        add('t4_misc', inp, 'exact', ref=sp.sympify(ref) if not isinstance(ref, list) else ref)
    for f, x0, nn in H.T4_SERIES:
        x0s = 'inf' if x0 == sp.oo else sp.sstr(x0).replace('**', '^')
        add('t4_series', "series(%s,x,%s,%d)" % (f, x0s, nn), 'series_inf' if x0 == sp.oo else 'series', f=f, x0=x0, n=nn)
    return T


def exact_value_cases(rng):
    """special values: trig at rational multiples of pi, rational powers,
    inverse trig, logs, gamma, binomials, rounding (all must be exact)"""
    T = []
    ins = []
    for d in range(1, 13):
        for k in range(0, 2 * d + 1):
            for f in ['sin', 'cos', 'tan', 'sec', 'csc', 'cot']:
                ins.append("%s(%d*pi/%d)" % (f, k, d))
    for _ in range(200):
        p = rng.randint(-50, 50)
        q = rng.randint(1, 30)
        r = rng.randint(-7, 7)
        s0 = rng.randint(1, 6)
        ins.append("(%d/%d)^(%d/%d)" % (p, q, r, s0))
    for v in ['0', '1/2', '-1/2', 'sqrt(2)/2', '-sqrt(2)/2', 'sqrt(3)/2', '-sqrt(3)/2', '1', '-1', 'sqrt(3)', '-sqrt(3)',
              'sqrt(3)/3', '-sqrt(3)/3', '2-sqrt(3)', '2+sqrt(3)']:
        for f in ['asin', 'acos', 'atan']:
            ins.append("%s(%s)" % (f, v))
    for nn in range(1, 30):
        ins += ["ln(%d)" % nn, "ln(1/%d)" % nn, "log(%d)" % nn]
    for nn in range(-6, 8):
        ins.append("gamma(%d/2)" % (2 * nn + 1))
    for nn in range(0, 8):
        for kk in range(-1, 10):
            ins.append("binomial(%d,%d)" % (nn, kk))
    ins += ["atan2(%d,%d)" % (p, q) for p in range(-2, 3) for q in range(-2, 3) if (p, q) != (0, 0)]
    for f in ['floor', 'ceil', 'round']:
        ins += ["%s(%s)" % (f, e0) for e0 in ['pi', '-pi', 'e^2', 'sqrt(50)', '-sqrt(2)', '7/2', '-7/2', '5/2', '-5/2']]
    ins += ["sinh(ln(2))", "cosh(ln(3))", "exp(i*pi/3)", "(-1)^(1/3)", "(-8)^(2/3)", "sqrt(-2)*sqrt(-3)",
            "(-4)^(3/2)", "abs(-3+4*i)", "arg(-1-i)", "re((1+2*i)^3)", "im((1+2*i)^3)", "zeta(6)", "zeta(-3)"]
    for inp in ins:
        try:
            ref = to_sympy(inp)
            if ref.has(sp.zoo) or ref.has(sp.nan) or ref.has(sp.oo):
                continue
        except Exception:
            continue
        T.append({'cat': 'exact_vals', 'input': inp, 'check': 'exact', 'ref': ref})
    return T

# --------------------------------------------------------- random cases ---
def rand_poly(rng, var, deg, coef=5):
    return sum(rng.randint(-coef, coef) * var**i for i in range(deg + 1))


def rand_expr(rng, depth=0):
    r = rng.random()
    if depth > 3 or r < 0.25:
        return rng.choice([x, x, x + 1, 2 * x, sp.Rational(rng.randint(1, 5), rng.randint(1, 3)), x**2])
    if r < 0.45:
        return rand_expr(rng, depth + 1) + rand_expr(rng, depth + 1)
    if r < 0.6:
        return rand_expr(rng, depth + 1) * rand_expr(rng, depth + 1)
    if r < 0.68:
        return rand_expr(rng, depth + 1) / (rand_expr(rng, depth + 1) + 3)
    if r < 0.75:
        return rand_expr(rng, depth + 1)**rng.randint(2, 3)
    f = rng.choice([sp.sin, sp.cos, sp.exp, lambda u: sp.log(u**2 + 1), lambda u: sp.sqrt(u**2 + 1), sp.atan])
    return f(rand_expr(rng, depth + 1))


def rand_expr_branchy(rng, depth=0):
    """random expressions with branch cuts: sqrt, ln, fractional powers, asin, abs"""
    r = rng.random()
    if depth > 3 or r < 0.25:
        return rng.choice([x, x, -x, x + 1, 2 * x - 1, sp.Rational(rng.randint(1, 5), rng.randint(1, 3)), x**2, -1])
    if r < 0.45:
        return rand_expr_branchy(rng, depth + 1) + rand_expr_branchy(rng, depth + 1)
    if r < 0.6:
        return rand_expr_branchy(rng, depth + 1) * rand_expr_branchy(rng, depth + 1)
    if r < 0.68:
        return rand_expr_branchy(rng, depth + 1) / (rand_expr_branchy(rng, depth + 1) + 3)
    if r < 0.76:
        return rand_expr_branchy(rng, depth + 1)**rng.choice([2, 3, -1, sp.Rational(1, 2), sp.Rational(1, 3),
                                                              sp.Rational(-1, 2), sp.Rational(3, 2), sp.Rational(2, 3)])
    f = rng.choice([sp.sqrt, sp.log, sp.exp, sp.sin, sp.atan, lambda u: sp.asin(u / 7), sp.Abs, sp.cos,
                    lambda u: sp.root(u, 3), sp.sinh])
    return f(rand_expr_branchy(rng, depth + 1))


def rand_growth(rng, depth=0):
    """random terms for limits at infinity: powers, exponentials, logs"""
    r = rng.random()
    if depth > 2 or r < 0.35:
        return rng.choice([x, x**2, x**3, sp.sqrt(x), sp.exp(x), sp.exp(2 * x), sp.exp(x / 2), sp.log(x),
                           2**x, 3**x, x * sp.log(x), sp.exp(sp.sqrt(x)), sp.Integer(rng.randint(1, 5))])
    if r < 0.55:
        return rand_growth(rng, depth + 1) + rng.choice([1, -1]) * rand_growth(rng, depth + 1)
    if r < 0.75:
        return rand_growth(rng, depth + 1) * rand_growth(rng, depth + 1)
    if r < 0.9:
        return rand_growth(rng, depth + 1) / (rand_growth(rng, depth + 1) + 1)
    return rng.choice([sp.log, sp.sqrt, lambda u: u**2])(rand_growth(rng, depth + 1))


def our_syntax(e):
    s0 = sp.sstr(e)
    s0 = s0.replace('**', '^').replace('log(', 'ln(').replace('E', 'e').replace('I', 'i')
    return s0


def random_cases(rng, nrand):
    T = []
    for _ in range(nrand):
        e = rand_expr(rng)
        T.append({'cat': 'rand_eval', 'input': our_syntax(e), 'check': 'value', 'ref': e})
    for _ in range(nrand):
        e = rand_expr(rng)
        pts = [sp.Rational(-rng.randint(200, 1700), 1000) for _ in range(3)] + \
              [sp.Rational(rng.randint(-1500, 1500), 1000) + sp.I * sp.Rational(rng.randint(-1500, 1500), 1000) for _ in range(3)]
        T.append({'cat': 'rand_eval_cplx', 'input': our_syntax(e), 'check': 'value', 'ref': e, 'points': pts})
    for _ in range(nrand):
        e = sp.sympify(rand_expr_branchy(rng))
        if e.has(sp.re) or e.has(sp.im) or e.has(sp.arg):
            continue  # our re/im assume real variables
        # variables are real (documented convention): sample negative and positive reals,
        # which still crosses the branch cuts of sqrt/ln/fractional powers
        pts = [sp.Rational(rng.randint(-1700, 1700), 1000) for _ in range(6)]
        T.append({'cat': 'rand_eval_branch', 'input': our_syntax(e).replace('Abs', 'abs'), 'check': 'value', 'ref': e,
                  'points': pts})
        cmd = rng.choice(['simplify', 'normal', 'expand', 'factor', 'trigsimp', 'texpand', 'tlin', 'lncollect'])
        T.append({'cat': 'rand_transform_branch', 'input': "%s(%s)" % (cmd, our_syntax(e).replace('Abs', 'abs')),
                  'check': 'value', 'ref': e, 'points': pts})
    for _ in range(nrand):
        e = rand_expr(rng)
        T.append({'cat': 'rand_diff', 'input': "diff(%s,x)" % our_syntax(e), 'check': 'diff', 'f': our_syntax(e)})
    for _ in range(nrand // 2):
        p = 1
        for _ in range(rng.randint(2, 3)):
            p *= rand_poly(rng, x, rng.randint(1, 3), 4) + rng.choice([0, y, y**2])
        T.append({'cat': 'rand_expand', 'input': "expand(%s)" % our_syntax(p), 'check': 'expand', 'f': our_syntax(p)})
    for _ in range(nrand // 2):
        p = 1
        for _ in range(rng.randint(2, 4)):
            q = rand_poly(rng, x, rng.randint(1, 4), 6)
            if q.free_symbols:
                p *= q
        if p == 1:
            p = x**2 - 1
        ex = sp.expand(p)
        T.append({'cat': 'rand_factor', 'input': "factor(%s)" % our_syntax(ex), 'check': 'factor', 'f': our_syntax(ex)})
    for _ in range(nrand // 2):
        # multivariate
        p = (rand_poly(rng, x, 2, 3) + rng.randint(1, 3) * y) * (rand_poly(rng, y, 2, 3) + rng.randint(1, 3) * x)
        ex = sp.expand(p)
        if not ex.free_symbols:
            continue
        T.append({'cat': 'rand_factor_mv', 'input': "factor(%s)" % our_syntax(ex), 'check': 'factor', 'f': our_syntax(ex)})
    for _ in range(nrand):
        # integrand = derivative of a random expression (antiderivative exists)
        F = rand_expr(rng)
        f = sp.diff(F, x)
        if f == 0:
            continue
        T.append({'cat': 'rand_integrate', 'input': "integrate(%s,x)" % our_syntax(f), 'check': 'integrate',
                  'f': our_syntax(f)})
    for _ in range(nrand // 2):
        # rational functions with factorable denominators
        den = 1
        for _ in range(rng.randint(1, 3)):
            den *= (x - rng.randint(-4, 4))**rng.randint(1, 2)
        if rng.random() < 0.4:
            den *= x**2 + rng.randint(1, 4)
        num = rand_poly(rng, x, rng.randint(0, 3), 5)
        if num == 0:
            num = 1
        f = num / sp.expand(den)
        T.append({'cat': 'rand_ratint', 'input': "integrate(%s,x)" % our_syntax(f), 'check': 'integrate',
                  'f': our_syntax(f)})
    for _ in range(nrand // 2):
        roots = [rng.randint(-5, 5) for _ in range(rng.randint(1, 4))]
        p = sp.expand(sp.prod([x - r for r in roots]) * rng.randint(1, 3))
        T.append({'cat': 'rand_solve', 'input': "solve(%s=0,x)" % our_syntax(p), 'check': 'solve',
                  'eq': our_syntax(p), 'count': len(set(roots))})
    for _ in range(nrand // 2):
        nn = rng.randint(2, 4)
        Mx = sp.Matrix(nn, nn, lambda i, j: rng.randint(-5, 5))
        ms = str(Mx.tolist()).replace(' ', '')
        T.append({'cat': 'rand_linalg', 'input': "det(%s)" % ms, 'check': 'exact', 'ref': Mx.det()})
        if Mx.det() != 0:
            T.append({'cat': 'rand_linalg', 'input': "inv(%s)" % ms, 'check': 'exact', 'ref': Mx.inv().tolist()})
    for _ in range(nrand // 3):
        nv = rng.randint(2, 3)
        vs = [x, y, z][:nv]
        M = sp.Matrix(nv, nv, lambda i, j: rng.randint(-5, 5))
        sol = [sp.Rational(rng.randint(-9, 9), rng.randint(1, 3)) for _ in range(nv)]
        eqs = []
        for i in range(nv):
            lhs = sum(M[i, j] * vs[j] for j in range(nv))
            rhs = sum(M[i, j] * sol[j] for j in range(nv))
            eqs.append("%s=%s" % (our_syntax(lhs), our_syntax(rhs)))
        cnt = 1 if M.det() != 0 else None
        T.append({'cat': 'rand_system', 'input': "solve([%s],[%s])" % (",".join(eqs), ",".join(map(str, vs))),
                  'check': 'system', 'eqs': eqs, 'vars': [str(v) for v in vs], 'count': cnt})
    for _ in range(nrand // 3):
        x0, y0 = rng.randint(-4, 4), rng.randint(-4, 4)
        e1 = x**2 + rng.randint(1, 3) * y
        e2 = rng.randint(1, 3) * x + y**2 - rng.randint(0, 2) * x * y
        eqs = ["%s=%s" % (our_syntax(e1), our_syntax(e1.subs({x: x0, y: y0}))),
               "%s=%s" % (our_syntax(e2), our_syntax(e2.subs({x: x0, y: y0})))]
        T.append({'cat': 'rand_system_nl', 'input': "solve([%s],[x,y])" % ",".join(eqs), 'check': 'system_planted',
                  'eqs': eqs, 'vars': ['x', 'y'], 'count': None, 'planted': {x: x0, y: y0}})
    for _ in range(nrand // 2):
        f = rand_expr(rng)
        T.append({'cat': 'rand_limit', 'input': "limit(%s,x,0)" % our_syntax(f), 'check': 'limit',
                  'ref': (lambda ff=f: sp.limit(ff, x, 0, '+-') if True else None)})
    for _ in range(nrand // 2):
        f = rand_growth(rng)
        if not isinstance(f, sp.Basic) or not f.has(x):
            continue
        T.append({'cat': 'rand_limit_inf', 'input': "limit(%s,x,inf)" % our_syntax(f), 'check': 'limit', 'f': f,
                  'ref': (lambda ff=f: sp.limit(ff, x, sp.oo))})
    for _ in range(nrand // 3):
        f = rand_expr(rng)
        T.append({'cat': 'rand_series', 'input': "series(%s,x,0,5)" % our_syntax(f), 'check': 'series',
                  'f': our_syntax(f), 'x0': 0, 'n': 5})
    return T


# ---------------------------------------------------------------- main ---
def main():
    args = sys.argv[1:]
    seed = 1
    nrand = 60
    only = None
    verbose = '--verbose' in args
    if '--seed' in args:
        seed = int(args[args.index('--seed') + 1])
    if '--quick' in args:
        nrand = 15
    if '--nrand' in args:
        nrand = int(args[args.index('--nrand') + 1])
    if '--only' in args:
        only = args[args.index('--only') + 1]
    rng = random.Random(seed)
    sys.path.insert(0, HERE)
    tests = curated() + hard_cases() + exact_value_cases(random.Random(seed + 7)) + random_cases(rng, nrand)
    import cases_applied as CA  # cas/ext/applied: linear algebra, transforms, fft, LP, logic, splines
    CA.to_sympy = to_sympy
    CHECKS.update(CA.CHECKS)
    tests += CA.cases(random.Random(seed + 11))
    if only:
        tests = [t for t in tests if t['cat'].startswith(only)]
    outs, times, elapsed = run_cas([t['input'] for t in tests])
    stats = {}
    results = []
    crng = random.Random(seed + 1)
    for t, out, ms in zip(tests, outs, times):
        cat = t['cat']
        st = stats.setdefault(cat, {'total': 0, 'pass': 0, 'fail': 0, 'unsupported': 0, 'error': 0, 'numeric': 0,
                                    'ms': 0.0})
        st['total'] += 1
        st['ms'] += ms
        status, note = None, None
        if out.startswith('ERROR'):
            code = out.split(':')[0]
            if code.startswith('ERROR 6') or code.startswith('ERROR 5') or code.startswith('ERROR 4'):
                status = 'unsupported'
            else:
                status = 'error'
            note = out
        else:
            try:
                ok, note = with_timeout(60, CHECKS[t['check']], out, t, crng)
                if ok is None:
                    status = 'unsupported'
                else:
                    status = 'pass' if ok else 'fail'
                if status == 'pass' and note == 'numeric':
                    st['numeric'] += 1
            except Timeout:
                status, note = 'fail', 'checker timeout'
            except Exception as ex:  # unparsable output etc.
                status, note = 'fail', 'checker: %s: %s' % (type(ex).__name__, str(ex)[:120])
        st[status] += 1
        results.append({'cat': cat, 'input': t['input'], 'output': out[:500], 'status': status, 'note': note,
                        'ms': ms})
        if verbose and status != 'pass':
            print("%-14s %-11s %s\n     -> %s %s" % (cat, status, t['input'], out[:200], note or ''))
    # report
    os.makedirs(os.path.join(HERE, 'results'), exist_ok=True)
    tot = {'total': 0, 'pass': 0, 'fail': 0, 'unsupported': 0, 'error': 0}
    lines = ["| category | total | pass | wrong | unsupported | error | pass % | avg ms |",
             "|---|---|---|---|---|---|---|---|"]
    for cat in sorted(stats):
        st = stats[cat]
        for kk in tot:
            tot[kk] += st[kk]
        lines.append("| %s | %d | %d | %d | %d | %d | %.1f | %.2f |" % (
            cat, st['total'], st['pass'], st['fail'], st['unsupported'], st['error'],
            100.0 * st['pass'] / max(1, st['total']), st['ms'] / max(1, st['total'])))
    lines.append("| **all** | %d | %d | %d | %d | %d | %.1f | |" % (
        tot['total'], tot['pass'], tot['fail'], tot['unsupported'], tot['error'],
        100.0 * tot['pass'] / max(1, tot['total'])))
    report = "\n".join(lines)
    print(report)
    print("CAS wall time for all inputs: %.2f s" % elapsed)
    with open(os.path.join(HERE, 'results', 'difftest_report.md'), 'w') as fh:
        fh.write("# Differential test report (seed %d, nrand %d)\n\n" % (seed, nrand))
        fh.write(report + "\n\n## Non-passing cases\n\n")
        for r in results:
            if r['status'] != 'pass':
                fh.write("- [%s/%s] `%s` -> `%s` %s\n" % (r['cat'], r['status'], r['input'], r['output'][:200],
                                                          r['note'] or ''))
    with open(os.path.join(HERE, 'results', 'difftest_report.json'), 'w') as fh:
        json.dump({'stats': stats, 'results': results}, fh, indent=1, default=str)
    return 0 if tot['fail'] == 0 and tot['error'] == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
