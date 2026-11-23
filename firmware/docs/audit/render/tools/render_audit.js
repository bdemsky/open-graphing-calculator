#!/usr/bin/env node
/* Render audit of Sparkle Town: the 1x framebuffer vs the Mac page, scene by scene
 * (firmware/docs/audit/RENDER_AUDIT.md; `make render-audit`).
 *
 *   node docs/audit/render/tools/render_audit.js [--page web/dist/sparkle.html] [--label public]
 *        [--out DIR] [--jobs ui,games,mechs,twists,town,events,screens,occl,overlays] [--only SUBSTR]
 *        [--seeds 1,2] [--skills 0-85] [--chrome PATH] [--simscripts DIR]
 *
 * Headless Google Chrome over the DevTools protocol (node's own WebSocket, nothing to install),
 * a 1280 x 960 window at devicePixelRatio 1: the play area is exactly 4x the 320 x 240 screen and
 * the module draws at display scale 4. The page's animation loop is held (requestAnimationFrame
 * replaced before the page loads), so scenes are deterministic: the module moves only with the
 * console lines (`mq step`, ...) or `sim.frame` in the occlusion timelines. inpage.js does the
 * capture and the comparisons (see there).
 *
 * Writes DIR/<label>/results.jsonl (one capture a line: job, capture, the order, findings,
 * substitutions, texts, occlusions), DIR/<label>/shots/<job>/<capture>.png (1x x2 | Mac | boxes of
 * the findings) and DIR/<label>/fb/<job>__<capture>.png (the 1x framebuffer, for the simulator
 * comparison: sim_compare.py), and with --simscripts the same scenes as simulator scripts.
 */
"use strict";
const fs = require("fs");
const path = require("path");
const os = require("os");
const HC = require(path.join(__dirname, "..", "..", "..", "..", "tools", "headless_chrome.js"));

const args = {};
for (let i = 2; i < process.argv.length; i += 2) args[process.argv[i].replace(/^--/, "")] = process.argv[i + 1];
const FW = path.resolve(__dirname, "../../../..");
const PAGE = path.resolve(args.page || path.join(FW, "web/dist/sparkle.html"));
const LABEL = args.label || "public";
const OUT = path.resolve(args.out || path.join(os.tmpdir(), "sparkle_render_audit"), LABEL);
const CHROME = args.chrome || "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
const JOBS = (args.jobs || "ui,games,mechs,twists,town,events,screens,occl,overlays").split(",");
const SEEDS = (args.seeds || "1,2").split(",").map(Number);
const SK = (args.skills || "0-85").split("-").map(Number);
const SIMSCRIPTS = args.simscripts ? path.resolve(args.simscripts) : null;
const INPAGE = fs.readFileSync(path.join(__dirname, "inpage.js"), "utf8");
const WIN_W = 1280, WIN_H = 960;

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

/* ---------------------------------------------------------------- the scenes */
const NEW = ["mq seed 5", "mq fun off", "mq new Mia 2"];
const cap = (name, o) => Object.assign({ cap: name }, o || {});
const ANIM = ["mq step 3"];

/* the UI tests' scripts: each screenshot is a capture */
function uiJobs() {
  const dir = path.join(FW, "tests/ui");
  return fs.readdirSync(dir).filter(f => /^4[789]_mathquest.*\.txt$/.test(f)).sort().map(f => {
    const lines = [];
    for (let l of fs.readFileSync(path.join(dir, f), "utf8").split("\n")) {
      l = l.trim();
      if (!l || l[0] === "#") continue;
      const m = /^screenshot\s+(\S+)\.png/.exec(l);
      if (m) { lines.push(cap(m[1])); continue; }
      if (/^(requirecfg|fs |module |app |modx sfx|expect)/.test(l)) continue;
      lines.push(l);
    }
    return { name: "ui_" + f.replace(/\.txt$/, ""), lines: lines, sim: true };
  });
}

/* every skill at its lowest and highest rung (rung counts asked of the page), 2 seeds: the stage,
 * the play frame (+ animation), a dud */
