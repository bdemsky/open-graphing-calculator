// Shared by the page's tests (page.test.js, refs.test.js): the built page's script in a fake DOM
// (fakedom.js), a fake Web Serial port whose device is the simulator, and a wait helper.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { spawn } = require('node:child_process');
const D = require('./fakedom.js');

const FW = path.resolve(__dirname, '..', '..', '..');
const HTML = process.env.NB_HTML || path.join(FW, 'build', 'gen', 'calculator_notebook.html');
const SIM = process.env.CALCSIM || path.join(FW, 'build', 'sim', 'calcsim');
const HAS_PY = process.env.NB_PY_MODULE !== '0';

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(f, ms = 15000, what = 'condition') {
  const t0 = Date.now();
  for (;;) {
    const v = await f();
    if (v) return v;
    if (Date.now() - t0 > ms) throw new Error('timed out waiting for ' + (typeof what === 'function' ? await what() : what));
    await sleep(25);
  }
}

/* The page's script in a fresh context; serial: a fake navigator.serial or none. */
function loadPage(serial) {
  const { body, script } = D.parsePage(fs.readFileSync(HTML, 'utf8'));
  const doc = D.makeDocument(body);
  const win = D.makeWindow(doc, { navigator: serial ? { serial } : {} });
  const ctx = vm.createContext(win);
  vm.runInContext(script, ctx, { filename: 'page.js' });
  const $ = (s) => doc.querySelector(s);
  const key = (target, k, mods = {}) => target.dispatchEvent(new D.Ev('keydown', Object.assign({ key: k }, mods)));
  const cells = () => $('#cells').children;
  const ta = (i) => cells()[i].querySelector('.src');
  const outText = (i) => cells()[i].querySelector('.outputs').textContent;
  const prompt = (i) => cells()[i].querySelector('.prompt').textContent;
  const type = (i, text) => { const t = ta(i); t.value = text; t.dispatchEvent(new D.Ev('input')); };
  const click = (i) => cells()[i].dispatchEvent(new D.Ev('mousedown'));
  return { doc, win, $, key, cells, ta, outText, prompt, type, click };
}

/* A Web Serial port whose device is a simulator started at each open(), its flash in a file
 * (the notebook store survives a "reboot"). */
class SimPort {
  constructor(flash) { this.listeners = {}; this.opens = 0; this.child = null; this.sent = ''; this.flash = flash; }
  getInfo() { return { usbVendorId: 0x1209, usbProductId: 0x0001 }; }
  async open() {
    if (this.child) throw new Error('already open');
    this.opens++;
    const child = spawn(SIM, ['--realtime', '--flash', this.flash], { stdio: ['pipe', 'pipe', 'ignore'] });
    this.child = child;
    let ctl;
    this.readable = new ReadableStream({
      start(c) { ctl = c; child.stdout.on('data', (d) => { try { c.enqueue(new Uint8Array(d)); } catch (e) { /* closed */ } }); },
      cancel() { child.kill('SIGKILL'); },
    });
    this.ctl = ctl;
    child.on('exit', () => { try { ctl.close(); } catch (e) { /* errored */ } });
    this.writable = new WritableStream({ write: (chunk) => {
      this.sent += Buffer.from(chunk).toString('latin1');
      return new Promise((res) => child.stdin.write(Buffer.from(chunk), () => res()));
    } });
  }
  async setSignals() {}
  async close() { this.kill(); }
  kill() { if (this.child) { this.child.kill('SIGKILL'); this.child = null; } }
  drop() { // the device leaves the bus
    const c = this.ctl;
    this.kill();
    try { c.error(new Error('The device has been lost.')); } catch (e) { /* closed */ }
  }
}

/* navigator.serial granting `port` */
function fakeSerial(port) {
  let granted = false;
  return {
    requestPort: async () => { granted = true; return port; },
    getPorts: async () => (granted ? [port] : []),
    addEventListener() {},
  };
}

module.exports = { D, FW, HTML, SIM, HAS_PY, sleep, until, loadPage, SimPort, fakeSerial };
