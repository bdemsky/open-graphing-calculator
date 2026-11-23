#!/usr/bin/env python3
"""
Baseline runs of the Wester benchmark on other CAS engines (Giac, SymPy,
Maxima), graded by the same SymPy-based checker as our CAS
(../run_wester.py: grade(), the same categories and the same per-problem
expected answers, checks and sampling domains).

For each problem, the input in our syntax (../problems.txt) is translated to
the engine's syntax (engine_<name>.py: rename table plus call rewrites), or
taken from the per-engine override table <name>/overrides.txt. The input runs
in a fresh engine process with a wall-clock timeout. The engine output is then
translated back to our syntax and graded.

Classification (as in run_wester.py, adapted to the engines):
  UNSUPPORTED  the engine has no such function (detected at run time: Giac
               type()=identifier, SymPy/Maxima no translation or an unknown
               name), or the override table says there is no equivalent, or
               the problem has no input at all (problems.txt 'unsupported:'
               and no engine override: counted as "not attempted")
  UNEVAL       output contains an unevaluated integrate/sum/limit/...; the
               engine raised a "not implemented" error; the output equals
               the input; floats where an exact answer is required
  ERROR        any other engine error
  NA           problems.txt 'na:' (excluded from scoring, as for our CAS)
Our CAS's alt:/override: lines are NOT used for the engines; each engine has
its own alt:/override: lines in its overrides.txt (with the reason).

Usage: run_baselines.py [--engine giac,sympy,maxima] [--only IDS|SECTIONS]
                        [-j N] [--timeout SEC] [--compare-only]
Writes <engine>/results.json, <engine>/results.md, <engine>/translated.txt
and COMPARISON.md.
"""
import argparse
import json
import os
import random
import re
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
WESTER = os.path.dirname(HERE)
sys.path.insert(0, WESTER)
sys.path.insert(0, HERE)

import sympy as sp                                     # noqa: E402
import run_wester as W                                 # noqa: E402
from syntax import split_top, IDENT                     # noqa: E402

CATS = W.CATS

# Output names the engines produce that the grader must be able to parse.
W.LOCAL.update({
    'Not': sp.Not, 'Ne': sp.Ne, 'surd': lambda a_, n_: sp.real_root(a_, n_), 'bessely': sp.bessely,
    'besseli': sp.besseli, 'besselk': sp.besselk, 'erfc': sp.erfc, 'expint': sp.expint,
    'uppergamma': sp.uppergamma, 'lowergamma': sp.lowergamma, 'li': sp.li, 'Shi': sp.Shi, 'Chi': sp.Chi,
    'fresnels': sp.fresnels, 'fresnelc': sp.fresnelc, 'elliptic_k': sp.elliptic_k,
    'elliptic_pi': sp.elliptic_pi, 'hermite': sp.hermite, 'laguerre': sp.laguerre, 'jacobi': sp.jacobi,
    'gegenbauer': sp.gegenbauer, 'airyai': sp.airyai, 'airybi': sp.airybi, 'harmonic': sp.harmonic,
    'loggamma': sp.loggamma, 'lerchphi': sp.lerchphi, 'Mod': sp.Mod, 'beta': sp.beta, 'Beta': sp.beta,
    'ff': sp.ff, 'catalan': sp.catalan, 'chebyshevu': sp.chebyshevu, 'sinc': sp.sinc,
    'stirling': lambda n_, k_: sp.functions.combinatorial.numbers.stirling(n_, k_),
    'erf2': sp.erf2, 'erfinv': sp.erfinv, 'cosh': sp.cosh, 'coth': sp.coth, 'acoth': sp.acoth,
    'asech': sp.asech, 'acsch': sp.acsch, 'Ynm': sp.Ynm, 'dirichlet_eta': sp.dirichlet_eta,
    'betainc_regularized': sp.betainc_regularized,
})



def _piecewise(*args):
    """piecewise(c1,v1,c2,v2,...,default) as a SymPy Piecewise.  run_wester's own
    _piecewise passes (cond, value) pairs to sp.Piecewise, which wants
    (value, cond); our CAS never prints piecewise, so it only matters here."""
    args = list(args)
    pairs = []
    while len(args) >= 2:
        c_, v_ = args.pop(0), args.pop(0)
        pairs.append((v_, c_))
    pairs.append((args[0] if args else sp.nan, True))
    return sp.Piecewise(*pairs)