function gameJobs(rungs) {
  const jobs = [];
  for (let s = SK[0]; s <= (SK[1] === undefined ? SK[0] : SK[1]); s++) {
    const top = (rungs[s] || 4) - 1;
    const lines = ["mq fun off", "mq opengames"];
    const plan = [];
    for (let d = 0; d <= top; d++) /* the ends with every seed, the rungs between with the first */
      (d === 0 || d === top ? SEEDS : SEEDS.slice(0, 1)).forEach(sd => plan.push([d, sd]));
    for (const [d, sd] of plan) {
      const t = `d${d}_r${sd}`;
      lines.push(`mq new Mia${sd} 2`, "mq engine stop", "mq grant bosses", `mq seed ${sd}`, `mq skill ${s} ${d}`, "mq step 22");
      if (sd === SEEDS[0]) lines.push(cap(t + "_stage"));
      lines.push("mq step 60", cap(t + "_play", { anim: ANIM, order: true }), "mq try", "mq step 10", cap(t + "_dud", { order: true }));
    }
    jobs.push({ name: `game_s${String(s).padStart(2, "0")}`, lines: lines, sim: true });
  }
  return jobs;
}

/* every game with a few skills that fit it (found in the page: `mq mech` says when not) */
const MECHS = ["Bubble Pop", "Cloud Catch", "Gate Dash", "Kart Race", "Peekaboo", "Sort Slide", "Hop Along", "Sky Fling",
  "Pie Shop", "Build It", "Clock Tower", "Shop Till", "Treasure Dig", "Treasure Heist", "Cake Kitchen", "Pet Shop"];
function mechJobs() {
  return MECHS.map((m, k) => ({ name: "mech_" + m.replace(/ /g, ""), mech: m, lines: [], dynamic: "mech" }));
}

const TWIST = ["none", "night", "giant", "flip", "mirror", "remix"];
function twistJobs() {
  const jobs = [];
  const games = [["Bubble Pop", 17], ["Gate Dash", 17], ["Clock Tower", 38], ["Shop Till", 45], ["Pie Shop", 29], ["Kart Race", 17], ["Sky Fling", 25], ["Build It", 3], ["Cake Kitchen", 0], ["Treasure Dig", 70], ["Sort Slide", 14], ["Hop Along", 15]];
  for (let t = 1; t < TWIST.length; t++) {
    const lines = NEW.concat(["mq scene off", "mq opengames", "mq grant bosses", "mq engine stop"]);
    for (const [g, s] of games)
      lines.push(`mq skill ${s} 1`, `mq mech "${g}"`, `mq twist ${t}`, "mq step 20", cap(g.replace(/ /g, "") + "_" + TWIST[t], { anim: ANIM, order: true }));
    jobs.push({ name: "twist_" + TWIST[t], lines: lines, sim: true });
  }
  return jobs;
}

function townJobs() {
  const jobs = [];
  [["day", 0], ["dusk", 12], ["night", 15]].forEach(([nm, min]) => {
    const lines = NEW.concat(["mq grant all", "mq opengames", `mq playtime ${min}`, "mq go map", "mq step 10"]);
    for (let x = 20; x <= 1400; x += 230) lines.push(`mq town ${x}`, "mq step 12", cap(`x${x}`, { anim: ANIM }));
    jobs.push({ name: "town_" + nm, lines: lines, sim: true });
  });
  return jobs;
}

const EVENTS = ["visitor", "lantern", "bubbles", "parade", "snow", "rainbow", "picnic", "kites", "birthday", "stars"];
function eventJobs() {
  return EVENTS.map(ev => {
    const lines = NEW.concat(["mq grant all", `mq event ${ev}`, "mq go map", "mq step 20"]);
    for (const x of [20, 400, 800, 1200]) lines.push(`mq town ${x}`, "mq step 12", cap(`x${x}`, { anim: ANIM }));
    return { name: "event_" + ev, lines: lines, sim: true };
  });
}

const ROOM = ["mq fun 36000", "mq grant bosses", "mq coins 9000", "mq style Pink_stripes", "mq buy Sleepy_kitten", "mq buy Fish_bowl",
  "mq buy Star_lamp", "mq buy Bookshelf", "mq buy Cat_clock", "mq buy Fairy_lights", "mq buy Heart_paper", "mq buy Pink_carpet",
  "mq style Heart_paper", "mq style Pink_carpet", "mq buy Sunny_window", "mq buy Cloud_sofa", "mq buy Teddy_bear",
  "mq place Sleepy_kitten 7 7", "mq place Fish_bowl 10 5", "mq place Bookshelf 16 1", "mq place Star_lamp 13 4",
  "mq place Cat_clock 10 1", "mq place Fairy_lights 0 0", "mq place Sunny_window 3 1", "mq place Cloud_sofa 2 5",
  "mq place Teddy_bear 14 7", "mq sel -1", "mq step 30"];
