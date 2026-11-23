#!/usr/bin/env node
/* Headless tests for the WebAssembly simulator (run by `make web-test`).
 *
 * 1. Runs key sequences and every tests/ui/*.txt console script in the WASM
 *    build (fresh instance each) and in the native simulator, saves the WASM
 *    screenshots as PNGs and compares them pixel by pixel with the native ones.
 * 2. Smoke-tests the built page: runs its inline scripts in a node vm with a
 *    minimal fake DOM, clicks hot-spots / sends keyboard events, runs animation
 *    frames, and checks the canvas and calculator state.
 *
 *   node run_web_test.js --module build/web/calc_module.js --sim build/sim/calcsim
 *                        --out web/test [--page web/dist/calculator.html]
 */
"use strict";
const fs = require("fs");
const os = require("os");
const path = require("path");
const zlib = require("zlib");
const vm = require("vm");
const { execFileSync } = require("child_process");

const args = {};
for (let i = 2; i < process.argv.length; i += 2) args[process.argv[i].replace(/^--/, "")] = process.argv[i + 1];
const FW = path.resolve(__dirname, "../..");
const MODULE = path.resolve(args.module || path.join(FW, "build/web/calc_module.js"));
const SIM = path.resolve(args.sim || path.join(FW, "build/sim/calcsim"));
const OUT = path.resolve(args.out || __dirname);
const PAGE = args.page ? path.resolve(args.page) : null;
const CalcSim = require(path.join(FW, "web/page/calc_api.js"));
const createCalcModule = require(MODULE);
const W = 320, H = 240;

/* ------------------------------------------------------------------ PNG */
const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();
function crc32(buf) {
  let c = 0xffffffff;
  for (const b of buf) c = CRC_TABLE[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type, "ascii"), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
function writePNG(file, rgba, w, h) {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 3 + 1)] = 0;
    for (let x = 0; x < w; x++)
      for (let c = 0; c < 3; c++) raw[y * (w * 3 + 1) + 1 + x * 3 + c] = rgba[(y * w + x) * 4 + c];
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ihdr),
    chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]));
}
function readPNG(file) { // 8-bit RGB/RGBA, non-interlaced -> {w, h, rgba}
  const b = fs.readFileSync(file);
  let p = 8, w, h, ct, idat = [];
  while (p < b.length) {
    const len = b.readUInt32BE(p), type = b.toString("ascii", p + 4, p + 8), d = b.subarray(p + 8, p + 8 + len);
    if (type === "IHDR") {
      w = d.readUInt32BE(0); h = d.readUInt32BE(4); ct = d[9];
      if (d[8] !== 8 || (ct !== 2 && ct !== 6) || d[12]) throw new Error(file + ": unsupported PNG");
    } else if (type === "IDAT") idat.push(d);
    p += 12 + len;
  }
  const bpp = ct === 6 ? 4 : 3, stride = w * bpp, raw = zlib.inflateSync(Buffer.concat(idat));
  const out = Buffer.alloc(stride * h), rgba = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)], src = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let i = 0; i < stride; i++) {
      const a = i >= bpp ? out[y * stride + i - bpp] : 0, up = y ? out[(y - 1) * stride + i] : 0;
      const c = i >= bpp && y ? out[(y - 1) * stride + i - bpp] : 0;
      let v = src[i];
      if (f === 1) v += a; else if (f === 2) v += up; else if (f === 3) v += (a + up) >> 1;
      else if (f === 4) { const pp = a + up - c, pa = Math.abs(pp - a), pb = Math.abs(pp - up), pc = Math.abs(pp - c);
        v += pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
      out[y * stride + i] = v & 255;
    }
  }
  for (let i = 0; i < w * h; i++) {
    for (let c = 0; c < 3; c++) rgba[i * 4 + c] = out[i * bpp + c];
    rgba[i * 4 + 3] = 255;
  }
  return { w, h, rgba };
}
function rgbaFrom565(px) {
  const out = new Uint8Array(W * H * 4);
  for (let i = 0; i < W * H; i++) {
    const p = px[i], r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    out[i * 4] = (r << 3) | (r >> 2); out[i * 4 + 1] = (g << 2) | (g >> 4); out[i * 4 + 2] = (b << 3) | (b >> 2);
    out[i * 4 + 3] = 255;
  }
  return out;
}

