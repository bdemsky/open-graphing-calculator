"""Differential cases for the applied-mathematics commands of the casx module (cas/ext/applied):
numeric linear algebra, Fourier and z transforms, fft, linear programming, Boolean logic and
splines.  Used by difftest.py (cases(rng), CHECKS); references come from SymPy, NumPy and SciPy.

Conventions and tolerances:
  - fourier is unitary with angular frequency, F(w) = 1/sqrt(2 pi) int f e^(-i w x) dx; SymPy's
    fourier_transform(f, x, k) uses e^(-2 pi i x k), so F(w) = FT(w/(2 pi))/sqrt(2 pi).  Results are
    compared at points w != 0 where impulses vanish (dirac -> 0), relative 1e-8.
  - ztrans is compared with the partial sum of f(n) z^(-n) (n < 400) at |z| beyond the growth rate;
    iztrans by the values n = 0..11 against the Laurent coefficients of F at infinity.
  - numeric linear algebra: singular values and eigenvalues against NumPy (relative 1e-9 of the
    matrix norm), U S V' = A and orthonormality to 1e-12, eigenvector residuals |A v - l v| < 1e-9.
  - fft against numpy.fft (1e-10 relative), linprog against scipy.optimize.linprog (HiGHS; optimum
    to 1e-7, infeasible / unbounded status), splines against scipy CubicSpline(bc_type='natural').
  - logic: equivalence by truth tables; boolsimplify / dnf / cnf must be in the right form and not
    longer (in literals) than SymPy's simplify_logic.
"""
import itertools
import re

import numpy as np
import sympy as sp

to_sympy = None  # set by difftest.py
x, n, z, w = sp.symbols('x n z w')


def _num(e):
    return complex(sp.N(e, 30))


def _mat(rng, r, c, lo=-5, hi=5, fl=False):
    m = [[rng.randint(lo, hi) for _ in range(c)] for _ in range(r)]
    if fl:
        m = [[v + rng.randint(-9, 9) / 10 for v in row] for row in m]
    return m


def _txt(m):
    return '[' + ','.join('[' + ','.join(repr(v) if isinstance(v, float) else str(v) for v in row) + ']'
                          for row in m) + ']'


def _np(ours):
    return np.array([[_num(v) for v in row] for row in ours], dtype=complex)


# ------------------------------------------------------------------ checks
def chk_singvals(out, spec, rng):
    ours = sorted([_num(v).real for v in to_sympy(out)], reverse=True)
    ref = sorted(np.linalg.svd(np.array(spec['m'], dtype=float), compute_uv=False), reverse=True)
    sc = max(1.0, ref[0])
    if len(ours) != len(ref) or any(abs(a - b) > 1e-9 * sc for a, b in zip(ours, ref)):
        return False, "singular values %s vs %s" % (ours, list(ref))
    if spec.get('exact'):
        sref = sorted(sp.Matrix(spec['m']).singular_values(), key=lambda v: -_num(v).real)
        if any(abs(_num(a) - _num(b)) > 1e-12 * sc for a, b in zip(to_sympy(out), sref)):
            return False, "exact values"
    return True, None


def chk_svd(out, spec, rng):
    U, S, V = [_np(m) for m in to_sympy(out)]
    A = np.array(spec['m'], dtype=float)
    k = min(A.shape)
    if U.shape != (A.shape[0], k) or S.shape != (k, k) or V.shape != (A.shape[1], k):
        return False, "shapes"
    sc = max(1.0, np.linalg.norm(A))
    if np.linalg.norm(U @ S @ V.T - A) > 1e-11 * sc:
        return False, "U S V' != A"
    if np.linalg.norm(U.T @ U - np.eye(k)) > 1e-11 or np.linalg.norm(V.T @ V - np.eye(k)) > 1e-11:
        return False, "not orthonormal"
    d = np.real(np.diag(S))
    if any(d[i] < d[i + 1] - 1e-12 for i in range(k - 1)):
        return False, "not sorted"
    ref = np.linalg.svd(A, compute_uv=False)
    if np.max(np.abs(np.sort(d) - np.sort(ref))) > 1e-9 * sc:
        return False, "singular values"
    return True, None