W.LOCAL['piecewise'] = _piecewise


def _grade_odesys(prob, ours, rng, chk):
    """run_wester.grade_odesys, but the free constants are counted in the
    solution itself: its count on the residuals finds none when the residuals
    cancel exactly (our CAS solves no ODE systems, so it never hit this)."""
    var = sp.Symbol(prob.get('var', 't'))
    d = W.sol_dict(ours)
    env = {'sp': sp, 't': var, 'q': sp.Symbol('q')}
    for nm in 'uvw':
        env[nm.upper()] = d.get(nm, sp.Integer(0))
    res = eval(prob['expect'], env)
    consts = {str(s_) for e_ in d.values() for s_ in sp.sympify(e_).free_symbols if re.match(r'^c\d+$', str(s_))}
    for r_ in res:
        if W.equiv(sp.sympify(r_), sp.Integer(0), rng, dict(prob['domain'])) is not True:
            return W.Result('WRONG', 'does not satisfy the system')
    if len(consts) < int(chk[1]):
        return W.Result('WRONG', 'only %d of %s constants' % (len(consts), chk[1]))
    return W.Result('PASS')


W.grade_odesys = _grade_odesys


def antideriv_fd(prob, out, rng):
    """fallback for an undecided antiderivative check (outputs with floor,
    sign, ...): central differences of the output at sample points, compared
    with the integrand.  True / False / None"""
    var = sp.Symbol(prob.get('var', 'x'))
    F = W.to_sympy(out)
    f = W.to_sympy(prob['expect'])
    syms = [s_ for s_ in W.free_syms(F, f) if s_ != var]
    good = 0
    for _ in range(24):
        if good >= 6:
            break
        sub = {s_: W._sample(s_, rng, prob['domain']) for s_ in syms}
        x0 = W._sample(var, rng, prob['domain'])
        h = sp.Rational(1, 10 ** 8)
        try:
            Fs = F.subs(sub)
            d = (sp.N(Fs.subs(var, x0 + h), 40) - sp.N(Fs.subs(var, x0 - h), 40)) / (2 * h)
            d, fv = complex(sp.N(d, 20)), complex(sp.N(f.subs(sub).subs(var, x0), 20))
        except Exception:
            continue
        if any(q_ != q_ or abs(q_) == float('inf') for q_ in (d.real, d.imag, fv.real, fv.imag)):
            continue
        if abs(d - fv) > 1e-6 * (1 + abs(fv)):
            return False
        good += 1
    return True if good >= 3 else None

ENGINE_NAMES = ['giac', 'sympy', 'maxima']
OVERRIDE_KEYS = ('expect', 'check', 'var', 'eq', 'need', 'tol', 'slack', 'points', 'floats', 'ic')


def load_engine(name):
    if name == 'giac':
        import engine_giac as e
    elif name == 'sympy':
        import engine_sympy as e
    elif name == 'maxima':
        import engine_maxima as e
    else:
        raise SystemExit('unknown engine ' + name)
    return e


# -------------------------------------------------------------- overrides ---
def load_overrides(path):
    """== ID blocks; keys: in pre expect check ... unsupported uncertain alt override note"""
    ovs, cur = {}, None
    if not os.path.exists(path):
        return ovs
    for raw in open(path):
        line = raw.rstrip('\n')
        if line.startswith('#') or not line.strip():
            continue
        m = re.match(r'^==\s*(\S+)', line)
        if m:
            cur = ovs.setdefault(m.group(1), {'pre': [], 'alt': [], 'domain': {}, 'allow': set()})
            continue
        m = re.match(r'^\s*(\w+):\s?(.*)$', line)
        if not m or cur is None:
            raise SystemExit('bad line in %s: %s' % (path, line))
        key, val = m.group(1), m.group(2).strip()
        if key == 'pre':
            cur['pre'].append(val)
        elif key == 'in' and cur.get('in') is not None:
            cur['pre'].append(cur['in'])
            cur['in'] = val
        elif key == 'alt':
            cat, valx, note = [q.strip() for q in val.split('|', 2)]
            cur['alt'].append((cat, valx, note))
        elif key == 'allow':
            cur['allow'] |= set(val.split())
        elif key == 'domain':
            for item in val.split():
                nm, d = item.split('=')
                if ':' in d:
                    lo, hi = d.split(':')
                    d = (float(sp.N(W.to_sympy(lo))), float(sp.N(W.to_sympy(hi))))
                cur['domain'][nm] = d
        elif key in ('note', 'uncertain') and key in cur:
            cur[key] += ' ' + val
        else:
            cur[key] = val
    return ovs


