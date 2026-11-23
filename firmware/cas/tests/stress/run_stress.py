#!/usr/bin/env python3
"""Resource-exhaustion stress test of the CAS (`make -C firmware/cas stress`).

Every input must end in a result or a clean CAS error ("ERROR n: ..."): no
AddressSanitizer / UBSan report, no crash, no hang past the per-line timeout,
under a small arena and a small C stack.  The REPL is the sanitizer build
(build/cas_repl_asan); `-s KB` runs the engine on a thread whose stack is the
limit plus 512 KB, so a recursion that skips stack_check really overflows.

Inputs: the Wester problems (tests/wester/problems.txt), every Nth case of the
translated suites (build/suites/*/cases.jsonl, when present), the fuzz corpus
(FUZZ_DUMP=1 build/fuzz N), the adversarial list (tests/stress/adversarial.txt)
and generated deep nestings.  Each configuration runs them all; the memory
guards of the suite runner apply (lib/suitelib.py: at most 3 processes,
2 GB each, 6 GB in all).

Exit status 1 if any case crashed, hung or tripped a sanitizer.
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CAS = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(CAS, 'tests', 'suites', 'lib'))
import suitelib as S  # noqa: E402

CONFIGS = [('arena 2 MB, stack 16 KB', ['-k', '2048', '-s', '16']),
           ('arena 256 KB, stack 16 KB', ['-k', '256', '-s', '16'])]


def wester_cases():
    cases, cur = [], None
    for ln in open(os.path.join(CAS, 'tests', 'wester', 'problems.txt')):
        ln = ln.rstrip('\n')
        if ln.startswith('== '):
            cur = {'id': 'wester/' + ln[3:].split('|')[0].strip(), 'lines': []}
            cases.append(cur)
        elif cur is not None and (ln.startswith('pre:') or ln.startswith('in:')):
            cur['lines'].append(ln.split(':', 1)[1].strip())
    return [c for c in cases if c['lines']]


def suite_cases(every):
    out = []
    for s in ('rubi', 'maxima', 'sympy', 'giac'):
        p = os.path.join(CAS, 'build', 'suites', s, 'cases.jsonl')
        if not os.path.exists(p):
            continue
        for i, c in enumerate(S.load_cases(p)):
            if i % every == 0 and c.get('lines') and not c.get('unsupported') and not c.get('na'):
                out.append({'id': c['id'], 'lines': c['lines']})
    return out


def fuzz_cases(n):
    exe = os.path.join(CAS, 'build', 'fuzz')
    if not os.path.exists(exe):
        return []
    env = dict(os.environ, FUZZ_DUMP='1', ASAN_OPTIONS='detect_leaks=0')
    out = subprocess.run([exe, str(n), '7'], capture_output=True, text=True, errors='replace', env=env).stdout
    return [{'id': 'fuzz/%d' % i, 'lines': [ln]} for i, ln in enumerate(out.split('\n')[:n]) if ln.strip()]


def adversarial_cases():
    lines = [ln.rstrip('\n') for ln in open(os.path.join(HERE, 'adversarial.txt')) if ln.strip()]
    cases = [{'id': 'adv/%d' % i, 'lines': [ln]} for i, ln in enumerate(lines)]
    deep = {
        'parens': '(' * 10000 + 'x' + ')' * 10000,
        'sin': 'sin(' * 10000 + 'x' + ')' * 10000,
        'powtower': '^'.join(['x'] * 10000),
        'neg': '-' * 10000 + 'x',
        'list': '[' * 10000 + '1' + ']' * 10000,
        'sum': '+'.join('x^%d' % k for k in range(4000)),
        'nested_add': ''.join('1+(' for _ in range(5000)) + 'x' + ')' * 5000,
        'deep_fn': 'f(' * 3000 + 'x' + ')' * 3000,
    }
    for k, v in deep.items():
        cases.append({'id': 'deep/' + k, 'lines': [v]})
        cases.append({'id': 'deep/' + k + '/simplify', 'lines': ['simplify(' + v + ')']})
    # values built deep by repetition (not by the parser): u := sin(u) many times
    cases.append({'id': 'deep/built', 'lines': ['u:=x'] + ['u:=sin(u)'] * 3000 + ['diff(u,x)', 'simplify(u)', 'u']})
    # out of memory one after another, then normal work must still succeed
    oom = ['expand((x+y+z)^300)', 'factorial(19999)^200', 'expand((a+b+c+d)^120)']
    cases.append({'id': 'recover/oom-then-normal',
                  'lines': oom + oom + ['v:=3', 'factor(x^2-1)', 'integrate(x*sin(x),x)', 'v+1']})
    return cases


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repl', default=os.path.join(CAS, 'build', 'cas_repl_asan'))
    ap.add_argument('--every', type=int, default=40, help='suite sample: every Nth case')
    ap.add_argument('--fuzz', type=int, default=2000)
    ap.add_argument('--timeout', type=int, default=4000, help='ms per line')
    ap.add_argument('-j', type=int, default=S.MAX_JOBS)
    a = ap.parse_args()
    S.self_memguard()
    cases = wester_cases() + suite_cases(a.every) + fuzz_cases(a.fuzz) + adversarial_cases()
    bad = 0
    report = {}
    for name, extra in CONFIGS:
        res = S.run_cases(cases, a.repl, timeout_ms=a.timeout, jobs=a.j, chunk=25, progress=True, extra=extra)
        counts = {}
        for c in cases:
            r = res.get(c['id'])
            st = r['status'] if r else 'missing'
            if st == 'ok':
                st = 'error' if r['out'].startswith('ERROR') else 'result'
            counts[st] = counts.get(st, 0) + 1
            if st not in ('result', 'error'):
                bad += 1
                print('[%s] %s %s: %s\n    %s' % (name, st.upper(), c['id'], ' ; '.join(c['lines'])[:300],
                                                  (r or {}).get('stderr', '')[-1500:].replace('\n', '\n    ')))
        report[name] = counts
        print('%s: %d cases: %s' % (name, len(cases), ', '.join('%s %d' % kv for kv in sorted(counts.items()))))
    with open(os.path.join(CAS, 'build', 'stress_report.json'), 'w') as f:
        json.dump(report, f, indent=1)
    print('stress: %s' % ('FAIL (%d bad)' % bad if bad else 'ok: every case ended in a result or a clean error'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
