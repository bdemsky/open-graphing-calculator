"""Maxima (Macsyma's descendant; Wester's own transcripts are Macsyma) as a
baseline engine, driven by `maxima --very-quiet --batch-string`.

STATUS: run with Maxima 5.49 (MacPorts, ECL); every WRONG, UNGRADED and ERROR
case was reviewed (maxima/overrides.txt has the per-problem decisions).

Translation (our syntax -> Maxima):
  constants   pi e i inf -inf euler_gamma -> %pi %e %i inf minf %gamma
  := on a name -> ':' (assignment); f(x) := ... stays a function definition
  [[..]]      -> matrix([..],[..])
  renames     ln->log, conj->conjugate, re/im->realpart/imagpart, sign->signum,
              heaviside->unit_step, dirac->delta, besselj->bessel_j, ...
  rewrites    log(a)->log(a)/log(10), subst(e,x,a)->subst(a,x,e), map(L,f)->map(f,L),
              series(f,x,a,n)->taylor(f,x,a,n-1), sum->simplify_sum(sum(..)),
              product->ev(product(..),simpproduct), solve(ineq)->fourier_elim,
              dsolve->ode2 (+ic1/ic2), systems->desolve, rsolve->solve_rec,
              simplify->wsimp (the smallest of ratsimp, radcan, trigsimp,
              fullratsimp, sqrtdenest), assume(n,integer)->declare(n,integer)
A command missing from the table makes the problem UNSUPPORTED.
"""
import os
import re
import shutil
import signal
import subprocess
import tempfile
import threading
import time

from syntax import rewrite_calls, split_top, call_names, top_equal, rename_idents, match_close

NAME = 'maxima'

RENAME = {
    'ln': 'log', 'conj': 'conjugate', 're': 'realpart', 'im': 'imagpart', 'sign': 'signum', 'ceil': 'ceiling',
    'heaviside': 'unit_step', 'dirac': 'delta', 'besselj': 'bessel_j', 'Ci': 'expintegral_ci',
    'fibonacci': 'fib', 'euler': 'totient', 'primroot': 'zn_primroot', 'invmod': 'inv_mod',
    'partition': 'num_partitions', 'normal': 'ratsimp', 'cfactor': 'gfactor', 'texpand': 'trigexpand',
    'tcollect': 'trigreduce', 'rem': 'remainder', 'ifactor': 'factor', 'chebyshevt': 'chebyshev_t',
    'legendre': 'legendre_p', 'legendrep': 'assoc_legendre_p', 'ellipticE': 'elliptic_e',
    'jacobidn': None, 'stddev': 'std', 'det': 'determinant', 'inv': 'invert', 'ker': 'nullspace',
    'identity': 'ident', 'transpose': 'transpose', 'expm': None, 'simplify': 'wsimp',
    'intersect': None, 'setdiff': None, 'linsolve': 'linsolve', 'csolve': 'solve',
    'bernoulli': 'bern', 'nextprime': 'next_prime', 'prevprime': 'prev_prime', 'ilaplace': 'ilt',
}
SAME = {'integrate', 'diff', 'limit', 'factor', 'expand', 'partfrac', 'trigsimp', 'resultant', 'binomial',
        'factorial', 'gamma', 'zeta', 'erf', 'abs', 'floor', 'sqrt', 'exp', 'sin', 'cos', 'tan', 'sec', 'acot',
        'atan', 'asin', 'acos', 'asinh', 'sinh', 'cosh', 'max', 'min', 'bernoulli', 'stirling1', 'pochhammer',
        'prevprime', 'nextprime', 'horner', 'mean', 'median', 'residue', 'laplace', 'ilaplace', 'minor',
        'rank', 'jacobian', 'hessian', 'wronskian', 'charpoly', 'gramschmidt', 'potential', 'curl',
        'solve', 'assume', 'union', 'cartprod', 'map', 'subst', 'log', 'approx', 'gcd', 'ichinrem', 'contfrac',
        'quartile1', 'binompdf', 'binomcdf', 'normalcdf', 'tcdf', 'chi2cdf', 'series', 'sum', 'product',
        'dsolve', 'rsolve', 'eigenvals', 'eigenvects', 'jordan', 'minpoly', 'sinm', 'norm', 'cross',
        'vpotential', 'blockdiag', 'mod', 'lu', 'pade', 'hypergeom', 'digamma', 'polylog', 'nint',
        'implicitdiff', 'fourier_an', 'fourier_bn', 'factorial2', 'grad', 'xor', 'not'} | set(RENAME)
