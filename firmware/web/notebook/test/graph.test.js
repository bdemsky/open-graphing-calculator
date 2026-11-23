// Graph cells in both views (docs/NOTEBOOK.md "Graph cells", "The browser page"): the page shows every
// graph cell the calculator has, the calculator's changes (functions, window, pan / zoom, the graph
// app) reach the page, the page's edits (functions, range, pan / zoom, released) reach the
// calculator's cell, the lock while either side edits one, a cell without a function drawn on
// neither side, 3D graph cells made in the page and drawn here from `nb sample3d`, and a page that
// connects after the cells were made. The built page in a fake DOM, the simulator as the device.
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { D, HTML, SIM, HAS_PY, sleep, until, loadPage, SimPort, fakeSerial } = require('./pagelib.js');

const skip = (!fs.existsSync(HTML) && 'no built page (make notebook)') || (!fs.existsSync(SIM) && 'no simulator') ||
  (!HAS_PY && 'no Python module');

test.describe('graph cells: the calculator and the page', { skip }, () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'calcnb-g-'));
  const port = new SimPort(path.join(dir, 'flash.bin'));
  let P;
  const S = () => P.win.CalcNB.page.state;
  const client = () => P.win.CalcNB.page.client();
  const calc = async (cmd) => {
    const r = await P.win.CalcNB.page.link().command(cmd);
    if (!r.ok) throw new Error(cmd + ': ' + r.error);
    return r.lines;
  };
  const st = async (k) => { const l = (await calc('state ' + k)).find((x) => x.startsWith(k + '=')); return l ? l.slice(k.length + 1) : null; };
  const calcCells = async () => (await calc('nb doc')).filter((l) => l.startsWith('cell=')).map((l) => JSON.parse(l.slice(5)));
  const view = (i) => S().views.get(S().nb.cells[i].id);
  const plotOf = (i) => { const k = view(i).kept.get('plot'); return k && k.p; };
  const kinds = () => P.cells().map((c) => (/kind-(\w+)/.exec(c.className) || [])[1]);
  const specs = () => P.win.CalcNB.page.client().doc.cells.map((c) => c.spec);
  const deselect = async () => { if ((await st('home_sel')) !== '-1') await calc('key CLEAR'); };

  test.before(async () => {
    P = loadPage(fakeSerial(port));
    P.$('#btn-connect').click();
    await until(() => P.$('#conn').dataset.state === 'on', 15000, 'connected');
    await until(() => client() && client().doc, 5000, 'the notebook');
  });
  test.after(() => {
    if (P) { P.win.CalcNB.page.state.wanted = false; P.win.stopTimers(); } // (no reconnecting to a new simulator)
    port.kill();
    fs.rmSync(dir, { recursive: true, force: true });
  });

  test('a cell made a graph in the page (the reported bug): drawn on both sides, of its own function', async () => {
    await calc('type "x^2\\n"');
    await until(() => P.cells().length === 1, 5000, 'the calculation');
    P.click(0);
    P.key(P.cells()[0], 'g');
    await until(() => kinds()[0] === 'graph', 5000, 'a graph cell');
    // the page draws it (before: no output at all, while the calculator drew the cell's text as a function)
    await until(() => P.cells()[0].querySelector('.plot') && P.cells()[0].querySelector('.plot').strokes > 0, 10000, 'the plot in the page');
    // ... and so does the calculator, of the cell's own function (not the Y= functions, not an old picture)
    const c = (await calcCells())[0];
    assert.equal(c.kind, 'graph');
    assert.match(c.spec, /\nf 1 0 0 x\^2$/);
    assert.equal(await st('gcell_fn'), 'x^2');
    assert.equal(await st('gcell_empty'), '0');
    await calc('redraw');
    await calc('expect text "y1=x²"');
    assert.equal(P.ta(0).value, 'x^2');
  });

  test('a graph cell without a function: no plot on either side, a line saying what to do; then a function', async () => {
    P.key(P.cells()[0], 'b');
    await until(() => P.cells().length === 2, 5000, 'a new cell');
    P.key(P.cells()[1], 'g');
    await until(() => kinds()[1] === 'graph', 5000, 'a graph cell');
    assert.equal(await st('gcell_empty'), '1');
    assert.equal(await st('gcell_h'), '20');
    await calc('redraw');
    await calc('expect text "graph: enter a function"');
    assert.ok(P.cells()[1].querySelector('.graphempty'));
    assert.equal(P.cells()[1].querySelector('.plot'), null);
    // the calculator's graph screens and back: the page's empty cell stays (only a cell the graph flow made is removed)
    await calc('key F1');
    await calc('key HOME');
    assert.equal((await calcCells()).length, 2);
    // a function typed in the page: the plot on both
    P.click(1);
    P.key(P.cells()[1], 'Enter');
    P.type(1, 'sin(x)\ncos(x)');
    P.key(P.ta(1), 'Escape');
    await until(async () => /\nf 1 0 0 sin\(x\)\nf 1 1 0 cos\(x\)$/.test((await calcCells())[1].spec || ''), 5000, 'the spec on the calculator');
    assert.equal(await st('gcell_empty'), '0');
    assert.equal(await st('gcell_caption'), '#0050D8y1=sin(x) #D81C18y2=cos(x)');
    await until(() => P.cells()[1].querySelector('.plot') && P.cells()[1].querySelector('.plot').strokes >= 2, 10000, 'two curves in the page');
    await until(() => plotOf(1).data.series.every((s) => s.ys.length === 400), 10000, 'sampled');
    assert.equal(plotOf(1).data.series.map((s) => s.color).join(), '0,1');
  });

  test('the calculator pans and zooms the cell in place: the page follows in the same window', async () => {
    for (let i = 0; i < 6 && (await st('home_sel')) !== '2'; i++) await calc('key UP');
    assert.equal(await st('home_sel'), '2');
    await calc('key RIGHT');
    assert.equal(await st('gcell_window'), '-7.5 12.5 -10 10');
    await until(() => plotOf(1) && plotOf(1).view.xmin === -7.5 && plotOf(1).view.xmax === 12.5, 5000, () => 'the view: ' + JSON.stringify(plotOf(1).view));
    await calc('key ADD');
    await until(() => plotOf(1).view.xmin === -2.5 && plotOf(1).view.ymax === 5, 5000, () => 'zoomed: ' + JSON.stringify(plotOf(1).view));
    assert.equal(P.cells()[1].querySelector('.range').querySelectorAll('input').map((e) => e.value).join(), '-2.5,7.5,-5,5');
    await calc('key CLEAR');
  });

  test('the page pans its plot: the calculator\'s cell gets the window once the gesture is over', async () => {
    const p = plotOf(1);
    const before = port.sent.split('nb spec ').length;
    for (let i = 0; i < 5; i++) { p.pan(-0.1, 0); await sleep(20); } // a drag: many moves
    await until(async () => (await st('gcell_window')) === '2.5 12.5 -5 5', 5000, async () => 'the window: ' + (await st('gcell_window')));
    await sleep(800);
    assert.equal(port.sent.split('nb spec ').length - before, 1, 'one command for the gesture');
    assert.equal(plotOf(1), p, 'the same plot (not drawn anew)');
    // the range typed in the page
    const ymax = P.cells()[1].querySelector('.range').querySelectorAll('input')[3];
    ymax.value = '20';
    ymax.dispatchEvent(new D.Ev('change'));
    await until(async () => (await st('gcell_window')) === '2.5 12.5 -5 20', 5000, 'y max on the calculator');
  });

  test('the graph app on the cell (ENTER): locked in the page, its edits back in the page after HOME', async () => {
    for (let i = 0; i < 6 && (await st('home_sel')) !== '2'; i++) await calc('key UP');
    await calc('key ENTER');
    assert.equal(await st('screen'), 'graph');
    await until(() => /locked/.test(P.cells()[1].className), 5000, 'locked in the page');
    assert.equal(P.ta(1).readOnly, true);
    await calc('key F1'); // Y=: a third function
    await calc('key DOWN');
    await calc('key DOWN');
    await calc('type "x/2"');
    await calc('key HOME');
    await until(() => !/locked/.test(P.cells()[1].className), 5000, 'unlocked');
    await until(() => P.ta(1).value === 'sin(x)\ncos(x)\nx/2', 5000, () => 'the functions: ' + JSON.stringify(P.ta(1).value));
    await until(() => P.cells()[1].querySelector('.plot').strokes >= 3, 10000, 'three curves');
  });

  test('a cell the page edits: the calculator does not open it', async () => {
    P.click(1);
    P.key(P.cells()[1], 'Enter');
    const id = S().nb.cells[1].id;
    await until(async () => (await st('nb_page_lock')) === id, 5000, 'the page lock');
    for (let i = 0; i < 6 && (await st('home_sel')) !== '2'; i++) await calc('key UP');
    await calc('key ENTER');
    assert.equal(await st('screen'), 'message');
    await calc('expect text "This cell is being edited"');
    await calc('key CLEAR');
    P.key(P.ta(1), 'Escape');
    await until(async () => (await st('nb_page_lock')) === '', 5000, 'unlocked');
    await deselect(); // (CLEAR on an empty entry line with nothing selected would start a new notebook)
  });

  test('a 3D graph made in the page: a real 3D cell on the calculator, drawn here from its samples', async () => {
    P.click(1);
    P.key(P.cells()[1], 'b');
    await until(() => P.cells().length === 3, 5000, () => 'a new cell: ' + JSON.stringify(port.sent.slice(-600)) + ' mode=' + S().mode + ' sel=' + S().sel);
    P.key(P.cells()[2], 'G');
    await until(() => kinds()[2] === 'graph3d', 5000, 'a 3D graph cell');
    assert.match(specs()[2], /^3d /);
    assert.doesNotMatch(specs()[2], /\nf /); // (no function yet: no plot on either side)
    assert.ok(P.cells()[2].querySelector('.graphempty'));
    P.click(2);
    P.key(P.cells()[2], 'Enter');
    P.type(2, 'sin(x)*cos(y)');
    P.key(P.ta(2), 'Escape');
    await until(async () => (await st('gcell_fn')) === 'sin(x)*cos(y)', 5000, 'the function on the calculator');
    assert.equal(await st('gcell_view'), '-60 25 1');
    await calc('redraw');
    await calc('expect text "z1=sin(x)·cos(y)"');
    const cv = await until(() => P.cells()[2].querySelector('.plot3d'), 5000, 'a 3D canvas');
    const sf = await until(() => view(2).kept.get('surface') && view(2).kept.get('surface').p, 5000, 'the surface');
    await until(() => sf.quads > 1000, 10000, () => 'drawn: ' + sf.quads + ' cells');
    assert.ok(cv.fills > 1000);
    assert.match(port.sent, /nb sample3d [0-9a-f]+ 40 40 1\n/);
    // turning and zooming are the page's: the calculator's cell keeps its view, nothing is sent
    const sent = port.sent.length;
    sf.turn(30, 10);
    sf.zoom(1.5);
    assert.deepEqual([sf.cam.azim, sf.cam.elev, sf.cam.zoom], [-30, 35, 1.5]);
    await sleep(300);
    assert.equal(await st('gcell_view'), '-60 25 1');
    assert.doesNotMatch(port.sent.slice(sent), /nb (spec|sample3d)/);
    sf.reset();
    assert.equal(sf.cam.azim, -60);
    // the range: x from -2 to 2 on the calculator
    const xmin = P.cells()[2].querySelector('.range').querySelectorAll('input')[0];
    xmin.value = '-2';
    xmin.dispatchEvent(new D.Ev('change'));
    await until(async () => /\nw -2 5 -5 5 -5 5\n/.test((await calcCells())[2].spec), 5000, 'the 3D window');
    // ENTER on the calculator: the 3D graph app
    await calc('key UP');
    await calc('key ENTER');
    assert.equal(await st('screen'), 'graph3d');
    await calc('key HOME');
  });

  test('a 3D graph made on the calculator: drawn in the page the same way', async () => {
    await deselect();
    await calc('key MENU'); // Insert 3D graph: a cell of the 3D graph's surfaces (the last one opened)
    await calc('key UP');
    await calc('key ENTER');
    assert.equal(await st('screen'), 'home');
    await until(() => P.cells().length === 4 && kinds()[3] === 'graph3d', 5000, 'the 3D cell in the page');
    const sf = await until(() => view(3).kept.get('surface') && view(3).kept.get('surface').p, 5000, 'the surface');
    await until(() => sf.quads > 1000, 10000, 'drawn');
    assert.equal(P.cells()[3].querySelector('.picture'), null); // (not the static picture any more)
    assert.equal(P.ta(3).value, 'sin(x)*cos(y)');
  });

  test('cells made while the page did not follow: all there once it does again', async () => {
    const c = client(), before = c.resyncs;
    await calc('nb sync off');
    await calc('key F1');
    await calc('key CLEAR');
    await calc('type "x^3"');
    await calc('key F5');
    await deselect();
    await calc('type "a:=2\\n"');
    await calc('key F1');
    await calc('key CLEAR');
    await calc('type "a*x"');
    await calc('key F5');
    const cells = await calcCells();
    assert.equal(cells.length, 7);
    await until(() => c.resyncs > before && P.cells().length === 7, 10000, 'read again');
    assert.equal(kinds().join(), 'graph,graph,graph3d,graph3d,graph,math,graph');
    for (const i of [4, 6]) await until(() => P.cells()[i].querySelector('.plot') && P.cells()[i].querySelector('.plot').strokes > 0, 10000, 'plot ' + i);
    assert.match(P.ta(6).value, /\na\*x$/);
  });
});
