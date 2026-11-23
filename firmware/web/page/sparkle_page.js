/* Sparkle Town, standalone (web/dist/sparkle.html, `make sparkle-web`): the Math Quest module alone,
 * full window, at the screen's real resolution, with sound.
 *
 * Globals expected: createCalcModule (the -DWEB_SPARKLE build), CalcSim (calc_api.js), SparkleAudio
 * (sparkle_audio.js), SPARKLE_ART ({json, uri}: web/assets/sparkle_art.*), optionally
 * SPARKLE_CONFIG ({files, theme, persistKey; boot: console lines run after the start, noHelp}) and SPARKLE_EXTRA_ART ([{json, uri}]): what a
 * private build adds (more pictures, a theme pack chosen at the first start).
 *
 * Pieces (each exported on window.Sparkle for the tests, web/test/run_sparkle_test.js):
 *   layout    the canvas at devicePixelRatio, the 4:3 play area as big as fits, centred, the
 *             sides around it;
 *   Renderer  runs the frame's drawing commands (platform/web/web_sparkle.c) on the play area's
 *             canvas: rectangles, anti-aliased shapes, text in a rounded web font, pictures from
 *             the high-resolution atlas, and the core's own pixels;
 *   sides     the 16:10 (or any) rest of the window: the scene's edge colours carried outward
 *             into a soft pastel background with drifting sparkles; window.sparkleSides(ctx, info)
 *             may replace it (info: the rectangles, the scene the module last named);
 *   input     mouse / trackpad -> touch (drags too), Safari's pinch (gesture events) -> a pinch,
 *             the keyboard -> keys (KEYMAP), a cheat sheet on the first start;
 *   sound     window.calcSound -> SparkleAudio, unlocked by the first click or key;
 *   progress  what a power cycle keeps (the saved game) in localStorage, every 10 s and when the
 *             page is hidden or closed.
 */