CONST = {'pi': '%pi', 'e': '%e', 'i': '%i', 'euler_gamma': '%gamma'}

PRELUDE = r'''display2d:false$ linel:1000000$ ratprint:false$ simpsum:true$ cflength:1$
for p in ["simplify_sum","solve_rec","descriptive","distrib","vect","diag","functs","fourier_elim",
          "orthopoly","eigen","linearalgebra","fourie","hypergeometric","to_poly_solve"] do errcatch(load(p))$
wsimp(e):=block([c,best],c:[e,ratsimp(e),RADCAN trigsimp(e),fullratsimp(e)],
  c:append(c,errcatch(sqrtdenest(e))),best:e,
  for x in c do if slength(string(x))<slength(string(best)) then best:x,best)$
wsol_ok(s,v):=listp(s) and every(lambda([q],not atom(q) and op(q)="=" and lhs(q)=v and freeof(v,rhs(q))),s)$
wtps(e,v):=block([t,r],t:errcatch(to_poly_solve(e,v)),
  if t=[] or atom(first(t)) or op(first(t))#'%union then return(false),
  t:args(first(t)),
  if not every(lambda([q],listp(q) and length(q)=1),t) then return('raw),
  r:map(first,t),
  if not wsol_ok(r,v) then return(false),
  r:map(rhs,r),
  for u in listofvars(r) do if slength(string(u))>2 and substring(string(u),1,3)="%z" then r:subst(0,u,r),
  unique(r))$
wsolve(e,v):=block([s,t],
  if not freeof(sin,cos,tan,sec,csc,cot,e) then (t:wtps(e,v),if listp(t) then return(t)),
  s:solve(e,v),
  if wsol_ok(s,v) then return(map(rhs,s)),
  t:wtps(e,v),
  if listp(t) then return(t),
  if t='raw then return(first(errcatch(to_poly_solve(e,v)))),
  unsolved(s))$
'''


def _mat(s):
    """[[..],[..]] literals -> matrix([..],[..])"""
    out, i = '', 0
    while i < len(s):
        if s.startswith('[[', i) and (i == 0 or not (s[i - 1].isalnum() or s[i - 1] in '_])')):
            j = match_close(s, i)
            rows = split_top(s[i + 1:j])
            out += 'matrix(%s)' % ','.join(_mat(r) for r in rows)
            i = j + 1
        else:
            out += s[i]
            i += 1
    return out


def _ode(args):
    eqs, fs, var = args[0].strip(), args[1].strip(), args[2].strip()
    funcs = [f.strip() for f in split_top(fs[1:-1])] if fs.startswith('[') else [fs]
    items = split_top(eqs[1:-1]) if eqs.startswith('[') else [eqs]
    odes, ics = [], []
    for it in items:
        m = re.match(r"^\s*([A-Za-z_]\w*)('*)\((.+)\)\s*=\s*(.+)$", it)
        if m and m.group(1) in funcs and m.group(3).strip() != var:
            ics.append((m.group(1), len(m.group(2)), m.group(3), m.group(4)))
            continue
        e = it
        for f in funcs:
            if len(funcs) == 1:
                e = re.sub(r"\b%s''" % f, "'diff(%s,%s,2)" % (f, var), e)
                e = re.sub(r"\b%s'" % f, "'diff(%s,%s)" % (f, var), e)
            else:
                e = re.sub(r"\b%s''" % f, "diff(%s(%s),%s,2)" % (f, var, var), e)
                e = re.sub(r"\b%s'" % f, "diff(%s(%s),%s)" % (f, var, var), e)
                e = re.sub(r'\b%s\b(?![(])' % f, '%s(%s)' % (f, var), e)
        odes.append(e)
    if len(funcs) > 1:
        return 'desolve([%s],[%s])' % (','.join(odes), ','.join('%s(%s)' % (f, var) for f in funcs))
    f = funcs[0]
    sol = 'ode2(%s,%s,%s)' % (odes[0], f, var)
    if not ics:
        return sol
    pts = {ic[2].strip() for ic in ics}
    if len(ics) == 1 and ics[0][1] == 0:
        app = 'ic1(s_,%s=%s,%s=%s)' % (var, ics[0][2], f, ics[0][3])
    elif len(ics) == 2 and len(pts) == 1:
        y0 = [ic for ic in ics if ic[1] == 0][0]
        d0 = [ic for ic in ics if ic[1] == 1][0]
        app = "ic2(s_,%s=%s,%s=%s,'diff(%s,%s)=%s)" % (var, y0[2], f, y0[3], f, var, d0[3])
    elif len(ics) == 2 and all(ic[1] == 0 for ic in ics):
        app = 'bc2(s_,%s=%s,%s=%s,%s=%s,%s=%s)' % (var, ics[0][2], f, ics[0][3], var, ics[1][2], f, ics[1][3])
    else:
        raise ValueError('boundary conditions on derivatives at two points')
    # ode2 returns false when it finds no solution: keep that false (UNEVAL) instead of an ic error
    return 'block([s_:%s],if s_=false then false else %s)' % (sol, app)