function screenJobs() {
  const J = [];
  const add = (name, lines) => J.push({ name: name, lines: lines, sim: true });
  add("scr_title", ["mq go title", "mq step 30", cap("title", { anim: ANIM })]);
  add("scr_name", ["mq go name", "mq step 10", cap("name"), "touch tap 127 152", "mq step 2", "touch tap 297 124", "mq step 2", "touch tap 25 124", "mq step 2", "touch tap 127 152", "mq step 2", "touch tap 297 124", "mq step 2", "touch tap 25 124", "mq step 4", cap("name_typed")]);
  add("scr_basic", NEW.concat(["mq grant all", "mq opengames",
    "mq go world", "mq step 20", cap("world"), "mq world 3", "mq step 20", cap("world3"),
    "mq go arcade", "mq step 20", cap("arcade", { anim: ANIM }),
    "mq go stickers", "mq step 10", cap("stickers"),
    "mq go parent", "mq step 10", cap("parent"), "key DOWN", "mq step 4", cap("parent_down"), "key RIGHT", "mq step 4", cap("parent_right"),
    "key F2", "mq step 4", cap("parent_f2"), "key F3", "mq step 4", cap("parent_f3"),
    "mq go complete", "mq step 20", cap("complete"), "mq go break", "mq step 20", cap("break"),
    "mq go surprise", "mq step 20", cap("surprise"), "mq go offer", "mq step 20", cap("offer"),
    "mq go chapter", "mq step 20", cap("chapter"), "mq go done", "mq step 20", cap("done"),
    "mq go example", "mq step 20", cap("example"), "mq go feedback", "mq step 20", cap("feedback")]));
  add("scr_home", NEW.concat(["mq go map", "key F1", "mq step 4"], ROOM, [cap("room", { anim: ANIM }),
    "mq sel 2", "mq step 4", cap("room_sel"), "mq go shop", "mq step 10", cap("shop"), "key RIGHT", "mq step 4", cap("shop_p2"),
    "mq go book", "mq step 10", cap("book"), "mq go showoff", "mq step 10", cap("showoff"), "mq go wardrobe", "mq step 10", cap("wardrobe"),
    "mq room 1", "mq step 10", cap("room1"), "mq room 2", "mq step 10", cap("room2")]));
  add("scr_shop_counter", NEW.concat(["mq go map", "mq fun 36000", "mq coins 300", "mq pay setup 0 0", "mq pay open Heart_chair",
    "mq step 6", cap("counter"), "mq pay put d", "mq pay put p", "mq step 4", cap("counter_coins"), "mq pay go", "mq step 4",
    cap("counter_thanks"), "mq pay done", "mq pay open Star_lamp", "mq pay put p", "mq pay go", "mq pay help", "mq step 4",
    cap("counter_help"), "mq pay back", "mq pay back", "mq pay setup 2 0 0", "mq pay open Bookshelf", "mq pay put 1", "mq pay go",
    "key 7", "key 5", "mq step 4", cap("counter_change"), "mq pay back", "mq coins 300", "mq pay setup 3 5 -1 1",
    "mq pay open Little_desk", "mq step 4", cap("counter_picky"), "mq pay put n", "mq pay put n", "mq pay put n", "mq pay put p",
    "mq pay go", "mq step 4", cap("counter_swap")]));
  /* dress-up's rack and the home story (mq_homestory.c): the rack, a piece tried on, its counter in dress-up, the
   * buddy's thought, the friend's card (dress-up and the room), the buddy at the window, the walk-out, the thank-you */
  const SIX = [].concat(...Array(6).fill(["mq correct", "mq step 40"]));
  add("scr_homestory", NEW.concat(["mq engine stop", "mq go map", "mq coins 300", "mq go wardrobe", "mq step 6", cap("rack"),
    "touch tap 154 79", "mq step 4", cap("tryon"), "mq pay setup 1 0", "touch tap 174 139", "mq step 6", cap("rack_counter"),
    "mq pay back", "mq pay back", "key CLEAR", "mq story time 370", "mq step 30", cap("thought", { anim: ANIM }),
    "mq story time 400", "mq step 50", cap("story_card"), "mq story later", "mq go room", "mq story time 10",
    "mq story time 420", "mq step 120", cap("story_room_card"), "mq story later", "mq step 60", cap("story_window", { anim: ANIM }),
    "mq story time 470", "mq step 20", cap("story_walkout"), "mq step 70", "mq step 60"], SIX, ["mq step 30", cap("story_thanks")]));
  /* the ways to work it (mq_strat.c): every model, worked part way, and a teaching segment */
  const WAYS = [["jumps", "0 bulb 38 25", ["10"]], ["places", "34 bulb 2 12 5", ["2", "17", "32"]], ["give", "4 bulb 299 45", ["1"]],
    ["groups", "7 bulb 4 6", ["0", "1"]], ["array_split", "16 bulb 7 6 5", ["5", "35"]], ["array_turn", "14 bulb 2 4 1", ["45"]],
    ["area", "9 bulb 23 4", ["20", "12"]], ["peel", "22 bulb 96 4", ["0"]], ["friendly", "18 bulb 25 7 4", ["2"]],
    ["tape", "25 bulb 18 7", ["0"]], ["strip", "27 bulb 3 4", ["4", "0"]], ["pie", "28 bulb 2 6 0", ["6", "1"]],
    ["money", "35 bulb 247", ["2", "17"]], ["plot", "37 bulb", ["1", "2"]], ["timeline", "36 bulb 165 85", ["10", "5"]],
    ["areadiv", "23 bulb 96 4", ["20"]], ["fline", "29 bulb 3 4", ["1"]]];
  const wl = NEW.concat(["mq scene off", "mq school 4 40", "mq engine stop", "mq notebook 2"]);
  for (const [nm, open, acts] of WAYS)
    wl.push("mq skill 16 0", "mq strat " + open, "mq step 4", "mq strat try", ...acts.map(a => "mq strat act " + a), "mq step 6",
      cap("way_" + nm, { anim: ANIM }), "mq strat back");
  wl.push("mq skill 16 0", "mq strat 16 show", "mq step 10", "mq strat next", "mq strat next", "mq step 20", cap("way_teach", { anim: ANIM }), "mq strat back");
  add("scr_ways", wl);
  add("scr_merge", NEW.concat(["mq grant all", "mq merge energy 40", "mq go merge", "mq step 10", cap("merge"),
    "mq merge gen 0", "mq merge gen 0", "mq merge gen 1", "mq merge gen 2", "mq step 10", cap("merge_gen"),
    "mq merge put 0 18", "mq merge put 1 18", "mq merge put 7 20", "mq merge put 8 33", "mq step 10", cap("merge_full", { anim: ANIM })]));
  const toys = ["mq go arcade", "mq step 4", "touch tap 160 202", "mq step 4", cap("menu")];
  const pos = [[56, 70], [160, 70], [264, 70], [56, 128], [160, 128], [264, 128], [56, 186], [160, 186], [264, 186]];
  pos.forEach((p, k) => toys.push(`touch tap ${p[0]} ${p[1]}`, "mq step 40", cap("toy" + (k + 1), { anim: ANIM }), "key CLEAR", "mq step 4"));
  add("scr_toys", NEW.concat(toys));
  const fun = NEW.concat(["mq grant all", "mq fun 36000", "mq go map"]);
  ["bubbles", "catch", "dance", "story", "squishy", "dress", "place", "maker", "jam"].forEach(b =>
    fun.push(`mq fun beat ${b}`, "mq step 30", cap("beat_" + b, { anim: ANIM }), "mq step 400", "mq go map", "mq step 4"));
  add("scr_fun", fun);
  const act = NEW.concat(["mq grant all"]);
  [0, 1, 2].forEach(a => act.push(`mq act ${a}`, "mq step 30", cap("act" + a, { anim: ANIM }), "mq step 200", cap("act" + a + "_late"), "mq go map"));
  add("scr_action", act);
  const fg = NEW.concat(["mq grant all"]);
  [0, 1, 2, 3].forEach(g => fg.push("mq go arcade", "mq step 4", `mq game ${g}`, "mq step 30", cap("factgame" + g, { anim: ANIM }), "key CLEAR", "mq step 4"));
  add("scr_factgames", fg);
  const help = NEW.concat(["mq engine stop", "mq skill 24 0", "mq step 80", "mq help", "mq step 10", cap("helper", { anim: ANIM }),
    "mq next", "mq step 10", cap("helper_next"), "mq bulb", "mq step 10", cap("bulb"),
    "mq skill 10 1", "mq step 80", "mq notebook 0", "mq step 4", cap("notebook0"), "mq notebook 2", "mq step 4", cap("notebook2"),
    "mq teach 1", "mq step 10", cap("teach1"), "mq step 60", cap("teach1_late"), "mq teach 3", "mq step 30", cap("teach3"),
    "mq teach 5", "mq step 30", cap("teach5"), "mq teach 8", "mq step 30", cap("teach8")]);
  add("scr_helper", help);
  return J;
}

