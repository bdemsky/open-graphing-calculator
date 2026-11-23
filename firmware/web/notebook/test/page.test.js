// The built page (NB_HTML) without a browser: its contents (one file, nothing fetched,
// reproducible), then its script in a minimal DOM (test/fakedom.js) with a fake Web Serial
// port wired to the simulator. The page and the calculator's home screen are two views of
// one notebook: connecting shows the home screen's notebook (unsaved too) and leaves the
// calculator where it is, edits and runs from either side show on the other, a graph cell made
// on the calculator, locks, naming it, switching notebooks from either side, import and export,
// the unsaved mark, a missed event, the device leaving and coming back (its notebook store kept
// in a flash file); and the read-only mode of a browser without Web Serial. The real rendering
// still needs a look in Chrome (README.md).
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const { spawnSync } = require('node:child_process');
const { D, HTML, SIM, HAS_PY, sleep, until, loadPage, SimPort } = require('./pagelib.js');

const noPage = !fs.existsSync(HTML) && 'no built page (make notebook)';

test('the page is one self-contained file', { skip: noPage }, () => {
  const html = fs.readFileSync(HTML, 'utf8');
  const size = Buffer.byteLength(html);
  assert.ok(size < 1000 * 1024, 'size ' + size);
  assert.match(html, /Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; font-src data:; img-src data: blob:; connect-src 'none'/);
  assert.equal((html.match(/<script/g) || []).length, 1, 'one inline script');
  assert.doesNotMatch(html, /<script[^>]+src=|<link[^>]+href=|@import/);
  assert.doesNotMatch(html.replace(/url\(data:/g, ''), /url\(/);
  assert.doesNotMatch(html, /\s(src|href)="(https?:)?\/\//);
  assert.equal((html.match(/url\(data:font\/woff2;base64,/g) || []).length, 11);
  assert.match(html, /KaTeX 0\.16\.22, MIT License/);
  assert.doesNotMatch(html, /KEYDEF|id="keypad"|id="screen"/); // no screen mirror any more
  const { script } = D.parsePage(html);
  new vm.Script(script, { filename: 'page.js' }); // parses
  const r = spawnSync('python3', [path.join(__dirname, '..', 'build.py'), '--check', HTML], { encoding: 'utf8' });
  assert.equal(r.status, 0, 'build.py --check: ' + r.stdout + r.stderr);
});

test.describe('the page and the calculator: one notebook, the home screen\'s', { skip: noPage || (!fs.existsSync(SIM) && 'no simulator') || (!HAS_PY && 'no Python module') }, () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'calcnb-'));
  const port = new SimPort(path.join(dir, 'flash.bin'));
  let granted = false;
  const serial = {
    requestPort: async (opts) => {
      assert.equal(JSON.stringify(opts.filters), JSON.stringify([{ usbVendorId: 0x1209, usbProductId: 0x0001 }]));
      granted = true;
      return port;
    },
    getPorts: async () => (granted ? [port] : []),
    addEventListener() {},
  };
  let P;
  /* the calculator, through the page's own link (its replies must not reach the page's commands) */
  const calc = async (cmd) => {
    const r = await P.win.CalcNB.page.link().command(cmd);
    if (!r.ok) throw new Error(cmd + ': ' + r.error);
    return r.lines;
  };
  const st = async (k) => { const l = (await calc('state ' + k)).find((x) => x.startsWith(k + '=')); return l ? l.slice(k.length + 1) : null; };
  const nbField = async (n, f) => (await calc('nb cell ' + n)).filter((l) => l.startsWith(f + '=')).map((l) => l.slice(f.length + 1)).join('\n');
  const kinds = () => P.cells().map((c) => (/kind-(\w+)/.exec(c.className) || [])[1]);
  test.before(() => { P = loadPage(serial); });
  test.after(() => { P && P.win.stopTimers(); port.kill(); fs.rmSync(dir, { recursive: true, force: true }); });

  test('not connected: the welcome notebook, read-only', () => {
    assert.equal(P.$('#conn').dataset.state, 'off');
    assert.equal(P.$('#banner').hidden, true);
    assert.equal(P.$('#offline').hidden, false);
    assert.match(P.$('#offline').textContent, /Not connected/);
    assert.match(P.doc.body.className, /readonly/);
    assert.equal(P.cells().length, 1);
    assert.match(P.cells()[0].querySelector('.md').innerHTML, /<h1 id="calculator-notebook">/);
    assert.match(P.cells()[0].querySelector('.md').innerHTML, /class="katex"/); // KaTeX typeset the math
    P.key(P.cells()[0], 'b'); // no editing
    assert.equal(P.cells().length, 1);
    P.$('#btn-addcell').click(); // "+ cell" says why nothing happens
    assert.equal(P.cells().length, 1);
    assert.match(P.$('#toast').textContent, /Connect the calculator to edit/);
  });

  test('connect: the page shows the home screen\'s notebook (unsaved); the calculator stays on its home screen', async () => {
    P.$('#btn-connect').click();
    await until(() => P.$('#conn').dataset.state === 'on', 15000, 'connected');
    assert.match(P.$('#conntext').textContent, /Connected: calculator \d+\.\d+\.\d+/);
    await until(() => P.win.CalcNB.page.client() && P.win.CalcNB.page.client().doc, 5000, 'the notebook');
    assert.equal(P.$('#nbname').value, '');
    assert.match(P.$('#nbname').placeholder, /Unsaved/);
    assert.match(P.doc.title, /^Unsaved/);
    assert.equal(await st('nb_sync'), '1');
    await sleep(500);
    assert.equal(await st('screen'), 'home'); // not switched to another app
    assert.equal(await st('home_nb'), '');
    assert.equal(P.$('#offline').hidden, true);
    assert.doesNotMatch(P.doc.body.className, /readonly/);
    assert.equal(P.cells().length, 0);
    assert.equal(P.$('#empty').hidden, false);
  });

  test('a calculation on the calculator\'s home screen: a cell in the page', async () => {
    await calc('type "6*7\\n"');
    await until(() => P.cells().length === 1 && /42/.test(P.outText(0)), 5000, 'the calculation');
    assert.deepEqual(kinds(), ['math']);
    assert.equal(P.ta(0).value, '6*7');
    assert.equal(P.$('#empty').hidden, true);
  });

  test('"+ cell" adds a cell at the end, ready to type, whatever is selected', async () => {
    P.cells()[0].dispatchEvent(new D.Ev('mousedown')); // the first (only) cell selected
    const n = P.cells().length;
    P.$('#btn-addcell').click();
    await until(() => P.cells().length === n + 1, 5000, 'the new cell');
    assert.equal(P.ta(n).value, '');
    await until(() => P.doc.activeElement === P.ta(n), 5000, 'editing the new cell');
    P.key(P.ta(n), 'Escape');
    P.$('#btn-del') && P.cells()[n].dispatchEvent(new D.Ev('mousedown'));
    P.key(P.cells()[n], 'd'); P.key(P.cells()[n], 'd'); // dd deletes it again
    await until(() => P.cells().length === n, 5000, 'deleted');
  });

  test('an edit in the page: locked on the calculator, then there', async () => {
    P.click(0);
    P.key(P.cells()[0], 'Enter');
    assert.equal(P.doc.activeElement, P.ta(0));
    const id = P.win.CalcNB.page.client().doc.cells[0].id;
    await until(async () => (await st('nb_page_lock')) === id, 5000, 'the page lock');
    P.type(0, 'integrate(x^2,x)');
    await until(async () => (await nbField(1, 'src')) === 'integrate(x^2,x)', 5000, 'the source on the calculator');
    await calc('redraw');
    await calc('expect text "integrate"');
    // the calculator cannot edit it meanwhile (MENU 0 Edit in place on it)
    await calc('key UP');
    await calc('key UP');
    await calc('key MENU');
    await calc('key 0');
    assert.equal(await st('screen'), 'message');
    await calc('expect text "This cell is being edited"');
    await calc('key CLEAR');
    assert.equal(await st('home_edit'), '-1');
    P.key(P.ta(0), 'Escape');
    await until(async () => (await st('nb_page_lock')) === '', 5000, 'unlocked');
  });

  test('run from the page: once, on the calculator; outputs on both', async () => {
    P.click(0);
    P.key(P.cells()[0], 'Enter', { ctrlKey: true });
    await until(() => P.cells()[0].querySelector('.latex') && /x\^3\/3/.test(P.cells()[0].querySelector('.latex').title), 10000, 'the result');
    assert.match(P.cells()[0].querySelector('.latex').innerHTML, /katex/);
    assert.equal(await nbField(1, 'val'), 'x^3/3');
    // a Python cell: B, Y, type, Shift-Enter (a new cell after the last)
    P.key(P.cells()[0], 'b');
    await until(() => P.cells().length === 2, 5000, 'a new cell');
    P.key(P.cells()[1], 'y');
    await until(() => /kind-python/.test(P.cells()[1].className), 5000, 'python');
    P.click(1);
    P.key(P.cells()[1], 'Enter');
    P.type(1, 'print(6*7)\n"done"');
    P.key(P.ta(1), 'Enter', { shiftKey: true });
    await until(() => /42/.test(P.outText(1)) && /'done'/.test(P.outText(1)), 10000, 'the output');
    await until(() => P.cells().length === 3, 5000, 'Shift-Enter on the last cell adds one');
    assert.equal(await nbField(2, 'out'), '42');
    assert.equal(await nbField(2, 'val'), "'done'");
    P.key(P.ta(2), 'Escape');
    // the same cells on the calculator's home screen
    assert.equal(await st('history'), '3');
  });

  test('an edit on the calculator (ENTER on the Python cell): locked in the page, shown as typed, then committed', async () => {
    // home: UP to the Python cell's input (past the empty cell's two items and its own output)
    for (let i = 0; i < 9 && (await st('home_sel')) !== '2'; i++) await calc('key UP');
    assert.equal(await st('home_kind'), 'python');
    await calc('key ENTER');
    assert.equal(await st('screen'), 'notebook', (await calc('state')).filter((l) => /^(home_|nb_|toast|screen)/.test(l)).join(' '));
    await until(() => /locked/.test(P.cells()[1].className), 5000, 'locked in the page');
    assert.match(P.cells()[1].textContent, /editing on the calculator/);
    assert.equal(P.ta(1).readOnly, true);
    // the page cannot edit it
    P.click(1);
    P.key(P.cells()[1], 'Enter');
    assert.notEqual(P.doc.activeElement, P.ta(1));
    await calc('type "*2"');
    await until(() => P.ta(1).value === 'print(6*7)\n"done"*2', 5000, () => 'the draft: ' + P.ta(1).value);
    await calc('key F1'); // run: back on the home screen
    await until(() => !/locked/.test(P.cells()[1].className), 5000, 'unlocked');
    assert.equal(await st('screen'), 'home');
    await until(() => /'donedone'/.test(P.outText(1)), 10000, 'run on the calculator');
    assert.equal(P.ta(1).value, 'print(6*7)\n"done"*2');
  });

  test('a graph cell made on the calculator (y=, graph): in the page, its functions sampled there', async () => {
    await calc('key F1');
    await calc('key DOWN'); // (Y1 is empty: a second function on Y2)
    await calc('type "x^2"');
    await calc('key UP');
    await calc('type "sin(x)"');
    await calc('key F5');
    assert.equal(await st('screen'), 'home');
    assert.equal(await st('home_kind'), 'graph');
    await until(() => P.cells().length === 4 && /kind-graph/.test(P.cells()[3].className), 5000, 'the graph cell');
    await until(() => P.cells()[3].querySelector('.plot'), 5000, 'a plot');
    const plot = P.cells()[3].querySelector('.plot');
    await until(() => plot.strokes >= 2, 10000, 'both functions drawn');
    const c = P.win.CalcNB.page.client().doc.cells[3];
    assert.match(c.spec, /\nf 1 \d+ \d+ sin\(x\)\nf 1 \d+ \d+ x\^2/);
    assert.equal(P.ta(3).value, 'sin(x)\nx^2'); // (its functions, one a line: editable here too)
    assert.equal(P.ta(3).readOnly, false);
  });

  test('interrupt (Ctrl-C) stops a cell running on the calculator', async () => {
    P.click(1);
    P.key(P.cells()[1], 'Enter');
    P.type(1, 'while True:\n    pass');
    P.key(P.ta(1), 'Enter', { ctrlKey: true });
    await until(() => P.$('#conn').dataset.state === 'busy', 5000, 'running');
    assert.equal(P.prompt(1), 'In [*]:');
    assert.equal(P.$('#btn-interrupt').disabled, false);
    P.$('#btn-interrupt').click();
    await until(() => /KeyboardInterrupt/.test(P.outText(1)), 10000, 'KeyboardInterrupt');
    await until(() => P.$('#conn').dataset.state === 'on', 5000, 'idle');
  });

  test('a name for it from the page: saved on the calculator; changes marked on both until saved', async () => {
    const n = P.$('#nbname');
    n.value = 'Lab';
    n.dispatchEvent(new D.Ev('change'));
    await until(async () => (await st('home_nb')) === 'Lab', 5000, 'saved as Lab');
    await until(() => P.$('#nbname').value === 'Lab', 5000, 'the name in the page');
    assert.equal(await st('home_title'), 'Notebook: Lab');
    await calc('type "1+1\\n"');
    await until(() => P.cells().length === 5, 5000, 'the calculation');
    assert.equal(await st('nb_dirty'), '1');
    await until(() => !P.$('#dirty').hidden, 3000, 'the page mark');
    await calc('redraw');
    await calc('expect text "Notebook: *Lab"');
    await calc('nb idle 300');
    await until(async () => (await st('nb_dirty')) === '0', 5000, async () => 'the idle save: dirty=' + (await st('nb_dirty')));
    await until(() => P.$('#dirty').hidden, 3000, 'the mark gone');
    await calc('nb idle 0');
  });

  test('a missed event: the revision tells, the page reads it all again', async () => {
    const c = P.win.CalcNB.page.client();
    const before = c.resyncs;
    await calc('nb sync off');
    await calc('nb set 3 "5*5"'); // no event: the page does not follow just now
    await calc('nb sync on');
    await until(() => c.resyncs > before && P.ta(2).value === '5*5', 8000, 'resync');
  });

  test('switching notebooks: from the page (the home screen shows it), and from the calculator', async () => {
    P.$('#btn-new').click();
    await until(() => P.$('#nbname').value === 'Notebook 1', 5000, 'a new notebook');
    assert.equal(await st('home_nb'), 'Notebook 1');
    assert.equal(await st('screen'), 'home');
    assert.equal(P.cells().length, 1);
    // the list: open Lab again
    P.$('#btn-notebooks').click();
    await until(() => P.$('#nblist').children.length === 2, 5000, 'the list');
    const row = P.$('#nblist').children.find((li) => /Lab/.test(li.textContent));
    row.querySelector('button').click();
    await until(() => P.$('#nbname').value === 'Lab' && P.cells().length === 5, 5000, () => 'Lab: ' + P.$('#nbname').value + ' ' + P.cells().length);
    assert.equal(await st('home_nb'), 'Lab');
    assert.equal(await st('home_title'), 'Notebook: Lab');
    // the calculator: MENU 3 Open, the list, ENTER on Notebook 1: the page shows it
    await calc('key MENU');
    await calc('key 3');
    assert.equal(await st('screen'), 'notebook');
    for (let i = 0; i < 3 && (await st('nb_list_sel')) !== 'Notebook 1'; i++) await calc('key DOWN');
    await calc('key ENTER');
    assert.equal(await st('screen'), 'home');
    await until(() => P.$('#nbname').value === 'Notebook 1' && P.cells().length === 1, 5000, 'opened on the calculator');
    // and MENU 4 New there: an unsaved one, in the page too
    await calc('key MENU');
    await calc('key 4');
    await until(() => P.$('#nbname').value === '' && P.cells().length === 0, 5000, 'a new unsaved notebook');
    assert.equal(await st('home_nb'), '');
  });

  test('open a file from the computer: into the calculator, opened on both; export', async () => {
    const nb = { nbformat: 4, nbformat_minor: 5, metadata: {}, cells: [
      { cell_type: 'markdown', id: 'm1', metadata: {}, source: ['## From the computer'] },
      { cell_type: 'code', id: 'c1', execution_count: null, metadata: {}, outputs: [], source: ['3*14'] }] };
    const file = P.$('#file');
    file.files = [{ name: 'Lab 3.ipynb', text: async () => JSON.stringify(nb) }];
    file.dispatchEvent(new D.Ev('change'));
    await until(() => P.$('#nbname').value === 'Lab 3' && P.cells().length === 2, 10000, 'imported');
    assert.equal(await st('home_nb'), 'Lab 3');
    assert.equal(await st('nb_cells'), '2');
    P.click(1);
    P.key(P.cells()[1], 'Enter', { ctrlKey: true });
    await until(() => /42/.test(P.outText(1)), 10000, 'run');
    P.win.blobs.length = 0;
    P.$('#btn-download').click();
    await until(() => P.win.blobs.length, 5000, 'download');
    const out = JSON.parse(await P.win.blobs[0].text());
    assert.equal(out.nbformat, 4);
    assert.deepEqual(out.cells.map((c) => c.id), ['m1', 'c1']);
    assert.equal(out.cells[1].outputs[0].data['text/plain'].join(''), '42');
    assert.equal(await st('nb_dirty'), '0'); // an export saves on the calculator first
  });

  test('the device drops off and comes back: the page reconnects and reads the notebook again', async () => {
    const opens = port.opens;
    port.drop();
    await until(() => P.$('#conn').dataset.state === 'wait', 5000, 'reconnecting state');
    assert.equal(P.$('#offline').hidden, false);
    assert.match(P.doc.body.className, /readonly/);
    assert.equal(P.cells().length, 2); // the last state, read-only
    await until(() => P.$('#conn').dataset.state === 'on' && port.opens === opens + 1, 15000, 'reconnected');
    // a "rebooted" calculator (its store kept): the notebook it has is the page's
    await until(() => P.win.CalcNB.page.client() && P.win.CalcNB.page.client().doc, 10000, 'the notebook again');
    assert.equal(P.$('#nbname').value, await st('home_nb'));
    assert.equal(await st('screen'), 'home');
  });

  test('disconnect', async () => {
    P.$('#btn-connect').click();
    await until(() => P.$('#conn').dataset.state === 'off', 5000, 'disconnected');
    assert.match(port.sent, /nb sync off/);
    assert.equal(P.$('#offline').hidden, false);
  });
});

test('without Web Serial: a banner, and notebooks open read-only', { skip: noPage }, async () => {
  const P = loadPage(null);
  try {
    assert.equal(P.$('#banner').hidden, false);
    assert.match(P.$('#banner').textContent, /Open this file in Chrome or Edge to connect to the calculator/);
    assert.match(P.doc.body.className, /readonly/);
    const nb = { nbformat: 4, nbformat_minor: 5, metadata: {}, cells: [
      { cell_type: 'markdown', id: 'm', metadata: {}, source: ['## Hello $\\frac{1}{2}$'] },
      { cell_type: 'code', id: 'c', execution_count: 7, metadata: {}, source: ['1+1'],
        outputs: [{ output_type: 'execute_result', execution_count: 7, metadata: {}, data: { 'text/plain': ['2'], 'text/latex': ['$2$'] } },
          { output_type: 'display_data', metadata: {}, data: { 'image/png': 'iVBORw0KGgo=', 'text/plain': 'img' } }] }] };
    const file = P.$('#file');
    file.files = [{ name: 'Read.ipynb', text: async () => JSON.stringify(nb) }];
    file.dispatchEvent(new D.Ev('change'));
    await until(() => P.cells().length === 2 && P.prompt(1) === 'In [7]:', 3000, 'opened');
    assert.match(P.cells()[0].querySelector('.md').innerHTML, /<h2 id="hello">Hello <span class="katex">/);
    assert.equal(P.ta(1).readOnly, true);
    assert.ok(P.cells()[1].querySelector('img'));
    assert.match(P.cells()[1].querySelector('.latex').innerHTML, /katex/);
    P.key(P.cells()[1], 'b'); // no editing
    assert.equal(P.cells().length, 2);
    P.win.blobs.length = 0;
    P.$('#btn-download').click(); // a copy can still be downloaded
    await until(() => P.win.blobs.length, 2000, 'download');
    assert.equal(JSON.parse(await P.win.blobs[0].text()).cells.length, 2);
  } finally { P.win.stopTimers(); }
});