def _call(name, a):
    n = len(a)
    if name == 'log':
        return '(log(%s)/log(10))' % a[0] if n == 1 else '(log(%s)/log(%s))' % (a[0], a[1])
    if name == 'subst':
        return 'subst(%s,%s,%s)' % (a[2], a[1], a[0])
    if name == 'map':
        return 'map(%s,%s)' % (a[1], a[0])
    if name == 'approx':
        return 'block([fpprec:%s],bfloat(%s))' % (a[1], a[0]) if n == 2 else 'rectform(float(%s))' % a[0]
    if name == 'gcd' and n > 2:
        r = a[0]
        for b in a[1:]:
            r = 'gcd(%s,%s)' % (r, b)
        return r
    if name == 'ichinrem':
        return 'block([p:%s,q:%s],[chinese([p[1],q[1]],[p[2],q[2]]),lcm(p[2],q[2])])' % (a[0], a[1])
    if name == 'contfrac':
        return 'firstn(cf(bfloat(%s)),%s)' % (a[0], a[1])
    if name == 'quartile1':
        return 'quantile(%s,1/4)' % a[0]
    if name == 'binompdf':
        return 'pdf_binomial(%s,%s,%s)' % (a[2], a[0], a[1])
    if name == 'binomcdf':
        return 'cdf_binomial(%s,%s,%s)' % (a[2], a[0], a[1])
    if name in ('normalcdf', 'tcdf', 'chi2cdf'):
        lo, hi = a[0].strip(), a[1].strip()
        if name == 'normalcdf':
            ps = (a[2], a[3]) if n == 4 else ('0', '1')
            F = lambda v: 'cdf_normal(%s,%s,%s)' % (v, ps[0], ps[1])
        elif name == 'tcdf':
            F = lambda v: 'cdf_student_t(%s,%s)' % (v, a[2])
        else:
            F = lambda v: 'cdf_chi2(%s,%s)' % (v, a[2])
        hi_ = '1' if hi == 'inf' else F(hi)
        lo_ = '0' if lo in ('minf', '-inf') else F(lo)
        return '(%s-%s)' % (hi_, lo_)
    if name == 'series':
        if a[1].strip().startswith('['):
            return 'ratdisrep(taylor(%s,%s,%s,(%s)-1))' % tuple(a)
        if a[2].strip() in ('inf', 'minf'):
            return 'ratdisrep(taylor(%s,%s,%s,%s))' % tuple(a)
        return 'ratdisrep(taylor(%s,%s,%s,(%s)-1))' % tuple(a)
    if name == 'sum':
        return 'simplify_sum(sum(%s))' % ','.join(a)
    if name == 'product':
        return 'ev(product(%s),simpproduct)' % ','.join(a) if n == 4 else 'apply("*",%s)' % a[0]
    if name == 'limit' and n == 4:
        return 'limit(%s,%s,%s,%s)' % (a[0], a[1], a[2], 'plus' if a[3].strip() == '1' else 'minus')
    if name == 'solve' and n == 2 and re.search(r'[<>]', a[0]):
        ineqs = a[0].strip() if a[0].strip().startswith('[') else '[%s]' % a[0]
        vs = a[1].strip() if a[1].strip().startswith('[') else '[%s]' % a[1]
        return 'fourier_elim(%s,%s)' % (ineqs, vs)
    if name == 'dsolve' and n == 3:
        return _ode(a)
    if name == 'csolve' and n == 2 and re.search(r'\b(conj|conjugate|re|im|realpart|imagpart|abs)\(', a[0]):
        # Maxima's symbols are real unless declared: conjugate(z) would simplify to z
        return '(declare(%s,complex),wsolve(%s,%s))' % (a[1], a[0], a[1])
    if name in ('solve', 'csolve') and n == 2 and not a[1].strip().startswith('['):
        return 'wsolve(%s,%s)' % (a[0], a[1])
    if name == 'rsolve':
        f = re.match(r'\s*([A-Za-z_]\w*)\(', a[1]).group(1)
        idx = lambda s: re.sub(r'\b%s\(([^()]*)\)' % f, r'%s[\1]' % f, s)
        ics = [idx(c) for c in split_top(a[2].strip()[1:-1])] if n == 3 else []
        return 'solve_rec(%s,%s%s)' % (idx(a[0]), idx(a[1]), ''.join(',' + c for c in ics))
    if name == 'eigenvals':
        return 'first(eigenvalues(%s))' % a[0]
    if name == 'eigenvects':
        return ('block([e:eigenvectors(%s)],makelist([e[1][1][k],e[2][k]],k,1,length(e[1][1])))' % a[0])
    if name == 'jordan':
        return 'dispJordan(jordan(%s))' % a[0]
    if name == 'minpoly':
        return 'subst(%s,x,minimalPoly(jordan(%s)))' % (a[1], a[0])
    if name == 'charpoly':
        return 'expand((-1)^length(%s)*charpoly(%s,%s))' % (a[0], a[0], a[1])
    if name == 'expm':
        return 'mat_function(exp,%s)' % a[0]
    if name == 'sinm':
        return 'mat_function(sin,%s)' % a[0]
    if name == 'norm' and a[0].strip().startswith('['):
        return 'mat_norm(matrix(%s),%s)' % (a[0], a[1] if n == 2 else 'frobenius')
    if name == 'norm':
        return 'mat_norm(%s,%s)' % (a[0], a[1] if n == 2 else 'frobenius')
    if name == 'cross':
        return 'express(%s ~ %s)' % (a[0], a[1])
    if name == 'curl':
        return '(scalefactors(%s),ev(express(curl(%s)),diff))' % (a[1], a[0])
    if name == 'potential':
        return '(scalefactors(%s),potential(%s))' % (a[1], a[0])
    if name == 'vpotential':
        return '(scalefactors(%s),vectorpotential(%s))' % (a[1], a[0])
    if name == 'jacobidn':
        return 'jacobi_dn(%s,(%s)^2)' % (a[0], a[1])     # Maxima's second argument is the parameter m = k^2
    if name == 'blockdiag':
        return 'diag([%s])' % ','.join(x if x.strip().startswith('matrix') else 'matrix([%s])' % x for x in a)
    if name == 'mod' and a[0].strip().startswith('matrix'):
        return 'matrixmap(lambda([u],mod(u,%s)),%s)' % (a[1], a[0])
    if name == 'lu':
        return 'block([r:get_lu_factors(lu_factor(%s))],[r[2],r[3],transpose(r[1])])' % a[0]
    if name == 'pade':
        return 'first(pade(taylor(%s,%s,0,(%s)+(%s)),%s,%s))' % (a[0], a[1], a[2], a[3], a[2], a[3])
    if name == 'hypergeom':
        return 'hgfred(%s,%s,%s)' % tuple(a)
    if name == 'digamma':
        return 'psi[0](%s)' % a[0]
    if name == 'polylog':
        return 'li[%s](%s)' % (a[0], a[1])
    if name == 'nint':
        return 'first(quad_qags(%s,%s,%s,%s))' % tuple(a)
    if name == 'implicitdiff':
        eq, y, x = a
        return 'block([s],depends(%s,%s),s:solve(diff(%s,%s),diff(%s,%s)),rhs(first(s)))' % (y, x, eq, x, y, x)
    if name in ('fourier_an', 'fourier_bn'):
        f, x, T, nn, a0 = a
        k = 2 if name == 'fourier_an' else 3
        if nn.strip() == '0' and name == 'fourier_an':
            return 'rhs(apply(ev,[first(fourier(%s,%s,(%s)/2))]))' % (f, x, T)    # fourier returns %t labels
        return 'ev(rhs(apply(ev,[fourier(%s,%s,(%s)/2)[%d]])),n=%s)' % (f, x, T, k, nn)
    if name == 'factorial2':
        return '(%s)!!' % a[0]
    if name == 'grad':
        return 'makelist(diff(%s,v_),v_,%s)' % (a[0], a[1])
    if name == 'xor':
        return '(((%s) and not (%s)) or (not (%s) and (%s)))' % (a[0], a[1], a[0], a[1])
    if name == 'union':
        return 'union(%s)' % ','.join('setify(%s)' % x for x in a)
    if name == 'intersect':
        return 'intersection(%s)' % ','.join('setify(%s)' % x for x in a)
    if name == 'setdiff':
        return 'setdifference(setify(%s),setify(%s))' % (a[0], a[1])
    if name == 'cartprod':
        return 'cartesian_product(setify(%s),setify(%s))' % (a[0], a[1])
    if name == 'assume' and n == 2 and a[1].strip() in ('integer', 'complex'):
        return 'declare(%s,%s)' % (a[0], a[1].strip())
    if name == 'assume' and n == 1 and ' and ' in a[0]:
        return 'assume(%s)' % ','.join(split_top(a[0], ' and '))
    if name in RENAME and RENAME[name]:
        return '%s(%s)' % (RENAME[name], ','.join(a))
    return None


