"""
Shared machinery for the third-party test suites (tests/suites/<suite>/):
case files, batched execution of build/cas_repl with crash isolation, the
grader (reusing tests/wester/run_wester.py's SymPy equivalence checker), and
result/summary writing.

Case record (one JSON object per line in <suite>/cases.jsonl):
  id        unique id, e.g. "maxima/rtest_limit/12"
  group     file / chapter used for per-group totals
  src       the original input text (for the report)
  src_expect the original expected answer text
  lines     our-syntax input lines; the last one is graded
  check     equiv | antideriv | set | bool | value | undef | noref
  expect    serialised expected value (see ser/deser), or None
  var       variable for antideriv
  domain    {sym: pos|real|neg|int|lo:hi}  sampling domains (default pos)
  uncertain non-empty = translation may change the meaning; a WRONG becomes
            TRANSLATION-UNCERTAIN
  na        not applicable (Maxima-only programming, display, flags ...)
  unsupported  no translation exists (missing function in our CAS)

Categories (CATS below).  WRONG/CRASH are the bug signals.
"""
import json
import os
import queue
import random
import re
import signal
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, ProcessPoolExecutor
import multiprocessing as mp

import sympy as sp

LIB = os.path.dirname(os.path.abspath(__file__))
SUITES = os.path.dirname(LIB)
TESTS = os.path.dirname(SUITES)
ROOT = os.path.dirname(TESTS)            # firmware/cas
sys.path.insert(0, os.path.join(TESTS, 'wester'))
import run_wester as W  # noqa: E402  (grader: to_sympy, equiv, BUILTINS ...)


def _piecewise_cv(*args):
    # our syntax is piecewise(cond1, value1, ..., [otherwise]); SymPy wants (value, cond)
    args = list(args)
    pairs = []
    while len(args) >= 2:
        c, v = args.pop(0), args.pop(0)
        pairs.append((v, c))
    pairs.append((args[0] if args else sp.nan, True))
    return sp.Piecewise(*pairs)


W.LOCAL['piecewise'] = _piecewise_cv

REPL = os.path.join(ROOT, 'build', 'cas_repl')
ASAN_REPL = os.path.join(ROOT, 'build', 'cas_repl_asan')
CATS = ['PASS', 'WRONG', 'CRASH', 'TIMEOUT', 'ERROR', 'UNEVAL', 'UNSUPPORTED',
        'TRANSLATION-UNCERTAIN', 'UNGRADED', 'NA']


# ------------------------------------------------------- serialisation ---
def ser(v):
    """SymPy-ish value -> JSON-able"""
    if isinstance(v, W.Eqn):
        return {'t': 'eq', 'l': ser(v.lhs), 'r': ser(v.rhs)}
    if isinstance(v, tuple) and v and v[0] == 'Eq':
        return {'t': 'eq', 'l': ser(v[1]), 'r': ser(v[2])}
    if isinstance(v, tuple) and v and v[0] == 'Matrix':
        return {'t': 'list', 'v': [ser(r) for r in v[1]]}
    if isinstance(v, sp.MatrixBase):
        return {'t': 'list', 'v': [[ser(x) for x in row] for row in v.tolist()]}
    if isinstance(v, (list, tuple)):
        return {'t': 'list', 'v': [ser(x) for x in v]}
    if isinstance(v, bool) or v is sp.true or v is sp.false:
        return {'t': 'bool', 'v': bool(v)}
    if isinstance(v, str):
        return {'t': 'str', 'v': v}
    return {'t': 'expr', 'v': sp.srepr(sp.sympify(v))}


def deser(d):
    t = d['t']
    if t == 'eq':
        return W.Eqn(deser(d['l']), deser(d['r']))
    if t == 'list':
        return [deser(x) for x in d['v']]
    if t == 'bool':
        return sp.true if d['v'] else sp.false
    if t == 'str':
        return d['v']
    if isinstance(d['v'], dict):
        return deser(d['v'])
    if isinstance(d['v'], list):
        return [deser(x) if isinstance(x, dict) else x for x in d['v']]
    return eval(d['v'], SREPR_NS)


SREPR_NS = {k: getattr(sp, k) for k in dir(sp) if not k.startswith('_')}
SREPR_NS.update({'Symbol': sp.Symbol, 'Integer': sp.Integer, 'Rational': sp.Rational,
                 'Float': sp.Float, 'ExprCondPair': sp.functions.elementary.piecewise.ExprCondPair,
                 'Str': sp.core.symbol.Str, 'MatrixElement': sp.matrices.expressions.matexpr.MatrixElement})


def pretty(d):
    """expected value -> short readable text"""
    if d is None:
        return ''
    try:
        v = deser(d)
    except Exception:
        return str(d)[:200]
    return _pretty(v)