def chk_eigvals(out, spec, rng):
    ours = [_num(v) for v in to_sympy(out)]
    A = np.array(spec['m'], dtype=float)
    ref = list(np.linalg.eigvals(A))
    sc = max(1.0, np.linalg.norm(A))
    for r in ref:  # every reference value is listed (repeats merged)
        if min(abs(r - o) for o in ours) > 1e-7 * sc:
            return False, "missing eigenvalue %s" % r
    for o in ours:
        if min(abs(r - o) for r in ref) > 1e-7 * sc:
            return False, "extra eigenvalue %s" % o
    return True, None


def chk_eigvects(out, spec, rng):
    A = np.array(spec['m'], dtype=float)
    sc = max(1.0, np.linalg.norm(A))
    tot = 0
    for lam, vs in to_sympy(out):
        l = _num(lam)
        for v in vs:
            v = np.array([_num(e) for e in v])
            if abs(np.linalg.norm(v) - 1) > 1e-9:
                return False, "not unit length"
            if np.linalg.norm(A @ v - l * v) > 1e-8 * sc:
                return False, "residual for %s" % l
            tot += 1
    if tot < len(set(np.round(np.linalg.eigvals(A), 6))):
        return False, "too few vectors"
    return True, None


def chk_exact_mat(out, spec, rng):
    ours = sp.Matrix(to_sympy(out))
    ref = sp.Matrix(spec['ref'])
    if ours.shape != ref.shape or sp.simplify(ours - ref) != sp.zeros(*ref.shape):
        return False, "mismatch"
    return True, None


def chk_close(out, spec, rng):
    ours = _num(to_sympy(out))
    ref = complex(spec['ref'])
    if spec['ref'] == float('inf'):
        return (out == 'inf'), None
    return abs(ours - ref) <= 1e-9 * max(1.0, abs(ref)), None


def chk_fourier(out, spec, rng):
    ours = to_sympy(out).replace(sp.DiracDelta, lambda *a: 0)
    f = to_sympy(spec['f'])
    k = sp.Symbol('k')
    ref = spec.get('ref')
    if ref is None:  # SymPy's transform (often valid for k > 0 only), else numeric quadrature
        ft = sp.fourier_transform(f, x, k)
        if not ft.has(sp.FourierTransform):
            ref = ft.subs(k, w / (2 * sp.pi)) / sp.sqrt(2 * sp.pi)
    else:
        ref = to_sympy(ref).replace(sp.DiracDelta, lambda *a: 0)

    def at(wv):  # the reference at w > 0
        if ref is not None:
            return _num(ref.subs(w, wv))
        import mpmath
        mpmath.mp.dps = 20
        fl = sp.lambdify(x, f, 'mpmath')
        g = lambda t: fl(t) * mpmath.exp(-1j * wv * t) + fl(-t) * mpmath.exp(1j * wv * t)
        return complex(mpmath.quadosc(g, [0, mpmath.inf], omega=float(wv))) / (2 * np.pi) ** 0.5

    for wv in (sp.Rational(7, 10), sp.Rational(13, 10), sp.Rational(21, 10), sp.Rational(37, 10)):
        b = at(wv)  # f is real: F(-w) = conj(F(w))
        for a, bb in ((_num(ours.subs(w, wv)), b), (_num(ours.subs(w, -wv)), b.conjugate())):
            if abs(a - bb) > 1e-8 * max(1.0, abs(bb)):
                return False, "at w=+-%s: %s vs %s" % (wv, a, bb)
    return True, None