def translate_line(line, check):
    line = re.sub(r'(?<![\w.])-inf\b', 'minf', line)
    line = rename_idents(line, CONST)
    line = _mat(line)
    i = line.find('matrix(')
    while i >= 0:                                   # M^(1/2): matrix square root
        j = match_close(line, i + 6)
        if line.startswith('^(1/2)', j + 1):
            line = line[:i] + 'mat_function(sqrt,%s)' % line[i:j + 1] + line[j + 7:]
        i = line.find('matrix(', i + 7)
    line = rewrite_calls(line, _call)
    m = re.match(r'^\s*([A-Za-z_]\w*)\s*:=\s*(.*)$', line)
    if m:
        line = '%s:%s' % (m.group(1), m.group(2))
    if check == 'bool' and re.search(r'[<>]|=', line) and not line.startswith('is('):
        eq = top_equal(line)
        line = 'is(equal(%s,%s))' % eq if eq else 'is(%s)' % line    # '=' in is() is syntactic
    return line


def translate(prob, ov):
    if ov.get('in') is not None:
        return {'lines': ov.get('pre', []) + [ov['in']], 'notes': ['override'], 'unsupported': None}
    src = prob['pre'] + [prob['in']]
    userdef = {m.group(1) for l in src for m in [re.match(r'^\s*([A-Za-z_]\w*)\s*\(.*?\)\s*:=', l)] if m}
    used = {nm for l in src for nm in call_names(l)}
    odef = set()
    for l in src:
        for m in re.finditer(r'dsolve\(.*,\s*\[?([a-z,\s]+)\]?\s*,\s*[a-z]\s*\)$', l):
            odef |= {f.strip() for f in m.group(1).split(',')}
        if 'rsolve(' in l:
            odef.add('f')
    missing = sorted(nm for nm in used - SAME - userdef - set(prob['allow']) - odef - {'piecewise'} if len(nm) > 1)
    if 'piecewise' in used:
        missing.append('piecewise (Maxima has no piecewise expressions)')
    if re.search(r'\b0[xo][0-9A-Fa-f]+', ' '.join(src)):
        missing.append('hex/octal literals')
    try:
        lines = [translate_line(l, prob.get('check', 'exact') if k == len(src) - 1 else 'exact')
                 for k, l in enumerate(src)]
    except Exception as ex:
        return {'lines': [], 'notes': [], 'unsupported': 'no translation: %s' % ex}
    if missing:
        return {'lines': lines, 'notes': [], 'unsupported': 'no Maxima counterpart for ' + ', '.join(missing)}
    return {'lines': lines, 'notes': [], 'unsupported': None}


