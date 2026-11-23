#!/usr/bin/env node
/* Screenshots of the standalone Sparkle Town page in a real browser, at a Mac's resolution
 * (web/test/mac/*.png, for review): headless Google Chrome driven over the DevTools protocol
 * (node's own WebSocket; nothing to install).
 *
 *   node web/test/run_mac_shots.js [--page web/dist/sparkle.html] [--out web/test/mac]
 *        [--chrome PATH] [--only NAME] [--w 1470 --h 956 --dpr 2]
 *
 * Each shot boots the page fresh, runs console lines (the module's `mq ...` test hooks) and
 * waits for the art; the shot is the whole window at devicePixelRatio (2: 2940 x 1912). It also
 * prints the page's vector coverage for the scene (Sparkle.coverage()).
 */
"use strict";
const fs = require("fs");
const path = require("path");
const os = require("os");
const HC = require(path.join(__dirname, "..", "..", "tools", "headless_chrome.js"));

const args = {};
for (let i = 2; i < process.argv.length; i += 2) args[process.argv[i].replace(/^--/, "")] = process.argv[i + 1];
const FW = path.resolve(__dirname, "../..");
const PAGE = path.resolve(args.page || path.join(FW, "web/dist/sparkle.html"));
const OUT = path.resolve(args.out || path.join(FW, "web/test/mac"));
const CHROME = args.chrome || "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
const W = +(args.w || 1470), H = +(args.h || 956), DPR = +(args.dpr || 2);

const NEW = ["mq seed 5", "mq fun off", "mq new Mia 2"];
const ROOM = ["mq fun 36000", "mq grant bosses", "mq coins 9000", "mq style Pink_stripes", "mq buy Sleepy_kitten", "mq buy Fish_bowl",
  "mq buy Star_lamp", "mq buy Bookshelf", "mq buy Cat_clock", "mq buy Fairy_lights", "mq buy Heart_paper", "mq buy Pink_carpet",
  "mq style Heart_paper", "mq style Pink_carpet", "mq buy Sunny_window", "mq buy Cloud_sofa", "mq buy Teddy_bear",
  "mq place Sleepy_kitten 7 7", "mq place Fish_bowl 10 5", "mq place Bookshelf 16 1", "mq place Star_lamp 13 4",
  "mq place Cat_clock 10 1", "mq place Fairy_lights 0 0", "mq place Sunny_window 3 1", "mq place Cloud_sofa 2 5",
  "mq place Teddy_bear 14 7", "mq sel -1", "mq step 30"];
const SHOTS = {
  town: NEW.concat(["mq go map", "mq step 20"]),
  town_dpr1: { dpr: 1, lines: NEW.concat(["mq go map", "mq step 20"]) },
  town_dpr3: { dpr: 3, lines: NEW.concat(["mq go map", "mq step 20"]) },
  room: NEW.concat(["mq go map", "key F1", "mq step 4"], ROOM),
  shop: NEW.concat(["mq go map", "mq fun 36000", "mq coins 300", "key F1", "mq step 4", "key F1", "mq step 6"]),
  pop: NEW.concat(["mq skill 17 1", "mq mech 0", "mq step 60"]),
  game: NEW.concat(["mq level 0 0", "mq step 80"]),
  kart: NEW.concat(["mq skill 17 1", "mq mech 3", "mq step 60"]),
  squishy: NEW.concat(["mq go map", "mq fun 36000", "mq fun beat squishy", "mq step 20"]),
  shelf: NEW.concat(["mq go map", "mq fun 36000", "mq fun beat squishy", "mq step 300", "mq fun beat maker", "mq step 300",
    "mq go map", "key F1", "mq step 10"]),
  title: ["mq go title", "mq step 30"],
  arcade: NEW.concat(["mq go arcade", "mq step 20"]),
  clock: NEW.concat(["mq skill 38 1", "mq teach done", "mq step 150"]),
  coins: NEW.concat(["mq skill 44 1", "mq teach done", "mq step 150"]),
  coinspop: NEW.concat(["mq skill 44 1", "mq mech 0", "mq teach done", "mq step 150"]),
  el0: NEW.concat(["mq skill 40 0", "mq teach done", "mq step 150"]),
  el2: NEW.concat(["mq skill 40 2", "mq teach done", "mq step 150"]),
  t1r2: NEW.concat(["mq skill 39 2", "mq teach done", "mq step 150"]),
  world3: NEW.concat(["mq world 3", "mq step 30"]),
  night: NEW.concat(["mq skill 17 1", "mq mech 0", "mq twist 1", "mq step 80"]),
  day: NEW.concat(["mq skill 17 1", "mq mech 0", "mq step 80"]),
  clockpop: NEW.concat(["mq skill 38 1", "mq mech 0", "mq teach done", "mq step 150"]),
  clockcatch: NEW.concat(["mq skill 39 1", "mq mech 1", "mq teach done", "mq step 150"]),
  bongo: NEW.concat(["mq go toys", "mq step 4", "mq tap 56 70", "mq step 100", "mq correct", "mq step 3"]),
  /* the purse and the shop's counter (mq_purse.c) */
  purse: NEW.concat(["mq go map", "mq fun 36000", "mq coins 300", "mq pay setup 0 0", "mq pay open Heart_chair", "mq pay put d",
    "mq pay put p", "mq step 8"]),
  purse_change: NEW.concat(["mq go map", "mq fun 36000", "mq coins 300", "mq pay setup 2 0 0", "mq pay open Bookshelf",
    "mq pay put 1", "mq pay go", "key 7", "key 5", "mq step 8"]),
  purse_picky: NEW.concat(["mq go map", "mq fun 36000", "mq coins 300", "mq pay setup 3 5 -1 1", "mq pay open Little_desk",
    "mq pay put n", "mq pay put n", "mq pay put n", "mq pay put p", "mq pay go", "mq step 8"]),
};

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