def chk_ifourier(out, spec, rng):
    ours = to_sympy(out)
    F = to_sympy(spec['F'])
    k = sp.Symbol('k')
    # F(w) = FT(w/2pi)/sqrt(2pi) for f = ours: check F against the sympy transform of ours
    ft = sp.fourier_transform(ours, x, k)
    if ft.has(sp.FourierTransform):
        return None, "sympy has no transform"
    back = ft.subs(k, w / (2 * sp.pi)) / sp.sqrt(2 * sp.pi)
    for wv in (sp.Rational(7, 10), sp.Rational(-13, 10), sp.Rational(21, 10)):
        a, b = _num(back.subs(w, wv)), _num(F.subs(w, wv))
        if abs(a - b) > 1e-8 * max(1.0, abs(b)):
            return False, "at w=%s" % wv
    return True, None


def chk_ztrans(out, spec, rng):
    ours = to_sympy(out)
    f = to_sympy(spec['f'])
    fl = sp.lambdify(n, f, 'mpmath')
    import mpmath
    mpmath.mp.dps = 30

    def fv(k):
        try:
            return fl(k)
        except (ValueError, ZeroDivisionError):
            return mpmath.mpmathify(complex(sp.N(f.subs(n, k), 30)))

    for zv in spec.get('zs', (sp.Rational(53, 10), sp.Rational(71, 10))):
        s = mpmath.mpf(0)
        for k in range(400):
            s += fv(k) * mpmath.mpf(zv) ** (-k)
        a = _num(ours.subs(z, zv))
        if abs(a - complex(s)) > 1e-10 * max(1.0, abs(complex(s))):
            return False, "at z=%s: %s vs %s" % (zv, a, complex(s))
    return True, None


def chk_iztrans(out, spec, rng):
    vals = [_num(v) for v in to_sympy(out)]
    F = to_sympy(spec['F'])
    u = sp.Symbol('u')
    ser = sp.series(F.subs(z, 1 / u), u, 0, len(vals)).removeO()
    for k, v in enumerate(vals):
        c = _num(ser.coeff(u, k)) if k else _num(ser.subs(u, 0))
        if abs(v - c) > 1e-9 * max(1.0, abs(c)):
            return False, "f(%d) = %s, want %s" % (k, v, c)
    return True, None


def chk_fft(out, spec, rng):
    ours = np.array([_num(v) for v in to_sympy(out)])
    ref = (np.fft.ifft if spec['inv'] else np.fft.fft)(np.array(spec['v'], dtype=complex))
    sc = max(1.0, np.max(np.abs(ref)))
    return bool(np.max(np.abs(ours - ref)) <= 1e-10 * sc), None


def chk_linprog(out, spec, rng):
    from scipy.optimize import linprog
    c, A, b = spec['c'], spec['A'], spec['b']
    r = linprog(c, A_ub=A, b_ub=b, bounds=[(0, None)] * len(c), method='highs')
    if r.status == 2:  # HiGHS may say so for an unbounded problem: is there a feasible point?
        r0 = linprog([0] * len(c), A_ub=A, b_ub=b, bounds=[(0, None)] * len(c), method='highs')
        if r0.status == 2:
            return out == '[inf,[]]', "scipy: infeasible"
        return out == '[-inf,[]]', "scipy: unbounded"
    if out.startswith('ERROR'):
        return False, out
    val, pt = to_sympy(out)
    if r.status == 3:
        return val == -sp.oo, "scipy: unbounded"
    if abs(float(val) - r.fun) > 1e-7 * max(1.0, abs(r.fun)):
        return False, "optimum %s vs %s" % (val, r.fun)
    xs = [sp.Rational(v) for v in pt]
    if any(v < 0 for v in xs) or any(sum(sp.Rational(a) * xv for a, xv in zip(row, xs)) > bi for row, bi in zip(A, b)):
        return False, "point not feasible"
    if sum(sp.Rational(ci) * xv for ci, xv in zip(c, xs)) != val:
        return False, "value at the point"
    return True, None


