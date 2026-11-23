#!/usr/bin/env python3
"""Writes COMPARISON.md from ../results.json (our CAS) and
<engine>/results.json (run_baselines.py).  Gap categories come from the
"Gaps by capability" table of ../REPORT.md."""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WESTER = os.path.dirname(HERE)
sys.path.insert(0, WESTER)
sys.path.insert(0, HERE)

ENGINES = [('giac', 'Giac'), ('sympy', 'SymPy'), ('maxima', 'Maxima')]
CATS = ['PASS', 'WRONG', 'UNEVAL', 'UNSUPPORTED', 'ERROR', 'TIMEOUT', 'CRASH', 'UNGRADED', 'NA']


METHOD = r'''## How the engines were run and translated

Layout: `run_baselines.py` is the runner and grader. `engine_giac.py`, `engine_sympy.py` (with
`sympy_worker.py`) and `engine_maxima.py` are the translation layers. `<engine>/overrides.txt` is the
per-problem override table, `<engine>/translated.txt` holds every translated input with its notes,
and `<engine>/results.{json,md}` hold the results. Every problem runs in a fresh engine process with a
30 s wall-clock limit (`--timeout`). Our CAS gets 10 s per line.

**Giac** is the CAS of the HP Prime and of Xcas. The baseline is the official Xcas macOS build
(`xcas_mac.dmg.gz`, giac 2.1.0) from the Giac site. MacPorts has no giac port. The package installs
into `/Applications` as root, so it was not installed: the payload was unpacked into a user directory and
`usr/bin/icas` runs in place (`GIAC=/path/to/usr/bin/icas`). It needs MacPorts' readline, which the runner
finds through `DYLD_FALLBACK_LIBRARY_PATH=/opt/local/lib`. Caveat: desktop giac 2.1 is newer than
the giac inside the HP Prime firmware. The Prime's CAS also hides some commands behind its own menus
and names, so it may do slightly worse than this column. Our input syntax is close to Giac's. The
translation renames the commands whose names differ (approx -> evalf, minpoly -> pmin,
chebyshevt -> tchebyshev1, besselj -> BesselJ, gamma -> Gamma, ...). It also rewrites those whose
arguments or meaning differ:
- `log(a)` -> `log10(a)`: Giac's log is ln.
- `dsolve(eqs,y,x)` -> `desolve(eqs,x,y)`.
- `series(f,x,a,n)` -> `series(f,x,a,n-1)`: Giac keeps the order-n term.
- `pade` degrees.
- `jordan`/`smith`/`lu` take the matrix that we return out of Giac's result list.
- `eigenvects` is built from `jordan`.
- statistics argument orders.
The session starts with `with_sqrt(0)`, so `factor` works over Q. Giac's default also extends by
square roots: x^4-3x^2+1 comes out in four linear factors.

On output, `list[..]`, `set[..]` and `matrix[..]` become lists, and `order_size` terms are dropped.
`% n` residue tags are dropped (the printed symmetric residue is kept), `c_0` becomes `c0`, and
`1/2*x` becomes `x/2`. A function Giac does not know (`type(name)` is `identifier` at run time) makes
the problem UNSUPPORTED. Problems that `problems.txt` leaves without input, but where Giac has the
feature (modular equations, finite-field factoring, isolve, spline, rectangular form via evalc,
complex declarations, ODE systems in matrix form), are posed in `giac/overrides.txt` with Wester's
expected answers.

**SymPy** (1.14.0 in `.venv`). Each of our commands maps to the SymPy function a SymPy user would
call (`sympy_worker.CMDS`). `solve` runs on real symbols. `csolve` runs on complex unknowns. `dsolve`
uses `dsolve(..., ics=)`. `laplace` uses `laplace_transform`. `fourier_an` uses `fourier_series`.
`simplify` is `simplify` only: `sqrtdenest` and `radsimp` are not tried, so the denesting problems
C14-C21 are UNEVAL. Symbols are real, as in our CAS and in the expected answers.
`assume(x>0)`-style assumptions become symbol assumptions. Truth-value questions under relational
assumptions use `ask()`. N3 is a SymPy `ask()` error: it claims x^4-x > 0 for all real x. Commands
with no SymPy counterpart are UNSUPPORTED: mode, linreg, minpoly, pade, ztrans, jacobidn,
vpotential, the Wronskian matrix, natural splines, and multivariate series.

**Maxima** (MacPorts maxima 5.49, ECL) runs in batch mode (`maxima --very-quiet --batch=`), one
process per problem, at most 3 at once, and each process group is killed above 2 GB of resident
memory. The prelude loads simplify_sum, solve_rec, descriptive, distrib, vect, diag, functs,
fourier_elim, orthopoly, eigen, linearalgebra, fourie, hypergeometric and to_poly_solve. Output is
read with `string()` (one line, `display2d:false`); `%pi`/`%e`/`%i`, `[x = ..]` solution lists,
`%union` and `%z`/`%c`/`%r` constants are translated back. Its choices:
- `simplify` -> the smallest of ratsimp, trigsimp, fullratsimp and sqrtdenest.
- `solve` -> `solve`, then `to_poly_solve` when solve leaves it unsolved; trigonometric equations go
  to `to_poly_solve` first (solve keeps only the principal solution), with the integer parameters set
  to 0. `csolve` of an equation with conjugate/re/im/abs declares the unknown complex (Maxima's
  symbols are real).
- `sum` -> `simplify_sum`; `series` -> `taylor`.
- `dsolve` -> `ode2` + `ic1`/`ic2`; systems -> `desolve`; `rsolve` -> `solve_rec`.
- inequalities -> `fourier_elim`; `assume(n,integer)` -> `declare`.
- `piecewise`, hex literals, `rref`, `smith`, `svd`, `diag(M,k)`, `mode`, `minimize`, `fourier` and
  `ztrans` are UNSUPPORTED.
Maxima asks sign questions ("Is x positive, negative or zero?") where other engines branch or assume.
Batch mode cannot answer, so these are ERRORs (13 problems); an assumption is added only when the
problem states it. Every WRONG, UNGRADED and ERROR case was reviewed: `maxima/overrides.txt` notes
why each remaining WRONG is Maxima's own answer (radexpand simplifications of sqrt(1/z) and
sqrt(exp(6i)), rank over radicals/trig, float underflow, inaccurate algsys roots, ...).

**Manual classifications.** Outputs that the checker cannot grade (`check: manual`, or chain-rule
derivatives of unknown functions) were classified by hand. These are the `alt:` lines in each
`overrides.txt`, pinned to the exact output and each with its reason. If an output changes, the
problem is graded automatically again. Our CAS's own `alt:`/`override:` lines in `problems.txt` are
not used for the engines. Where an engine's output is the same as ours, the same classification was
used: `undef` for the principal-value integral W9 is UNEVAL for every engine, as for ours. Fourier
transforms (Y8-Y10) are graded in each engine's own normalization.

To regenerate: `make -C firmware/cas wester-baselines` (all available engines), or
`firmware/cas/tests/wester/baselines/run_baselines.py --engine giac --only M,P` and
`run_baselines.py --compare-only`.
'''


