/* Thin JS wrapper around the WebAssembly calculator (platform/web/web_api.h).
 * Works in the browser (defines window.CalcSim) and in node (module.exports).
 *
 *   const sim = await CalcSim.create(createCalcModule);
 *   sim.key("ENTER", true); sim.key("ENTER", false);   // or sim.tap("ENTER")
 *   sim.frame(16);                                      // advance 16 ms of virtual time
 *   const rgba = sim.lcdRGBA();                         // Uint8ClampedArray(320*240*4)
 *   sim.console("state last_output");                   // debug console, returns its output
 *   sim.writeFile("/CALC/THEMES/HOME.THM", bytes);       // a file onto the simulated microSD card
 *   var kept = sim.sessionGet(); ... sim.sessionPut(kept); // keep / restore what a power cycle keeps
 *   sim.runPreset({card: 64, modules: ["MATHQ"], files: {...}, app: "mathquest", theme: "HOME.THM",
 *                  console: ["mq school 3 10 default"]});   // (console: lines run last, every start)
 *   sim.installAllModules();                            // every module bundled into the emulator
 */
(function (root) {
  "use strict";
  var W = 320, H = 240;

  function CalcSim(M) {
    this.M = M;
    this.width = W;
    this.height = H;
    this._ids = {};
    M._web_init();
  }

  CalcSim.create = function (factory, opts) {
    return Promise.resolve(factory(opts || {})).then(function (M) { return new CalcSim(M); });
  };

  CalcSim.prototype._withString = function (s, fn) {
    var M = this.M, n = M.lengthBytesUTF8(s) + 1, p = M._malloc(n);
    try {
      M.stringToUTF8(s, p, n);
      return fn(p);
    } finally {
      M._free(p);
    }
  };

  /* Key id for a key name (keys/layout.py: "F1", "SHIFT", "7") or console alias ("bksp", "+"); 0 if unknown. */
  CalcSim.prototype.keyId = function (name) {
    if (typeof name === "number") return name;
    if (!(name in this._ids))
      this._ids[name] = this._withString(String(name), this.M._web_key_from_name.bind(this.M));
    return this._ids[name];
  };

  CalcSim.prototype.keyName = function (id) { return this.M.UTF8ToString(this.M._web_key_name(id)); };
  CalcSim.prototype.key = function (name, down) {
    var id = this.keyId(name);
    if (id) this.M._web_key(id, down ? 1 : 0);
    return id;
  };
  CalcSim.prototype.tap = function (name) { this.key(name, true); this.key(name, false); };
  CalcSim.prototype.frame = function (dtMs) { this.M._web_frame(Math.max(0, Math.round(dtMs)) >>> 0); };
  CalcSim.prototype.serial = function () { return this.M._web_lcd_serial() >>> 0; };
  CalcSim.prototype.backlight = function () { return this.M._web_backlight(); };
  CalcSim.prototype.displayOn = function () { return !!this.M._web_display_on(); };
  CalcSim.prototype.millis = function () { return this.M._web_millis() >>> 0; };
  CalcSim.prototype.version = function () { return this.M.UTF8ToString(this.M._web_version()); };

  /* View into WASM memory (re-fetched each call: memory growth replaces the buffer). */
  CalcSim.prototype.lcdRGBA = function () {
    var p = this.M._web_lcd_rgba();
    return new Uint8ClampedArray(this.M.HEAPU8.buffer, p, W * H * 4);
  };
  CalcSim.prototype.framebuffer565 = function () {
    return new Uint16Array(this.M.HEAPU8.buffer, this.M._web_framebuffer(), W * H);
  };
  CalcSim.prototype.panel565 = function () {
    return new Uint16Array(this.M.HEAPU8.buffer, this.M._web_panel(), W * H);
  };

  /* Run one console line to completion (native-simulator time semantics) and return its output. */
  CalcSim.prototype.console = function (line) {
    var M = this.M;
    this._withString(String(line), M._web_console_run.bind(M));
    return M.UTF8ToString(M._web_console_take());
  };
  /* Display touch in LCD pixels (0..319, 0..239); state 1 = down, 2 = move, 0 = up. */
  CalcSim.prototype.touch = function (x, y, state) { this.M._web_touch(x | 0, y | 0, state | 0); };
  /* Same for finger `id` (0..4): two fingers make a pinch. */
  CalcSim.prototype.touchId = function (id, x, y, state) {
    this.M._web_touch_id(id | 0, x | 0, y | 0, state | 0);
  };
  CalcSim.prototype.expectFailures = function () { return this.M._web_expect_failures(); };

  /* bytes from a Uint8Array, an Array or a base64 string */
  function toBytes(d) {
    if (d instanceof Uint8Array) return d;
    if (typeof d === "string") {
      var bin = typeof atob === "function" ? atob(d) : Buffer.from(d, "base64").toString("binary");
      var u = new Uint8Array(bin.length);
      for (var i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
      return u;
    }
    return new Uint8Array(d);
  }
  CalcSim.toBase64 = function (u) {
    if (typeof btoa !== "function") return Buffer.from(u).toString("base64");
    var s = "";
    for (var i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000));
    return btoa(s);
  };
  CalcSim.toBytes = toBytes;

  /* Write a file onto the simulated microSD card (inserted and formatted first: runPreset or
   * the console `fs insert 64`, `fs format`). Returns 0 or a negative FS error. */
  CalcSim.prototype.writeFile = function (path, data) {
    var M = this.M, u = toBytes(data), p = M._malloc(Math.max(1, u.length));
    try {
      M.HEAPU8.set(u, p);
      return this._withString(String(path), function (sp) { return M._web_write_file(sp, p, u.length); });
    } finally {
      M._free(p);
    }
  };

  /* The session as it is now (what a power cycle keeps), a copy. */
  CalcSim.prototype.sessionGet = function () {
    var M = this.M, lp = M._malloc(4);
    try {
      var p = M._web_session_get(lp), n = new Uint32Array(M.HEAPU8.buffer, lp, 1)[0];
      return new Uint8Array(M.HEAPU8.buffer, p, n).slice();
    } finally {
      M._free(lp);
    }
  };

  /* Restore a kept session (restarts the app with it, as after a power cycle). 0 or <0. */
  CalcSim.prototype.sessionPut = function (data) {
    var M = this.M, u = toBytes(data), lp = M._malloc(4);
    try {
      var p = M._web_session_buffer(lp), cap = new Uint32Array(M.HEAPU8.buffer, lp, 1)[0];
      if (!p || u.length > cap) return -1;
      M.HEAPU8.set(u, p);
      return M._web_session_put(u.length);
    } finally {
      M._free(lp);
    }
  };

  /* Install every module bundled into the emulator (Memory > Modules lists them; the newest
   * version of each, unless it is already the active one), through the same installer and
   * checks as a card file. The result shows on the LCD (a message box). Returns the console
   * output ("failed=N" and the message). */
  CalcSim.prototype.installAllModules = function () { return this.console("module bundled install"); };

  /* A startup preset (page option, tests): install every bundled module if cfg.allModules,
   * insert and format a card of cfg.card MB, put the
   * module library on it and install the modules whose file names start with cfg.modules[i]
   * ("MATHQ"), write cfg.files ({path: bytes or base64}), restore cfg.session if given, open
   * cfg.app, and choose cfg.theme in Math Quest (when no session was restored). */
  CalcSim.prototype.runPreset = function (cfg) {
    var self = this, log = [];
    function run(line) { var out = self.console(line); log.push(line + " -> " + out.trim()); return out; }
    cfg = cfg || {};
    if (cfg.allModules) {
      log.push("installAllModules -> " + self.installAllModules().trim().replace(/\n/g, " "));
      run("key CLEAR");
    }
    if (cfg.card) {
      run("fs insert " + (cfg.card | 0));
      run("fs format");
    }
    if (cfg.modules && cfg.modules.length) {
      run("module library extra");
      var names = run("fs ls /CALC/MODULES").split(/\s+/);
      cfg.modules.forEach(function (prefix) {
        names.filter(function (n) { return n.toUpperCase().indexOf(String(prefix).toUpperCase()) === 0 && /\.MOD$/i.test(n); })
          .forEach(function (n) { run("module install /CALC/MODULES/" + n); });
      });
    }
    Object.keys(cfg.files || {}).forEach(function (p) {
      var r = self.writeFile(p, cfg.files[p]);
      log.push("writeFile " + p + " -> " + r);
    });
    var restored = false;
    if (cfg.session) {
      restored = self.sessionPut(cfg.session) === 0;
      log.push("session -> " + (restored ? "restored" : "not a session"));
    }
    if (cfg.app) {
      run("key CLEAR");
      run("app " + cfg.app);
    }
    if (cfg.theme && !restored)
      run("mq theme " + cfg.theme);
    (cfg.console || []).forEach(function (line) { run(String(line)); }); /* a page's own presets, every start */
    return log;
  };

  if (typeof module !== "undefined" && module.exports) module.exports = CalcSim;
  else root.CalcSim = CalcSim;
})(typeof window !== "undefined" ? window : this);
