/* Render audit, the in-page half (firmware/docs/audit/render/tools; run by render_audit.js).
 *
 * Injected into the standalone Sparkle Town page (public or private build) once it is ready. The
 * page's own animation loop is held (render_audit.js replaces requestAnimationFrame before the
 * page loads), so the module only moves when the harness says so.
 *
 * One capture of the current scene renders it three ways from the same module state:
 *   A  the 1x framebuffer (what the calculator / simulator shows; the module draws it always);
 *   C  the Mac page as it is: the module's drawing commands run by the page's Renderer, with the
 *      vector art and the high-resolution atlas registered (sprites and pictures substituted);
 *   B  the same commands with no art registered: every bracket falls back to the module's own
 *      shapes, rectangles and text -- "the 1x intent at high resolution".
 * and compares them:
 *   hook   A vs B: the hr hooks describe what the module drew (shape geometry, text);
 *   subst  C vs B per substituted element (sprite / picture): presence, position, tint; the
 *          module's commands each substitution swallowed (aligning the C and B streams);
 *   text   every text the page draws: squeezed by maxWidth, out of its clip band or the screen,
 *          overlapping another text, hidden by later drawing;
 *   blank  pixels nothing drew (the canvas is cleared to magenta first);
 *   occl   things drawn later over texts, buttons and pictures, verified by re-rendering the
 *          stream without the occluder (the visible share lost).
 * Exposes window.RA.
 */