# ------------------------------------------------------------------ binary ---
def find_binary():
    for c in [os.environ.get('MAXIMA'), shutil.which('maxima'), '/opt/local/bin/maxima', '/usr/local/bin/maxima']:
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    return None


def available():
    b = find_binary()
    if not b:
        return False, 'Maxima not found: install it with `sudo port install maxima` (or set MAXIMA=/path/to/maxima)'
    try:
        p = subprocess.run([b, '--version'], capture_output=True, text=True, timeout=60)
        ver = ' '.join(l for l in p.stdout.splitlines() if l.strip() and not l.startswith(';;;'))
        return True, '%s (%s)' % (ver or 'maxima', b)
    except Exception as ex:
        return False, 'Maxima at %s does not start: %s' % (b, ex)


MAX_PROCS = 3                       # Maxima processes at once, whatever -j is
MEM_LIMIT = 2 << 30                 # bytes of resident memory per problem (its process group)
_SLOTS = threading.BoundedSemaphore(MAX_PROCS)


def _group_rss(pgid):
    try:
        out = subprocess.run(['ps', '-A', '-o', 'pgid=,rss='], capture_output=True, text=True, timeout=10).stdout
    except Exception:
        return 0
    return sum(int(r) for g, r in (l.split() for l in out.splitlines() if len(l.split()) == 2)
               if g == str(pgid)) * 1024