/* --------------------------------------------------------------- scripts */
// Custom key sequences (also run natively for comparison).
const SCENARIOS = {
  "seq_2plus3_enter": ["key 2 + 3 ENTER", "expect state last_output 5", "screenshot seq_2plus3_enter.png"],
  "seq_yeq_sin_x_graph": ["key F1 SIN X RPAREN F5 ENTER", "expect state screen graph", "screenshot seq_yeq_sin_x_graph.png"],
  "seq_typed_math": ['type "sqrt(2)*3^2\\n"', 'key SQRT 2 RPAREN ENTER', "key MENU",
    "screenshot seq_typed_math.png"],
  "seq_table": ["key F1 X POW 2 SHIFT F5", "key DOWN DOWN", "screenshot seq_table.png"],
  "seq_power_off_on": ["key 7 SHIFT ON", "screenshot seq_off.png", "key ON", "screenshot seq_on_again.png"],
};

// Screenshots that differ from the native simulator for a known reason of the platform.
const ABOUT_LEN = "the web page has no Python or notebook-page module, so About is shorter and its scrollbar differs";
const PLATFORM_DIFF = { "evlog_about.png": ABOUT_LEN, "evlog_after_power.png": ABOUT_LEN };

function parseScript(text) {
  return text.split(/\r?\n/).map(l => l.trim()).filter(l => l && !l.startsWith("#"));
}

async function runWasm(lines, shotDir) {
  const sim = await CalcSim.create(createCalcModule);
  const shots = [], errors = [];
  let log = "";
  for (const line of lines) {
    const out = sim.console(line);
    log += "> " + line + "\n" + out;
    for (const l of out.split("\n")) if (l.startsWith("ERR")) errors.push(line + " -> " + l);
    const m = /^screenshot\s+(\S+)(\s+fb)?/.exec(line);
    if (m) {
      const rgba = m[2] ? rgbaFrom565(sim.framebuffer565()) : sim.lcdRGBA().slice();
      writePNG(path.join(shotDir, path.basename(m[1])), rgba, W, H);
      shots.push(path.basename(m[1]));
    }
    if (/^quit\b/.test(line)) break;
    if (/^SKIP/m.test(out)) break; /* requirecfg: the script is not for this build */
  }
  return { shots, errors, log, expectFailures: sim.expectFailures() };
}

function runNative(scriptFile, shotDir) {
  fs.mkdirSync(shotDir, { recursive: true });
  try {
    return { out: execFileSync(SIM, ["--script", scriptFile, "--shots", shotDir], { encoding: "utf8" }), ok: true };
  } catch (e) {
    return { out: String(e.stdout || ""), ok: false };
  }
}

function comparePNG(a, b) {
  const A = readPNG(a), B = readPNG(b);
  if (A.w !== B.w || A.h !== B.h) return { diff: -1 };
  let diff = 0, first = null;
  for (let i = 0; i < A.w * A.h; i++)
    if (A.rgba[i * 4] !== B.rgba[i * 4] || A.rgba[i * 4 + 1] !== B.rgba[i * 4 + 1] || A.rgba[i * 4 + 2] !== B.rgba[i * 4 + 2]) {
      if (!first) first = [i % A.w, (i / A.w) | 0];
      diff++;
    }
  return { diff, first };
}

/* ------------------------------------------------------------ page smoke */
/* A fake localStorage (a Map), shared between "visits" to model a reload. */
function fakeStorage() {
  const m = new Map();
  return { map: m, getItem: k => (m.has(k) ? m.get(k) : null), setItem: (k, v) => m.set(k, String(v)),
    removeItem: k => m.delete(k) };
}