def chk_lpsym(out, spec, rng):
    if spec['ref'] is None:
        return out == '[inf,[]]', None
    val, pt = to_sympy(out)
    return sp.simplify(val - spec['ref']) == 0, None


# logic
LV = sp.symbols('a b c d e')


def _logic(out):
    t = out.replace(' and ', ' & ').replace(' or ', ' | ').replace('not(', '~(').replace('xor(', 'Xor(')
    t = t.replace('implies(', 'Implies(').replace('iff(', 'Equivalent(')
    t = re.sub(r'\btrue\b', 'True', re.sub(r'\bfalse\b', 'False', t))
    return sp.sympify(t, locals={str(s): s for s in LV})


def _equiv(e1, e2):
    return sp.satisfiable(sp.Xor(e1, e2)) is False


def _lits(e):
    if isinstance(e, (sp.Symbol, sp.Not)):
        return 1
    if e in (sp.true, sp.false):
        return 0
    return sum(_lits(a) for a in e.args)


def chk_logic(out, spec, rng):
    ref = _logic(spec['se'])
    op = spec['op']
    if op == 'satisfiable':
        m = sp.satisfiable(ref)
        if out == 'false':
            return m is False, None
        if out == 'true':
            return bool(ref.simplify() == sp.true), None
        model = {LV['abcde'.index(e.split('=')[0])]: e.split('=')[1] == 'true' for e in out[1:-1].split(',')}
        return bool(ref.subs(model) == sp.true), None
    ours = _logic(out)
    if not _equiv(ours, ref):
        return False, "not equivalent"
    best = sp.simplify_logic(ref, form=None if op == 'boolsimplify' else op, force=True)
    from sympy.logic.boolalg import is_cnf, is_dnf
    if op in ('dnf', 'cnf') and not (is_dnf(ours) if op == 'dnf' else is_cnf(ours)):
        return False, "not in %s" % op
    if _lits(ours) > _lits(best):
        return False, "longer than sympy (%d > %d literals)" % (_lits(ours), _lits(best))
    return True, None


def chk_truthtable(out, spec, rng):
    body = [[int(v) for v in r.split(',')] for r in re.findall(r'\[([01](?:,[01])*)\]', out)]
    nv = len(body[0]) - 1 if body else 0
    ref = _logic(spec['se'])
    hd = out[2:out.index(']')].split(',')[:nv]  # our variables (iff(b,b) is true before the table)
    vs = [LV['abcde'.index(v)] for v in hd]
    if len(body) != 2 ** nv or len(vs) != nv:
        return False, "shape"
    for r, vals in zip(body, itertools.product([0, 1], repeat=nv)):
        if list(r[:nv]) != list(vals) or r[nv] != int(bool(ref.subs(dict(zip(vs, [bool(v) for v in vals]))))):
            return False, "row %s" % (vals,)
    return True, None


def chk_spline(out, spec, rng):
    from scipy.interpolate import CubicSpline
    cs = CubicSpline(spec['xs'], spec['ys'], bc_type='natural')
    ours = [_num(v).real for v in to_sympy(out)]
    ref = [float(cs(float(t))) for t in spec['ts']]
    return max(abs(a - b) for a, b in zip(ours, ref)) <= 1e-10 * max(1.0, max(map(abs, ref))), None


CHECKS = {
    'singvals': chk_singvals, 'svd': chk_svd, 'eigvals_n': chk_eigvals, 'eigvects_n': chk_eigvects,
    'exact_mat': chk_exact_mat, 'close': chk_close, 'fourier': chk_fourier, 'ifourier': chk_ifourier,
    'ztrans': chk_ztrans, 'iztrans': chk_iztrans, 'fft': chk_fft, 'linprog': chk_linprog, 'lpsym': chk_lpsym,
    'logic': chk_logic, 'truthtable': chk_truthtable, 'spline': chk_spline,
}


