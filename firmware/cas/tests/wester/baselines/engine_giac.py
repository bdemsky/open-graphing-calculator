"""Giac (the CAS of the HP Prime and of Xcas) driven through its icas
command-line front end.

Translation: this CAS's syntax is close to Giac's (both use ^, ln, i, e, pi,
inf, [..] lists, [[..]] matrices, := definitions, piecewise, y' in desolve), so
most inputs pass through unchanged.  The table below renames the functions
whose Giac names differ and rewrites the calls whose argument order or
semantics differ.  Anything that still fails is fixed per problem in
giac/overrides.txt.

Giac is located through $GIAC (path to icas), then PATH (icas, giac), then
<repo>/tools/giac/usr/bin/icas (an unpacked Xcas payload), then the standard
Xcas install /Applications/usr/bin/icas.  XCAS_ROOT is derived
from the binary location.  The macOS Xcas build links readline 8.3, which it
does not ship; MacPorts' /opt/local/lib is added to DYLD_FALLBACK_LIBRARY_PATH.
"""
import os
import re
import shutil
import subprocess
import time

from syntax import rewrite_calls, split_top, call_names, replace_call_terms, match_close

NAME = 'giac'
# repo root: tools/giac holds an unpacked Xcas payload (gitignored, no admin install)
_REPO = os.path.realpath(os.path.join(os.path.dirname(__file__), '../../../../..'))

# our name -> Giac name (same arguments)
RENAME = {
    'approx': 'evalf', 'minpoly': 'pmin', 'primroot': 'znprimroot', 'chebyshevt': 'tchebyshev1',
    'digamma': 'Psi', 'zeta': 'Zeta', 'besselj': 'BesselJ', 'heaviside': 'Heaviside', 'dirac': 'Dirac',
    'gamma': 'Gamma', 'nint': 'romberg', 'trigsimp': 'trigsimplify', 'expm': 'exp', 'sinm': 'sin',
    'mod': 'irem', 'linreg': 'linear_regression', 'linsolve': 'solve',
}

NOTES = {
    'log': 'log(a) is log10 here; Giac log is ln, so log(a) -> log10(a), log(a,b) -> logb(a,b)',
    'invmod': 'invmod(a,m) -> inv(a % m); the modular result is reduced to an integer',
    'digits': 'digits(n,b) -> revlist(convert(n,base,b))',
    'contfrac': 'contfrac(x,n) -> mid(dfc(evalf(x,60),n),0,n): dfc of an exact irrational may stop at a periodic tail',
    'xor': 'logical xor is an infix operator in Giac',
    'union': 'set[..] union set[..]', 'intersect': 'set[..] intersect set[..]', 'setdiff': 'set[..] minus set[..]',
    'dsolve': 'dsolve(eqs,y,x) -> desolve(eqs,x,y)',
    'jordan': 'jordan(M) returns [P,J]; J taken', 'smith': 'smith(M) returns [U,S,V]; S taken',
    'lu': 'lu(M) returns [p,L,U] with a permutation list p; reordered to [L,U,P]',
    'minor': 'minor(M,i,j) -> delcols(delrows(M,i-1),j-1)',
    'binompdf': 'binomial(n,k,p)', 'binomcdf': 'binomial_cdf(n,p,k)',
    'normalcdf': 'normal_cdf(mu,sigma,a,b)', 'tcdf': 'student_cdf(df,a,b)', 'chi2cdf': '1-chisquare_cdf(df,a) for b=inf',
    'pade': 'our pade(f,x,m,n) is the [m/n] approximant; Giac pade(f,x,m+n,n+1)',
    'eigenvects': 'built from jordan(M): [lambda_k, [column k of P]] (valid for diagonalizable M)',
    'norm': 'norm(M,inf) -> matrix_norm(M,inf)',
    'series': 'our series(f,x,a,n) drops O((x-a)^n); Giac series(f,x,a,n) keeps the (x-a)^n term, so n-1 '
              '(unchanged at x=inf, where Giac counts the order of 1/x differently)',
    'factor': 'factor over Q: the session starts with with_sqrt(0) (Giac otherwise factors over Q(sqrt))',
    'solve': 'a system solve(eqs,[vars]) returns value lists; turned into [var=value] lists, free var=var dropped',
}


def _lst(a):
    a = a.strip()
    return 'set' + a if a.startswith('[') else a