/* occlusion timelines: real frames (sim.frame: toasts, coins in flight, curtains -- `mq step`
 * hides them), sampled over 8 s after entering each screen and after an action */
const TL = [0, 6, 12, 25, 50, 100, 200];
function occlJobs() {
  const J = [];
  const add = (name, lines) => J.push({ name: name, lines: lines });
  add("occl_shop", NEW.concat(["mq fun 36000", "mq coins 300", "mq go map", "key F1", "mq step 4", "key F1",
    cap("enter", { timeline: TL }), "touch tap 60 100", cap("tap_item", { timeline: TL }), "touch tap 160 200", cap("tap_buy", { timeline: TL })]));
  add("occl_room", NEW.concat(["mq go map", "key F1", "mq step 4"], ROOM, [cap("enter", { timeline: TL }),
    "mq sel 1", cap("select", { timeline: TL }), "touch tap 160 120", cap("tap", { timeline: TL })]));
  add("occl_counter", NEW.concat(["mq fun 36000", "mq coins 300", "mq pay setup 0 0", "mq pay open Heart_chair",
    cap("enter", { timeline: TL }), "mq pay put d", "mq pay go", cap("miss", { timeline: TL }), "mq pay put p", "mq pay put p",
    "mq pay go", cap("paid", { timeline: TL })]));
  add("occl_book", NEW.concat(["mq coins 900", "mq go book", cap("enter", { timeline: TL }), "key RIGHT", cap("page", { timeline: TL })]));
  add("occl_merge", NEW.concat(["mq grant all", "mq merge energy 40", "mq go merge", cap("enter", { timeline: TL }),
    "touch tap 40 200", cap("tap_gen", { timeline: TL }), "mq merge put 0 18", "mq merge put 1 18", "mq merge move 0 1", cap("merged", { timeline: TL })]));
  add("occl_town", NEW.concat(["mq grant all", "mq hearts 0 3", "mq go map", cap("enter", { timeline: TL }), "key RIGHT", cap("walk", { timeline: TL })]));
  add("occl_toys", NEW.concat(["mq go arcade", cap("arcade", { timeline: TL }), "touch tap 160 202", cap("toybox", { timeline: TL }),
    "touch tap 56 70", cap("bongo", { timeline: TL })]));
  add("occl_game", NEW.concat(["mq engine stop", "mq level 0 0", cap("start", { timeline: TL }), "mq correct", cap("cheer", { timeline: TL }),
    "mq next", "mq step 40", "mq wrong", cap("miss", { timeline: TL })]));
  add("occl_till", NEW.concat(["mq engine stop", "mq scene off", "mq skill 45 1", "mq mech \"Shop Till\"", cap("start", { timeline: TL }),
    "mq correct", cap("cheer", { timeline: TL })]));
  /* unlock toasts ("New! ...") over the screen she is on; a purchase; a twist's banner */
  const TL2 = [0, 6, 12, 25, 50, 75, 100, 150, 200, 300];
  ["shop", "book", "room", "wardrobe", "map", "arcade"].forEach(sc =>
    add("occl_toast_" + sc, NEW.concat(["mq fun 36000", "mq coins 300", "mq grant stars 12", `mq go ${sc}`, cap("enter", { timeline: TL2 })])));
  add("occl_buy", NEW.concat(["mq fun 36000", "mq coins 300", "mq go shop", "mq step 400", "touch tap 60 100", "mq step 4",
    "touch tap 188 153", cap("bought", { timeline: TL2 })]));
  add("occl_twist", NEW.concat(["mq engine stop", "mq scene off", "mq level 0 0", "mq step 4", "mq twist 1", cap("night", { timeline: TL2 }),
    "mq twist 4", cap("mirror", { timeline: TL2 })]));
  add("occl_parent", NEW.concat(["mq grant all", "mq go parent", cap("enter", { timeline: TL }), "key DOWN", "key DOWN", cap("down", { timeline: TL })]));
  add("occl_earn", NEW.concat(["mq engine stop", "mq fun 36000", "mq level 0 0", "mq step 40", "mq correct", "mq step 2", "mq next", "mq correct",
    "mq next", "mq correct", "mq next", "mq correct", "mq next", "mq correct", cap("streak", { timeline: TL }), "mq go map", cap("map_after", { timeline: TL })]));
  return J;
}

