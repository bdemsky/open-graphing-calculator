"""SymPy (the .venv's SymPy) as a baseline engine.

Translation: each line of our syntax becomes Python/SymPy text that
sympy_worker.py evaluates with parse_expr in a namespace mapping each of our
commands to the SymPy function a SymPy user would call (sympy_worker.CMDS:
integrate -> sp.integrate, solve -> sp.solve on real symbols,
csolve -> sp.solve on complex symbols, dsolve -> sp.dsolve with ics,
laplace -> laplace_transform, ...).  Text-level rewrites done here:
  ^ -> **;  a=b -> Eq(a,b);  'and'/'or'/'not' -> And/Or/Not;
  [[..]] -> Matrix([[..]]);  dsolve(eqs,y,x): y'' -> Derivative(y(x),x,2),
  y -> y(x), initial conditions y(0)=a, y'(0)=b -> ics dict.
Symbols are real (as in our CAS and in the expected answers); assume(n,integer),
assume(x>0) and friends become symbol assumptions; relations between symbols
are passed to ask() for the truth-value (bool) problems.
A command with no SymPy counterpart makes the problem UNSUPPORTED.
"""
import json
import os
import re
import subprocess
import sys
import time

from syntax import rewrite_calls, split_top, call_names, top_equal, match_close

NAME = 'sympy'
HERE = os.path.dirname(os.path.abspath(__file__))
WORKER = os.path.join(HERE, 'sympy_worker.py')

import sympy_worker                                    # noqa: E402  (for the command table)

KNOWN = set(sympy_worker.CMDS) | {'not', 'assume', 'Eq', 'And', 'Or', 'Not', 'Matrix', 'dsolve'}

NOTES = {
    'solve': 'solve -> sp.solve on real symbols (real solutions); systems -> dict solutions; '
             'inequalities -> solve_univariate_inequality / reduce_inequalities',
    'csolve': 'csolve -> sp.solve with the unknowns made complex',
    'dsolve': 'dsolve -> sp.dsolve(eq, y(x), ics=...)',
    'normal': 'normal -> cancel(together(e))', 'tcollect': 'tcollect -> TR8(TR7(e)) (fu: products and powers to sums)',
    'cfactor': 'cfactor -> factor(e, gaussian=True)', 'series': 'series -> series(f,x,a,n).removeO()',
    'mean': 'list statistics via sympy.stats.DiscreteUniform', 'median': 'list statistics via sympy.stats',
    'stddev': 'population standard deviation (sympy.stats.std)', 'hex': "Python's '%X' format (SymPy has no base conversion)",
    'fourier_an': 'fourier_series(f,(x,a,a+T)).an, coefficient of cos(2 pi n x/T)',
    'fourier_bn': 'fourier_series(f,(x,a,a+T)).bn, coefficient of sin(2 pi n x/T)',
    'hypergeom': 'hyperexpand(hyper(a,b,z))', 'curl': 'sympy.vector curl in CoordSys3D',
    'potential': 'sympy.vector scalar_potential', 'smith': 'smith_normal_form over QQ[x]',
    'sinm': 'Matrix.analytic_func(sin)', 'expm': 'Matrix.exp()', 'rank': 'Matrix.rank() (default zero test)',
    'eigenvals': 'Matrix.eigenvals() keys', 'grad': 'derive_by_array(f, vars)',
    'implicitdiff': 'idiff', 'minimize': 'sympy.calculus.util.minimum', 'log': 'log(a) is log10: log(a,10)',
    'rsolve': 'rsolve(eq, f(n), {ics})', 'ichinrem': 'sympy.ntheory.modular.crt',
}


def _dsolve(args):
    eqs, fs, var = args[0].strip(), args[1].strip(), args[2].strip()
    funcs = [f.strip() for f in split_top(fs[1:-1])] if fs.startswith('[') else [fs]
    items = split_top(eqs[1:-1]) if eqs.startswith('[') else [eqs]
    odes, ics = [], []
    for it in items:
        m = re.match(r"^\s*([A-Za-z_]\w*)('*)\((.+)\)\s*=\s*(.+)$", it)
        if m and m.group(1) in funcs and m.group(3).strip() != var:
            key = '%s(%s)' % (m.group(1), var)
            if m.group(2):
                key += '.diff(%s,%d)' % (var, len(m.group(2)))
            key += '.subs(%s,%s)' % (var, conv(m.group(3)))
            ics.append('%s:%s' % (key, conv(m.group(4))))
            continue
        e = it
        for f in funcs:
            e = re.sub(r"\b%s''" % f, 'Derivative(%s(%s),%s,2)' % (f, var, var), e)
            e = re.sub(r"\b%s'" % f, 'Derivative(%s(%s),%s)' % (f, var, var), e)
            e = re.sub(r'\b%s\b(?!\()' % f, '%s(%s)' % (f, var), e)
        odes.append(conv(e))
    return 'dsolve_([%s],{%s},[%s],%s)' % (','.join(odes), ','.join(ics),
                                            ','.join('%s(%s)' % (f, var) for f in funcs), var)


