/* Sparkle Town, the Mac page's vector art (modules/mathquest/art_vector, tools/gen_mq_vector.py).
 *
 * Globals expected: SPARKLE_VECTOR ({svg: {id: text}, meta: {id: [vbx, vby, vbw, vbh, cw, ch]},
 * hash: {hex: id}}: web/tools/build_sparkle_html.py), optionally SPARKLE_EXTRA_VECTOR (a list of
 * the same: what a private build adds; later ones win).
 *
 * Every picture is an SVG: decoded once (an Image of the SVG), then rasterised at the size it is
 * drawn at in device pixels -- the scale rounded up to steps of 2^(1/4), so a sprite that zooms
 * or bobs reuses a few rasters -- and kept in a cache (least recently used, about 48 M pixels).
 * Silhouettes and the town's dusk / night tint are rasters of their own (the module's mq_tint
 * formula, per pixel). A picture is found by the hash of the 1x pixel art the module draws (the
 * cast, stickers, items: `hash`) or by the id the module names (MODX_HR_SPRITE: sprites, scenes).
 *
 * Exported as window.SparkleVector: {VectorArt, fnv, rigs}.
 */
(function (root) {
  "use strict";
  var doc = root.document;

  /* FNV-1a of a string (the id of a sprite the module names: platform/web/web_sparkle.c) */
  function fnv(s) {
    var h = 2166136261 >>> 0;
    for (var i = 0; i < s.length; i++) {
      h ^= s.charCodeAt(i) & 255;
      h = Math.imul(h, 16777619) >>> 0;
    }
    return h >>> 0;
  }
  function hex8(h) { return ("00000000" + (h >>> 0).toString(16)).slice(-8); }

  var STEP = Math.pow(2, 0.25);
  function bucket(P) { /* device px per unit, rounded up to a step of 2^(1/4) */
    if (!(P > 0)) return 1;
    return Math.pow(STEP, Math.ceil(Math.log(P) / Math.log(STEP) - 1e-9));
  }

  function VectorArt() {
    this.items = {};        /* id -> {id, m, img, ready} */
    this.byHash = {};       /* hex hash of the pixel art -> id */
    this.bySprite = {};     /* fnv(id) -> id (MODX_HR_SPRITE) */
    this.cache = new Map(); /* key -> {cv, px} */
    this.cachePx = 0;
    this.maxPx = 48e6;
    this.pending = 0;
    this.onready = null;
    this.rasters = 0;       /* rasterisations (tests) */
    this.failed = [];
  }
  VectorArt.prototype.add = function (pack) {
    if (!pack || !pack.svg) return;
    var self = this, ids = Object.keys(pack.svg);
    ids.forEach(function (id) {
      var m = (pack.meta && pack.meta[id]) || [0, 0, 1, 1, 1, 1];
      var it = { id: id, m: m, img: null, ready: false, svg: pack.svg[id] };
      self.items[id] = it;
      self.bySprite[fnv(id)] = id;
      if (!doc || !root.Image) return;
      var img = new root.Image();
      self.pending++;
      img.onload = function () {
        it.img = img, it.ready = true;
        self._done();
      };
      img.onerror = function () { self.failed.push(id); self._done(); };
      img.src = "data:image/svg+xml;charset=utf-8," + encodeURIComponent(pack.svg[id]);
    });
    Object.keys(pack.hash || {}).forEach(function (h) { self.byHash[h] = pack.hash[h]; });
    /* new art: the old rasters of these ids go */
    this.cache.forEach(function (v, k) { if (pack.svg[k.split("|")[0]]) { self.cachePx -= v.px; self.cache.delete(k); } });
  };
  VectorArt.prototype._done = function () {
    if (--this.pending === 0 && this.onready) this.onready();
  };
  /* the id for a picture's hash (only when its art is ready) */
  VectorArt.prototype.idOfHash = function (hash) {
    var id = this.byHash[hex8(hash)];
    return id && this.items[id] && this.items[id].ready ? id : null;
  };
  VectorArt.prototype.idOfSprite = function (h) {
    var id = this.bySprite[h >>> 0];
    if (id && this.items[id] && this.items[id].ready) return id;
    id = rigIds[h >>> 0];
    return id && this.rigReady(id) ? id : null;
  };
  VectorArt.prototype.has = function (id) { var it = this.items[id]; return !!(it && it.ready); };
  /* a rig is ready when the pictures it draws are (rig.uses: ids) */
  VectorArt.prototype.rigReady = function (id) {
    var r = SparkleVector.rigs[id], self = this;
    return !!r && (!r.uses || r.uses.every(function (u) { return self.has(u); }));
  };
  /* the hashes to tell the module's platform (pictures with vector art, sprite ids) */
  VectorArt.prototype.pictureHashes = function () {
    var out = [], self = this;
    Object.keys(this.byHash).forEach(function (h) { if (self.has(self.byHash[h])) out.push(parseInt(h, 16) >>> 0); });
    return out;
  };
  VectorArt.prototype.spriteHashes = function () {
    var out = [], self = this;
    Object.keys(this.bySprite).forEach(function (h) { if (self.has(self.bySprite[h])) out.push(+h >>> 0); });
    Object.keys(SparkleVector.rigs).forEach(function (id) {
      rigIds[fnv(id)] = id;
      if (self.rigReady(id) && !self.has(id)) out.push(fnv(id));
    });
    return out;
  };
  var rigIds = {};

  /* the raster of id at P device px per unit (bucketed); tint 0 / 1 dusk / 2 night; sil: an
   * RGB565 colour (a silhouette) or -1; grey: 0..1 (a closed building) */
  VectorArt.prototype.raster = function (id, P, tint, sil, grey) {
    var it = this.items[id];
    if (!it || !it.ready) return null;
    var B = bucket(P), m = it.m;
    var w = Math.max(1, Math.ceil(m[2] * B)), h = Math.max(1, Math.ceil(m[3] * B));
    var lim = 2400; /* never a monster raster */
    if (w > lim || h > lim) { var r = lim / Math.max(w, h); B *= r; w = Math.max(1, Math.ceil(m[2] * B)); h = Math.max(1, Math.ceil(m[3] * B)); }
    var key = id + "|" + B.toFixed(4) + "|" + (tint || 0) + "|" + (sil === undefined ? -1 : sil) + "|" + (grey || 0);
    var e = this.cache.get(key);
    if (e) { this.cache.delete(key); this.cache.set(key, e); return e; }
    var cv = doc.createElement("canvas");
    cv.width = w, cv.height = h;
    var c = cv.getContext("2d");
    c.drawImage(it.img, 0, 0, w, h);
    if (sil !== undefined && sil >= 0) {
      c.globalCompositeOperation = "source-in";
      c.fillStyle = css565(sil);
      c.fillRect(0, 0, w, h);
      c.globalCompositeOperation = "source-over";
    } else if (tint || grey) recolour(c, w, h, tint, grey);
    this.rasters++;
    e = { cv: cv, px: w * h, B: B, w: w, h: h };
    this.cache.set(key, e);
    this.cachePx += e.px;
    while (this.cachePx > this.maxPx && this.cache.size > 1) {
      var k0 = this.cache.keys().next().value, v0 = this.cache.get(k0);
      this.cache.delete(k0);
      this.cachePx -= v0.px;
    }
    return e;
  };
  /* draw id with its design box (0, 0, cw, ch) at (x, y, w, h) of ctx's current units, where one
   * unit is `dev` device pixels (for the raster's resolution) */
  VectorArt.prototype.draw = function (ctx, id, x, y, w, h, dev, tint, sil, grey) {
    var it = this.items[id];
    if (!it || !it.ready) return false;
    var m = it.m, ux = w / m[4], uy = h / m[5];
    var e = this.raster(id, dev * Math.max(ux, uy), tint, sil, grey);
    if (!e) return false;
    ctx.drawImage(e.cv, 0, 0, e.w, e.h, x + m[0] * ux, y + m[1] * uy, e.w / e.B * ux, e.h / e.B * uy);
    return true;
  };

  var colCache = {};
  function css565(c) {
    var s = colCache[c];
    if (s) return s;
    var r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (colCache[c] = "rgb(" + ((r << 3) | (r >> 2)) + "," + ((g << 2) | (g >> 4)) + "," + ((b << 3) | (b >> 2)) + ")");
  }
  /* the module's mq_tint (modules/mathquest/mq_vis.c) in 8 bits a channel; grey: toward a soft grey */
  function recolour(c, w, h, tint, grey) {
    var im;
    try { im = c.getImageData(0, 0, w, h); } catch (err) { return; }
    var d = im.data;
    if (!d) return;
    for (var i = 0; i < d.length; i += 4) {
      var r = d[i], g = d[i + 1], b = d[i + 2];
      if (grey) {
        var l = r * 0.3 + g * 0.55 + b * 0.15;
        r += (l * 0.92 + 18 - r) * grey, g += (l * 0.92 + 16 - g) * grey, b += (l * 0.92 + 26 - b) * grey;
      }
      if (tint === 2) r = r * 5 / 16 + 16, g = g * 6 / 16 + 20, b = b * 9 / 16 + 72;
      else if (tint === 1) r = r * 15 / 16 + 8, g = g * 14 / 16 + 12, b = b * 12 / 16 + 16;
      d[i] = r, d[i + 1] = g, d[i + 2] = b;
    }
    c.putImageData(im, 0, 0);
  }

  var SparkleVector = { VectorArt: VectorArt, fnv: fnv, hex8: hex8, bucket: bucket, css565: css565, rigs: {} };
  root.SparkleVector = SparkleVector;
  if (typeof module !== "undefined" && module.exports) module.exports = SparkleVector;
})(typeof window !== "undefined" ? window : this);
