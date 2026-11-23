// The notebook model (src/model.js), markdown (src/markdown.js) and the plotter's pure parts (src/plot.js).
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const M = require('../src/model.js');
const MD = require('../src/markdown.js');
const PL = require('../src/plot.js');

let n = 0;
M.setIdSource(() => 'c' + (++n));

test('a new notebook round-trips through .ipynb', () => {
  const nb = new M.Notebook();
  nb.cells[0].source = 'print("hi")\n1+1';
  nb.cells[0].execution_count = 1;
  nb.cells[0].outputs = [{ output_type: 'stream', name: 'stdout', text: 'hi\n' },
    { output_type: 'execute_result', execution_count: 1, data: { 'text/plain': '2' }, metadata: {} }];
  const md = nb.insert(0, 'markdown', '# Title\n\n$x^2$');
  const m = nb.insert(2, 'math', 'diff(sin(x), x)\nsolve(x^2=2, x)');
  const g = nb.insert(3, 'graph', 'sin(x)\n1/x');
  g.range = { xmin: -5, xmax: 5, ymin: -2, ymax: 2 };
  const j = M.toIpynb(nb, { firmware: '0.1.0' });
  assert.equal(j.nbformat, 4);
  assert.equal(j.nbformat_minor, 5);
  assert.deepEqual(j.metadata.kernelspec, { display_name: 'Python 3', language: 'python', name: 'python3' });
  assert.deepEqual(j.metadata.calculator, { format: 1, firmware: '0.1.0' });
  assert.equal(j.cells[0].cell_type, 'markdown');
  assert.deepEqual(j.cells[0].source, ['# Title\n', '\n', '$x^2$']);
  assert.deepEqual(j.cells[1].outputs[0].text, ['hi\n']);
  // math: the calculator's convention
  assert.deepEqual(j.cells[2].metadata, { calculator: { input: 'diff(sin(x), x)\nsolve(x^2=2, x)', kind: 'math' } });
  assert.equal(j.cells[2].source.join(''), 'import calc\ncalc.eval(r"diff(sin(x), x)")\ncalc.eval(r"solve(x^2=2, x)")');
  assert.deepEqual(j.cells[3].metadata.calculator, { input: 'sin(x)\n1/x', kind: 'graph', xmax: 5, xmin: -5, ymin: -2, ymax: 2 });
  assert.equal(j.cells[3].source.join(''), 'import calc\ncalc.plot(r"sin(x)", -5, 5)\ncalc.plot(r"1/x", -5, 5)');
  for (const c of j.cells) assert.match(c.id, /^[a-zA-Z0-9_-]{1,64}$/);
  const back = M.fromIpynb(JSON.stringify(j), 'x.ipynb');
  assert.deepEqual(back.cells.map((c) => c.kind), ['markdown', 'python', 'math', 'graph']);
  assert.equal(back.cells[1].source, 'print("hi")\n1+1');
  assert.equal(back.cells[1].outputs[0].text, 'hi\n');
  assert.equal(back.cells[2].source, m.source);
  assert.deepEqual(back.cells[3].range, g.range);
  assert.equal(back.cells[0].id, md.id);
  assert.deepEqual(M.toIpynb(back, { firmware: '0.1.0' }), j);
});

