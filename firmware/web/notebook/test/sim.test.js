// The page's protocol layer and its notebook client (sync.js) against the simulator
// (build/sim/calcsim): a child process's stdin/stdout stand in for the Web Serial port. Also:
// the CALC drive serves the built page as CALCULATOR.HTML byte for byte, and a notebook the
// calculator exports passes nbformat.
// Environment (set by `make notebook-test`): CALCSIM, NB_HTML, NB_OUT, NB_PYTHON,
// NB_PY_MODULE (0: this configuration has no Python), NB_WEBUI (0: no webui module).
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawn, spawnSync } = require('node:child_process');
const P = require('../src/proto.js');
const M = require('../src/model.js');
const S = require('../src/sync.js');

const FW = path.resolve(__dirname, '..', '..', '..');
const SIM = process.env.CALCSIM || path.join(FW, 'build', 'sim', 'calcsim');
const HTML = process.env.NB_HTML || path.join(FW, 'build', 'gen', 'calculator_notebook.html');
const OUT = process.env.NB_OUT || path.join(FW, 'build', 'gen', 'notebook-test');
const HAS_PY = process.env.NB_PY_MODULE !== '0';
const HAS_WEB = process.env.NB_WEBUI !== '0';
const skip = !fs.existsSync(SIM) && 'no simulator (make sim)';

/* One simulator per test file, killed at the end whatever happens. */
let child = null, link = null;
function start() {
  child = spawn(SIM, [], { stdio: ['pipe', 'pipe', 'inherit'] });
  link = new P.CalcLink((bytes) => new Promise((res, rej) => child.stdin.write(Buffer.from(bytes), (e) => (e ? rej(e) : res()))));
  child.stdout.on('data', (d) => link.feed(d));
  child.on('exit', () => link.close('simulator exited'));
}
function stop() {
  if (child && child.exitCode === null) {
    try { child.stdin.end('quit\n'); } catch (e) { /* closed */ }
    const t = setTimeout(() => child.kill('SIGKILL'), 3000);
    child.on('exit', () => clearTimeout(t));
  }
}
process.on('exit', () => { if (child && child.exitCode === null) child.kill('SIGKILL'); });
async function until(f, what, ms = 10000) {
  const t0 = Date.now();
  while (!f()) {
    if (Date.now() - t0 > ms) throw new Error('timed out waiting for ' + what);
    await new Promise((r) => setTimeout(r, 20));
  }
}