def _call(name, args):
    n = len(args)
    if name in RENAME:
        return '%s(%s)' % (RENAME[name], ','.join(args))
    if name == 'log':
        return 'log10(%s)' % args[0] if n == 1 else 'logb(%s,%s)' % (args[0], args[1])
    if name == 'invmod':
        return 'inv((%s) %% (%s))' % (args[0], args[1])
    if name == 'digits':
        return 'revlist(convert(%s,base,%s))' % (args[0], args[1])
    if name == 'contfrac':
        return 'mid(dfc(evalf(%s,60),%s),0,%s)' % (args[0], args[1], args[1])
    if name == 'xor':
        return '((%s) xor (%s))' % (args[0], args[1])
    if name in ('union', 'intersect', 'setdiff'):
        op = {'union': ' union ', 'intersect': ' intersect ', 'setdiff': ' minus '}[name]
        return '(' + op.join(_lst(a) for a in args) + ')'
    if name == 'dsolve' and n == 3:
        return 'desolve(%s,%s,%s)' % (args[0], args[2], args[1])
    if name == 'jordan':
        return 'jordan(%s)[1]' % args[0]
    if name == 'smith':
        return 'smith(%s)[1]' % args[0]
    if name == 'lu':
        m = args[0]
        return '[lu(%s)[1],lu(%s)[2],permu2mat(lu(%s)[0])]' % (m, m, m)
    if name == 'minor':
        return 'delcols(delrows(%s,%s-1),%s-1)' % (args[0], args[1], args[2])
    if name == 'binompdf':
        return 'binomial(%s,%s,%s)' % (args[0], args[2], args[1])
    if name == 'binomcdf':
        return 'binomial_cdf(%s,%s,%s)' % (args[0], args[1], args[2])
    if name == 'normalcdf':
        mu, sg = (args[2], args[3]) if n == 4 else ('0', '1')
        return 'normal_cdf(%s,%s,%s,%s)' % (mu, sg, args[0], args[1])
    if name == 'tcdf':
        return 'student_cdf(%s,%s,%s)' % (args[2], args[0], args[1])
    if name == 'chi2cdf':
        if args[1].strip() == 'inf':
            return '(1-chisquare_cdf(%s,%s))' % (args[2], args[0])
        return '(chisquare_cdf(%s,%s)-chisquare_cdf(%s,%s))' % (args[2], args[1], args[2], args[0])
    if name == 'pade':
        return 'pade(%s,%s,(%s)+(%s),(%s)+1)' % (args[0], args[1], args[2], args[3], args[3])
    if name == 'eigenvects':
        m = args[0]
        return 'seq([jordan(%s)[1][k,k],[col(jordan(%s)[0],k)]],k,0,rowdim(%s)-1)' % (m, m, m)
    if name == 'series' and n == 4 and args[2].strip() not in ('inf', '-inf'):
        return 'series(%s,%s,%s,(%s)-1)' % tuple(args)
    if name == 'norm' and n == 2:
        return 'matrix_norm(%s,%s)' % (args[0], args[1])
    return None


def translate_line(line):
    line = re.sub(r'(\[\[[^\^]*?\]\])\^\(1/2\)', r'sqrt(\1)', line)
    return rewrite_calls(line, _call)


def _solve_vars(line):
    m = re.match(r'^\s*(solve|csolve|linsolve)\((.*)\)\s*$', line)
    if not m:
        return None
    args = split_top(m.group(2))
    if len(args) == 2 and args[1].strip().startswith('['):
        return [v.strip() for v in split_top(args[1].strip()[1:-1])]
    return None


def translate(prob, ov):
    """-> dict(lines, notes, unsupported)"""
    if ov.get('in') is not None:
        lines = ov.get('pre', []) + [ov['in']]
        return {'lines': lines, 'notes': ['override'], 'unsupported': None, 'solvevars': _solve_vars(ov['in'])}
    src = prob['pre'] + [prob['in']]
    lines = [translate_line(l) for l in src]
    notes = sorted({NOTES[nm] for l in src for nm in call_names(l) if nm in NOTES})
    return {'lines': lines, 'notes': notes, 'unsupported': None, 'solvevars': _solve_vars(prob['in'])}