test('a notebook written by the calculator opens', () => {
  const dev = {
    cells: [
      { cell_type: 'code', execution_count: 2, id: 'a1', metadata: { calculator: { input: 'x\u00b2\u2212\u03c0', kind: 'math' } },
        outputs: [{ data: { 'text/latex': '$x^{2}-\\pi$', 'text/plain': ['x^2-pi'] }, execution_count: 2, metadata: {}, output_type: 'execute_result' }],
        source: ['import calc\n', 'calc.eval(r"x^2-pi")'] },
      { cell_type: 'code', execution_count: 3, id: 'a2', metadata: { calculator: { input: 'sin(x), \u207b5, 5', kind: 'graph', xmax: 5, xmin: -5 } },
        outputs: [{ data: { 'image/png': 'iVBORw0KGgo=', 'text/plain': '<plot>' }, metadata: { calculator: { plot: 'sin(x)', xmin: -5, xmax: 5 } }, output_type: 'display_data' }],
        source: 'import calc\ncalc.plot(r"sin(x)", -5, 5)' },
      { cell_type: 'raw', id: 'a3', metadata: {}, source: 'raw text' },
    ],
    metadata: { calculator: { firmware: '0.1.0', format: 1 } }, nbformat: 4, nbformat_minor: 5,
  };
  const nb = M.fromIpynb(dev, 'Dev.ipynb');
  assert.equal(nb.cells[0].kind, 'math');
  assert.equal(nb.cells[0].source, 'x\u00b2\u2212\u03c0');
  assert.equal(M.toCas(nb.cells[0].source), 'x^2-pi');
  assert.equal(nb.cells[0].outputs[0].data['text/plain'], 'x^2-pi');
  assert.equal(nb.cells[1].kind, 'graph');
  assert.equal(nb.cells[1].source, 'sin(x)');
  assert.deepEqual(nb.cells[1].range, { xmin: -5, xmax: 5, ymin: null, ymax: null });
  assert.equal(nb.cells[2].kind, 'raw');
  // the image of a plot drawn on the calculator stays
  const j = M.toIpynb(nb);
  assert.equal(j.cells[1].outputs[0].data['image/png'], 'iVBORw0KGgo=');
});

test('the one-line graph form, CAS characters, literals', () => {
  assert.deepEqual(M.parseGraphLine('max(x, 2), -3, 4'), { expr: 'max(x, 2)', xmin: -3, xmax: 4 });
  assert.deepEqual(M.parseGraphLine('max(x, 2)'), { expr: 'max(x, 2)', xmin: null, xmax: null });
  assert.equal(M.toCas('\u221a(x)\u00d72\u00f73 \u2264 sin\u207b\u00b9(x)'), 'sqrt(x)*2/3 <= sin^(-1)(x)');
  // del: the \u00b7 and \u00d7 of \u2207\u00b7F and \u2207\u00d7F stay, elsewhere they multiply; \u2207\u00b2 is \u2207^2
  assert.equal(M.toCas('\u2207\u00b7[x,y]+\u2207 \u00d7[y,-x]\u00b72'), '\u2207\u00b7[x,y]+\u2207 \u00d7[y,-x]*2');
  assert.equal(M.toCas('\u2207\u00b2(x\u00b7y)'), '\u2207^2(x*y)');
  // an ODE's initial conditions reach the CAS as calls y(0), y'(0)
  assert.equal(M.toCas("dsolve([y''+y=0,y(0)=1,y'(0)=0],y,x)"), "dsolve([y''+y=0,y(0)=1,y'(0)=0],y,x)");
  assert.equal(M.mathSource("dsolve([y'=2\u00b7y,y(0)=3],y,x)"), "import calc\ncalc.eval(r\"dsolve([y'=2*y,y(0)=3],y,x)\")");
  assert.equal(M.rawLiteral('a"b'), '"a\\"b"');
  assert.equal(M.rawLiteral('x\\'), '"x\\\\"');
  assert.equal(M.rawLiteral('\\frac'), 'r"\\frac"');
});

test('graph outputs: samples in metadata, PNG when saving, not-a-notebook errors', () => {
  const nb = new M.Notebook();
  nb.cells[0].kind = 'graph';
  nb.cells[0].source = 'x';
  nb.cells[0].outputs = [{ output_type: 'display_data', data: { 'text/plain': '<plot of x>' },
    metadata: { calculator: { plot: 'x', xmin: -1, xmax: 1, series: [{ expr: 'x', ys: [-1, null, 1] }] } } }];
  const j = M.toIpynb(nb, { png: () => 'UE5H' });
  assert.equal(j.cells[0].outputs[0].data['image/png'], 'UE5H');
  assert.deepEqual(j.cells[0].outputs[0].metadata.calculator.series[0].ys, [-1, null, 1]);
  assert.throws(() => M.fromIpynb('{"nbformat": 3, "cells": []}'), /nbformat 3/);
  assert.throws(() => M.fromIpynb('[]'), /not a Jupyter/);
  assert.throws(() => M.fromIpynb('not json'));
});