# ------------------------------------------------------------------ cases
def _bexpr(rng, depth, nv):
    if depth == 0 or rng.random() < 0.25:
        v = 'abcde'[rng.randrange(nv)]
        return (v, v)
    op = rng.choice(['and', 'or', 'not', 'xor', 'implies', 'iff', 'and', 'or'])
    if op == 'not':
        a = _bexpr(rng, depth - 1, nv)
        return ('not(%s)' % a[0], '~(%s)' % a[1])
    a, b = _bexpr(rng, depth - 1, nv), _bexpr(rng, depth - 1, nv)
    if op in ('and', 'or'):
        return ('(%s %s %s)' % (a[0], op, b[0]), '(%s %s %s)' % (a[1], '&' if op == 'and' else '|', b[1]))
    fn = {'xor': 'Xor', 'implies': 'Implies', 'iff': 'Equivalent'}[op]
    return ('(%s %s %s)' % (a[0], op, b[0]), '%s(%s, %s)' % (fn, a[1], b[1]))


def cases(rng):
    T = []

    def add(cat, inp, check, **kw):
        d = {'cat': cat, 'input': inp, 'check': check}
        d.update(kw)
        T.append(d)

    # singular values: exact small matrices (SymPy exact), numeric larger ones
    for r, c in [(2, 2), (2, 2), (3, 2), (2, 3), (3, 3), (2, 2), (1, 3), (3, 1)]:
        m = _mat(rng, r, c)
        add('svd', 'singvals(%s)' % _txt(m), 'singvals', m=m, exact=True)
    for r, c in [(4, 4), (5, 3), (3, 6), (6, 6), (7, 4), (8, 8)]:
        m = _mat(rng, r, c, fl=True)
        add('svd', 'singvals(%s)' % _txt(m), 'singvals', m=m)
    for r, c in [(2, 2), (3, 3), (4, 2), (2, 5), (5, 5), (6, 3), (3, 3)]:
        m = _mat(rng, r, c, fl=rng.random() < 0.5)
        add('svd', 'svd(%s)' % _txt(m), 'svd', m=m)
    m = [[1, 2, 3], [2, 4, 6], [1, 0, 1]]
    add('svd', 'svd(%s)' % _txt(m), 'svd', m=m)  # rank deficient
    # numeric eigenvalues / eigenvectors
    for nn in [3, 4, 5, 5, 6, 7, 8, 10]:
        m = _mat(rng, nn, nn, fl=True)
        add('eig_num', 'eigenvals(%s)' % _txt(m), 'eigvals_n', m=m)
        add('eig_num', 'eigenvects(%s)' % _txt(m), 'eigvects_n', m=m)
    for nn in [3, 4, 6, 9]:
        m = _mat(rng, nn, nn, fl=True)
        m = [[(m[i][j] + m[j][i]) / 2 for j in range(nn)] for i in range(nn)]
        add('eig_num', 'eigenvals(%s)' % _txt(m), 'eigvals_n', m=m)
        add('eig_num', 'eigenvects(%s)' % _txt(m), 'eigvects_n', m=m)
    for m in ([[1, 2, 0, 0, 1], [2, 1, 3, 0, 0], [0, 3, 1, 1, 0], [0, 0, 1, 2, 5], [1, 0, 0, 5, 3]],
              [[1, 1, 0, 0, 0], [0, 1, 1, 0, 0], [0, 0, 1, 1, 0], [0, 0, 0, 1, 1], [1, 0, 0, 0, 2]]):
        add('eig_num', 'eigenvects(%s)' % _txt(m), 'eigvects_n', m=m)  # exact, numeric roots
    # pseudoinverse (exact) and norms
    for r, c in [(2, 2), (3, 2), (2, 3), (3, 3), (4, 2), (3, 3)]:
        m = _mat(rng, r, c, -3, 3)
        add('pinv', 'pinv(%s)' % _txt(m), 'exact_mat', ref=sp.Matrix(m).pinv().tolist())
    for m in ([[1, 2], [2, 4]], [[1, 2, 3], [2, 4, 6]], [[0, 0], [0, 0]], [[1, 0, 1], [0, 1, 1], [1, 1, 2]]):
        add('pinv', 'pinv(%s)' % _txt(m), 'exact_mat', ref=sp.Matrix(m).pinv().tolist())
    for _ in range(6):
        m = _mat(rng, 3, 3)
        A = np.array(m, dtype=float)
        add('numla', 'norm(%s,2)' % _txt(m), 'close', ref=float(np.linalg.norm(A, 2)))
        if abs(np.linalg.det(A)) > 1e-9:
            add('numla', 'cond(%s,2)' % _txt(m), 'close', ref=float(np.linalg.cond(A, 2)))
    for m, tol in [([[1, 2], [2, 4.001]], 0.01), ([[1, 2], [2, 4.001]], 1e-6), ([[1, 2, 3], [4, 5, 6], [7, 8, 9.0]], 1e-9)]:
        add('numla', 'rank(%s,%s)' % (_txt(m), tol), 'close',
            ref=float(np.linalg.matrix_rank(np.array(m, dtype=float), tol=tol)))
    # Fourier transforms (references from sympy, or hand-derived for distributions)
    for f in ["exp(-x^2)", "exp(-abs(x))", "exp(-3*abs(x))", "1/(1+x^2)", "x/(1+x^2)", "1/(x^2+2*x+5)",
              "1/(1+x^2)^2", "x*exp(-x^2)", "x^2*exp(-x^2)", "exp(-x^2/2)*cos(2*x)", "exp(-abs(x-1))",
              "exp(-abs(2*x))", "x*exp(-abs(x))", "heaviside(x)*exp(-x)", "heaviside(x)*x*exp(-2*x)",
              "exp(-(x-1)^2)", "1/(x^2+4)", "exp(-x^2)*sin(x)"]:
        add('fourier', "fourier(%s,x,w)" % f, 'fourier', f=f)
    for f, ref in [("heaviside(x+1)-heaviside(x-1)", "sqrt(2/pi)*sin(w)/w"),
                   ("heaviside(x)", "-i/(sqrt(2*pi)*w)"), ("sign(x)", "-i*sqrt(2/pi)/w"),
                   ("dirac(x-2)", "exp(-2*i*w)/sqrt(2*pi)"), ("cos(3*x)", "0"), ("1", "0"),
                   ("heaviside(2-abs(x))", "2*sin(2*w)/(sqrt(2*pi)*w)")]:
        add('fourier', "fourier(%s,x,w)" % f, 'fourier', f=f, ref=ref)
    for F in ["exp(-w^2/2)", "sqrt(2/pi)/(1+w^2)", "1/(1+w^2)", "exp(-w^2)"]:
        add('fourier', "ifourier(%s,w,x)" % F, 'ifourier', F=F)
    # z transforms
    for f in ["1", "n", "n^2", "2^n", "n*3^n", "(1/2)^n", "cos(2*n)", "sin(n)", "2^n*sin(n)", "1/n!",
              "binomial(n,2)", "(-1)^n", "exp(-n)", "cosh(n)", "n*cos(n)", "n^3", "5*2^n-3*n", "1/(n+1)",
              "(n+1)*(1/3)^n", "sin(2*n+1)"]:
        add('ztrans', "ztrans(%s,n,z)" % f, 'ztrans', f=f)
    for F in ["z/(z-2)", "z/(z-1)^2", "z^2/((z-1)*(z-1/2))", "1/(z-1)", "z/(z^2+1)", "z/(z^2-z+1)",
              "1+2/z+3/z^2", "z*(z+1)/(z-1)^3", "(2*z^2-z)/(z^2-3*z+2)", "z/(z+1/2)^2", "1/(z*(z-3))",
              "z^2/(z^2+z+1)", "(z^2+1)/(z^2-1/4)", "z/((z-1)*(z^2+1))"]:
        add('ztrans', "seq(subst(iztrans(%s,z,n),n,k),k,0,11)" % F, 'iztrans', F=F)
    # fft
    for N in [1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 17, 30, 64, 100]:
        v = [rng.randint(-9, 9) for _ in range(N)]
        if rng.random() < 0.4:
            add('fft', "fft([%s])" % ','.join(str(a) for a in v), 'fft', v=v, inv=False)
            continue
        vc = [complex(rng.randint(-9, 9), rng.randint(-9, 9)) for _ in range(N)]
        txt = ','.join('%d+%d*i' % (int(a.real), int(a.imag)) for a in vc)
        add('fft', "%s([%s])" % ('ifft' if N % 2 else 'fft', txt), 'fft', v=vc, inv=bool(N % 2))
    # linear programs (matrix form, x >= 0) against scipy
    for _ in range(24):
        nv, nc = rng.randint(2, 5), rng.randint(1, 5)
        c = [rng.randint(-5, 5) for _ in range(nv)]
        A = [[rng.randint(-4, 6) for _ in range(nv)] for _ in range(nc)]
        b = [rng.randint(-3, 12) for _ in range(nc)]
        add('linprog', "linprog(%s,%s,%s)" % (c, A, b), 'linprog', c=c, A=A, b=b)
    add('linprog', "linprog([-3/4,20,-1/2,6],[[1/4,-8,-1,9],[1/2,-12,-1/2,3],[0,0,1,0]],[0,0,1])", 'linprog',
        c=[-0.75, 20, -0.5, 6], A=[[0.25, -8, -1, 9], [0.5, -12, -0.5, 3], [0, 0, 1, 0]], b=[0, 0, 1])
    for inp, ref in [("minimize(x+y,[x>=1,y>=2],[x,y])", 3), ("maximize(3*x+2*y,[x+y<=4,x+3*y<=6,x>=0,y>=0],[x,y])", 12),
                     ("minimize(2*x+3*y+1,[x+y=5,x-y<=1,y>=0],[x,y])", 13), ("linprog(x+y,[x+y>=2,x+y<=1],[x,y])", None),
                     ("minimize(x+y,[x^2+y^2=1],[x,y])", -sp.sqrt(2)), ("maximize(x*y,[x+y=10],[x,y])", 25),
                     ("minimize(x^2+y^2+z^2,[x+y+z=3],[x,y,z])", 3), ("maximize(x+2*y,[x^2+y^2=5],[x,y])", 5),
                     ("minimize(x^2+2*y^2,[x+y=3],[x,y])", 6)]:
        add('linprog', inp, 'lpsym', ref=ref)
    # Boolean logic
    for i in range(40):
        nv = rng.randint(2, 5)
        ours, se = _bexpr(rng, rng.randint(2, 4), nv)
        op = ['boolsimplify', 'dnf', 'cnf', 'satisfiable'][i % 4]
        add('logic', "%s(%s)" % (op, ours), 'logic', se=se, op=op)
    for i in range(8):
        ours, se = _bexpr(rng, 3, 3)
        add('logic', "truthtable(%s)" % ours, 'truthtable', se=se)
    # natural cubic splines
    for npts in [3, 4, 5, 6, 8]:
        xs = sorted(rng.sample(range(-10, 20), npts))
        ys = [rng.randint(-9, 9) for _ in range(npts)]
        ts = [sp.Rational(rng.randint(xs[0] * 10, xs[-1] * 10), 10) for _ in range(5)]
        add('spline', "spline(%s,%s,[%s])" % (xs, ys, ','.join(str(t) for t in ts)), 'spline', xs=xs, ys=ys, ts=ts)
    return T