# ------------------------------------------------------------------ binary ---
def find_binary():
    cands = [os.environ.get('GIAC')] + [shutil.which(n) for n in ('icas', 'giac')] + \
            [os.path.join(_REPO, 'tools/giac/usr/bin/icas'),
             '/Applications/usr/bin/icas', os.path.expanduser('~/Applications/usr/bin/icas')]
    for c in cands:
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    return None


def _env(binpath):
    env = dict(os.environ)
    root = os.path.dirname(os.path.dirname(os.path.realpath(binpath)))
    if os.path.isdir(os.path.join(root, 'share', 'giac')):
        env.setdefault('XCAS_ROOT', root)
    fb = env.get('DYLD_FALLBACK_LIBRARY_PATH', '')
    env['DYLD_FALLBACK_LIBRARY_PATH'] = ':'.join(p for p in (fb, '/opt/local/lib', '/usr/local/lib', '/usr/lib') if p)
    return env


def available():
    b = find_binary()
    if not b:
        return False, 'Giac not found (set GIAC=/path/to/icas)'
    r = _session(b, ['version()'], 60)
    if r['status'] != 'ok':
        return False, 'Giac at %s does not start: %s' % (b, r.get('msg', r['status']))
    return True, '%s, icas command line' % r['blocks'][-1].strip().strip('"').split(',')[0]


PROMPT = re.compile(r'^(\d+)>> ', re.M)


def _session(binpath, lines, timeout):
    t0 = time.time()
    try:
        p = subprocess.run([binpath], input='\n'.join(lines) + '\n', capture_output=True, text=True,
                           errors='replace', env=_env(binpath), timeout=timeout)
    except subprocess.TimeoutExpired:
        return {'status': 'timeout', 'time': time.time() - t0}
    out = p.stdout
    ms = list(PROMPT.finditer(out))
    blocks = []
    for k in range(len(lines)):
        if k >= len(ms):
            break
        start = out.index('\n', ms[k].end()) + 1 if '\n' in out[ms[k].end():] else len(out)
        end = ms[k + 1].start() if k + 1 < len(ms) else len(out)
        blocks.append(out[start:end].rstrip('\n'))
    if p.returncode != 0 or len(blocks) < len(lines):
        return {'status': 'crash', 'msg': 'rc=%s, %d of %d outputs' % (p.returncode, len(blocks), len(lines)),
                'stderr': p.stderr[-2000:], 'time': time.time() - t0, 'blocks': blocks}
    return {'status': 'ok', 'blocks': blocks, 'stderr': p.stderr, 'time': time.time() - t0}


SKIP_TYPE = {'type', 'seq', 'col', 'rowdim', 'permu2mat', 'mid', 'dfc', 'evalf', 'revlist', 'convert'}


def run(trans, prob, timeout):
    b = find_binary()
    lines = list(trans['lines'])
    userdef = set()
    for l in lines:
        m = re.match(r'^\s*([A-Za-z_]\w*)\s*\(.*?\)\s*:=', l)
        if m:
            userdef.add(m.group(1))
    names = sorted({nm for l in lines for nm in call_names(l)} - userdef - set(prob['allow']) - SKIP_TYPE)
    names = [nm for nm in names if len(nm) > 1]
    probe = '[' + ','.join('type(%s)' % nm for nm in names) + ']' if names else '0'
    r = _session(b, PREFIX + lines + [probe], timeout)
    res = {'time': r['time'], 'raw': ''}
    if r['status'] != 'ok':
        res.update(status=r['status'], msg=r.get('msg', ''), stderr=r.get('stderr', ''))
        return res
    blocks = r['blocks'][len(PREFIX):]
    types = blocks[-1].strip().strip('[]').split(',')
    unknown = [nm for nm, ty in zip(names, types) if ty.strip() == 'identifier']
    if unknown:
        res.update(status='unsupported', msg='no Giac function ' + ','.join(unknown))
        return res
    syntax_err = 'syntax error' in r['stderr']
    for k, blk in enumerate(blocks[:-1]):
        if 'Error:' in blk or (syntax_err and blk.strip() == 'undef'):
            msg = blk.replace('\n', ' ').strip().strip('"')
            if syntax_err and blk.strip() == 'undef':
                msg = 'Giac syntax error: ' + re.sub(r'\s+', ' ', r['stderr'].split('syntax error', 1)[1][:120])
            res.update(status='error', msg=('line %d: ' % (k + 1) if k < len(blocks) - 2 else '') + msg,
                       raw=blk)
            return res
    raw = blocks[-2]
    res['raw'] = raw
    last = [l for l in raw.split('\n') if l.strip()]
    last = last[-1] if last else ''
    if '"' in last and prob.get('check', 'exact') != 'str':
        msg = last.strip()
        res.update(status='uneval' if re.search(r'[Uu]nable to (solve|find)', msg) else 'error', msg=msg)
        return res
    res['status'] = 'ok'
    res['out'] = to_ours(raw, trans.get('solvevars'))
    return res


