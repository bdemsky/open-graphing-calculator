// Earlier results in the page and on the calculator (docs/NOTEBOOK.md "Ans and Out[n]"): one
// execution counter (In [n] here, [n] on the calculator's home screen), Ans and Out[n] in Math
// and Python cells run from the page, a missing n, the hover buttons that insert a cell's input,
// result or Out[n] (at the cursor of the cell being edited, else in a new cell), and Alt+Up /
// Alt+Down recalling earlier inputs. The page runs in a fake DOM, the calculator is the simulator.
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { HTML, SIM, HAS_PY, sleep, until, loadPage, SimPort, fakeSerial } = require('./pagelib.js');

const skip = (!fs.existsSync(HTML) && 'no built page (make notebook)') || (!fs.existsSync(SIM) && 'no simulator') ||
  (!HAS_PY && 'no Python module');

test.describe('Ans and Out[n]: the page and the calculator', { skip }, () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'calcnb-refs-'));
  const port = new SimPort(path.join(dir, 'flash.bin'));
  let P;
  const calc = async (cmd) => {
    const r = await P.win.CalcNB.page.link().command(cmd);
    if (!r.ok) throw new Error(cmd + ': ' + r.error);
    return r.lines;
  };
  const st = async (k) => { const l = (await calc('state ' + k)).find((x) => x.startsWith(k + '=')); return l ? l.slice(k.length + 1) : null; };
  const nbField = async (n, f) => (await calc('nb cell ' + n)).filter((l) => l.startsWith(f + '=')).map((l) => l.slice(f.length + 1)).join('\n');
  const kinds = () => P.cells().map((c) => (/kind-(\w+)/.exec(c.className) || [])[1]);
  const btn = (i, where, cls) => {
    const bars = P.cells()[i].querySelectorAll('.refbar').filter((b) => (where === 'input' ? b.parentNode.className === 'inwrap' : b.parentNode.className !== 'inwrap'));
    return bars.length ? bars[0].querySelector('.' + cls) : null;
  };
  /* a new cell of `kind` after the last one, being edited */
  const newCell = async (kind) => {
    const n = P.cells().length;
    P.click(n - 1);
    P.key(P.cells()[n - 1], 'b');
    await until(() => P.cells().length === n + 1, 5000, 'a new cell');
    if (kind === 'math') {
      P.key(P.cells()[n], 't');
      await until(() => /kind-math/.test(P.cells()[n].className), 5000, 'a math cell');
    }
    P.click(n);
    P.key(P.cells()[n], 'Enter');
    return n;
  };
  const runCell = async (i, text) => {
    P.type(i, text);
    P.key(P.ta(i), 'Enter', { ctrlKey: true });
    await until(() => /In \[\d+\]/.test(P.prompt(i)) && P.outText(i), 10000, () => 'cell ' + i + ' run: ' + P.prompt(i));
  };

  test.before(async () => {
    P = loadPage(fakeSerial(port));
    P.$('#btn-connect').click();
    await until(() => P.$('#conn').dataset.state === 'on', 15000, 'connected');
    await until(() => P.win.CalcNB.page.client() && P.win.CalcNB.page.client().doc, 5000, 'the notebook');
  });
  test.after(() => { P && P.win.stopTimers(); port.kill(); fs.rmSync(dir, { recursive: true, force: true }); });

  test('one execution counter: In [n] in the page is the calculator\'s [n]', async () => {
    await calc('type "1/3\\n"');
    await calc('type "x^2+1\\n"');
    await until(() => P.cells().length === 2 && /x/.test(P.outText(1)), 5000, 'the calculations');
    assert.equal(P.prompt(0), 'In [1]:');
    assert.equal(P.prompt(1), 'In [2]:');
    assert.match(P.outText(1), /Out\[2\]:/);
    assert.equal(await nbField(2, 'count'), '2');
    await calc('redraw');
    await calc('expect text "[2]"'); // the home screen's [n] by each result
  });

  test('Out[n] and Ans in a Math cell run from the page; a missing n', async () => {
    const i = await newCell('math');
    await runCell(i, 'Out[1]*3');
    assert.equal(P.prompt(i), 'In [3]:');
    assert.equal(await nbField(3, 'val'), '1');
    const j = await newCell('math');
    await runCell(j, 'Ans+out[2]');
    assert.equal(await nbField(4, 'val'), 'x^2+2');
    const k = await newCell('math');
    await runCell(k, 'Out[42]+1');
    assert.match(P.outText(k), /Out\[42\]: no such result/);
    assert.equal(await nbField(5, 'ename'), 'CASError');
    // the calculator's home screen: the same numbers, Out[4] there too
    await calc('type "Out[4]-1\\n"');
    assert.equal(await st('last_output'), 'x^2+1');
    await until(() => P.cells().length === 6 && P.prompt(5) === 'In [6]:', 5000, 'the calculation in the page');
  });

  test('Ans and Out in a Python cell run from the page', async () => {
    const i = await newCell('python');
    await runCell(i, '[Ans, Out[3], type(Out[2]).__name__, 42 in Out]');
    assert.match(P.outText(i), /\[x\^2\+1, 1, 'Expr', False\]/);
    assert.equal(P.prompt(i), 'In [7]:');
    const j = await newCell('python');
    await runCell(j, 'Out[7][2] + "!"');
    assert.match(P.outText(j), /'Expr!'/);
    const k = await newCell('python');
    await runCell(k, 'Out[99]');
    assert.match(P.outText(k), /KeyError: Out\[99\]: no such result/);
  });

  test('hover buttons: insert an input, a result or Out[n] at the cursor of the cell being edited', async () => {
    const i = await newCell('math');
    assert.equal(P.doc.activeElement, P.ta(i));
    P.type(i, '2*');
    P.ta(i).setSelectionRange(2, 2);
    assert.ok(btn(i - 1, 'input', 'ref-text'), 'an input bar');
    assert.equal(btn(i - 1, 'input', 'ref-out'), null, 'an error has no Out[n]');
    btn(1, 'result', 'ref-out').click(); // Out[2]
    assert.equal(P.ta(i).value, '2*Out[2]');
    assert.equal(btn(1, 'result', 'ref-out').textContent, 'insert Out[2]');
    P.ta(i).setSelectionRange(0, 0);
    btn(1, 'input', 'ref-text').click(); // x^2+1, before the rest
    assert.equal(P.ta(i).value, 'x^2+12*Out[2]');
    P.type(i, '');
    btn(0, 'result', 'ref-text').click(); // the result 1/3
    assert.equal(P.ta(i).value, '1/3');
    assert.equal(P.doc.activeElement, P.ta(i)); // still editing here
    await until(async () => (await nbField(i + 1, 'src')) === '1/3', 5000, 'the text on the calculator');
    P.key(P.ta(i), 'Enter', { ctrlKey: true });
    await until(() => /In \[\d+\]/.test(P.prompt(i)) && /1\/3|frac/.test(P.outText(i) + P.cells()[i].querySelector('.outputs').innerHTML), 10000, 'run');
  });

  test('hover buttons with no cell being edited: a new cell at the end with the text', async () => {
    const n = P.cells().length;
    P.click(0);
    assert.notEqual(P.doc.activeElement, P.ta(0));
    btn(1, 'result', 'ref-out').click();
    await until(() => P.cells().length === n + 1, 5000, 'a new cell');
    await until(() => P.ta(n).value === 'Out[2]', 5000, () => 'its text: ' + P.ta(n).value);
    assert.equal(kinds()[n], 'math');
    // a Python cell's result: a new Python cell
    P.key(P.ta(n), 'Escape');
    btn(6, 'result', 'ref-text').click();
    await until(() => P.cells().length === n + 2 && P.ta(n + 1).value === "[x^2+1, 1, 'Expr', False]", 5000, 'a Python cell');
    assert.equal(kinds()[n + 1], 'python');
    P.key(P.ta(n + 1), 'Escape');
    await sleep(100);
  });

  test('Alt+Up / Alt+Down: the earlier inputs of that kind, then the text as it was', async () => {
    const i = await newCell('math');
    P.type(i, 'draft');
    P.key(P.ta(i), 'ArrowUp', { altKey: true });
    assert.equal(P.ta(i).value, 'Out[2]'); // the nearest math input above
    P.key(P.ta(i), 'ArrowUp', { altKey: true });
    assert.equal(P.ta(i).value, '1/3');
    P.key(P.ta(i), 'ArrowUp', { altKey: true });
    assert.equal(P.ta(i).value, 'Out[4]-1');
    P.key(P.ta(i), 'ArrowDown', { altKey: true });
    P.key(P.ta(i), 'ArrowDown', { altKey: true });
    assert.equal(P.ta(i).value, 'Out[2]');
    P.key(P.ta(i), 'ArrowDown', { altKey: true });
    assert.equal(P.ta(i).value, 'draft');
    P.key(P.ta(i), 'ArrowDown', { altKey: true }); // (nothing newer)
    assert.equal(P.ta(i).value, 'draft');
    P.key(P.ta(i), 'ArrowUp', { altKey: true });
    await until(async () => (await nbField(i + 1, 'src')) === 'Out[2]', 5000, 'sent to the calculator');
    P.key(P.ta(i), 'Escape');
  });

  test('disconnect', async () => {
    P.$('#btn-connect').click();
    await until(() => P.$('#conn').dataset.state === 'off', 5000, 'disconnected');
  });
});
