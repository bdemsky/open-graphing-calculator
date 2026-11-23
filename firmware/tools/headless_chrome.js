/* Headless Google Chrome for the test harnesses (the render audit, the playtest bot's Mac driver, the Mac
 * screenshots): one browser per run, always cleaned up.
 *
 *   const HC = require(".../tools/headless_chrome.js");
 *   const b = await HC.launch({ chrome, args: [...extra flags], timeoutMs, label });
 *   ... b.port, b.wsUrl (the DevTools endpoint) ...
 *   await b.close();
 *
 * - The browser runs in its own process group (detached) with its own temporary profile directory
 *   (os.tmpdir()/calc-headless-*): close() kills the whole group (every helper process) and deletes the
 *   profile. It is also done on process exit, SIGINT, SIGTERM, SIGHUP, an uncaught exception or rejection.
 * - A run that takes longer than timeoutMs (default CALC_HEADLESS_TIMEOUT_MS, else 3 hours) kills its browser
 *   group and exits with status 124.
 * - At most CALC_HEADLESS_MAX (default 2) of these browsers run at once on the machine: a launch waits for a
 *   free slot (lock directories under os.tmpdir()/calc-headless-slots, freed by close or when their owner died).
 * `make kill-test-browsers` kills any left behind (only these: never a normal Chrome).
 */
"use strict";
const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawn } = require("child_process");

const CHROME_DEFAULT = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
const PREFIX = "calc-headless-";
const SLOTS = path.join(os.tmpdir(), "calc-headless-slots");
const MAX = Math.max(1, parseInt(process.env.CALC_HEADLESS_MAX || "2", 10) || 2);
const sleep = ms => new Promise(r => setTimeout(r, ms));
const live = new Set();
let handlersOn = false;

function alive(pid) {
  try { process.kill(pid, 0); return true; } catch (e) { return e.code === "EPERM"; }
}

function killGroup(pid) {
  if (!pid) return;
  try { process.kill(-pid, "SIGKILL"); } catch (e) { /* (gone) */ }
  try { process.kill(pid, "SIGKILL"); } catch (e) { /* (gone) */ }
}

function cleanupAll() {
  for (const b of Array.from(live)) b.closeSync();
}

function installHandlers() {
  if (handlersOn) return;
  handlersOn = true;
  process.on("exit", cleanupAll);
  for (const sig of ["SIGINT", "SIGTERM", "SIGHUP"])
    process.on(sig, () => { cleanupAll(); process.exit(sig === "SIGINT" ? 130 : 143); });
  process.on("uncaughtException", e => { console.error(e); cleanupAll(); process.exit(1); });
  process.on("unhandledRejection", e => { console.error(e); cleanupAll(); process.exit(1); });
}

/* a free slot of MAX (a lock directory holding its owner's pid); waits for one */
async function takeSlot(label) {
  fs.mkdirSync(SLOTS, { recursive: true });
  for (let waited = 0; ; waited += 500) {
    for (let i = 0; i < MAX; i++) {
      const d = path.join(SLOTS, "slot-" + i);
      try {
        fs.mkdirSync(d);
        fs.writeFileSync(path.join(d, "pid"), String(process.pid) + " " + (label || ""));
        return d;
      } catch (e) {
        if (e.code !== "EEXIST") throw e;
        let owner = 0;
        try { owner = parseInt(fs.readFileSync(path.join(d, "pid"), "utf8"), 10) || 0; } catch (e2) { /* (being written) */ }
        if (owner && !alive(owner)) /* its owner died: the slot is free again */
          try { fs.rmSync(d, { recursive: true, force: true }); } catch (e3) { /* (raced) */ }
      }
    }
    if (waited && waited % 30000 === 0)
      console.error(`(headless chrome: waiting for a free browser slot, ${MAX} in use)`);
    await sleep(500);
  }
}

async function launch(opts) {
  opts = opts || {};
  installHandlers();
  const slot = await takeSlot(opts.label);
  const prof = fs.mkdtempSync(path.join(os.tmpdir(), PREFIX));
  const port = opts.port || 9300 + Math.floor(Math.random() * 600);
  const args = ["--headless=new", "--remote-debugging-port=" + port, "--user-data-dir=" + prof, "--hide-scrollbars",
    "--allow-file-access-from-files", "--no-first-run", "--no-default-browser-check"].concat(opts.args || [], ["about:blank"]);
  const proc = spawn(opts.chrome || CHROME_DEFAULT, args, { stdio: "ignore", detached: true });
  proc.unref();
  const b = {
    proc, port, prof, wsUrl: null, closed: false,
    closeSync() {
      if (b.closed) return;
      b.closed = true;
      live.delete(b);
      killGroup(proc.pid);
      try { fs.rmSync(prof, { recursive: true, force: true }); } catch (e) { /* ignore */ }
      try { fs.rmSync(slot, { recursive: true, force: true }); } catch (e) { /* ignore */ }
      if (b.timer) clearTimeout(b.timer);
    },
    async close() {
      b.closeSync();
      for (let i = 0; i < 20 && alive(proc.pid); i++) await sleep(50);
    },
  };
  live.add(b);
  const ms = opts.timeoutMs || parseInt(process.env.CALC_HEADLESS_TIMEOUT_MS || "0", 10) || 3 * 3600 * 1000;
  b.timer = setTimeout(() => {
    console.error(`headless chrome: the run took over ${Math.round(ms / 1000)} s; the browser is killed`);
    cleanupAll();
    process.exit(124);
  }, ms);
  b.timer.unref();
  let ver = null;
  for (let i = 0; i < 150 && !ver; i++) {
    try { ver = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json(); } catch (e) { await sleep(100); }
  }
  if (!ver) {
    b.closeSync();
    throw new Error("chrome did not start");
  }
  b.wsUrl = ver.webSocketDebuggerUrl;
  return b;
}

module.exports = { launch, cleanupAll, PREFIX };