/* ---------------------------------------------------------------- Chrome */
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

async function openPage(c, w, h, dpr, live) {
  const { targetId } = await c.send("Target.createTarget", { url: "about:blank" });
  const { sessionId } = await c.send("Target.attachToTarget", { targetId, flatten: true });
  const s = (m, p) => c.send(m, p, sessionId);
  await s("Emulation.setDeviceMetricsOverride", { width: w, height: h, deviceScaleFactor: dpr, mobile: false, screenWidth: w, screenHeight: h });
  await s("Page.enable");
  await s("Runtime.enable");
  await s("Page.addScriptToEvaluateOnNewDocument", { source:
    "try { localStorage.clear(); } catch (e) {}\n" +
    (live ? "" : "window.__raHold = true; window.requestAnimationFrame = function (f) { window.__raf = f; return 1; };") });
  await s("Page.navigate", { url: "file://" + PAGE });
  const ev = async (expr) => {
    const r = await s("Runtime.evaluate", { expression: expr, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) throw new Error(expr.slice(0, 80) + ": " + JSON.stringify(r.exceptionDetails).slice(0, 600));
    return r.result.value;
  };
  let ok = false;
  for (let i = 0; i < 400 && !ok; i++) {
    await sleep(100);
    ok = await ev("!!(window.Sparkle && Sparkle.ready && (!Sparkle.vec || Sparkle.vec.pending === 0) && Sparkle.renderer.arts.every(a => a.img))").catch(() => false);
  }
  if (!ok) throw new Error("the page did not get ready");
  await ev("document.getElementById('help').hidden = true; 1");
  return { ev, close: () => c.send("Target.closeTarget", { targetId }), s };
}

function writePNG(file, dataUrl) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, Buffer.from(dataUrl.split(",")[1], "base64"));
}

