/* Page logic for calculator.html: hot-spots, keyboard shortcuts, LCD canvas,
 * and the requestAnimationFrame loop that drives the WebAssembly calculator.
 * Globals expected: createCalcModule (Emscripten), CalcSim (calc_api.js),
 * CALC_KEYMAP (keymap.json from web/tools/render_calc.py: picture, display and key
 * rectangles), CALC_KEYS (keys/layout.py: legends and keyboard shortcuts). */
(function () {
  "use strict";
  var map = window.CALC_KEYMAP;
  var IW = map.image.width, IH = map.image.height;
  var calcEl = document.getElementById("calc");
  var stage = document.getElementById("stage");
  var lcd = document.getElementById("lcd");
  var statusEl = document.getElementById("status");
  var ctx = lcd.getContext("2d");
  var sim = null;
  var spots = {};          // key name (keys/layout.py) -> hot-spot element
  var downCount = {};      // key name -> number of active presses (pointers + keyboard)
  var MIN_PRESS_MS = 90;   // keep the pressed look visible for quick taps

  function pct(v, total) { return (100 * v / total) + "%"; }
  function place(el, r) {
    el.style.left = pct(r[0], IW);
    el.style.top = pct(r[1], IH);
    el.style.width = pct(r[2], IW);
    el.style.height = pct(r[3], IH);
  }

  /* ---------------------------------------------------------------- layout */
  function fit() {
    var cs = getComputedStyle(stage);
    var aw = stage.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight);
    var ah = stage.clientHeight - parseFloat(cs.paddingTop) - parseFloat(cs.paddingBottom);
    var h = Math.max(100, Math.min(ah, aw * IH / IW));
    calcEl.style.height = h + "px";
    calcEl.style.width = (h * IW / IH) + "px";
  }
  place(lcd, map.display.rect);
  window.addEventListener("resize", fit);
  fit();

  /* ------------------------------------------------------------------ keys */
  function setStatus(text, isError) {
    statusEl.textContent = text;
    statusEl.className = isError ? "error" : "";
  }

  function press(name) {
    downCount[name] = (downCount[name] || 0) + 1;
    var el = spots[name];
    if (el) {
      el.classList.add("pressed");
      el._since = Date.now();
    }
    if (downCount[name] === 1 && sim) sim.key(name, true);
  }

  function release(name) {
    if (!downCount[name]) return;
    downCount[name]--;
    if (downCount[name] > 0) return;
    if (sim) sim.key(name, false);
    var el = spots[name];
    if (el) {
      var left = MIN_PRESS_MS - (Date.now() - (el._since || 0));
      if (left > 0) setTimeout(function () { if (!downCount[name]) el.classList.remove("pressed"); }, left);
      else el.classList.remove("pressed");
    }
  }

  map.keys.forEach(function (k) {
    var b = document.createElement("button");
    b.type = "button";
    b.className = "key";
    b.tabIndex = -1;
    b.dataset.key = k.name;
    var label = k.legend + (k.shift ? " (shift: " + k.shift + ")" : "") + (k.alpha ? " (alpha: " + k.alpha + ")" : "");
    b.setAttribute("aria-label", label);
    b.title = label;
    place(b, k.rect);
    var pointers = {};
    b.addEventListener("pointerdown", function (e) {
      if (e.button !== undefined && e.button !== 0) return;
      e.preventDefault();
      if (b.setPointerCapture) { try { b.setPointerCapture(e.pointerId); } catch (err) { /* ignore */ } }
      if (pointers[e.pointerId]) return;
      pointers[e.pointerId] = true;
      press(k.name);
    });
    function up(e) {
      if (!pointers[e.pointerId]) return;
      delete pointers[e.pointerId];
      release(k.name);
    }
    b.addEventListener("pointerup", up);
    b.addEventListener("pointercancel", up);
    b.addEventListener("lostpointercapture", up);
    b.addEventListener("contextmenu", function (e) { e.preventDefault(); });
    calcEl.appendChild(b);
    spots[k.name] = b;
  });

  /* ----------------------------------------------- display touch (forwarded) */
  /* Every pointer on the LCD becomes a finger (id 0..4) for the calculator's
   * gesture recognizer: tap, drag, and two-finger pinch on touch screens. The
   * mouse wheel over the LCD makes a synthetic pinch (zoom on the graph). */
  var fingers = {}; /* pointerId -> finger id */
  function lcdXY(e) {
    var r = lcd.getBoundingClientRect();
    var x = Math.floor((e.clientX - r.left) * 320 / r.width);
    var y = Math.floor((e.clientY - r.top) * 240 / r.height);
    return [Math.max(0, Math.min(319, x)), Math.max(0, Math.min(239, y))];
  }
  function freeFinger() {
    var used = {};
    for (var k in fingers) used[fingers[k]] = true;
    for (var i = 0; i < 5; i++) if (!used[i]) return i;
    return -1;
  }
  function touchAt(e, state) {
    var id = fingers[e.pointerId];
    if (id === undefined || !sim) return;
    var p = lcdXY(e);
    if (id === 0) sim.touch(p[0], p[1], state);
    else sim.touchId(id, p[0], p[1], state);
  }
  lcd.addEventListener("pointerdown", function (e) {
    if (e.button !== undefined && e.button !== 0) return;
    var id = freeFinger();
    if (id < 0) return;
    e.preventDefault();
    fingers[e.pointerId] = id;
    if (lcd.setPointerCapture) { try { lcd.setPointerCapture(e.pointerId); } catch (err) { /* ignore */ } }
    touchAt(e, 1);
  });
  lcd.addEventListener("pointermove", function (e) { touchAt(e, 2); });
  function touchUp(e) {
    if (fingers[e.pointerId] === undefined) return;
    touchAt(e, 0);
    delete fingers[e.pointerId];
  }
  lcd.addEventListener("pointerup", touchUp);
  lcd.addEventListener("pointercancel", touchUp);
  lcd.addEventListener("wheel", function (e) {
    if (!sim || Object.keys(fingers).length) return;
    e.preventDefault();
    var p = lcdXY(e), d0 = 60, d1 = e.deltaY < 0 ? 75 : 48;
    sim.touchId(3, p[0] - d0 / 2, p[1], 1); sim.touchId(4, p[0] + d0 / 2, p[1], 1);
    sim.touchId(3, p[0] - d1 / 2, p[1], 2); sim.touchId(4, p[0] + d1 / 2, p[1], 2);
    sim.touchId(4, p[0] + d1 / 2, p[1], 0); sim.touchId(3, p[0] - d1 / 2, p[1], 0);
  }, { passive: false });

  /* -------------------------------------------------------------- keyboard */
  // keyboard shortcuts from keys/layout.py ("kbd": KeyboardEvent.key values)
  var KBD = {};
  (window.CALC_KEYS || []).forEach(function (k) {
    (k.kbd || []).forEach(function (c) { KBD[c] = k.name; });
  });
  var heldByCode = {}; // KeyboardEvent.code -> calculator key (so keyup releases what keydown pressed)

  window.addEventListener("keydown", function (e) {
    if (e.ctrlKey || e.metaKey || e.altKey) return; // leave browser shortcuts alone
    var name = KBD[e.key];
    if (!name) return;
    e.preventDefault();
    var code = e.code || e.key;
    if (heldByCode[code]) return; // OS auto-repeat: the calculator does its own repeat
    heldByCode[code] = name;
    press(name);
  });
  window.addEventListener("keyup", function (e) {
    var code = e.code || e.key;
    var name = heldByCode[code];
    if (!name) return;
    e.preventDefault();
    delete heldByCode[code];
    release(name);
  });
  function releaseAll() {
    Object.keys(heldByCode).forEach(function (c) { var n = heldByCode[c]; delete heldByCode[c]; release(n); });
  }
  window.addEventListener("blur", releaseAll);

  /* ------------------------------------------------------------ LCD + loop */
  var image = ctx.createImageData(320, 240);
  var lastSerial = -1, lastBacklight = -1, lastTs = null;

  function draw() {
    var s = sim.serial();
    if (s !== lastSerial) {
      lastSerial = s;
      image.data.set(sim.lcdRGBA());
      ctx.putImageData(image, 0, 0);
    }
    var bl = sim.displayOn() ? sim.backlight() : 0;
    if (bl !== lastBacklight) {
      lastBacklight = bl;
      // 60 % (the default) shows the pixels as-is; dimmer/brighter around that.
      lcd.style.filter = "brightness(" + Math.min(1.25, 0.25 + 0.75 * bl / 60).toFixed(3) + ")";
    }
  }

  function loop(ts) {
    var dt = lastTs === null ? 16 : ts - lastTs;
    lastTs = ts;
    try {
      sim.frame(dt);
      draw();
    } catch (err) {
      setStatus("Simulator stopped: " + err, true);
      throw err;
    }
    window.requestAnimationFrame(loop);
  }

  /* ------------------------------------------------------------- startup
   * A page can start in a prepared state: window.CALC_CONFIG = {allModules, card, modules, files,
   * app, theme, persistKey} (see CalcSim.runPreset), ?preset=mathquest (a card, Math Quest
   * installed and open) or ?preset=all (every module bundled into the emulator installed). The
   * "Install all modules" button does the same as ?preset=all and remembers it: later visits
   * install them again at startup (the store itself is not kept). With persistKey, what a power
   * cycle keeps (the session: settings, saved progress) is kept in this browser's localStorage
   * and comes back on the next visit -- a convenience for this viewer only; it silently does
   * nothing where storage is blocked. */
  var ALL_FLAG = "calc_all_modules", ALL_KEY = "calc_preset_all";
  var store = null;
  try { store = window.localStorage || null; } catch (e) { store = null; }
  function storeGet(k) { try { return store ? store.getItem(k) : null; } catch (e) { return null; } }
  function storeSet(k, v) {
    try { if (store) { if (v === null) store.removeItem(k); else store.setItem(k, v); } } catch (e) { /* full or blocked */ }
  }
  var keeping = null;
  function keepSession(s, key) {
    if (!store || keeping) return;
    keeping = key;
    var keep = function () { storeSet(key, CalcSim.toBase64(s.sessionGet())); };
    if (typeof setInterval === "function") setInterval(keep, 15000);
    if (window.addEventListener) window.addEventListener("pagehide", keep);
    if (typeof document !== "undefined" && document.addEventListener)
      document.addEventListener("visibilitychange", function () { if (document.hidden) keep(); });
  }

  var allBtn = document.getElementById("allmods");
  function allLabel(on) {
    if (!allBtn) return;
    allBtn.textContent = on ? "All modules installed" : "Install all modules";
    allBtn.title = on ? "Installed again on every visit to this page. Click to stop that."
                      : "Install every module bundled into the emulator (Memory › Modules)";
    allBtn.setAttribute("aria-pressed", on ? "true" : "false");
  }

  function startup(s) {
    var cfg = window.CALC_CONFIG || null;
    var m = /[?&]preset=([a-z]+)/.exec((window.location && window.location.search) || "");
    if (!cfg && m && m[1] === "mathquest")
      cfg = { card: 64, modules: ["MATHQ"], app: "mathquest", persistKey: "calc_preset_mathquest" };
    if (!cfg && ((m && m[1] === "all") || storeGet(ALL_FLAG) === "1"))
      cfg = { allModules: true, persistKey: ALL_KEY };
    allLabel(!!(cfg && cfg.allModules));
    if (!cfg) return;
    if (cfg.persistKey) { var kept = storeGet(cfg.persistKey); if (kept) cfg.session = kept; }
    try { s.runPreset(cfg); } catch (e) { console.warn("preset:", e); }
    if (cfg.persistKey) keepSession(s, cfg.persistKey);
  }

  if (allBtn) allBtn.addEventListener("click", function () {
    if (!sim) return;
    if (storeGet(ALL_FLAG) === "1") { /* stop installing them at startup */
      storeSet(ALL_FLAG, null);
      allLabel(false);
      return;
    }
    sim.installAllModules(); /* the result shows on the LCD */
    storeSet(ALL_FLAG, "1");
    keepSession(sim, ALL_KEY);
    allLabel(true);
  });

  setStatus("Loading…");
  CalcSim.create(createCalcModule).then(function (s) {
    sim = s;
    window.calc = s; // for the devtools console: calc.console("state"), calc.tap("ENTER"), ...
    map.keys.forEach(function (k) {
      if (!s.keyId(k.name)) console.warn("key without firmware mapping:", k.name);
    });
    startup(s);
    // keys pressed while loading
    Object.keys(downCount).forEach(function (n) { if (downCount[n]) s.key(n, true); });
    draw();
    setStatus("Firmware " + s.version());
    window.requestAnimationFrame(loop);
  }).catch(function (err) {
    setStatus("Could not start the simulator: " + err, true);
  });
})();
