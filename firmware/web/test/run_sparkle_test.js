#!/usr/bin/env node
/* Tests of the standalone Sparkle Town page (web/dist/sparkle.html, `make sparkle-test`).
 *
 * Boots the built page in a node vm with a fake DOM (a recording 2D canvas, a fake Image that
 * "decodes" the art at once, a shared fake localStorage, no Web Audio), and checks:
 *   - it reaches the game: the module bound, the Sparkle Town screen, the cheat sheet on the first
 *     start (and not again), the page locked inside the game (HOME / leaving come back);
 *   - the layout: canvas at devicePixelRatio, the 4:3 play area centred and as big as fits,
 *     the module's display scale, client -> logical mapping;
 *   - drawing: smooth shapes, web-font text, pictures from the high-resolution atlas -- every
 *     picture the module draws on the town map and in a game has its art (the hashes of
 *     platform/web/web_sparkle.c and tools/gen_mq_art_hires.py agree);
 *   - input: the key map (every key exists; keydown / keyup reach the calculator), clicks become
 *     taps (the arcade opens the toy box), the pinch becomes two fingers;
 *   - sound: window.calcSound receives the expected ids in a scripted play (music per area, a
 *     cheer and a dud, Bongo Beats' drums and its rhythm track);
 *   - progress survives a reload (localStorage);
 *   - the size of the page and of its art, and that the art is up to date (--check).
 *
 *   node web/test/run_sparkle_test.js --page web/dist/sparkle.html
 */
"use strict";
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const { execFileSync } = require("child_process");

const args = {};
for (let i = 2; i < process.argv.length; i += 2) args[process.argv[i].replace(/^--/, "")] = process.argv[i + 1];
const FW = path.resolve(__dirname, "../..");
const PAGE = path.resolve(args.page || path.join(FW, "web/dist/sparkle.html"));
const MAX_PAGE = 10 * 1024 * 1024, MAX_ART = 1024 * 1024, MAX_VECTOR = 3 * 1024 * 1024;

let failures = 0;
function check(ok, what, detail) {
  console.log(`${ok ? "PASS" : "FAIL"} ${what}${!ok && detail !== undefined ? ": " + detail : ""}`);
  if (!ok) failures++;
  return ok;
}

function fakeStorage() {
  const m = new Map();
  return { map: m, getItem: k => (m.has(k) ? m.get(k) : null), setItem: (k, v) => m.set(k, String(v)), removeItem: k => m.delete(k) };
}

/* a 2D context that records what is drawn */
function fakeCtx(canvas, rec) {
  const ctx = {
    canvas, font: "", fillStyle: "", strokeStyle: "", lineWidth: 1, lineCap: "", textAlign: "", textBaseline: "",
    imageSmoothingEnabled: true, imageSmoothingQuality: "low", globalCompositeOperation: "", shadowColor: "", shadowBlur: 0,
    measureText(s) { const m = /([\d.]+)px/.exec(ctx.font); return { width: s.length * (m ? +m[1] : 10) * 0.5 }; },
    createImageData: (w, h) => ({ width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }),
    createLinearGradient: () => ({ addColorStop() {} }), createRadialGradient: () => ({ addColorStop() {} }),
    fillText(s) { rec.fillText.push(s); },
    drawImage(img) { rec.drawImage++; if (img && img.isAtlas) rec.atlasDraws++; },
  };
  for (const f of ["fillRect", "arc", "arcTo", "beginPath", "closePath", "moveTo", "lineTo", "rect", "clip", "fill", "stroke",
    "save", "restore", "setTransform", "transform", "putImageData", "translate", "scale", "rotate", "ellipse", "arcTo",
    "bezierCurveTo", "quadraticCurveTo", "fillRect", "clearRect", "strokeRect"])
    if (!ctx[f]) ctx[f] = function () { rec[f] = (rec[f] || 0) + 1; };
  return ctx;
}

function loadPage(opts) {
  opts = opts || {};
  let html = fs.readFileSync(PAGE, "utf8");
  if (opts.config) html = html.replace("<!--SPARKLE_EXTRA-->", "<script>window.SPARKLE_CONFIG = " + JSON.stringify(opts.config) + ";</script>");
  const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
  const listeners = new Map();
  const rec = { fillText: [], drawImage: 0, atlasDraws: 0 };
  const rafQueue = [], intervals = [];
  function on(target) { return listeners.get(target) || listeners.set(target, {}).get(target); }
  function el(id, tag) {
    const e = { id, tagName: tag, style: {}, hidden: id === "menu" || id === "help", textContent: "", value: 70, width: 300, height: 150,
      addEventListener(t, f) { on(e)[t] = f; }, setPointerCapture() {}, setAttribute() {} };
    if (tag === "canvas") e.getContext = () => e._ctx || (e._ctx = fakeCtx(e, rec));
    return e;
  }
  const ids = ["view", "status", "menubtn", "menu", "mute", "volume", "keys", "fullscreen", "help"];
  const els = {};
  ids.forEach(i => { els[i] = el(i, i === "view" ? "canvas" : "div"); });
  const document = {
    getElementById: id => els[id] || null, createElement: tag => el(null, tag), hidden: false,
    addEventListener(t, f) { on(document)[t] = f; }, documentElement: {},
  };
  class FakeImage {
    set src(v) { this._src = v; this.isAtlas = true; this.width = 4096; setTimeout(() => this.onload && this.onload(), 0); }
    get src() { return this._src; }
  }
  const win = {
    innerWidth: opts.w || 1470, innerHeight: opts.h || 956, devicePixelRatio: opts.dpr || 2, screen: { height: opts.h || 956 },
    addEventListener(t, f) { on(win)[t] = f; },
    requestAnimationFrame(f) { rafQueue.push(f); return rafQueue.length; },
    localStorage: opts.storage, Image: FakeImage, location: { search: "" },
  };
  const logs = [];
  const sandbox = {
    window: win, document, console: { log: (...a) => logs.push(a.join(" ")), warn: (...a) => logs.push("WARN " + a.join(" ")),
      error: (...a) => logs.push("ERROR " + a.join(" ")) },
    setTimeout, clearTimeout, setInterval: (f, ms) => { intervals.push(f); return intervals.length; }, clearInterval() {},
    WebAssembly, TextDecoder, TextEncoder, URL, Promise, btoa, atob, performance,
    Uint8Array, Uint8ClampedArray, Uint16Array, Uint32Array, Int32Array, Float64Array, Date, Math,
  };
  sandbox.globalThis = sandbox;
  win.document = document;
  Object.setPrototypeOf(win, sandbox);
  vm.createContext(sandbox);
  for (const src of scripts) {
    vm.runInContext(src, sandbox, { filename: "sparkle.html" });
    for (const k of Object.keys(win)) if (!(k in sandbox)) sandbox[k] = win[k];
  }
  return new Promise(resolve => {
    const t0 = Date.now();
    (function wait() {
      const S = win.Sparkle;
      if (!S || !S.ready) {
        if (Date.now() - t0 > 20000) return resolve(null);
        return setTimeout(wait, 10);
      }
      let ts = 0;
      const pg = {
        win, els, rec, logs, S, sim: S.sim, storage: opts.storage, intervals,
        frames(n) { for (let i = 0; i < n; i++) { const f = rafQueue.shift(); if (f) f((ts += 40)); } },
        fire(target, type, ev) { const h = on(target)[type]; if (h) h(Object.assign({ preventDefault() {}, button: 0 }, ev)); },
        state(k) { const m = new RegExp("^" + k + "=(.*)$", "m").exec(S.sim.console("state")); return m ? m[1].trim() : null; },
        con(line) { return S.sim.console(line); },
        stats() { /* commands since the last call, by kind; [8] pictures without art */
          const M = S.sim.M, p = M._web_sparkle_stats();
          return Array.from(new Uint32Array(M.HEAPU8.buffer, p, 12));
        },
        /* a click at logical (x, y): pointer events at the client position */
        click(x, y) {
          const L = S.L, cx = (L.play.x + (x + 0.5) * L.z) / L.dpr, cy = (L.play.y + (y + 0.5) * L.z) / L.dpr;
          pg.fire(els.view, "pointerdown", { pointerId: 7, clientX: cx, clientY: cy });
          pg.fire(els.view, "pointerup", { pointerId: 7, clientX: cx, clientY: cy });
        },
      };
      setTimeout(() => resolve(pg), 20); /* the art "decodes" */
    })();
  });
}

