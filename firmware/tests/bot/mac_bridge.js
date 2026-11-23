#!/usr/bin/env node
/* The playtest bot's Mac driver (tests/bot/drivers.py MacDriver): the Sparkle Town page in headless
 * Google Chrome over the DevTools protocol, driven with REAL input events (pointer clicks and drags,
 * the wheel, a ctrl+wheel trackpad pinch, keys) at the page's devicePixelRatio, plus the module's
 * console. One JSON request a line on stdin, one JSON answer a line on stdout.
 *
 *   {op: hello|boot|reload|con|click|drag|key|wheel|pinch|texts|fb|parity|shot|quit, ...}
 *
 * Every answer carries `errors` (page exceptions and console errors since the last answer); the
 * driver turns them into crashes. `texts` returns the renderer's text calls of a full redraw with
 * their logical boxes, `parity` compares the canvas (scaled to 320x240) with the 1x framebuffer.
 */
"use strict";
const fs = require("fs");
const os = require("os");
const path = require("path");
const readline = require("readline");
const HC = require(path.join(__dirname, "..", "..", "tools", "headless_chrome.js"));

const args = {};
for (let i = 2; i < process.argv.length; i += 2) args[process.argv[i].replace(/^--/, "")] = process.argv[i + 1];
const PAGE = path.resolve(args.page);
const CHROME = args.chrome || "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
const W = +(args.w || 1470), H = +(args.h || 956), DPR = +(args.dpr || 2);
const sleep = ms => new Promise(r => setTimeout(r, ms));

/* calculator key names -> the DOM keys the page maps (KEYMAP in web/page/sparkle_page.js) */
const DOMKEY = { UP: "ArrowUp", DOWN: "ArrowDown", LEFT: "ArrowLeft", RIGHT: "ArrowRight", ENTER: "Enter", DEL: "Backspace",
  CLEAR: "Escape", DOT: ".", SUB: "-", DIV: "/", F1: "F1", F2: "F2", F3: "F3", F4: "F4", F5: "F5" };
for (let d = 0; d <= 9; d++) DOMKEY[String(d)] = String(d);
const VK = { ArrowUp: 38, ArrowDown: 40, ArrowLeft: 37, ArrowRight: 39, Enter: 13, Backspace: 8, Escape: 27, ".": 190, "-": 189,
  "/": 191, F1: 112, F2: 113, F3: 114, F4: 115, F5: 116 };

async function cdp(wsUrl) {
  const ws = new WebSocket(wsUrl);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
  let id = 0;
  const pending = new Map(), handlers = [];
  ws.onmessage = ev => {
    const m = JSON.parse(ev.data);
    if (m.id && pending.has(m.id)) {
      const p = pending.get(m.id); pending.delete(m.id);
      m.error ? p.rej(new Error(JSON.stringify(m.error))) : p.res(m.result);
    } else if (m.method) handlers.forEach(h => h(m));
  };
  return {
    send(method, params, sessionId) {
      return new Promise((res, rej) => { const i = ++id; pending.set(i, { res, rej }); ws.send(JSON.stringify({ id: i, method, params: params || {}, sessionId })); });
    },
    on(h) { handlers.push(h); },
    close() { ws.close(); },
  };
}

/* installed in the page after each load: text capture with logical boxes */
const HOOK = `(function(){
  var R = Sparkle.renderer; if (R.__bot) return 1; R.__bot = true;
  var orig = R._text;
  window.__botTexts = null;
  R._text = function (a, s) {
    if (window.__botTexts) {
      var S = this.S || 1, x = a[4] / 16 / S, y = a[5] / 16 / S, w = a[6] / 16 / S, h = a[7] / 16 / S;
      var c = this.ctx, st = Math.max(0, Math.min(3, a[0]));
      var q = 1;
      try { c.save(); c.font = "700 " + (h * this.k * 0.78).toFixed(1) + "px sans-serif";
            var tw = c.measureText(s).width / this.k; c.restore(); if (tw > w && s.length > 1) q = w / tw; } catch (e) {}
      window.__botTexts.push({ s: s, x: Math.round(x), y: Math.round(y), w: Math.round(w), h: Math.round(h), q: +q.toFixed(2),
        band: a[3] ? [Math.round(a[2] / S), Math.round(a[3] / S)] : null, clip: this.clip ? this.clip.map(function (v) { return Math.round(v / S); }) : null });
    }
    return orig.call(this, a, s);
  };
  return 1;
})()`;