def effective_problem(prob, ov):
    p = dict(prob)
    p['alt'] = list(ov.get('alt', []))
    p['allow'] = set(prob['allow']) | ov.get('allow', set())
    p['domain'] = dict(prob['domain'])
    p['domain'].update(ov.get('domain', {}))
    p.pop('override', None)
    if 'override' in ov:
        p['override'] = ov['override']
    for k in OVERRIDE_KEYS:
        if k in ov:
            p[k] = ov[k]
    return p


# --------------------------------------------------------- classification ---
UNEVAL_EXTRA = {'hypergeom', 'meijerg', 'subst', 'Integral', 'Sum', 'Limit', 'Product', 'RootOf', 'CRootOf',
                'ConditionSet', 'nounform', 'intersect', 'union', 'setdiff', 'LaplaceTransform',
                'InverseLaplaceTransform', 'FourierTransform', 'MellinTransform', 'RootSum'}
W.LOCAL.update({'And': sp.And, 'Or': sp.Or, 'Eq': sp.Eq})
for _nm in UNEVAL_EXTRA:
    W.LOCAL.setdefault(_nm, sp.Function(_nm))
W.LOCAL['hypergeom'] = lambda a_, b_, z_: sp.hyper(a_, b_, z_)


def pick_branch(prob, out, rng):
    """A conditional answer piecewise(c1,v1,...,fallback) whose fallback is
    unevaluated (SymPy: 'this value when n > -1, else the unevaluated sum'):
    if the same evaluated branch applies at every sample point of the
    problem's domain, return that branch (printed in our syntax), else None."""
    import sympy_worker
    try:
        e = W.to_sympy(out)
    except Exception:
        return None
    if not isinstance(e, sp.Piecewise):
        return None
    syms = W.free_syms(e)
    picks = set()
    for _ in range(12):
        sub = {s_: W._sample(s_, rng, prob['domain']) for s_ in syms}
        for j, (val, cond) in enumerate(e.args):
            try:
                c = cond.subs(sub)
                if c not in (sp.true, sp.false):
                    c = sp.true if bool(sp.N(c)) else sp.false
            except Exception:
                return None
            if c is sp.true:
                picks.add(j)
                break
        else:
            return None
    if len(picks) != 1:
        return None
    val = e.args[picks.pop()][0]
    s = sympy_worker.show(val)
    if any(nm in s for nm in ('integrate(', 'sum(', 'product(', 'limit(')):
        return None
    return s


def _single_list(out):
    o = out.strip()
    if o.startswith('[') and o.endswith(']') and not o.startswith('[['):
        inner = o[1:-1]
        try:
            if inner.strip() and len(split_top(inner, ',')) == 1 and W.outer_parens('(' + inner + ')'):
                return inner.strip()
        except Exception:
            return None
    return None


def unchanged(prob, out):
    """the output is the argument of the outermost command (simplify(X) -> X),
    compared as unevaluated parses; the size-based simp check would pass it
    whenever X is already small (cos(3x)/cos(x))"""
    m = re.match(r'^\s*(simplify|normal|trigsimp|factor|expand|tcollect|texpand)\((.*)\)\s*$', prob.get('in') or '')
    if not m or len(split_top(m.group(2))) != 1:
        return False
    try:
        a_ = W.to_sympy(m.group(2), evaluate=False)
        o_ = W.to_sympy(out, evaluate=False)
        e_ = W.to_sympy(prob['expect'], evaluate=False)
        if sp.srepr(a_) == sp.srepr(e_):
            return False
        return sp.srepr(a_) == sp.srepr(o_)
    except Exception:
        return False