def load(path):
    return json.load(open(path)) if os.path.exists(path) else None


def expand_ids(text):
    ids = []
    for m in re.finditer(r'\b([A-Z])(\d+)([a-z]?)(?:-([A-Z])?(\d+)([a-z]?))?', text):
        L, a, sa, L2, b, sb = m.groups()
        if b is None:
            ids.append('%s%s%s' % (L, a, sa))
        else:
            for k in range(int(a), int(b) + 1):
                ids.append('%s%d' % (L, k))
            if sb:
                ids.append('%s%s%s' % (L, b, sb))
    return ids


def gaps_from_report(path):
    """[(num, name, [ids])] from the REPORT.md gap table"""
    gaps = []
    if not os.path.exists(path):
        return gaps
    txt = open(path).read()
    sec = txt.split('## Gaps by capability', 1)
    if len(sec) < 2:
        return gaps
    for line in sec[1].split('\n'):
        m = re.match(r'^\|\s*(\d+)\s*\|\s*(.*?)\s*\|\s*(.*?)\s*\|', line)
        if m:
            name = re.sub(r'\*\*(.*?)\*\*.*', r'\1', m.group(2)).strip().rstrip(':')
            gaps.append((int(m.group(1)), name, expand_ids(m.group(3))))
        elif line.startswith('## '):
            break
    return gaps