/* The notebook client (sync.js) against a scripted calculator: events in order, a gap read again. */
test('sync: events, the revision, a missed event, cells as the page shows them', async () => {
  const S = require('../src/sync.js');
  assert.deepEqual(S.parseEvent('nb1 12 1 cell ab12'), { rev: 12, dirty: true, type: 'cell', args: ['ab12'] });
  assert.equal(S.parseEvent('power off'), null);
  assert.equal(S.parseEvent('nb2 1 0 doc'), null); // another protocol version
  assert.equal(S.nameOf('My: lab/1.ipynb'), 'My_ lab_1');
  assert.equal(S.nameOf('.ipynb'), 'Imported');
  const cell = (id, src, extra) => JSON.stringify(Object.assign({ id, kind: 'math', lock: '', src, out: '', val: '', tex: '', ename: '',
    evalue: '', tb: '', plot: '', xmin: -10, xmax: 10, count: 0, ran: 0 }, extra || {}));
  let rev = 5, srcB = 'b', docs = 0;
  const cmds = [];
  const link = {
    command: async (c) => {
      cmds.push(c);
      if (c === 'nb sync on') return { ok: true, lines: ['rev=' + rev, 'sync=1'] };
      if (c === 'nb doc') {
        docs++;
        return { ok: true, lines: ['head={"name":"T","where":"flash","running":"","note":"","open":1,"rev":' + rev + ',"dirty":0,"counter":0,"cells":2}',
          'cell=' + cell('a', 'x'), 'cell=' + cell('b', srcB)] };
      }
      if (c === 'nb get b') return { ok: true, lines: ['cell=' + cell('b', srcB, { val: '2', tex: '2', count: 1 })] };
      if (c === 'nb ids') return { ok: true, lines: ['ids=b a'] };
      return { ok: true, lines: [] };
    },
  };
  const c = new S.NbClient(link);
  const seen = [];
  c.on('cell', (x) => seen.push('cell ' + x.id)).on('order', () => seen.push('order')).on('run', (id) => seen.push('run ' + id));
  await c.start();
  assert.equal(c.rev, 5);
  assert.equal(docs, 1);
  srcB = 'b2';
  c.handleLog('nb1 6 1 run b');
  c.handleLog('nb1 7 1 cell b');
  c.handleLog('nb1 8 1 run -');
  c.handleLog('nb1 9 1 order');
  c.handleLog('nb1 4 0 meta'); // old: ignored
  await new Promise((r) => setTimeout(r, 10));
  assert.deepEqual(seen, ['run b', 'cell b', 'run ', 'order']);
  assert.equal(c.rev, 9);
  assert.deepEqual(c.doc.cells.map((x) => x.id), ['b', 'a']);
  assert.equal(c.doc.dirty, true);
  const out = S.outputsOf(c.cell('b'));
  assert.deepEqual(out.map((o) => o.output_type), ['execute_result']);
  assert.equal(out[0].data['text/latex'], '$2$');
  rev = 20;
  c.handleLog('nb1 12 1 cell a'); // 10 and 11 were lost: everything is read again
  await new Promise((r) => setTimeout(r, 10));
  assert.equal(docs, 2);
  assert.equal(c.rev, 20);
  // long sources go in pieces
  await c.setSource('a', 'x'.repeat(1000));
  assert.ok(cmds.includes('nb setbuf a'));
  assert.ok(cmds.filter((x) => x.startsWith('py buf ')).length >= 3);
});