def classify(prob, run, rng):
    if run['status'] == 'timeout':
        return W.Result('TIMEOUT', 'wall-clock timeout')
    if run['status'] == 'crash':
        return W.Result('CRASH', run.get('msg', ''))
    if run['status'] == 'unsupported':
        return W.Result('UNSUPPORTED', run.get('msg', ''))
    chk = prob.get('check', 'exact')
    if run['status'] in ('error', 'uneval'):
        if chk.startswith('error'):
            return W.Result('PASS', 'error expected: ' + run.get('msg', '')[:150])
        return W.Result('UNEVAL' if run['status'] == 'uneval' else 'ERROR', run.get('msg', '')[:300])
    out = run['out'].strip()
    for cat, val, note in prob['alt']:
        if out.replace(' ', '') == val.replace(' ', ''):
            return W.Result(cat, note)
    if chk.startswith('error'):
        if out in ('undef', 'inf', '-inf'):
            return W.Result('PASS', 'divergence reported as ' + out)
        return W.Result('WRONG', 'expected an error/divergence')
    allowed = prob['allow']
    names = set(re.findall(r'([A-Za-z_][A-Za-z_0-9]*)\(', out))
    unev = {nm for nm in names if (nm in W.UNEVAL_NAMES or nm in UNEVAL_EXTRA) and nm not in allowed}
    if chk.split()[0] in ('value', 'rel'):
        unev -= {'hypergeom', 'meijerg'}       # a numeric value through 2F1 is still a value
    cond_note = ''
    if out.startswith('piecewise(') and 'piecewise' not in prob.get('expect', ''):
        br = pick_branch(prob, out, random.Random(7))
        if br is not None:
            out = br
            cond_note = ' (conditional answer: the branch that holds on the problem domain)'
            names = set(re.findall(r'([A-Za-z_][A-Za-z_0-9]*)\(', out))
            unev = {nm for nm in names if (nm in W.UNEVAL_NAMES or nm in UNEVAL_EXTRA) and nm not in allowed}
    if unev:
        return W.Result('UNEVAL', 'unevaluated ' + ','.join(sorted(unev)))
    unknown = {nm for nm in names if nm not in W.LOCAL and nm not in allowed and len(nm) > 1
               and nm not in W.BUILTINS}
    if unknown:
        return W.Result('UNGRADED', 'output uses unknown function(s) ' + ','.join(sorted(unknown)))
    if out.replace(' ', '') == run.get('echo', '\0').replace(' ', ''):
        return W.Result('UNEVAL', 'returned unevaluated (output = input)')
    kind = chk.split()[0]
    if kind in ('simp', 'form', 'factor') and unchanged(prob, out):
        return W.Result('UNEVAL', 'returned unchanged (output = argument of %s)' % prob['in'].split('(')[0])
    if kind in ('exact', 'simp', 'form', 'value', 'rel', 'factor', 'terms', 'rect', 'antideriv') and \
            not prob.get('expect', '').strip().startswith('['):
        one = _single_list(out)
        if one is not None:
            out = one
    try:
        res = W.grade(prob, out, rng)
        if kind == 'form' and res.cat == 'PASS' and not out.startswith('['):
            # the form check compares SymPy's evaluated parses, so SymPy's own
            # evaluation can do the simplification (10*(1029/1000)^(1/3)/7 ->
            # 3^(1/3), sqrt(997)-991026973^(1/6) -> 0); require the printed
            # output to be no bigger than the expected form as well
            o_ops = W.ops(W.to_sympy(out, evaluate=False))
            e_ops = W.ops(W.to_sympy(prob['expect'], evaluate=False))
            if int(o_ops) > int(e_ops):
                res = W.Result('UNEVAL', 'equivalent but not in the requested form (SymPy\'s evaluation of the '
                                         'output did the simplification)')
        if kind == 'antideriv' and res.cat == 'UNGRADED':
            r2 = antideriv_fd(prob, out, rng)
            if r2 is not None:
                res = W.Result('PASS' if r2 else 'WRONG', 'derivative checked by central differences')
        if cond_note:
            res = W.Result(res.cat, (res.note + cond_note).strip())
        return res
    except W.Timeout:
        return W.Result('UNGRADED', 'grader timeout')
    except Exception as ex:
        return W.Result('UNGRADED', 'grader exception: %s' % (str(ex)[:150]))