test.describe('simulator', { skip }, () => {
  test.before(start);
  test.after(stop);

  test('version and log lines', async () => {
    const v = await link.version();
    assert.match(v.version, /^\d+\.\d+\.\d+/);
    assert.equal(v.platform, 'sim');
  });

  test('keys and typing reach the calculator', async () => {
    await link.key('HOME');
    await link.type('2+3\n');
    const st = await link.kv('state last_output');
    assert.equal(st.last_output, '5');
    await link.key('CLEAR');
  });

  test('CAS eval', async () => {
    const r = await link.evalCas('diff(sin(x),x)');
    assert.deepEqual(r, { ok: true, result: 'cos(x)' });
    assert.equal((await link.evalCas('1/0')).ok, false);
  });

  test.describe('python', { skip: !HAS_PY && 'no Python module in this configuration' }, () => {
    test('a cell: output, value, LaTeX; state persists', async () => {
      let streamed = '';
      const r = await link.pyexec('a = 6\nfor i in range(3):\n    print(i, "π")\ncalc.diff(sin(x)*a, x)', (t) => { streamed += t; });
      assert.equal(r.stdout, '0 π\n1 π\n2 π\n');
      assert.equal(streamed, r.stdout);
      assert.equal(r.value, '6*cos(x)');
      assert.equal(r.latex, '6\\cos\\left(x\\right)');
      assert.equal((await link.pyexec('a * 7')).value, '42');
    });

    test('an error gives a traceback', async () => {
      const r = await link.pyexec('print("before")\n1/0');
      assert.equal(r.stdout, 'before\n');
      assert.equal(r.error.ename, 'ZeroDivisionError');
      assert.equal(r.error.traceback[0], 'Traceback (most recent call last):');
    });

    test('a long cell (more than one console line) and a long value', async () => {
      const code = Array.from({ length: 40 }, (_, i) => 'v' + i + ' = ' + i + '  # "quoted" \\ text').join('\n') + '\nlist(range(200))';
      assert.ok(Buffer.byteLength(code) > 1000);
      const r = await link.pyexec(code);
      assert.equal(r.error, null);
      assert.deepEqual(JSON.parse(r.value), Array.from({ length: 200 }, (_, i) => i));
      assert.equal((await link.pyexec('v39')).value, '39');
    });

    test('interrupt stops a running loop (Ctrl-C)', async () => {
      const p = link.pyexec('n = 0\nwhile True:\n    n += 1');
      await new Promise((r) => setTimeout(r, 300));
      await link.interrupt();
      const r = await p;
      assert.equal(r.error && r.error.ename, 'KeyboardInterrupt');
      assert.ok(+(await link.pyexec('n')).value > 0);
      assert.equal((await link.pyexec('1+1')).value, '2'); // and the next cell runs normally
    });

    test('the notebook client: a notebook made, run, sampled, locked, exported, imported', async (t) => {
      const c = new S.NbClient(link);
      link.onLog = (t) => c.handleLog(t);
      await link.command('nb store wipe');
      await c.start();
      assert.equal(c.doc.open, 0);
      await c.create('Sim test');
      await until(() => c.doc.open && c.doc.name === 'Sim test', 'opened');
      const first = c.doc.cells[0].id;
      await c.setSource(first, 'integrate(x^2,x)');
      const py = await c.insert(1, 'python');
      await c.setSource(py, 'from calc import *\nprint("hello")\nsolve(x**2-2, x)');
      const md = await c.insert(0, 'text');
      await c.setSource(md, '# Test\n\nThe integral $\\int_0^1 x\\,dx$.');
      const g = await c.insert(3, 'graph');
      await c.setSource(g, 'sin(x)/x, -6, 6');
      const bad = await c.insert(4, 'python');
      await c.setSource(bad, 'undefined_name');
      await c.runAll().catch(() => {});
      await until(() => c.cell(bad) && c.cell(bad).ename === 'NameError', 'run all');
      assert.deepEqual(c.doc.cells.map((x) => x.kind), ['text', 'math', 'python', 'graph', 'python']);
      assert.equal(c.cell(first).val, 'x^3/3');
      assert.equal(c.cell(py).out, 'hello\n');
      assert.equal(c.cell(py).val, '[-sqrt(2), sqrt(2)]');
      assert.match(c.cell(g).plot, /^\(*sin\(x\)\)*\/\(*x\)*$/); // (the CAS text of its spec's function)
      assert.match(c.cell(g).spec, /\nf 1 0 0 sin\(x\)\/x$/);
      assert.deepEqual(c.doc.cells.slice(1).map((x) => x.count), [1, 2, 3, 4]);
      assert.equal(c.doc.dirty, true);
      // the graph sampled for the page: values, the hole at 0 left out
      const s = await c.sample(g, 41, -2, 2);
      assert.equal(s.ys.length, 41);
      assert.ok(Math.abs(s.ys[0] - Math.sin(-2) / -2) < 1e-8);
      assert.ok(Number.isNaN(s.ys[20]));
      // locks: the page's, refused for a cell the calculator edits
      await c.lock(first);
      assert.equal((await link.kv('state nb_page_lock')).nb_page_lock, first);
      await c.unlock(first);
      // the home screen edits the math cell in place (UP to its input, MENU 0)
      await link.command('key CLEAR');
      for (let i = 0; i < 12 && (await link.kv('state home_sel')).home_sel !== '2'; i++) await link.command('key UP');
      await link.command('key MENU');
      await link.command('key 0');
      await until(() => c.cell(first).lock === 'calc', 'the calculator lock');
      await assert.rejects(c.lock(first), /editing on the calculator/);
      await assert.rejects(c.setSource(first, 'no'), /editing on the calculator/);
      await link.command('key CLEAR'); // the line cleared, then given up
      await link.command('key CLEAR');
      await until(() => c.cell(first).lock === '', 'unlocked');
      // export: valid nbformat (the calculator's own writer, its plot as a PNG)
      const text = await c.exportJson();
      assert.equal((await link.kv('state nb_dirty')).nb_dirty, '0'); // saved first
      fs.mkdirSync(OUT, { recursive: true });
      const file = path.join(OUT, 'sim-test.ipynb');
      fs.writeFileSync(file, text);
      const back = M.fromIpynb(text, 'sim-test.ipynb');
      assert.deepEqual(back.cells.map((x) => x.kind), ['markdown', 'math', 'python', 'graph', 'python']);
      assert.match(JSON.parse(text).cells[3].outputs[0].data['image/png'], /^iVBORw0KGgo/);
      const py3 = process.env.NB_PYTHON;
      if (py3 && fs.existsSync(py3)) {
        const v = spawnSync(py3, ['-c', 'import sys, nbformat; nb = nbformat.read(sys.argv[1], as_version=4); nbformat.validate(nb); print("valid", len(nb.cells))', file], { encoding: 'utf8' });
        assert.equal(v.status, 0, v.stderr);
        assert.match(v.stdout, /valid 5/);
      } else t.diagnostic('nbformat validation skipped (make notebook-venv)');
      // import: the file under another name, opened
      await c.importIpynb('Copy', new TextEncoder().encode(text));
      await until(() => c.doc.name === 'Copy' && c.doc.cells.length === 5, 'imported');
      assert.equal(c.doc.cells[2].val, '[-sqrt(2), sqrt(2)]');
      const names = (await c.list()).map((x) => x.name).sort();
      assert.deepEqual(names, ['Copy', 'Sim test']);
      await assert.rejects(c.importIpynb('Big', new Uint8Array(300 * 1024)), /too large/);
      link.onLog = null;
      await c.stop();
    });
  });

  test('the CALC drive serves the page as CALCULATOR.HTML', { skip: (!HAS_WEB || !fs.existsSync(HTML)) && 'no webui module / page' }, async () => {
    const html = fs.readFileSync(HTML);
    assert.ok((await link.command('usb host on')).ok);
    const ls = await link.command('usb ls');
    const line = ls.lines.find((l) => l.startsWith('CALCULATOR.HTML'));
    assert.ok(line, ls.lines.join('\n'));
    assert.match(line, new RegExp('\\s' + html.length + ' ro$'));
    const crc = await link.command('usb crc CALCULATOR.HTML');
    assert.deepEqual(crc.lines, ['size=' + html.length + ' crc32=' + P.crc32(html).toString(16).padStart(8, '0')]);
    const readme = await link.command('usb cat README.TXT');
    assert.ok(readme.lines.some((l) => l.includes('CALCULATOR.HTML')));
    await link.command('usb host off');
  });
});