test('markdown: blocks, inline, math, and nothing unsafe', () => {
  const math = (t, d) => (d ? '[D:' : '[I:') + t + ']';
  const h = MD.renderMarkdown('# A *b*\n\nx $a_1 * b_2$ and *c* $$\\sum_k k$$\n\n- [x] done\n- todo\n\n3. three\n4. four', { renderMath: math });
  assert.match(h, /<h1 id="a-b">A <em>b<\/em><\/h1>/);
  assert.match(h, /\[I:a_1 \* b_2\]/);
  assert.match(h, /<em>c<\/em>/);
  assert.match(h, /\[D:\\sum_k k\]/);
  assert.match(h, /<input type="checkbox" disabled checked> done/);
  assert.match(h, /<ol start="3">/);
  const bad = MD.renderMarkdown('<img src=x onerror=alert(1)> [a](javascript:alert(1)) ![p](http://example.com/p.png) <b>b</b>');
  assert.doesNotMatch(bad, /<img|<b>|javascript:|href="http:\/\/example/);
  assert.match(bad, /&lt;img src=x/);
  const att = MD.renderMarkdown('![p](attachment:p.png)', { attachments: { 'p.png': { 'image/png': 'AAAA' } } });
  assert.match(att, /<img alt="p" src="data:image\/png;base64,AAAA">/);
  assert.match(MD.renderMarkdown('`$x$` and \\$5'), /<code>\$x\$<\/code> and \$5/);
  assert.match(MD.renderMarkdown('a | b\n--|--\n1 | 2'), /<td>1<\/td><td>2<\/td>/);
});

test('plot: ticks, gaps at poles and holes, automatic y range', () => {
  const t = PL.ticks(-10, 10, 4);
  assert.equal(t.step, 5);
  assert.deepEqual(t.values, [-10, -5, 0, 5, 10]);
  assert.equal(PL.ticks(0, 1, 5).step, 0.2);
  assert.equal(PL.tickLabel(0.30000000000000004, 0.1), '0.3');
  // tan x across pi/2: one segment each side
  const n = 101, x0 = 0, x1 = 3, ys = [];
  for (let i = 0; i < n; i++) ys.push(Math.tan(x0 + (i * (x1 - x0)) / (n - 1)));
  const segs = PL.segments(x0, x1, ys, { ymin: -5, ymax: 5 });
  assert.equal(segs.length, 2);
  // a hole (NaN / null) splits too; a steep but continuous function does not
  assert.equal(PL.segments(0, 4, [1, 2, null, 4, 5], { ymin: 0, ymax: 6 }).length, 2);
  assert.equal(PL.segments(0, 1, [0, 100, 200], { ymin: 0, ymax: 10 }).length, 1);
  const spiky = Array.from({ length: 100 }, (_, i) => (i === 50 ? 1e9 : i === 51 ? null : Math.sin(i / 10)));
  const r = PL.autoRange([spiky]);
  assert.ok(r.ymin < -0.9 && r.ymin > -2 && r.ymax > 0.9 && r.ymax < 2, JSON.stringify(r));
  const c = PL.autoRange([[3, 3]]); // a constant: around it
  assert.ok(c.ymin < 3 && c.ymax > 3);
});

test('pyTranslate: calculator symbols become Python outside strings and comments', () => {
  assert.equal(M.pyTranslate('x^2 + y²'), 'x**2 + y**2');
  assert.equal(M.pyTranslate('√(2)*π'), 'sqrt(2)*pi');
  assert.equal(M.pyTranslate('a ≤ b ≠ c'), 'a <= b != c');
  assert.equal(M.pyTranslate('print("x^2") # 2^3'), 'print("x^2") # 2^3');
  assert.equal(M.pyTranslate("s = 'it\\'s ^'\nx^3"), "s = 'it\\'s ^'\nx**3");
  assert.equal(M.pyTranslate('x**2'), 'x**2');
});