# ------------------------------------------------------------------- main ---
def run_engine(name, probs, args):
    eng = load_engine(name)
    ok, info = eng.available()
    edir = os.path.join(HERE, name)
    os.makedirs(edir, exist_ok=True)
    if not ok:
        print('%s: not available: %s' % (name, info))
        json.dump({'engine': name, 'available': False, 'info': info}, open(os.path.join(edir, 'status.json'), 'w'),
                  indent=1)
        # still write the translations, for review before the engine is installed
        ovs = load_overrides(os.path.join(edir, 'overrides.txt'))
        recs = []
        for p in probs:
            ov = ovs.get(p['id'], {})
            if 'na' in p or (p.get('in') is None and ov.get('in') is None):
                continue
            t = eng.translate(p, ov) if not ov.get('unsupported') else {'lines': [], 'notes': []}
            recs.append({'id': p['id'], 'pre': p['pre'], 'input': p.get('in') or '', 'translated': t.get('lines', []),
                         'translation_notes': t.get('notes', []) + (['UNSUPPORTED: ' + t['unsupported']]
                                                                     if t.get('unsupported') else []),
                         'override_note': ov.get('note', ''), 'uncertain': ov.get('uncertain', '')})
        write_translated(name, recs, os.path.join(edir, 'translated.txt'))
        return None
    print('%s: %s' % (name, info), flush=True)
    ovs = load_overrides(os.path.join(edir, 'overrides.txt'))
    jobs = {}
    trans = {}
    for p in probs:
        ov = ovs.get(p['id'], {})
        if 'na' in p:
            continue
        if ov.get('unsupported'):
            continue
        if p.get('in') is None and ov.get('in') is None:
            continue
        t = eng.translate(p, ov)
        trans[p['id']] = t
        if t.get('unsupported'):
            continue
        jobs[p['id']] = (p, t)

    def work(item):
        p, t = item
        return eng.run(t, effective_problem(p, ovs.get(p['id'], {})), args.timeout)

    with ThreadPoolExecutor(max_workers=args.j) as ex:
        runs = dict(zip(jobs.keys(), ex.map(work, jobs.values())))
    results = []
    for p in probs:
        rng = random.Random(args.seed)
        ov = ovs.get(p['id'], {})
        ep = effective_problem(p, ov)
        t = trans.get(p['id'], {})
        run = {'out': '', 'raw': '', 'time': 0}
        attempted = True
        if 'na' in p:
            res = W.Result('NA', p['na'])
            attempted = False
        elif ov.get('unsupported'):
            res = W.Result('UNSUPPORTED', ov['unsupported'])
        elif p.get('in') is None and ov.get('in') is None:
            res = W.Result('UNSUPPORTED', 'not attempted: no input in problems.txt (%s)' % p.get('unsupported', ''))
            attempted = False
        elif t.get('unsupported'):
            res = W.Result('UNSUPPORTED', t['unsupported'])
        else:
            run = runs[p['id']]
            run.setdefault('echo', t['lines'][-1] if t.get('lines') else '')
            res = classify(ep, run, rng)
        if ep.get('override'):
            cat, why = [q.strip() for q in ep['override'].split('|', 1)]
            if cat != res.cat:
                res = W.Result(cat, why + ' (auto: %s %s)' % (res.cat, res.note))
        rec = {'id': p['id'], 'section': p['section'], 'section_name': p['section_name'], 'title': p['title'],
               'input': p.get('in') or '', 'pre': p['pre'], 'translated': t.get('lines', []),
               'translation_notes': t.get('notes', []), 'uncertain': ov.get('uncertain', ''),
               'override_note': ov.get('note', ''), 'expect': ep.get('expect', ''),
               'check': ep.get('check', 'exact'), 'raw': run.get('raw', ''), 'output': run.get('out', ''),
               'engine_msg': run.get('msg', ''), 'category': res.cat, 'note': res.note,
               'attempted': attempted, 'time': round(run.get('time', 0), 3)}
        results.append(rec)
        if args.verbose or res.cat not in ('PASS', 'NA'):
            print('%-6s %-11s %s\n       in:  %s\n       out: %s\n       exp: %s   %s' % (
                p['id'], res.cat, p['title'], ' ; '.join(t.get('lines', [])), (run.get('raw') or '')[:300],
                ep.get('expect', ''), res.note), flush=True)
    if args.only:
        old = os.path.join(edir, 'results.json')
        if os.path.exists(old):
            keep = {r['id']: r for r in json.load(open(old))}
            for r in results:
                keep[r['id']] = r
            order = [p['id'] for p in W.load_problems(W.PROBLEMS)]
            results = [keep[i] for i in order if i in keep]
    json.dump(results, open(os.path.join(edir, 'results.json'), 'w'), indent=1)
    json.dump({'engine': name, 'available': True, 'info': info, 'timeout_s': args.timeout},
              open(os.path.join(edir, 'status.json'), 'w'), indent=1)
    write_md(name, info, results, os.path.join(edir, 'results.md'))
    write_translated(name, results, os.path.join(edir, 'translated.txt'))
    return results