async function cdp(wsUrl) {
  const ws = new WebSocket(wsUrl);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
  let id = 0;
  const pending = new Map();
  ws.onmessage = ev => {
    const m = JSON.parse(ev.data);
    if (m.id && pending.has(m.id)) { const p = pending.get(m.id); pending.delete(m.id); m.error ? p.rej(new Error(JSON.stringify(m.error))) : p.res(m.result); }
  };
  return {
    send(method, params, sessionId) {
      return new Promise((res, rej) => { const i = ++id; pending.set(i, { res, rej }); ws.send(JSON.stringify({ id: i, method, params: params || {}, sessionId })); });
    },
    close() { ws.close(); },
  };
}

async function main() {
  fs.mkdirSync(OUT, { recursive: true });
  const browser = await HC.launch({ chrome: CHROME, label: "mac shots" }); /* (tools/headless_chrome.js: always cleaned up) */
  const c = await cdp(browser.wsUrl);
  const only = args.only ? args.only.split(",") : Object.keys(SHOTS);
  let failed = 0;
  for (const name of only) {
    const spec = Array.isArray(SHOTS[name]) ? { lines: SHOTS[name] } : SHOTS[name];
    const dpr = spec.dpr || DPR;
    const { targetId } = await c.send("Target.createTarget", { url: "about:blank" });
    const { sessionId } = await c.send("Target.attachToTarget", { targetId, flatten: true });
    const s = (m, p) => c.send(m, p, sessionId);
    await s("Emulation.setDeviceMetricsOverride", { width: W, height: H, deviceScaleFactor: dpr, mobile: false, screenWidth: W, screenHeight: H });
    await s("Page.enable");
    await s("Page.navigate", { url: "file://" + PAGE });
    const ev = async (expr) => (await s("Runtime.evaluate", { expression: expr, returnByValue: true, awaitPromise: true })).result.value;
    let ok = false;
    for (let i = 0; i < 300 && !ok; i++) { await sleep(100); ok = await ev("!!(window.Sparkle && Sparkle.ready && (!Sparkle.vec || Sparkle.vec.pending === 0))"); }
    if (!ok) { console.log("FAIL " + name + ": the page did not get ready"); failed++; continue; }
    await ev("document.getElementById('help').hidden = true; 1");
    for (const line of spec.lines) await ev(`Sparkle.sim.console(${JSON.stringify(line)})`);
    await ev("Sparkle.coverage(); Sparkle.renderer.seen = {}; Sparkle.renderer.missed = {}; Sparkle.renderer.spriteMissed = {}; Sparkle.sim.console('redraw'); 1");
    await sleep(900);
    if (args.probe) console.log(name + " probe: " + await ev(fs.readFileSync(args.probe, "utf8"))); /* a debugging expression */
    const cov = await ev("JSON.stringify(Sparkle.coverage ? Sparkle.coverage() : {})");
    const shot = await s("Page.captureScreenshot", { format: "png", captureBeyondViewport: false });
    const file = path.join(OUT, "sparkle_" + name + ".png");
    fs.writeFileSync(file, Buffer.from(shot.data, "base64"));
    console.log(`${name}: ${path.relative(FW, file)} (${W}x${H} @${dpr}x) coverage ${cov}`);
    await c.send("Target.closeTarget", { targetId });
  }
  c.close();
  await browser.close();
  process.exit(failed ? 1 : 0);
}
main().catch(e => { console.error(e); process.exit(1); });