def _run_guarded(cmd, timeout, d):
    """run cmd in its own process group with a wall-clock limit and a memory limit
    (the whole group is killed); returns an object with .stdout and .returncode"""
    outp = os.path.join(d, 'out.txt')
    with open(outp, 'w') as fo:
        pr = subprocess.Popen(cmd, stdout=fo, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                              start_new_session=True)
        t0, err = time.time(), None
        while pr.poll() is None:
            if time.time() - t0 > timeout:
                err = subprocess.TimeoutExpired(cmd, timeout)
            elif _group_rss(pr.pid) > MEM_LIMIT:
                err = MemoryError()
            if err is not None:
                try:
                    os.killpg(pr.pid, signal.SIGKILL)
                except OSError:
                    pass
                pr.wait()
                os.remove(outp)
                raise err
            time.sleep(0.25)
    out = open(outp, errors='replace').read()
    os.remove(outp)
    return subprocess.CompletedProcess(cmd, pr.returncode, out, '')


def program(lines):
    out = [PRELUDE.replace('RADCAN ', 'radcan(e),' if os.environ.get('WESTER_MAXIMA_RADCAN') else ''),
           'printf(true,"~%@@BEGIN~%")$']
    for k, l in enumerate(lines[:-1]):
        out.append('if errcatch(%s)=[] then printf(true,"~%%@@ERR line %d~%%")$' % (l, k + 1))
    out.append('__r:errcatch(%s)$' % lines[-1])
    out.append('if __r=[] then printf(true,"~%@@ERR~%") else printf(true,"~%@@OUT ~a~%",string(first(__r)))$')
    return '\n'.join(out) + '\n'