def write_md(name, info, results, path):
    secs = []
    for r in results:
        if (r['section'], r['section_name']) not in secs:
            secs.append((r['section'], r['section_name']))
    tot = {c: 0 for c in CATS}
    lines = ['# Wester benchmark baseline: %s' % name, '', 'engine: `%s`' % info, '',
             'Graded with `../run_wester.py` grade(); see `../COMPARISON.md`.', '',
             '| section | ' + ' | '.join(CATS) + ' | total |', '|---' * (len(CATS) + 2) + '|']
    for sec, nm in secs:
        cnt = {c: 0 for c in CATS}
        for r in results:
            if r['section'] == sec:
                cnt[r['category']] += 1
                tot[r['category']] += 1
        lines.append('| %s %s | ' % (sec, nm) + ' | '.join(str(cnt[c]) for c in CATS) + ' | %d |' % sum(cnt.values()))
    lines.append('| **total** | ' + ' | '.join('**%d**' % tot[c] for c in CATS) + ' | **%d** |' % sum(tot.values()))
    lines.append('')
    for cat in CATS:
        rs = [r for r in results if r['category'] == cat]
        if not rs or cat == 'NA':
            continue
        lines += ['## %s (%d)' % (cat, len(rs)), '']
        for r in rs:
            lines.append('- **%s** %s -- `%s` -> `%s` (expected `%s`) %s%s' % (
                r['id'], r['title'], ' ; '.join(r['translated'])[:300] or r['input'], (r['raw'] or '')[:200],
                r['expect'], r['note'], (' [translation uncertain: %s]' % r['uncertain']) if r['uncertain'] else ''))
        lines.append('')
    open(path, 'w').write('\n'.join(lines) + '\n')
    print('\n'.join(lines[:len(secs) + 9]))


def write_translated(name, results, path):
    out = ['# %s translations of problems.txt (generated by run_baselines.py)' % name,
           '# id | our input  =>  engine input lines | notes', '']
    for r in results:
        if not r['translated']:
            continue
        ours = ' ; '.join(r['pre'] + [r['input']])
        out.append('== %s' % r['id'])
        out.append('ours:   %s' % ours)
        for l in r['translated']:
            out.append('%s:  %s' % (name, l))
        for n in r['translation_notes']:
            if n != 'override':
                out.append('note:   %s' % n)
        if 'override' in r['translation_notes']:
            out.append('note:   per-problem override (%s/overrides.txt)%s' % (
                name, (': ' + r['override_note']) if r['override_note'] else ''))
        if r['uncertain']:
            out.append('UNCERTAIN: %s' % r['uncertain'])
        out.append('')
    open(path, 'w').write('\n'.join(out) + '\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--engine', default=','.join(ENGINE_NAMES))
    ap.add_argument('--only', default=None)
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('--timeout', type=float, default=30.0, help='wall-clock seconds per problem')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--verbose', '-v', action='store_true')
    ap.add_argument('--compare-only', action='store_true')
    args = ap.parse_args()
    if not args.compare_only:
        probs = W.load_problems(W.PROBLEMS)
        if args.only:
            sel = set(args.only.split(','))
            probs = [p for p in probs if p['id'] in sel or p['section'] in sel]
        for name in args.engine.split(','):
            run_engine(name, probs, args)
    import compare
    compare.main()


if __name__ == '__main__':
    main()