def _pretty(v):
    if isinstance(v, W.Eqn):
        return '%s = %s' % (_pretty(v.lhs), _pretty(v.rhs))
    if isinstance(v, list):
        return '[' + ', '.join(_pretty(x) for x in v) + ']'
    return str(v).replace('**', '^')


def load_cases(path):
    out = []
    with open(path) as f:
        for line in f:
            if line.strip():
                out.append(json.loads(line))
    return out


def save_cases(path, cases):
    with open(path, 'w') as f:
        for c in cases:
            f.write(json.dumps(c, sort_keys=True) + '\n')


# ------------------------------------------------------- memory guard ---
# The suites must not take the machine down (a translator once grew to
# 15 GB): every child process (cas_repl, forked grader) is registered with
# MEMW, a thread polling `ps` that kills a child whose RSS (with its
# descendants) exceeds MEM_LIMIT_MB, or the largest child when the total
# (children + this process) exceeds MEM_TOTAL_MB.  A killed case is recorded
# as ERROR(memory).  At most MAX_JOBS children run at once.
MEM_LIMIT_MB = int(os.environ.get('SUITES_MEM_LIMIT_MB', 2048))
MEM_TOTAL_MB = int(os.environ.get('SUITES_MEM_TOTAL_MB', 6144))
MAX_JOBS = 3
CASE_WALL = 150           # s without a graded case before a grader child is killed


def _ps_table():
    try:
        out = subprocess.run(['ps', '-A', '-o', 'pid=,ppid=,rss='], capture_output=True,
                             text=True, timeout=10).stdout
    except (OSError, subprocess.SubprocessError):
        return {}
    t = {}
    for ln in out.split('\n'):
        f = ln.split()
        if len(f) == 3 and f[0].isdigit() and f[1].isdigit() and f[2].isdigit():
            t[int(f[0])] = (int(f[1]), int(f[2]))       # pid -> (ppid, rss KiB)
    return t


