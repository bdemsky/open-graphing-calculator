// Graph cell specs as the page reads and writes them (sync.js parseSpec / buildSpec / outputsOf) and
// the 3D renderer's pure parts (plot3d.js): no simulator needed.
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const S = require('../src/sync.js');
const R = require('../src/plot3d.js');

test('a 2D spec read: window, functions with colour and style, on/off', () => {
  const sp = S.parseSpec('w -5 5 -2 8 1 2\no 0 1 140\nf 1 0 0 x^2\nf 0 3 1 sin(x) + 1');
  assert.equal(sp.d3, false);
  assert.deepEqual(sp.w, [-5, 5, -2, 8]);
  assert.deepEqual(sp.scl, [1, 2]);
  assert.deepEqual(sp.o, [0, 1, 140]);
  assert.deepEqual(sp.fns.map((f) => [f.k, f.on, f.color, f.style, f.expr]), [[1, true, 0, 0, 'x^2'], [2, false, 3, 1, 'sin(x) + 1']]);
});

test('a 2D spec written: functions keep their colour, style and on/off; new ones the next colour', () => {
  const old = 'w -5 5 -2 8 1 2\no 0 1 140\nf 1 0 1 x^2\nf 0 3 2 sin(x)';
  assert.equal(S.buildSpec(old, ['x^3', '', 'cos(x)', 'tan(x)'], [-1, 1, -3, 3], false),
    'w -1 1 -3 3 1 2\no 0 1 140\nf 1 0 1 x^3\nf 0 3 2 cos(x)\nf 1 2 0 tan(x)');
  assert.equal(S.buildSpec(null, [], [-10, 10, -10, 10], false), 'w -10 10 -10 10 1 1\no 0 1 120');
  assert.equal(S.buildSpec(null, ['a', 'b', 'c', 'd', 'e', 'f', 'g'], [0, 1, 0, 1], false).split('\nf ').length, 7); // (six at most)
});

test('a 3D spec: read, written, made from a 2D one', () => {
  const sp3 = '3d -45 30 1.5 1 1 1 0 24 200\nw -2 2 -3 3 -1 1\np 0 6 0 6 0 6\no 0 0 100\nf 1 4 0 x*y\nf 1 1 1 cos(u)\tsin(u)\tv';
  const sp = S.parseSpec(sp3);
  assert.equal(sp.d3, true);
  assert.deepEqual(sp.w, [-2, 2, -3, 3, -1, 1]);
  assert.deepEqual(sp.view.slice(0, 5), [-45, 30, 1.5, 1, 1]);
  assert.deepEqual(sp.fns.map((f) => [f.kind, f.color, f.parts.length]), [[0, 4, 1], [1, 1, 3]]);
  assert.equal(S.buildSpec(sp3, ['sin(x)'], [-5, 5, -5, 5, -2, 2], true),
    '3d -45 30 1.5 1 1 1 0 24 200\nw -5 5 -5 5 -2 2\np 0 6 0 6 0 6\no 0 0 100\nf 1 4 0 sin(x)');
  assert.match(S.buildSpec('w -1 1 -1 1 1 1\no 0 1 90\nf 1 2 0 x', ['x*y'], [-5, 5, -5, 5, -5, 5], true),
    /^3d -60 25 1 1 0 1 1 24 200\nw -5 5 -5 5 -5 5\np 0 6\.28318530717959 .*\no 0 0 90\nf 1 0 0 x\*y$/);
  // the calculator's contour line is kept
  assert.equal(S.buildSpec('3d -45 30 1 1 1 1 0 24 200\nw -2 2 -3 3 -1 1\np 0 6 0 6 0 6\no 0 0 100\nc 1 1 7\nf 1 4 0 x*y', ['x-y'], [-2, 2, -3, 3, -1, 1], true),
    '3d -45 30 1 1 1 1 0 24 200\nw -2 2 -3 3 -1 1\np 0 6 0 6 0 6\no 0 0 100\nc 1 1 7\nf 1 4 0 x-y');
});

test('outputs of graph cells: every cell shows something', () => {
  const kinds = (c) => S.outputsOf(Object.assign({ id: 'a', kind: 'graph', src: '' }, c)).map((o) => o.metadata.calculator);
  assert.equal(kinds({ spec: 'w -1 1 -1 1\no 0 1 120' })[0].empty, true); // no function: no plot, a hint
  assert.equal(kinds({ spec: 'w -1 1 -1 1\no 0 1 120\nf 0 0 0 x' })[0].empty, true); // switched off
  assert.deepEqual(kinds({ spec: 'w -1 1 -2 2\no 0 1 120\nf 1 3 1 x' })[0].fns.map((f) => [f.color, f.style]), [[3, 1]]);
  assert.equal(kinds({ src: 'sin(x), -1, 1' })[0].picture, true); // (no spec: the calculator's picture)
  assert.equal(kinds({ spec: '3d -60 25 1 1 0 1 1 24 200\nw -5 5 -5 5 -5 5\no 0 0 120\nf 1 0 0 x*y' })[0].surface, true);
  assert.equal(kinds({ spec: '3d -60 25 1 1 0 1 1 24 200\nw -5 5 -5 5 -5 5\no 0 0 120\nf 1 0 2 t\tt\tt' })[0].picture, true);
  assert.equal(S.kindOf({ kind: 'graph', spec: '3d 1 2 3 4 5 6 7 8 9' }), 'graph3d');
  assert.equal(S.kindOf({ kind: 'graph', spec: 'w 1 2 3 4' }), 'graph');
});