/* the canvas against the 1x framebuffer: 16x16 blocks with detail in one and not the other */
const PARITY = `(function(){
  var L = Sparkle.L, view = document.getElementById('view');
  var off = document.createElement('canvas'); off.width = 320; off.height = 240;
  var oc = off.getContext('2d'); oc.imageSmoothingEnabled = true; oc.imageSmoothingQuality = 'high';
  if (Sparkle.redraw) { /* one redraw of the module as it is now: the 1x framebuffer and the canvas from the same frame
                         * (else an animation between the page's last frame and this read shows as missing blocks) */
    window.__botTexts = [];
    var pc = Sparkle.redraw();
    var macTexts = window.__botTexts; window.__botTexts = null;
    var t1x = Sparkle.sim.console('text wh');
    oc.drawImage(pc, 0, 0, pc.width, pc.height, 0, 0, 320, 240);
  } else
    oc.drawImage(view, L.play.x, L.play.y, L.play.w, L.play.h, 0, 0, 320, 240);
  var cv = oc.getImageData(0, 0, 320, 240).data, fb = Sparkle.sim.framebuffer565();
  function lum565(p) { var r = (p >> 11) << 3, g = ((p >> 5) & 63) << 2, b = (p & 31) << 3; return 0.3 * r + 0.59 * g + 0.11 * b; }
  var missing = [], extra = [];
  for (var by = 0; by < 15; by++) for (var bx = 0; bx < 20; bx++) {
    var s1 = 0, q1 = 0, s2 = 0, q2 = 0, n = 0;
    for (var y = by * 16; y < by * 16 + 16; y++) for (var x = bx * 16; x < bx * 16 + 16; x++) {
      var a = lum565(fb[y * 320 + x]), i = (y * 320 + x) * 4, b = 0.3 * cv[i] + 0.59 * cv[i + 1] + 0.11 * cv[i + 2];
      s1 += a; q1 += a * a; s2 += b; q2 += b * b; n++;
    }
    var d1 = Math.sqrt(Math.max(0, q1 / n - (s1 / n) * (s1 / n))), d2 = Math.sqrt(Math.max(0, q2 / n - (s2 / n) * (s2 / n)));
    if (d1 > 18 && d2 < 3) missing.push([bx * 16, by * 16, Math.round(d1), Math.round(d2)]);
    if (d2 > 18 && d1 < 3) extra.push([bx * 16, by * 16, Math.round(d1), Math.round(d2)]);
  }
  return JSON.stringify({ missing: missing, extra: extra, macTexts: typeof macTexts === 'undefined' ? null : macTexts,
                          t1x: typeof t1x === 'undefined' ? null : t1x });
})()`;