def conv(s):
    """our expression text -> SymPy text (without the ^ -> ** step)"""
    s = s.strip()
    parts = split_top(s, ' or ')
    if len(parts) > 1:
        return 'Or(%s)' % ','.join(conv(p) for p in parts)
    parts = split_top(s, ' and ')
    if len(parts) > 1:
        return 'And(%s)' % ','.join(conv(p) for p in parts)
    eq = top_equal(s)
    if eq:
        return 'Eq(%s,%s)' % (conv(eq[0]), conv(eq[1]))
    out, i = '', 0
    while i < len(s):
        ch = s[i]
        if ch in '([':
            j = match_close(s, i)
            inner = s[i + 1:j]
            items = [conv(p) for p in split_top(inner)] if inner.strip() else []
            grp = ch + ','.join(items) + (')' if ch == '(' else ']')
            if ch == '[' and items and items[0].startswith('[') and (not out or not (out[-1].isalnum() or out[-1] in '_)]')):
                grp = 'Matrix(%s)' % grp
            out += grp
            i = j + 1
        else:
            out += ch
            i += 1
    return out


def translate_line(line):
    line = rewrite_calls(line, lambda nm, a: _dsolve(a) if nm == 'dsolve' and len(a) == 3 else None)
    m = re.match(r'^\s*([A-Za-z_]\w*\s*(\([^()]*\))?)\s*:=\s*(.*)$', line)
    if m:
        out = '%s := %s' % (m.group(1), conv(m.group(3)))
    else:
        out = conv(line)
    out = re.sub(r'\bnot\(', 'Not(', out)
    return out.replace('^', '**')


def translate(prob, ov):
    if ov.get('in') is not None:
        return {'lines': ov.get('pre', []) + [ov['in']], 'notes': ['override'], 'unsupported': None}
    src = prob['pre'] + [prob['in']]
    userdef = set()
    for l in src:
        m = re.match(r'^\s*([A-Za-z_]\w*)\s*\(.*?\)\s*:=', l)
        if m:
            userdef.add(m.group(1))
    used = {nm for l in src for nm in call_names(l)}
    odef = set()
    for l in src:
        for m in re.finditer(r'dsolve\(.*,\s*\[?([a-z,\s]+)\]?\s*,\s*[a-z]\s*\)$', l):
            odef |= {f.strip() for f in m.group(1).split(',')}
        if 'rsolve(' in l:
            odef.add('f')
    missing = sorted(used - KNOWN - userdef - set(prob['allow']) - odef)
    missing = [m for m in missing if len(m) > 1]
    lines = [translate_line(l) for l in src]
    notes = sorted({NOTES[nm] for nm in used if nm in NOTES})
    if missing:
        return {'lines': lines, 'notes': notes, 'unsupported': 'no SymPy counterpart for ' + ','.join(missing)}
    return {'lines': lines, 'notes': notes, 'unsupported': None}


def available():
    try:
        import sympy
        return True, 'SymPy %s (Python %s)' % (sympy.__version__, sys.version.split()[0])
    except Exception as ex:
        return False, str(ex)


def run(trans, prob, timeout):
    t0 = time.time()
    job = {'lines': trans['lines'], 'ask': prob.get('check', 'exact') == 'bool'}
    try:
        p = subprocess.run([sys.executable, WORKER], input=json.dumps(job), capture_output=True, text=True,
                           timeout=timeout, cwd=HERE)
    except subprocess.TimeoutExpired:
        return {'status': 'timeout', 'time': time.time() - t0}
    res = {'time': time.time() - t0, 'echo': prob.get('in') or ''}
    try:
        r = json.loads(p.stdout.strip().split('\n')[-1])
    except Exception:
        res.update(status='crash', msg='rc=%s %s' % (p.returncode, p.stderr.strip()[-300:]))
        return res
    res.update(status=r['status'], msg=r.get('msg', ''))
    if r['status'] == 'ok':
        res['out'] = r['out']
        res['raw'] = r['out']
    else:
        res['raw'] = r.get('msg', '')
    return res