// Notebooks written by the calculator's own Notebook app (make nb-validate exports them to
// build/nbcheck): every one opens here, its Math and Graph cells are recognised, and it is
// written back with the same cells.
const DEVICE_DIR = process.env.NB_DEVICE_DIR || path.join(FW, 'build', 'nbcheck');
const deviceFiles = fs.existsSync(DEVICE_DIR) ? fs.readdirSync(DEVICE_DIR).filter((f) => f.endsWith('.ipynb')) : [];
test('notebooks written by the calculator open in the page', { skip: !deviceFiles.length && 'no exported notebooks (make nb-validate)' }, () => {
  const kinds = new Set();
  for (const f of deviceFiles) {
    const text = fs.readFileSync(path.join(DEVICE_DIR, f), 'utf8');
    const nb = M.fromIpynb(text, f);
    const orig = JSON.parse(text);
    assert.equal(nb.cells.length, orig.cells.length, f);
    nb.cells.forEach((c, i) => {
      kinds.add(c.kind);
      const cal = orig.cells[i].metadata && orig.cells[i].metadata.calculator;
      if (cal && cal.kind === 'math') assert.equal(c.kind, 'math', f);
      if (cal && cal.kind === 'graph') { assert.equal(c.kind, 'graph', f); assert.ok(c.range.xmin < c.range.xmax); }
    });
    const back = M.toIpynb(nb);
    assert.deepEqual(back.cells.map((c) => c.id), orig.cells.map((c) => c.id));
  }
  assert.ok(kinds.has('math') && kinds.has('graph') && kinds.has('python') && kinds.has('markdown'), [...kinds].join(','));
});