PREFIX = ['with_sqrt(0)']


# ------------------------------------------------------------------ output ---
def _mod(m):
    a, n = int(m.group(1)), int(m.group(2))
    return str(a % n)


OUT_RENAME = {'Gamma': 'gamma', 'Zeta': 'zeta', 'BesselJ': 'besselj', 'besselJ': 'besselj', 'BesselY': 'bessely',
              'Dirac': 'dirac', 'Heaviside': 'heaviside'}


def _outcall(name, args):
    if name in OUT_RENAME:
        return '%s(%s)' % (OUT_RENAME[name], ','.join(args))
    if name == 'Psi':
        return 'digamma(%s)' % args[0] if len(args) == 1 else 'polygamma(%s,%s)' % (args[1], args[0])
    if name == 'surd':
        return 'surd(%s,%s)' % (args[0], args[1])
    if name == 'not':
        return 'Not(%s)' % args[0]
    return None


def _factor_end(s, j):
    """end index of the factor starting at s[j] (atom, call, or (..), with ^atom)"""
    m = re.match(r'[A-Za-z_]\w*', s[j:])
    if m:
        k = j + m.end()
        if k < len(s) and s[k] == '(':
            k = match_close(s, k) + 1
    elif j < len(s) and s[j] == '(':
        k = match_close(s, j) + 1
    else:
        return None
    if k < len(s) and s[k] == '^':
        m2 = re.match(r'\^(\d+|[A-Za-z_]\w*|\()', s[k:])
        if not m2:
            return None
        if m2.group(1) == '(':
            k = match_close(s, k + 1) + 1
        else:
            k += m2.end()
    return k


def coeff_last(s):
    """cosmetic: Giac prints 1/2*x; rewrite p/q*F as p*F/q (F/q for p=1) so the
    size-based simp/form checks compare like with like"""
    out, i = '', 0
    rx = re.compile(r'(?<![\w.)\]^/])(\d+)/(\d+)\*')
    while True:
        m = rx.search(s, i)
        if not m:
            return out + s[i:]
        k = _factor_end(s, m.end())
        if k is None or (k < len(s) and s[k] in '^!'):
            out += s[i:m.end()]
            i = m.end()
            continue
        f = s[m.end():k]
        out += s[i:m.start()] + ('%s/%s' % (f, m.group(2)) if m.group(1) == '1' else
                                 '%s*%s/%s' % (m.group(1), f, m.group(2)))
        i = k


def _eqs(vals, names):
    if not vals.startswith('['):
        return None
    items = split_top(vals[1:-1])
    if len(items) != len(names):
        return None
    eqs = ['%s=%s' % (n, v.strip()) for n, v in zip(names, items) if v.strip() != n]
    return '[' + ','.join(eqs) + ']'


def to_ours(raw, solvevars=None):
    lines = [l for l in raw.split('\n') if l.strip()]
    s = lines[-1].strip() if lines else ''
    s = re.sub(r'\b(list|set|matrix)\[', '[', s)
    s = replace_call_terms(s, 'order_size', '0')
    s = s.replace('+infinity', 'inf').replace('-infinity', '-inf')
    s = re.sub(r'\binfinity\b', 'inf', s)
    s = re.sub(r'(?<![\w)])(-?\d+)\s*%\s*\d+', r'\1', s)
    s = re.sub(r'\bc_(\d+)\b', r'c\1', s)
    s = rewrite_calls(s, _outcall)
    if len(split_top(s, ',')) > 1:
        s = '[' + s + ']'
    s = coeff_last(s)
    if solvevars and s.startswith('[[') and '=' not in s:
        sols = [_eqs(v.strip(), solvevars) for v in split_top(s[1:-1])]
        if all(sols):
            s = '[' + ','.join(sols) + ']'
    return s