/* ---------------------------------------------------------------- running a job */
let resultsFd = null;
async function runJob(c, job) {
  const pg = await openPage(c, WIN_W, WIN_H, 1);
  await pg.ev(INPAGE + "\n1");
  const con = async (l) => pg.ev(`(function(){ var o = Sparkle.sim.console(${JSON.stringify(l)}); RA.take(); return o; })()`);
  const simLines = [];
  let ncap = 0;
  if (job.dynamic === "mech") { /* find skills the game fits */
    await con("mq fun off"); await con("mq new Mia 2"); await con("mq engine stop"); await con("mq grant bosses"); await con("mq opengames"); await con("mq scene off");
    const fit = [];
    for (let s = 0; s < 86; s++) {
      await con(`mq skill ${s} 1`);
      const o = await con(`mq mech "${job.mech}"`);
      if (!/does not fit|error|no game/i.test(o)) fit.push(s);
    }
    fs.appendFileSync(path.join(OUT, "fits.txt"), job.mech + ": " + fit.join(" ") + "\n");
    const pick = fit.length <= 4 ? fit : [fit[0], fit[Math.floor(fit.length / 3)], fit[Math.floor(2 * fit.length / 3)], fit[fit.length - 1]];
    job.lines = ["mq fun off", "mq new Mia 2", "mq engine stop", "mq grant bosses", "mq opengames", "mq scene off"];
    for (const s of pick) job.lines.push("mq seed 3", `mq skill ${s} 1`, `mq mech "${job.mech}"`, "mq step 40", cap("s" + s, { anim: ANIM, order: true }));
    job.sim = true;
  }
  for (const l of job.lines) {
    if (typeof l === "string") { await con(l); simLines.push(l); continue; }
    const pts = l.timeline || [null];
    let done = 0;
    for (const f of pts) {
      if (f !== null && f > done) { await pg.ev(`(function(){ for (var i = 0; i < ${f - done}; i++) Sparkle.sim.frame(40); RA.take(); return 1; })()`); done = f; }
      const capName = l.cap + (f !== null ? `_f${String(f).padStart(3, "0")}` : "");
      const opts = { noB: f !== null && f > 0, occlMax: 14 };
      const r = await pg.ev(`JSON.stringify(RA.capture(${JSON.stringify(opts)}))`).then(JSON.parse);
      r.job = job.name, r.capture = capName, r.label = LABEL, r.t_ms = f !== null ? f * 40 : null;
      if (l.order || /^ui_|^game_|^mech_|^twist_/.test(job.name)) {
        const st = {};
        for (const k of ["mq_scene", "mq_skill", "mq_q", "mq_answer", "mq_mech", "mq_diff", "mq_mod", "mq_moment", "mq_theme"])
          st[k] = (await pg.ev(`Sparkle.sim.console("state ${k}")`)).split("\n")[0].replace(/^[^=]*=/, "");
        r.state = st;
      }
      const sev = r.findings.map(x => x.sev);
      const png = await pg.ev(`RA.composite(2, ${JSON.stringify(r.findings)})`);
      const shot = path.join(OUT, "shots", job.name, capName + ".png");
      writePNG(shot, png);
      r.shot = path.relative(OUT, shot);
      if (f === null || f === 0) {
        const fb = await pg.ev(`(function(){ var c = document.createElement('canvas'); c.width = 320; c.height = 240; var x = c.getContext('2d'), im = x.createImageData(320, 240), fb = Sparkle.sim.framebuffer565();
          for (var i = 0; i < 76800; i++) { var v = fb[i], r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31; im.data[i*4] = (r << 3) | (r >> 2); im.data[i*4+1] = (g << 2) | (g >> 4); im.data[i*4+2] = (b << 3) | (b >> 2); im.data[i*4+3] = 255; }
          x.putImageData(im, 0, 0); return c.toDataURL('image/png'); })()`);
        writePNG(path.join(OUT, "fb", job.name + "__" + capName + ".png"), fb);
      }
      if (l.anim) {
        r.anim = await pg.ev(`JSON.stringify(RA.animCheck(${JSON.stringify(l.anim)}))`).then(JSON.parse);
      }
      fs.writeSync(resultsFd, JSON.stringify(r) + "\n");
      ncap++;
      if (f === null || f === 0) simLines.push({ shot: job.name + "__" + capName + ".png" });
      if (l.anim) simLines.push(...l.anim);
      if (sev.includes("high")) process.stdout.write("!");
    }
  }
  if (SIMSCRIPTS && job.sim) {
    fs.mkdirSync(SIMSCRIPTS, { recursive: true });
    const head = ["requirecfg CONTENT_QSPI", "fs insert 64", "fs format", "module library extra", "module install /CALC/MODULES/MATHQ010.MOD", "key CLEAR", "app mathquest"];
    const body = simLines.map(l => typeof l === "string" ? l : "screenshot " + l.shot);
    fs.writeFileSync(path.join(SIMSCRIPTS, job.name + ".txt"), head.concat(body).join("\n") + "\n");
  }
  await pg.close();
  return ncap;
}