/* Load the built page in a node vm with a minimal fake DOM (opts.search: the URL's query,
 * opts.storage: localStorage). Resolves when the calculator is ready. */
function loadPage(file, opts) {
  opts = opts || {};
  const html = fs.readFileSync(file, "utf8");
  const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);

  // --- minimal fake DOM
  const listeners = new Map();
  const rafQueue = [];
  let putCount = 0;
  function el(id, tag) {
    const e = {
      id, tagName: tag, style: {}, dataset: {}, children: [], className: "", textContent: "", title: "",
      clientWidth: 1000, clientHeight: 800, attrs: {},
      classList: {
        set: new Set(),
        add(c) { this.set.add(c); }, remove(c) { this.set.delete(c); }, contains(c) { return this.set.has(c); },
      },
      addEventListener(t, f) { (listeners.get(e) || listeners.set(e, {}).get(e))[t] = f; },
      setAttribute(k, v) { e.attrs[k] = v; },
      appendChild(c) { e.children.push(c); return c; },
      setPointerCapture() {},
      getBoundingClientRect() { return { left: 100, top: 50, width: 640, height: 480 }; },
      getContext() {
        return { createImageData: (w, h) => ({ width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }),
          putImageData: (img) => { putCount++; e.lastImage = img; } };
      },
    };
    return e;
  }
  const els = { calc: el("calc", "div"), stage: el("stage", "div"), lcd: el("lcd", "canvas"), status: el("status", "span"),
    calcimg: el("calcimg", "img"), allmods: el("allmods", "button") };
  const win = {
    addEventListener(t, f) { (listeners.get(win) || listeners.set(win, {}).get(win))[t] = f; },
    requestAnimationFrame(f) { rafQueue.push(f); return rafQueue.length; },
    location: { search: opts.search || "" },
  };
  if (opts.storage) win.localStorage = opts.storage;
  const document = {
    getElementById: id => els[id] || null,
    createElement: tag => el(null, tag),
  };
  const logs = [];
  const sandbox = {
    window: win, document, console: { log: (...a) => logs.push(a.join(" ")), warn: (...a) => logs.push("WARN " + a.join(" ")),
      error: (...a) => logs.push("ERROR " + a.join(" ")) },
    getComputedStyle: () => ({ paddingLeft: "16", paddingRight: "16", paddingTop: "8", paddingBottom: "4" }),
    setTimeout, clearTimeout, WebAssembly, TextDecoder, TextEncoder, URL, Promise, btoa, atob,
    Uint8Array, Uint8ClampedArray, Uint16Array, Int32Array, Float64Array, Date, Math,
  };
  sandbox.globalThis = sandbox;
  win.document = document;
  Object.setPrototypeOf(win, sandbox); // window.X falls back to globals
  vm.createContext(sandbox);
  // scripts share globals through `window`; mirror window props as globals after each script
  for (const src of scripts) {
    vm.runInContext(src, sandbox, { filename: "calculator.html" });
    for (const k of Object.keys(win)) if (!(k in sandbox)) sandbox[k] = win[k];
    if (sandbox.CalcSim && !win.CalcSim) win.CalcSim = sandbox.CalcSim;
  }
  return new Promise(resolve => {
    const t0 = Date.now();
    (function waitReady() {
      if (!win.calc) {
        if (Date.now() - t0 > 10000) return resolve(null);
        return setTimeout(waitReady, 10);
      }
      let ts = 0;
      resolve({ win, els, logs, sim: win.calc, putCount: () => putCount,
        frames: n => { for (let i = 0; i < n; i++) { const f = rafQueue.shift(); if (f) f((ts += 16)); } },
        fire: (target, type, ev) => listeners.get(target)[type](Object.assign({ preventDefault() {}, button: 0 }, ev)) });
    })();
  });
}