(function (root) {
  "use strict";
  var W = 320, H = 240;
  var doc = root.document;
  var CFG = root.SPARKLE_CONFIG || {};
  var PERSIST = CFG.persistKey || "sparkle_town_progress";

  /* ---------------------------------------------------------------- layout */
  /* The display scale the module draws at: physical pixels per logical one, from the largest the
   * window can be on this screen (2..8). */
  function chooseScale(screenH, winH, dpr) {
    var h = Math.max(screenH || 0, winH || 0) * (dpr || 1);
    return Math.max(2, Math.min(8, Math.floor(h / H)));
  }
  /* cssW x cssH window at dpr -> the canvas (device px), the play area in it (device px, 4:3,
   * centred), its device pixels per logical pixel (z) and per physical pixel of scale S (k). */
  /* corner: the CSS px the corner menu button takes from the window's top-right corner (its margin and
   * size): the play area keeps out of it -- beside it in a wide window anyway, and in a 4:3 one (1024 x 768,
   * 800 x 600, a 4:3 display's full screen) a little smaller, moved left or down, whichever keeps it bigger
   * (the render audit's M10) */
  var MENU_CORNER = 14 + 46 + 6;
  function layout(cssW, cssH, dpr, S, corner) {
    dpr = dpr || 1;
    var dw = Math.max(1, Math.round(cssW * dpr)), dh = Math.max(1, Math.round(cssH * dpr));
    var z = Math.min(dw / W, dh / H); /* as big as the window allows (everything is drawn as shapes) */
    var pw = Math.round(W * z), ph = Math.round(H * z);
    var px = Math.floor((dw - pw) / 2), py = Math.floor((dh - ph) / 2);
    var g = Math.round((corner === undefined ? MENU_CORNER : corner) * dpr);
    if (g > 0 && px + pw > dw - g && py < g) { /* the corner button would sit on the game */
      var zl = Math.min((dw - g) / W, dh / H), zd = Math.min(dw / W, (dh - g) / H);
      z = Math.max(zl, zd), pw = Math.round(W * z), ph = Math.round(H * z);
      if (zl >= zd) px = Math.max(0, Math.floor((dw - g - pw) / 2)), py = Math.floor((dh - ph) / 2);
      else px = Math.floor((dw - pw) / 2), py = g + Math.max(0, Math.floor((dh - g - ph) / 2));
    }
    return { canvasW: dw, canvasH: dh, dpr: dpr, play: { x: px, y: py, w: pw, h: ph }, z: z, k: z / (S || 1), S: S || 1 };
  }
  /* client (CSS px) -> logical screen coordinates, or null outside the play area */
  function toLogical(L, cx, cy, clamp) {
    var x = (cx * L.dpr - L.play.x) / L.z, y = (cy * L.dpr - L.play.y) / L.z;
    if (!clamp && (x < 0 || y < 0 || x >= W || y >= H)) return null;
    return [Math.max(0, Math.min(W - 1, Math.floor(x))), Math.max(0, Math.min(H - 1, Math.floor(y)))];
  }

  /* ---------------------------------------------------------------- keys */
  /* KeyboardEvent.key -> calculator key (keys/layout.py names) */
  var KEYMAP = {
    ArrowUp: "UP", ArrowDown: "DOWN", ArrowLeft: "LEFT", ArrowRight: "RIGHT",
    Enter: "ENTER", Backspace: "DEL", Delete: "DEL", Escape: "CLEAR",
    "0": "0", "1": "1", "2": "2", "3": "3", "4": "4", "5": "5", "6": "6", "7": "7", "8": "8", "9": "9",
    ".": "DOT", ",": "DOT", "-": "SUB", "/": "DIV",
    F1: "F1", F2: "F2", F3: "F3", F4: "F4", F5: "F5"
  };

  /* ---------------------------------------------------------------- the renderer */
  var C_FILL = 1, C_LINE = 2, C_CLIP = 3, C_SHAPE = 4, C_TEXT = 5, C_IMAGE = 6, C_COPY = 7, C_SCENE = 8, C_SPRITE = 9,
    C_SOFTBAR = 10;
  var SH_CIRCLE = 1, SH_RING = 2, SH_RRECT = 3, SH_LINE = 4, SH_POLY = 5;
  var FONT = '"SF Pro Rounded", ui-rounded, "Arial Rounded MT Bold", "Nunito", system-ui, -apple-system, sans-serif';
  var STYLE_WEIGHT = [600, 700, 800, 800];
  var colCache = {};
  function css565(c) {
    var s = colCache[c];
    if (s) return s;
    var r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (colCache[c] = "rgb(" + ((r << 3) | (r >> 2)) + "," + ((g << 2) | (g >> 4)) + "," + ((b << 3) | (b >> 2)) + ")");
  }
  function utf8(bytes) {
    if (typeof TextDecoder !== "undefined") return new TextDecoder("utf-8").decode(bytes);
    var s = ""; for (var i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
    try { return decodeURIComponent(escape(s)); } catch (e) { return s; }
  }

  /* ctx: the play area's 2D context; art: {index: {hash: [x, y, w, h, artW, artH]}, image} list */
  function Renderer(ctx, opts) {
    this.ctx = ctx;
    this.k = 1;          /* device px per physical px of the module */
    this.S = 1;
    this.arts = [];      /* [{sprites, img}] */
    this.clipped = false;
    this.scene = null;
    this.counts = {};    /* commands run, by name (tests) */
    this.texts = [];     /* the last frames' texts (tests) */
    this.scratch = opts && opts.scratch; /* a 320 x 240 canvas for the core's pixels */
    this.tint = {};
    this.vec = opts && opts.vec;  /* SparkleVector.VectorArt: the vector set */
    this.seen = {};               /* vector ids drawn (the coverage report) */
    this.missed = {};             /* hashes of pictures drawn without vector art */
  }
  Renderer.prototype.setScale = function (S, k) { this.S = S; this.k = k; };
  Renderer.prototype.addArt = function (sprites, img) { this.arts.push({ sprites: sprites, img: img }); };
  Renderer.prototype.hashes = function () {
    var out = [];
    this.arts.forEach(function (a) { Object.keys(a.sprites).forEach(function (h) { out.push(parseInt(h, 16) >>> 0); }); });
    return out;
  };
  Renderer.prototype.find = function (hash) {
    var key = ("00000000" + (hash >>> 0).toString(16)).slice(-8);
    for (var i = this.arts.length - 1; i >= 0; i--) {
      var e = this.arts[i].sprites[key];
      if (e && this.arts[i].img) return { e: e, img: this.arts[i].img, key: key };
    }
    return null;
  };
  Renderer.prototype._count = function (n) { this.counts[n] = (this.counts[n] || 0) + 1; };
  Renderer.prototype._band = function (y0, y1) { /* a shape's clip band (physical rows) */
    if (!y1) return false;
    var c = this.ctx, k = this.k;
    c.save();
    c.beginPath();
    c.rect(-1e5, y0 * k, 2e5, (y1 - y0) * k);
    c.clip();
    return true;
  };
  Renderer.prototype._setClip = function (x0, y0, x1, y1) {
    var c = this.ctx, k = this.k;
    if (this.clipped) c.restore();
    this.clip = [x0, y0, x1, y1];
    c.save();
    c.beginPath();
    c.rect(Math.round(x0 * k), Math.round(y0 * k), Math.round(x1 * k) - Math.round(x0 * k), Math.round(y1 * k) - Math.round(y0 * k));
    c.clip();
    this.clipped = true;
  };
  Renderer.prototype.reset = function () {
    if (this.clipped) this.ctx.restore();
    this.clipped = false;
    this.clip = null;
  };

  /* run the words cmd[0..n) (Int32Array) */
  Renderer.prototype.run = function (cmd, n) {
    var c = this.ctx, k = this.k, i = 0, a, x0, y0, x1, y1;
    this.texts = [];
    while (i < n) {
      var op = cmd[i];
      switch (op) {
      case C_FILL:
        x0 = Math.round(cmd[i + 1] * k), y0 = Math.round(cmd[i + 2] * k);
        x1 = Math.round((cmd[i + 1] + cmd[i + 3]) * k), y1 = Math.round((cmd[i + 2] + cmd[i + 4]) * k);
        c.fillStyle = css565(cmd[i + 5]);
        c.fillRect(x0, y0, x1 - x0, y1 - y0);
        this._count("fill");
        i += 6;
        break;
      case C_LINE:
        c.strokeStyle = css565(cmd[i + 5]);
        c.lineWidth = k;
        c.lineCap = "square";
        c.beginPath();
        c.moveTo((cmd[i + 1] + 0.5) * k, (cmd[i + 2] + 0.5) * k);
        c.lineTo((cmd[i + 3] + 0.5) * k, (cmd[i + 4] + 0.5) * k);
        c.stroke();
        this._count("line");
        i += 6;
        break;
      case C_CLIP:
        this._setClip(cmd[i + 1], cmd[i + 2], cmd[i + 3], cmd[i + 4]);
        i += 5;
        break;
      case C_SHAPE:
        this._shape(cmd.subarray(i + 2, i + 2 + cmd[i + 1]));
        i += 2 + cmd[i + 1];
        break;
      case C_TEXT: {
        var nb = cmd[i + 1], words = Math.floor((nb + 4) / 4);
        a = cmd.subarray(i + 2, i + 10);
        var bytes = new Uint8Array(cmd.buffer, cmd.byteOffset + (i + 10) * 4, nb);
        this._text(a, utf8(bytes));
        i += 10 + words;
        break;
      }
      case C_IMAGE:
        this._image(cmd[i + 1], cmd.subarray(i + 2, i + 14));
        i += 14;
        break;
      case C_COPY: {
        var w = cmd[i + 3], h = cmd[i + 4], nw = Math.floor((w * h + 1) / 2);
        this._copy(cmd[i + 1], cmd[i + 2], w, h, new Uint16Array(cmd.buffer, cmd.byteOffset + (i + 5) * 4, w * h));
        i += 5 + nw;
        break;
      }
      case C_SCENE:
        this.scene = { id: cmd[i + 1], sky: cmd[i + 2], ground: cmd[i + 3], accent: cmd[i + 4] };
        i += 5;
        break;
      case C_SPRITE:
        this._sprite(cmd[i + 2] >>> 0, cmd.subarray(i + 3, i + 2 + cmd[i + 1]));
        i += 2 + cmd[i + 1];
        break;
      case C_SOFTBAR:
        this._softbar(cmd[i + 1], cmd[i + 2], cmd[i + 3], cmd[i + 4], cmd[i + 5]);
        i += 6;
        break;
      default:
        throw new Error("sparkle: bad command " + op + " at " + i);
      }
    }
  };

  Renderer.prototype._shape = function (a) {
    var c = this.ctx, f = this.k / 16, kind = a[0], band = this._band(a[2], a[3]);
    c.fillStyle = c.strokeStyle = css565(a[1]);
    c.beginPath();
    switch (kind) {
    case SH_CIRCLE:
      c.arc(a[4] * f, a[5] * f, Math.max(0.5, a[6] * f), 0, Math.PI * 2);
      c.fill();
      break;
    case SH_RING:
      c.lineWidth = a[7] * f;
      c.arc(a[4] * f, a[5] * f, Math.max(0.5, a[6] * f), 0, Math.PI * 2);
      c.stroke();
      break;
    case SH_RRECT: {
      var x = a[4] * f, y = a[5] * f, w = a[6] * f, h = a[7] * f, r = Math.min(a[8] * f, w / 2, h / 2);
      if (w > 0 && h > 0) {
        c.moveTo(x + r, y);
        c.arcTo(x + w, y, x + w, y + h, r);
        c.arcTo(x + w, y + h, x, y + h, r);
        c.arcTo(x, y + h, x, y, r);
        c.arcTo(x, y, x + w, y, r);
        c.closePath();
        c.fill();
      }
      break;
    }
    case SH_LINE:
      c.lineWidth = a[8] * f;
      c.lineCap = "round";
      c.moveTo(a[4] * f, a[5] * f);
      c.lineTo(a[6] * f, a[7] * f);
      c.stroke();
      break;
    case SH_POLY: {
      var n = a[4];
      for (var j = 0; j < n; j++) {
        if (j) c.lineTo(a[5 + 2 * j] * f, a[6 + 2 * j] * f);
        else c.moveTo(a[5] * f, a[6] * f);
      }
      c.closePath();
      c.fill("evenodd");
      break;
    }
    default:
      break;
    }
    this._count("shape" + kind);
    if (band) c.restore();
  };

  /* text a = [style | align << 4, colour, clip0, clip1, x16, y16, w16, h16]: the 1x layout box, a web font.
   * The font's size is fitted to the box by measuring the text in whatever font this browser has (SF Pro
   * Rounded in Safari, another rounded face elsewhere): about the cell's height, smaller when the text is
   * wider than the box (down to half the cell before anything is squeezed); a single character keeps its
   * shape (never squashed: the render audit's M7) and sits on the box's middle. Then placed in the box as
   * the module placed it: left, centred or right (M2) */
  var TEXT_MIN = 0.5, TEXT_FIT_LO = 0.66, TEXT_FIT_HI = 0.84;
  Renderer.prototype._text = function (a, s) {
    var c = this.ctx, f = this.k / 16, st = Math.max(0, Math.min(3, a[0] & 15)), align = (a[0] >> 4) & 3;
    var x = a[4] * f, y = a[5] * f, w = a[6] * f, h = a[7] * f;
    var band = this._band(a[2], a[3]);
    var size = h * 0.78, font = function (sz) { return STYLE_WEIGHT[st] + " " + sz.toFixed(1) + "px " + FONT; };
    c.font = font(size);
    var tw = c.measureText(s).width, one = Array.from ? Array.from(s).length === 1 : s.length === 1;
    if (tw > 0 && !one) {
      var fitted = Math.max(h * TEXT_FIT_LO, Math.min(h * TEXT_FIT_HI, size * w / tw));
      if (fitted * tw / size > w) fitted = Math.max(h * TEXT_MIN, size * w / tw); /* too wide even so: smaller */
      if (Math.abs(fitted - size) > 0.25) { size = fitted; c.font = font(size); tw = c.measureText(s).width; }
    }
    c.fillStyle = css565(a[1]);
    c.textBaseline = "alphabetic";
    c.textAlign = "left";
    var squeeze = !one && tw > w * 1.01, dw = squeeze ? w : tw;
    var bx = one || align === 1 ? x + (w - dw) / 2 : align === 2 ? x + w - dw : x, by = y + h * 0.8;
    if (squeeze) c.fillText(s, bx, by, w);
    else c.fillText(s, bx, by);
    this.texts.push(s);
    this._count("text");
    if (band) c.restore();
  };

  /* the module's mq_tint on an RGB565 colour (1 dusk, 2 night) */
  function tint565(c, tint) {
    if (!tint || c < 0) return c;
    var r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    if (tint === 2) r = (r * 5 >> 4) + 2, g = (g * 6 >> 4) + 5, b = (b * 9 >> 4) + 9;
    else r = (r * 15 >> 4) + 1, g = (g * 14 >> 4) + 3, b = (b * 12 >> 4) + 2;
    return Math.min(31, r) << 11 | Math.min(63, g) << 5 | Math.min(31, b);
  }
  /* a = [sil, clip0, clip1, x16, y16, art16, w, h, flags, shear16, sy16, tint]: the picture as the module
   * drew it, tinted as it was (the render audit's H2: dusk and night reach the pictures too) */
  Renderer.prototype._image = function (hash, a) {
    var vid = this.vec ? this.vec.idOfHash(hash) : null;
    var hit = vid ? null : this.find(hash);
    this._count(vid ? "vector" : hit ? "image" : "imageMiss");
    if (vid) this.seen[vid] = (this.seen[vid] || 0) + 1;
    else this.missed[("00000000" + (hash >>> 0).toString(16)).slice(-8)] = 1;
    if (!hit && !vid) return;
    var c = this.ctx, k = this.k, e = hit && hit.e, img = hit && hit.img;
    var X = a[3] / 16, Y = a[4] / 16, p = a[5] / 16, w = a[6], h = a[7], fl = a[8], shear = a[9] / 16, q = a[10] / 16;
    var tint = a[11] === 1 || a[11] === 2 ? a[11] : 0, sil = tint565(a[0], tint);
    if (vid) {
      var vb = this._band(a[1], a[2]);
      c.save();
      this._imageXf(X, Y, p, w, h, fl, shear, q);
      this.vec.draw(c, vid, 0, 0, w, h, k * p * Math.max(1, q), sil >= 0 ? 0 : tint, sil);
      c.restore();
      if (vb) c.restore();
      return;
    }
    var src = img, sx = e[0], sy = e[1], sw = e[2], sh = e[3];
    if ((sil >= 0 || tint) && doc) { /* a silhouette (the picture in one colour) or tinted: a canvas of its own */
      var key = hit.key + ":" + sil + ":" + tint, t = this.tint[key];
      if (!t) {
        t = doc.createElement("canvas");
        t.width = sw, t.height = sh;
        var tc = t.getContext("2d");
        tc.drawImage(img, sx, sy, sw, sh, 0, 0, sw, sh);
        if (sil >= 0) {
          tc.globalCompositeOperation = "source-in";
          tc.fillStyle = css565(sil);
          tc.fillRect(0, 0, sw, sh);
        } else {
          var m = doc.createElement("canvas");
          m.width = sw, m.height = sh;
          m.getContext("2d").drawImage(t, 0, 0);
          tintPass(tc, sw, sh, tint, m);
        }
        this.tint[key] = t;
      }
      src = t, sx = 0, sy = 0;
    }
    var band = this._band(a[1], a[2]);
    c.save();
    this._imageXf(X, Y, p, w, h, fl, shear, q);
    c.imageSmoothingEnabled = true;
    if ("imageSmoothingQuality" in c) c.imageSmoothingQuality = "high";
    c.drawImage(src, sx, sy, sw, sh, 0, 0, w, h);
    c.restore();
    if (band) c.restore();
  };

  /* a sprite the module named (MODX_HR_SPRITE), b = [colour, clip0, clip1, x16, y16, w16, h16, flags,
   * t, tint, n, logical w, h, params...]: its vector art, or its rig (SparkleVector.rigs: drawn in
   * the box's logical pixels, animated parts) */
  Renderer.prototype._sprite = function (hash, b) {
    var V = this.vec, id = V && V.idOfSprite(hash);
    this._count(id ? "sprite" : "spriteMiss");
    if (!id) return;
    this.seen[id] = (this.seen[id] || 0) + 1;
    var c = this.ctx, f = this.k / 16, band = this._band(b[1], b[2]);
    var spr = { id: id, x: b[3] * f, y: b[4] * f, w: b[5] * f, h: b[6] * f, flags: b[7], t: b[8] >>> 0, tint: b[9],
      lw: b[11], lh: b[12], v: b.subarray(13, 11 + b[10]), k: this.k, z: this.k * this.S };
    c.save();
    if (spr.flags & 1) { c.translate(spr.x * 2 + spr.w, 0); c.scale(-1, 1); }
    if (spr.flags & 2) { c.translate(0, spr.y * 2 + spr.h); c.scale(1, -1); }
    var rig = root.SparkleVector && root.SparkleVector.rigs[id];
    spr.night = spr.tint;
    if (rig && spr.tint && !rig.ownTint && doc) { /* dusk / night: the rig drawn untinted off screen, then the
                                                   * module's mq_tint over every pixel it drew (the render
                                                   * audit's H1: rigs paint literal colours) */
      c.restore();
      this._rigTinted(rig, V, spr);
      if (band) c.restore();
      return;
    }
    if (rig) { /* the rig draws in the box's logical pixels */
      c.translate(spr.x, spr.y);
      c.scale(spr.w / Math.max(1, spr.lw), spr.h / Math.max(1, spr.lh));
      spr.dev = spr.w / Math.max(1, spr.lw);
      rig(c, V, spr);
    } else V.draw(c, id, spr.x, spr.y, spr.w, spr.h, 1, spr.tint);
    c.restore();
    if (band) c.restore();
  };
  /* a rig into an off-screen canvas (its box and a margin, device px), tinted, then onto the canvas */
  Renderer.prototype._rigTinted = function (rig, V, spr) {
    var dev = spr.w / Math.max(1, spr.lw), pad = Math.ceil(12 * dev);
    var X0 = Math.floor(spr.x) - pad, Y0 = Math.floor(spr.y) - pad;
    var W = Math.ceil(spr.x + spr.w) + pad - X0, H = Math.ceil(spr.y + spr.h) + pad - Y0;
    if (W <= 0 || H <= 0) return;
    var o = this._offscreen("off", W, H), oc = o.getContext("2d");
    oc.save();
    oc.beginPath(); oc.rect(0, 0, W, H); oc.clip();
    oc.translate(-X0, -Y0);
    if (spr.flags & 1) { oc.translate(spr.x * 2 + spr.w, 0); oc.scale(-1, 1); }
    if (spr.flags & 2) { oc.translate(0, spr.y * 2 + spr.h); oc.scale(1, -1); }
    oc.translate(spr.x, spr.y);
    oc.scale(spr.w / Math.max(1, spr.lw), spr.h / Math.max(1, spr.lh));
    var tint = spr.tint;
    spr.dev = dev, spr.tint = 0; /* (its colours as by day; spr.night says what it is) */
    rig(oc, V, spr);
    spr.tint = tint;
    oc.restore();
    tintPass(oc, W, H, tint, this._offscreen("offMask", W, H, o));
    this.ctx.drawImage(o, 0, 0, W, H, X0, Y0, W, H);
  };
  /* a cleared scratch canvas of at least W x H (copy: a copy of another's pixels) */
  Renderer.prototype._offscreen = function (name, W, H, copy) {
    var o = this[name] || (this[name] = doc.createElement("canvas"));
    if (o.width < W || o.height < H) { o.width = Math.max(o.width, W); o.height = Math.max(o.height, H); }
    var oc = o.getContext("2d");
    oc.setTransform(1, 0, 0, 1, 0, 0);
    oc.globalCompositeOperation = "source-over";
    oc.globalAlpha = 1;
    oc.clearRect(0, 0, W, H);
    if (copy) oc.drawImage(copy, 0, 0, W, H, 0, 0, W, H);
    return o;
  };

  /* a part (x y w h, 1x) of a game's soft-key bar: candy pills in vector (their words come as text) */
  var PILL = [[0xFDDB, 0xF3F4], [0xAF39, 0x5DF1], [0xAEDF, 0x5D5C], [0xD63F, 0x9BFC], [0xFEB5, 0xF50B]];
  Renderer.prototype._softbar = function (x, y, w, h, mask) {
    var c = this.ctx, z = this.k * this.S, clip = this.clip, was = this.clipped;
    this._count("softbar");
    if (was) { c.restore(); this.clipped = false; } /* the core clipped it already (as for its pixels) */
    c.save();
    c.beginPath();
    c.rect(Math.floor(x * z), Math.floor(y * z), Math.ceil(w * z) + 1, Math.ceil(h * z) + 1);
    c.clip();
    c.fillStyle = css565(0xFF9E);
    c.fillRect(x * z, y * z, w * z, h * z);
    for (var i = 0; i < 5; i++) {
      if (!(mask >> i & 1)) continue;
      var px = (i * 64 + 1.5) * z, py = (224 + 1.2) * z, pw = 61 * z, ph = 13.8 * z, r = ph * 0.48;
      var cand = css565(PILL[i][0]), edge = css565(PILL[i][1]);
      rrPath(c, px, py + z * 1.0, pw, ph, r); /* the edge below */
      c.fillStyle = edge;
      c.fill();
      rrPath(c, px, py, pw, ph - z * 0.6, r);
      var g = c.createLinearGradient(0, py, 0, py + ph);
      g.addColorStop(0, mixCss(PILL[i][0], 0.45));
      g.addColorStop(0.6, cand);
      g.addColorStop(1, cand);
      c.fillStyle = g;
      c.fill();
      rrPath(c, px + r * 0.6, py + ph * 0.12, pw - r * 1.2, ph * 0.22, ph * 0.11); /* the shine */
      c.fillStyle = "rgba(255,255,255,0.75)";
      c.fill();
    }
    c.restore();
    if (was && clip) this._setClip(clip[0], clip[1], clip[2], clip[3]);
  };
  /* the module's mq_tint (modules/mathquest/mq_vis.c), c' = c * m + a per channel (dusk 1, night 2), over
   * what a canvas holds in (0, 0, W, H): multiply, add, then the drawn pixels' own alpha again (mask: a
   * copy of them) */
  var TINT_MUL = { 1: "rgb(239,223,191)", 2: "rgb(80,96,143)" }, TINT_ADD = { 1: "rgb(8,12,16)", 2: "rgb(16,20,72)" };
  function tintPass(oc, W, H, tint, mask) {
    if (!TINT_MUL[tint]) return;
    oc.save();
    oc.setTransform(1, 0, 0, 1, 0, 0);
    oc.globalCompositeOperation = "multiply";
    oc.fillStyle = TINT_MUL[tint]; oc.fillRect(0, 0, W, H);
    oc.globalCompositeOperation = "lighter";
    oc.fillStyle = TINT_ADD[tint]; oc.fillRect(0, 0, W, H);
    oc.globalCompositeOperation = "destination-in";
    oc.drawImage(mask, 0, 0, W, H, 0, 0, W, H);
    oc.restore();
  }
  function rrPath(c, x, y, w, h, r) {
    r = Math.min(r, w / 2, h / 2);
    c.beginPath();
    c.moveTo(x + r, y);
    c.arcTo(x + w, y, x + w, y + h, r);
    c.arcTo(x + w, y + h, x, y + h, r);
    c.arcTo(x, y + h, x, y, r);
    c.arcTo(x, y, x + w, y, r);
    c.closePath();
  }
  function mixCss(c565, t) { /* an RGB565 colour toward white */
    var r = ((c565 >> 11) & 31) * 8.22, g = ((c565 >> 5) & 63) * 4.05, b = (c565 & 31) * 8.22;
    return "rgb(" + Math.round(r + (255 - r) * t) + "," + Math.round(g + (255 - g) * t) + "," + Math.round(b + (255 - b) * t) + ")";
  }

  /* a picture's transform: physical (X, Y) + art pixels of size p, sheared and squashed (sprite
   * animations), mirrored and turned (flags) */
  Renderer.prototype._imageXf = function (X, Y, p, w, h, fl, shear, q) {
    var c = this.ctx, k = this.k, rot = fl & 3, dw = rot & 1 ? h : w;
    c.setTransform(k, 0, 0, k, 0, 0);
    if (shear || q !== 1)
      c.transform(p, 0, -shear / h, p * q, X + shear, Y + h * p * (1 - q));
    else
      c.transform(p, 0, 0, p, X, Y);
    if (fl & 4) c.transform(-1, 0, 0, 1, dw, 0);
    if (rot === 1) c.transform(0, 1, -1, 0, h, 0);
    else if (rot === 2) c.transform(-1, 0, 0, -1, w, h);
    else if (rot === 3) c.transform(0, -1, 1, 0, 0, w);
  };

  /* the core's pixels (1x) at x, y: scaled up without smoothing */
  Renderer.prototype._copy = function (x, y, w, h, px) {
    var c = this.ctx, sc = this.scratch;
    this._count("copy");
    if (!sc || w <= 0 || h <= 0) return;
    var sctx = sc.getContext("2d"), im = sctx.createImageData(w, h), d = im.data;
    for (var i = 0; i < w * h; i++) {
      var v = px[i], r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
      d[i * 4] = (r << 3) | (r >> 2); d[i * 4 + 1] = (g << 2) | (g >> 4); d[i * 4 + 2] = (b << 3) | (b >> 2); d[i * 4 + 3] = 255;
    }
    sctx.putImageData(im, x, y);
    var z = this.k * this.S, clip = this.clip;
    if (this.clipped) c.restore(); /* the core clipped it already */
    c.imageSmoothingEnabled = false;
    var X0 = Math.round(x * z), Y0 = Math.round(y * z);
    c.drawImage(sc, x, y, w, h, X0, Y0, Math.round((x + w) * z) - X0, Math.round((y + h) * z) - Y0);
    c.imageSmoothingEnabled = true;
    if (this.clipped) { this.clipped = false; this._setClip(clip[0], clip[1], clip[2], clip[3]); }
  };

  /* ---------------------------------------------------------------- the sides */
  function pastel(c, t) { /* mix an RGB565 colour toward cream */
    var r = ((c >> 11) & 31) * 8.22, g = ((c >> 5) & 63) * 4.05, b = (c & 31) * 8.22;
    return [r + (255 - r) * t, g + (250 - g) * t, b + (245 - b) * t];
  }
  function Sides() {
    this.bubbles = [];
    var r = 12345;
    function rnd() { r = (r * 1103515245 + 12345) & 0x7fffffff; return r / 0x7fffffff; }
    for (var i = 0; i < 26; i++)
      this.bubbles.push({ x: rnd(), y: rnd(), r: 0.006 + rnd() * 0.018, s: 0.004 + rnd() * 0.012, ph: rnd() * 6.28,
        star: rnd() < 0.35, hue: Math.floor(rnd() * 4) });
    this.edges = null;
  }
  /* fb: the 1x framebuffer (RGB565), to carry the scene's edge colours outward */
  Sides.prototype.sample = function (fb) {
    var L = [], R = [];
    for (var y = 0; y < H; y += 4) { L.push(fb[y * W + 1]); R.push(fb[y * W + W - 2]); }
    this.edges = { L: L, R: R, top: fb[20 * W + 160], bottom: fb[(H - 20) * W + 160] };
  };
  Sides.prototype.draw = function (ctx, Lyt, t, scene) {
    var cw = Lyt.canvasW, ch = Lyt.canvasH, P = Lyt.play;
    if (typeof root.sparkleSides === "function") { /* a module / page hook may paint them itself */
      try { if (root.sparkleSides(ctx, { canvas: [cw, ch], play: P, scene: scene, edges: this.edges, t: t }) === true) return; }
      catch (e) { /* fall back */ }
    }
    var g = ctx.createLinearGradient(0, 0, 0, ch);
    g.addColorStop(0, "#fde9f3"); g.addColorStop(0.55, "#efe9ff"); g.addColorStop(1, "#e3f6ef");
    ctx.fillStyle = g;
    ctx.fillRect(0, 0, cw, ch);
    var E = this.edges;
    if (E && P.x > 0) { /* the scene's edge colours, bands fading outward */
      var bandH = P.h / E.L.length;
      for (var side = 0; side < 2; side++) {
        var cols = side ? E.R : E.L, x0 = side ? P.x + P.w : 0, w = P.x;
        for (var i = 0; i < cols.length; i++) {
          var c0 = pastel(cols[i], 0.25), y = P.y + i * bandH;
          var gr = ctx.createLinearGradient(side ? x0 : x0 + w, 0, side ? x0 + w : x0, 0);
          gr.addColorStop(0, "rgba(" + (c0[0] | 0) + "," + (c0[1] | 0) + "," + (c0[2] | 0) + ",0.95)");
          gr.addColorStop(0.7, "rgba(" + (c0[0] | 0) + "," + (c0[1] | 0) + "," + (c0[2] | 0) + ",0.35)");
          gr.addColorStop(1, "rgba(" + (c0[0] | 0) + "," + (c0[1] | 0) + "," + (c0[2] | 0) + ",0)");
          ctx.fillStyle = gr;
          ctx.fillRect(x0, Math.floor(y), w, Math.ceil(bandH) + 1);
        }
      }
    }
    /* drifting bubbles, hearts of light and stars */
    var hues = ["rgba(255,182,213,0.55)", "rgba(201,182,255,0.5)", "rgba(168,230,207,0.55)", "rgba(255,230,140,0.6)"];
    var u = Math.min(cw, ch);
    for (var b = 0; b < this.bubbles.length; b++) {
      var B = this.bubbles[b], bx = B.x * cw + Math.sin(t / 2400 + B.ph) * u * 0.02;
      var by = ((B.y - t * B.s / 1000 / 8) % 1 + 1) % 1 * ch, r = B.r * u;
      if (bx + r > P.x && bx - r < P.x + P.w && by + r > P.y && by - r < P.y + P.h) continue; /* not over the game */
      ctx.fillStyle = hues[B.hue];
      ctx.beginPath();
      if (B.star) {
        for (var j = 0; j < 10; j++) {
          var rr = j % 2 ? r * 0.45 : r, an = -Math.PI / 2 + j * Math.PI / 5 + t / 3000 + B.ph;
          if (j) ctx.lineTo(bx + Math.cos(an) * rr, by + Math.sin(an) * rr); else ctx.moveTo(bx + Math.cos(an) * rr, by + Math.sin(an) * rr);
        }
        ctx.closePath();
      } else ctx.arc(bx, by, r, 0, Math.PI * 2);
      ctx.fill();
    }
    /* a soft frame around the play area */
    ctx.save();
    ctx.shadowColor = "rgba(120, 80, 140, 0.28)";
    ctx.shadowBlur = Math.max(8, u * 0.02);
    ctx.fillStyle = "#fff";
    ctx.fillRect(P.x, P.y, P.w, P.h);
    ctx.restore();
  };

  var Sparkle = { layout: layout, chooseScale: chooseScale, toLogical: toLogical, KEYMAP: KEYMAP, Renderer: Renderer,
    Sides: Sides, css565: css565, W: W, H: H };
  root.Sparkle = Sparkle;
  if (typeof module !== "undefined" && module.exports) module.exports = Sparkle;
  if (!doc || !doc.getElementById || !doc.getElementById("view")) return; /* (loaded as a library) */

  /* ================================================================ the page */
  var view = doc.getElementById("view"), vctx = view.getContext("2d");
  var play = doc.createElement("canvas"), pctx = play.getContext("2d");
  var scratch = doc.createElement("canvas");
  scratch.width = W, scratch.height = H;
  var store = null;
  try { store = root.localStorage || null; } catch (e) { store = null; }
  function sget(k) { try { return store ? store.getItem(k) : null; } catch (e) { return null; } }
  function sset(k, v) { try { if (store) store.setItem(k, v); } catch (e) { /* full or blocked */ } }

  var dpr = root.devicePixelRatio || 1;
  var scr = root.screen || {};
  var S = chooseScale(scr.height, root.innerHeight, dpr);
  var vec = root.SparkleVector ? new root.SparkleVector.VectorArt() : null;
  var L = null, sim = null, M = null, R = new Renderer(pctx, { scratch: scratch, vec: vec }), sides = new Sides();
  Sparkle.vec = vec;
  /* what the frames since the last reset drew: vector ids, and pictures / sprites without vector art */
  Sparkle.coverage = function () {
    var sm = M && M._web_sparkle_sprite_misses ? M.UTF8ToString(M._web_sparkle_sprite_misses()).split("\n").filter(Boolean) : [];
    R.spriteMissed = R.spriteMissed || {};
    sm.forEach(function (x) { R.spriteMissed[x] = 1; });
    return { vector: Object.keys(R.seen).length, ids: Object.keys(R.seen).sort(), missed: Object.keys(R.missed).sort(),
      spritesMissed: Object.keys(R.spriteMissed).sort() };
  };
  if (vec) {
    vec.onready = function () { if (sim) { registerArt(); redraw(); } };
    vec.add(root.SPARKLE_VECTOR);
    (root.SPARKLE_EXTRA_VECTOR || []).forEach(function (p) { vec.add(p); });
  }
  var audio = new SparkleAudio({ store: store });
  Sparkle.audio = audio;
  Sparkle.renderer = R;
  Sparkle.S = S;
  /* the characters talk (calcSpeak: the speak hook): cute babble or words; the old blips (sfx 21) only
   * when no line came with them (an older module) */
  var voice = root.SparkleVoice ? new root.SparkleVoice({ audio: audio }) : null;
  Sparkle.voice = voice;
  root.calcSpeak = function (who, text) {
    if (voice) voice.say(who & 255, text, { words: !!(who & 0x100), now: !!(who & 0x200) });
  };
  root.calcSound = function (kind, id, param) {
    var t = (typeof performance !== "undefined" && performance.now) ? performance.now() : Date.now();
    if (kind === "sfx" && id === 21 && voice && t - voice.lastSay < 250) { audio.log.push([kind, id, param]); return; }
    audio.handle(kind, id, param);
  };

  /* art: the atlas images (decoded asynchronously; a picture not ready yet is drawn as pixels) */
  function loadArt(a) {
    if (!a || !a.json) return;
    var img = new root.Image(), entry = { sprites: a.json.sprites, img: null };
    R.arts.push(entry);
    img.onload = function () { entry.img = img; if (sim) { registerArt(); redraw(); } };
    img.src = a.uri;
  }
  function registerArt() {
    var hs = [];
    R.arts.forEach(function (e) { if (e.img) Object.keys(e.sprites).forEach(function (h) { hs.push(parseInt(h, 16) >>> 0); }); });
    if (vec) hs = hs.concat(vec.pictureHashes());
    if (vec && M._web_sparkle_vec) {
      var sv = vec.spriteHashes(), q = M._malloc(Math.max(4, sv.length * 4));
      new Uint32Array(M.HEAPU8.buffer, q, sv.length).set(sv);
      M._web_sparkle_vec(q, sv.length);
      M._free(q);
    }
    var p = M._malloc(Math.max(4, hs.length * 4));
    new Uint32Array(M.HEAPU8.buffer, p, hs.length).set(hs);
    M._web_sparkle_art(p, hs.length);
    M._free(p);
  }
  /* the font's widths for the module (MODX_HR_MEASURE): per style, the advance of each printable character
   * and of the cent sign at 100 px, x 16 -- a sentence of runs ("That's " "12c" "!") is then set as this font
   * has it, with no gap between the runs */
  function registerMetrics() {
    if (!M || !M._web_sparkle_metrics || !doc.createElement) return;
    var cv = doc.createElement("canvas").getContext("2d"), n = 96, w = new Int32Array(4 * n);
    if (!cv) return;
    for (var st = 0; st < 4; st++) {
      cv.font = STYLE_WEIGHT[st] + " 100px " + FONT;
      for (var i = 0; i < n; i++)
        w[st * n + i] = Math.round(cv.measureText(i < n - 1 ? String.fromCharCode(32 + i) : "¢").width * 16);
    }
    var p = M._malloc(w.length * 4);
    new Int32Array(M.HEAPU8.buffer, p, w.length).set(w);
    M._web_sparkle_metrics(p, w.length);
    M._free(p);
  }
  loadArt(root.SPARKLE_ART);

  (root.SPARKLE_EXTRA_ART || []).forEach(loadArt);

  function resize() {
    var cw = root.innerWidth, ch = root.innerHeight;
    dpr = root.devicePixelRatio || 1;
    L = layout(cw, ch, dpr, S);
    Sparkle.L = L;
    view.width = L.canvasW, view.height = L.canvasH;
    view.style.width = cw + "px", view.style.height = ch + "px";
    if (play.width !== L.play.w || play.height !== L.play.h) {
      play.width = L.play.w, play.height = L.play.h;
      R.reset();
      R.setScale(S, L.k);
      redraw();
    }
  }
  function redraw() {
    if (!M) return;
    R.reset();
    M._web_sparkle_redraw();
    drain();
  }
  var take = null;
  function drain() {
    var lp = M._malloc(4);
    try {
      var p = M._web_sparkle_take(lp), n = new Uint32Array(M.HEAPU8.buffer, lp, 1)[0];
      if (n) R.run(new Int32Array(M.HEAPU8.buffer, p, n), n);
    } finally { M._free(lp); }
  }

  /* ---- input: pointer -> touch (fingers 0..2), pinch -> fingers 3 and 4 */
  var fingers = {};
  function freeFinger() {
    var used = {};
    for (var k in fingers) used[fingers[k]] = true;
    for (var i = 0; i < 3; i++) if (!used[i]) return i;
    return -1;
  }
  function touchAt(e, state) {
    var id = fingers[e.pointerId];
    if (id === undefined || !sim) return;
    var p = toLogical(L, e.clientX, e.clientY, true);
    if (id === 0) sim.touch(p[0], p[1], state); else sim.touchId(id, p[0], p[1], state);
  }
  view.addEventListener("pointerdown", function (e) {
    gesture();
    if (e.button !== undefined && e.button !== 0) return;
    if (!toLogical(L, e.clientX, e.clientY, false)) return; /* beside the game */
    var id = freeFinger();
    if (id < 0) return;
    e.preventDefault();
    fingers[e.pointerId] = id;
    try { view.setPointerCapture(e.pointerId); } catch (err) { /* ignore */ }
    touchAt(e, 1);
  });
  view.addEventListener("pointermove", function (e) { touchAt(e, 2); });
  function up(e) { if (fingers[e.pointerId] === undefined) return; touchAt(e, 0); delete fingers[e.pointerId]; }
  view.addEventListener("pointerup", up);
  view.addEventListener("pointercancel", up);
  view.addEventListener("contextmenu", function (e) { e.preventDefault(); });

  /* Safari trackpad pinch: gesture events (scale 1 = start); two fingers 60 px apart scaled */
  var pinch = null;
  function pinchMove(scale, state) {
    if (!pinch || !sim) return;
    var d = Math.max(8, Math.min(300, 60 * scale)) / 2, cx = pinch[0], cy = pinch[1];
    sim.touchId(3, Math.max(0, cx - d), cy, state);
    sim.touchId(4, Math.min(W - 1, cx + d), cy, state);
  }
  Sparkle.pinch = function (phase, scale, cx, cy) { /* phase: start | change | end (tests use it too) */
    if (phase === "start") { pinch = [cx === undefined ? 160 : cx, cy === undefined ? 120 : cy]; pinchMove(1, 1); }
    else if (phase === "change") pinchMove(scale, 2);
    else if (pinch) { pinchMove(scale, 0); pinch = null; }
  };
  function gpos(e) { var p = toLogical(L, e.clientX || 0, e.clientY || 0, true); return p; }
  doc.addEventListener("gesturestart", function (e) { e.preventDefault(); gesture(); var p = gpos(e); Sparkle.pinch("start", 1, p[0], p[1]); });
  doc.addEventListener("gesturechange", function (e) { e.preventDefault(); Sparkle.pinch("change", e.scale); });
  doc.addEventListener("gestureend", function (e) { e.preventDefault(); Sparkle.pinch("end", e.scale); });
  /* (other browsers: a pinch on the trackpad is a wheel event with ctrlKey) */
  var wheelPinch = null, wheelTimer = 0;
  /* a plain wheel or two-finger trackpad scroll: notches to the game (MODX_EV_SCROLL; only an adjustable widget
   * under the pointer takes them -- a lock's wheel, a slider, a line, a clock hand, a list, the shop's pages --
   * nowhere else does a scroll do anything). The deltas are the browser's, so macOS natural scrolling is
   * followed as set (+ = the content moves up, the same for a mouse and a trackpad). Each mouse-wheel click
   * is one notch, never more; a trackpad's small pixel deltas gather into notches of 40 px (at most one per
   * event, at most one per 60 ms); its inertial tail after the fingers lift (deltas falling away from the
   * peak) is ignored until the fingers push again or the gesture ends (a 250 ms gap). Ctrl+wheel is a pinch. */
  var WH = { acc: 0, t: -1e9, abs: 0, peak: 0, fall: 0, mom: false, dir: 0, nt: -1e9 };
  function wheelNotches(e, t) {
    var dy = e.deltaY || 0, gap = t - WH.t;
    WH.t = t;
    if (!dy) return 0;
    var dir = dy > 0 ? 1 : -1, px = e.deltaMode === 1 ? dy * 40 : e.deltaMode === 2 ? dy * 400 : dy, a = Math.abs(px);
    var wd = e.wheelDeltaY;
    var discrete = e.deltaMode === 1 || e.deltaMode === 2 ||
      (wd ? wd % 120 === 0 && wd !== -3 * dy : a >= 50 && Math.round(dy) === dy && gap > 60);
    if (gap > 250 || dir !== WH.dir) WH.acc = 0, WH.abs = 0, WH.peak = 0, WH.fall = 0, WH.mom = false;
    WH.dir = dir;
    if (discrete) { WH.acc = 0; WH.nt = t; return dir; } /* one click: one notch */
    if (WH.mom) { /* the tail: ignored, unless the fingers push again */
      if (a > WH.abs * 1.5 + 2) WH.mom = false, WH.peak = a, WH.fall = 0;
      else { WH.abs = a; return 0; }
    }
    WH.fall = a < WH.abs ? WH.fall + 1 : 0;
    WH.abs = a;
    if (a > WH.peak) WH.peak = a;
    if (WH.peak >= 8 && WH.fall >= 3 && a < WH.peak / 2) { WH.mom = true; WH.acc = 0; return 0; }
    WH.acc += px;
    if (Math.abs(WH.acc) < 40 || t - WH.nt < 60) { WH.acc = Math.max(-40, Math.min(40, WH.acc)); return 0; }
    WH.acc -= dir * 40; WH.nt = t;
    WH.acc = Math.max(-40, Math.min(40, WH.acc));
    return dir;
  }
  Sparkle.wheelNotches = function (e, t) { return wheelNotches(e, t); }; /* (tests) */
  Sparkle.text = function (ch) { /* -> true if the game's text field took the character (tests use it too) */
    var c = ch.charCodeAt(0);
    return !!(c >= 32 && c < 127 && M && M._web_sparkle_text && M._web_sparkle_text(c));
  };
  Sparkle.scroll = function (dy, x, y) { /* (tests use it too) */
    if (M && M._web_sparkle_scroll) M._web_sparkle_scroll(dy | 0, x === undefined ? 160 : x, y === undefined ? 120 : y);
  };
  view.addEventListener("wheel", function (e) {
    e.preventDefault(); /* (the page itself never scrolls or bounces) */
    if (!e.ctrlKey) { /* a scroll */
      var n = wheelNotches(e, e.timeStamp !== undefined ? e.timeStamp : performance.now());
      if (n) { var p = gpos(e); Sparkle.scroll(n, p[0], p[1]); }
      return;
    }
    if (!wheelPinch) { var p = gpos(e); Sparkle.pinch("start", 1, p[0], p[1]); wheelPinch = 1; }
    wheelPinch = Math.max(0.2, Math.min(5, wheelPinch * Math.exp(-e.deltaY / 100)));
    Sparkle.pinch("change", wheelPinch);
    clearTimeout(wheelTimer);
    wheelTimer = setTimeout(function () { Sparkle.pinch("end", wheelPinch); wheelPinch = null; }, 180);
  }, { passive: false });

  /* keyboard */
  var held = {};
  root.addEventListener("keydown", function (e) {
    gesture();
    if (e.metaKey || e.ctrlKey || e.altKey) return;
    if (help && !help.hidden) { hideHelp(); if (e.key === "Escape" || e.key === "?") { e.preventDefault(); return; } }
    /* a printable key: to the game's text field first (the name: letters, space, - and '); the calculator's key if none took it */
    if (e.key && e.key.length === 1 && Sparkle.text(e.key)) { e.preventDefault(); return; }
    if (e.key === "?") { showHelp(); e.preventDefault(); return; }
    if ((e.key === "r" || e.key === "R") && sim) { /* a leftover's R: the calculator's letter key (ALPHA, 9) */
      e.preventDefault();
      sim.key("ALPHA", true); sim.key("ALPHA", false); sim.key("9", true); sim.key("9", false);
      return;
    }
    var name = KEYMAP[e.key];
    if (!name) return;
    e.preventDefault();
    var code = e.code || e.key;
    if (held[code]) return; /* the game repeats keys itself */
    held[code] = name;
    if (sim) sim.key(name, true);
  });
  root.addEventListener("keyup", function (e) {
    var code = e.code || e.key, name = held[code];
    if (!name) return;
    delete held[code];
    if (sim) sim.key(name, false);
  });
  root.addEventListener("blur", function () {
    Object.keys(held).forEach(function (c) { if (sim) sim.key(held[c], false); delete held[c]; });
  });

  /* ---- the cheat sheet (first start; "?" or the menu shows it again) and the corner menu */
  var help = doc.getElementById("help");
  function showHelp() { if (help) help.hidden = false; }
  function hideHelp() { if (help) { help.hidden = true; sset("sparkle_help_seen", "1"); } }
  if (help) {
    help.addEventListener("pointerdown", function (e) { e.preventDefault(); gesture(); hideHelp(); });
    if (sget("sparkle_help_seen") !== "1" && !CFG.noHelp) showHelp();
  }
  var menuBtn = doc.getElementById("menubtn"), menu = doc.getElementById("menu");
  var muteBtn = doc.getElementById("mute"), vol = doc.getElementById("volume");
  function menuLabel() {
    if (muteBtn) muteBtn.textContent = audio.muted ? "Sound off" : "Sound on";
    if (menuBtn) menuBtn.textContent = audio.muted ? "🔇" : "🔊";
    if (vol) vol.value = Math.round(audio.volume * 100);
  }
  if (menuBtn) menuBtn.addEventListener("click", function () { gesture(); menu.hidden = !menu.hidden; menuLabel(); });
  if (muteBtn) muteBtn.addEventListener("click", function () { audio.setMuted(!audio.muted); menuLabel(); });
  if (vol) vol.addEventListener("input", function () { audio.setVolume(vol.value / 100); if (audio.muted) audio.setMuted(false); menuLabel(); });
  var keysBtn = doc.getElementById("keys");
  if (keysBtn) keysBtn.addEventListener("click", function () { menu.hidden = true; showHelp(); });
  var gBtnEl = doc.getElementById("grownups"); /* the grown-ups' gate, without the F-keys */
  Sparkle.grownups = function () { if (menu) menu.hidden = true; if (sim) sim.console("mq gate"); };
  if (gBtnEl) gBtnEl.addEventListener("click", function () { gesture(); Sparkle.grownups(); });
  var fsBtn = doc.getElementById("fullscreen");
  if (fsBtn) fsBtn.addEventListener("click", function () {
    var el = doc.documentElement, f = el.requestFullscreen || el.webkitRequestFullscreen;
    if (doc.fullscreenElement || doc.webkitFullscreenElement) (doc.exitFullscreen || doc.webkitExitFullscreen).call(doc);
    else if (f) f.call(el);
    menu.hidden = true;
  });
  menuLabel();

  /* sound starts with the first gesture (Safari) */
  function gesture() { audio.unlock(); }

  /* ---- progress: the saved session in localStorage */
  /* (the module's own state first -- it saves at most every other second -- so a reload keeps her very last
   * answer too: her problem histories, boxes and rungs) */
  function keep() { if (sim) { try { sim.console("mq save"); } catch (e) { /* (no module) */ } sset(PERSIST, CalcSim.toBase64(sim.sessionGet())); } }
  Sparkle.keep = keep;
  Sparkle.redraw = function () { redraw(); return play; }; /* (tests, the bot: the canvas of this very module state) */

  /* ---- the loop */
  var last = null, lastSides = 0;
  function frame(ts) {
    var dt = last === null ? 16 : ts - last;
    last = ts;
    try {
      sim.frame(dt);
      drain();
      if (ts - lastSides > 200) { sides.sample(sim.framebuffer565()); lastSides = ts; }
      sides.draw(vctx, L, ts, R.scene);
      vctx.drawImage(play, L.play.x, L.play.y);
    } catch (err) {
      var st = doc.getElementById("status");
      if (st) { st.textContent = "Sparkle Town stopped: " + err; st.hidden = false; }
      throw err;
    }
    root.requestAnimationFrame(frame);
  }

  var status = doc.getElementById("status");
  CalcSim.create(createCalcModule).then(function (s) {
    sim = s, M = s.M;
    Sparkle.sim = s;
    root.calc = s; /* the devtools console */
    M._web_sparkle_set_scale(S);
    registerArt();
    registerMetrics();
    var cfg = { card: 64, modules: ["MATHQ"], app: "mathquest", persistKey: PERSIST };
    if (CFG.files) cfg.files = CFG.files;
    if (CFG.theme) cfg.theme = CFG.theme;
    var kept = sget(PERSIST);
    if (kept) cfg.session = kept;
    Sparkle.presetLog = s.runPreset(cfg);
    M._web_sparkle_lock();
    (CFG.boot || []).forEach(function (line) { Sparkle.presetLog.push(line + " -> " + s.console(line).trim()); });
    resize();
    root.addEventListener("resize", resize);
    setInterval(keep, 10000);
    root.addEventListener("pagehide", keep);
    root.addEventListener("beforeunload", keep);
    doc.addEventListener("visibilitychange", function () { if (doc.hidden) keep(); });
    if (status) status.hidden = true;
    Sparkle.ready = true;
    root.requestAnimationFrame(frame);
  }).catch(function (err) {
    if (status) { status.textContent = "Sparkle Town could not start: " + err; status.hidden = false; }
  });
})(typeof window !== "undefined" ? window : this);