/* the page's own overlays at window sizes a Mac has */
async function overlayJob(c) {
  const sizes = [[1470, 956, 2], [1512, 982, 2], [1440, 900, 2], [1280, 800, 2], [1024, 768, 2], [800, 600, 2], [735, 956, 2], [1728, 1117, 2]];
  const out = [];
  for (const [w, h, d] of sizes) {
    const pg = await openPage(c, w, h, d, true); /* the page's own loop runs: the window as she sees it */
    for (const l of NEW.concat(["mq fun 36000", "mq coins 300", "mq grant stars 12", "mq go shop", "mq step 10"])) await pg.ev(`Sparkle.sim.console(${JSON.stringify(l)})`);
    await sleep(700);
    const r = await pg.ev(`(function(){ var o = {}; ["menubtn","menu","help"].forEach(function(id){ var e = document.getElementById(id), b = e.getBoundingClientRect(), L = Sparkle.L, d = L.dpr;
      o[id] = { hidden: e.hidden, css: [b.left, b.top, b.right, b.bottom].map(Math.round), logical: [(b.left*d-L.play.x)/L.z, (b.top*d-L.play.y)/L.z, (b.right*d-L.play.x)/L.z, (b.bottom*d-L.play.y)/L.z].map(function(v){return +v.toFixed(1);}) }; });
      o.play = Sparkle.L.play; o.z = Sparkle.L.z; o.S = Sparkle.S; return JSON.stringify(o); })()`).then(JSON.parse);
    const mb = r.menubtn.logical;
    r.menuOverPlay = mb[2] > 0 && mb[3] > 0 && mb[0] < 320 && mb[1] < 240;
    r.size = [w, h, d];
    if (r.menuOverPlay || w === 1470) {
      const shot = await pg.s("Page.captureScreenshot", { format: "png" });
      const f = path.join(OUT, "shots", "overlays", `window_${w}x${h}.png`);
      fs.mkdirSync(path.dirname(f), { recursive: true });
      fs.writeFileSync(f, Buffer.from(shot.data, "base64"));
      r.shot = path.relative(OUT, f);
    }
    out.push(r);
    await pg.close();
  }
  /* theme / art id collisions */
  const pg = await openPage(c, WIN_W, WIN_H, 1);
  await pg.ev(INPAGE + "\n1");
  const col = await pg.ev("JSON.stringify(RA.idCollisions())").then(JSON.parse);
  const cov = await pg.ev("JSON.stringify({vec: Object.keys(Sparkle.vec.items).length, rigs: Object.keys(SparkleVector.rigs).length, theme: Sparkle.sim.console('state mq_theme').trim()})").then(JSON.parse);
  await pg.close();
  fs.writeFileSync(path.join(OUT, "overlays.json"), JSON.stringify({ windows: out, collisions: col, coverage: cov }, null, 1));
}