class MemWatch:
    def __init__(self, limit_mb=MEM_LIMIT_MB, total_mb=MEM_TOTAL_MB, period=0.1):
        self.limit, self.total, self.period = limit_mb * 1024, total_mb * 1024, period
        self.pids, self.killed = set(), {}
        self.lock = threading.Lock()
        self.th = None

    def add(self, pid):
        with self.lock:
            self.pids.add(pid)
            if self.th is None:
                self.th = threading.Thread(target=self._loop, daemon=True)
                self.th.start()

    def done(self, pid):
        """unregister; -> peak RSS in MiB if the child was killed for memory, else None"""
        with self.lock:
            self.pids.discard(pid)
            return self.killed.pop(pid, None)

    def _kill(self, pid, tree, rss):
        self.killed[pid] = rss // 1024
        for q in tree:
            try:
                os.kill(q, signal.SIGKILL)
            except OSError:
                pass

    def _loop(self):
        me = os.getpid()
        while True:
            time.sleep(self.period)
            with self.lock:
                roots = set(self.pids)
            if not roots:
                continue
            t = _ps_table()
            kids = {}
            for pid, (pp, _) in t.items():
                kids.setdefault(pp, []).append(pid)
            usage = {}
            for r in roots:
                if r not in t:
                    continue
                tree, st = [], [r]
                while st:
                    q = st.pop()
                    tree.append(q)
                    st.extend(kids.get(q, []))
                usage[r] = (sum(t[q][1] for q in tree if q in t), tree)
            with self.lock:
                for r, (rss, tree) in usage.items():
                    if rss > self.limit and r in self.pids:
                        self._kill(r, tree, rss)
                live = {r: u for r, u in usage.items() if r in self.pids and r not in self.killed}
                tot = sum(u[0] for u in live.values()) + t.get(me, (0, 0))[1]
                if tot > self.total and live:
                    r = max(live, key=lambda k: live[k][0])
                    print('memory guard: total %d MB > %d MB, killing pid %d (%d MB)' % (
                        tot // 1024, self.total // 1024, r, live[r][0] // 1024), file=sys.stderr)
                    self._kill(r, live[r][1], live[r][0])


MEMW = MemWatch()


class Isolated(Exception):
    """isolated(): the child died, was killed by the memory guard or timed out"""


def isolated(fn, *args, timeout=30):
    """Run fn(*args) in a forked child under the memory guard (MEM_LIMIT_MB)
    and return its (picklable) result; exceptions of fn are re-raised.  Used
    by translators for SymPy evaluations that can allocate without bound in C
    code (huge integer powers) where a SIGALRM timeout never fires."""
    ctx = mp.get_context('fork')
    rd, wr = ctx.Pipe(duplex=False)

    def child():
        try:
            try:
                wr.send(('ok', fn(*args)))
            except BaseException as ex:          # noqa: BLE001  (reported to the parent)
                try:
                    wr.send(('exc', ex))
                except Exception:
                    wr.send(('exc', RuntimeError(str(ex)[:200])))
        finally:
            wr.close()
            os._exit(0)
    pr = ctx.Process(target=child)
    pr.start()
    MEMW.add(pr.pid)
    wr.close()
    try:
        if not rd.poll(timeout):
            pr.kill()
            raise Isolated('timeout')
        tag, val = rd.recv()
    except (EOFError, OSError):
        tag, val = None, None
    finally:
        pr.join()
        killed = MEMW.done(pr.pid)
        rd.close()
    if tag is None:
        raise Isolated('memory guard (%s MB)' % killed if killed is not None else 'child died')
    if tag == 'exc':
        raise val
    return val


def self_memguard(limit_mb=MEM_LIMIT_MB, period=0.5):
    """For in-process tools (translators): abort this process if its own RSS
    exceeds limit_mb, instead of letting it eat the machine."""
    me = os.getpid()

    def loop():
        while True:
            time.sleep(period)
            rss = _ps_table().get(me, (0, 0))[1]
            if rss > limit_mb * 1024:
                print('memory guard: %s RSS %d MB > %d MB, aborting' % (
                    os.path.basename(sys.argv[0]), rss // 1024, limit_mb), file=sys.stderr, flush=True)
                os._exit(3)
    threading.Thread(target=loop, daemon=True).start()


# ------------------------------------------------------------- runner ---
def _env():
    env = dict(os.environ)
    env.setdefault('ASAN_OPTIONS', 'detect_leaks=0:abort_on_error=0')
    env.setdefault('UBSAN_OPTIONS', 'print_stacktrace=1:halt_on_error=1')
    return env


def run_chunk(cases, repl, timeout_ms, mem_mb=None, slack_s=15.0, extra=()):
    """Run cases in one cas_repl process (':reset' between cases).  Returns
    {id: run} where run = {status: ok|crash|hang, out, stderr, time}.  A case
    that kills the process is recorded and the rest is rerun in a new
    process; a case after which no output arrives within its time budget is
    recorded as a hang (the engine did not honour the -t interrupt)."""
    results = {}
    todo = list(cases)
    while todo:
        lines, spans = [], []
        for c in todo:
            st = len(lines)
            lines.extend(c['lines'])
            spans.append((st, len(lines)))
            lines.append(':reset')
        argv = [repl, '-b', '-t', str(timeout_ms)] + (['-m', str(mem_mb)] if mem_mb else []) + list(extra)
        t0 = time.time()
        p = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, errors='replace', env=_env(),
                             bufsize=1)
        MEMW.add(p.pid)
        q = queue.Queue()
        errbuf = []

        def writer():
            try:
                for ln in lines:
                    p.stdin.write(ln + '\n')
                p.stdin.close()
            except (BrokenPipeError, OSError, ValueError):
                pass

        def reader():
            for ln in p.stdout:
                q.put(ln.rstrip('\n'))
            q.put(None)

        def ereader():
            for ln in p.stderr:
                if len(errbuf) < 400:
                    errbuf.append(ln)

        ths = [threading.Thread(target=f, daemon=True) for f in (writer, reader, ereader)]
        for th in ths:
            th.start()
        outs, times = [], []
        hung = False
        ci = 0           # case whose output we are waiting for
        tcase = time.time()
        while True:
            # budget for the line being produced
            while ci < len(spans) and len(outs) >= spans[ci][1] + 1:
                ci += 1
                tcase = time.time()
            if ci < len(spans):
                nl = spans[ci][1] - spans[ci][0]
                budget = timeout_ms / 1000.0 * nl + slack_s
                remaining = budget - (time.time() - tcase)
            else:
                remaining = slack_s
            try:
                ln = q.get(timeout=max(0.05, remaining))
            except queue.Empty:
                hung = True
                p.kill()
                break
            if ln is None:
                break
            outs.append(ln)
            times.append(time.time())
        try:
            p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
        for th in ths[1:]:
            th.join(timeout=5)
        memkill = MEMW.done(p.pid)
        stderr = ''.join(errbuf)
        rc = p.returncode
        done_upto = len(todo)
        prev_t = t0
        for i, (c, (s0, s1)) in enumerate(zip(todo, spans)):
            if len(outs) >= s1:
                tend = times[s1 - 1] if s1 - 1 < len(times) else time.time()
                results[c['id']] = {'status': 'ok', 'out': outs[s1 - 1], 'pre_out': outs[s0:s1 - 1],
                                    'time': round(tend - prev_t, 3)}
                prev_t = times[s1] if s1 < len(times) else tend
                continue
            # this case did not complete
            if memkill is not None:
                results[c['id']] = {'status': 'memory', 'out': '', 'pre_out': outs[s0:],
                                    'stderr': 'killed by the memory guard at %d MB RSS' % memkill,
                                    'time': round(time.time() - prev_t, 3)}
            elif hung:
                results[c['id']] = {'status': 'hang', 'out': '', 'pre_out': outs[s0:],
                                    'stderr': stderr[-3000:], 'time': round(time.time() - prev_t, 3)}
            else:
                results[c['id']] = {'status': 'crash', 'out': '', 'pre_out': outs[s0:], 'rc': rc,
                                    'stderr': stderr[-6000:], 'time': round(time.time() - prev_t, 3),
                                    'after': i}
            done_upto = i + 1
            break
        todo = todo[done_upto:]
    return results


def run_cases(cases, repl, timeout_ms=5000, jobs=MAX_JOBS, chunk=40, mem_mb=None, progress=True,
              isolate_crashes=True, extra=()):
    jobs = max(1, min(jobs, MAX_JOBS))
    todo = [c for c in cases if not c.get('na') and not c.get('unsupported')]
    chunks = [todo[i:i + chunk] for i in range(0, len(todo), chunk)]
    res = {}
    t0 = time.time()
    done = [0]

    def work(ch):
        r = run_chunk(ch, repl, timeout_ms, mem_mb, extra=extra)
        done[0] += 1
        if progress and (done[0] % 20 == 0 or done[0] == len(chunks)):
            print('  ran %d/%d chunks (%.0fs)' % (done[0], len(chunks), time.time() - t0),
                  file=sys.stderr, flush=True)
        return r

    with ThreadPoolExecutor(max_workers=jobs) as ex:
        for r in ex.map(work, chunks):
            res.update(r)
    if isolate_crashes:
        # confirm every crash/hang in a process of its own
        bad = [c for c in todo if res[c['id']]['status'] != 'ok']
        if bad:
            def again(c):
                return c['id'], run_chunk([c], repl, timeout_ms, mem_mb, extra=extra)[c['id']]
            with ThreadPoolExecutor(max_workers=jobs) as ex:
                for cid, r in ex.map(again, bad):
                    first = res[cid]
                    if r['status'] == 'ok':
                        r = dict(first)
                        r['note'] = 'only after earlier cases in the same process (state dependent)'
                    res[cid] = r
    return res


# ------------------------------------------------------------- grading ---
BRANCHY = (sp.log, sp.Abs, sp.sign, sp.arg, sp.re, sp.im, sp.conjugate, sp.floor, sp.ceiling,
           sp.asin, sp.acos, sp.atan, sp.acot, sp.asec, sp.acsc, sp.asinh, sp.acosh, sp.atanh,
           sp.acoth, sp.asech, sp.acsch, sp.Heaviside, sp.Max, sp.Min, sp.LambertW, sp.atan2,
           sp.Piecewise, sp.polylog, sp.frac, sp.Mod, sp.DiracDelta)


def branchy(e):
    if isinstance(e, W.Eqn):
        return branchy(e.lhs) or branchy(e.rhs)
    if isinstance(e, (list, tuple)):
        return any(branchy(x) for x in e)
    if not isinstance(e, sp.Basic):
        return True
    for n in sp.preorder_traversal(e):
        if isinstance(n, BRANCHY):
            return True
        if isinstance(n, sp.Pow) and not n.exp.is_Integer:
            return True
        if isinstance(n, sp.Function) and not isinstance(n, (sp.exp, sp.sin, sp.cos, sp.tan, sp.cot,
                                                              sp.sec, sp.csc, sp.sinh, sp.cosh,
                                                              sp.tanh, sp.coth, sp.sech, sp.csch)):
            if n.func.__name__ not in ('gamma', 'erf', 'erfc', 'erfi', 'factorial', 'binomial'):
                return True
        if isinstance(n, (sp.core.relational.Relational, sp.logic.boolalg.Boolean)) and not isinstance(n, sp.Symbol):
            return True
    return False


def complex_check(e1, e2, rng, npts=4, tol=1e-8):
    """True/False/None: compare at random complex points (only meaningful
    for expressions without branch cuts)."""
    syms = W.free_syms(e1, e2)
    good = 0
    for _ in range(npts * 3):
        if good >= npts:
            break
        sub = {s: sp.Rational(rng.randint(-1500, 1500), 1000) + sp.I * sp.Rational(rng.randint(-1500, 1500), 1000)
               for s in syms}
        try:
            v1 = complex(sp.N(e1.subs(sub), 30))
            v2 = complex(sp.N(e2.subs(sub), 30))
        except (TypeError, ValueError, ZeroDivisionError, OverflowError, AttributeError):
            continue
        if any(q != q or abs(q) == float('inf') for q in (v1.real, v1.imag, v2.real, v2.imag)):
            continue
        if abs(v1 - v2) > tol * (1 + abs(v1) + abs(v2)):
            return False
        good += 1
    return True if good >= 2 else None


def equiv_full(ours, exp_, rng, domain, tol=1e-7):
    """positive-real spot checks (run_wester.equiv), then complex points when
    neither side has branch cuts.  Returns (True|False|None, note)."""
    r = W.equiv(ours, exp_, rng, domain, tol)
    if r is not True:
        return r, ''
    if isinstance(ours, sp.Basic) and isinstance(exp_, sp.Basic) and ours.free_symbols and \
            not branchy(ours) and not branchy(exp_) and \
            not any(domain.get(str(q)) == 'int' for q in W.free_syms(ours, exp_)):
        # (integer-valued symbols: identities like sin(pi*k) = 0 only hold at integers)
        try:
            rc = W.with_timeout(20, complex_check, ours, exp_, rng)
        except W.Timeout:
            rc = None
        if rc is False:
            return False, 'agrees at positive reals but differs at complex points'
    return True, ''


def is_undef_out(out):
    return out in ('undef', 'ERROR 2: division by zero') or out.startswith('ERROR 2') or \
        out.startswith('ERROR 4')


def grade_antideriv(ours, integrand, var, rng, domain, npts=5):
    """d/dvar(ours) == integrand at random points.  Numeric (mpmath central
    difference at 40 digits) first; mixed results fall back to SymPy
    simplify of the symbolic difference.  Returns (cat, note)."""
    import mpmath
    if isinstance(ours, (list, W.Eqn)) or not isinstance(ours, sp.Basic):
        return 'WRONG', 'not an expression'
    syms = sorted((ours.free_symbols | integrand.free_symbols) - {var}, key=str)
    mpmath.mp.dps = 70
    try:
        F = sp.lambdify([var] + syms, ours, modules='mpmath')
        f = sp.lambdify([var] + syms, integrand, modules='mpmath')
    except Exception as ex:
        return 'UNGRADED', 'lambdify: %s' % str(ex)[:80]
    good = bad = 0
    h = mpmath.mpf('1e-28')
    cbad = 0
    worst = 0
    for _ in range(npts * 4):
        if good + bad >= npts:
            break
        x0 = mpmath.mpf(rng.randint(150, 1850)) / 1000
        ps = []
        for s in syms:
            d = domain.get(str(s), 'pos')
            if d == 'int':
                ps.append(mpmath.mpf(rng.randint(1, 5)))
            elif d == 'real':
                ps.append(mpmath.mpf(rng.randint(-1700, 1700)) / 1000)
            else:
                ps.append(mpmath.mpf(rng.randint(200, 1700)) / 1000)
        try:
            d1 = (F(x0 + h, *ps) - F(x0 - h, *ps)) / (2 * h)
            v = f(x0, *ps)
            d1, v = mpmath.mpc(d1), mpmath.mpc(v)
            # a huge antiderivative (Gamma(n+101, a x)/a^101 against x^101 Gamma(n, a x)) cancels
            # in the difference: more digits so the cancellation does not swamp the result
            Fx = abs(mpmath.mpc(F(x0, *ps)))
            if Fx > mpmath.mpf(10) ** 20 * (1 + abs(v)):
                with mpmath.workdps(70 + int(mpmath.log10(Fx)) + 10):
                    d1 = mpmath.mpc((F(x0 + h, *ps) - F(x0 - h, *ps)) / (2 * h))
        except Exception:
            continue
        if not (mpmath.isfinite(d1.real) and mpmath.isfinite(d1.imag) and
                mpmath.isfinite(v.real) and mpmath.isfinite(v.imag)):
            continue
        err = abs(d1 - v) / (1 + abs(v))
        if err >= 1e-12:
            try:
                # a mismatch at an ill-conditioned point does not count: F changes by more
                # than a thousandth of itself over the step h (e^(e^(2 e^3/x)) near x = 0.15
                # has |F'/F| ~ 10^118), so the central difference is not in its linear
                # regime and says nothing about F'
                Fx = abs(mpmath.mpc(F(x0, *ps)))
                if Fx > 0 and abs(d1) * h > Fx * mpmath.mpf('1e-3'):
                    continue
            except Exception:
                continue
        if err < 1e-12:
            good += 1
        elif abs(v.imag) > 1e-30 * (1 + abs(v)):
            # integrand not real here: our antiderivative may sit on a branch
            # cut (e.g. asin of a real > 1); only real points decide
            cbad += 1
        else:
            bad += 1
            worst = max(worst, float(err))
    if bad == 0 and good >= 2:
        return 'PASS', '' if not cbad else 'mismatch only where the integrand is complex (%d points)' % cbad
    if bad == 0 and cbad and good < 2:
        bad = cbad
    if good + bad < 2:
        sym = _sym_check(ours, integrand, var)
        if sym:
            return 'PASS', 'symbolic check'
        return 'UNGRADED', 'no numeric sample points'
    # disagreement somewhere: try the symbolic check before calling it wrong
    if _sym_check(ours, integrand, var):
        return 'PASS', 'symbolic check (numeric mismatch at %d/%d points)' % (bad, good + bad)
    if good:
        return 'WRONG', 'derivative mismatch at %d/%d points (possible branch issue), rel err %.2g' % (
            bad, good + bad, worst)
    return 'WRONG', 'derivative != integrand at all %d points, rel err %.2g' % (bad, worst)


def _sym_check(ours, integrand, var):
    try:
        vr = sp.Symbol(str(var) + '_r', positive=True)
        d = sp.diff(ours.subs(var, vr), vr) - integrand.subs(var, vr)
        return W.with_timeout(20, lambda: sp.simplify(d) == 0)
    except W.Timeout:
        return False
    except Exception:
        return False


IDENT = re.compile(r'([A-Za-z_][A-Za-z_0-9]*)\(')


def classify(case, run, seed=1):
    """-> (category, note)"""
    rng = random.Random(seed)
    if case.get('na'):
        return 'NA', case['na']
    if case.get('unsupported'):
        return 'UNSUPPORTED', case['unsupported']
    st = run['status']
    if st == 'memory':
        return 'ERROR', 'ERROR(memory): %s' % run.get('stderr', 'killed by the memory guard')
    if st == 'crash':
        first = ''
        for ln in run.get('stderr', '').split('\n'):
            if 'ERROR: AddressSanitizer' in ln or 'runtime error' in ln or 'SUMMARY' in ln:
                first = ln.strip()
                break
        return 'CRASH', ('rc=%s %s %s' % (run.get('rc'), first, run.get('note', ''))).strip()
    if st == 'hang':
        return 'TIMEOUT', 'no output within the time budget: -t interrupt not honoured ' + run.get('note', '')
    for ln, o in zip(case['lines'][:-1], run.get('pre_out', [])):
        m = re.match(r'^ERROR (\d+): ?(.*)$', o.strip())
        if m:
            if m.group(1) == '5':
                return 'TIMEOUT', 'setup line timed out: ' + ln
            if case.get('check') == 'error':
                return 'PASS', 'error expected'
            return 'UNSUPPORTED' if m.group(1) in ('3', '6') else 'ERROR', 'setup line %s: %s' % (ln, o)
    out = run['out'].strip()
    chk = case.get('check', 'equiv')
    m = re.match(r'^ERROR (\d+): ?(.*)$', out)
    if m:
        code, msg = int(m.group(1)), m.group(2)
        if chk in ('error', 'undef'):
            return 'PASS', 'error expected: ' + msg
        if code == 5:
            return 'TIMEOUT', msg
        if code == 3:
            return 'UNSUPPORTED', msg
        if code == 6:
            return 'UNEVAL', 'ERROR 6: ' + msg
        return 'ERROR', 'ERROR %d: %s' % (code, msg)
    if chk == 'noref':
        return 'NA', 'no checkable reference (ran without crash)'
    if chk == 'error':
        if out in ('undef', 'inf', '-inf'):
            return 'PASS', 'divergence reported as ' + out
        return _wrong(case, 'expected an error')
    if chk == 'undef':
        if out in ('undef',):
            return 'PASS', ''
        if case.get('accept_bounded') and out.startswith('['):
            return 'PASS', 'interval'
        return _wrong(case, 'expected undefined (%s)' % case.get('src_expect', ''))
    allowed = set(case.get('allow', []))
    names = set(IDENT.findall(out))
    unknown = {n for n in names if n not in W.BUILTINS and n not in allowed and len(n) > 1}
    if unknown:
        return 'UNSUPPORTED', 'unknown function(s) echoed: ' + ','.join(sorted(unknown))
    unev = {n for n in names if n in W.UNEVAL_NAMES and n not in allowed}
    if unev:
        return 'UNEVAL', 'unevaluated ' + ','.join(sorted(unev))
    if case.get('lines') and re.sub(r'[ ()]', '', out) == re.sub(r'[ ()]', '', case['lines'][-1]) and \
            IDENT.search(out):
        return 'UNEVAL', 'returned unevaluated (output = input)'
    if re.search(r'[A-Za-z_]\(\[|\^\[|\]\^', out) and not re.match(r'^\w+\(\[\[', out):
        return 'UNEVAL', 'function not mapped over a list'
    try:
        return W.with_timeout(60, _grade, case, out, rng)
    except W.Timeout:
        return 'UNGRADED', 'grader timeout'
    except Exception as ex:
        return 'UNGRADED', 'grader exception: %s' % str(ex)[:150]


def _wrong(case, note):
    if case.get('uncertain'):
        return 'TRANSLATION-UNCERTAIN', 'WRONG? %s [%s]' % (note, case['uncertain'])
    return 'WRONG', note


def _parse_out(out):
    return W.to_sympy(out)


def _grade(case, out, rng):
    chk = case.get('check', 'equiv')
    domain = case.get('domain', {}) or {}
    tol = float(case.get('tol', 1e-7))
    if chk == 'bool':
        if out not in ('true', 'false'):
            return 'UNEVAL', 'no truth value'
        want = 'true' if deser(case['expect']) == sp.true else 'false'
        return ('PASS', '') if out == want else _wrong(case, 'boolean')
    if chk == 'str':
        want = deser(case['expect'])
        return ('PASS', '') if out.replace(' ', '') == want.replace(' ', '') else _wrong(case, 'text')
    try:
        ours = _parse_out(out)
    except Exception as ex:
        return 'UNGRADED', 'cannot parse our output: %s' % str(ex)[:100]
    if chk == 'antideriv':
        var = sp.Symbol(case.get('var', 'x'))
        f = deser(case['expect'])
        cat, note = grade_antideriv(ours, f, var, rng, domain)
        if cat == 'WRONG':
            return _wrong(case, note)
        return cat, note
    exp_ = deser(case['expect'])
    if chk == 'set':
        return _grade_set(case, ours, exp_, rng, domain, tol)
    if chk == 'value':
        r = W.equiv(ours, exp_, rng, domain, tol)
        if r is None:
            return 'UNGRADED', 'undecided'
        return ('PASS', '') if r else _wrong(case, 'numeric value')
    # equiv
    r, note = equiv_full(ours, exp_, rng, domain, tol)
    if r is None:
        return 'UNGRADED', 'equivalence undecided'
    if r is False:
        if case.get('orig'):
            try:
                if W.equiv(ours, deser(case['orig']), rng, domain, tol):
                    return 'UNEVAL', 'series: returned the function itself, not its expansion'
            except Exception:
                pass
        if W.has_float(ours) and not W.has_float(exp_):
            return 'UNGRADED', 'float result disagrees with exact reference (precision loss?)'
        return _wrong(case, note)
    if W.has_float(ours) and not W.has_float(exp_):
        return 'UNEVAL', 'numeric instead of exact'
    return 'PASS', note


def _sols(v, var=None):
    """solution list -> list of values (x = v -> v); [[x=..,y=..]] kept"""
    if not isinstance(v, list):
        v = [v]
    out = []
    for s in v:
        if isinstance(s, W.Eqn) and isinstance(s.lhs, sp.Symbol):
            out.append(s.rhs)
        elif isinstance(s, list) and all(isinstance(q, W.Eqn) for q in s):
            out.append(sorted([(str(q.lhs), q.rhs) for q in s], key=lambda t: t[0]))
        else:
            out.append(s)
    return out


def _grade_set(case, ours, exp_, rng, domain, tol):
    a, b = _sols(ours), _sols(exp_)
    if a and isinstance(a[0], list) or b and isinstance(b[0], list):
        # systems: compare sorted var lists
        def norm(L):
            return [[v for _, v in s] if isinstance(s, list) else [s] for s in L]
        a, b = norm(a), norm(b)
    rest = list(b)
    extra, undecided = [], False
    for o in a:
        for j, r0 in enumerate(rest):
            rr = W.equiv(o, r0, rng, domain, tol)
            if rr:
                rest.pop(j)
                break
            if rr is None:
                undecided = True
        else:
            extra.append(o)
    if not extra and not rest:
        if W.has_float(a) and not W.has_float(b):
            return 'UNEVAL', 'numeric instead of exact'
        return 'PASS', ''
    if undecided:
        return 'UNGRADED', 'set comparison undecided'
    if not extra and rest:
        return _wrong(case, 'missing %d solution(s)' % len(rest))
    return _wrong(case, '%d unexpected / %d missing' % (len(extra), len(rest)))


def _classify_one(args):
    case, run = args
    try:
        cat, note = classify(case, run)
    except Exception as ex:
        cat, note = 'UNGRADED', 'classify exception %s' % str(ex)[:100]
    return case['id'], cat, note


def classify_all(cases, runs, jobs=MAX_JOBS):
    jobs = max(1, min(jobs, MAX_JOBS))
    items = []
    for c in cases:
        r = runs.get(c['id'], {'status': 'ok', 'out': '', 'time': 0})
        items.append((c, r))
    out = {}
    if jobs <= 1:
        for it in items:
            cid, cat, note = _classify_one(it)
            out[cid] = (cat, note)
        return out
    # Each batch is graded in a forked child that sends one result per case.
    # A child that dies (SymPy stack overflow, memory blow-up on a huge
    # expression) or sends nothing for CASE_WALL seconds (stuck in C code,
    # where the SIGALRM timeout of with_timeout never fires) is killed; the
    # case it was on becomes UNGRADED/ERROR and a new child takes the rest.
    batches = [items[i:i + 32] for i in range(0, len(items), 32)]

    def grade_batch(batch):
        done = []
        while len(done) < len(batch):
            rest = batch[len(done):]
            ctx = mp.get_context('fork')
            rd, wr = ctx.Pipe(duplex=False)
            pr = ctx.Process(target=_grade_child, args=(rest, wr))
            pr.start()
            MEMW.add(pr.pid)
            wr.close()
            got = 0
            stuck = False
            while got < len(rest):
                try:
                    if not rd.poll(CASE_WALL):
                        stuck = True
                        break
                    done.append(rd.recv())
                    got += 1
                except (EOFError, OSError):
                    break
            if got < len(rest):
                try:
                    os.kill(pr.pid, signal.SIGKILL)
                except OSError:
                    pass
            pr.join()
            rd.close()
            memkill = MEMW.done(pr.pid)
            if got < len(rest):
                cid = rest[got][0]['id']
                if memkill is not None:
                    # the grader (SymPy), not our CAS, ran out of memory
                    done.append((cid, 'UNGRADED', 'grader killed by the memory guard at %d MB RSS' % memkill))
                elif stuck:
                    done.append((cid, 'UNGRADED', 'grader stuck for %d s (in C code) and killed' % CASE_WALL))
                else:
                    done.append((cid, 'UNGRADED', 'grader process died (exit %s)' % pr.exitcode))
        return done

    with ThreadPoolExecutor(max_workers=jobs) as ex:
        for res in ex.map(grade_batch, batches):
            for cid, cat, note in res:
                out[cid] = (cat, note)
    return out


def _grade_child(batch, wr):
    try:
        sys.setrecursionlimit(20000)
        for it in batch:
            wr.send(_classify_one(it))
    finally:
        wr.close()


# ------------------------------------------------------------- results ---
def write_results(suite_dir, name, cases, runs, cls, repl, extra_md=''):
    recs = []
    for c in cases:
        r = runs.get(c['id'], {'out': '', 'time': 0})
        cat, note = cls[c['id']]
        rec = {'id': c['id'], 'group': c.get('group', ''), 'src': c.get('src', ''),
               'src_expect': c.get('src_expect', ''), 'input': c['lines'][-1] if c.get('lines') else '',
               'pre': c.get('lines', [])[:-1], 'output': r.get('out', ''), 'expect': pretty(c.get('expect')),
               'check': c.get('check', ''), 'category': cat, 'note': note, 'time': r.get('time', 0)}
        if cat in ('CRASH', 'TIMEOUT') and r.get('stderr'):
            rec['stderr'] = r['stderr'][-4000:]
        recs.append(rec)
    base = os.path.join(suite_dir, 'results' + ('_asan' if 'asan' in os.path.basename(repl) else ''))
    with open(base + '.json', 'w') as f:
        json.dump(recs, f, indent=0)
    write_md(base + '.md', name, recs, repl, extra_md)
    return recs


def totals(recs, key='group'):
    groups = []
    for r in recs:
        if r[key] not in groups:
            groups.append(r[key])
    tab = {g: {c: 0 for c in CATS} for g in groups}
    tot = {c: 0 for c in CATS}
    for r in recs:
        tab[r[key]][r['category']] += 1
        tot[r['category']] += 1
    return groups, tab, tot


def write_md(path, name, recs, repl, extra_md=''):
    groups, tab, tot = totals(recs)
    L = ['# %s suite results' % name, '', 'binary: `%s`' % os.path.relpath(repl, ROOT), '',
         '| group | ' + ' | '.join(CATS) + ' | total |', '|---' * (len(CATS) + 2) + '|']
    for g in groups:
        L.append('| %s | ' % g + ' | '.join(str(tab[g][c]) for c in CATS) + ' | %d |' % sum(tab[g].values()))
    L.append('| **total** | ' + ' | '.join('**%d**' % tot[c] for c in CATS) + ' | **%d** |' % sum(tot.values()))
    L.append('')
    if extra_md:
        L += [extra_md, '']
    for cat in ['WRONG', 'CRASH', 'TIMEOUT', 'TRANSLATION-UNCERTAIN', 'ERROR', 'UNGRADED']:
        rs = [r for r in recs if r['category'] == cat]
        if not rs:
            continue
        L += ['## %s (%d)' % (cat, len(rs)), '']
        for r in rs[:3000]:
            L.append('- **%s** `%s` -> `%s` (expected `%s`) %s' % (
                r['id'], (' ; '.join(r['pre'] + [r['input']]))[:400], r['output'][:300],
                r['expect'][:300], r['note'][:300]))
        L.append('')
    with open(path, 'w') as f:
        f.write('\n'.join(L) + '\n')


def print_totals(name, recs):
    _, _, tot = totals(recs)
    print('%-8s ' % name + '  '.join('%s %d' % (c, tot[c]) for c in CATS if tot[c]) +
          '  total %d' % len(recs), flush=True)
