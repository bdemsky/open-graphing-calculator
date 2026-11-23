/* Sparkle Town, the Mac page's rigs: vector things with moving parts (sparkle_vector.js).
 *
 * A rig draws a sprite the module named (MODX_HR_SPRITE) in the box's logical pixels, by the
 * module's animation clock (spr.t, ms) and parameters (spr.v), from the pictures of the vector
 * set (V.draw, rasterised at the device's resolution) and from paths of its own: the clock
 * hands and the ferris wheel turn, smoke curls, bubbles wobble and shimmer, squishies squish
 * (the module's soft-body points, drawn as a smooth curve), the worlds' backgrounds drift.
 * Every rig is deterministic in (t, v): the same frame looks the same.
 *
 * rig(ctx, V, spr): spr = {id, lw, lh (the box, logical px), t, tint, v, dev (device px per
 * logical px), flags}. rig.uses: the pictures it needs (it waits for them).
 */
(function (root) {
  "use strict";
  var SV = root.SparkleVector;
  if (!SV) return;
  var R = SV.rigs;
  var INK = "#5a3d5c";

  /* ---- colours */
  function rgb565(c) { return [((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31]; }
  function tintRGB(p, tint) { /* the module's mq_tint, in 8 bits */
    var r = p[0], g = p[1], b = p[2];
    if (tint === 2) return [r * 5 / 16 + 16, g * 6 / 16 + 20, b * 9 / 16 + 72];
    if (tint === 1) return [r * 15 / 16 + 8, g * 14 / 16 + 12, b * 12 / 16 + 16];
    return p;
  }
  function css(p, a) {
    return (a === undefined ? "rgb(" : "rgba(") + Math.round(p[0]) + "," + Math.round(p[1]) + "," + Math.round(p[2]) +
      (a === undefined ? ")" : "," + a + ")");
  }
  function mix(p, q, t) { return [p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t, p[2] + (q[2] - p[2]) * t]; }
  var W3 = [255, 255, 255];
  function light(p, t) { return mix(p, W3, t); }
  function deep(p, t) { return mix(mix(p, [0, 0, 0], t * 0.8), [120, 70, 150], t * 0.35); }
  function hexRGB(h) { return [parseInt(h.slice(1, 3), 16), parseInt(h.slice(3, 5), 16), parseInt(h.slice(5, 7), 16)]; }
  /* a colour for spr: an RGB array or "#rrggbb", tinted like the module's */
  function T(spr, p) { return tintRGB(typeof p === "string" ? hexRGB(p) : p, spr.tint); }

  /* ---- paths */
  function rr(c, x, y, w, h, r) {
    r = Math.max(0, Math.min(r, w / 2, h / 2));
    c.beginPath();
    c.moveTo(x + r, y);
    c.arcTo(x + w, y, x + w, y + h, r);
    c.arcTo(x + w, y + h, x, y + h, r);
    c.arcTo(x, y + h, x, y, r);
    c.arcTo(x, y, x + w, y, r);
    c.closePath();
  }
  function circ(c, x, y, r) { c.beginPath(); c.arc(x, y, Math.max(0.01, r), 0, Math.PI * 2); }
  function ell(c, x, y, rx, ry) { c.beginPath(); c.ellipse(x, y, Math.max(0.01, rx), Math.max(0.01, ry), 0, 0, Math.PI * 2); }
  function star(c, x, y, r, inner, rot) {
    c.beginPath();
    for (var i = 0; i < 10; i++) {
      var a = (rot || -Math.PI / 2) + i * Math.PI / 5, rr_ = i % 2 ? r * (inner || 0.46) : r;
      if (i) c.lineTo(x + Math.cos(a) * rr_, y + Math.sin(a) * rr_); else c.moveTo(x + Math.cos(a) * rr_, y + Math.sin(a) * rr_);
    }
    c.closePath();
  }
  function heart(c, x, y, r) {
    var t = y - r * 0.55;
    c.beginPath();
    c.moveTo(x, y + r);
    c.bezierCurveTo(x - r * 0.35, y + r * 0.7, x - r * 1.12, y + r * 0.25, x - r * 1.08, t + r * 0.25);
    c.bezierCurveTo(x - r * 1.02, t - r * 0.45, x - r * 0.2, t - r * 0.55, x, t + r * 0.05);
    c.bezierCurveTo(x + r * 0.2, t - r * 0.55, x + r * 1.02, t - r * 0.45, x + r * 1.08, t + r * 0.25);
    c.bezierCurveTo(x + r * 1.12, y + r * 0.25, x + r * 0.35, y + r * 0.7, x, y + r);
    c.closePath();
  }
  /* a closed smooth curve through points (Catmull-Rom) */
  function smooth(c, pts) {
    var n = pts.length;
    c.beginPath();
    c.moveTo(pts[0][0], pts[0][1]);
    for (var i = 0; i < n; i++) {
      var p0 = pts[(i + n - 1) % n], p1 = pts[i], p2 = pts[(i + 1) % n], p3 = pts[(i + 2) % n];
      c.bezierCurveTo(p1[0] + (p2[0] - p0[0]) / 6, p1[1] + (p2[1] - p0[1]) / 6, p2[0] - (p3[0] - p1[0]) / 6, p2[1] - (p3[1] - p1[1]) / 6,
        p2[0], p2[1]);
    }
    c.closePath();
  }
  /* fill the current path with a soft vertical gradient of p (from y0 to y1), outline it */
  function fillSoft(c, p, y0, y1, ow) {
    var g = c.createLinearGradient(0, y0, 0, y1);
    g.addColorStop(0, css(light(p, 0.32)));
    g.addColorStop(0.55, css(p));
    g.addColorStop(1, css(deep(p, 0.14)));
    c.fillStyle = g;
    if (ow) { c.lineWidth = ow; c.strokeStyle = INK; c.lineJoin = "round"; c.stroke(); }
    c.fill();
  }
  function outlined(c, p, y0, y1, ow) { /* outline behind, then the soft fill */
    c.lineWidth = ow * 2;
    c.strokeStyle = INK;
    c.lineJoin = "round";
    c.stroke();
    fillSoft(c, p, y0, y1, 0);
  }
  function shine(c, x, y, r, a) { circ(c, x, y, r); c.fillStyle = "rgba(255,255,255," + (a || 0.9) + ")"; c.fill(); }
  function gloss(c, x, y, rx, ry, rot, a) {
    c.save(); c.translate(x, y); c.rotate((rot || -30) * Math.PI / 180);
    ell(c, 0, 0, rx, ry); c.fillStyle = "rgba(255,255,255," + (a || 0.55) + ")"; c.fill(); c.restore();
  }
  function glow(c, x, y, r, p, a) {
    var g = c.createRadialGradient(x, y, 0, x, y, r);
    g.addColorStop(0, css(p, a)); g.addColorStop(1, css(p, 0));
    c.fillStyle = g; circ(c, x, y, r); c.fill();
  }
  function tri(v, period, amp) { /* the module's tri_wave */
    var p = ((v % period) + period) % period, h = period / 2, w = p < h ? p : period - p;
    return w * 2 * amp / h - amp;
  }
  function draw(c, V, spr, id, x, y, w, h, grey) { return V.draw(c, id, x, y, w, h, spr.dev, spr.tint, -1, grey || 0); }
  SV.kit = { rr: rr, circ: circ, ell: ell, star: star, heart: heart, smooth: smooth, fillSoft: fillSoft, outlined: outlined,
    shine: shine, gloss: gloss, glow: glow, rgb565: rgb565, tintRGB: tintRGB, css: css, mix: mix, light: light, deep: deep };

  /* a fluffy cloud at (x, y) (the module's cloud(): s in quarters) */
  function cloud(c, spr, x, y, s, a) {
    var k = s / 4, p = T(spr, [255, 255, 255]);
    c.save();
    c.globalAlpha = a === undefined ? 0.95 : a;
    var g = c.createLinearGradient(0, y - 9 * k, 0, y + 6 * k);
    g.addColorStop(0, css(p)); g.addColorStop(0.7, css(p)); g.addColorStop(1, css(mix(p, T(spr, [214, 222, 248]), 1)));
    c.fillStyle = g;
    c.beginPath();
    c.arc(x, y, 5 * k, 0, Math.PI * 2); c.moveTo(x + 6 * k + 7 * k, y - 3 * k);
    c.arc(x + 6 * k, y - 3 * k, 7 * k, 0, Math.PI * 2); c.moveTo(x + 13 * k + 5 * k, y);
    c.arc(x + 13 * k, y, 5 * k, 0, Math.PI * 2);
    c.rect(x, y - 0.5, 13 * k, 5 * k + 1);
    c.fill();
    gloss(c, x + 5 * k, y - 6 * k, 2.6 * k, 1.2 * k, -10, 0.7);
    c.restore();
  }
  function flowerAt(c, spr, x, y, r, col) {
    var p = T(spr, col);
    c.fillStyle = css(p);
    c.strokeStyle = css(deep(p, 0.25)); c.lineWidth = 0.4;
    for (var i = 0; i < 5; i++) {
      var a = i * 2 * Math.PI / 5 - Math.PI / 2;
      circ(c, x + Math.cos(a) * r * 0.62, y + Math.sin(a) * r * 0.62, r * 0.55); c.fill(); c.stroke();
    }
    circ(c, x, y, r * 0.4); c.fillStyle = css(T(spr, [255, 214, 90])); c.fill();
  }

  /* ================================================================ the town */
  var BUILD = { /* the windows (box coordinates) of each building: lit at night, flower boxes later */
    "town/garden": [], "town/post": [[20, 66], [70, 66]], "town/bakery": [[20, 80], [70, 80]], "town/clock": [[45, 78]],
    "town/shop": [], "town/art": [[22, 74], [68, 74]], "town/fair": [], "town/castle": [], "town/star": []
  };
  function litWindow(c, x, y) {
    glow(c, x + 5, y + 5, 14, [255, 230, 150], 0.55);
    rr(c, x, y, 10, 10, 1.2);
    var g = c.createLinearGradient(0, y, 0, y + 10);
    g.addColorStop(0, "#fff4c0"); g.addColorStop(1, "#ffd27a");
    c.fillStyle = g; c.fill();
    c.fillStyle = "rgba(255,255,255,0.8)";
    c.fillRect(x + 4.3, y, 1.4, 10); c.fillRect(x, y + 4.3, 10, 1.4);
  }
  function flowerBox(c, spr, x, y) {
    rr(c, x - 1, y + 11, 12, 3.4, 1);
    c.lineWidth = 0.8; c.strokeStyle = INK; c.stroke();
    c.fillStyle = css(T(spr, [176, 133, 96])); c.fill();
    for (var k = 0; k < 3; k++) flowerAt(c, spr, x + 1.2 + k * 3.8, y + 10, 1.9, k % 2 ? [255, 179, 199] : [255, 230, 128]);
  }
  function smoke(c, spr, x, y) { /* the module's chimney smoke, soft */
    for (var i = 0; i < 3; i++) {
      var ph = (spr.t / 40 + i * 33) % 100;
      var sx = x + 4 + Math.floor(ph / 12) % 3 - 1 + ph / 25, sy = y - 3 - ph / 4, r = 2 + ph / 40;
      c.globalAlpha = 0.85 * (1 - ph / 130);
      circ(c, sx, sy, r * 1.15); c.fillStyle = css(T(spr, [246, 246, 252])); c.fill();
      c.globalAlpha = 1;
    }
  }
  Object.keys(BUILD).forEach(function (id) {
    R[id] = function (c, V, spr) {
      var stage = spr.v[0] | 0, on = stage > 0, t = spr.t;
      if (id === "town/bakery" && on) smoke(c, spr, 76, 44);
      draw(c, V, spr, id, 0, 0, 100, 120, on ? 0 : 0.62);
      if (id === "town/clock") { /* the hands turn */
        var a = (t / 25 % 360) * Math.PI / 180, cx = 50, cy = 56;
        c.lineCap = "round"; c.strokeStyle = INK;
        c.lineWidth = 1.6; c.beginPath(); c.moveTo(cx, cy); c.lineTo(cx + Math.sin(a / 12) * 7, cy - Math.cos(a / 12) * 7); c.stroke();
        c.lineWidth = 1.1; c.beginPath(); c.moveTo(cx, cy); c.lineTo(cx + Math.sin(a) * 11, cy - Math.cos(a) * 11); c.stroke();
        circ(c, cx, cy, 1.4); c.fillStyle = css([255, 122, 162]); c.fill();
      }
      if (id === "town/star" && on && Math.floor(t / 500) % 2) { /* the lighthouse's light */
        glow(c, 77, 32, 12, [255, 240, 150], 0.9);
        circ(c, 77, 32, 3); c.fillStyle = "#fff3a0"; c.fill();
      }
      var ws = BUILD[id];
      for (var i = 0; i < ws.length; i++) {
        if (on && spr.tint) litWindow(c, ws[i][0], ws[i][1]);
        if (stage >= 2) flowerBox(c, spr, ws[i][0], ws[i][1]);
      }
    };
    R[id].uses = [id];
  });
  R["town/house"] = function (c, V, spr) {
    smoke(c, spr, 56, 6);
    draw(c, V, spr, "town/house", 0, 0, 80, 72);
    if (spr.tint) { litWindow(c, 18, 34); litWindow(c, 52, 34); flowerBox(c, spr, 18, 34); flowerBox(c, spr, 52, 34); }
  };
  R["town/house"].uses = ["town/house"];
  R["town/lamp"] = function (c, V, spr) {
    var lit = spr.v[0] | 0;
    if (spr.tint) glow(c, 10, 10, 14, [255, 236, 170], 0.75);
    draw(c, V, spr, lit ? "town/lamp_lit" : "town/lamp", 0, 0, 20, 42);
  };
  R["town/lamp"].uses = ["town/lamp", "town/lamp_lit"];
  R["town/fountain"] = function (c, V, spr) {
    draw(c, V, spr, "town/fountain", 0, 0, 20, 20);
    var h = 10 + (Math.floor(spr.t / 200) % 3);
    var g = c.createLinearGradient(0, 6 - h, 0, 6);
    g.addColorStop(0, css(T(spr, [220, 240, 255]), 0.95)); g.addColorStop(1, css(T(spr, [159, 216, 255]), 0.9));
    c.fillStyle = g;
    c.beginPath(); c.moveTo(8.8, 6); c.quadraticCurveTo(9.4, 6 - h, 10, 6 - h); c.quadraticCurveTo(10.6, 6 - h, 11.2, 6); c.closePath(); c.fill();
    for (var i = 0; i < 4; i++) {
      var ph = ((spr.t / 18 + i * 25) % 100) / 100, sx = i % 2 ? 1 : -1;
      circ(c, 10 + sx * ph * 6, 6 - h + ph * ph * (h + 6), 0.7); c.fillStyle = css(T(spr, [200, 232, 255])); c.fill();
    }
  };
  R["town/fountain"].uses = ["town/fountain"];
  R["town/tree"] = function (c, V, spr) {
    var up = Math.floor(spr.t / 300) % 2;
    c.strokeStyle = css(T(spr, [150, 140, 170])); c.lineWidth = 0.6;
    c.beginPath(); c.moveTo(16, 24); c.quadraticCurveTo(18.5, 16, 19, 11 - up); c.stroke();
    draw(c, V, spr, "town/tree", 0, 0, 24, 36);
    draw(c, V, spr, "town/balloon", 15, -1 - up, 8, 16);
  };
  R["town/tree"].uses = ["town/tree", "town/balloon"];
  R["town/funfair"] = function (c, V, spr) {
    var t = spr.t, a = (t / 30 % 360) * Math.PI / 180, cx = 72, cy = 34;
    c.lineWidth = 2.4; c.strokeStyle = css(T(spr, [206, 196, 240]));
    circ(c, cx, cy, 30); c.stroke();
    c.lineWidth = 0.9; c.strokeStyle = css(T(spr, [190, 178, 230]));
    circ(c, cx, cy, 22); c.stroke();
    for (var k = 0; k < 6; k++) {
      var ga = a + k * Math.PI / 3, gx = cx + Math.cos(ga) * 30, gy = cy + Math.sin(ga) * 30;
      c.strokeStyle = css(T(spr, [214, 206, 244])); c.lineWidth = 1.1;
      c.beginPath(); c.moveTo(cx, cy); c.lineTo(gx, gy); c.stroke();
    }
    draw(c, V, spr, "town/funfair", 0, 0, 104, 90);
    for (k = 0; k < 6; k++) { /* the gondolas hang down */
      ga = a + k * Math.PI / 3; gx = cx + Math.cos(ga) * 30; gy = cy + Math.sin(ga) * 30;
      rr(c, gx - 4.5, gy, 9, 7, 2.5);
      outlined(c, T(spr, k % 2 ? [255, 179, 199] : [159, 216, 255]), gy, gy + 7, 0.5);
      c.fillStyle = "rgba(255,255,255,0.6)"; c.fillRect(gx - 2.5, gy + 1.2, 5, 1.6);
    }
    circ(c, cx, cy, 3); outlined(c, T(spr, [255, 230, 128]), cy - 3, cy + 3, 0.5);
    for (k = 0; k < 4; k++) { /* the carousel's horses go up and down */
      var px = 12 + k * 12, up = Math.sin((t / 6 % 360) * Math.PI / 180 + k * Math.PI / 2) * 4;
      c.fillStyle = css(T(spr, [150, 140, 170])); c.fillRect(px + 1.6, 54, 1, 24);
      rr(c, px - 3, 64 + up, 10, 6, 3);
      outlined(c, T(spr, k % 2 ? [255, 255, 255] : [201, 182, 255]), 64 + up, 70 + up, 0.5);
      circ(c, px + 6, 63 + up, 2); outlined(c, T(spr, k % 2 ? [255, 255, 255] : [201, 182, 255]), 61 + up, 65 + up, 0.4);
    }
  };
  R["town/funfair"].uses = ["town/funfair"];
  R["town/sky"] = function (c, V, spr) {
    var tod = spr.v[0] | 0, w = spr.lw, h = spr.lh;
    var g = c.createLinearGradient(0, 0, 0, h);
    if (tod === 2) { /* night: the module's own sky, tinted as it tints it (the render audit: it was far darker) */
      g.addColorStop(0, css(T(spr, [196, 228, 255]))); g.addColorStop(0.5, css(T(spr, [210, 238, 255])));
      g.addColorStop(1, css(T(spr, [222, 243, 255])));
    }
    else if (tod === 1) { g.addColorStop(0, "#ffb8b0"); g.addColorStop(0.5, "#ffd2c0"); g.addColorStop(1, "#ffe8cc"); }
    else { g.addColorStop(0, "#bfe4ff"); g.addColorStop(0.55, "#d6eeff"); g.addColorStop(1, "#ecf8ff"); }
    c.fillStyle = g;
    c.fillRect(-1, -1, w + 2, h + 2);
    if (tod === 1) glow(c, w * 0.8, h * 0.95, 70, [255, 220, 150], 0.6);
  };
  R["town/cloud"] = function (c, V, spr) { /* the module's own puffs (its footprint), soft and tinted */
    var p = T(spr, [255, 255, 255]);
    c.beginPath();
    c.arc(7.5, 13.5, 6.5, 0, Math.PI * 2); c.moveTo(25, 9.5);
    c.arc(16.5, 9.5, 8.5, 0, Math.PI * 2); c.moveTo(33, 13.5);
    c.arc(26.5, 13.5, 6.5, 0, Math.PI * 2);
    c.rect(7.5, 13, 19, 7.5);
    var g = c.createLinearGradient(0, 1, 0, 20);
    g.addColorStop(0, css(p)); g.addColorStop(0.8, css(p)); g.addColorStop(1, css(mix(p, T(spr, [214, 222, 248]), 0.3)));
    c.fillStyle = g; c.fill(); /* (as white as the module's puffs: only a breath of shade at the bottom) */
  };
  R["town/hills"] = function (c, V, spr) {
    var cam = spr.v[1] | 0;
    for (var i = 0; i < 6; i++) {
      var x = i * 120 - Math.floor(cam / 3) % 120 + 20, p = T(spr, i % 2 ? [190, 230, 200] : [176, 222, 192]);
      ell(c, x, 26, 80, 26);
      var g = c.createLinearGradient(0, 0, 0, 40);
      g.addColorStop(0, css(light(p, 0.25))); g.addColorStop(1, css(deep(p, 0.08)));
      c.fillStyle = g; c.fill();
      gloss(c, x - 30, 10, 18, 3, -8, 0.25);
    }
  };
  R["town/street"] = function (c, V, spr) {
    var cam = spr.v[1] | 0, w = spr.lw, h = spr.lh;
    var g = c.createLinearGradient(0, 0, 0, 18);
    g.addColorStop(0, css(T(spr, [228, 216, 236]))); g.addColorStop(1, css(T(spr, [240, 232, 244])));
    c.fillStyle = g; c.fillRect(-1, 0, w + 2, 18.5);
    g = c.createLinearGradient(0, 18, 0, 50);
    g.addColorStop(0, css(T(spr, [252, 236, 224]))); g.addColorStop(1, css(T(spr, [248, 226, 214])));
    c.fillStyle = g; c.fillRect(-1, 18, w + 2, 32.5);
    c.fillStyle = css(T(spr, [226, 200, 190])); c.fillRect(-1, 18, w + 2, 1.6);
    for (var k = 0; k < 12; k++) { rr(c, k * 32 - cam % 32, 38, 16, 2.4, 1.2); c.fillStyle = css(T(spr, [236, 212, 202])); c.fill(); }
    g = c.createLinearGradient(0, 50, 0, h);
    g.addColorStop(0, css(T(spr, [184, 234, 198]))); g.addColorStop(1, css(T(spr, [156, 214, 174])));
    c.fillStyle = g; c.fillRect(-1, 50, w + 2, h - 49);
    c.fillStyle = css(T(spr, [150, 210, 168])); c.fillRect(-1, 50, w + 2, 1);
    for (k = 0; k < 16; k++) {
      var fx = ((k * 47 - Math.floor(cam * 5 / 4)) % 752 + 752) % 752 - 20;
      if (fx < w + 10) flowerAt(c, spr, fx, 54 + (k % 3) * 2, 2.6, k % 3 === 0 ? [255, 179, 199] : k % 3 === 1 ? [255, 230, 128] : [255, 255, 255]);
    }
  };

  /* ================================================================ the worlds' places */
  function sky2(c, w, h, a, b, split) {
    var g = c.createLinearGradient(0, 0, 0, h);
    g.addColorStop(0, a); g.addColorStop(split || 1, b);
    if (split) g.addColorStop(1, b);
    c.fillStyle = g; c.fillRect(-1, -1, w + 2, h + 2);
  }
  function hill(c, spr, x, y, rx, ry, col) {
    ell(c, x, y, rx, ry);
    var g = c.createLinearGradient(0, y - ry, 0, y + ry * 0.4), p = T(spr, col);
    g.addColorStop(0, css(light(p, 0.3))); g.addColorStop(1, css(p));
    c.fillStyle = g; c.fill();
  }
  function bgFlower(c, spr, x, y, sway, col) {
    c.strokeStyle = css(T(spr, [92, 170, 110])); c.lineWidth = 1.1; c.lineCap = "round";
    c.beginPath(); c.moveTo(x, y); c.quadraticCurveTo(x + sway * 0.2, y - 6, x + sway, y - 12); c.stroke();
    ell(c, x + 2.6, y - 5, 2, 1); c.fillStyle = css(T(spr, [120, 200, 130])); c.fill();
    flowerAt(c, spr, x + sway, y - 14, 3.3, col);
  }
  function gearPath(c, x, y, r, ang) {
    c.beginPath();
    for (var k = 0; k < 16; k++) {
      var a = ang + k * Math.PI / 8, rr_ = k % 2 ? r : r + 3;
      var a0 = a - Math.PI / 16, a1 = a + Math.PI / 16;
      if (!k) c.moveTo(x + Math.cos(a0) * rr_, y + Math.sin(a0) * rr_);
      c.lineTo(x + Math.cos(a0) * rr_, y + Math.sin(a0) * rr_); c.lineTo(x + Math.cos(a1) * rr_, y + Math.sin(a1) * rr_);
    }
    c.closePath();
    c.moveTo(x + r / 3, y); c.arc(x, y, r / 3, 0, Math.PI * 2, true);
  }
  var BG = {
    garden: function (c, spr, w, h) {
      var t = spr.t, y1 = h;
      sky2(c, w, h, "#c4e6fa", "#e2f6ec", 0.36);
      c.fillStyle = "#d6f4e2"; c.fillRect(-1, h / 3, w + 2, h);
      for (var i = 0; i < 3; i++) cloud(c, spr, ((i * 130 + 360) - Math.floor(t / 90 % 400)) - 40, 14 + (i % 2) * 14, 4);
      hill(c, spr, 60, y1 - 4, 110, 26, [186, 228, 196]);
      hill(c, spr, 260, y1 - 2, 120, 34, [196, 234, 204]);
      var fc = [[255, 179, 199], [201, 182, 255], [255, 122, 162], [255, 230, 128]];
      for (i = 0; i < 9; i++) bgFlower(c, spr, 14 + i * 37, y1 - 3, tri(t / 12 + i * 31, 90, 2), fc[i % 4]);
      var bx = Math.floor(t / 25 % 400) - 40, by = 40 + tri(t / 6, 80, 8), wing = Math.floor(t / 120) % 2 ? 3 : 1.4;
      c.fillStyle = css(T(spr, [201, 182, 255]));
      ell(c, bx - 3, by, wing, 3.4); c.fill(); ell(c, bx + 3, by, wing, 3.4); c.fill();
      rr(c, bx - 0.6, by - 2.5, 1.2, 5.5, 0.6); c.fillStyle = INK; c.fill();
    },
    post: function (c, spr, w, h) {
      var t = spr.t, y1 = h;
      sky2(c, w, h, "#cde4ff", "#e4f0ff");
      for (var i = 0; i < 2; i++) cloud(c, spr, ((i * 200 + 380) - Math.floor(t / 70 % 420)) - 40, 16 + i * 10, 4);
      var rx = [0, 52, 110, 168, 220, 272];
      for (i = 0; i < 6; i++) {
        var bh = 26 + (i * 13) % 20, x = rx[i];
        rr(c, x, y1 - bh, 48, bh + 2, 1.5); fillSoft(c, T(spr, i % 2 ? [190, 214, 244] : [200, 222, 248]), y1 - bh, y1);
        c.beginPath(); c.moveTo(x - 3, y1 - bh); c.lineTo(x + 51, y1 - bh); c.lineTo(x + 24, y1 - bh - 13); c.closePath();
        fillSoft(c, T(spr, [170, 196, 236]), y1 - bh - 13, y1 - bh);
        for (var k = 0; k < 2; k++) { rr(c, x + 10 + k * 20, y1 - bh + 8, 6, 6, 1); c.fillStyle = css(T(spr, [255, 246, 200])); c.fill(); }
      }
      var px0 = Math.floor(t / 12 % 480) - 60, py0 = 30 + tri(t / 4, 140, 10);
      c.beginPath(); c.moveTo(px0, py0 - 5); c.lineTo(px0 + 14, py0); c.lineTo(px0, py0 + 3); c.closePath();
      c.fillStyle = "#fff"; c.fill(); c.strokeStyle = css(T(spr, [159, 216, 255])); c.lineWidth = 0.8; c.stroke();
    },
    bakery: function (c, spr, w, h) {
      var t = spr.t, y1 = h;
      c.fillStyle = css(T(spr, [255, 232, 240])); c.fillRect(-1, -1, w + 2, h + 2);
      for (var i = 0; i < 11; i++) { c.fillStyle = css(T(spr, [255, 242, 247])); c.fillRect(i * 30, 0, 12, h - 30); }
      for (i = 0; i < 11; i++) { c.fillStyle = css(T(spr, [255, 220, 232])); for (var d = 0; d < h - 34; d += 14) { circ(c, i * 30 + 21, d + 8, 1.2); c.fill(); } }
      rr(c, -2, y1 - 30, w + 4, 4.5, 1.5); fillSoft(c, T(spr, [222, 170, 140]), y1 - 30, y1 - 26);
      c.fillStyle = css(T(spr, [250, 214, 220])); c.fillRect(-1, y1 - 26, w + 2, 27);
      var cc = [[159, 216, 255], [168, 230, 207], [201, 182, 255], [255, 211, 165]];
      for (i = 0; i < 4; i++) { /* steaming cups */
        var x = 30 + i * 84, y = y1 - 31;
        for (var s = 0; s < 3; s++) {
          var ph = (t / 30 + s * 40 + i * 23) % 120;
          c.globalAlpha = 0.85 * (1 - ph / 140);
          circ(c, x + tri(t / 10 + s * 37, 60, 3), y - 10 - ph / 6, 1 + ph / 50); c.fillStyle = "#fff"; c.fill();
          c.globalAlpha = 1;
        }
        c.lineWidth = 1.4; c.strokeStyle = css(T(spr, cc[i])); circ(c, x + 7, y - 4, 2.2); c.stroke();
        rr(c, x - 6, y - 8, 12, 9, 3); outlined(c, T(spr, cc[i]), y - 8, y + 1, 0.4);
        rr(c, x - 8, y, 16, 2, 1); c.fillStyle = "#fff"; c.fill();
      }
      for (i = 0; i < 3; i++) { /* cakes on the shelf */
        x = 72 + i * 84;
        rr(c, x - 9, y1 - 42, 18, 12, 3); outlined(c, T(spr, [255, 211, 165]), y1 - 42, y1 - 30, 0.4);
        rr(c, x - 9.5, y1 - 45, 19, 5.5, 2.5); fillSoft(c, T(spr, [255, 179, 199]), y1 - 45, y1 - 39);
        circ(c, x, y1 - 46.5, 2.2); fillSoft(c, T(spr, [236, 70, 90]), y1 - 49, y1 - 44); shine(c, x - 0.6, y1 - 47.2, 0.6);
      }
      for (i = 0; i < 2; i++) { /* hanging lamps */
        var lx = 90 + i * 140, sw = tri(t / 14 + i * 50, 120, 3);
        c.strokeStyle = css(T(spr, [150, 140, 170])); c.lineWidth = 0.7;
        c.beginPath(); c.moveTo(lx, 0); c.lineTo(lx + sw, 14); c.stroke();
        glow(c, lx + sw, 18, 14, [255, 236, 170], 0.5);
        c.beginPath(); c.arc(lx + sw, 18, 5.5, Math.PI, 0); c.closePath(); outlined(c, T(spr, [255, 179, 199]), 12, 18, 0.4);
        circ(c, lx + sw, 18.5, 2.2); c.fillStyle = "#fff3a0"; c.fill();
      }
    },
    clock: function (c, spr, w, h) {
      var t = spr.t, y1 = h;
      sky2(c, w, h, "#e2dcff", "#f0ecff");
      for (var i = 0; i < 7; i++) if ((Math.floor(t / 300) + i * 3) % 5) { star(c, 21 + i * 46, 7 + (i * 17) % 30, 2.2); c.fillStyle = "#fff"; c.fill(); }
      var a = (t / 30 % 360) * Math.PI / 180;
      var gs = [[26, y1 - 30, 22, a], [62, y1 - 8, 14, -a * 1.5 + 0.35], [296, 34, 18, -a]];
      for (i = 0; i < 3; i++) {
        gearPath(c, gs[i][0], gs[i][1], gs[i][2], gs[i][3]);
        var g = c.createRadialGradient(gs[i][0] - 5, gs[i][1] - 5, 2, gs[i][0], gs[i][1], gs[i][2] + 3);
        g.addColorStop(0, css(T(spr, [232, 224, 255]))); g.addColorStop(1, css(T(spr, [200, 188, 240])));
        c.fillStyle = g; c.fill("evenodd");
      }
      var sw = tri(t / 6, 200, 16);
      c.strokeStyle = css(T(spr, [200, 186, 236])); c.lineWidth = 1.2;
      c.beginPath(); c.moveTo(300, 0); c.lineTo(300 + sw, y1 - 40); c.stroke();
      circ(c, 300 + sw, y1 - 38, 6.5); fillSoft(c, T(spr, [240, 196, 80]), y1 - 45, y1 - 31, 0.6); shine(c, 298 + sw, y1 - 40, 1.4, 0.7);
    },
    shop: function (c, spr, w, h) {
      var t = spr.t, y1 = h;
      c.fillStyle = css(T(spr, [255, 240, 224])); c.fillRect(-1, -1, w + 2, h + 2);
      for (var i = 0; i < 16; i++) { circ(c, 10 + i * 20, 4, 10); fillSoft(c, T(spr, i % 2 ? [255, 255, 255] : [255, 196, 160]), -6, 14); }
      for (var s = 0; s < 2; s++) {
        var y = y1 - 20 - s * 34;
        rr(c, -2, y, w + 4, 3.2, 1); fillSoft(c, T(spr, [214, 170, 130]), y, y + 3);
        for (i = 0; i < 7; i++) {
          var x = 20 + i * 46 + s * 20;
          rr(c, x - 6, y - 14, 12, 14, 3.5); outlined(c, T(spr, s ? [206, 236, 250] : [255, 226, 236]), y - 14, y, 0.4);
          rr(c, x - 5, y - 16.5, 10, 3.2, 1); fillSoft(c, T(spr, [176, 133, 96]), y - 16, y - 13);
          c.fillStyle = "rgba(255,255,255,0.6)"; c.fillRect(x - 4, y - 11, 1.4, 8);
        }
      }
      if (Math.floor(t / 400) % 3 === 0) { star(c, 40 + Math.floor(t / 1200 % 6) * 48, y1 - 40, 3.6); c.fillStyle = "#ffe680"; c.fill(); }
    },
    art: function (c, spr, w, h) {
      var t = spr.t, y1 = h, pc = [[255, 179, 199], [159, 216, 255], [168, 230, 207], [255, 230, 128], [201, 182, 255]];
      c.fillStyle = css(T(spr, [255, 250, 232])); c.fillRect(-1, -1, w + 2, h + 2);
      for (var i = 0; i < 6; i++) { /* splats */
        var x = 24 + i * 56, y = 22 + (i * 23) % 40, r = 6 + i % 3, p = T(spr, pc[i % 5]);
        c.fillStyle = css(p, 0.85);
        circ(c, x, y, r); c.fill();
        for (var k = 0; k < 5; k++) { var a = k * 1.26 + i; circ(c, x + Math.cos(a) * r * 1.15, y + Math.sin(a) * r * 1.15, r * 0.28); c.fill(); }
      }
      for (i = 0; i < 6; i++) {
        var ph = (t / 20 + i * 53) % (h + 20);
        circ(c, 30 + i * 52 + tri(t / 9 + i * 17, 90, 4), y1 - ph, 3.2); fillSoft(c, T(spr, pc[(i + 2) % 5]), y1 - ph - 3, y1 - ph + 3);
      }
      c.strokeStyle = css(T(spr, [176, 133, 96])); c.lineWidth = 2; c.lineCap = "round";
      c.beginPath(); c.moveTo(272, y1); c.lineTo(286, y1 - 46); c.moveTo(306, y1); c.lineTo(290, y1 - 46); c.stroke();
      rr(c, 274, y1 - 46, 30, 22, 1.5); outlined(c, [255, 255, 255], y1 - 46, y1 - 24, 0.5);
      heart(c, 289, y1 - 36, 4.5); fillSoft(c, T(spr, [255, 122, 162]), y1 - 41, y1 - 31);
    },
    fair: function (c, spr, w, h) {
      var t = spr.t, y1 = h, cx = 250, cy = 56, r = 42, a = (t / 40 % 360) * Math.PI / 180;
      sky2(c, w, h, "#d4f0ff", "#ecfaff");
      c.strokeStyle = css(T(spr, [196, 220, 240])); c.lineWidth = 2.2; circ(c, cx, cy, r); c.stroke();
      c.lineWidth = 1;
      for (var k = 0; k < 8; k++) { var ga = a + k * Math.PI / 4; c.beginPath(); c.moveTo(cx, cy); c.lineTo(cx + Math.cos(ga) * r, cy + Math.sin(ga) * r); c.stroke(); }
      c.lineWidth = 2; c.beginPath(); c.moveTo(cx, cy); c.lineTo(cx - 16, y1); c.moveTo(cx, cy); c.lineTo(cx + 16, y1); c.stroke();
      for (k = 0; k < 8; k++) {
        ga = a + k * Math.PI / 4; var gx = cx + Math.cos(ga) * r, gy = cy + Math.sin(ga) * r;
        rr(c, gx - 4, gy, 8, 6.5, 2.2); outlined(c, T(spr, k % 2 ? [255, 179, 199] : [255, 230, 128]), gy, gy + 6.5, 0.4);
      }
      var fcol = [[255, 122, 162], [255, 230, 128], [159, 216, 255], [168, 230, 207], [201, 182, 255]];
      c.strokeStyle = css(T(spr, [150, 140, 170])); c.lineWidth = 0.7;
      c.beginPath(); c.moveTo(0, 2); c.quadraticCurveTo(w / 2, 6, w, 2); c.stroke();
      for (var i = 0; i < 12; i++) {
        var x = 6 + i * 27, wv = tri(t / 8 + i * 29, 80, 2), yy = 2 + 4 * Math.sin(Math.PI * x / w);
        c.beginPath(); c.moveTo(x - 6, yy); c.lineTo(x + 6, yy); c.lineTo(x + wv, yy + 11); c.closePath();
        fillSoft(c, T(spr, fcol[i % 5]), yy, yy + 11);
      }
      for (i = 0; i < 3; i++) {
        var ph = (t / 18 + i * 97) % (h + 30), bx = 30 + i * 70 + tri(t / 11 + i * 13, 100, 5), by = y1 + 10 - ph;
        c.strokeStyle = css(T(spr, [150, 140, 170])); c.lineWidth = 0.6;
        c.beginPath(); c.moveTo(bx, by + 6); c.quadraticCurveTo(bx - 2, by + 10, bx, by + 14); c.stroke();
        ell(c, bx, by, 5.4, 6.4); outlined(c, T(spr, i === 1 ? [159, 216, 255] : [255, 122, 162]), by - 6, by + 6, 0.4);
        gloss(c, bx - 2, by - 2.5, 1.2, 2, -20, 0.7);
      }
    },
    sky: function (c, spr, w, h) {
      var t = spr.t, y1 = h, x = 230;
      sky2(c, w, h, "#dcd6ff", "#f0ecff");
      for (var i = 0; i < 10; i++) if ((Math.floor(t / 250) + i * 7) % 4) { star(c, 14 + i * 31, 8 + (i * 29) % 46, 2.4 + i % 2); c.fillStyle = "#fff"; c.fill(); }
      rr(c, x, y1 - 50, 50, 36, 1.5); fillSoft(c, T(spr, [206, 200, 244]), y1 - 50, y1 - 14, 0.6);
      rr(c, x - 8, y1 - 64, 14, 50, 1.5); fillSoft(c, T(spr, [214, 208, 248]), y1 - 64, y1 - 14, 0.6);
      rr(c, x + 44, y1 - 64, 14, 50, 1.5); fillSoft(c, T(spr, [214, 208, 248]), y1 - 64, y1 - 14, 0.6);
      for (var k = 0; k < 2; k++) {
        var x0 = k ? x + 42 : x - 10;
        c.beginPath(); c.moveTo(x0, y1 - 64); c.lineTo(x0 + 18, y1 - 64); c.lineTo(x0 + 9, y1 - 79); c.closePath();
        fillSoft(c, T(spr, [255, 196, 220]), y1 - 79, y1 - 64, 0.6);
      }
      rr(c, x + 20, y1 - 34, 10, 16, 5); c.fillStyle = css(T(spr, [155, 127, 230])); c.fill();
      for (i = 0; i < 3; i++) cloud(c, spr, ((i * 150 + 380) - Math.floor(t / 60 % 440)) - 50, y1 - 10 - (i % 2) * 18, 5);
    },
    star: function (c, spr, w, h) {
      var t = spr.t, wy = Math.floor(h * 2 / 3);
      sky2(c, w, wy, "#d6e8fa", "#eef6ff");
      var g = c.createLinearGradient(0, wy, 0, h);
      g.addColorStop(0, css(T(spr, [170, 210, 240]))); g.addColorStop(1, css(T(spr, [130, 184, 228])));
      c.fillStyle = g; c.fillRect(-1, wy, w + 2, h - wy + 1);
      for (var i = 0; i < 12; i++) {
        var x = i * 30 + (t / 40 % 30) - 10, y = wy + 1 + (i % 2) * 8;
        c.strokeStyle = "rgba(255,255,255,0.9)"; c.lineWidth = 1.2; c.lineCap = "round";
        c.beginPath(); c.moveTo(x - 7, y); c.quadraticCurveTo(x, y - 2.4, x + 7, y); c.stroke();
      }
      var beam = tri(t / 5, 400, 1000);
      if (beam > 0) {
        var bg = c.createLinearGradient(38, 0, 38 + beam / 6, 0);
        bg.addColorStop(0, "rgba(255,250,200,0.95)"); bg.addColorStop(1, "rgba(255,250,200,0.6)"); /* (its tip shows: it sweeps) */
        c.fillStyle = bg; c.beginPath(); c.moveTo(38, wy - 57); c.lineTo(38 + beam / 6, wy - 66); c.lineTo(38 + beam / 6, wy - 48); c.closePath(); c.fill();
      }
      c.beginPath(); c.moveTo(27, wy - 54); c.lineTo(37, wy - 54); c.lineTo(39, wy); c.lineTo(25, wy); c.closePath();
      fillSoft(c, [255, 255, 255], wy - 54, wy, 0.6);
      c.fillStyle = css(T(spr, [255, 122, 162])); c.fillRect(25.6, wy - 40, 12.8, 8);
      rr(c, 24, wy - 61, 16, 7, 1.5); fillSoft(c, T(spr, [110, 86, 130]), wy - 61, wy - 54, 0.5);
      c.fillStyle = "#fff3a0"; c.fillRect(27, wy - 59.5, 10, 4);
      for (i = 0; i < 2; i++) {
        var bx = 150 + i * 110, by = wy + 6 + tri(t / 8 + i * 60, 100, 2);
        c.strokeStyle = INK; c.lineWidth = 0.9; c.beginPath(); c.moveTo(bx, by - 22); c.lineTo(bx, by - 5); c.stroke();
        c.beginPath(); c.moveTo(bx + 1, by - 22); c.lineTo(bx + 1, by - 6); c.lineTo(bx + 12, by - 6); c.closePath(); fillSoft(c, [255, 255, 255], by - 22, by - 6, 0.5);
        c.beginPath(); c.moveTo(bx - 14, by - 5); c.lineTo(bx + 14, by - 5); c.lineTo(bx + 9, by + 3); c.lineTo(bx - 9, by + 3); c.closePath();
        fillSoft(c, T(spr, i ? [255, 122, 162] : [255, 164, 92]), by - 5, by + 3, 0.6);
      }
      var gx = Math.floor(t / 16 % 420) - 50, gy = 20 + tri(t / 5, 120, 6);
      c.strokeStyle = INK; c.lineWidth = 1; c.lineCap = "round";
      c.beginPath(); c.moveTo(gx - 5, gy - 2); c.quadraticCurveTo(gx - 2, gy - 3, gx, gy); c.quadraticCurveTo(gx + 2, gy - 3, gx + 5, gy - 2); c.stroke();
    }
  };
  Object.keys(BG).forEach(function (k) {
    R["bg/" + k] = function (c, V, spr) {
      c.save();
      c.beginPath(); c.rect(0, 0, spr.lw, spr.lh); c.clip();
      BG[k](c, spr, spr.lw, spr.lh); /* (night and dusk: the page tints it all afterwards, like the module) */
      c.restore();
    };
  });

  /* ================================================================ games */
  R["game/bubble"] = function (c, V, spr) {
    var col = rgb565(spr.v[0]), k = spr.v[1] | 0, r = spr.v[2] | 0, t = spr.t;
    var x = spr.lw / 2, y = spr.lh / 2;
    var wv = Math.sin(((t / 5 + k * 77) % 360) * Math.PI / 180) * 2;
    var rx = r + wv, ry = r - wv;
    var g = c.createRadialGradient(x - rx * 0.35, y - ry * 0.4, r * 0.1, x, y, r * 1.05);
    g.addColorStop(0, "rgba(255,255,255,0.95)");
    g.addColorStop(0.55, css(light(col, 0.6), 0.9));
    g.addColorStop(0.86, css(light(col, 0.2), 0.95));
    g.addColorStop(1, css(col));
    ell(c, x, y, rx, ry); c.fillStyle = g; c.fill();
    c.lineWidth = Math.max(0.8, r * 0.08); c.strokeStyle = css(deep(col, 0.18), 0.9); c.stroke();
    /* a rainbow sheen */
    c.save(); ell(c, x, y, rx - 1, ry - 1); c.clip();
    var hue = c.createLinearGradient(x - r, y + r, x + r, y - r);
    hue.addColorStop(0, "rgba(255,180,220,0)"); hue.addColorStop(0.45, "rgba(180,220,255,0.28)"); hue.addColorStop(0.6, "rgba(200,255,220,0.25)"); hue.addColorStop(1, "rgba(255,240,180,0)");
    c.fillStyle = hue; c.fillRect(x - r - 3, y - r - 3, 2 * r + 6, 2 * r + 6); c.restore();
    gloss(c, x - rx / 3, y - ry / 3, r / 4 + 1, r / 5 + 0.8, -35, 0.9);
    shine(c, x + rx * 0.45, y + ry * 0.42, Math.max(0.6, r * 0.07), 0.8);
    var a = ((t / 4 + k * 90) % 360) * Math.PI / 180;
    star(c, x + Math.cos(a) * (r - 1), y + Math.sin(a) * (r - 1), 2.6, 0.4);
    c.fillStyle = Math.floor(t / 160 + k) % 2 ? "#ffffff" : "#ffe680"; c.fill();
  };
  R["game/ring"] = function (c, V, spr) {
    var col = rgb565(spr.v[0]), r = spr.lw / 2, x = r, y = r;
    circ(c, x, y, r); c.fillStyle = css(deep(col, 0.3)); c.fill();
    circ(c, x, y, r - 0.8); fillSoft(c, col, 0, 2 * r);
    circ(c, x, y, r - 3);
    var g = c.createRadialGradient(x, y - r * 0.3, 1, x, y, r - 3);
    g.addColorStop(0, "#ffffff"); g.addColorStop(1, css(light(col, 0.82)));
    c.fillStyle = g; c.fill();
    gloss(c, x - r * 0.6, y - r * 0.6, 2.2, 1.2, -45, 0.85);
  };
  R["game/kart"] = function (c, V, spr) {
    var col = rgb565(spr.v[0]), w = spr.lw, h = spr.lh, big = w > 34;
    var bx = 2, by = 2, bw = w - 4, bh = big ? 16 : 12, wr = big ? 4.4 : 3.4;
    rr(c, bx, by, bw, bh, big ? 6 : 5); outlined(c, col, by, by + bh, 0.7);
    rr(c, bx + 3, by + 2, bw - 6, 3, 1.5); c.fillStyle = "rgba(255,255,255,0.55)"; c.fill();
    circ(c, bx + bw - 4, by + bh / 2, 1.6); c.fillStyle = "#fff6c0"; c.fill();
    var wy = big ? 18 : 14, wx = big ? [8, 30] : [8, 25];
    for (var i = 0; i < 2; i++) {
      circ(c, wx[i], wy, wr); c.fillStyle = "#5a3d5c"; c.fill();
      circ(c, wx[i], wy, wr * 0.45); c.fillStyle = "#c8c8d6"; c.fill();
    }
  };
  R["game/power"] = function (c, V, spr) {
    var wand = spr.v[0] | 0;
    c.lineWidth = 2.4; c.strokeStyle = Math.floor(spr.t / 200) % 2 ? "#ff7aa2" : "#9b7fe6";
    circ(c, 12, 12, 11.4); c.stroke();
    draw(c, V, spr, wand ? "game/power_wand" : "game/power_slow", 0, 0, 24, 24);
  };
  R["game/power"].uses = ["game/power_wand", "game/power_slow"];
  R["game/pad"] = function (c, V, spr) { draw(c, V, spr, spr.v[0] ? "game/pad_gone" : "game/pad", 0, 0, 54, 30); };
  R["game/pad"].uses = ["game/pad", "game/pad_gone"];
  R["game/gate_big"] = function (c, V, spr) {
    var open = spr.v[0] | 0;
    draw(c, V, spr, "game/gate_door", 10, 13, 20 - open, 34);
    draw(c, V, spr, "game/gate_door", 32 + open, 13, 20 - open, 34);
    draw(c, V, spr, "game/gate_big_posts", 0, 0, 62, 50);
  };
  R["game/gate_big"].uses = ["game/gate_door", "game/gate_big_posts"];
  R["game/boost"] = function (c, V, spr) {
    var st = spr.v[0] | 0, glowk = spr.v[1] | 0;
    draw(c, V, spr, "game/boost_" + st, 0, 0, 58, 24);
    for (var k = 0; k < 3; k++) {
      var ax = 13 + k * 12, y = 12, on = k === glowk || st === 2;
      c.beginPath(); c.moveTo(ax, y - 6); c.lineTo(ax + 8, y + 1); c.lineTo(ax, y + 8); c.closePath();
      c.lineJoin = "round"; c.lineWidth = 1.6; c.strokeStyle = on ? "#ff7aa2" : "#ffffff"; c.stroke();
      c.fillStyle = on ? "#ff7aa2" : "#ffffff"; c.fill();
      if (on) glow(c, ax + 3, y + 1, 9, [255, 122, 162], 0.35);
    }
  };
  R["game/boost"].uses = ["game/boost_0", "game/boost_1", "game/boost_2"];
  /* the clocks, drawn to the module's own footprint (the render audit's M1): the rim (r + pad), the white
   * face (r), and the minute marks where the 1x clock has them -- dots at the rim on the big clock (its own
   * sprite, after the numerals), short strokes in from the rim on the small one -- so nothing reaches the
   * numerals' ring (r - 12) */
  function clockFace(c, spr, r, pad, rim) {
    var cx = spr.lw / 2, cy = spr.lh / 2;
    circ(c, cx, cy, r + pad); outlined(c, rim, cy - r - pad, cy + r + pad, 0.5);
    circ(c, cx, cy, r);
    var g = c.createRadialGradient(cx - r * 0.2, cy - r * 0.3, r * 0.1, cx, cy, r);
    g.addColorStop(0, "#ffffff"); g.addColorStop(1, "#f2eefc");
    c.fillStyle = g; c.fill();
    gloss(c, cx - r * 0.45, cy - r * 0.62, r * 0.2, r * 0.07, -35, 0.6);
  }
  R["game/clock_face"] = function (c, V, spr) { clockFace(c, spr, spr.v[0] | 0, 4, [155, 127, 230]); };
  R["game/clock_ticks"] = function (c, V, spr) {
    var r = spr.v[0] | 0, cx = spr.lw / 2, cy = spr.lh / 2;
    for (var k = 0; k < 60; k++) {
      var a = k * Math.PI / 30, big = k % 5 === 0, rr0 = r - 3 + (big ? 0.5 : 0);
      circ(c, cx + Math.sin(a) * rr0 + 0.5, cy - Math.cos(a) * rr0 + 0.5, big ? 1.25 : 0.6);
      c.fillStyle = big ? INK : "#968299"; c.fill();
    }
  };
  R["game/clock_small"] = function (c, V, spr) {
    var r = spr.v[0] | 0, cx = spr.lw / 2, cy = spr.lh / 2;
    clockFace(c, spr, r, 3, [201, 182, 255]);
    c.lineCap = "round";
    for (var k = 0; k < 60; k++) {
      var a = k * Math.PI / 30, l = k % 5 ? 2 : 5, s = Math.sin(a), co = Math.cos(a);
      c.strokeStyle = k % 5 ? "#d9d9e0" : INK; c.lineWidth = k % 5 ? 0.7 : 1.2;
      c.beginPath(); c.moveTo(cx + s * (r - l), cy - co * (r - l)); c.lineTo(cx + s * (r - 0.8), cy - co * (r - 0.8)); c.stroke();
    }
  };
  R["game/clock_hands"] = function (c, V, spr) {
    var r = spr.v[0] | 0, ha = (spr.v[1] | 0) * Math.PI / 180, ma = (spr.v[2] | 0) * Math.PI / 180, big = spr.v[3] | 0;
    var cx = spr.lw / 2, cy = spr.lh / 2, hl = big ? 34 : r / 2, ml = big ? 54 : r - 6;
    function hand(a, len, wd, col) {
      c.lineCap = "round";
      c.strokeStyle = INK; c.lineWidth = wd + 1.2;
      c.beginPath(); c.moveTo(cx - Math.sin(a) * 3, cy + Math.cos(a) * 3); c.lineTo(cx + Math.sin(a) * len, cy - Math.cos(a) * len); c.stroke();
      c.strokeStyle = col; c.lineWidth = wd;
      c.beginPath(); c.moveTo(cx - Math.sin(a) * 3, cy + Math.cos(a) * 3); c.lineTo(cx + Math.sin(a) * len, cy - Math.cos(a) * len); c.stroke();
    }
    hand(ha, hl, big ? 3.6 : 2.4, "#7a5a8c");
    hand(ma, ml, big ? 2.6 : 1.8, "#ff7aa2");
    circ(c, cx, cy, big ? 4.4 : 3.2); fillSoft(c, [255, 230, 128], cy - 4, cy + 4, 0.8);
    if (big) { /* grab me */
      var gx = cx + Math.sin(ma) * 54, gy = cy - Math.cos(ma) * 54;
      glow(c, gx, gy, 10, [255, 122, 162], 0.35);
      circ(c, gx, gy, 5.4); fillSoft(c, [255, 122, 162], gy - 5, gy + 5, 0.9);
      shine(c, gx - 1.6, gy - 1.8, 1.2);
    }
  };
  R["game/cake"] = function (c, V, spr) {
    var n = spr.v[0] | 0;
    for (var i = 0; i < n; i++) draw(c, V, spr, i % 2 ? "game/cake_pink" : "game/cake_cream", 16 - 2 * i, 64 - 9 * i, 48 - 4 * i, 9);
    if (n) { /* the cherry */
      var y = 60 - 9 * n;
      circ(c, 30, y, 3.4); fillSoft(c, [236, 70, 80], y - 3, y + 3, 0.8); shine(c, 29, y - 1.2, 0.9);
      c.strokeStyle = "#5cbf8a"; c.lineWidth = 0.9; c.beginPath(); c.moveTo(30, y - 3); c.quadraticCurveTo(31, y - 6, 33, y - 7); c.stroke();
    }
  };
  R["game/cake"].uses = ["game/cake_pink", "game/cake_cream"];
  R["game/oven"] = function (c, V, spr) { draw(c, V, spr, spr.v[0] ? "game/oven_hot" : "game/oven", 0, 0, 78, 72); };
  R["game/oven"].uses = ["game/oven", "game/oven_hot"];
  R["game/till"] = function (c, V, spr) { draw(c, V, spr, spr.v[0] ? "game/till_pets" : "game/till", 0, 0, spr.lw, spr.lh); };
  R["game/till"].uses = ["game/till", "game/till_pets"];
  R["game/bowl"] = function (c, V, spr) { var p = spr.v[0] | 0; draw(c, V, spr, "game/bowl", -p, 0, 108 + 2 * p, 36); };
  R["game/bowl"].uses = ["game/bowl"];
  /* the story's things in their colours */
  [["bead", 5], ["balloon", 5], ["card", 5], ["flower", 2], ["marble", 2]].forEach(function (e) {
    var id = "stage/" + e[0], n = e[1], uses = [id];
    for (var i = 1; i < n; i++) uses.push(id + "_" + i);
    R[id] = function (c, V, spr) { var k = (spr.v[0] | 0) % n; draw(c, V, spr, k ? id + "_" + k : id, 0, 0, 16, 17); };
    R[id].uses = uses;
  });

  /* ================================================================ squishies */
  R["squishy"] = function (c, V, spr) {
    var v = spr.v, kind = v[0], col = rgb565(v[1]), sparkly = v[2], squeezed = v[3], blink = v[4];
    var cx = spr.lw / 2, cy = spr.lh / 2, fx = cx + v[5], fy = cy + v[6], g = v[7], n = (v.length - 8) >> 1;
    if (n < 3) return;
    var pts = [], minY = 1e9, maxY = -1e9, minX = 1e9, maxX = -1e9;
    for (var i = 0; i < n; i++) {
      var p = [cx + v[8 + 2 * i] + 0.5, cy + v[9 + 2 * i] + 0.5];
      pts.push(p);
      minY = Math.min(minY, p[1]); maxY = Math.max(maxY, p[1]); minX = Math.min(minX, p[0]); maxX = Math.max(maxX, p[0]);
    }
    var s = (maxX - minX) / 64; /* about 1 for the big one, 1/4 on the shelf */
    smooth(c, pts);
    c.lineWidth = Math.max(0.9, 2.2 * s); c.strokeStyle = INK; c.lineJoin = "round"; c.stroke();
    var gr = c.createRadialGradient(cx - (maxX - minX) * 0.18, minY + (maxY - minY) * 0.3, 1, cx, cy, (maxX - minX) * 0.62);
    gr.addColorStop(0, css(light(col, 0.55))); gr.addColorStop(0.65, css(col)); gr.addColorStop(1, css(deep(col, 0.16)));
    c.fillStyle = gr; c.fill();
    c.save(); smooth(c, pts); c.clip();
    gloss(c, cx - (maxX - minX) * 0.22, minY + (maxY - minY) * 0.22, (maxX - minX) * 0.14, (maxY - minY) * 0.07, -30, 0.7);
    shine(c, cx - (maxX - minX) * 0.05, minY + (maxY - minY) * 0.12, Math.max(0.5, 1.6 * s), 0.85);
    if (kind === 5 || kind === 2) { /* bear / cat ears: inner pink */
      for (var e = -1; e <= 1; e += 2) {
        var ex = cx + e * (maxX - minX) * 0.3, ey = minY + (maxY - minY) * 0.1;
        circ(c, ex, ey, 4.5 * s); c.fillStyle = css(mix(col, [255, 150, 170], 0.5), 0.7); c.fill();
      }
    }
    if (kind === 6) { /* strawberry seeds: black spots (the module's), and the leafy top */
      c.fillStyle = "rgba(40,30,40,0.9)";
      for (var k = 0; k < 7; k++) { ell(c, cx - 14 * s + (k % 4) * 9 * s + (k > 3 ? 4 * s : 0), cy + (k > 3 ? 10 : 2) * s, 1.1 * s, 1.6 * s); c.fill(); }
    }
    c.restore();
    if (kind === 6) { /* the strawberry's leafy green top (as the module's) */
      for (var lf = -2; lf <= 2; lf++) {
        c.save(); c.translate(cx + lf * 4.2 * s, minY + 2.2 * s); c.rotate(lf * 0.45);
        ell(c, 0, 0, 2.2 * s, 4 * s); c.fillStyle = "#4fb35e"; c.fill(); c.restore();
      }
    }
    /* the face */
    var lw = Math.max(0.7, 1.4 * s);
    c.strokeStyle = INK; c.lineCap = "round"; c.lineJoin = "round"; c.lineWidth = lw;
    if (squeezed) {
      for (var sx = -1; sx <= 1; sx += 2) {
        var x0 = fx + sx * (g + 2), x1 = fx + sx * (g - 1);
        c.beginPath(); c.moveTo(x0, fy - 3 * Math.max(s, 0.4)); c.lineTo(x1, fy - 1 * Math.max(s, 0.4)); c.lineTo(x0, fy + 1 * Math.max(s, 0.4)); c.stroke();
      }
      ell(c, fx, fy + 5 * s + 1, 2.2 * Math.max(s, 0.5), 2.6 * Math.max(s, 0.5)); c.fillStyle = "#c4426c"; c.fill();
    } else {
      for (sx = -1; sx <= 1; sx += 2) {
        var exx = fx + sx * g;
        if (blink) { c.beginPath(); c.arc(exx, fy - 0.5, 1.8 * Math.max(s, 0.5), 0.15 * Math.PI, 0.85 * Math.PI); c.stroke(); }
        else {
          var er = Math.max(0.9, 2.1 * s);
          ell(c, exx, fy, er, er * 1.25); c.fillStyle = INK; c.fill();
          shine(c, exx - er * 0.35, fy - er * 0.45, er * 0.42, 1);
        }
      }
      c.beginPath(); c.moveTo(fx - 2.2 * Math.max(s, 0.45), fy + 4 * Math.max(s, 0.45));
      c.quadraticCurveTo(fx, fy + 6.2 * Math.max(s, 0.45), fx + 2.2 * Math.max(s, 0.45), fy + 4 * Math.max(s, 0.45)); c.stroke();
    }
    for (sx = -1; sx <= 1; sx += 2) {
      var bxx = fx + sx * (g + 4 * Math.max(s, 0.4));
      var bg = c.createRadialGradient(bxx, fy + 4 * Math.max(s, 0.4), 0, bxx, fy + 4 * Math.max(s, 0.4), 3.4 * Math.max(s, 0.4));
      bg.addColorStop(0, "rgba(255,140,165,0.75)"); bg.addColorStop(1, "rgba(255,140,165,0)");
      c.fillStyle = bg; circ(c, bxx, fy + 4 * Math.max(s, 0.4), 3.4 * Math.max(s, 0.4)); c.fill();
    }
    if (sparkly) for (i = 0; i < 3; i++) {
      var sx2 = cx + (i - 1) * 22 * s * 1.0, sy2 = cy - 30 * s + ((spr.t / 160 + i) % 3 | 0);
      star(c, sx2, sy2, Math.max(1.6, 3.6 * s), 0.42); fillSoft(c, [255, 230, 128], sy2 - 3, sy2 + 3, 0.5);
    }
  };

  /* ================================================================ walls and floors */
  function patFill(c, spr, x, y, w, h, p) {
    var g = c.createLinearGradient(0, y, 0, y + h);
    g.addColorStop(0, css(light(p, 0.12))); g.addColorStop(1, css(deep(p, 0.04)));
    c.fillStyle = g; c.fillRect(x, y, w, h);
  }
  var PAT = {
    stripes: function (c, s, w, h, C) { for (var i = 4; i < w; i += 20) { c.fillStyle = css(C[1]); c.fillRect(i, 0, 8, h); c.fillStyle = css(light(C[1], 0.35)); c.fillRect(i + 1.5, 0, 1.2, h); } },
    dots: function (c, s, w, h, C) { for (var j = 0; j * 16 < h; j++) for (var i = 0; i * 20 < w + 20; i++) { circ(c, i * 20 + (j % 2) * 10 + 4.5, j * 16 + 7.5, 2.4); c.fillStyle = css((i + j) % 2 ? C[2] : C[1]); c.fill(); shine(c, i * 20 + (j % 2) * 10 + 3.8, j * 16 + 6.7, 0.6, 0.7); } },
    checks: function (c, s, w, h, C) { for (var j = 0; j * 16 < h; j++) for (var i = 0; i * 16 < w; i++) if ((i + j) % 2) { rr(c, i * 16 + 0.5, j * 16 + 0.5, 15, Math.min(16, h - j * 16) - 1, 2); c.fillStyle = css(C[1]); c.fill(); } },
    hearts: function (c, s, w, h, C) { for (var j = 0; j * 20 < h; j++) for (var i = 0; i * 24 < w + 24; i++) { var x = i * 24 + (j % 2) * 12 + 6, y = j * 20 + 9; heart(c, x, y, 5.2); fillSoft(c, C[1], y - 6, y + 5); shine(c, x - 2.4, y - 2.6, 0.8, 0.7); } },
    clouds: function (c, s, w, h, C, seed) { for (var i = 0; i < 4; i++) { var cx = 30 + i * 80 + (seed * 13 % 20), cy = 14 + (i % 2) * 30; c.fillStyle = css(C[1]); circ(c, cx, cy, 8); c.fill(); circ(c, cx + 10, cy + 3, 7); c.fill(); circ(c, cx - 10, cy + 3, 6); c.fill(); gloss(c, cx - 2, cy - 4, 3, 1.4, -10, 0.5); } },
    panels: function (c, s, w, h, C) { c.fillStyle = css(C[1]); c.fillRect(0, h - h / 3, w, 2); for (var i = 0; i * 32 < w; i++) { rr(c, i * 32 + 4, h - h / 3 + 6, 24, h / 3 - 10, 2.5); c.lineWidth = 1.2; c.strokeStyle = css(C[1]); c.stroke(); } },
    stars: function (c, s, w, h, C) { for (var j = 0; j * 20 < h; j++) for (var i = 0; i * 26 < w + 26; i++) { var r = (i + j) % 3 ? 2.6 : 3.6; star(c, i * 26 + (j % 2) * 13 + 5, j * 20 + 8, r); c.fillStyle = css(C[1]); c.fill(); } },
    scallops: function (c, s, w, h, C) { c.lineWidth = 1.2; c.strokeStyle = css(C[1]); for (var j = 0; j * 14 < h; j++) for (var i = 0; i * 16 < w + 16; i++) { c.beginPath(); c.arc(i * 16 + (j % 2) * 8, j * 14, 7, 0, Math.PI); c.stroke(); } },
    bricks: function (c, s, w, h, C) { for (var j = 0; j * 12 < h; j++) for (var i = -1; i * 24 < w + 24; i++) { rr(c, i * 24 + (j % 2) * 12 + 0.6, j * 12 + 0.6, 22.8, 10.8, 1.6); c.fillStyle = css(light(C[0], 0.12)); c.fill(); c.lineWidth = 0.8; c.strokeStyle = css(C[1]); c.stroke(); } },
    skyfence: function (c, s, w, h, C) {
      for (var i = 0; i < 3; i++) { var cx = 40 + i * 110, cy = 16 + (i % 2) * 12; c.fillStyle = css(C[1]); circ(c, cx, cy, 8); c.fill(); circ(c, cx + 10, cy + 3, 7); c.fill(); circ(c, cx - 10, cy + 3, 6); c.fill(); }
      rr(c, -1, h - 22, w + 2, 4, 1.5); fillSoft(c, C[2], h - 22, h - 18, 0.5); rr(c, -1, h - 10, w + 2, 4, 1.5); fillSoft(c, C[2], h - 10, h - 6, 0.5);
      for (i = 0; i * 14 < w; i++) { c.beginPath(); c.moveTo(i * 14 + 2, h + 1); c.lineTo(i * 14 + 2, h - 30); c.lineTo(i * 14 + 6, h - 34); c.lineTo(i * 14 + 10, h - 30); c.lineTo(i * 14 + 10, h + 1); c.closePath(); fillSoft(c, C[2], h - 34, h, 0.6); }
    },
    leaves: function (c, s, w, h, C) { for (var i = 0; i * 40 < w; i++) { c.strokeStyle = css(C[2]); c.lineWidth = 1.2; c.beginPath(); c.moveTo(i * 40 + 20, 0); c.quadraticCurveTo(i * 40 + 22, h / 2, i * 40 + 14, h); c.stroke(); for (var j = 0; j * 18 < h; j++) { var lx = i * 40 + 20 - j + ((j % 2) ? 5 : -5), ly = j * 18 + 8; c.save(); c.translate(lx, ly); c.rotate((j % 2 ? 0.6 : -0.6)); ell(c, 0, 0, 4.4, 2.4); c.fillStyle = css(C[1]); c.fill(); c.restore(); } } },
    planks: function (c, s, w, h, C) { for (var j = 0; j * 10 < h; j++) { c.fillStyle = css(C[1]); c.fillRect(0, j * 10, w, 1); for (var i = 0; i * 48 < w + 48; i++) c.fillRect(i * 48 + (j % 3) * 16, j * 10, 1, Math.min(10, h - j * 10)); c.fillStyle = css(light(C[0], 0.25)); c.fillRect(0, j * 10 + 2, w, 1.2); } },
    tiles: function (c, s, w, h, C) { for (var j = 0; j * 16 < h; j++) for (var i = 0; i * 16 < w; i++) { rr(c, i * 16 + 0.8, j * 16 + 0.8, 14.4, 14.4, 2.4); c.fillStyle = css(light(C[0], 0.16)); c.fill(); c.lineWidth = 0.8; c.strokeStyle = css(C[1]); c.stroke(); } },
    carpet: function (c, s, w, h, C) { c.fillStyle = css(C[1]); for (var j = 0; j * 6 < h; j++) for (var i = 0; i * 8 < w; i++) { circ(c, i * 8 + (j % 2) * 4 + 3, j * 6 + 3, 1.1); c.fill(); } },
    grass: function (c, s, w, h, C) { c.strokeStyle = css(C[1]); c.lineWidth = 1; c.lineCap = "round"; for (var j = 0; j * 8 < h; j++) for (var i = 0; i * 10 < w; i++) { var x = i * 10 + (j % 2) * 5 + 0.5, y = j * 8 + 5; c.beginPath(); c.moveTo(x - 1, y); c.lineTo(x - 1.6, y - 3); c.moveTo(x + 0.6, y); c.lineTo(x + 1.4, y - 3.2); c.stroke(); } },
    cloudfloor: function (c, s, w, h, C) { c.fillStyle = css(C[1]); for (var i = 0; i * 30 < w + 30; i++) { circ(c, i * 30, 6 + (i % 2) * 4, 10.5); c.fill(); } for (i = 0; i * 40 < w; i++) { circ(c, i * 40 + 20, h - 10, 7.5); c.fill(); gloss(c, i * 40 + 17, h - 14, 2.4, 1.2, -10, 0.5); } },
    stone: function (c, s, w, h, C) { for (var j = 0; j * 14 < h; j++) for (var i = -1; i * 30 < w + 30; i++) { rr(c, i * 30 + (j % 2) * 15 + (j * 3) % 7 + 0.8, j * 14 + 0.8, 28.4, 12.4, 4); c.fillStyle = css(light(C[0], 0.1)); c.fill(); c.lineWidth = 0.8; c.strokeStyle = css(C[1]); c.stroke(); } },
    bark: function (c, s, w, h, C) { c.fillStyle = css(C[1]); for (var i = 0; i * 12 < w; i++) { rr(c, i * 12 + (i % 3) * 2, 0, 2, h, 1); c.fill(); } c.lineWidth = 1.2; c.strokeStyle = css(C[1]); for (i = 0; i < 3; i++) { ell(c, 50 + i * 100, 20 + (i % 2) * 30, 4.5, 3.4); c.stroke(); } }
  };
  Object.keys(PAT).forEach(function (k) {
    R["pat/" + k] = function (c, V, spr) {
      var C = [rgb565(spr.v[0]), rgb565(spr.v[1]), rgb565(spr.v[2])], w = spr.lw, h = spr.lh;
      c.save();
      c.beginPath(); c.rect(0, 0, w, h); c.clip();
      patFill(c, spr, -1, -1, w + 2, h + 2, C[0]);
      PAT[k](c, spr, w, h, C, spr.v[3] | 0);
      c.restore();
    };
  });

  /* ================================================================ buttons */
  R["ui/button"] = function (c, V, spr) {
    var col = rgb565(spr.v[0]), dy = spr.v[1] | 0, w = spr.lw, h = spr.lh - 2;
    rr(c, 0.3, 2, w - 0.6, h, 8); c.fillStyle = css(deep(col, 0.32)); c.fill();
    rr(c, 0.3, dy + 0.3, w - 0.6, h - 1.3, 8);
    var g = c.createLinearGradient(0, dy, 0, dy + h);
    g.addColorStop(0, css(light(col, 0.45))); g.addColorStop(0.5, css(col)); g.addColorStop(1, css(deep(col, 0.06)));
    c.fillStyle = g; c.fill();
    c.lineWidth = 0.8; c.strokeStyle = css(deep(col, 0.3), 0.7); c.stroke();
    rr(c, 6, dy + 2, w - 12, Math.min(5, h * 0.22), 2.5); c.fillStyle = "rgba(255,255,255,0.6)"; c.fill();
  };

  /* the town's rigs tint themselves (T(spr, ...), lit windows and lamps stay bright at night); every
   * other rig is drawn as by day and tinted by the page afterwards (sparkle_page.js _rigTinted) */
  Object.keys(R).forEach(function (id) { if (id.indexOf("town/") === 0) R[id].ownTint = true; });
})(typeof window !== "undefined" ? window : this);
