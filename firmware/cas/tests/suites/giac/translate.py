#!/usr/bin/env python3
"""
Giac's check/ directory -> cases.jsonl.

(a) testX / X.out pairs (limit, integrate, cas, normalize, factor, partfrac,
    other): the input is split into commands at top-level ';' and aligned 1:1
    with the output lines of X.out (giac prints one comma-terminated line per
    command).  Indefinite integrals are graded by differentiation; every
    other command against giac's printed result (SymPy equivalence).  Files
    whose command and output counts differ are not aligned (whole file NA).
(b) integration lists intind*, intheb1-4, intmit (entries
    [integrand, var, steps, antiderivative, flag]), graded by
    differentiation; duplicates (same integrand and variable text) dropped.
Pi/PI are pi (the lists write i*Pi); giac's 0/1/2 isprime output is a truth
value; a giac "... Error ..." string output means an error is expected.
TP*-sol.cas sessions are not used: they are programming/numeric sessions
whose outputs cannot be aligned reliably.

Usage: translate.py CHECK_DIR OUT.jsonl
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'lib'))
import sympy as sp            # noqa: E402
import mparse as M            # noqa: E402
import maxima_dialect as MD   # noqa: E402
import suitelib as S          # noqa: E402

PAIRS = [('testlimit', 'limit.out'), ('testintegrate', 'integrate.out'), ('testcas', 'cas.out'),
         ('testnormalize', 'normalize.out'), ('testfactor', 'factor.out'),
         ('testpartfrac', 'partfrac.out'), ('testother', None)]
LISTS = ['intind', 'intinda', 'intindbo', 'intindbr', 'intindc', 'intindh', 'intindj', 'intindm',
         'intinds', 'intindt', 'intindw', 'intheb1', 'intheb2', 'intheb3', 'intheb4', 'intmit']

# id -> dict of fields to override after translation (manual review)
OVERRIDES = {
    # giac's eval(2*x*2) keeps the product unevaluated: just a value check
}

# ------------------------------------------------------------ name maps ---
_e = MD._e


def _limit(em, a):
    if len(a) >= 2 and a[1][0] == 'op' and a[1][1] == '=':
        x, pt = a[1][2]
        rest = a[2:]
    elif len(a) >= 3:
        x, pt = a[1], a[2]
        rest = a[3:]
    else:
        raise M.Untranslatable('limit arguments')
    s = 'limit(%s,%s,%s' % (_e(em, a[0]), _e(em, x), _e(em, pt))
    if rest:
        d = _e(em, rest[0])
        if d not in ('1', '-1'):
            raise M.Untranslatable('limit direction')
        s += ',' + d
    return s + ')'


def _series(em, a):
    # giac series(f, x=a, n) keeps (x-a)^n; default order 5.  Ours stops below n.
    if len(a) >= 2 and a[1][0] == 'op' and a[1][1] == '=':
        x, pt = a[1][2]
        rest = a[2:]
    elif len(a) >= 3:
        x, pt = a[1], a[2]
        rest = a[3:]
    elif len(a) == 2:
        x, pt, rest = a[1], ('num', '0'), []
    else:
        raise M.Untranslatable('series arguments')
    n = 5
    if rest:
        if rest[0][0] != 'num':
            raise M.Untranslatable('series order')
        n = int(rest[0][1])
    return 'series(%s,%s,%s,%d)' % (_e(em, a[0]), _e(em, x), _e(em, pt), n + 1)


def _integrate(em, a):
    if len(a) in (2, 4):
        return 'integrate(%s)' % ','.join(_e(em, x) for x in a)
    raise M.Untranslatable('integrate arguments')


def _diff(em, a):
    if len(a) == 1:
        raise M.Untranslatable('diff without variable')
    return 'diff(%s)' % ','.join(_e(em, x) for x in a)


def _solve(em, a):
    if len(a) == 1:
        return 'solve(%s,x)' % _e(em, a[0])
    return 'solve(%s,%s)' % (_e(em, a[0]), _e(em, a[1]))


def _subst(em, a):
    return 'subst(%s)' % ','.join(_e(em, x) for x in a)


def _log(em, a):
    if len(a) != 1:
        raise M.Untranslatable('log arity')
    return 'ln(%s)' % _e(em, a[0])


def _factor(em, a):
    if len(a) != 1:
        raise M.Untranslatable('factor over an extension')
    return 'factor(%s)' % _e(em, a[0])


def _coeff(em, a):
    if len(a) != 3:
        raise M.Untranslatable('coeff list form')       # giac coeff(p,x) is the coefficient list
    return 'coeff(%s)' % ','.join(_e(em, x) for x in a)


FUNCS = {k: v for k, v in MD.FUNCS.items()
         if k not in ('subst', 'limit', 'taylor', 'solve', 'log', 'tlimit', 'integrate', 'diff',
                      'factor', 'coeff', 'num', 'divide', 'gcd', 'max', 'min', 'round', 'mod')}
FUNCS.update({
    'ln': 'ln', 'log': _log, 'arcsin': 'asin', 'arccos': 'acos', 'arctan': MD._atan,
    'arccot': 'acot', 'arcsec': 'asec', 'arccsc': 'acsc', 'arcsinh': 'asinh', 'arccosh': 'acosh',
    'arctanh': 'atanh', 'arccoth': MD.of_recip('atanh'), 'arcsech': MD.of_recip('acosh'),
    'arccsch': MD.of_recip('asinh'), 'asinh': 'asinh', 'acosh': 'acosh', 'atanh': 'atanh',
    'sign': 'sign', 're': 're', 'im': 'im', 'conj': 'conj', 'arg': 'arg',
    'integrate': _integrate, 'int': _integrate, 'diff': _diff, 'derive': _diff, 'limit': _limit,
    'series': _series, 'solve': _solve, 'subst': _subst, 'factor': _factor,
    'normal': 'normal', 'ratnormal': 'normal', 'non_recursive_normal': 'normal', 'simplify': 'simplify',
    'partfrac': 'partfrac', 'gcd': 'gcd', 'lcm': 'lcm', 'det': 'det', 'inv': 'inv',
    'tran': 'transpose', 'transpose': 'transpose', 'idn': 'identity', 'identity': 'identity',
    'ker': 'ker', 'rref': 'rref', 'tlin': 'tlin', 'texpand': 'texpand', 'iquo': 'iquo',
    'irem': 'irem', 'nextprime': 'nextprime', 'prevprime': 'prevprime', 'resultant': 'resultant',
    'evalf': 'approx', 'approx': 'approx', 'expand': 'expand', 'abs': 'abs', 'Ei': 'Ei', 'Si': 'Si',
    'Ci': 'Ci', 'erf': 'erf', 'erfi': 'erfi', 'gamma': 'gamma', 'Gamma': 'gamma',
    'exp': 'exp', 'sqrt': 'sqrt', 'sec': 'sec', 'csc': 'csc', 'cot': 'cot',
    'is_prime': 'isprime', 'isprime': 'isprime', 'ichinrem': 'ichinrem', 'coeff': _coeff,
    'sort': 'sort', 'horner': 'horner',
})
CONSTS = {'pi': 'pi', 'Pi': 'pi', 'PI': 'pi', 'e': 'e', 'i': 'i', 'I': 'i', 'inf': 'inf', 'infinity': 'inf',
          'true': 'true', 'false': 'false', 'euler_gamma': 'euler_gamma', 'undef': 'undef'}
SPF = {'arcsin': sp.asin, 'arccos': sp.acos, 'arctan': sp.atan, 'arccot': sp.acot, 'arcsec': sp.asec,
       'arccsc': sp.acsc, 'arcsinh': sp.asinh, 'arccosh': sp.acosh, 'arctanh': sp.atanh,
       'arccoth': sp.acoth, 'arcsech': sp.asech, 'arccsch': sp.acsch, 'Gamma': sp.gamma,
       'GAMMA': sp.gamma, 'Ei': sp.Ei, 'Si': sp.Si, 'Ci': sp.Ci, 'erfi': sp.erfi}


def conv():
    # giac reads Pi (and PI) as pi; the integration lists write i*Pi
    return M.SympyConv('giac', funcs=SPF, consts={'I': sp.I, 'Pi': sp.pi, 'PI': sp.pi})


def emitter():
    return M.OurEmitter(FUNCS, CONSTS)


# ------------------------------------------------------------- reading ---
def strip_comments(s):
    out = []
    for ln in s.split('\n'):
        # // comments (not inside strings; giac check files have no // in strings)
        q = False
        for j, ch in enumerate(ln):
            if ch == '"':
                q = not q
            if not q and ln.startswith('//', j):
                ln = ln[:j]
                break
        out.append(ln)
    return '\n'.join(out)


def split_cmds(text):
    """top-level ';' split (':;' quiet terminators included)"""
    cmds, depth, cur, q = [], 0, [], False
    for ch in text:
        if ch == '"':
            q = not q
        if not q:
            if ch in '([{':
                depth += 1
            elif ch in ')]}':
                depth -= 1
            if ch == ';' and depth == 0:
                cmds.append(''.join(cur).strip())
                cur = []
                continue
        cur.append(ch)
    if ''.join(cur).strip():
        cmds.append(''.join(cur).strip())
    out = []
    for c in cmds:
        c = ' '.join(c.split())
        if c.endswith(':'):
            c = c[:-1].strip()
        out.append(c)
    return [c for c in out if c]


def split_top(s, sep=','):
    parts, depth, cur, q = [], 0, [], False
    for ch in s:
        if ch == '"':
            q = not q
        if not q:
            if ch in '([{':
                depth += 1
            elif ch in ')]}':
                depth -= 1
            if ch == sep and depth == 0:
                parts.append(''.join(cur).strip())
                cur = []
                continue
        cur.append(ch)
    parts.append(''.join(cur).strip())
    return parts


def giac_out_to_text(s):
    """giac printed value -> parsable text"""
    s = s.strip()
    s = re.sub(r'\b(matrix|list|set)\[', '[', s)
    s = s.replace('+infinity', 'inf').replace('-infinity', '-inf')
    return s


def strip_order(ast):
    """drop the (x-a)^n*order_size(..) term of a giac series"""
    if ast[0] == 'op' and ast[1] in ('+', '-'):
        l, r = ast[2]
        if has_order(r):
            return strip_order(l)
        if has_order(l):
            return strip_order(r) if ast[1] == '+' else ('neg', strip_order(r))
        return ('op', ast[1], [strip_order(l), r])
    if has_order(ast):
        return ('num', '0')
    return ast


def has_order(a):
    return any(n[0] == 'call' and n[1] == 'order_size' for n in M.walk(a))


def expected_value(out_text, kind):
    t = giac_out_to_text(out_text)
    if t.startswith('"'):
        raise M.NoRef('string/error output: %s' % t[:60])
    if re.search(r'\b(poly1|rootof|GF|assume|line)\b', t):
        raise M.NoRef('giac-specific output object')
    ast = M.parse(t, 'giac')
    if kind == 'series':
        ast = strip_order(ast)
    if any(n[0] == 'call' and n[1] in ('integrate', 'limit', 'sum', 'order_size') for n in M.walk(ast)):
        raise M.NoRef('giac left it unevaluated')
    v = conv().conv(ast)
    return v


def names_in(ast):
    syms, funcs = M.names_used(ast)
    return syms | funcs


# ------------------------------------------------------------ (a) pairs ---
def translate_pair(check, inp, outp):
    stem = inp[4:] if inp.startswith('test') else inp
    text = strip_comments(open(os.path.join(check, inp), errors='replace').read())
    cmds = split_cmds(text)
    outs = None
    if outp and os.path.exists(os.path.join(check, outp)):
        outs = [ln.rstrip() for ln in open(os.path.join(check, outp), errors='replace').read().split('\n')]
        outs = [ln[:-1] if ln.endswith(',') else ln for ln in outs if ln.strip() != '']
    aligned = outs is not None and len(outs) == len(cmds)
    cases = []
    assigns = {}      # name -> (line or None if untranslatable, deps)
    for n, cmd in enumerate(cmds, 1):
        cid = 'giac/%s/%d' % (stem, n)
        c = {'id': cid, 'group': inp, 'src': cmd, 'src_expect': outs[n - 1] if aligned else '',
             'lines': [''], 'check': 'equiv'}
        cases.append(c)
        if re.fullmatch(r'\d+', cmd):
            c['na'] = 'label line'
            continue
        if cmd.startswith('cas_setup') or cmd.startswith('xcas_mode'):
            c['na'] = 'giac session setup'
            continue
        try:
            ast = M.parse(cmd, 'giac')
        except M.ParseError as ex:
            c['na'] = 'parse: %s' % ex
            continue
        pre, pick = [], None
        # (assume(..), X, purge(..)) sequences: setup + graded middle item
        if ast[0] == 'call' and ast[1] == '__seq':
            items = ast[2]
            main = [k for k, it in enumerate(items) if not (it[0] == 'call' and it[1] in ('assume', 'purge'))]
            if len(main) != 1:
                c['na'] = 'command sequence'
                continue
            pick = main[0]
            for it in items[:pick]:
                try:
                    pre.append(emitter().emit(it))
                except M.Untranslatable as ex:
                    c['unsupported'] = str(ex)
            ast = items[pick]
            c['uncertain'] = 'assumption from a giac command sequence'
        # assignments
        if ast[0] == 'op' and ast[1] == ':=':
            lhs, rhs = ast[2]
            if lhs[0] != 'sym':
                c['na'] = 'giac-specific assignment'
                continue
            try:
                line = '%s:=%s' % (emitter().sym(lhs[1]), emitter().emit(rhs))
                assigns[lhs[1]] = (line, names_in(rhs))
            except M.Untranslatable as ex:
                assigns[lhs[1]] = (None, set())
                c['unsupported'] = str(ex)
                continue
            c['na'] = 'assignment (used as setup for later commands)'
            continue
        if 'unsupported' in c:
            continue
        # setup lines for names assigned earlier
        need, seen, order = list(names_in(ast)), set(), []
        bad = None
        while need:
            nm = need.pop()
            if nm in seen or nm not in assigns:
                continue
            seen.add(nm)
            line, deps = assigns[nm]
            if line is None:
                bad = nm
                break
            order.append(nm)
            need.extend(deps)
        if bad:
            c['unsupported'] = 'depends on untranslatable assignment of ' + bad
            continue
        pre = [assigns[nm][0] for nm in reversed(order)] + pre
        try:
            line = emitter().emit(ast)
        except M.Untranslatable as ex:
            c['unsupported'] = str(ex)
            continue
        c['lines'] = pre + [line]
        is_indef = ast[0] == 'call' and ast[1] in ('integrate', 'int') and len(ast[2]) == 2
        if is_indef:
            var = ast[2][1]
            try:
                f = conv().conv(ast[2][0])
                c['check'] = 'antideriv'
                c['var'] = emitter().sym(var[1])
                c['expect'] = S.ser(f)
            except M.NoRef as ex:
                c['check'] = 'noref'
                c['note'] = str(ex)
            continue
        if not aligned:
            c['check'] = 'noref'
            continue
        out = outs[n - 1]
        if pick is not None:
            parts = split_top(out)
            if len(parts) <= pick:
                c['check'] = 'noref'
                continue
            out = parts[pick]
        kind = 'series' if ast[0] == 'call' and ast[1] == 'series' else ''
        try:
            gt = giac_out_to_text(out)
            if gt == 'infinity':
                c['check'] = 'undef'
                c['uncertain'] = 'giac unsigned infinity'
                continue
            if gt == 'undef':
                c['check'] = 'undef'
                continue
            if gt.startswith('"') and 'Error' in gt:
                c['check'] = 'error'           # giac reports an error (e.g. ichinrem: no solution)
                c['note'] = gt[:80]
                continue
            v = expected_value(out, kind)
            if isinstance(v, str):
                raise M.NoRef('string output')
            if isinstance(v, tuple) and v[0] == 'Eq':
                pass
            if ast[0] == 'call' and ast[1] == 'solve':
                c['check'] = 'set'
            if ast[0] == 'call' and ast[1] == 'ker':
                c['uncertain'] = 'kernel basis is not unique (giac rows vs ours)'
            if ast[0] == 'call' and ast[1] in ('isprime', 'is_prime') and v in (0, 1, 2):
                v = sp.true if v else sp.false      # giac 0 / 1 (pseudoprime) / 2 (proven prime)
            c['expect'] = S.ser(v)
            if v in (sp.true, sp.false):
                c['check'] = 'bool'
        except (M.NoRef, M.ParseError) as ex:
            c['check'] = 'noref'
            c['note'] = str(ex)
        except Exception as ex:
            c['check'] = 'noref'
            c['note'] = 'expected: %s' % str(ex)[:80]
    if outs is not None and not aligned:
        print('giac: %s: %d commands vs %d outputs, not aligned' % (inp, len(cmds), len(outs)),
              file=sys.stderr)
    return cases


# ------------------------------------------------------------ (b) lists ---
def list_entries(text):
    i = text.find('lst:=[')
    if i < 0:
        return []
    body = text[i + len('lst:=['):]
    depth, cur, out, q = 0, [], [], False
    for ch in body:
        if ch == '"':
            q = not q
        if not q:
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


def translate_list(check, name, seen):
    text = strip_comments(open(os.path.join(check, name), errors='replace').read())
    cases = []
    for n, ent in enumerate(list_entries(text), 1):
        parts = split_top(' '.join(ent.split()))
        cid = 'giac/%s/%d' % (name, n)
        if len(parts) < 2:
            continue
        key = (parts[0].replace(' ', ''), parts[1].strip())
        if key in seen:
            continue
        seen.add(key)
        c = {'id': cid, 'group': name, 'src': 'integrate(%s, %s)' % (parts[0], parts[1]),
             'src_expect': parts[3] if len(parts) > 3 else '', 'check': 'antideriv', 'lines': ['']}
        cases.append(c)
        try:
            ig = M.parse(parts[0], 'giac')
            var = M.parse(parts[1], 'giac')
            if var[0] != 'sym':
                raise M.ParseError('variable')
            em = emitter()
            c['lines'] = ['integrate(%s,%s)' % (em.emit(ig), em.sym(var[1]))]
            c['var'] = em.sym(var[1])
            f = conv().conv(ig)
            if isinstance(f, (list, tuple, str)):
                raise M.NoRef('integrand not scalar')
            c['expect'] = S.ser(f)
        except M.ParseError as ex:
            c['na'] = 'parse: %s' % ex
        except M.Untranslatable as ex:
            c['unsupported'] = str(ex)
        except M.NoRef as ex:
            c['unsupported'] = 'no SymPy meaning for integrand: %s' % ex
        except Exception as ex:
            c['na'] = 'integrand: %s' % str(ex)[:100]
    return cases


def main():
    S.self_memguard()           # abort above 2 GB RSS (lib/suitelib.py)
    check, outp = sys.argv[1], sys.argv[2]
    cases = []
    for inp, o in PAIRS:
        if os.path.exists(os.path.join(check, inp)):
            cases.extend(translate_pair(check, inp, o))
    seen = set()
    for name in LISTS:
        if os.path.exists(os.path.join(check, name)):
            cases.extend(translate_list(check, name, seen))
    for c in cases:
        c.update(OVERRIDES.get(c['id'], {}))
    os.makedirs(os.path.dirname(os.path.abspath(outp)), exist_ok=True)
    S.save_cases(outp, cases)
    na = sum(1 for c in cases if c.get('na'))
    un = sum(1 for c in cases if c.get('unsupported'))
    print('giac: %d cases (%d na, %d unsupported)' % (len(cases), na, un))


if __name__ == '__main__':
    main()