function pageSmokeTest(file) {
  const html = fs.readFileSync(file, "utf8");
  const problems = [];
  if (/(src|href)\s*=\s*["']?(https?:)?\/\//i.test(html)) problems.push("external src/href found");
  if (/https?:\/\//.test(html.replace(/data:[^"']*/g, ""))) problems.push("http(s) URL text found");
  if (!/<title>Open Graphing Calculator<\/title>/.test(html)) problems.push("title missing");
  return loadPage(file).then(pg => {
    if (!pg) return { problems: problems.concat("module never became ready"), logs: [] };
    const { win, els, logs, sim, frames, fire } = pg;
    return new Promise(resolve => {
    (function () {
      const spots = {};
      for (const c of els.calc.children) spots[c.dataset.key] = c;
      frames(2);
      // click 2 + 3 enter on the hot-spots
      for (const k of ["2", "ADD", "3", "ENTER"]) {
        fire(spots[k], "pointerdown", { pointerId: 1 });
        if (!spots[k].classList.contains("pressed")) problems.push("no pressed state on " + k);
        frames(1);
        fire(spots[k], "pointerup", { pointerId: 1 });
        frames(1);
      }
      let out = sim.console("state last_output");
      if (!/last_output=5\b/.test(out)) problems.push("click 2+3 enter gave: " + out.trim());
      // keyboard: 6 * 7 Enter, Esc
      for (const [key, code] of [["6", "Digit6"], ["*", "Digit8"], ["7", "Digit7"], ["Enter", "Enter"]]) {
        fire(win, "keydown", { key, code });
        fire(win, "keydown", { key, code, repeat: true }); // auto-repeat must be ignored
        frames(1);
        fire(win, "keyup", { key, code });
        frames(1);
      }
      out = sim.console("state last_output");
      if (!/last_output=42\b/.test(out)) problems.push("keyboard 6*7 Enter gave: " + out.trim());
      fire(win, "keydown", { key: "F1", code: "F1" }); frames(1); fire(win, "keyup", { key: "F1", code: "F1" }); frames(1);
      out = sim.console("state screen");
      if (!/screen=yeq/.test(out)) problems.push("F1 did not open Y=: " + out.trim());
      // touch forwarding: canvas rect is 640x480 at (100,50) -> (110,50) in LCD pixels
      fire(els.lcd, "pointerdown", { pointerId: 7, clientX: 320, clientY: 150 });
      const ts_ = sim.M._web_touch_state();
      if (ts_ !== ((50 << 16) | 110)) problems.push("touch mapping wrong: " + ts_);
      fire(els.lcd, "pointerup", { pointerId: 7, clientX: 320, clientY: 150 });
      if (sim.M._web_touch_state() !== -1) problems.push("touch release not forwarded");
      frames(3);
      if (!pg.putCount()) problems.push("canvas never drawn");
      const keymap = win.CALC_KEYMAP;
      if (Object.keys(spots).length !== keymap.keys.length) problems.push("hot-spot count mismatch");
      // the LCD canvas sits exactly on the panel's active area (CAD, case.AA) at the picture's scale,
      // 320x240 pixels = 64.8 x 48.6 mm (0.2025 mm per pixel), inside the case window (case.VA)
      {
        const d = keymap.display, im = keymap.image, sc = im.scale_px_per_mm;
        const exp = (mm) => [im.width / 2 + (mm[0] - mm[2] / 2) * sc, im.height / 2 - (mm[1] + mm[3] / 2) * sc,
          mm[2] * sc, mm[3] * sc];
        const near = (r, e) => r.every((v, i) => Math.abs(v - e[i]) <= 1.0);
        if (!d.active_mm || !near(d.rect, exp(d.active_mm))) problems.push("LCD rect does not match the CAD active area: " + JSON.stringify(d));
        if (d.viewing_mm && !near(d.viewing_area, exp(d.viewing_mm))) problems.push("viewing area does not match the CAD window");
        if (Math.abs(d.rect[2] / d.rect[3] - 4 / 3) > 0.01) problems.push("LCD rect aspect is not 4:3: " + d.rect);
        if (d.active_mm && Math.abs(d.active_mm[2] / 320 - 0.2025) > 0.001) problems.push("LCD pixel pitch is not 0.2025 mm");
        const vr = d.viewing_area, r = d.rect;
        if (r[0] < vr[0] - 1 || r[1] < vr[1] - 1 || r[0] + r[2] > vr[0] + vr[2] + 1 || r[1] + r[3] > vr[1] + vr[3] + 1)
          problems.push("LCD rect is not inside the case window");
      }
      // every key of the layout has a hot-spot, and the firmware knows it
      const names = (win.CALC_KEYS || []).map(k => k.name).sort().join(",");
      if (names !== keymap.keys.map(k => k.name).sort().join(",")) problems.push("hot-spots differ from keys/layout.py");
      for (const k of keymap.keys) if (!sim.keyId(k.name)) problems.push("no firmware key for hot-spot " + k.name);
      if (logs.some(l => l.startsWith("WARN") || l.startsWith("ERROR"))) problems.push("console: " + logs.join(" | "));
      if (/error/.test(els.status.className)) problems.push("status error: " + els.status.textContent);
      const putCount = pg.putCount();
      resolve({ problems, logs, status: els.status.textContent, spots: Object.keys(spots).length, putCount,
        lastImage: els.lcd.lastImage });
    })();
    });
  });
}

/* "Install all modules": every module bundled into the emulator installed (`module bundled`
 * then lists each as a reinstall), by the button, by ?preset=all, and again after a reload,
 * with the session (saved progress) kept in localStorage. */
async function pageAllModulesTest(file) {
  const problems = [];
  const allInstalled = (sim, what) => {
    const out = sim.console("module bundled");
    const rows = out.split("\n").filter(l => /^\S+ \d+\.\d+\.\d+ /.test(l));
    if (rows.length < 3 || rows.some(l => !/: reinstall$/.test(l)))
      problems.push(what + ": not all bundled modules installed: " + out.trim().replace(/\n/g, " | "));
    for (const m of ["manual", "animation", "mathquest"])
      if (!rows.some(l => l.startsWith(m + " "))) problems.push(what + ": " + m + " is not bundled");
  };
  // the button: nothing installed before, everything after; remembered in localStorage
  const storage = fakeStorage();
  let pg = await loadPage(file, { storage });
  if (!pg) return ["page never became ready"];
  pg.frames(2);
  if (pg.els.allmods.textContent !== "Install all modules") problems.push("button label: " + pg.els.allmods.textContent);
  if (/mathquest \S+ \S+: reinstall/.test(pg.sim.console("module bundled"))) problems.push("Math Quest installed before the click");
  pg.fire(pg.els.allmods, "click", {});
  pg.frames(2);
  allInstalled(pg.sim, "button");
  if (!/screen=message/.test(pg.sim.console("state screen"))) problems.push("button: no result message on the LCD");
  if (storage.getItem("calc_all_modules") !== "1") problems.push("button: not remembered");
  if (pg.els.allmods.textContent !== "All modules installed") problems.push("button label after: " + pg.els.allmods.textContent);
  // progress in Math Quest, kept when the page goes away
  pg.sim.console("key CLEAR");
  pg.sim.console("mq new Ana 2");
  pg.sim.console("mq coins 77");
  pg.sim.console("mq save");
  pg.fire(pg.win, "pagehide", {});
  if (!storage.getItem("calc_preset_all")) problems.push("button: the session was not kept");
  // reload: installed again at startup, the progress is back
  pg = await loadPage(file, { storage });
  if (!pg) return problems.concat("reload: page never became ready");
  pg.frames(2);
  allInstalled(pg.sim, "reload");
  pg.sim.console("app mathquest");
  if (!/mq_coins=77\b/.test(pg.sim.console("state"))) problems.push("reload: Math Quest progress not restored");
  if (pg.els.allmods.textContent !== "All modules installed") problems.push("reload: button label " + pg.els.allmods.textContent);
  // clicking again stops installing them at startup
  pg.fire(pg.els.allmods, "click", {});
  if (storage.getItem("calc_all_modules") !== null) problems.push("second click did not forget");
  // ?preset=all on a fresh browser
  pg = await loadPage(file, { search: "?preset=all", storage: fakeStorage() });
  if (!pg) return problems.concat("?preset=all: page never became ready");
  pg.frames(2);
  allInstalled(pg.sim, "?preset=all");
  if (!/screen=home/.test(pg.sim.console("state screen"))) problems.push("?preset=all: the install message was left open");
  if (pg.logs.some(l => l.startsWith("WARN") || l.startsWith("ERROR"))) problems.push("console: " + pg.logs.join(" | "));
  return problems;
}

/* ------------------------------------------------------------------ the page API: files, presets, kept sessions */
/* A tiny theme pack (version 1: one 8 x 8 sprite for the bakery slot), built here. */
function tinyTheme() {
  const pal = Buffer.from([0, 0, 0x00, 0xf8]); // transparent, red
  const ent = Buffer.alloc(16);
  ent.write("Berry", 0, "ascii");
  ent[12] = 2;
  const px = Buffer.alloc(64, 1);
  const payload = Buffer.concat([pal, ent, px]);
  const h = Buffer.alloc(64);
  h.write("MQTH", 0, "ascii");
  h.writeUInt16LE(1, 4), h.writeUInt16LE(64, 6);
  h.write("Web test", 8, "ascii");
  h[32] = 8, h[33] = 8, h[34] = 1, h[35] = 0, h.writeUInt16LE(2, 36);
  h.writeUInt32LE(payload.length, 40), h.writeUInt32LE(crc32(payload) >>> 0, 44);
  return new Uint8Array(Buffer.concat([h, payload]));
}

async function apiTest() {
  const problems = [];
  const sim = await CalcSim.create(createCalcModule);
  const log = sim.runPreset({ card: 64, modules: ["MATHQ"], files: { "/CALC/THEMES/WEBTEST.THM": tinyTheme() },
    app: "mathquest", theme: "WEBTEST.THM" });
  if (!log.some(l => /^module install \/CALC\/MODULES\/MATHQ/.test(l))) problems.push("preset did not install Math Quest: " + log.join(" | "));
  if (!/WEBTEST\.THM/.test(sim.console("fs ls /CALC/THEMES"))) problems.push("writeFile: the file is not on the card");
  let st = sim.console("state");
  if (!/screen=mathquest/.test(st)) problems.push("preset did not open the app");
  if (!/mq_theme=WEBTEST\.THM/.test(st)) problems.push("preset theme not loaded: " + (st.match(/mq_theme=.*/) || [""])[0]);
  if (sim.writeFile("/CALC/X/Y.BIN", new Uint8Array([1, 2, 3])) !== 0) problems.push("writeFile into a new folder failed");
  // kept sessions: progress survives a "reload" (a new instance with the kept session)
  sim.console("mq new Ana 2");
  sim.console("mq coins 123");
  sim.console("mq save");
  const kept = sim.sessionGet();
  if (!(kept.length > 100)) problems.push("sessionGet: " + kept.length + " bytes");
  const sim2 = await CalcSim.create(createCalcModule);
  sim2.runPreset({ card: 64, modules: ["MATHQ"], app: "mathquest", session: CalcSim.toBase64(kept) });
  st = sim2.console("state");
  if (!/mq_coins=123\b/.test(st)) problems.push("kept session not restored: " + (st.match(/mq_coins=.*/) || ["no mq_coins"])[0]);
  if (sim2.sessionPut(new Uint8Array([1, 2, 3, 4])) === 0) problems.push("sessionPut accepted garbage");
  return problems;
}

/* ------------------------------------------------------------------ main */
(async () => {
  let failures = 0;
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "calcweb-"));
  const nativeDir = path.join(OUT, "native");
  const jobs = [];
  for (const [name, lines] of Object.entries(SCENARIOS)) {
    const f = path.join(tmp, name + ".txt");
    fs.writeFileSync(f, lines.join("\n") + "\n");
    jobs.push({ name, file: f, lines, wasmDir: OUT, nativeDir });
  }
  const uiDir = path.join(FW, "tests/ui");
  for (const f of fs.readdirSync(uiDir).filter(f => f.endsWith(".txt")).sort()) {
    // {ver:NAME} / {mod:NAME} (module versions of this build) are filled in as tests/run_ui.py does
    const file = execFileSync("python3", ["-c", "import sys; sys.path.insert(0, sys.argv[1]); import run_ui; " +
      "print(run_ui.substitute(sys.argv[2], sys.argv[3]))", path.join(FW, "tests"), path.join(uiDir, f), tmp],
      { encoding: "utf8", cwd: FW }).trim();
    jobs.push({ name: "ui/" + f, file, lines: parseScript(fs.readFileSync(file, "utf8")),
      wasmDir: path.join(OUT, "ui"), nativeDir: path.join(tmp, "native_ui") });
  }

  for (const j of jobs) {
    const w = await runWasm(j.lines, j.wasmDir);
    const n = runNative(j.file, j.nativeDir);
    const msgs = [];
    if (w.errors.length) msgs.push("wasm ERR: " + w.errors.join("; "));
    if (w.expectFailures) msgs.push(`wasm expect failures: ${w.expectFailures}`);
    if (!n.ok) msgs.push("native simulator failed");
    let same = 0;
    for (const s of w.shots) {
      const c = comparePNG(path.join(j.wasmDir, s), path.join(j.nativeDir, s));
      if (c.diff === 0) same++;
      else if (PLATFORM_DIFF[s]) console.log(`    ${s}: ${c.diff} pixels differ (expected: ${PLATFORM_DIFF[s]})`);
      else msgs.push(`${s}: ${c.diff} pixels differ from native (first at ${c.first})`);
    }
    const ok = msgs.length === 0;
    if (!ok) failures++;
    console.log(`${ok ? "PASS" : "FAIL"} ${j.name}: ${w.shots.length} screenshots, ${same} identical to native` +
      (msgs.length ? "\n    " + msgs.join("\n    ") : ""));
  }

  if (PAGE) {
    const r = await pageSmokeTest(PAGE);
    if (r.lastImage) writePNG(path.join(OUT, "page_smoke_lcd.png"), r.lastImage.data, W, H);
    const ok = r.problems.length === 0;
    if (!ok) failures++;
    console.log(`${ok ? "PASS" : "FAIL"} page smoke test: ${r.spots} hot-spots, ${r.putCount} canvas updates, status "${r.status}"` +
      (r.problems.length ? "\n    " + r.problems.join("\n    ") : ""));
    const p = await pageAllModulesTest(PAGE);
    if (p.length) failures++;
    console.log(`${p.length ? "FAIL" : "PASS"} page: install all modules (button, ?preset=all, reload)` +
      (p.length ? "\n    " + p.join("\n    ") : ""));
  }
  {
    const p = await apiTest();
    if (p.length) failures++;
    console.log(`${p.length ? "FAIL" : "PASS"} page API: writeFile, presets, kept sessions` +
      (p.length ? "\n    " + p.join("\n    ") : ""));
  }
  fs.rmSync(tmp, { recursive: true, force: true });
  console.log(failures ? `${failures} web test(s) failed` : "web tests passed");
  process.exit(failures ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