function soundIds(log, kind) { return log.filter(e => e[0] === kind).map(e => e[1]); }

async function main() {
  /* ---- size and freshness */
  const size = fs.statSync(PAGE).size;
  const art = path.join(FW, "web/assets/sparkle_art");
  const artSize = fs.statSync(art + ".webp").size;
  check(size < MAX_PAGE, `page size ${(size / 1048576).toFixed(2)} MB (< ${MAX_PAGE / 1048576} MB)`, size);
  check(artSize < MAX_ART, `high-resolution art ${(artSize / 1024).toFixed(0)} KB (< ${MAX_ART / 1024} KB)`, artSize);
  try {
    const out = execFileSync("python3", [path.join(FW, "tools/gen_mq_art_hires.py"), "--check", art], { encoding: "utf8" });
    check(true, "art up to date (" + out.trim() + ")");
  } catch (e) {
    check(false, "art up to date (make sparkle-art)", (e.stderr || e.message || "").toString().trim());
  }

  /* ---- boot */
  const storage = fakeStorage();
  const pg = await loadPage({ storage });
  if (!check(!!pg, "the page boots")) return;
  const S = pg.S;
  pg.frames(3);
  check(pg.state("screen") === "mathquest", "it opens Sparkle Town", pg.state("screen"));
  check(pg.state("mq_module") === "ok", "the module is bound", pg.state("mq_module"));
  check(pg.els.status.hidden === true, "the loading notice is gone");
  check(pg.els.help.hidden === false, "the cheat sheet shows on the first start");
  pg.fire(pg.win, "keydown", { key: "Enter", code: "Enter" });
  pg.fire(pg.win, "keyup", { key: "Enter", code: "Enter" });
  check(pg.els.help.hidden === true && storage.getItem("sparkle_help_seen") === "1", "a key hides it for good");

  /* ---- layout and scale */
  const L = S.L;
  check(S.S === 7 && S.sim.M._web_sparkle_scale() === 7, "display scale 7 for a 956 pt screen at 2x", S.S);
  check(L.canvasW === 2940 && L.canvasH === 1912, "canvas at devicePixelRatio", `${L.canvasW}x${L.canvasH}`);
  check(Math.abs(L.play.w / L.play.h - 4 / 3) < 0.01 && Math.abs(L.play.x * 2 + L.play.w - L.canvasW) <= 1 &&
    Math.abs(L.play.y * 2 + L.play.h - L.canvasH) <= 1, "the 4:3 play area centred", JSON.stringify(L.play));
  const l2 = S.layout(1280, 800, 2, 6);
  check(l2.play.h === 1600 && l2.play.w === 2133 && Math.abs(l2.k - 1600 / 240 / 6) < 1e-9, "the play area fills the height (1280x800 @2x)",
    JSON.stringify(l2));
  const l3 = S.layout(1000, 600, 1, 2);
  check(l3.z === 2.5 && l3.play.h === 600 && l3.play.x === 100, "and is centred between the sides (1000x600 @1x)", JSON.stringify(l3));
  const l4 = S.layout(400, 300, 1, 2, 0);
  check(l4.z === 1.25 && l4.play.x === 0, "small windows scale down smoothly", JSON.stringify(l4));
  /* the corner menu button (14 + 46 px, top right) never sits on the game, 4:3 windows too (render audit M10) */
  [[1024, 768, 2], [800, 600, 2], [1470, 956, 2], [1280, 800, 2], [735, 956, 2], [1440, 1080, 1]].forEach(function (sz) {
    const lw = S.layout(sz[0], sz[1], sz[2], 2), d = sz[2], bx0 = (sz[0] - 60) * d, by1 = 60 * d;
    const clear = lw.play.x + lw.play.w <= bx0 || lw.play.y >= by1;
    check(clear && lw.play.w >= 0.85 * Math.min(sz[0] * d, sz[1] * d * 4 / 3), "menu button beside the game at " + sz.join("x"),
      JSON.stringify(lw.play));
  });
  check(S.chooseScale(1117, 900, 2) === 8 && S.chooseScale(600, 600, 1) === 2, "the scale follows the screen (2..8)");
  const tl = S.toLogical(L, (L.play.x + 0.5) / 2, (L.play.y + 0.5) / 2, false),
    br = S.toLogical(L, (L.play.x + L.play.w - 1) / 2, (L.play.y + L.play.h - 1) / 2, false);
  check(tl[0] === 0 && tl[1] === 0 && br[0] === 319 && br[1] === 239 && S.toLogical(L, 5, 5, false) === null,
    "client -> logical coordinates (outside the play area: none)", JSON.stringify([tl, br]));

  /* ---- drawing: the town map has the cast, shapes and text */
  pg.con("mq seed 5"); pg.con("mq fun off"); pg.con("mq new Mia 2"); pg.con("mq go map");
  pg.stats();
  S.renderer.counts = {};
  pg.con("redraw");
  pg.frames(4);
  let st = pg.stats(), c = S.renderer.counts;
  check(st[6] > 5 && (c.vector || 0) > 5, "the map's pictures are drawn from the vector art", `${st[6]} images, counts ${JSON.stringify(c)}`);
  check(st[8] === 0 && !c.imageMiss, "every picture on the map has its art", `${st[8]} missing`);
  check((c.shape1 || 0) + (c.shape3 || 0) + (c.shape5 || 0) > 10, "shapes are drawn smoothly (circles, rounded rectangles, polygons)", JSON.stringify(c));
  check((c.sprite || 0) > 5 && st[9] > 5 && st[10] === 0, "the town's buildings, sky and street are the vector set's", `${st[9]} sprites, ${st[10]} without art`);
  check((c.softbar || 0) > 0, "the soft keys are vector pills", JSON.stringify(c));
  check(pg.rec.fillText.some(s => /Mia/.test(s)), "text in the web font", pg.rec.fillText.slice(0, 8).join(" | "));
  check(S.renderer.k === L.z / S.S, "commands scaled by z / S", S.renderer.k);

  /* ---- the vector set: coverage, every scene of normal play, every resolution */
  const VEC = pg.win.SPARKLE_VECTOR, V = S.vec;
  check(!!(VEC && V), "the page has the vector set (window.SPARKLE_VECTOR, Sparkle.vec)");
  const pics = JSON.parse(execFileSync("python3", ["-c",
    "import sys, json; sys.path.insert(0, sys.argv[1]); import gen_mq_art_hires as H; " +
    "print(json.dumps(['%08x' % p[1] for p in H.pictures(hires=False)]))", path.join(FW, "tools")], { encoding: "utf8" }));
  const noVec = pics.filter(h => !VEC.hash[h]);
  check(noVec.length === 0, `every built-in picture has vector art (${pics.length - noVec.length} of ${pics.length}: the cast in every face, stickers, every item frame)`,
    noVec.slice(0, 5).join(" "));
  /* every sprite id the module names (string literals of mq_hr_sprite in its sources) has vector art or a rig */
  const MQ = path.join(FW, "modules/mathquest");
  const ids = new Set();
  for (const f of fs.readdirSync(MQ).filter(f => /\.c$/.test(f))) {
    const src = fs.readFileSync(path.join(MQ, f), "utf8");
    for (const m of src.matchAll(/"((?:town|bg|game|stage|prop|pat|ui|model)\/[a-z_]+|squishy)"/g)) ids.add(m[1]);
  }
  const APPROVED = new Set([]); /* ids allowed to stay shapes (none) */
  const noArt = [...ids].filter(id => !V.has(id) && !V.rigReady(id) && !APPROVED.has(id));
  check(ids.size > 80 && noArt.length === 0, `every thing the module names has vector art (${ids.size - noArt.length} of ${ids.size} ids)`, noArt.join(" "));
  const vecBytes = Object.values(VEC.svg).reduce((a, s) => a + s.length, 0);
  check(vecBytes < MAX_VECTOR, `the vector set: ${Object.keys(VEC.svg).length} pictures, ${(vecBytes / 1024).toFixed(0)} KB (< ${MAX_VECTOR / 1048576} MB)`, vecBytes);
  try {
    const out = execFileSync("python3", [path.join(FW, "tools/gen_mq_vector.py"), "--check", MQ + "/art_vector"], { encoding: "utf8" });
    check(true, "vector art up to date (" + out.trim() + ")");
  } catch (e) {
    check(false, "vector art up to date (make sparkle-vector)", (e.stderr || e.message || "").toString().trim());
  }
  /* the scenes of normal play draw from the vector set, and nothing they name lacks art */
  const SCENES = [["the town", ["mq go map"]], ["her room", ["mq go map", "key F1", "mq step 4"]],
    ["the shop", ["mq go map", "key F1", "mq step 4", "key F1", "mq step 4"]], ["the arcade", ["mq go arcade", "mq step 4"]],
    ["a game", ["mq level 0 0", "mq step 60"]], ["Bubble Pop", ["mq skill 17 1", "mq mech 0", "mq step 40"]],
    ["Kart Race", ["mq skill 17 1", "mq mech 3", "mq step 40"]], ["a squishy", ["mq go map", "mq fun 36000", "mq fun beat squishy", "mq step 10"]],
    ["a way's model (blocks)", ["mq scene off", "mq skill 16 0", "mq strat 34 bulb 2 12 5", "mq strat try", "mq strat act 2", "mq strat act 17", "mq strat act 32", "mq step 4"]],
    ["a way's model (the garden turned)", ["mq skill 16 0", "mq strat 14 bulb 2 4 1", "mq strat try", "mq strat act 45", "mq step 4"]]];
  const ps = await loadPage({ storage: fakeStorage() });
  ["mq seed 5", "mq fun off", "mq new Mia 2"].forEach(l => ps.con(l));
  for (const [what, lines] of SCENES) {
    lines.forEach(l => ps.con(l));
    ps.stats(); ps.S.renderer.counts = {};
    ps.con("redraw"); ps.frames(3);
    const s2 = ps.stats(), c2 = ps.S.renderer.counts, cov = ps.S.coverage();
    check(s2[8] === 0 && s2[10] === 0 && (c2.vector || 0) + (c2.sprite || 0) > 3 && !c2.imageMiss && !c2.spriteMiss,
      `${what}: every picture and thing vector (${c2.vector || 0} pictures, ${c2.sprite || 0} things)`,
      `missing: ${s2[8]} pictures, sprites ${cov.spritesMissed.join(" ")}`);
    ps.con("key CLEAR"); ps.con("key CLEAR"); ps.con("mq step 4");
  }
  for (const dpr of [1, 2, 3]) { /* each resolution rasterises the vectors at its own size */
    const p3 = await loadPage({ storage: fakeStorage(), dpr, w: 1470, h: 956 });
    if (!check(!!p3, `the page boots at devicePixelRatio ${dpr}`)) continue;
    p3.frames(2);
    p3.con("mq seed 5"); p3.con("mq fun off"); p3.con("mq new Mia 2"); p3.con("mq go map");
    p3.S.renderer.counts = {};
    p3.con("redraw"); p3.frames(3);
    const c3 = p3.S.renderer.counts, Vd = p3.S.vec;
    let maxW = 0;
    Vd.cache.forEach(e => { maxW = Math.max(maxW, e.w); });
    check((c3.vector || 0) > 5 && (c3.sprite || 0) > 5 && Vd.rasters > 10 && p3.S.L.canvasW === Math.round(1470 * dpr),
      `at ${dpr}x: vector pictures rasterised at device size (${Vd.rasters} rasters, the widest ${maxW} px, canvas ${p3.S.L.canvasW})`,
      JSON.stringify(c3));
    check(!p3.logs.some(l => /^ERROR/.test(l)), `at ${dpr}x: no errors`, p3.logs.filter(l => /^ERROR/.test(l)).slice(0, 2).join(" | "));
  }

  /* ---- input: keys */
  const km = S.KEYMAP, bad = Object.keys(km).filter(k => !S.sim.keyId(km[k]));
  check(bad.length === 0, "every key of the key map exists", bad.join(" "));
  const pressed = [];
  const realKey = S.sim.key.bind(S.sim);
  S.sim.key = (n, d) => { pressed.push(n + (d ? "+" : "-")); return realKey(n, d); };
  for (const [key, code] of [["ArrowUp", "ArrowUp"], ["ArrowLeft", "ArrowLeft"], ["7", "Digit7"], ["Enter", "Enter"],
    ["Backspace", "Backspace"], ["Escape", "Escape"], ["q", "KeyQ"]]) {
    pg.fire(pg.win, "keydown", { key, code });
    pg.fire(pg.win, "keydown", { key, code }); /* auto-repeat: once */
    pg.fire(pg.win, "keyup", { key, code });
  }
  S.sim.key = realKey;
  check(pressed.join(" ") === "UP+ UP- LEFT+ LEFT- 7+ 7- ENTER+ ENTER- DEL+ DEL- CLEAR+ CLEAR-",
    "arrows, digits, Enter, Backspace and Escape reach the matching keys (others not)", pressed.join(" "));
  check(pg.state("screen") === "mathquest", "still in Sparkle Town after the keys");

  /* ---- input: R (a leftover) is the calculator's letter key; the wheel and the trackpad roll a heist wheel */
  pressed.length = 0;
  S.sim.key = (n, d) => { pressed.push(n + (d ? "+" : "-")); return realKey(n, d); };
  pg.fire(pg.win, "keydown", { key: "r", code: "KeyR" });
  S.sim.key = realKey;
  check(pressed.join(" ") === "ALPHA+ ALPHA- 9+ 9-", "r types the R of a leftover (ALPHA, 9)", pressed.join(" "));
  /* (the wheels themselves: on her own with column work, so no notebook; the big idea already met) */
  pg.con("mq notebook 2"); pg.con("mq teach 4"); pg.con("mq teach done"); pg.con("mq teach 5"); pg.con("mq teach done");
  pg.con("mq skill 16 5"); pg.con('mq mech "Treasure Heist"'); pg.con("mq step 80"); /* (after the acted story) */
  if (pg.state("mq_teach")) pg.con("mq teach done"); /* (its big idea may come first: the wheels after it) */
  const lock0 = pg.state("mq_lock") || "", nW = lock0.length, pitch = Math.min(40, Math.floor(304 / Math.max(1, nW)));
  const wx = 160 - nW * pitch / 2 + pitch / 2, wy = 18 + 58 + 44 + 32, L2 = S.L;
  const wcx = (L2.play.x + (wx + 0.5) * L2.z) / L2.dpr, wcy = (L2.play.y + (wy + 0.5) * L2.z) / L2.dpr;
  let wt = 100000, prevented = 0;
  const wheel = (dy, o) => { wt += (o && o.gap) || 16; prevented = 0;
    pg.fire(pg.els.view, "wheel", Object.assign({ deltaY: dy, deltaMode: 0, clientX: wcx, clientY: wcy, timeStamp: wt,
      preventDefault() { prevented++; } }, o || {})); };
  wheel(-100, { gap: 500, wheelDeltaY: 120 });
  const lock1 = pg.state("mq_lock") || "";
  check(nW >= 2 && lock1[0] === String.fromCharCode(lock0.charCodeAt(0) + 1) && prevented === 1,
    "one click of a mouse wheel up rolls the wheel under the pointer up one notch (never more), the page not scrolled",
    lock0 + " -> " + lock1);
  wheel(30, { gap: 500, clientX: 100, clientY: 100 }); wheel(30, { gap: 16 });
  check((pg.state("mq_lock") || "")[0] === String.fromCharCode(lock1.charCodeAt(0) - 1),
    "a trackpad's small scrolls gather into a notch", pg.state("mq_lock"));
  /* ---- the wheel streams: a mouse (discrete clicks), a trackpad with its inertial tail, Ctrl+wheel (a pinch) */
  {
    const realScroll = S.scroll, steps = [];
    S.scroll = (n, x, y) => { steps.push(n); };
    let t = 2e6;
    const feed = (dys, gap, o) => { for (const dy of dys) { t += gap; pg.fire(pg.els.view, "wheel", Object.assign({ deltaY: dy,
      deltaMode: 0, clientX: wcx, clientY: wcy, timeStamp: t }, o || {})); } t += 600; };
    /* a mouse: five clicks down (Chrome: 100 px, wheelDelta -120 each; Safari's lines; Firefox's lines) */
    feed([100, 100, 100, 100, 100], 120, { wheelDeltaY: -120 });
    check(steps.join() === "1,1,1,1,1", "five clicks of a mouse wheel: five notches, one each", steps.join());
    steps.length = 0;
    feed([3, 3, 3], 120, { deltaMode: 1 });
    check(steps.join() === "1,1,1", "a wheel in lines (Firefox): one notch a click", steps.join());
    steps.length = 0;
    feed([-100, -100], 90, {});
    check(steps.join() === "-1,-1", "a mouse wheel up (no wheelDelta): one notch a click, up", steps.join());
    /* a trackpad: the fingers' push (rising then steady), then the momentum tail falling slowly away */
    steps.length = 0;
    const push = [2, 4, 6, 8, 10, 12, 12, 12, 10], tail = [];
    for (let v = 9; v >= 0.5; v *= 0.93) tail.push(Math.round(v * 10) / 10);
    feed(push.concat(tail), 16, {});
    const sum = push.concat(tail).reduce((a, b) => a + b, 0);
    check(steps.length >= 1 && steps.length <= 3 && steps.every(s => s === 1),
      "a trackpad flick: the fingers' scroll steps, its inertial tail is damped (" + Math.round(sum) + " px would be " +
      Math.floor(sum / 40) + " notches)", steps.join());
    steps.length = 0;
    const slow = []; for (let i = 0; i < 20; i++) slow.push(-6);
    feed(slow, 16, {});
    check(steps.length === 3 && steps.every(s => s === -1), "a slow steady trackpad scroll up (120 px): a notch every 40 px", steps.join());
    /* Ctrl+wheel: a pinch, never a scroll */
    steps.length = 0;
    const realPinch = S.pinch, pinches = [];
    S.pinch = (ph) => pinches.push(ph);
    feed([-20, -20, -20, -20], 16, { ctrlKey: true });
    await new Promise(r => setTimeout(r, 250));
    S.pinch = realPinch;
    check(steps.length === 0 && pinches[0] === "start" && pinches.includes("change") && pinches[pinches.length - 1] === "end",
      "Ctrl+wheel (a trackpad pinch) is a pinch, not a scroll", steps.join() + " / " + pinches.join());
    S.scroll = realScroll;
  }
  /* ---- the wheel does nothing outside an adjustable widget: no screen changes, no walking, no paging */
  {
    let t = 3e6, bad = [];
    const rnd = (k => () => (k = (k * 1103515245 + 12345) % 2147483648) / 2147483648)(7);
    for (const where of ["title", "map", "world", "room", "stickers", "parent"]) {
      pg.con("mq go " + where); pg.frames(2);
      const sc = pg.state("mq_scene");
      for (let i = 0; i < 40; i++) {
        const x = Math.floor(rnd() * 320), y = Math.floor(rnd() * 240), L = S.L;
        t += rnd() < 0.3 ? 400 : 16;
        pg.fire(pg.els.view, "wheel", { deltaY: (rnd() < 0.5 ? -1 : 1) * (rnd() < 0.5 ? 100 : 1 + Math.floor(rnd() * 30)),
          deltaMode: 0, timeStamp: t, clientX: (L.play.x + (x + 0.5) * L.z) / L.dpr, clientY: (L.play.y + (y + 0.5) * L.z) / L.dpr });
        pg.frames(1);
      }
      if (pg.state("mq_scene") !== sc) bad.push(where + "->" + pg.state("mq_scene"));
    }
    check(!bad.length, "random wheel and trackpad scrolls on the town, worlds, rooms, title, stickers and grown-ups' screens change no screen", bad.join(" "));
    check(/overscroll-behavior:\s*none/.test(require("fs").readFileSync(PAGE, "utf8")), "the page never scrolls or bounces (overscroll-behavior: none)");
  }
  pg.con("mq go title"); pg.frames(2);
  pg.con("key CLEAR"); pg.con("key CLEAR");

  /* ---- input: the computer keyboard types a name (letters in both cases, space, - and '; Backspace,
   * Enter, Escape), up to 16 characters, with no calculator key pressed for the printable ones */
  const typeKeys = (s) => { for (const ch of s) { pg.fire(pg.win, "keydown", { key: ch, code: "Key" + ch }); pg.fire(pg.win, "keyup", { key: ch, code: "Key" + ch }); } };
  const tapKey = (k) => { pg.fire(pg.win, "keydown", { key: k, code: k }); pg.fire(pg.win, "keyup", { key: k, code: k }); };
  pg.con("mq go title"); pg.frames(2);
  pg.click(235, 130); pg.frames(2);
  check(pg.state("mq_scene") === "name", "a click on the free card opens the name screen", pg.state("mq_scene"));
  typeKeys("ab"); tapKey("Escape"); pg.frames(1);
  check(pg.state("mq_scene") === "title", "Escape cancels the name", pg.state("mq_scene"));
  pg.click(235, 130); pg.frames(2);
  pressed.length = 0;
  S.sim.key = (n, d) => { pressed.push(n + (d ? "+" : "-")); return realKey(n, d); };
  typeKeys("zoe-Mae O'Briensxy");
  S.sim.key = realKey;
  check(pressed.length === 0, "printable keys go to the name, not to calculator keys (- is not SUB, r not R)", pressed.join(" "));
  pg.frames(1);
  check(pg.rec.fillText.some(s => s === "Zoe-Mae O'Briens_"), "the name as typed, its first letter a capital, 16 characters at most",
    pg.rec.fillText.filter(s => /_$/.test(s)).slice(-2).join(" | "));
  tapKey("Backspace"); tapKey("Enter"); pg.frames(2);
  check(pg.state("mq_player") === "Zoe-Mae O'Brien", "Backspace deletes, Enter confirms", pg.state("mq_player"));
  pg.con("key F5"); pg.con("key CLEAR"); pg.con("mq player 0"); pg.con("mq step 4"); /* (Mia again: the reload below) */

  /* ---- night mode on the Mac (the playtest: "night mode looks the exact same"; the render audit's H1, H2):
   * a place's painted backdrop and every other rig are drawn as by day and then tinted with the module's
   * own mq_tint (multiply, add); the town's rigs tint themselves; the pictures carry the tint word too */
  {
    const R = S.renderer, origRig = R._rigTinted, origImg = R._image, rigs = [], imgs = [], ops = [];
    R._rigTinted = function (rig, V, spr) { rigs.push([spr.id, spr.tint]); return origRig.apply(this, arguments); };
    R._image = function (hash, a) { imgs.push(a[11]); return origImg.apply(this, arguments); };
    const night = (twist) => {
      rigs.length = 0, imgs.length = 0;
      pg.con("mq scene off"); pg.con("mq opengames"); pg.con("mq grant bosses"); pg.con("mq engine stop");
      pg.con("mq skill 17 1"); pg.con('mq mech "Bubble Pop"'); pg.con("mq twist " + twist); pg.con("mq step 20");
      pg.frames(2);
      return { bg: rigs.filter(r => /^bg\//.test(r[0])), game: rigs.filter(r => /^game\//.test(r[0])), imgs: imgs.slice() };
    };
    const n = night(1), d = night(2);
    check(n.bg.length > 0 && n.bg.every(r => r[1] === 2), "night: the place's backdrop is tinted like the module's colours",
      JSON.stringify(n.bg.slice(0, 4)));
    check(!d.bg.length && !d.game.length, "by day (another twist): nothing is tinted", JSON.stringify(d.bg));
    imgs.length = 0;
    pg.con("mq twist 0"); pg.con("mq grant all"); pg.con("mq playtime 15"); pg.con("mq go map"); pg.con("mq step 10"); pg.frames(2);
    const tn = imgs.slice();
    imgs.length = 0;
    pg.con("mq playtime 0"); pg.con("mq step 10"); pg.frames(2);
    check(tn.some(t => t === 2) && imgs.length > 0 && imgs.every(t => t === 0),
      "the town's pictures carry the tint word (night 2, day 0)", JSON.stringify([tn.slice(0, 4), imgs.slice(0, 4)]));
    R._rigTinted = origRig, R._image = origImg;
    check(pg.win.SparkleVector.rigs["town/house"].ownTint && !pg.win.SparkleVector.rigs["bg/garden"].ownTint,
      "the town's rigs tint themselves, the places' are tinted by the page");
    pg.con("mq twist 0"); pg.con("mq go map"); pg.con("mq step 4");
  }
  /* ---- the pink cake on the Mac (the playtest: "How much of the cake is pink?" with no pink on the cake):
   * the question's pie shows its pink slices from the first frame, in each game that asks it */
  {
    const R = S.renderer, orig = R._shape, PINK = (255 >> 3) << 11 | (179 >> 2) << 5 | (199 >> 3), SH_POLY = 5;
    const pink = [];
    R._shape = function (a) { if (a[0] === SH_POLY && a[1] === PINK) pink.push([a[5] + "," + a[6], Array.from(a.slice(5, 5 + 2 * a[4])).join(",")]); return orig.apply(this, arguments); };
    const slices = (mech, rung) => {
      pg.con("mq scene off"); pg.con("mq engine stop"); pg.con("mq skill 24 " + rung); pg.con('mq mech "' + mech + '"');
      pg.con("mq step 1"); pink.length = 0; pg.frames(1);
      const by = {}; pink.forEach(c => { (by[c[0]] = by[c[0]] || new Set()).add(c[1]); });
      return Math.max(0, ...Object.values(by).map(s => s.size)); /* the distinct pink sectors around one pie's centre */
    };
    ["Bubble Pop", "Cloud Catch"].forEach(mech => {
      const one = slices(mech, 0), more = slices(mech, 2);
      check(one === 1 && more >= 2, `${mech}: the cake's pink slices are drawn from the first frame of the question (1/2 or 1/4: one; 2 or more)`,
        `${one} ${more}`);
    });
    R._shape = orig;
    pg.con("mq go map"); pg.con("mq step 4");
  }
  /* ---- the grown-ups' gate from the corner menu (no F-keys needed): a problem past the curriculum, its answer
   * typed on the computer's keys or clicked on the pad; clicks, a held click and wrong answers never open it */
  pg.con("mq go map"); pg.frames(2);
  pg.con("mq gate seed 11"); /* (5n + 17 = 57: n = 8) */
  if (typeof S.grownups === "function") S.grownups();
  pg.frames(2);
  check(pg.rec.fillText.some(t => /Grown-ups only/.test(t)) && pg.state("mq_gate") === "up", "the corner menu's Grown-ups... shows the gate",
    pg.rec.fillText.slice(-4).join(" | "));
  check(pg.state("mq_gate_text") === "If 5n + 17 = 57, what is n?", "the gate asks a grown-up's problem", pg.state("mq_gate_text"));
  pg.con("touch 160 130 down"); pg.con("touch 160 130 up"); pg.click(60, 80); pg.con("touch 250 100 down"); pg.con("wait 2300");
  pg.con("touch 250 100 up"); pg.frames(2);
  check(pg.state("mq_scene") === "map", "clicks and a held click on the gate open nothing", pg.state("mq_scene"));
  tapKey("9"); tapKey("Enter"); pg.frames(2);
  check(pg.state("mq_scene") === "map" && pg.state("mq_gate") === "wait", "a wrong answer opens nothing and pauses", pg.state("mq_gate"));
  pg.con("mq step 60"); pg.con("mq gate seed 11"); pg.frames(2);
  tapKey("8"); tapKey("Enter"); pg.frames(2);
  check(pg.state("mq_scene") === "parent", "the right answer typed on the keyboard opens the grown-ups' screen", pg.state("mq_scene"));
  pg.con("key CLEAR"); pg.con("mq step 2");
  pg.con("mq gate seed 11"); S.grownups(); pg.frames(2);
  pg.click(254, 113); pg.frames(1); pg.click(254, 181); pg.frames(2); /* (the pad's 8, then OK) */
  check(pg.state("mq_scene") === "parent", "the right answer clicked on the pad opens the grown-ups' screen", pg.state("mq_scene"));
  pg.con("key CLEAR"); pg.con("mq step 2");

  /* ---- input: a click is a tap (the arcade opens the toy box) */
  pg.con("mq go arcade"); pg.con("mq step 4");
  pg.click(160, 202);
  pg.frames(2);
  check(pg.state("mq_scene") === "toys", "a click on the arcade's toy box opens it", pg.state("mq_scene"));
  pg.click(5, 5); /* the status bar: nothing breaks */
  const touched = [];
  const realT = S.sim.touchId.bind(S.sim);
  S.sim.touchId = (id, x, y, s) => { touched.push(id + ":" + s); return realT(id, x, y, s); };
  S.pinch("start", 1, 160, 120); S.pinch("change", 1.6); S.pinch("change", 2.2); S.pinch("end", 2.2);
  S.sim.touchId = realT;
  check(touched.join(" ") === "3:1 4:1 3:2 4:2 3:2 4:2 3:0 4:0", "a trackpad pinch becomes two fingers", touched.join(" "));

  /* ---- sound */
  const log = S.audio.log;
  check(typeof pg.win.calcSound === "function", "window.calcSound is the synth's");
  log.length = 0;
  pg.click(56, 70); /* Bongo Beats: the cats sing an original song along the beat clock */
  pg.con("mq step 80"); pg.con("mq correct"); pg.con("mq step 40");
  let sfx = soundIds(log, "sfx");
  check(sfx.includes(25) && sfx.includes(28) && sfx.includes(29), "Bongo Beats: the cats sing (mew 25), paws on the bongos (28), the bass (29)", sfx.join(","));
  ["mew", "trill", "meow", "paw", "bass", "chord", "tamb"].forEach(function (nm, k) { /* the new sounds synthesise */
    try { S.audio.sfx(25 + k, 72 | 2 << 7); } catch (e) { check(false, "sfx " + nm + " plays", e.message); }
  });
  { /* clicked bongos (the Mac's mouse: pointer down and up, the core's DOWN then TAP): one click is one drum -- a right
     * one on the beat is "perfect"/"good", the combo grows, and no fizzle's thud follows (the user: "I think bongo beats
     * is broken": the tap reached the pad's button too and drummed again) */
    const comboNow = () => +(pg.state("mq_toy") || "0").split(" ").pop();
    let clicked = 0, combo0 = comboNow(), combo = combo0;
    log.length = 0;
    for (let f = 0; f < 400 && clicked < 3; f++) {
      const b = (pg.state("mq_beats") || "").split(" ");
      if (b.length === 2 && +b[1] >= -20 && +b[1] <= 30) {
        pg.click(8 + 77 * +b[0] + 34, 18 + 156 + 20); pg.frames(1); clicked++, combo = comboNow();
      }
      pg.con("mq step 1");
    }
    check(clicked === 3 && combo === combo0 + 3, "Bongo Beats: three clicked right drums, the combo +3", clicked + " " + combo0 + " -> " + combo);
    check(!soundIds(log, "sfx").includes(9), "Bongo Beats: no fizzle's thud after a clicked right drum", soundIds(log, "sfx").join(","));
  }
  for (let k = 0; k < 12; k++) { pg.con("mq correct"); pg.con("mq step 60"); } /* every order drummed: the finale */
  sfx = soundIds(log, "sfx");
  check(sfx.includes(26) && sfx.includes(27), "a combo trills (26), the finale's big meow (27)", sfx.join(","));
  check(soundIds(log, "music").includes(4), "after the song: the arcade track (music 4)", soundIds(log, "music").join(","));
  pg.con("key CLEAR"); pg.click(160, 70); pg.con("mq step 4"); /* the jam: the free drums on the rhythm track */
  check(soundIds(log, "music").includes(8), "the jam asks for the rhythm track (music 8)", soundIds(log, "music").join(","));
  pg.con("key CLEAR");
  log.length = 0;
  pg.con("mq skill 17 1"); pg.con("mq step 30"); pg.con("mq correct"); pg.con("mq step 20");
  pg.con("mq skill 6 1"); pg.con("mq step 30"); pg.con("mq wrong"); pg.con("mq step 20");
  sfx = soundIds(log, "sfx"); let mus = soundIds(log, "music");
  check(sfx.includes(8), "a right answer cheers (sfx 8)", sfx.join(","));
  check(sfx.includes(9), "a wrong one is a soft dud (sfx 9)", sfx.join(","));
  check(mus.some(m => m >= 1 && m <= 7), "a game plays an area's music", mus.join(","));
  pg.con("mq level 0 0"); pg.con("mq step 5");
  check(soundIds(log, "sfx").includes(21), "a friend tells the story in their voice (sfx 21)", soundIds(log, "sfx").join(","));
  const unknown = log.filter(e => (e[0] === "sfx" && !(e[1] >= 1 && e[1] <= 24)) || (e[0] === "music" && !(e[1] >= 0 && e[1] <= 8)));
  check(unknown.length === 0, "only known sound ids", JSON.stringify(unknown));
  /* ---- voices: the characters talk (calcSpeak), cute babble by default, words for reading help */
  const SV = pg.win.SparkleVoice, voice = S.voice;
  check(typeof pg.win.calcSpeak === "function" && !!SV && !!voice, "window.calcSpeak talks through SparkleVoice");
  voice.log.length = 0;
  pg.con("mq level 0 1"); pg.con("mq step 5");
  check(voice.log.length > 0 && voice.log[0][1] === "babble", "an order is said aloud, as babble by default",
    JSON.stringify(voice.log.slice(0, 2)));
  const p1 = SV.plan("How many beads?", SV.PROFILES[0]), p2 = SV.plan("How many beads?", SV.PROFILES[0]);
  check(JSON.stringify(p1) === JSON.stringify(p2) && p1.length === 4, "the babble is deterministic: a blip a syllable", JSON.stringify(p1));
  check(p1[3].glide > 1.2, "a question rises at its end", JSON.stringify(p1[3]));
  const p3 = SV.plan("Yay! We did it!", SV.PROFILES[0]), p4 = SV.plan("All done.", SV.PROFILES[0]);
  check(p3.some(s => s.glide > 1.1) && p4[p4.length - 1].glide < 1, "bouncy on \"!\", down on \".\"");
  check(p1.every((s, i) => i === 0 || s.t > p1[i - 1].t) && p1[p1.length - 1].t < 1000, "syllable-timed, about a tenth of a second each");
  const slow = SV.plan("How many beads?", SV.PROFILES[3]); /* Olli: deep and slow */
  check(slow[slow.length - 1].t > p1[p1.length - 1].t && slow[0].f0 < p1[0].f0, "each friend's own pitch and speed");
  const fake = { getVoices: () => [{ name: "Fred", lang: "en-US" }, { name: "Karen", lang: "en-AU" }, { name: "Samantha (Enhanced)", lang: "en-US" }] };
  const v2 = new SV({ synth: fake });
  check(v2.pickVoice(SV.PROFILES[0]).name === "Samantha (Enhanced)", "words: an enhanced child-friendly voice first");
  check(v2.pickVoice({ babble: {}, tts: { voice: "Karen" } }).name === "Karen", "a profile may name its voice");
  check(SV.PROFILES.length === 17 && SV.PROFILES.every(p => p.tts.pitch >= 1.1 && p.tts.pitch <= 1.5),
    "a voice profile for every friend and visitor, pitched up gently for words");
  check(Object.keys(pg.win.SparkleAudio.TRACKS).length === 8 && pg.win.SparkleAudio.SFX_NAMES.length === 34 &&
    pg.win.SparkleAudio.VOICES.length === 17, "a synth for every sfx id and every music track, a voice for every friend and visitor");

  /* ---- locked inside the game */
  pg.con("key HOME"); pg.frames(2);
  check(pg.state("screen") === "mathquest", "HOME stays in Sparkle Town", pg.state("screen"));
  pg.con("app home"); pg.frames(2);
  check(pg.state("screen") === "mathquest", "another app is switched back to Sparkle Town", pg.state("screen"));

  /* ---- progress across a reload */
  pg.con("mq go map"); pg.con("mq save"); /* (the module saves at most once a second; this test does not wait) */
  const stars = pg.state("mq_stars"), coins = pg.state("mq_coins");
  pg.fire(pg.win, "pagehide", {});
  check(!!storage.getItem("sparkle_town_progress"), "progress saved in localStorage when the page goes away");
  const pg2 = await loadPage({ storage });
  if (check(!!pg2, "the page boots again")) {
    pg2.frames(2);
    pg2.con("mq player 0"); pg2.con("mq step 2");
    check(pg2.state("mq_player") === pg.state("mq_player") && pg2.state("mq_coins") === coins && pg2.state("mq_stars") === stars,
      "the player, coins and stars come back", `${pg2.state("mq_player")} ${pg2.state("mq_coins")} ${pg2.state("mq_stars")} (was ${pg.state("mq_player")} ${coins} ${stars})`);
    check(pg2.els.help.hidden === true, "no cheat sheet the second time");
  }
  /* ---- her tracking on the Mac page (the user: "I'm not sure your problem tracking is working"): 50 orders, a
   * reload (the page's own save on pagehide), 50 more -- the problem histories, boxes, rungs, review and the
   * play-time clock come back as they were, and no problem she got right comes back before its time */
  {
    const st2 = fakeStorage();
    let tp = await loadPage({ storage: st2 });
    if (check(!!tp, "tracking: the page boots")) {
      tp.frames(2);
      tp.con("mq new Tia 2"); tp.con("mq fun off"); tp.con("mq scene off"); tp.con("mq school 3 10");
      const asked = [], rightOnce = new Map();
      const play = (n) => {
        let k = 0;
        for (let round = 0; k < n && round < 60; round++) {
          tp.con("mq level " + [0, 2, 1, 4][round % 4] + " 0"); tp.con("mq step 40");
          for (let j = 0; j < 12 && k < n && tp.state("mq_scene") === "play"; j++) {
            if (tp.state("mq_moment") !== "play") { tp.con("mq step 40"); tp.con("mq next"); continue; }
            const skill = tp.state("mq_skill"), info = tp.con("mq info"), key = (/id ([0-9a-f]+)/.exec(info) || ["", info])[1] + " " + info.split("\n")[0].trim();
            asked.push({ skill, key, at: k });
            if (skill !== "Fact garden") {
              if (rightOnce.has(key)) asked[asked.length - 1].repeat = true;
              rightOnce.set(key, true);
            }
            tp.con("mq step 60"); tp.con("mq correct"); tp.con("mq step 50");
            for (let s = 0; s < 6 && tp.state("mq_scene") === "play" && tp.state("mq_moment") !== "play"; s++) tp.con("mq next"), tp.con("mq step 10");
            k++;
          }
        }
        return k;
      };
      const n1 = play(50);
      const before = tp.con("mq tracking").trim();
      tp.fire(tp.win, "pagehide", {});
      tp = await loadPage({ storage: st2 });
      if (check(!!tp, "tracking: the page boots again")) {
        tp.frames(2); tp.con("mq player 0"); tp.con("mq step 2");
        const after = tp.con("mq tracking").trim();
        check(after === before, "tracking: histories, boxes, rungs, review and the play clock survive a reload", `${before} -> ${after}`);
        const n2 = play(50);
        const reps = asked.filter(a => a.repeat);
        check(n1 + n2 >= 90 && reps.length === 0, `tracking: ${n1 + n2} orders over a reload, no right-answered problem back before its time`,
          reps.slice(0, 3).map(a => a.key).join(" || "));
        const t2 = tp.con("mq tracking").trim(), m = /rungs (\d+)/.exec(t2), m0 = /rungs (\d+)/.exec(before);
        check(m && m0 && +m[1] >= +m0[1], "tracking: the rungs keep climbing after the reload", `${before} -> ${t2}`);
      }
    }
  }
  /* ---- the grown-ups' grade for an existing player on the Mac page (the user: "I set 4th grade ... not much
   * difference"): grade 3 week 10, some play, then grade 4 week 10 -- most of the next orders are grade 4, and the
   * setting is kept over a reload */
  {
    const st3 = fakeStorage();
    let gp = await loadPage({ storage: st3 });
    if (check(!!gp, "grade: the page boots")) {
      gp.frames(2);
      gp.con("mq new Gia 2"); gp.con("mq fun off"); gp.con("mq scene off"); gp.con("mq school 3 10");
      const low = [];
      const run = (n) => {
        let k = 0, g4 = 0;
        for (let round = 0; k < n && round < 40; round++) {
          gp.con("mq level " + [0, 2, 1, 4][round % 4] + " 0"); gp.con("mq step 40");
          for (let j = 0; j < 12 && k < n && gp.state("mq_scene") === "play"; j++) {
            if (gp.state("mq_moment") !== "play") { gp.con("mq step 40"); gp.con("mq next"); continue; }
            if (gp.state("mq_skill") !== "Fact garden") { k++; g4 += +gp.state("mq_sk_grade") >= 4; if (+gp.state("mq_sk_grade") < 4) low.push(gp.state("mq_skill")); }
            gp.con("mq step 60"); gp.con("mq correct"); gp.con("mq step 50");
            for (let s = 0; s < 6 && gp.state("mq_scene") === "play" && gp.state("mq_moment") !== "play"; s++) gp.con("mq next"), gp.con("mq step 10");
          }
        }
        return k ? g4 * 100 / k : 0;
      };
      run(30);
      gp.con("mq school 4 10");
      const up = run(20);
      gp.fire(gp.win, "pagehide", {});
      gp = await loadPage({ storage: st3 });
      if (check(!!gp, "grade: the page boots again")) {
        gp.frames(2); gp.con("mq player 0"); gp.con("mq step 2");
        const sch = gp.state("mq_school") + " placing " + gp.state("mq_placing") + " strands " + gp.state("mq_strands") + " player " + gp.state("mq_player"), up2 = run(20);
        check(up >= 70 && /^4 10/.test(sch || "") && / strands 4 4 4 4/.test(sch), /* (after the reload: REMAINING_WORK item 3) */
          "grade: an existing player set to grade 4: the next orders grade 4, kept over a reload", `${up.toFixed(0)} % then ${up2.toFixed(0)} %, school ${sch}; below: ${low.slice(-8).join(", ")}`);
      }
    }
  }
  const errs = pg.logs.filter(l => /^ERROR/.test(l));
  check(errs.length === 0, "no errors logged", errs.slice(0, 3).join(" | "));
}

main().then(() => {
  console.log(failures ? `${failures} sparkle test(s) FAILED` : "all sparkle tests passed");
  process.exit(failures ? 1 : 0);
}).catch(e => { console.error(e); process.exit(1); });