def esc(s, n=110):
    s = (s or '').replace('|', '\\|').replace('\n', ' ')
    return s if len(s) <= n else s[:n - 3] + '...'


def counts(rs):
    c = {k: 0 for k in CATS}
    for r in rs:
        c[r['category']] += 1
    return c


def ours_guard_flips(ours):
    """apply the baseline-only grader guards (form, unchanged) to our PASSes"""
    import run_baselines as RB
    import run_wester as W
    probs = {p['id']: p for p in W.load_problems(W.PROBLEMS)}
    flips = []
    for r in ours:
        if r['category'] != 'PASS' or r['id'] not in probs:
            continue
        p = probs[r['id']]
        kind = p.get('check', 'exact').split()[0]
        out = r['output']
        try:
            if kind in ('simp', 'form', 'factor') and RB.unchanged(p, out):
                flips.append((r['id'], 'returned unchanged'))
            elif kind == 'form' and not out.startswith('['):
                o = W.ops(W.to_sympy(out, evaluate=False))
                e = W.ops(W.to_sympy(p['expect'], evaluate=False))
                if int(o) > int(e):
                    flips.append((r['id'], 'form guard'))
        except Exception:
            pass
    return flips


def main():
    ours = load(os.path.join(WESTER, 'results.json'))
    if ours is None:
        print('no ../results.json: run make wester first')
        return
    O = {r['id']: r for r in ours}
    order = [r['id'] for r in ours]
    eng = {}
    status = {}
    for key, nm in ENGINES:
        status[key] = load(os.path.join(HERE, key, 'status.json')) or {'available': False, 'info': 'not run'}
        rs = load(os.path.join(HERE, key, 'results.json'))
        if rs and status[key].get('available'):
            eng[key] = {r['id']: r for r in rs}
    ran = [(k, n) for k, n in ENGINES if k in eng]
    L = []
    w = L.append
    w('# Wester benchmark: baselines from other CAS engines')
    w('')
    w('Generated by `run_baselines.py` / `compare.py` (`make -C firmware/cas wester-baselines`). '
      'Each engine gets the same 497 problems as our CAS (`../problems.txt`), translated to its own syntax, '
      'and is graded by the same SymPy checker (`../run_wester.py` `grade()`), with the same expected answers, '
      'checks and sampling domains. Our column is `../results.json` as committed with this snapshot of '
      '`problems.txt`.')
    w('')
    w('| engine | version | status |')
    w('|---|---|---|')
    w('| ours | `firmware/cas` `build/cas_repl` | `../results.json` (10 s per line, 24 MiB arena) |')
    for k, n in ENGINES:
        st = status[k]
        w('| %s | %s | %s |' % (n, esc(st.get('info', ''), 80) if st.get('available') else '-',
                                'ran, %s s wall clock per problem' % st.get('timeout_s', '?') if st.get('available')
                                else '**not run**: ' + esc(st.get('info', ''), 120)))
    w('')
    # ------------------------------------------------------------ totals
    w('## Totals')
    w('')
    w('PASS share is of the scored problems (all but NA). "not attempted" counts problems that '
      '`problems.txt` gives no input for (`unsupported:` for our CAS) and that the engine\'s override table '
      'does not pose either; they are scored UNSUPPORTED, as for our CAS.')
    w('')
    w('| engine | PASS | WRONG | UNEVAL | UNSUPPORTED | ERROR | TIMEOUT | CRASH | UNGRADED | NA | PASS share | not attempted |')
    w('|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|')
    rows = [('ours', ours)] + [(n, [eng[k][i] for i in order if i in eng[k]]) for k, n in ran]
    for nm, rs in rows:
        c = counts(rs)
        scored = len(rs) - c['NA']
        na = sum(1 for r in rs if r.get('attempted') is False and r['category'] != 'NA') if nm != 'ours' else \
            sum(1 for r in rs if r['category'] == 'UNSUPPORTED' and not r.get('input'))
        w('| %s | %s | %.1f %% | %d |' % (nm, ' | '.join(('**%d**' % c[k]) if k == 'PASS' else str(c[k]) for k in CATS),
                                          100.0 * c['PASS'] / max(scored, 1), na))
    for k, n in ENGINES:
        if k not in eng:
            w('| %s | not run | | | | | | | | | | |' % n)
    w('')
    # ------------------------------------------------------------ sections
    w('## Per section (PASS / scored)')
    w('')
    hdr = ['ours'] + [n for k, n in ENGINES]
    w('| section | ' + ' | '.join(hdr) + ' |')
    w('|---|' + '---:|' * len(hdr))
    secs = []
    for r in ours:
        if (r['section'], r['section_name']) not in secs:
            secs.append((r['section'], r['section_name']))
    tot = {h: [0, 0] for h in hdr}
    for sec, sname in secs:
        cells = []
        ids = [r['id'] for r in ours if r['section'] == sec]
        for h, src in [('ours', O)] + [(n, eng.get(k)) for k, n in ENGINES]:
            if src is None:
                cells.append('-')
                continue
            sc = [src[i] for i in ids if i in src and src[i]['category'] != 'NA']
            p = sum(1 for r in sc if r['category'] == 'PASS')
            tot[h][0] += p
            tot[h][1] += len(sc)
            cells.append('%d / %d' % (p, len(sc)))
        w('| %s %s | %s |' % (sec, sname, ' | '.join(cells)))
    w('| **total** | %s |' % ' | '.join(('**%d / %d**' % tuple(tot[h])) if tot[h][1] else '-' for h in hdr))
    w('')
    # ------------------------------------------------------------ roadmap
    gaps = gaps_from_report(os.path.join(WESTER, 'REPORT.md'))
    gap_of = {}
    for num, name, ids in gaps:
        for i in ids:
            gap_of.setdefault(i, (num, name))
    if 'giac' in eng:
        G = eng['giac']
        gp = [i for i in order if i in G and G[i]['category'] == 'PASS' and O[i]['category'] != 'PASS']
        w('## Giac passes, we do not: the roadmap to beating the HP Prime (%d problems)' % len(gp))
        w('')
        w('Grouped by the capability gaps of `../REPORT.md` ("Gaps by capability"; a problem listed under '
          'several gaps is shown under the first). Our category is in parentheses. A `*` marks a problem '
          'that `problems.txt` poses no input for (our CAS has no such command); the Giac input comes '
          'from `giac/overrides.txt`.')
        w('')
        groups = {}
        for i in gp:
            groups.setdefault(gap_of.get(i, (999, 'not listed in a REPORT.md gap')), []).append(i)
        w('| gap | # | problems (our category) |')
        w('|---|---:|---|')
        for key in sorted(groups):
            num, name = key
            w('| %s | %d | %s |' % ('%d %s' % (num, name) if num != 999 else name, len(groups[key]),
                                    ', '.join('%s%s (%s)' % (i, '' if O[i].get('input') else '*', O[i]['category'])
                                              for i in groups[key])))
        w('')
        w('<details><summary>Giac\'s answers for these problems</summary>')
        w('')
        w('| id | problem | our output | Giac output |')
        w('|---|---|---|---|')
        for i in gp:
            w('| %s | %s | `%s` | `%s` |' % (i, esc(O[i]['title'], 60), esc(O[i]['output'], 60), esc(G[i]['output'], 70)))
        w('')
        w('</details>')
        w('')
        op = [i for i in order if i in G and O[i]['category'] == 'PASS' and G[i]['category'] != 'PASS']
        w('## We pass, Giac does not (%d problems)' % len(op))
        w('')
        w('| id | problem | Giac category | Giac output / note |')
        w('|---|---|---|---|')
        for i in op:
            w('| %s | %s | %s | `%s` %s |' % (i, esc(O[i]['title'], 60), G[i]['category'],
                                             esc(G[i]['output'] or G[i]['engine_msg'], 60), esc(G[i]['note'], 60)))
        w('')
        both = sum(1 for i in order if i in G and O[i]['category'] == 'PASS' and G[i]['category'] == 'PASS')
        w('Both pass: %d. Giac only: %d. Ours only: %d.' % (both, len(gp), len(op)))
        w('')
    # ------------------------------------------------------------ WRONG answers
    w('## WRONG answers per engine')
    w('')
    for nm, src in [('ours', O)] + [(n, eng.get(k)) for k, n in ENGINES]:
        if src is None:
            continue
        wr = [i for i in order if i in src and src[i]['category'] == 'WRONG']
        w('### %s (%d)' % (nm, len(wr)))
        w('')
        if nm == 'ours':
            w('See `../REPORT.md` ("WRONG and CRASH results") for the analysis of each.')
            w('')
        w('| id | problem | output | expected | note |')
        w('|---|---|---|---|---|')
        for i in wr:
            r = src[i]
            ck = r.get('check', 'exact')
            exp = esc(r['expect'], 50) if r['expect'] and ck.split()[0] not in (
                'ode', 'odesys', 'nlsys', 'linsys', 'satisfy', 'antideriv', 'eigvecs') else '(check: %s)' % ck
            w('| %s | %s | `%s` | %s | %s |' % (i, esc(r['title'], 50), esc(r['output'], 70),
                                               ('`%s`' % exp) if not exp.startswith('(') else exp, esc(r['note'], 80)))
        w('')
    # ------------------------------------------------------------ crashes
    for k, n in ran:
        cr = [i for i in order if eng[k][i]['category'] == 'CRASH']
        if cr:
            w('%s crashed (signal) on: %s.' % (n, ', '.join(cr)))
            w('')
    # ------------------------------------------------------------ uncertain
    w('## Translations marked uncertain')
    w('')
    w('| engine | id | why |')
    w('|---|---|---|')
    for k, n in ENGINES:
        rs = load(os.path.join(HERE, k, 'results.json')) if k in eng else None
        if rs:
            for r in rs:
                if r.get('uncertain'):
                    w('| %s | %s | %s |' % (n, r['id'], esc(r['uncertain'], 200)))
    w('')
    flips = ours_guard_flips(ours)
    w('## Grader adjustments used for the engines')
    w('')
    w('All grading goes through `run_wester.grade()`. `run_baselines.py` adds, for the engines only:')
    w('')
    w('- `piecewise(c1,v1,...,default)` is parsed correctly. `run_wester`\'s `_piecewise` passes (cond, value) '
      'pairs to `sp.Piecewise`, which expects (value, cond). Our CAS never prints `piecewise`, so this only '
      'shows up for the engines.')
    w('- ODE systems: the free constants are counted in the solution, not in the residuals. The residuals '
      'cancel exactly, so `run_wester` counts 0 constants. Our CAS solves no systems, so it is unaffected.')
    w('- `form` checks also require the printed output to be no bigger than the expected form. Otherwise SymPy\'s '
      'own evaluation does the simplification: `10*(1029/1000)^(1/3)/7` parses to `3^(1/3)`, and '
      '`sqrt(997)-991026973^(1/6)` parses to `0`.')
    w('- `simp`/`form`/`factor` checks: an output identical to the argument of the outer `simplify(...)` is '
      'UNEVAL. The size check alone would pass `cos(3*x)/cos(x)` unchanged.')
    w('- An undecided `antideriv` check (outputs with `floor`, `sign`, ...) falls back to central differences at '
      'sample points.')
    w('- A conditional answer `piecewise(cond, value, <unevaluated sum/integral>)` is graded on the branch that '
      'holds at every sample point of the problem\'s domain, and the note says so.')
    w('- A one-element list is unwrapped when the expected answer is not a list. Giac\'s rsolve returns `[f]`.')
    w('- 2F1 values in numeric (`value`/`rel`) checks are evaluated, not treated as unevaluated.')
    w('')
    if flips:
        w('Applied to our own PASSes, the two output guards would reclassify: %s. They are left as PASS in '
          '`../results.json`; keep this in mind when comparing those rows.'
          % ', '.join('%s (%s)' % f for f in flips))
    else:
        w('Applied to our own PASSes, the two output guards change nothing. The comparison is like for like.')
    w('')
    w(METHOD)
    open(os.path.join(HERE, 'COMPARISON.md'), 'w').write('\n'.join(L).rstrip() + '\n')
    print('wrote COMPARISON.md')


if __name__ == '__main__':
    main()
