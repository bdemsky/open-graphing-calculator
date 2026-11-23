#!/usr/bin/env python3
"""
Run the third-party test suites against build/cas_repl (or the ASan/UBSan
build with --asan) and grade them.

  run_suites.py [--suite maxima,sympy,giac,rubi] [--asan] [-j N]
                [--timeout MS] [--sample N] [--rerun-bad]

For each suite, <suite>/translate.py (or extract.py) has produced
build/suites/<suite>/cases.jsonl (the Makefile target `suites` runs fetch +
translate first).  Writes build/suites/<suite>/results[_asan].json (full) and
tests/suites/<suite>/results[_asan].md (totals, every WRONG/CRASH/TIMEOUT ...).
--sample N runs every N-th runnable case only (used for the slow ASan pass
over Rubi).
"""
import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'lib'))
import suitelib as S  # noqa: E402

BUILD = os.path.join(S.ROOT, 'build', 'suites')
SUITES = ['maxima', 'sympy', 'giac', 'rubi']
# per-suite defaults: per-line timeout (ms) and chunk size
DEFAULTS = {'rubi': (4000, 50), 'maxima': (8000, 30), 'sympy': (8000, 30), 'giac': (8000, 30)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--suite', default=','.join(SUITES))
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('--repl', default=None)
    ap.add_argument('-j', type=int, default=S.MAX_JOBS, help='parallel jobs (capped at %d)' % S.MAX_JOBS)
    ap.add_argument('--timeout', type=int, default=None)
    ap.add_argument('--sample', type=int, default=1)
    ap.add_argument('--only', default=None, help='substring filter on case ids')
    ap.add_argument('--regrade', action='store_true',
                    help='grade the raw outputs saved by the previous run (build/suites/<suite>/runs*.json)')
    args = ap.parse_args()
    repl = args.repl or (S.ASAN_REPL if args.asan else S.REPL)
    for suite in args.suite.split(','):
        path = os.path.join(BUILD, suite, 'cases.jsonl')
        if not os.path.exists(path):
            print('%s: no %s (run make suites-fetch)' % (suite, path), file=sys.stderr)
            continue
        cases = S.load_cases(path)
        if args.only:
            cases = [c for c in cases if args.only in c['id']]
        if args.sample > 1:
            runnable = [c for c in cases if not c.get('na') and not c.get('unsupported')]
            keep = {c['id'] for c in runnable[::args.sample]}
            cases = [c for c in cases if c['id'] in keep]
        tmo, chunk = DEFAULTS.get(suite, (8000, 30))
        if args.timeout:
            tmo = args.timeout
        if args.asan:
            tmo *= 3
        t0 = time.time()
        print('%s: %d cases, timeout %d ms, %s' % (suite, len(cases), tmo, os.path.basename(repl)),
              file=sys.stderr, flush=True)
        rawp = os.path.join(BUILD, suite, 'runs%s%s.json' % ('_asan' if args.asan else '',
                                                           '_sample%d' % args.sample if args.sample > 1 else ''))
        if args.regrade and os.path.exists(rawp):
            runs = json.load(open(rawp))          # reuse the raw outputs of the last run
        else:
            runs = S.run_cases(cases, repl, timeout_ms=tmo, jobs=args.j, chunk=chunk)
            with open(rawp, 'w') as f:
                json.dump(runs, f)
        t1 = time.time()
        cls = S.classify_all(cases, runs, jobs=args.j)
        print('%s: ran in %.0fs, graded in %.0fs' % (suite, t1 - t0, time.time() - t1), file=sys.stderr)
        suffix = '_asan' if args.asan else ''
        if args.sample > 1:
            suffix += '_sample%d' % args.sample
        recs = []
        for c in cases:
            r = runs.get(c['id'], {'out': '', 'time': 0})
            cat, note = cls[c['id']]
            rec = {'id': c['id'], 'group': c.get('group', ''), 'src': c.get('src', ''),
                   'src_expect': c.get('src_expect', ''), 'lines': c.get('lines', []),
                   'output': r.get('out', ''), 'expect': S.pretty(c.get('expect')),
                   'check': c.get('check', ''), 'category': cat, 'note': note, 'time': r.get('time', 0)}
            if cat in ('CRASH', 'TIMEOUT') and r.get('stderr'):
                rec['stderr'] = r['stderr'][-4000:]
            recs.append(rec)
        os.makedirs(os.path.join(BUILD, suite), exist_ok=True)
        with open(os.path.join(BUILD, suite, 'results%s.json' % suffix), 'w') as f:
            json.dump(recs, f, indent=0)
        write_md(os.path.join(HERE, suite, 'results%s.md' % suffix), suite, recs, repl)
        S.print_totals(suite, recs)


def write_md(path, name, recs, repl):
    groups, tab, tot = S.totals(recs)
    L = ['# %s suite results' % name, '',
         'binary: `%s`; full per-case records: `build/suites/%s/%s.json`' % (
             os.path.relpath(repl, S.ROOT), name, os.path.splitext(os.path.basename(path))[0]), '',
         '| group | ' + ' | '.join(S.CATS) + ' | total |', '|---' * (len(S.CATS) + 2) + '|']
    for g in groups:
        L.append('| %s | ' % g + ' | '.join(str(tab[g][c]) for c in S.CATS) + ' | %d |' % sum(tab[g].values()))
    L.append('| **total** | ' + ' | '.join('**%d**' % tot[c] for c in S.CATS) +
             ' | **%d** |' % sum(tot.values()))
    L.append('')
    limits = {'WRONG': 5000, 'CRASH': 5000, 'TIMEOUT': 400, 'TRANSLATION-UNCERTAIN': 400,
              'ERROR': 400, 'UNGRADED': 200}
    for cat, lim in limits.items():
        rs = [r for r in recs if r['category'] == cat]
        if not rs:
            continue
        L += ['## %s (%d%s)' % (cat, len(rs), ', first %d listed' % lim if len(rs) > lim else ''), '']
        for r in rs[:lim]:
            L.append('- **%s** `%s` -> `%s` (expected `%s`) %s' % (
                r['id'], ' ; '.join(r['lines'])[:400], r['output'][:300].replace('`', "'"),
                r['expect'][:300], r['note'][:300]))
        L.append('')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write('\n'.join(L) + '\n')


if __name__ == '__main__':
    main()
