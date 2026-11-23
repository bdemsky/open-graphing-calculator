// Animated graph cells in both views (docs/NOTEBOOK.md "Animated graph cells"): a cell whose functions use
// t plays in the page at once (autoplay), from frames the calculator computes in one round trip
// (nb frames / nb frames3d); the t slider scrubs, the speed is the page's; the animation settings
// (t's range, seconds, mode) go both ways in the spec, the playing does not; a t-function typed in a
// Graph or 3D graph cell of the page makes it animated on both sides. The built page in a fake DOM,
// the simulator as the device.
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { D, HTML, SIM, HAS_PY, sleep, until, loadPage, SimPort, fakeSerial } = require('./pagelib.js');

const skip = (!fs.existsSync(HTML) && 'no built page (make notebook)') || (!fs.existsSync(SIM) && 'no simulator') ||
  (!HAS_PY && 'no Python module');

test.describe('animated graph cells: the calculator and the page', { skip }, () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'calcnb-a-'));
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
  const kept = (i, k) => { const v = view(i); return v && v.kept.get(k); };
  const kinds = () => P.cells().map((c) => (/kind-(\w+)/.exec(c.className) || [])[1]);
  const count = (re) => (port.sent.match(re) || []).length;

  test.before(async () => {
    P = loadPage(fakeSerial(port));
    P.$('#btn-connect').click();
    await until(() => P.$('#conn').dataset.state === 'on', 15000, 'connected');
    await until(() => client() && client().doc, 5000, 'the notebook');
  });
  test.after(() => {
    if (P) { P.win.CalcNB.page.state.wanted = false; P.win.stopTimers(); }
    port.kill();
    fs.rmSync(dir, { recursive: true, force: true });
  });

  test('a cell of t made on the calculator plays in the page at once, from one batch of frames', async () => {
    await calc('key F1'); // graph: the 2D editor
    assert.equal(await st('screen'), 'yeq');
    await calc('type "sin(x-t)"');
    await calc('key F5');
    assert.equal(await st('gcell_anim'), '1');
    await until(() => P.cells().length === 1 && P.cells()[0].querySelector('.animbar'), 10000, 'the animated cell in the page');
    const k = await until(() => kept(0, 'plot') && kept(0, 'plot').pl, 5000, 'the player');
    await until(() => k.playing && k.phase > 0, 10000, 'playing (autoplay)');
    assert.equal(count(/nb frames [0-9a-f]+ 48 200 -10 10\n/g), 1, 'one round trip for the frames');
    const p0 = k.phase;
    await sleep(300);
    assert.notEqual(k.phase, p0, 'the phase moves');
    const p = kept(0, 'plot').p;
    assert.equal(p.data.series.length, 1);
    assert.equal(p.data.series[0].ys.length, 200);
    // the calculator's cell does not play meanwhile: still at the start of t
    assert.equal(await st('gcell_t'), '0');
    assert.equal(await st('anim_playing'), (await st('anim_module')) === 'ok' ? '0' : null);
  });

  test('the t slider scrubs (pausing), the speed is the page\'s', async () => {
    const bar = P.cells()[0].querySelector('.animbar');
    const k = kept(0, 'plot').pl, p = kept(0, 'plot').p;
    const scrub = bar.querySelector('.scrub');
    scrub.value = '500';
    scrub.dispatchEvent(new D.Ev('input'));
    assert.equal(k.playing, false);
    assert.equal(k.phase, 0.5);
    assert.equal(bar.querySelector('.tval').textContent, 't = 3.142');
    assert.match(bar.querySelector('.play').textContent, /play/);
    // the curve is sin(x - π) there (between two frames: interpolated)
    const ys = p.data.series[0].ys, x0 = p.data.xmin, dx = (p.data.xmax - p.data.xmin) / (ys.length - 1);
    for (let i = 0; i < ys.length; i += 17) assert.ok(Math.abs(ys[i] - Math.sin(x0 + i * dx - Math.PI)) < 0.01, 'y at ' + i);
    const speed = bar.querySelector('select');
    speed.value = '2';
    speed.dispatchEvent(new D.Ev('change'));
    assert.equal(k.speed, 2);
    bar.querySelector('.play').dispatchEvent(new D.Ev('click'));
    assert.equal(k.playing, true);
    await sleep(200);
    assert.ok(k.phase > 0.5);
  });

  test('the settings both ways: the page\'s t range to the calculator, the calculator\'s back', async () => {
    const inputs = () => P.cells()[0].querySelector('.range').querySelectorAll('input');
    const tmax = inputs().find((e) => e.attrs['aria-label'] === 'tmax');
    assert.ok(tmax, 'the t range in the range row');
    tmax.value = '3';
    tmax.dispatchEvent(new D.Ev('change'));
    await until(async () => (await st('gcell_anim_cfg')) === '0 3 4 0', 5000, async () => 'on the calculator: ' + await st('gcell_anim_cfg'));
    assert.match((await calcCells())[0].spec, /\na 0 3 4 0$/);
    // the new range: the frames sampled again, the player the same speed choice
    await until(() => kept(0, 'plot') && kept(0, 'plot').pl.anim.tmax === 3, 5000, 'the player\'s range');
    await until(() => count(/nb frames [0-9a-f]+ 48 200/g) === 2, 5000, 'frames for the new range');
    // the calculator's graph app on the cell: its animation of t has the cell's settings; changed there,
    // they come back with HOME
    for (let i = 0; i < 4 && (await st('home_sel')) !== '0'; i++) await calc('key UP');
    await calc('key ENTER');
    assert.equal(await st('screen'), 'graph');
    await calc('anim cfg t 1 2 5 1');
    await calc('key HOME');
    await until(async () => (await st('gcell_anim_cfg')) === '1 2 5 1', 5000, 'written back');
    await until(() => kept(0, 'plot') && kept(0, 'plot').pl.anim.tmin === 1 && kept(0, 'plot').pl.anim.mode === 1, 5000, 'the page follows');
    const r = inputs();
    assert.equal(r.find((e) => e.attrs['aria-label'] === 'seconds').value, '5');
    assert.equal(P.cells()[0].querySelector('.range').querySelector('select').value, '1');
    await calc('key CLEAR');
  });

  test('a t-function typed in a Graph cell of the page: animated on both sides', async () => {
    P.click(0);
    P.key(P.cells()[0], 'b');
    await until(() => P.cells().length === 2, 5000, 'a new cell');
    P.key(P.cells()[1], 'g');
    await until(() => kinds()[1] === 'graph', 5000, 'a graph cell');
    P.click(1);
    P.key(P.cells()[1], 'Enter');
    P.type(1, 'cos(x+t)');
    P.key(P.ta(1), 'Escape');
    await until(async () => /\nf 1 0 0 cos\(x\+t\)\na 0 6\.28318530717959 4 0$/.test((await calcCells())[1].spec || ''), 5000, 'the spec on the calculator');
    for (let i = 0; i < 4 && (await st('home_sel')) !== '2'; i++) await calc('key UP');
    assert.equal(await st('gcell_anim'), '1');
    await calc('redraw');
    await calc('expect text anim');
    const k = await until(() => kept(1, 'plot') && kept(1, 'plot').pl, 10000, 'the player');
    await until(() => k.playing, 10000, 'playing');
    await calc('key CLEAR');
  });

  test('a 3D graph of t typed in the page: animated on both sides, its frames per surface, turned here', async () => {
    P.click(1);
    P.key(P.cells()[1], 'b');
    await until(() => P.cells().length === 3, 5000, 'a new cell');
    P.key(P.cells()[2], 'G');
    await until(() => kinds()[2] === 'graph3d', 5000, 'a 3D graph cell');
    P.click(2);
    P.key(P.cells()[2], 'Enter');
    P.type(2, 'sin(x-t)*cos(y)');
    P.key(P.ta(2), 'Escape');
    await until(async () => /\na 0 6\.28318530717959 4 0$/.test((await calcCells())[2].spec || ''), 5000, 'animated on the calculator');
    const e = await until(() => kept(2, 'surface'), 10000, 'the surface view');
    await until(() => e.pl.playing && e.p.quads > 500, 10000, () => 'playing: ' + e.pl.playing + ' quads ' + e.p.quads);
    assert.equal(count(/nb frames3d [0-9a-f]+ 24 32 32 1\n/g), 1);
    // turning is the page's (the calculator keeps its view), while it plays
    e.p.turn(30, 0);
    assert.equal(e.p.cam.azim, -30);
    await sleep(150);
    assert.equal(e.p.cam.azim, -30, 'the frames keep the camera');
    assert.ok(e.pl.phase > 0);
  });
});