async function main() {
  fs.mkdirSync(OUT, { recursive: true });
  /* one browser for the whole run, a page per scene (tools/headless_chrome.js: its own process group and
   * profile, killed and deleted on any exit, a run timeout, at most two at once) */
  const browser = await HC.launch({ chrome: CHROME, label: "render-audit " + LABEL });
  const c = await cdp(browser.wsUrl);
  resultsFd = fs.openSync(path.join(OUT, "results.jsonl"), args.append ? "a" : "w");
  let jobs = [];
  /* rung counts: asked of the page */
  let rungs = {};
  if (JOBS.includes("games")) {
    const pg = await openPage(c, WIN_W, WIN_H, 1);
    await pg.ev('Sparkle.sim.console("mq new Mia 2")');
    for (let s = 0; s < 86; s++) {
      const o = await pg.ev(`Sparkle.sim.console("mq rung ${s}")`);
      rungs[s] = (o.match(/^[ *] \d+: /mg) || []).length || 4;
    }
    await pg.close();
    fs.writeFileSync(path.join(OUT, "rungs.json"), JSON.stringify(rungs));
  }
  for (const j of JOBS) {
    if (j === "ui") jobs = jobs.concat(uiJobs());
    if (j === "games") jobs = jobs.concat(gameJobs(rungs));
    if (j === "mechs") jobs = jobs.concat(mechJobs());
    if (j === "twists") jobs = jobs.concat(twistJobs());
    if (j === "town") jobs = jobs.concat(townJobs());
    if (j === "events") jobs = jobs.concat(eventJobs());
    if (j === "screens") jobs = jobs.concat(screenJobs());
    if (j === "occl") jobs = jobs.concat(occlJobs());
  }
  if (args.only) jobs = jobs.filter(j => args.only.split(",").some(o => j.name.includes(o)));
  const t0 = Date.now();
  let total = 0;
  for (const job of jobs) {
    try {
      const n = await runJob(c, job);
      total += n;
      console.log(`${job.name}: ${n} captures (${((Date.now() - t0) / 1000).toFixed(0)} s)`);
    } catch (e) {
      console.log(`${job.name}: FAILED ${e.message.slice(0, 300)}`);
      fs.writeSync(resultsFd, JSON.stringify({ job: job.name, label: LABEL, error: e.message.slice(0, 500) }) + "\n");
    }
  }
  if (JOBS.includes("overlays")) await overlayJob(c).catch(e => console.log("overlays: FAILED " + e.message));
  fs.closeSync(resultsFd);
  c.close();
  await browser.close();
  console.log(`${LABEL}: ${total} captures -> ${OUT}`);
}
main().catch(e => { console.error(e); process.exit(1); });