test('animated cells: t in a function that is on (the calculator\'s rule), the "a" line read and written', () => {
  assert.equal(S.usesT('sin(x-t)'), true);
  assert.equal(S.usesT('2t+x'), true);
  assert.equal(S.usesT('t^2'), true);
  assert.equal(S.usesT('tan(x)'), false); // (a name, not t)
  assert.equal(S.usesT('xt'), false);
  assert.equal(S.usesT('t(x)'), false); // (a call)
  assert.equal(S.usesT('a_t+theta'), false);
  // 2D: animated when a function that is on uses t; the settings from the "a" line, else the defaults
  assert.equal(S.parseSpec('w -1 1 -1 1 1 1\no 0 1 120\nf 1 0 0 x').anim, null);
  assert.equal(S.parseSpec('w -1 1 -1 1 1 1\no 0 1 120\nf 0 0 0 x+t').anim, null); // (switched off)
  assert.deepEqual(S.parseSpec('w -1 1 -1 1 1 1\no 0 1 120\nf 1 0 0 sin(x-t)').anim, S.ANIM_DEFAULT);
  assert.deepEqual(S.parseSpec('w -1 1 -1 1 1 1\no 0 1 120\nf 1 0 0 sin(x-t)\na -1 2 6 1').anim, { tmin: -1, tmax: 2, secs: 6, mode: 1 });
  // 3D: not a curve's own t; a surface's t is
  assert.equal(S.parseSpec('3d -60 25 1 1 0 1 1 24 200\nw -5 5 -5 5 -5 5\no 0 0 120\nf 1 0 2 t\tt\tt').anim, null);
  assert.ok(S.parseSpec('3d -60 25 1 1 0 1 1 24 200\nw -5 5 -5 5 -5 5\no 0 0 120\nf 1 0 0 sin(x-t)').anim);
  // written: the "a" line when a function uses t (the old settings kept; given ones win), none otherwise
  assert.equal(S.buildSpec(null, ['sin(x-t)'], [-10, 10, -10, 10], false),
    'w -10 10 -10 10 1 1\no 0 1 120\nf 1 0 0 sin(x-t)\na 0 6.28318530717959 4 0');
  const old = 'w -10 10 -10 10 1 1\no 0 1 120\nf 1 0 0 sin(x-t)\na 0 3 2 2';
  assert.equal(S.buildSpec(old, ['cos(x+t)'], [-10, 10, -10, 10], false), 'w -10 10 -10 10 1 1\no 0 1 120\nf 1 0 0 cos(x+t)\na 0 3 2 2');
  assert.equal(S.buildSpec(old, ['cos(x)'], [-10, 10, -10, 10], false), 'w -10 10 -10 10 1 1\no 0 1 120\nf 1 0 0 cos(x)');
  assert.match(S.buildSpec(old, ['cos(x+t)'], [-10, 10, -10, 10], false, { tmin: 1, tmax: 5, secs: 8, mode: 1 }), /\na 1 5 8 1$/);
  assert.match(S.buildSpec(null, ['sin(x-t)*y'], [-5, 5, -5, 5, -5, 5], true), /\nf 1 0 0 sin\(x-t\)\*y\na 0 6\.28318530717959 4 0$/);
  // outputs: the settings go with the plot
  const o = S.outputsOf({ id: 'a', kind: 'graph', src: '', spec: old })[0].metadata.calculator;
  assert.deepEqual(o.anim, { tmin: 0, tmax: 3, secs: 2, mode: 2 });
});

test('the 3D view: the calculator\'s camera, projection, mesh, colour map', () => {
  const m = R.viewMatrix(0, 90); // from straight above: x right, y up
  const p = R.project3d(m, [1, 0, 0], 100, 100, 50);
  assert.ok(p[0] > 100 && Math.abs(p[1] - 100) < 1e-9);
  const q = R.project3d(m, [0, 1, 0], 100, 100, 50);
  assert.ok(q[1] < 100 && Math.abs(q[0] - 100) < 1e-9);
  // a 3 x 3 grid: 4 cells; one undefined corner drops the cells around it; z beyond the box is cut
  const zs = [0, 0, 0, 0, NaN, 0, 0, 0, 0];
  assert.equal(R.mesh3d({ nx: 3, ny: 3, zs }, [-1, 1, -1, 1, -1, 1]).length, 0);
  const cells = R.mesh3d({ nx: 3, ny: 3, zs: [0, 0, 0, 0, 5, 0, 0, 0, 9] }, [-1, 1, -1, 1, -1, 1]);
  assert.equal(cells.length, 4);
  assert.ok(cells.every((c) => c.p.every((v) => v[2] <= 1)));
  assert.equal(R.mesh3d({ nx: 2, ny: 2, zs: [5, 6, 7, 8] }, [-1, 1, -1, 1, -1, 1]).length, 0); // wholly above
  // the calculator's edge points stand in for undefined nodes: the cells reach the domain's edge
  const edged = R.mesh3d({ nx: 3, ny: 3, zs, edges: { 4: [0.25, 0, 0.5] } }, [-1, 1, -1, 1, -1, 1]);
  assert.equal(edged.length, 4);
  assert.deepEqual(edged[0].p[2], [0.25, 0, 0.5]);
  assert.deepEqual(R.heat(-1), [30, 70, 230]);
  assert.deepEqual(R.heat(1), [225, 45, 30]);
});