def run(trans, prob, timeout):
    b = find_binary()
    t0 = time.time()
    d = tempfile.mkdtemp(prefix='wester_maxima_')
    path = os.path.join(d, 'p.mac')
    open(path, 'w').write(program(trans['lines']))
    try:
        # batch (not batchload, which prints no question before it hits the end of stdin): a question
        # ("Is x positive?") is printed, then fails on the empty stdin
        with _SLOTS:
            p = _run_guarded([b, '--very-quiet', '--batch=' + path], timeout, d)
    except subprocess.TimeoutExpired:
        return {'status': 'timeout', 'time': time.time() - t0}
    except MemoryError:
        return {'status': 'crash', 'msg': 'killed: over %d MB of memory' % (MEM_LIMIT >> 20),
                'time': time.time() - t0}
    finally:
        try:
            os.remove(path)
            os.rmdir(d)
        except OSError:
            pass
    res = {'time': time.time() - t0, 'echo': prob.get('in') or ''}
    m = re.search(r'^@@ERR(.*)$', p.stdout, re.M)
    if m:
        txt = p.stdout[:m.start()]
        txt = txt[txt.rfind('@@BEGIN') + 7:] if '@@BEGIN' in txt else txt
        body = [l.strip() for l in txt.splitlines()
                if l.strip() and not l.startswith(('if errcatch(', '__r:', 'if __r')) and
                'Error while listening' not in l and 'C library explanation' not in l]
        q = [l for l in body if re.match(r'^(Is|Are|Does)\b.*\?\s*$', l)]
        if q:
            msg = 'Maxima asks: %s (no answer in batch mode)' % q[-1]
        else:
            msg = 'Maxima error%s: %s' % (m.group(1), ' '.join(body[-3:])[:250])
        res.update(status='error', msg=msg, raw=' '.join(body[-4:])[-500:])
        return res
    m = re.search(r'^@@OUT (.*)$', p.stdout, re.M)
    if not m:
        res.update(status='crash', msg='no output (rc=%s) %s' % (p.returncode, p.stderr[-300:]))
        return res
    raw = m.group(1).strip()
    last = trans['lines'][-1] if trans['lines'] else ''
    if raw.startswith('unsolved('):
        res.update(status='uneval', raw=raw, msg='solve and to_poly_solve leave it unsolved: ' + raw[:200])
        return res
    if raw == 'false' and re.match(r'^\s*(block\(\[s_:)?(ode2|solve_rec|desolve)\(', last):
        res.update(status='uneval', raw=raw, msg='%s finds no solution (returns false)' %
                   re.match(r'^\s*(?:block\(\[s_:)?(\w+)', last).group(1))
        return res
    res.update(status='ok', raw=raw, out=to_ours(raw, last))
    return res


# ------------------------------------------------------------------ output ---
OUT_RENAME = {'log': 'ln', 'bessel_j': 'besselj', 'bessel_y': 'bessely', 'expintegral_ci': 'Ci',
              'expintegral_si': 'Si', 'expintegral_ei': 'Ei', 'lambert_w': 'LambertW', 'signum': 'sign',
              'realpart': 're', 'imagpart': 'im', 'conjugate': 'conj', 'unit_step': 'heaviside',
              'delta': 'dirac', 'hypergeometric': 'hypergeom', 'fib': 'fibonacci', 'chebyshev_t': 'chebyshevt',
              'legendre_p': 'legendre', 'assoc_legendre_p': 'assoc_legendre', 'gamma_incomplete': 'uppergamma',
              'determinant': 'det', 'ilt': 'ilaplace'}


def _flat_col(v):
    """[[a],[b],[c]] (a column matrix) -> [a,b,c]"""
    v = v.strip()
    if v.startswith('[[') and v.endswith(']]'):
        rows = split_top(v[1:-1])
        if all(r.strip().startswith('[') and len(split_top(r.strip()[1:-1])) == 1 for r in rows):
            return '[%s]' % ','.join(r.strip()[1:-1] for r in rows)
    return v


def _genfact(a):
    """genfact(x, x/2, 2) is Maxima's x!!"""
    if len(a) == 3 and a[2].strip() == '2':
        import sympy as sp
        try:
            if sp.simplify(sp.sympify(a[1].replace('^', '**')) - sp.sympify(a[0].replace('^', '**')) / 2) == 0:
                return 'factorial2(%s)' % a[0]
        except Exception:
            return None
    return None


def _outcall(name, a):
    if name == 'span':
        return '[%s]' % ','.join(_flat_col(x) for x in a)
    if name == 'genfact':
        return _genfact(a)
    if name == 'gamma_incomplete_regularized':
        return '(uppergamma(%s,%s)/gamma(%s))' % (a[0], a[1], a[0])
    if name == 'beta_incomplete_regularized':
        return 'betainc_regularized(%s,%s,0,%s)' % (a[0], a[1], a[2])
    if name == 'matrix':
        return '[%s]' % ','.join(a)
    if name in OUT_RENAME:
        return '%s(%s)' % (OUT_RENAME[name], ','.join(a)) if OUT_RENAME[name] else '[%s]' % ','.join(a)
    if name == '%union':
        return '[%s]' % ','.join('(%s)' % ' and '.join(split_top(x.strip()[1:-1])) if x.strip().startswith('[')
                                 else x for x in a)
    return None


