#!/usr/bin/env python3
"""
Rubi integration test suite (Maxima-syntax port) -> cases.jsonl.

Each test is [integrand, var, steps, optimal antiderivative].  We send
integrate(integrand, var) and grade by differentiation (d/dvar(ours) ==
integrand, numeric at 40 digits plus a SymPy fallback), so the optimal
antiderivative is only kept for the report.

Usage: translate.py SRC_DIR OUT.jsonl [--sample N]   (N = keep every N-th
problem of each file; default 1 = all)
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'lib'))
import mparse as M            # noqa: E402
import maxima_dialect as MD   # noqa: E402
import suitelib as S          # noqa: E402
import sympy as sp            # noqa: E402

# per-problem overrides: id -> dict(fields) (e.g. {'na': 'reason'})
OVERRIDES = {}


def strip_comments(s):
    return re.sub(r'/\*.*?\*/', ' ', s, flags=re.S)


def entries(text):
    """top-level [ ... ] items of the lst: '[ ... ] list"""
    i = text.find("'[")
    if i < 0:
        i = text.find('[')
        body = text[i + 1:]
    else:
        body = text[i + 2:]
    depth, cur, out = 0, [], []
    for ch in body:
        if ch == '[':
            depth += 1
            if depth == 1:
                cur = []
                continue
        elif ch == ']':
            depth -= 1
            if depth == 0:
                out.append(''.join(cur))
                continue
            if depth < 0:
                break
        if depth >= 1:
            cur.append(ch)
    return out


def split_top(s):
    parts, depth, cur = [], 0, []
    for ch in s:
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
        if ch == ',' and depth == 0:
            parts.append(''.join(cur))
            cur = []
        else:
            cur.append(ch)
    parts.append(''.join(cur))
    return [p.strip() for p in parts]


def group_name(path, src):
    rel = os.path.relpath(path, src)
    top = rel.split(os.sep)[0]
    return top


def translate_file(path, src, sample):
    text = strip_comments(open(path, encoding='utf-8', errors='replace').read())
    base = os.path.splitext(os.path.basename(path))[0]
    fid = re.sub(r'[^0-9A-Za-z.]+', '_', base).strip('_')[:60]
    cases = []
    for n, ent in enumerate(entries(text), 1):
        if sample > 1 and (n - 1) % sample:
            continue
        parts = split_top(ent)
        cid = 'rubi/%s/%d' % (fid, n)
        c = {'id': cid, 'group': group_name(path, src), 'file': os.path.relpath(path, src),
             'src': 'integrate(%s, %s)' % (parts[0], parts[1] if len(parts) > 1 else '?'),
             'src_expect': parts[3] if len(parts) > 3 else '', 'check': 'antideriv', 'lines': ['']}
        try:
            if len(parts) < 4:
                raise M.ParseError('entry has %d fields' % len(parts))
            ig = M.parse(parts[0])
            var = M.parse(parts[1])
            if var[0] != 'sym':
                raise M.ParseError('variable')
            em = M.OurEmitter(MD.FUNCS, MD.CONSTS)
            our_ig = em.emit(ig)
            our_var = em.sym(var[1])
            c['lines'] = ['integrate(%s,%s)' % (our_ig, our_var)]
            c['var'] = our_var
            conv = M.SympyConv('maxima')
            f = conv.conv(ig)
            if isinstance(f, (list, tuple, str)):
                raise M.NoRef('integrand not scalar')
            if f.has(sp.acot):
                # the integrand is sent as acot(...), which our CAS reads as
                # pi/2 - atan(x) (SymPy/Maxima: atan(1/x)); grade against that meaning
                f = f.replace(sp.acot, lambda z: sp.pi / 2 - sp.atan(z))
            c['expect'] = S.ser(f)
        except M.ParseError as ex:
            c['na'] = 'parse: %s' % ex
        except M.Untranslatable as ex:
            c['unsupported'] = str(ex)
        except M.NoRef as ex:
            c['unsupported'] = 'no SymPy meaning for integrand: %s' % ex
        except Exception as ex:      # SymPy evaluation trouble on the integrand
            c['na'] = 'integrand: %s' % str(ex)[:100]
        c.update(OVERRIDES.get(cid, {}))
        cases.append(c)
    return cases


def main():
    S.self_memguard()           # abort above 2 GB RSS (lib/suitelib.py)
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('out')
    ap.add_argument('--sample', type=int, default=1)
    args = ap.parse_args()
    files = []
    for dp, dn, fn in os.walk(args.src):
        dn.sort()
        for f in sorted(fn):
            if f.endswith('.mac'):
                files.append(os.path.join(dp, f))
    files.sort(key=lambda p: [int(t) if t.isdigit() else t for t in re.split(r'(\d+)', os.path.relpath(p, args.src))])
    cases = []
    for p in files:
        cases.extend(translate_file(p, args.src, args.sample))
    S.save_cases(args.out, cases)
    na = sum(1 for c in cases if c.get('na'))
    un = sum(1 for c in cases if c.get('unsupported'))
    print('rubi: %d cases (%d na, %d unsupported) from %d files' % (len(cases), na, un, len(files)))


if __name__ == '__main__':
    main()