async function main() {
  /* one browser for the whole bridge (its own process group and profile, killed and deleted on any exit:
   * tools/headless_chrome.js) */
  const browser = await HC.launch({ chrome: CHROME, label: "bot", args: ["--disable-background-timer-throttling",
    "--disable-renderer-backgrounding", "--disable-backgrounding-occluded-windows"] });
  const cleanup = () => browser.closeSync();
  const c = await cdp(browser.wsUrl);
  const { targetId } = await c.send("Target.createTarget", { url: "about:blank" });
  const { sessionId } = await c.send("Target.attachToTarget", { targetId, flatten: true });
  const s = (m, p) => c.send(m, p, sessionId);
  let errors = [];
  c.on(m => {
    if (m.sessionId !== sessionId) return;
    if (m.method === "Runtime.exceptionThrown") {
      const d = m.params.exceptionDetails;
      errors.push("exception: " + ((d.exception && d.exception.description) || d.text || "?").split("\n")[0]);
    } else if (m.method === "Runtime.consoleAPICalled" && m.params.type === "error") {
      errors.push("console.error: " + m.params.args.map(a => a.value || a.description || "").join(" ").slice(0, 300));
    }
  });
  await s("Emulation.setDeviceMetricsOverride", { width: W, height: H, deviceScaleFactor: DPR, mobile: false, screenWidth: W, screenHeight: H });
  await s("Page.enable");
  await s("Runtime.enable");
  const ev = async (expr) => {
    const r = await s("Runtime.evaluate", { expression: expr, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) throw new Error("evaluate: " + (r.exceptionDetails.exception ? r.exceptionDetails.exception.description : r.exceptionDetails.text));
    return r.result.value;
  };
  const frames = (n) => ev(`new Promise(function (r) { var k = ${n || 2}; (function f() { if (--k < 0) return r(1); requestAnimationFrame(f); })(); })`);
  async function load(fresh) {
    if (fresh) {
      await s("Page.navigate", { url: "about:blank" });
      await sleep(50);
      await s("Page.navigate", { url: "file://" + PAGE });
      await sleep(100);
      await ev("try { localStorage.clear(); } catch (e) {} 1");
    }
    /* (a load that stalls -- a navigation lost while the previous page unloads, the vector art never settling --
     * is tried again, twice, before it counts as the page's hang: the boot hang of the long runs) */
    let ok = false;
    for (let attempt = 0; attempt < 3 && !ok; attempt++) {
      await s("Page.navigate", { url: "file://" + PAGE });
      for (let i = 0; i < 300 && !ok; i++) {
        await sleep(50);
        try { ok = await ev("!!(window.Sparkle && Sparkle.ready && (!Sparkle.vec || Sparkle.vec.pending === 0))"); } catch (e) { ok = false; }
      }
      if (!ok)
        try { ok = await ev("!!(window.Sparkle && Sparkle.ready)"); } catch (e) { ok = false; } /* (the art late: playable) */
    }
    if (!ok) throw new Error("the page did not get ready");
    await ev("var h = document.getElementById('help'); if (h) h.hidden = true; 1");
    await ev(HOOK);
    await frames(2);
  }
  async function client(x, y) {
    return ev(`(function(){ var L = Sparkle.L; return [(L.play.x + (${x} + 0.5) * L.z) / L.dpr, (L.play.y + (${y} + 0.5) * L.z) / L.dpr]; })()`);
  }
  const mouse = (type, p, extra) => s("Input.dispatchMouseEvent", Object.assign({ type, x: p[0], y: p[1], button: "left", clickCount: 1 }, extra || {}));

  const ops = {
    async hello() { return {}; },
    async boot(r) { await load(r.fresh !== false); return { S: await ev("Sparkle.S") }; },
    async reload() {
      /* (no explicit save: the page's own pagehide / interval saving is what is tested) */
      await load(false);
      return {};
    },
    async con(r) { return { out: await ev(`Sparkle.sim.console(${JSON.stringify(r.line)})`) }; },
    async click(r) {
      const p = await client(r.x, r.y);
      await mouse("mousePressed", p, { buttons: 1 });
      await mouse("mouseReleased", p, { buttons: 0 });
      await frames(2);
      return {};
    },
    /* one press in two halves (the bot looks at the screen in between, a kid's real press: 60-160 ms): a mouse
     * button (pressed, released -- Chrome adds the click) or a finger (touchstart, touchend) */
    async down(r) {
      const p = await client(r.x, r.y);
      if (r.how === "touch") {
        await s("Emulation.setTouchEmulationEnabled", { enabled: true, maxTouchPoints: 5 });
        await s("Input.dispatchTouchEvent", { type: "touchStart", touchPoints: [{ x: p[0], y: p[1], id: 1 }] });
      } else
        await mouse("mousePressed", p, { buttons: 1 });
      return {};
    },
    async up(r) {
      const p = await client(r.x, r.y);
      if (r.how === "touch") {
        await s("Input.dispatchTouchEvent", { type: "touchEnd", touchPoints: [] });
        await s("Emulation.setTouchEmulationEnabled", { enabled: false });
      } else
        await mouse("mouseReleased", p, { buttons: 0 });
      await frames(2);
      return {};
    },
    async drag(r) {
      const a = await client(r.x1, r.y1), b = await client(r.x2, r.y2), n = Math.max(1, r.steps || 6);
      await mouse("mousePressed", a, { buttons: 1 });
      for (let i = 1; i <= n; i++) {
        await mouse("mouseMoved", [a[0] + (b[0] - a[0]) * i / n, a[1] + (b[1] - a[1]) * i / n], { buttons: 1 });
        await frames(1);
      }
      await mouse("mouseReleased", b, { buttons: 0 });
      await frames(2);
      return {};
    },
    async key(r) {
      const k = DOMKEY[r.name.toUpperCase()] || DOMKEY[r.name];
      if (!k) { await ev(`Sparkle.sim.console(${JSON.stringify("key " + r.name)})`); await frames(2); return { via: "console" }; }
      const base = { key: k, code: /^\d$/.test(k) ? "Digit" + k : k.length === 1 ? k : k, windowsVirtualKeyCode: VK[k] || k.charCodeAt(0) };
      await s("Input.dispatchKeyEvent", Object.assign({ type: "keyDown" }, base, k.length === 1 ? { text: k } : {}));
      await s("Input.dispatchKeyEvent", Object.assign({ type: "keyUp" }, base));
      await frames(2);
      return {};
    },
    async wheel(r) {
      const p = await client(r.x, r.y);
      /* n clicks of a mouse wheel (each one notch: 100 px, Chrome's wheelDelta 120), a frame or two apart */
      for (let i = 0; i < Math.abs(r.n); i++) {
        await mouse("mouseWheel", p, { deltaX: 0, deltaY: r.n > 0 ? 100 : -100, button: "none" });
        await sleep(70);
        await frames(1);
      }
      await frames(2);
      return {};
    },
    async pinch(r) {
      const p = await client(r.x, r.y), steps = 5, d = -100 * Math.log(Math.max(0.2, Math.min(5, r.scale))) / steps;
      for (let i = 0; i < steps; i++) { await mouse("mouseWheel", p, { deltaX: 0, deltaY: d, modifiers: 2, button: "none" }); await frames(1); }
      await sleep(250);
      await frames(2);
      return {};
    },
    async hold(r) {
      const p = await client(r.x, r.y);
      await mouse("mousePressed", p, { buttons: 1 });
      await sleep(r.ms || 2500);
      await mouse("mouseReleased", p, { buttons: 0 });
      await frames(2);
      return {};
    },
    async thumb() {
      await frames(2);
      return { lum: await ev(`(function(){ var L = Sparkle.L, view = document.getElementById('view');
        var off = document.createElement('canvas'); off.width = 80; off.height = 60; var oc = off.getContext('2d');
        oc.drawImage(view, L.play.x, L.play.y, L.play.w, L.play.h, 0, 0, 80, 60);
        var d = oc.getImageData(0, 0, 80, 60).data, out = [];
        for (var i = 0; i < d.length; i += 4) out.push(Math.round(0.3 * d[i] + 0.59 * d[i + 1] + 0.11 * d[i + 2]));
        return out; })()`) };
    },
    async texts() {
      await frames(1); /* (what earlier console lines drew is drained first) */
      /* one full redraw, drained at once (no module frame in between: a frame's own texts would overlap it) */
      await ev(`(function(){ window.__botTexts = []; Sparkle.sim.console('redraw'); var M = Sparkle.sim.M, lp = M._malloc(4);
        try { var p = M._web_sparkle_take(lp), n = new Uint32Array(M.HEAPU8.buffer, lp, 1)[0];
              if (n) Sparkle.renderer.run(new Int32Array(M.HEAPU8.buffer, p, n), n); } finally { M._free(lp); } return 1; })()`);
      const t = await ev("(function(){ var t = window.__botTexts || []; window.__botTexts = null; return t; })()");
      const seen = new Set(), out = [];
      for (const e of t) { const k = e.s + "@" + e.x + "," + e.y; if (!seen.has(k)) { seen.add(k); out.push(e); } }
      return { texts: out };
    },
    async fb() {
      return { fb: await ev("(function(){ var a = Sparkle.sim.framebuffer565(), u = new Uint8Array(a.buffer, a.byteOffset, a.length * 2), s = ''; for (var i = 0; i < u.length; i += 8192) s += String.fromCharCode.apply(null, u.subarray(i, i + 8192)); return btoa(s); })()") };
    },
    async parity() {
      await ev("Sparkle.sim.console('redraw'); 1");
      await frames(3);
      return JSON.parse(await ev(PARITY));
    },
    async shot() {
      await frames(2);
      const r = await s("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
      return { png: r.data };
    },
    async eval(r) { return { v: await ev(r.js) }; }, /* (debugging the bot: any expression in the page) */
    async quit() { setTimeout(() => process.exit(0), 10); return {}; },
  };

  const rl = readline.createInterface({ input: process.stdin });
  const queue = [];
  let busy = false;
  rl.on("line", line => { queue.push(line); pump(); });
  rl.on("close", () => { cleanup(); process.exit(0); });
  async function pump() {
    if (busy) return;
    busy = true;
    while (queue.length) {
      const req = JSON.parse(queue.shift());
      let res;
      try {
        res = await ops[req.op](req);
        const st = await ev("(function(){ var e = document.getElementById('status'); return e && !e.hidden ? e.textContent : ''; })()").catch(() => "");
        if (st && /stopped|could not/.test(st)) errors.push("page status: " + st);
      } catch (e) {
        res = { fail: String(e.message || e) };
      }
      if (errors.length) { res.errors = errors; errors = []; }
      process.stdout.write(JSON.stringify(res) + "\n");
    }
    busy = false;
  }
}
main().catch(e => { process.stdout.write(JSON.stringify({ fail: String(e.stack || e) }) + "\n"); process.exit(1); });