def _not(s):
    """Maxima's prefix 'not x' -> Not(x) (Python would read 'not x' as False)"""
    while True:
        ms = [m for m in re.finditer(r'(?<![\w%])not\s+', s)]
        if not ms:
            return s
        m = ms[-1]
        j = m.end()
        if s[j] == '(':
            k = match_close(s, j)
        else:
            mm = re.match(r'[\w%.]+', s[j:])
            k = j + mm.end() - 1
            if k + 1 < len(s) and s[k + 1] == '(':
                k = match_close(s, k + 1)
        s = s[:m.start()] + 'Not(%s)' % s[j:k + 1] + s[k + 1:]


def _ineq_out(s):
    """fourier_elim: [c1,c2] or [c3] or ... -> (c1 and c2) or c3 ...; equations as Eq()"""
    def cond(c):
        eq = top_equal(c)
        return 'Eq(%s,%s)' % eq if eq else c.strip()
    parts = []
    for part in split_top(s, ' or '):
        part = part.strip()
        items = split_top(part[1:-1]) if part.startswith('[') and part.endswith(']') else [part]
        items = [cond(c) for c in items if c.strip()]
        parts.append(items[0] if len(items) == 1 else '(%s)' % ' and '.join(items))
    return ' or '.join(parts) if parts else '[]'


def to_ours(s, last=''):
    if re.match(r'^\s*solve_rec\(', last):
        s = re.sub(r'^\s*[A-Za-z_]\w*\[\w+\]\s*=\s*', '', s)          # f[n] = expr
    if re.match(r'^\s*desolve\(', last):
        fs = re.findall(r'\b([A-Za-z_]\w*)\(t\)\s*=', s)
        for k_, f_ in enumerate(fs):                                   # u(0) -> free constant
            s = re.sub(r'\b%s\(0\)' % f_, 'c%d' % (k_ + 1), s)
            s = re.sub(r'\b%s\(t\)' % f_, f_, s)
    s = s.replace('%phi', '((1+sqrt(5))/2)')
    s = re.sub(r'%f\[\d+,\d+\]\(', 'hypergeom(', s)
    s = re.sub(r"'(integrate|sum|limit|diff|product|at)\(", r'\1(', s)
    s = re.sub(r'psi\[0\]\(', 'digamma(', s)
    s = re.sub(r'li\[(\d+)\]\(([^()]*)\)', r'polylog(\1,\2)', s)
    s = re.sub(r'(\d)b([+-]?\d+)', r'\1e\2', s)
    s = s.replace('%pi', 'pi').replace('%gamma', 'euler_gamma').replace('%e', 'e').replace('%i', 'i')
    s = re.sub(r'\bminf\b', '-inf', s)
    s = re.sub(r'\binfinity\b', 'inf', s)
    for var, r in re.findall(r'\b([A-Za-z_]\w*)\s*=\s*(%r\d+)\b', s):     # linsolve parameters
        s = re.sub(r',?\s*\b%s\s*=\s*%s\b' % (var, re.escape(r)), '', s, count=1).replace('[,', '[')
        s = s.replace(r, var)
    s = re.sub(r'\b(und|ind)\b', 'undef', s)
    def const(m):
        return 'c%s' % (m.group(2) or '1')
    s = re.sub(r'%(c|k|r|z)(\d*)', const, s)
    s = s.replace('{', '[').replace('}', ']')
    s = rewrite_calls(s, _outcall)
    s = _not(s)
    if re.match(r'^\s*fourier_elim\(', last) and s.strip() not in ('emptyset', 'universalset'):
        return _ineq_out(s)
    if s.strip() == 'emptyset':
        s = '[]'
    if s.strip() == 'universalset':
        s = 'true'
    if s.startswith('[') and s.endswith(']') and re.search(r'[<>]', s) and '=' not in s.replace('<=', '').replace('>=', ''):
        items = split_top(s[1:-1])
        if len(items) > 1 and not any(' and ' in x or ' or ' in x for x in items):
            s = '(' + ' and '.join(items) + ')'
    return s