(function () {
  "use strict";
  const SP = window.Sparkle, M = SP.sim.M, R = SP.renderer;
  const W = 320, H = 240;
  const S = SP.S, L = SP.L, K = L.k, Z = K * S; /* device px per physical px; per logical px */
  const CW = R.ctx.canvas.width, CH = R.ctx.canvas.height;
  const C_FILL = 1, C_LINE = 2, C_CLIP = 3, C_SHAPE = 4, C_TEXT = 5, C_IMAGE = 6, C_COPY = 7, C_SCENE = 8, C_SPRITE = 9, C_SOFTBAR = 10;
  const OPN = ["", "fill", "line", "clip", "shape", "text", "image", "copy", "scene", "sprite", "softbar"];

  /* ---------------------------------------------------------------- the module's commands */
  function take() {
    const lp = M._malloc(4);
    const p = M._web_sparkle_take(lp), n = new Uint32Array(M.HEAPU8.buffer, lp, 1)[0];
    M._free(lp);
    return n ? new Int32Array(M.HEAPU8.buffer.slice(p, p + n * 4)) : new Int32Array(0);
  }
  /* the registration of the page's art (as the page's registerArt) */
  function artHashes() {
    let hs = [];
    R.arts.forEach(e => { if (e.img) Object.keys(e.sprites).forEach(h => hs.push(parseInt(h, 16) >>> 0)); });
    if (SP.vec) hs = hs.concat(SP.vec.pictureHashes());
    return hs;
  }
  function register(on) {
    const hs = on ? artHashes() : [], sv = on && SP.vec ? SP.vec.spriteHashes() : [];
    const send = (fn, arr) => {
      const q = M._malloc(Math.max(4, arr.length * 4));
      new Uint32Array(M.HEAPU8.buffer, q, arr.length).set(arr);
      fn(q, arr.length);
      M._free(q);
    };
    send(M._web_sparkle_vec.bind(M), sv);
    send(M._web_sparkle_art.bind(M), hs);
  }
  function redrawTake() {
    take();
    M._web_sparkle_redraw();
    return take();
  }

  /* parse a command stream into records {op, i, len, box (logical x0 y0 x1 y1), sig, ...} */
  const dec = new TextDecoder("utf-8");
  function parse(cmd) {
    const out = [];
    let i = 0;
    const n = cmd.length, P = v => v / S, P16 = v => v / 16 / S;
    while (i < n) {
      const op = cmd[i];
      let len = 0, r = { op: op, i: i };
      switch (op) {
      case C_FILL: len = 6; r.box = [P(cmd[i + 1]), P(cmd[i + 2]), P(cmd[i + 1] + cmd[i + 3]), P(cmd[i + 2] + cmd[i + 4])]; r.c = cmd[i + 5]; break;
      case C_LINE: len = 6; r.box = [P(Math.min(cmd[i + 1], cmd[i + 3])), P(Math.min(cmd[i + 2], cmd[i + 4])), P(Math.max(cmd[i + 1], cmd[i + 3]) + 1), P(Math.max(cmd[i + 2], cmd[i + 4]) + 1)]; r.c = cmd[i + 5]; break;
      case C_CLIP: len = 5; r.clip = [P(cmd[i + 1]), P(cmd[i + 2]), P(cmd[i + 3]), P(cmd[i + 4])]; break;
      case C_SHAPE: {
        len = 2 + cmd[i + 1];
        const a = cmd.subarray(i + 2, i + len), kind = a[0];
        r.kind = kind, r.c = a[1], r.band = [P(a[2]), P(a[3])];
        if (kind === 1 || kind === 2) { const cx = P16(a[4]), cy = P16(a[5]), rr = P16(a[6]); r.box = [cx - rr, cy - rr, cx + rr, cy + rr]; }
        else if (kind === 3) r.box = [P16(a[4]), P16(a[5]), P16(a[4] + a[6]), P16(a[5] + a[7])];
        else if (kind === 4) { const t = P16(a[8]) / 2; r.box = [Math.min(P16(a[4]), P16(a[6])) - t, Math.min(P16(a[5]), P16(a[7])) - t, Math.max(P16(a[4]), P16(a[6])) + t, Math.max(P16(a[5]), P16(a[7])) + t]; }
        else if (kind === 5) {
          let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
          for (let j = 0; j < a[4]; j++) { const x = P16(a[5 + 2 * j]), y = P16(a[6 + 2 * j]); x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y); }
          r.box = [x0, y0, x1, y1];
        }
        break;
      }
      case C_TEXT: {
        const nb = cmd[i + 1];
        len = 10 + Math.floor((nb + 4) / 4);
        r.st = cmd[i + 2], r.c = cmd[i + 3], r.band = [P(cmd[i + 4]), P(cmd[i + 5])];
        r.box = [P16(cmd[i + 6]), P16(cmd[i + 7]), P16(cmd[i + 6] + cmd[i + 8]), P16(cmd[i + 7] + cmd[i + 9])];
        r.s = dec.decode(new Uint8Array(cmd.buffer, cmd.byteOffset + (i + 10) * 4, nb));
        break;
      }
      case C_IMAGE: {
        len = 14;
        const a = cmd.subarray(i + 2, i + 14);
        r.tint = a[11];
        r.hash = ("00000000" + (cmd[i + 1] >>> 0).toString(16)).slice(-8);
        r.sil = a[0];
        const X = P16(a[3]), Y = P16(a[4]), p = a[5] / 16 / S, w = a[6], h = a[7], rot = a[8] & 3;
        const dw = (rot & 1 ? h : w) * p, dh = (rot & 1 ? w : h) * p;
        r.box = [X, Y, X + dw, Y + dh]; /* (as the page draws it: from X rightward, mirrored or not) */
        r.band = [P(a[1]), P(a[2])];
        break;
      }
      case C_COPY: { const w = cmd[i + 3], h = cmd[i + 4]; len = 5 + Math.floor((w * h + 1) / 2); r.box = [cmd[i + 1], cmd[i + 2], cmd[i + 1] + w, cmd[i + 2] + h]; break; }
      case C_SCENE: len = 5; break;
      case C_SPRITE: {
        len = 2 + cmd[i + 1];
        const b = cmd.subarray(i + 3, i + len);
        r.hash = cmd[i + 2] >>> 0;
        r.box = [P16(b[3]), P16(b[4]), P16(b[3] + b[5]), P16(b[4] + b[6])];
        r.tint = b[9], r.t = b[8] >>> 0, r.band = [P(b[1]), P(b[2])];
        r.params = Array.from(b.subarray(13, 11 + b[10]));
        break;
      }
      case C_SOFTBAR: len = 6; r.box = [cmd[i + 1], cmd[i + 2], cmd[i + 1] + cmd[i + 3], cmd[i + 2] + cmd[i + 4]]; break;
      default: throw new Error("bad command " + op + " at " + i);
      }
      r.len = len;
      r.sig = op === C_COPY ? "7:" + Array.from(cmd.subarray(i + 1, i + 5)).join(",") : Array.from(cmd.subarray(i, i + len)).join(",");
      out.push(r);
      i += len;
    }
    return out;
  }

  /* names */
  const vecIdOfHashHex = h => (SP.vec && SP.vec.byHash[h]) || null;
  function spriteName(h) {
    const V = SP.vec;
    if (!V) return null;
    if (V.bySprite[h]) return V.bySprite[h];
    for (const id in window.SparkleVector.rigs) if (window.SparkleVector.fnv(id) === h) return id;
    return null;
  }
  function label(r) {
    if (r.op === C_TEXT) return "text \"" + r.s + "\"";
    if (r.op === C_SPRITE) return "sprite " + (spriteName(r.hash) || r.hash.toString(16));
    if (r.op === C_IMAGE) return "picture " + (vecIdOfHashHex(r.hash) || ("#" + r.hash + (atlasHas(r.hash) ? " (atlas)" : "")));
    if (r.op === C_SHAPE || r.op === C_FILL) { /* a shape: its kind, colour and size (logical) */
      const v = r.c & 0xffff, rgb = (((v >> 11) & 31) * 255 / 31 << 16 | ((v >> 5) & 63) * 255 / 63 << 8 | (v & 31) * 255 / 31) >>> 0;
      const nm = r.op === C_FILL ? "rect" : ["", "circle", "ring", "rrect", "line", "poly"][r.kind] || "shape";
      return nm + " #" + ("000000" + rgb.toString(16)).slice(-6) + " " + Math.round(r.box[2] - r.box[0]) + "x" + Math.round(r.box[3] - r.box[1]) +
        "@" + Math.round(r.box[0]) + "," + Math.round(r.box[1]);
    }
    if (r.op === C_COPY) return "core pixels";
    if (r.op === C_SOFTBAR) return "soft-key bar";
    return OPN[r.op];
  }
  function atlasHas(h) { return R.arts.some(a => a.sprites[h]); }

  /* align C (substituted) with B (all shapes): each C sprite / picture -> the B commands it replaced */
  function align(cr, br) {
    const keep = r => r.op !== C_CLIP && r.op !== C_SCENE;
    const c = cr.filter(keep), b = br.filter(keep);
    const sub = r => r.op === C_SPRITE || r.op === C_IMAGE;
    let i = 0, j = 0, desync = 0;
    const runs = new Map(), where = [];
    while (i < c.length && j <= b.length) {
      if (j < b.length && c[i].sig === b[j].sig) { i++, j++; continue; }
      if (sub(c[i])) {
        let k = i;
        while (k < c.length && sub(c[k])) k++;
        let j2 = b.length;
        if (k < c.length) {
          j2 = -1;
          for (let t = j; t < b.length; t++)
            if (b[t].sig === c[k].sig && (k + 1 >= c.length || t + 1 >= b.length || sub(c[k + 1]) || b[t + 1].sig === c[k + 1].sig)) { j2 = t; break; }
          if (j2 < 0) { desync++; if (where.length < 4) where.push("no anchor after " + label(c[i]) + " for " + label(c[k]) + " " + c[k].sig.slice(0, 60)); i = k; continue; }
        }
        const group = c.slice(i, k);
        group.forEach(g => runs.set(g, []));
        for (let t = j; t < j2; t++) { /* to the substitution whose box holds it (else the first) */
          const r = b[t];
          let best = group[0];
          if (group.length > 1 && r.box) {
            const cx = (r.box[0] + r.box[2]) / 2, cy = (r.box[1] + r.box[3]) / 2;
            for (const g of group) if (g.box && cx >= g.box[0] && cx <= g.box[2] && cy >= g.box[1] && cy <= g.box[3]) { best = g; break; }
          }
          runs.get(best).push(r);
        }
        i = k, j = j2;
        continue;
      }
      /* a mismatch outside a substitution: resynchronise */
      desync++;
      if (where.length < 4) where.push("C " + label(c[i]) + " " + c[i].sig.slice(0, 60) + " vs B " + (b[j] ? label(b[j]) + " " + b[j].sig.slice(0, 60) : "end"));
      let found = -1;
      for (let t = j; t < Math.min(b.length, j + 4000); t++) if (b[t].sig === c[i].sig) { found = t; break; }
      if (found >= 0) j = found; else i++;
    }
    return { runs: runs, desync: desync, where: where };
  }

  /* ---------------------------------------------------------------- rendering */
  const TEXTLOG = [];
  let textRec = null;
  function hookContext(ctx) {
    if (ctx.__ra) return;
    ctx.__ra = true;
    const ft = ctx.fillText;
    ctx.fillText = function (s, x, y, maxW) {
      if (textRec) {
        const tw = this.measureText(s).width;
        TEXTLOG.push({ s: s, font: this.font, tw: tw, maxW: maxW === undefined ? null : maxW, x: x, y: y, a: textRec });
      }
      return ft.apply(this, arguments);
    };
  }
  const origText = SP.Renderer.prototype._text;
  SP.Renderer.prototype._text = function (a, s) {
    textRec = Array.from(a);
    try { return origText.call(this, a, s); } finally { textRec = null; }
  };

  const cB = document.createElement("canvas"); cB.width = CW, cB.height = CH;
  const scratchB = document.createElement("canvas"); scratchB.width = W, scratchB.height = H;
  const RB = new SP.Renderer(cB.getContext("2d"), { scratch: scratchB, vec: null });
  RB.setScale(S, K);
  const cX = document.createElement("canvas"); cX.width = CW, cX.height = CH; /* occlusion re-renders */
  const scratchX = document.createElement("canvas"); scratchX.width = W, scratchX.height = H;
  const RX = new SP.Renderer(cX.getContext("2d"), { scratch: scratchX, vec: SP.vec });
  RX.arts = R.arts, RX.setScale(S, K);
  hookContext(R.ctx);

  function clearMagenta(ctx) {
    ctx.save(); ctx.setTransform(1, 0, 0, 1, 0, 0); ctx.fillStyle = "#ff00ff"; ctx.fillRect(0, 0, CW, CH); ctx.restore();
  }
  function runOn(Rr, cmd) { Rr.reset(); clearMagenta(Rr.ctx); Rr.run(cmd, cmd.length); Rr.reset(); }
  function fbRGB() {
    const fb = SP.sim.framebuffer565(), o = new Uint8Array(W * H * 3);
    for (let i = 0; i < W * H; i++) {
      const v = fb[i], r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
      o[i * 3] = (r << 3) | (r >> 2), o[i * 3 + 1] = (g << 2) | (g >> 4), o[i * 3 + 2] = (b << 3) | (b >> 2);
    }
    return o;
  }
  /* a hi-res canvas -> 320 x 240: the pixel centre (point) and the 4x4 mean */
  function down(canvas) {
    const d = canvas.getContext("2d").getImageData(0, 0, CW, CH).data;
    const pt = new Uint8Array(W * H * 3), mean = new Uint8Array(W * H * 3);
    let magenta = 0;
    const blank = new Uint8Array(W * H);
    for (let y = 0; y < H; y++)
      for (let x = 0; x < W; x++) {
        const cx = Math.min(CW - 1, Math.floor((x + 0.5) * Z)), cy = Math.min(CH - 1, Math.floor((y + 0.5) * Z));
        const p = (cy * CW + cx) * 4, o = (y * W + x) * 3;
        pt[o] = d[p], pt[o + 1] = d[p + 1], pt[o + 2] = d[p + 2];
        let sr = 0, sg = 0, sb = 0, n = 0;
        const x0 = Math.floor(x * Z), x1 = Math.min(CW, Math.floor((x + 1) * Z)), y0 = Math.floor(y * Z), y1 = Math.min(CH, Math.floor((y + 1) * Z));
        for (let yy = y0; yy < y1; yy++) for (let xx = x0; xx < x1; xx++) { const q = (yy * CW + xx) * 4; sr += d[q], sg += d[q + 1], sb += d[q + 2], n++; }
        mean[o] = sr / n, mean[o + 1] = sg / n, mean[o + 2] = sb / n;
        if (d[p] === 255 && d[p + 1] === 0 && d[p + 2] === 255) { blank[y * W + x] = 1; magenta++; }
      }
    return { pt: pt, mean: mean, blank: blank, magenta: magenta, raw: d };
  }
  /* tolerant difference: each pixel against the nearest colour in the other's 3x3 neighbourhood */
  function tolDiff(X, Y) {
    const out = new Uint16Array(W * H);
    for (let y = 0; y < H; y++)
      for (let x = 0; x < W; x++) {
        const o = (y * W + x) * 3;
        let m1 = 1e9, m2 = 1e9;
        for (let dy = -1; dy <= 1; dy++) {
          const yy = y + dy; if (yy < 0 || yy >= H) continue;
          for (let dx = -1; dx <= 1; dx++) {
            const xx = x + dx; if (xx < 0 || xx >= W) continue;
            const q = (yy * W + xx) * 3;
            const d1 = Math.abs(X[o] - Y[q]) + Math.abs(X[o + 1] - Y[q + 1]) + Math.abs(X[o + 2] - Y[q + 2]);
            const d2 = Math.abs(Y[o] - X[q]) + Math.abs(Y[o + 1] - X[q + 1]) + Math.abs(Y[o + 2] - X[q + 2]);
            if (d1 < m1) m1 = d1;
            if (d2 < m2) m2 = d2;
          }
        }
        out[y * W + x] = Math.max(m1, m2);
      }
    return out;
  }
  const lum = (A, o) => A[o] * 0.3 + A[o + 1] * 0.59 + A[o + 2] * 0.11;
  function boxI(b) { return [Math.max(0, Math.floor(b[0])), Math.max(0, Math.floor(b[1])), Math.min(W, Math.ceil(b[2])), Math.min(H, Math.ceil(b[3]))]; }
  function area(b) { return Math.max(0, b[2] - b[0]) * Math.max(0, b[3] - b[1]); }
  function inter(a, b) { return [Math.max(a[0], b[0]), Math.max(a[1], b[1]), Math.min(a[2], b[2]), Math.min(a[3], b[3])]; }
  /* blobs of mask (8-connected) */
  function blobs(mask, minArea) {
    const seen = new Uint8Array(W * H), out = [];
    for (let p = 0; p < W * H; p++) {
      if (!mask[p] || seen[p]) continue;
      const st = [p]; seen[p] = 1;
      let n = 0, x0 = W, y0 = H, x1 = 0, y1 = 0;
      while (st.length) {
        const q = st.pop(), x = q % W, y = (q / W) | 0;
        n++; x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y);
        for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
          const xx = x + dx, yy = y + dy;
          if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
          const r = yy * W + xx;
          if (mask[r] && !seen[r]) { seen[r] = 1; st.push(r); }
        }
      }
      if (n >= minArea) out.push({ n: n, box: [x0, y0, x1 + 1, y1 + 1] });
    }
    return out;
  }
  /* the topmost record of list whose box holds most of b */
  function owner(list, b, pred) {
    let best = null, bestA = 0;
    for (let k = list.length - 1; k >= 0; k--) {
      const r = list[k];
      if (!r.box || (pred && !pred(r))) continue;
      const a = area(inter(r.box, b));
      if (a > bestA * 1.5 || (a > 0 && !best)) { best = r, bestA = a; if (a >= area(b) * 0.6) break; }
    }
    return best;
  }

  /* ---------------------------------------------------------------- one capture */
  let lastPair = null;
  function capture(opts) {
    opts = opts || {};
    const res = { findings: [], stats: {} };
    /* C: as the page draws it */
    register(true);
    const cmdC = redrawTake();
    TEXTLOG.length = 0;
    runOn(R, cmdC);
    const texts = TEXTLOG.slice();
    const A = fbRGB();
    const dC = down(R.ctx.canvas);
    /* B: no art */
    let cmdB = null, dB = null, AB = null;
    if (!opts.noB) {
      register(false);
      cmdB = redrawTake();
      AB = fbRGB();
      runOn(RB, cmdB);
      dB = down(cB);
      register(true);
      take();
    }
    const cr = parse(cmdC), br = cmdB ? parse(cmdB) : null;
    res.stats.cmdsC = cr.length, res.stats.cmdsB = br ? br.length : 0;
    res.stats.magentaC = dC.magenta, res.stats.magentaB = dB ? dB.magenta : 0;
    const add = (f) => res.findings.push(f);
    const textBoxes = cr.filter(r => r.op === C_TEXT);

    /* 1x drawn differently with art on and off (the module looks at the hooks' answers) */
    if (AB) {
      let nd = 0;
      for (let i = 0; i < W * H * 3; i += 3) if (A[i] !== AB[i] || A[i + 1] !== AB[i + 1] || A[i + 2] !== AB[i + 2]) nd++;
      res.stats.fbArtDiff = nd;
      if (nd > 20) add({ check: "1x-depends-on-art", sev: "low", n: nd, what: "the 1x framebuffer differs with the art registered or not (" + nd + " px)" });
    }

    /* blank pixels on the Mac */
    if (dC.magenta > 0) {
      const bl = blobs(dC.blank, 4);
      bl.slice(0, 6).forEach(b => add({ check: "blank", sev: b.n > 200 ? "high" : "med", box: b.box, n: b.n,
        what: "nothing drawn on the Mac page (" + b.n + " px)", near: (owner(cr, b.box) ? label(owner(cr, b.box)) : null) }));
    }

    /* hook check: A vs B (text masked) */
    let hookD = null;
    if (dB) {
      hookD = tolDiff(A, dB.pt);
      const mask = new Uint8Array(W * H);
      const tmask = new Uint8Array(W * H);
      for (const r of (br || [])) if (r.op === C_TEXT) { const b = boxI([r.box[0] - 2, r.box[1] - 2, r.box[2] + 2, r.box[3] + 2]); for (let y = b[1]; y < b[3]; y++) for (let x = b[0]; x < b[2]; x++) tmask[y * W + x] = 1; }
      for (let p = 0; p < W * H; p++) if (hookD[p] > 180 && !tmask[p] && !dB.blank[p]) mask[p] = 1;
      blobs(mask, 10).forEach(b => {
        const o = owner(br, b.box, r => r.op !== C_TEXT);
        add({ check: "hook", sev: b.n > 120 ? "med" : "low", box: b.box, n: b.n, what: "1x and its high-resolution shapes differ (" + b.n + " px)", el: o ? label(o) : null });
      });
    }

    /* substitutions: C vs B per sprite / picture */
    const subs = [];
    if (dB) {
      const al = align(cr, br);
      res.stats.desync = al.desync;
      res.stats.desyncWhere = al.where;
      const D = tolDiff(dC.pt, dB.pt);
      for (const r of cr) {
        if (r.op !== C_SPRITE && r.op !== C_IMAGE) continue;
        const run = al.runs.get(r) || [];
        const bi = boxI(r.box);
        /* the swallowed commands' extent (drawn content) */
        let ux0 = 1e9, uy0 = 1e9, ux1 = -1e9, uy1 = -1e9, nt = 0, outA = 0, outD = 0;
        const stexts = [], screen = [0, 0, W, H];
        for (const q of run) {
          if (!q.box) continue;
          const qb = inter(q.box, screen); /* only what is on the screen */
          if (qb[2] <= qb[0] || qb[3] <= qb[1]) continue;
          ux0 = Math.min(ux0, qb[0]), uy0 = Math.min(uy0, qb[1]), ux1 = Math.max(ux1, qb[2]), uy1 = Math.max(uy1, qb[3]);
          const oa = area(qb) - area(inter(qb, r.box));
          if (oa > 0) outA += oa, outD = Math.max(outD, r.box[0] - qb[0], r.box[1] - qb[1], qb[2] - r.box[2], qb[3] - r.box[3]);
          if (q.op === C_TEXT) { nt++; stexts.push(q.s); }
        }
        const ub = ux1 > ux0 ? [ux0, uy0, ux1, uy1] : null;
        /* the region compared: the box and what was swallowed */
        const reg = boxI(ub ? [Math.min(r.box[0], ub[0]), Math.min(r.box[1], ub[1]), Math.max(r.box[2], ub[2]), Math.max(r.box[3], ub[3])] : r.box);
        let n = 0, bad = 0, lc = 0, lb = 0, la = 0, no = 0, badO = 0;
        /* (pictures drawn later over it: their own art differs there, not this one's -- a cloud behind a house) */
        const ri = cr.indexOf(r), later = [];
        for (let k = ri + 1; k < cr.length; k++) {
          const q = cr[k];
          if ((q.op === C_SPRITE || q.op === C_IMAGE) && q.box && q.box[2] > reg[0] && q.box[0] < reg[2] && q.box[3] > reg[1] && q.box[1] < reg[3])
            later.push(q.box);
        }
        for (let y = reg[1]; y < reg[3]; y++) for (let x = reg[0]; x < reg[2]; x++) {
          const p = y * W + x, o = p * 3;
          const over = later.length && later.some(b => x + 0.5 >= b[0] && x + 0.5 <= b[2] && y + 0.5 >= b[1] && y + 0.5 <= b[3]);
          n++; if (D[p] > 200 && !over) bad++;
          if (x + 0.5 < r.box[0] || x + 0.5 > r.box[2] || y + 0.5 < r.box[1] || y + 0.5 > r.box[3]) { no++; if (D[p] > 200) badO++; }
          lc += lum(dC.mean, o), lb += lum(dB.mean, o), la += lum(A, o);
        }
        const e = { op: OPN[r.op], id: label(r), box: r.box.map(v => +v.toFixed(1)), tint: r.tint || 0, n: n,
          bad: n ? +(bad / n).toFixed(3) : 0, dL: n ? +((lc - lb) / n).toFixed(1) : 0, swallowed: run.length, texts: stexts };
        /* drawn outside the sprite's box, on the screen, and lost on the Mac there */
        if (ub && outA >= 20 && no && badO / no > 0.12) e.outside = +outD.toFixed(1), e.lostOutside = +(badO / no).toFixed(2);
        subs.push(e);
        if (nt) add({ check: "swallowed-text", sev: "high", el: e.id, box: e.box, what: "the substitution hides text the module drew inside it: " + stexts.slice(0, 6).map(s => JSON.stringify(s)).join(", ") });
        if (e.bad > 0.18 && n > 30) add({ check: "subst-differs", sev: e.bad > 0.4 ? "high" : "med", el: e.id, box: e.box, bad: e.bad, what: "the Mac art differs from the module's drawing over " + Math.round(e.bad * 100) + "% of its area" });
        if (e.outside > 3) add({ check: "subst-outside-box", sev: e.lostOutside > 0.35 ? "high" : "med", el: e.id, box: e.box, swallowedBox: ub.map(v => +v.toFixed(1)),
          what: "the module drew up to " + e.outside + " px outside the sprite's box inside its bracket; " + Math.round(e.lostOutside * 100) + "% of that area is missing on the Mac" });
        if (Math.abs(e.dL) > 28 && n > 30) add({ check: "tint", sev: Math.abs(e.dL) > 50 ? "high" : "med", el: e.id, box: e.box, dL: e.dL, tint: e.tint, what: "the Mac art is " + (e.dL > 0 ? "brighter" : "darker") + " than the module's colours by " + Math.abs(e.dL) + " (luma)" });
      }
    }
    res.subs = subs;

    /* texts: squeezed, out of band / screen, overlapping, hidden */
    const tmetrics = [];
    for (const t of texts) {
      const a = t.a, box = [a[4] / 16 / S, a[5] / 16 / S, (a[4] + a[6]) / 16 / S, (a[5] + a[7]) / 16 / S];
      const wdev = a[6] / 16 * K, squeeze = t.maxW !== null && t.tw > t.maxW ? t.maxW / t.tw : 1;
      const m = { s: t.s, box: box.map(v => +v.toFixed(1)), squeeze: +squeeze.toFixed(3), font: t.font };
      tmetrics.push(m);
      if (squeeze < 0.82) add({ check: "text-squeezed", sev: squeeze < 0.65 ? "high" : t.s.length > 1 ? "med" : "low", el: "text " + JSON.stringify(t.s), box: m.box,
        what: "the web font is " + Math.round((1 / squeeze - 1) * 100) + "% too wide for the 1x box: squashed by maxWidth to " + Math.round(squeeze * 100) + "%" });
      if (box[0] < -0.5 || box[2] > W + 0.5 || box[1] < -0.5 || box[3] > H + 0.5)
        add({ check: "text-offscreen", sev: "med", el: "text " + JSON.stringify(t.s), box: m.box, what: "text runs off the screen" });
      const b0 = a[2] / S, b1 = a[3] / S;
      if (a[3] && (box[1] < b0 - 1 || box[3] > b1 + 1) && box[3] > b0 && box[1] < b1)
        add({ check: "text-clipped", sev: "med", el: "text " + JSON.stringify(t.s), box: m.box, what: "text cut by its clip band (rows " + b0 + ".." + b1 + ")" });
    }
    res.texts = tmetrics;
    /* covered by something solid drawn later that is bigger than it (the item card over the shop's grid): it shows
     * nowhere, on the Mac or in 1x */
    const coveredLater = (r, art) => { /* (art: pictures and sprites count as covering too) */
      const ra = area(r.box);
      for (let j = cr.indexOf(r) + 1; j < cr.length; j++) {
        const q = cr[j];
        const solid = q.op === C_FILL || (art && (q.op === C_IMAGE || q.op === C_SPRITE)) || (q.op === C_SHAPE && (q.kind === 1 || q.kind === 3));
        if (!q.box || !solid || area(q.box) < 2 * ra) continue;
        if (area(inter(q.box, r.box)) >= 0.9 * ra) return true;
      }
      return false;
    };
    /* (a text that does not show on the Mac at all -- the shop's grid under the item card -- overlaps nothing) */
    const shows = r => {
      if (coveredLater(r, true)) return false;
      const v = r.c, tr = ((v >> 11) & 31) * 255 / 31, tg = ((v >> 5) & 63) * 255 / 63, tb = (v & 31) * 255 / 31;
      const x0 = Math.max(0, Math.floor(r.box[0] * Z)), y0 = Math.max(0, Math.floor(r.box[1] * Z)),
        x1 = Math.min(CW, Math.ceil(r.box[2] * Z)), y1 = Math.min(CH, Math.ceil(r.box[3] * Z));
      let n = 0;
      for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) {
        const o = (y * CW + x) * 4;
        if (Math.abs(dC.raw[o] - tr) + Math.abs(dC.raw[o + 1] - tg) + Math.abs(dC.raw[o + 2] - tb) < 70) n++;
      }
      return n >= 8;
    };
    for (let i = 0; i < textBoxes.length; i++) for (let j = i + 1; j < textBoxes.length; j++) {
      const p = textBoxes[i], q = textBoxes[j];
      if (p.s === q.s || !p.s.trim() || !q.s.trim()) continue;
      if (p.shows === undefined) p.shows = shows(p);
      if (q.shows === undefined) q.shows = shows(q);
      if (!p.shows || !q.shows) continue;
      const ia = area(inter(p.box, q.box));
      if (ia > 0.3 * Math.min(area(p.box), area(q.box)) && ia > 12)
        add({ check: "text-overlap", sev: "med", el: "text " + JSON.stringify(p.s) + " / " + JSON.stringify(q.s), box: inter(p.box, q.box).map(v => +v.toFixed(1)), what: "two texts overlap" });
    }

    /* occlusion: later drawing over texts, buttons and pictures (geometry), verified by pixels */
    const occl = [];
    const isContent = r => r.op === C_TEXT && r.s.trim().length > 0 || (r.op === C_SPRITE && /button/.test(spriteName(r.hash) || "")) ||
      r.op === C_IMAGE;
    const cand = [];
    for (let i = 0; i < cr.length; i++) {
      const r = cr[i];
      if (!r.box || !isContent(r) || area(r.box) < 6) continue;
      const ra = area(r.box);
      for (let j = i + 1; j < cr.length; j++) {
        const q = cr[j];
        if (!q.box || q.op === C_CLIP || q.op === C_SCENE) continue;
        if (q.op === C_TEXT && (q.s === r.s || !q.s.trim())) continue;
        const ia = area(inter(r.box, q.box));
        if (ia < 0.2 * ra) continue;
        if (area(q.box) > 0.85 * W * H) continue; /* a full-screen redraw: another layer of the same picture */
        if (r.op === C_SPRITE && q.box[0] >= r.box[0] - 1 && q.box[1] >= r.box[1] - 1 && q.box[2] <= r.box[2] + 1 && q.box[3] <= r.box[3] + 1)
          continue; /* (what sits on a button or a card: its own picture and words, not over it) */
        cand.push({ i: i, j: j, frac: ia / ra });
      }
    }
    /* group by occluded element; verify the biggest few by rendering without the occluder */
    cand.sort((a, b) => b.frac - a.frac);
    const tried = new Set();
    let verified = 0;
    for (const c of cand) {
      if (verified >= (opts.occlMax || 14)) break;
      const key = c.i + ":" + c.j;
      if (tried.has(key)) continue;
      tried.add(key);
      const r = cr[c.i], q = cr[c.j];
      /* the stream without q */
      const parts = [cmdC.subarray(0, q.i), cmdC.subarray(q.i + q.len)];
      const cmdX = new Int32Array(cmdC.length - q.len);
      cmdX.set(parts[0], 0), cmdX.set(parts[1], q.i);
      runOn(RX, cmdX);
      verified++;
      const b = boxI(r.box);
      const dx = cX.getContext("2d").getImageData(Math.floor(b[0] * Z), Math.floor(b[1] * Z), Math.max(1, Math.ceil((b[2] - b[0]) * Z)), Math.max(1, Math.ceil((b[3] - b[1]) * Z))).data;
      const ww = Math.max(1, Math.ceil((b[2] - b[0]) * Z));
      let changed = 0, tot = 0;
      for (let p = 0; p < dx.length; p += 4) {
        const xx = Math.floor(b[0] * Z) + (p / 4) % ww, yy = Math.floor(b[1] * Z) + Math.floor(p / 4 / ww);
        if (xx >= CW || yy >= CH) continue;
        const o = (yy * CW + xx) * 4;
        tot++;
        if (Math.abs(dx[p] - dC.raw[o]) + Math.abs(dx[p + 1] - dC.raw[o + 1]) + Math.abs(dx[p + 2] - dC.raw[o + 2]) > 60) changed++;
      }
      let hid = tot ? changed / tot : 0;
      if (hid > (r.op === C_TEXT ? 0.07 : 0.12)) {
        /* is it on show at all without q? The stream up to r (as r was drawn) against the stream without q:
         * where they differ, something else covers it -- a dialog it sits under, the room's own furniture --
         * and q is not what hides it (the item card's picture over the shop grid under the card) */
        const upto = cmdC.subarray(0, r.i + r.len);
        runOn(RX, upto);
        const dt = cX.getContext("2d").getImageData(Math.floor(b[0] * Z), Math.floor(b[1] * Z), ww, Math.max(1, Math.ceil((b[3] - b[1]) * Z))).data;
        runOn(RX, cmdX);
        const dx2 = cX.getContext("2d").getImageData(Math.floor(b[0] * Z), Math.floor(b[1] * Z), ww, Math.max(1, Math.ceil((b[3] - b[1]) * Z))).data;
        let same = 0, n2 = 0, inkT = 0, inkX = 0, inkC = 0;
        const v = r.c, tr = ((v >> 11) & 31) * 255 / 31, tg = ((v >> 5) & 63) * 255 / 63, tb = (v & 31) * 255 / 31;
        for (let p = 0; p < dt.length; p += 4) {
          const xx = Math.floor(b[0] * Z) + (p / 4) % ww, yy = Math.floor(b[1] * Z) + Math.floor(p / 4 / ww);
          if (xx >= CW || yy >= CH) continue;
          const o = (yy * CW + xx) * 4;
          n2++;
          if (Math.abs(dt[p] - dx2[p]) + Math.abs(dt[p + 1] - dx2[p + 1]) + Math.abs(dt[p + 2] - dx2[p + 2]) <= 60) same++;
          if (r.op === C_TEXT) {
            if (Math.abs(dt[p] - tr) + Math.abs(dt[p + 1] - tg) + Math.abs(dt[p + 2] - tb) < 70) inkT++;
            if (Math.abs(dx2[p] - tr) + Math.abs(dx2[p + 1] - tg) + Math.abs(dx2[p + 2] - tb) < 70) inkX++;
            if (Math.abs(dC.raw[o] - tr) + Math.abs(dC.raw[o + 1] - tg) + Math.abs(dC.raw[o + 2] - tb) < 70) inkC++;
          }
        }
        if (n2 && same / n2 < 0.5) continue; /* (hidden by something else already) */
        if (r.op === C_TEXT) { /* a text: how much of its ink q takes away, of what shows without q */
          if (inkX < 0.4 * inkT || inkX < 6) continue;
          hid = Math.max(0, (inkX - inkC) / inkX);
          if (hid <= 0.07) continue;
        }
        /* what kind of thing covers it: a panel (a dialog / curtain, deliberate), a particle
         * (sparkle, confetti), or an overlay (a banner, bubble, guide, hand, toast, coin pop-up) */
        const oa = area(q.box);
        /* (a picture over a picture: two things of the place in front of one another -- the friend walking past
         * the furniture, a rug under a kitten: depth, not an overlay) */
        const kind = oa >= 0.2 * W * H ? "panel" : oa < 40 ? "particle" : r.op === C_IMAGE && (q.op === C_IMAGE ||
          (q.op === C_SPRITE && /^sprite (game|town|stage|prop)\//.test(label(q)))) ? "depth" : "overlay";
        const e = { occluded: label(r), occluder: label(q), kind: kind, box: r.box.map(v => +v.toFixed(1)), obox: q.box.map(v => +v.toFixed(1)),
          geom: +c.frac.toFixed(2), hidden: +hid.toFixed(2), macOnly: false };
        occl.push(e);
      }
    }
    res.occl = occl;
    occl.forEach(o => add({ check: o.kind === "overlay" ? "occlusion" : "occlusion-" + o.kind,
      sev: o.kind === "panel" || o.kind === "depth" ? "low" : o.hidden > 0.45 ? "high" : o.hidden > 0.25 ? "med" : "low", el: o.occluded, by: o.occluder, box: o.box,
      hidden: o.hidden, what: o.occluder + " covers " + o.occluded + " (" + Math.round(o.hidden * 100) + "% of it hidden)" }));

    /* texts the Mac hides that the module's own drawing shows (C vs B: art drawn over them) */
    if (dB) {
      for (const r of cr) {
        if (r.op !== C_TEXT || !r.s.trim() || coveredLater(r, false)) continue; /* (under a panel: shown nowhere) */
        const v = r.c, tr = ((v >> 11) & 31) * 255 / 31, tg = ((v >> 5) & 63) * 255 / 63, tb = (v & 31) * 255 / 31;
        const x0 = Math.max(0, Math.floor(r.box[0] * Z)), y0 = Math.max(0, Math.floor(r.box[1] * Z)),
          x1 = Math.min(CW, Math.ceil(r.box[2] * Z)), y1 = Math.min(CH, Math.ceil(r.box[3] * Z));
        let nb = 0, nc = 0;
        for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) {
          const o = (y * CW + x) * 4;
          if (Math.abs(dB.raw[o] - tr) + Math.abs(dB.raw[o + 1] - tg) + Math.abs(dB.raw[o + 2] - tb) < 70) nb++;
          if (Math.abs(dC.raw[o] - tr) + Math.abs(dC.raw[o + 1] - tg) + Math.abs(dC.raw[o + 2] - tb) < 70) nc++;
        }
        if (nb >= 40 && nc < 0.7 * nb) {
          const by = owner(cr.slice(cr.indexOf(r) + 1), r.box, q => q.op === C_SPRITE || q.op === C_IMAGE);
          add({ check: "text-hidden-mac", sev: nc < 0.4 * nb ? "high" : "med", el: "text " + JSON.stringify(r.s), by: by ? label(by) : null,
            box: r.box.map(v2 => +v2.toFixed(1)), what: "the Mac page shows " + Math.round(100 * nc / nb) + "% of the text's ink the module's own drawing shows" + (by ? " (under " + label(by) + ")" : "") });
        }
      }
    }

    /* inventory */
    const inv = {};
    for (const r of cr) if (r.op === C_SPRITE || r.op === C_IMAGE) { const l = label(r); inv[l] = (inv[l] || 0) + 1; }
    res.inventory = inv;
    res.textsAll = cr.filter(r => r.op === C_TEXT).map(r => r.s);
    lastPair = { A: A, dC: dC, dB: dB, hookD: hookD, cr: cr };
    return res;
  }

  /* animation: 1x and Mac change together over a few frames (call after capture) */
  function animCheck(lines) {
    const A0 = lastPair.A, C0 = lastPair.dC.pt;
    for (const l of lines) SP.sim.console(l);
    take();
    register(true);
    const cmd = redrawTake();
    runOn(R, cmd);
    const A1 = fbRGB(), C1 = down(R.ctx.canvas).pt;
    const mA = new Uint8Array(W * H), mC = new Uint8Array(W * H);
    let nA = 0, nC = 0, frozen = 0;
    for (let p = 0; p < W * H; p++) {
      const o = p * 3;
      const dA = Math.abs(A0[o] - A1[o]) + Math.abs(A0[o + 1] - A1[o + 1]) + Math.abs(A0[o + 2] - A1[o + 2]);
      const dCc = Math.abs(C0[o] - C1[o]) + Math.abs(C0[o + 1] - C1[o + 1]) + Math.abs(C0[o + 2] - C1[o + 2]);
      if (dA > 60) mA[p] = 1, nA++;
      if (dCc > 30) mC[p] = 1, nC++;
    }
    /* blobs that move in 1x with no change on the Mac within 2 px */
    const out = [];
    blobs(mA, 12).forEach(b => {
      let any = 0;
      for (let y = Math.max(0, b.box[1] - 2); y < Math.min(H, b.box[3] + 2); y++) for (let x = Math.max(0, b.box[0] - 2); x < Math.min(W, b.box[2] + 2); x++) any += mC[y * W + x];
      if (!any) { frozen += b.n; out.push({ box: b.box, n: b.n, el: owner(parse(cmd), b.box) ? label(owner(parse(cmd), b.box)) : null }); }
    });
    return { changed1x: nA, changedMac: nC, frozen: out };
  }

  /* images: the side-by-side (1x x2 | Mac | a heat map of what differs) as a PNG data URL */
  function composite(scale, findings) {
    scale = scale || 2;
    const w = W * scale, h = H * scale, cv = document.createElement("canvas");
    cv.width = w * 3 + 16, cv.height = h;
    const c = cv.getContext("2d");
    c.fillStyle = "#222"; c.fillRect(0, 0, cv.width, cv.height);
    const a = document.createElement("canvas"); a.width = W, a.height = H;
    const ac = a.getContext("2d"), im = ac.createImageData(W, H);
    for (let i = 0; i < W * H; i++) { im.data[i * 4] = lastPair.A[i * 3]; im.data[i * 4 + 1] = lastPair.A[i * 3 + 1]; im.data[i * 4 + 2] = lastPair.A[i * 3 + 2]; im.data[i * 4 + 3] = 255; }
    ac.putImageData(im, 0, 0);
    c.imageSmoothingEnabled = false;
    c.drawImage(a, 0, 0, w, h);
    c.imageSmoothingEnabled = true;
    c.imageSmoothingQuality = "high";
    c.drawImage(R.ctx.canvas, w + 8, 0, w, h);
    /* heat: Mac (grey) with the findings' boxes */
    c.globalAlpha = 0.45; c.drawImage(R.ctx.canvas, 2 * w + 16, 0, w, h); c.globalAlpha = 1;
    const col = { high: "#ff1744", med: "#ff9100", low: "#2979ff" };
    (findings || []).forEach(f => {
      if (!f.box) return;
      c.strokeStyle = col[f.sev] || "#fff"; c.lineWidth = 2;
      c.strokeRect(2 * w + 16 + f.box[0] * scale, f.box[1] * scale, (f.box[2] - f.box[0]) * scale, (f.box[3] - f.box[1]) * scale);
    });
    return cv.toDataURL("image/png");
  }
  /* the Mac canvas alone (full resolution), for close looks */
  function macPNG() { return R.ctx.canvas.toDataURL("image/png"); }

  /* the page's own overlays (DOM) over the play area, in logical coordinates */
  function overlays() {
    const out = {};
    ["menubtn", "menu", "help", "status"].forEach(id => {
      const e = document.getElementById(id);
      if (!e) return;
      const r = e.getBoundingClientRect(), Lp = SP.L, d = Lp.dpr;
      const vis = !e.hidden && r.width > 0;
      const lx0 = (r.left * d - Lp.play.x) / Lp.z, ly0 = (r.top * d - Lp.play.y) / Lp.z, lx1 = (r.right * d - Lp.play.x) / Lp.z, ly1 = (r.bottom * d - Lp.play.y) / Lp.z;
      out[id] = { visible: vis, css: [r.left, r.top, r.right, r.bottom].map(v => Math.round(v)),
        logical: [lx0, ly0, lx1, ly1].map(v => +v.toFixed(1)), overPlay: vis && lx1 > 0 && ly1 > 0 && lx0 < W && ly0 < H };
    });
    return out;
  }

  function idCollisions() {
    const base = window.SPARKLE_VECTOR || { svg: {}, hash: {} }, extra = window.SPARKLE_EXTRA_VECTOR || [];
    const out = { ids: [], hashes: [], atlas: [] };
    extra.forEach(p => {
      Object.keys(p.svg || {}).forEach(id => { if (base.svg[id]) out.ids.push(id); });
      Object.keys(p.hash || {}).forEach(h => { if (base.hash && base.hash[h] && base.hash[h] !== p.hash[h]) out.hashes.push(h + ": " + base.hash[h] + " -> " + p.hash[h]); });
    });
    if (R.arts.length > 1) {
      const b = R.arts[0].sprites;
      R.arts.slice(1).forEach(a => Object.keys(a.sprites).forEach(h => { if (b[h]) out.atlas.push(h); }));
    }
    /* a picture both in an atlas and the vector set: the vector one wins (Renderer._image) */
    out.vectorOverAtlas = 0;
    if (SP.vec) R.arts.slice(1).forEach(a => Object.keys(a.sprites).forEach(h => { if (SP.vec.byHash[h]) out.vectorOverAtlas++; }));
    return out;
  }

  window.RA = { align: align, take: take, register: register, capture: capture, animCheck: animCheck, composite: composite, macPNG: macPNG,
    overlays: overlays, idCollisions: idCollisions, parse: parse, label: label, S: S, Z: Z };
})();
