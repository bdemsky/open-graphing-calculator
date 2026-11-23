#!/usr/bin/env python3
"""
Maxima testsuite (tests/rtest*.mac of the Maxima source) -> cases.jsonl.

An rtest file is a sequence of `input; expected;` pairs (terminators ; or $).
State-changing inputs (assignments, function definitions, kill, assume,
declare, option variables) are not cases: they update a per-file state, and
each case is sent with the definitions it uses (and the active assumptions) as
setup lines, so every case runs on its own.

  * indefinite integrate(f, x): graded by differentiating our answer
  * solve: order-free set of solutions
  * float expected: numeric value check
  * otherwise: mathematical equivalence with SymPy (run_wester's checker)
Cases that rely on non-default option variables (domain:complex, numer:true,
radexpand:all, ...) or declarations are marked uncertain: a mismatch becomes
TRANSLATION-UNCERTAIN instead of WRONG.

Usage: translate.py TESTS_DIR OUT.jsonl
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'lib'))
import mparse as M            # noqa: E402
import maxima_dialect as MD   # noqa: E402
import suitelib as S          # noqa: E402
import sympy as sp            # noqa: E402

sys.path.insert(0, HERE)
from overrides import OVERRIDES, SKIP_FILES  # noqa: E402

# option variables whose non-default value makes a case uncertain; value =
# default (as Maxima prints it)
OPTIONS = {
    'numer': 'false', 'float': 'false', 'domain': 'real', 'algebraic': 'false', 'radexpand': 'true',
    'logexpand': 'true', 'exponentialize': 'false', 'demoivre': 'false', 'keepfloat': 'false',
    'simp': 'true', 'triginverses': 'true', 'trigsign': 'true', 'trigexpand': 'false',
    'halfangles': 'false', 'intanalysis': 'true', 'logabs': 'false', 'logarc': 'false',
    '%emode': 'true', '%enumer': 'false', '%piargs': 'true', '%iargs': 'true', 'listarith': 'true',
    'sumexpand': 'false', 'simpsum': 'false', 'simpproduct': 'false', 'prederror': 'false',
    'factorflag': 'false', 'gamma_expand': 'false', 'besselexpand': 'false', 'erfflag': 'true',
    'tlimswitch': 'true', 'programmode': 'true', 'globalsolve': 'false', 'breakup': 'true',
    'realonly': 'false', 'ratfac': 'false', 'detout': 'false', 'doallmxops': 'true',
    'lhospitallim': '4', 'ratalgdenom': 'true', 'rootsconmode': 'true', 'logconcoeffp': 'false',
    'solveradcan': 'false', 'expop': '0', 'expon': '0', 'maxposex': '1000', 'maxnegex': '1000',
    'zerobern': 'true', 'zeta%pi': 'true', 'cauchysum': 'false', 'trigexpandplus': 'true',
    'trigexpandtimes': 'true', 'ratsimpexpons': 'false', 'radsubstflag': 'false',
    'logsimp': 'true', 'lognegint': 'false', 'logexpand_all': 'false', 'limsubst': 'false',
    'integrate_use_rootsof': 'false', 'gcd': 'spmod', 'algexact': 'false', 'scalarmatrixp': 'true',
    'matrix_element_mult': '*', 'dotscrules': 'false', 'dot0simp': 'true', 'dotexptsimp': 'true',
    'fpprec': '16', 'bftorat': 'false', 'ratepsilon': '2.0e-15', 'exptsubst': 'false',
    'sqrtdenest': None, 'abconvtest': 'false', 'intfaclim': 'true', 'savefactors': 'false',
    'berlefact': 'true', 'dontfactor': '[]', 'ratvars': '[]', 'ratweights': '[]', 'taylordepth': '3',
    'maxtayorder': 'true', 'powerdisp': None, 'besselexpand_all': None, 'expintexpand': 'false',
    'expintrep': 'false', 'gamma_expand_all': None, 'hypergeometric_representation': 'false',
    'factorial_expand': 'false', 'beta_expand': 'false', 'erf_representation': 'false',
    'trigexpand_all': None, 'nouns': 'false', 'exptdispflag': None, 'useminmax': None,
    'use_fast_arrays': None, 'linsolvewarn': None, 'linsolve_params': 'true',
    'backsubst': 'true', 'sparse': 'false', 'ratmx': 'false', 'lmxchar': None, 'rmxchar': None,
    'algepsilon': None, 'radexpand_all': None, 'm1pbranch': 'false', 'rombergtol': None,
    'newtonepsilon': None, '%e_to_numlog': 'false', 'exponentialize_all': None,
    'halfangles_all': None, 'opsubst': 'true', 'listconstvars': 'false', 'ratdenomdivide': 'true',
    'derivsubst': 'false', 'linechar': None, 'debugmode': None, 'factlim': '100000',
    'expandwrt_denom': 'false', 'expintexpand_all': None, 'gammalim': '10000',
    'ibase': '10', 'obase': '10', 'fpprec_all': None, 'piece': None, 'tellsimp': None,
}
HARMLESS = {'display2d', 'ratprint', 'linel', 'fpprintprec', 'stringdisp', 'powerdisp',
            'negsumdispflag', 'sqrtdispflag', 'exptdispflag', 'lmxchar', 'rmxchar', 'debugmode',
            'solvetrigwarn', 'linsolvewarn', 'tr_warn_bad_function_calls', 'translate',
            'savedef', 'inchar', 'outchar', 'linechar', 'file_search_maxima', 'leftjust',
            'display_format_internal', 'ttyoff', 'grind', 'tex_uses_prime_for_derivatives',
            'errormsg', 'error_syms', 'error_size', 'verbose', 'debugmode', 'showtime',
            'simplify_sum_verbose', 'integration_constant_counter', 'mode_checkp', 'ratepsilon'}
STATE_FUNCS = {'kill', 'assume', 'forget', 'declare', 'remove', 'remvalue', 'remfunction',
               'load', 'batchload', 'reset', 'killcontext', 'newcontext', 'supcontext',
               'activate', 'deactivate', 'tellsimp', 'tellsimpafter', 'defrule', 'matchdeclare',
               'put', 'remarray', 'remlet', 'let', 'gradef', 'depends', 'atvalue', 'alias',
               'declare_translated', 'texput', 'set_random_state', 'orderless', 'ordergreat',
               'unorder', 'modedeclare', 'local', 'array', 'dependencies', 'ratweight',
               'declare_constvalue', 'infix', 'prefix', 'postfix', 'nofix', 'matchfix', 'nary',
               'defmatch', 'forget', 'remlet', 'gnuplot_close', 'unassume', 'setup_autoload',
               'prederror', 'apply_cycles'}


def strip_comments(s):
    out, i, n = [], 0, len(s)
    while i < n:
        if s.startswith('/*', i):
            depth, i = 1, i + 2
            while i < n and depth:
                if s.startswith('/*', i):
                    depth += 1
                    i += 2
                elif s.startswith('*/', i):
                    depth -= 1
                    i += 2
                else:
                    i += 1
            out.append(' ')
        elif s[i] == '"':
            j = i + 1
            while j < n and s[j] != '"':
                j += 2 if s[j] == '\\' else 1
            out.append(s[i:j + 1])
            i = j + 1
        else:
            out.append(s[i])
            i += 1
    return ''.join(out)


def statements(s):
    out, cur, i, n = [], [], 0, len(s)
    depth = 0
    while i < n:
        ch = s[i]
        if ch == '"':
            j = i + 1
            while j < n and s[j] != '"':
                j += 2 if s[j] == '\\' else 1
            cur.append(s[i:j + 1])
            i = j + 1
            continue
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
        if ch in ';$' and depth <= 0:
            st = ''.join(cur).strip()
            out.append(st)
            cur = []
            depth = 0
        elif ch == '\\' and i + 1 < n:
            cur.append(s[i:i + 2])
            i += 2
            continue
        else:
            cur.append(ch)
        i += 1
    return out


class State:
    def __init__(self):
        self.reset()

    def reset(self):
        # name -> {'lines': [(seq, line)], 'used': set, 'bad': bool, 'late': set}
        self.defs = {}
        self.fmap = {}          # user function name -> our name
        self.seq = 0
        self.flags = {}
        self.assume = []        # our lines
        self.decl = []          # textual maxima declarations (uncertain)

    def define(self, name, line, used, self_ref=False, function=False):
        self.seq += 1
        syms, funcs = used
        names = (syms | funcs) - {name}
        old = self.defs.get(name)
        d = {'lines': [], 'used': set(), 'bad': False, 'late': set()}
        if self_ref and old:
            d = {'lines': list(old['lines']), 'used': set(old['used']), 'bad': old['bad'],
                 'late': set(old['late'])}
        if line is None:
            d['bad'] = True
        else:
            d['lines'].append((self.seq, line))
        d['used'] |= names
        # Maxima's `:` evaluates the right side now; ours substitutes at use.
        # Names that get a value only later would change the meaning.
        if not function:
            d['late'] |= {n for n in syms if n not in self.defs and n != name}
        self.defs[name] = d

    def taint(self, name):
        """a statement we could not translate changed `name`"""
        d = self.defs.get(name) or {'lines': [], 'used': set(), 'late': set()}
        d['bad'] = True
        self.defs[name] = d

    def uncertain(self):
        bits = ['%s:%s' % kv for kv in sorted(self.flags.items())]
        bits += self.decl
        return ', '.join(bits)


CUR_STATE = [None]


def emitter():
    st = CUR_STATE[0]
    if st is not None and st.fmap:
        f = dict(FUNCS)
        f.update(st.fmap)
        return M.OurEmitter(f, MD.CONSTS)
    return M.OurEmitter(FUNCS, MD.CONSTS)


def _ev(em, a):
    if not a:
        raise M.Untranslatable('ev')
    body = em.emit(a[0])
    for x in a[1:]:
        if x[0] == 'sym' and x[1] in ('numer', 'float', 'bfloat'):
            body = 'approx(%s)' % body
        elif x[0] == 'op' and x[1] == '=':
            body = 'subst(%s,%s)' % (body, em.emit(x))
        elif x[0] == 'list' and all(y[0] == 'op' and y[1] == '=' for y in x[1]):
            body = 'subst(%s,%s)' % (body, em.emit(x))
        elif x[0] == 'sym' and x[1] in ('expand', 'ratsimp', 'factor', 'simp', 'nouns', 'diff',
                                        'integrate', 'infeval', 'eval'):
            if x[1] in ('expand', 'ratsimp', 'factor'):
                body = '%s(%s)' % (x[1], body)
        else:
            raise M.Untranslatable('ev flag')
    return body


def _seq(em, a):
    raise M.Untranslatable('statement sequence')


def _makelist(em, a):
    if len(a) == 4:
        return 'seq(%s)' % ','.join(em.emit(x) for x in a)
    raise M.Untranslatable('makelist arguments')


def _sqrt_like(name):
    return lambda em, a: '%s(%s)' % (name, em.emit(a[0]))


def _ratsimp(em, a):
    if len(a) == 1:
        return 'ratsimp(%s)' % em.emit(a[0])
    raise M.Untranslatable('ratsimp with main variables')


def _factor(em, a):
    if len(a) == 1:
        return 'factor(%s)' % em.emit(a[0])
    raise M.Untranslatable('factor over an extension')


def _csolve(em, a):
    if len(a) != 2:
        raise M.Untranslatable('solve without variable list')
    return 'csolve(%s,%s)' % (em.emit(a[0]), em.emit(a[1]))


def _diff(em, a):
    # diff(f, x, n, y, m) -> nested; diff(f, x, y) also allowed
    if len(a) < 2:
        raise M.Untranslatable('total differential')
    body = em.emit(a[0])
    rest = a[1:]
    pairs = []
    while rest:
        v = rest[0]
        if v[0] != 'sym':
            raise M.Untranslatable('diff variable')
        if len(rest) > 1 and rest[1][0] == 'num':
            pairs.append((v, rest[1]))
            rest = rest[2:]
        else:
            pairs.append((v, ('num', '1')))
            rest = rest[1:]
    for v, k in pairs:
        if k[1] == '1':
            body = 'diff(%s,%s)' % (body, em.emit(v))
        else:
            body = 'diff(%s,%s,%s)' % (body, em.emit(v), k[1])
    return body


def _expand(em, a):
    if len(a) == 1:
        return 'expand(%s)' % em.emit(a[0])
    if len(a) == 3 and a[1] == ('num', '0') and a[2] == ('num', '0'):
        return em.emit(a[0])        # expand(e, 0, 0) only resimplifies
    raise M.Untranslatable('expand with limits')


FUNCS = dict(MD.FUNCS)
FUNCS.update({'solve': _csolve, 'diff': _diff, 'expand': _expand,
    'ev': _ev, '__seq': _seq, 'makelist': _makelist, 'bfloat': _sqrt_like('approx'),
    'rat': _ratsimp, 'ratsimp': _ratsimp, 'fullratsimp': _ratsimp, 'factor': _factor,
    'rootscontract': 'simplify', 'sqrtdenest': 'simplify', 'radcan': 'simplify',
    'scanmap': None, 'nounify': None, 'verbify': None,
})
FUNCS = {k: v for k, v in FUNCS.items() if v is not None}


def flag_value(a):
    if a[0] == 'sym':
        return a[1]
    if a[0] == 'num':
        return a[1]
    if a[0] == 'list' and not a[1]:
        return '[]'
    return None


def is_flag(name, val_ast):
    if name in OPTIONS or name in HARMLESS:
        return True
    v = flag_value(val_ast)
    return v in ('true', 'false', 'all', 'none') and not name.startswith('my')


def handle_state(ast, st, src):
    """returns True if ast was a state statement (no case)"""
    if ast[0] == 'call' and ast[1] == '__seq':
        items = ast[2]
        # (kill(all), ..., done) style setup
        if any(it[0] == 'call' and it[1] in STATE_FUNCS or (it[0] == 'op' and it[1] in (':', ':='))
               for it in items):
            for it in items:
                handle_state(it, st, src)
            return True
        return False
    if ast[0] == 'op' and ast[1] in (':', '::'):
        lhs, rhs = ast[2]
        if lhs[0] != 'sym':
            if lhs[0] == 'idx' and lhs[1][0] == 'sym':
                st.taint(lhs[1][1])
            elif lhs[0] == 'list':
                for x in lhs[1]:
                    if x[0] == 'sym':
                        st.taint(x[1])
            return True
        name = lhs[1]
        if is_flag(name, rhs):
            if name in HARMLESS:
                return True
            v = flag_value(rhs)
            if OPTIONS.get(name) == v:
                st.flags.pop(name, None)
            else:
                st.flags[name] = v if v is not None else '?'
            return True
        try:
            line = '%s:=%s' % (emitter().sym(name), emitter().emit(rhs))
        except M.Untranslatable:
            line = None
        st.define(name, line, M.names_used(rhs), self_ref=name in M.names_used(rhs)[0])
        return True
    if ast[0] == 'op' and ast[1] in (':=', '::='):
        lhs, rhs = ast[2]
        if lhs[0] == 'call' and all(x[0] == 'sym' for x in lhs[2]):
            name = lhs[1]
            our = name + '_f' if len(name) == 1 or name in M.RESERVED_OUR or name in FUNCS else name
            st.fmap[name] = our
            try:
                em = emitter()
                line = '%s(%s):=%s' % (our, ','.join(em.sym(x[1]) for x in lhs[2]), em.emit(rhs))
            except M.Untranslatable:
                line = None
            used = M.names_used(rhs)
            used = (used[0] - {x[1] for x in lhs[2]}, used[1])
            st.define(name, line, used, function=True)
        elif lhs[0] == 'call':
            st.taint(lhs[1])
        elif lhs[0] == 'idx' and lhs[1][0] == 'sym':
            st.taint(lhs[1][1])
        return True
    if ast[0] == 'call' and ast[1] in STATE_FUNCS:
        f, args = ast[1], ast[2]
        if f == 'kill':
            if any(x[0] == 'sym' and x[1] in ('all', 'allbut') for x in args) or \
                    any(x[0] == 'call' and x[1] == 'allbut' for x in args):
                st.reset()
                st.fmap = {}
            else:
                for x in args:
                    if x[0] == 'sym':
                        st.defs.pop(x[1], None)
                        st.flags.pop(x[1], None)
            return True
        if f in ('remvalue', 'remfunction'):
            for x in args:
                if x[0] == 'sym':
                    st.defs.pop(x[1], None)
                    if x[1] == 'all':
                        st.defs.clear()
            return True
        if f == 'reset':
            st.flags.clear()
            return True
        if f == 'assume':
            for x in args:
                try:
                    if x[0] == 'op' and x[1] in ('>', '<', '>=') and x[2][0][0] == 'sym' and \
                            x[2][1] == ('num', '0'):
                        st.assume.append('assume(%s)' % emitter().emit(x))
                    else:
                        st.decl.append('assume(%s)' % src[:40])
                except M.Untranslatable:
                    st.decl.append('assume(...)')
            return True
        if f in ('forget', 'unassume'):
            st.assume = []
            st.decl = [d for d in st.decl if not d.startswith('assume')]
            return True
        if f == 'declare':
            st.decl.append(re.sub(r'\s+', '', src)[:60])
            return True
        if f in ('killcontext', 'newcontext', 'supcontext'):
            st.assume, st.decl = [], []
            return True
        st.decl.append(f)
        return True
    return False


def setup_lines(ast, st):
    """definitions used by ast (transitively), then assumptions.
    Returns (lines, bad names, late-binding names)"""
    syms, funcs = M.names_used(ast)
    seen, bad, late = set(), [], set()
    stack = [n for n in (syms | funcs) if n in st.defs]
    while stack:
        n = stack.pop()
        if n in seen:
            continue
        seen.add(n)
        d = st.defs[n]
        if d['bad']:
            bad.append(n)
        late |= {m for m in d['late'] if m in st.defs}
        for m in d['used']:
            if m in st.defs and m not in seen:
                stack.append(m)
    lines = sorted(x for n in seen for x in st.defs[n]['lines'])
    return [ln for _, ln in lines] + list(st.assume), bad, late


def _huge(ast):
    """numbers SymPy would spend minutes on in C code (SIGALRM cannot stop it)"""
    for n in M.walk(ast):
        if n[0] == 'num' and len(n[1]) > 400:
            return True
        if n[0] == 'op' and n[1] == '^' and n[2][1][0] == 'num' and n[2][0][0] in ('num', 'neg', 'op'):
            try:
                if float(n[2][1][1]) > 2000:
                    return True
            except ValueError:
                pass
        if n[0] in ('fact', 'fact2') and n[1][0] == 'num':
            try:
                if float(n[1][1]) > 500:
                    return True
            except ValueError:
                pass
        if n[0] == 'call' and n[1] in ('factorial', 'gamma', 'binomial', 'fib', 'bern', 'zeta', 'double_factorial') and \
                any(a[0] == 'num' and len(a[1]) > 3 for a in n[2]):
            return True
    return False


def expected_value(ast):
    conv = M.SympyConv('maxima')
    if _huge(ast):
        raise M.NoRef('huge number')
    try:
        # forked child under the memory guard: SymPy can allocate without bound
        # in C (huge powers) where the alarm-based timeout never fires
        return S.isolated(conv.conv, ast, timeout=5)
    except S.Isolated as ex:
        raise M.NoRef('sympy evaluation of the expected value failed: %s' % ex)


def has_float_ast(a):
    return any(n[0] == 'num' and re.search(r'[.eEbB]', n[1]) for n in M.walk(a))


# Maxima language / test-harness functions: a case using them is NA (no
# mathematical capability is missing), unlike a missing math function
MAXIMA_LANG = set('''% block is closeto errcatch relerror niceindices test_bessel map featurep diffcheck
string test_value print_string_2d every some orderlessp ordergreatp ratdisrep complex_floatp
complex_bfloatp tex1 flatten firstn lastn test_table freeof full_listify facts close_p csign sign
op args part length first second third last rest append cons endcons reverse apply lambda subvar
listp numberp integerp floatnump bfloatp atom mapatom symbolp ratnump constantp nonscalarp
scalarp matrixp setp listofvars sublist sublis assoc member emptyp sort unique delete
fullmap maplist outermap xreduce lreduce rreduce tree_reduce funmake buildq macroexpand
print disp display ldisp grind sconcat printf concat simplode sexplode charat slength sequal
supcase sdowncase stringp eval_string parse_string ssearch ssubst tr_warnings_get
compile translate translate_file kill_assume_labels nounify verbify tellsimp defrule apply1
apply2 applyb1 apply_nouns get remprop properties propvars declare_translated is_boole_maxima
ev_mode check_overlaps errormsg error throw catch go return for while do thru unless
mode_identity define_variable optimize timer untimer trace untrace  rat_trig_simp
random random_permutation make_random_state gensym new structures defstruct
rembox box lbox dpart lpart inpart substpart substinpart ldefint pickapart reveal
zeroequiv equal notequal maybe compare featurep askinteger asksign ask facts
listarray arrayinfo makearray array arraymake arrayapply fillarray rearray
float_approx_equal approx_equal fpprintprec bf_fft fft ift bothcoef ratcoef coeff_match
time elapsed_real_time elapsed_run_time absolute_real_time'''.split())
USERFUNC = {'foo', 'bar', 'baz', 'fun', 'func', 'ff', 'gg', 'hh'}


def make_case(cid, group, src_in, src_exp, ast, east, st):
    c = {'id': cid, 'group': group, 'src': src_in, 'src_expect': src_exp, 'lines': [''], 'check': 'equiv'}
    tb_tol = None
    if ast[0] == 'call' and ast[1] == 'test_bessel' and len(ast[2]) == 3 and ast[2][2][0] == 'num':
        # rtest14 test_bessel(f, value, digits): f agrees with value to about `digits` digits;
        # graded as approx(f) against the value (relative tolerance, 3 digits of slack, and at
        # most 10 digits: approx prints 12)
        tb_tol = 10.0 ** -min(10, max(4, int(ast[2][2][1]) - 3))
        ast, east = ('call', 'float', [ast[2][0]]), ast[2][1]
    # undefined user functions (f(x), foo(x)) stay symbolic in both systems
    allow = set()
    for n in M.walk(ast):
        if n[0] == 'call' and n[1] not in FUNCS and n[1] not in st.defs and n[1] not in st.fmap and \
                (len(n[1]) == 1 or n[1] in USERFUNC):
            allow.add(n[1])
    try:
        em = emitter()
        for nm in allow:
            em.funcs = dict(em.funcs)
            em.funcs[nm] = nm
        line = em.emit(ast)
    except M.Untranslatable as ex:
        m = re.match(r'function (\S+)$', str(ex))
        if m and m.group(1) in MAXIMA_LANG or str(ex) in ('statement sequence', 'noun form', 'subscript',
                                                            "'' evaluation", 'if') or \
                str(ex).startswith('constant') or str(ex).startswith('assignment'):
            c['na'] = 'maxima-specific: %s' % ex
        else:
            c['unsupported'] = str(ex)
        return c
    if allow:
        c['allow'] = sorted(allow)
    pre, bad, late = setup_lines(ast, st)
    if bad:
        c['na'] = 'uses untranslatable definition(s): ' + ','.join(sorted(bad))
        return c
    if ast[0] == 'sym' or ast[0] == 'list' and all(x[0] == 'sym' for x in ast[1]):
        names = [ast] if ast[0] == 'sym' else ast[1]
        if not all(x[1] in st.defs for x in names):
            c['na'] = 'maxima system variable'
            return c
    for n in M.walk(ast):
        if n[0] == 'call' and n[1] == 'taylor' and n is not ast:
            c['na'] = 'arithmetic on truncated taylor series (Maxima taylor objects)'
            return c
    c['lines'] = pre + [line]
    unc = st.uncertain()
    if late:
        unc = ', '.join(x for x in (unc, 'assignment evaluated later: ' + ','.join(sorted(late))) if x)
    if any(n[0] == 'call' and n[1] in ('acot', 'acoth') for n in M.walk(ast)):
        unc = ', '.join(x for x in (unc, 'acot branch convention') if x)
    if unc:
        c['uncertain'] = unc
    top = ast[1] if ast[0] == 'call' else None
    # indefinite integral: derivative check, independent of Maxima's form
    if top == 'integrate' and len(ast[2]) == 2 and ast[2][1][0] == 'sym':
        try:
            f = expected_value(ast[2][0])
            if isinstance(f, (list, tuple, str)):
                raise M.NoRef('integrand')
            if pre:
                raise M.NoRef('integrand uses definitions')
            c['check'] = 'antideriv'
            c['var'] = em.sym(ast[2][1][1])
            c['expect'] = S.ser(f)
            return c
        except (M.NoRef, Exception):
            pass
    if east is None:
        c['check'] = 'noref'
        return c
    if top == 'taylor':
        try:
            c['orig'] = S.ser(expected_value(ast[2][0]))
        except Exception:
            pass
    # special expected values
    if east[0] == 'sym' and east[1] in ('und', 'ind', 'infinity'):
        c['check'] = 'undef'
        if east[1] == 'ind':
            c['accept_bounded'] = True
        if east[1] == 'infinity':
            c['check'] = 'equiv'
            c['expect'] = S.ser(sp.zoo)
            c['check'] = 'undef'
        return c
    try:
        v = expected_value(east)
    except M.NoRef as ex:
        c['check'] = 'noref'
        c['noref'] = str(ex)
        return c
    except Exception as ex:
        c['check'] = 'noref'
        c['noref'] = 'sympy: %s' % str(ex)[:80]
        return c
    if isinstance(v, str):
        c['check'] = 'noref'
        return c
    if v is sp.true or v is sp.false:
        c['check'] = 'bool'
    elif top in ('solve', 'algsys'):
        c['check'] = 'set'
    elif has_float_ast(east):
        c['check'] = 'value'
        c['tol'] = tb_tol or 1e-6
    if isinstance(v, sp.Basic) and v.has(sp.zoo, sp.nan):
        c['check'] = 'noref'
        return c
    try:
        c['expect'] = S.ser(v)
    except Exception as ex:
        c['check'] = 'noref'
        c['noref'] = 'serialise: %s' % str(ex)[:80]
    return c


EV_SYMS = {'numer', 'float', 'bfloat', 'expand', 'ratsimp', 'factor', 'nouns', 'diff', 'integrate',
           'eval', 'infeval', 'simp'}


def split_top(s):
    parts, depth, cur, i = [], 0, [], 0
    while i < len(s):
        ch = s[i]
        if ch == '"':
            j = i + 1
            while j < len(s) and s[j] != '"':
                j += 2 if s[j] == '\\' else 1
            cur.append(s[i:j + 1])
            i = j + 1
            continue
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
        if ch == ',' and depth == 0:
            parts.append(''.join(cur))
            cur = []
        else:
            cur.append(ch)
        i += 1
    parts.append(''.join(cur))
    return parts


def parse_input(src):
    """`expr, opt1, opt2` (ev shorthand) -> (ev(expr, ...) ast, local flag notes)"""
    parts = split_top(src)
    if len(parts) == 1:
        return M.parse(src), []
    ast0 = M.parse(parts[0])
    keep, local = [], []
    for p in parts[1:]:
        o = M.parse(p)
        if o[0] == 'op' and o[1] == ':' and o[2][0][0] == 'sym':
            name, v = o[2][0][1], flag_value(o[2][1])
            if name in HARMLESS or OPTIONS.get(name) == v:
                continue
            local.append('%s:%s' % (name, v))
        elif o[0] == 'op' and o[1] == '=':
            keep.append(o)
        elif o[0] == 'list' and all(y[0] == 'op' and y[1] == '=' for y in o[1]):
            keep.append(o)
        elif o[0] == 'sym' and o[1] in EV_SYMS:
            if o[1] in ('numer', 'float', 'bfloat', 'expand', 'ratsimp', 'factor'):
                keep.append(o)
        elif o[0] == 'sym':
            local.append(o[1])
        else:
            raise M.ParseError('ev option')
    if keep:
        return ('call', 'ev', [ast0] + keep), local
    return ast0, local


def translate_file(path):
    base = os.path.splitext(os.path.basename(path))[0]
    text = strip_comments(open(path, encoding='utf-8', errors='replace').read())
    sts = [s for s in statements(text)]
    # drop trailing empty statement
    if sts and not sts[-1]:
        sts = sts[:-1]
    st = State()
    CUR_STATE[0] = st
    cases = []
    n = 0
    for k in range(0, len(sts) - 1, 2):
        src_in, src_exp = sts[k], sts[k + 1]
        n += 1
        cid = 'maxima/%s/%d' % (base, n)
        try:
            ast, local = parse_input(src_in)
        except M.ParseError as ex:
            taint_text(src_in, st)
            cases.append({'id': cid, 'group': base, 'src': src_in, 'src_expect': src_exp, 'lines': [''],
                          'na': 'maxima syntax not parsed: %s' % ex})
            continue
        if handle_state(ast, st, src_in):
            continue
        try:
            east = M.parse(src_exp) if src_exp else None
        except M.ParseError:
            east = None
        c = make_case(cid, base, src_in, src_exp, ast, east, st)
        if local and not c.get('na') and not c.get('unsupported'):
            c['uncertain'] = ', '.join(x for x in (c.get('uncertain', ''), ', '.join(local)) if x)
        if c.get('na') or c.get('unsupported'):
            taint_text(src_in, st)
        c.update(OVERRIDES.get(cid, {}))
        cases.append(c)
    return cases


MUTATORS = r'(?:push|pop|kill|remvalue|remfunction|declare|assume|forget|put|rem|set_random_state)'


def taint_text(src, st):
    """an untranslated statement may assign names (x: .., f(x):= .., push(v, l))"""
    for m in re.finditer(r'([A-Za-z_%][A-Za-z_0-9%]*)\s*(?:\[[^\]]*\])?\s*(?:\([^()]*\))?\s*::?=?(?!=)', src):
        st.taint(m.group(1))
    if re.search(r'\b' + MUTATORS + r'\b', src):
        for nm in re.findall(r'[A-Za-z_%][A-Za-z_0-9%]*', src):
            if nm in st.defs:
                st.taint(nm)


def main():
    S.self_memguard()           # abort above 2 GB RSS (lib/suitelib.py)
    src, out = sys.argv[1], sys.argv[2]
    files = sorted(f for f in os.listdir(src) if f.startswith('rtest') and f.endswith('.mac'))
    cases = []
    for f in files:
        if os.path.splitext(f)[0] in SKIP_FILES:
            continue
        cases.extend(translate_file(os.path.join(src, f)))
    S.save_cases(out, cases)
    na = sum(1 for c in cases if c.get('na'))
    un = sum(1 for c in cases if c.get('unsupported'))
    nr = sum(1 for c in cases if c.get('check') == 'noref' and not c.get('na') and not c.get('unsupported'))
    print('maxima: %d cases (%d na, %d unsupported, %d without checkable reference) from %d files' % (
        len(cases), na, un, nr, len(files)))


if __name__ == '__main__':
    main()
