#!/usr/bin/env python3
"""
Size of our antiderivatives against a reference: ranks integration results by
leaf-count ratio (ours / reference) so the ugliest common forms come first.

  intsize.py [--rubi SUBSTR[,SUBSTR...]] [--difftest] [--repl PATH] [-j N]
             [--top N] [--save OUT.json] [--compare OLD.json]

Sources:
  --rubi      Rubi cases (build/suites/rubi/cases.jsonl, `make suites-translate`)
              whose file name contains one of the substrings; reference = Rubi's
              optimal antiderivative.  "student" = the independent test suites
              (Apostol, Stewart, Timofeev, ...), rational and algebraic functions,
              exponentials, logarithms and the basic trig chapters.
  --difftest  the integrate categories of tests/difftest.py (integrate,
              h_integrate, t3_integrate, rand_integrate, rand_ratint); reference =
              SymPy's integrate (cached in build/intsize_sympy.json).

Leaf count: symbols, numbers and function names of the printed form (operators
not counted).  Only results without an unevaluated integrate are measured; the
correctness of the results is graded elsewhere (difftest, suites).  --save
writes {id: [ours, ref, output]}; --compare reports, per case present in both,
how many got smaller or larger, and the largest changes.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, 'suites', 'lib'))
import suitelib as S  # noqa: E402

STUDENT = ['0 Independent', '1.3.1 Rational', '1.3.2 Algebraic', '1.2.1.1 (a+b x+c x^2)^p', '2.3 Exponential',
           '3.5 Logarithm', '4.1.0 ', '4.2.0 ', '4.3.0 ', '4.5.0 ', '5.1.1 ', '5.3.1 ']
TOK = re.compile(r'%?[A-Za-z_][A-Za-z_0-9]*|\d+\.?\d*(?:[eE][-+]?\d+)?')


def leaves(s):
    return len(TOK.findall(s))


def rubi_cases(subs):
    if subs == ['student']:
        subs = STUDENT
    out = []
    for c in S.load_cases(os.path.join(ROOT, 'build', 'suites', 'rubi', 'cases.jsonl')):
        if c.get('na') or c.get('unsupported') or not c.get('src_expect'):
            continue
        if any(s in c['file'] for s in subs):
            out.append({'id': c['id'], 'lines': c['lines'], 'ref': c['src_expect']})
    return out


def _sympy_ref(f):
    import sympy as sp
    sys.path.insert(0, HERE)
    import difftest as D
    r = sp.integrate(D.to_sympy(f), D.x)
    if r.has(sp.Integral):
        return None
    return sp.sstr(r).replace('**', '^')


def difftest_cases():
    import random
    sys.path.insert(0, HERE)
    import difftest as D
    cats = ('integrate', 'h_integrate', 't3_integrate', 'rand_integrate', 'rand_ratint')
    tests = D.curated() + D.hard_cases() + D.random_cases(random.Random(1), 60)
    tests = [t for t in tests if t['cat'] in cats]
    cpath = os.path.join(ROOT, 'build', 'intsize_sympy.json')
    cache = json.load(open(cpath)) if os.path.exists(cpath) else {}
    out = []
    for i, t in enumerate(tests):
        f = t['f']
        if f not in cache:
            try:
                cache[f] = S.isolated(_sympy_ref, f, timeout=60)
            except Exception:
                cache[f] = None
            with open(cpath, 'w') as fh:
                json.dump(cache, fh, indent=0)
        if cache[f]:
            out.append({'id': '%s/%d' % (t['cat'], i), 'lines': [t['input']], 'ref': cache[f]})
    return out


def pct(v, q):
    v = sorted(v)
    return v[min(len(v) - 1, int(q * len(v)))] if v else float('nan')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rubi', default=None)
    ap.add_argument('--difftest', action='store_true')
    ap.add_argument('--repl', default=os.path.join(ROOT, 'build', 'cas_repl'))
    ap.add_argument('-j', type=int, default=3)
    ap.add_argument('--timeout', type=int, default=4000)
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--save', default=None)
    ap.add_argument('--compare', default=None)
    a = ap.parse_args()
    cases = []
    if a.rubi:
        cases += rubi_cases(a.rubi.split(','))
    if a.difftest:
        cases += difftest_cases()
    runs = S.run_cases(cases, a.repl, timeout_ms=a.timeout, jobs=a.j, chunk=50, progress=False)
    res = {}
    for c in cases:
        out = runs.get(c['id'], {}).get('out', '')
        if not out or out.startswith('ERROR') or 'integrate(' in out:
            continue
        res[c['id']] = [leaves(out), leaves(c['ref']), out, c['lines'][-1], c['ref']]
    ratios = [o / max(1, r) for o, r, *_ in res.values()]
    print('measured %d of %d cases: leaf ratio median %.2f, p90 %.2f, mean %.2f; ratio > 2: %d, > 4: %d'
          % (len(ratios), len(cases), pct(ratios, 0.5), pct(ratios, 0.9), sum(ratios) / max(1, len(ratios)),
             sum(r > 2 for r in ratios), sum(r > 4 for r in ratios)))
    worst = sorted(res.items(), key=lambda kv: -kv[1][0] / max(1, kv[1][1]))
    for cid, (o, r, out, inp, ref) in worst[:a.top]:
        print('%5.2f %4d/%-4d %s\n      ours %s\n      ref  %s' % (o / max(1, r), o, r, inp, out[:300], ref[:300]))
    if a.save:
        with open(a.save, 'w') as fh:
            json.dump({k: v[:3] for k, v in res.items()}, fh, indent=0)
    if a.compare:
        old = json.load(open(a.compare))
        common = [k for k in res if k in old]
        sm = [k for k in common if res[k][0] < old[k][0]]
        lg = [k for k in common if res[k][0] > old[k][0]]
        r0 = [old[k][0] / max(1, old[k][1]) for k in common]
        r1 = [res[k][0] / max(1, res[k][1]) for k in common]
        print('compare (%d common): smaller %d, larger %d; median %.2f -> %.2f, p90 %.2f -> %.2f; '
              'only before %d, only now %d' % (len(common), len(sm), len(lg), pct(r0, .5), pct(r1, .5),
                                                pct(r0, .9), pct(r1, .9), len(set(old) - set(res)),
                                                len(set(res) - set(old))))
        for k in sorted(lg, key=lambda k: old[k][0] - res[k][0])[:15]:
            print('  larger %d -> %d: %s\n      was %s\n      now %s' % (old[k][0], res[k][0], k, old[k][2][:200],
                                                                    res[k][2][:200]))


if __name__ == '__main__':
    main()
