/*
 * 3D graph cells: a surface z = f(x,y) sampled on the calculator (nb sample3d: a grid of z values
 * in the cell's x, y range), drawn on a 2D canvas by a small renderer of its own (no WebGL), the
 * way the calculator draws it (app/g3d_view.c, g3d_raster.c): the view box [xmin, xmax] x
 * [ymin, ymax] x [zmin, zmax] scaled to a cube, turned by the azimuth about +z and tilted by the
 * elevation, a weak perspective (the eye 6 half-widths away); flat-shaded grid cells (painter's
 * order, far ones first) lit by a light fixed to the camera, 0.3 + 0.7 |n.L|, the side facing away
 * darkened to 75 %; the entry's colour or (colour mode "height") the calculator's colour map; the
 * box and the axes through the origin. Parts outside the box in z are cut at its faces.
 *   drag           turn (azimuth, elevation)          wheel (once clicked) / pinch   zoom
 *   double click   back to the cell's view            arrows, + - 0 when it has the focus
 * Turning and zooming are the page's own: the calculator's cell keeps its view.
 * The pure parts (view, project, mesh, heat) are tested under node.
 */
(function (X) {
  'use strict';

  const EYE = 6;
  const rad = (d) => (d * Math.PI) / 180;

  /* the camera of g3_view_setup: rows screen right, screen up, towards the viewer */
  function viewMatrix(azim, elev) {
    const sa = Math.sin(rad(azim)), ca = Math.cos(rad(azim)), se = Math.sin(rad(elev)), ce = Math.cos(rad(elev));
    return [[ca, -sa, 0], [sa * se, ca * se, ce], [-sa * ce, -ca * ce, se]];
  }
  const eye = (m, p) => [0, 1, 2].map((r) => m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2]);
  function project(m, p, cx, cy, scale) {
    const e = eye(m, p), d = 1 / (EYE - e[2]), f = EYE * d * scale;
    return [cx + f * e[0], cy - f * e[1], e[2]];
  }

  /* the calculator's height colour map (g3_heat): h in [-1, 1] -> [r, g, b] */
  const STOPS = [[30, 70, 230], [0, 180, 220], [40, 180, 60], [240, 200, 0], [225, 45, 30]];
  function heat(h) {
    h = !(h > -1) ? -1 : h > 1 ? 1 : h;
    const t = (h + 1) * 2, k = Math.min(3, Math.floor(t)), f = t - k;
    return [0, 1, 2].map((i) => Math.round(STOPS[k][i] + (STOPS[k + 1][i] - STOPS[k][i]) * f));
  }
  const rgbOf = (hex) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));

  /* Grid cells of a sampled surface in normalised coordinates: [{p: [4 corners [x,y,z]], h}],
   * nodes with no value taken from s.edges (moved onto the domain's edge) when there, cells
   * with a corner still undefined left out, z cut to the box (a cell wholly above or below: out) */
  function mesh(s, w) {
    const out = [], nx = s.nx, ny = s.ny;
    const nz = (z) => (2 * (z - w[4])) / (w[5] - w[4]) - 1;
    const P = (i, j) => {
      const z = s.zs[j * nx + i], e = s.edges && s.edges[j * nx + i];
      if (Number.isFinite(z)) return [(2 * i) / (nx - 1) - 1, (2 * j) / (ny - 1) - 1, nz(z)];
      // a node with no value moved onto the edge of the domain (the calculator's edge= lines)
      if (e) return [(2 * (e[0] - w[0])) / (w[1] - w[0]) - 1, (2 * (e[1] - w[2])) / (w[3] - w[2]) - 1, nz(e[2])];
      return null;
    };
    for (let j = 0; j < ny - 1; j++) {
      for (let i = 0; i < nx - 1; i++) {
        const p = [P(i, j), P(i + 1, j), P(i + 1, j + 1), P(i, j + 1)];
        if (p.some((q) => !q)) continue;
        if (p.every((q) => q[2] > 1) || p.every((q) => q[2] < -1)) continue;
        const h = (p[0][2] + p[1][2] + p[2][2] + p[3][2]) / 4;
        for (const q of p) q[2] = Math.max(-1, Math.min(1, q[2]));
        out.push({ p, h });
      }
    }
    return out;
  }

  class Surface {
    /* canvas; data: {w: [6], view: [azim, elev, zoom, style, colormode, axes, box, ...],
     * surfaces: [{nx, ny, zs, color: '#rrggbb'}]}; opts.style: page colours */
    constructor(canvas, data, opts = {}) {
      this.c = canvas;
      this.opts = opts;
      this.setData(data);
      this.bind();
    }
    setData(data) {
      this.data = data;
      const v = data.view || [-60, 25, 1, 1, 0, 1, 1];
      this.home = { azim: v[0], elev: v[1], zoom: v[2] > 0.05 ? v[2] : 1 };
      if (!this.cam) this.cam = Object.assign({}, this.home);
      this.meshes = (data.surfaces || []).map((s) => ({ cells: mesh(s, data.w), color: s.color }));
    }
    size() {
      const r = this.c.getBoundingClientRect ? this.c.getBoundingClientRect() : { width: this.c.width, height: this.c.height };
      return { w: Math.max(50, r.width || this.c.width), h: Math.max(50, r.height || this.c.height) };
    }
    draw(w, h, dpr) {
      const s = w ? { w, h } : this.size();
      dpr = dpr || (globalThis.devicePixelRatio || 1);
      const W = Math.round(s.w * dpr), H = Math.round(s.h * dpr);
      if (this.c.width !== W || this.c.height !== H) { this.c.width = W; this.c.height = H; }
      const g = this.c.getContext('2d');
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      const css = (k, d) => (this.opts.style && this.opts.style[k]) || d;
      const bg = css('bg', '#ffffff'), boxc = css('axis', '#6b7280'), text = css('text', '#374151');
      g.fillStyle = bg;
      g.fillRect(0, 0, s.w, s.h);
      const cam = this.cam, m = viewMatrix(cam.azim, cam.elev), view = this.data.view || [];
      const scale = 0.36 * Math.min(s.w, s.h) * cam.zoom, cx = s.w / 2, cy = s.h / 2;
      const pr = (p) => project(m, p, cx, cy, scale);
      // the light fixed to the camera (upper left, in front), in world space; the direction towards the viewer
      const le = [-0.45, 0.55, 0.7], ll = Math.hypot(...le);
      const L = [0, 1, 2].map((k) => (m[0][k] * le[0] + m[1][k] * le[1] + m[2][k] * le[2]) / ll), T = m[2];
      const box = view[6] !== 0;
      const corners = [];
      for (const x of [-1, 1]) for (const y of [-1, 1]) for (const z of [-1, 1]) corners.push(pr([x, y, z]));
      const edges = [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]];
      const strokeEdges = (back) => {
        if (!box) return;
        g.strokeStyle = boxc;
        g.lineWidth = 1;
        g.beginPath();
        for (const [a, b] of edges) {
          const isBack = (corners[a][2] + corners[b][2]) / 2 < 0;
          if (isBack !== back) continue;
          g.moveTo(corners[a][0], corners[a][1]);
          g.lineTo(corners[b][0], corners[b][1]);
        }
        g.stroke();
      };
      strokeEdges(true);
      const quads = [];
      for (const ms of this.meshes) {
        const base = rgbOf(ms.color && ms.color[0] === '#' ? ms.color : '#0050dc');
        for (const q of ms.cells) {
          const a = q.p[0], b = q.p[1], c = q.p[2], d = q.p[3];
          const u = [c[0] - a[0], c[1] - a[1], c[2] - a[2]], v = [d[0] - b[0], d[1] - b[1], d[2] - b[2]];
          const n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
          const len = Math.hypot(...n);
          let k = 1;
          if (len > 0) {
            k = 0.3 + 0.7 * Math.abs((n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) / len);
            if (n[0] * T[0] + n[1] * T[1] + n[2] * T[2] < 0) k *= 0.75;
          }
          const col = view[4] === 1 ? heat(q.h) : base;
          const sp = q.p.map(pr);
          quads.push({ sp, depth: (sp[0][2] + sp[1][2] + sp[2][2] + sp[3][2]) / 4, col, k });
        }
      }
      // the axes through the world origin (view[5], as on the calculator), where it lies within
      // the box's other two ranges: short pieces sorted in with the cells, so the surface hides
      // the parts behind it
      const win = this.data.w || [-5, 5, -5, 5, -5, 5];
      const o = [0, 1, 2].map((k) => (2 * (0 - win[2 * k])) / (win[2 * k + 1] - win[2 * k]) - 1);
      const segs = [];
      if (view[5] !== 0) {
        for (let k = 0; k < 3; k++) {
          const k1 = (k + 1) % 3, k2 = (k + 2) % 3;
          if (!(Math.abs(o[k1]) <= 1 && Math.abs(o[k2]) <= 1)) continue;
          const N = 24;
          for (let i = 0; i < N; i++) {
            const a = o.slice(), b = o.slice();
            a[k] = -1 + (2 * i) / N;
            b[k] = -1 + (2 * (i + 1)) / N;
            const pa = pr(a), pb = pr(b);
            segs.push({ seg: [pa, pb], depth: (pa[2] + pb[2]) / 2 });
          }
        }
      }
      const items = quads.concat(segs).sort((p, q) => p.depth - q.depth);
      const wire = view[3] === 0;
      const rgb = (c, k) => 'rgb(' + c.map((x) => Math.round(Math.min(255, x * k))).join(',') + ')';
      const axisc = css('axisline', text);
      for (const q of items) {
        if (q.seg) {
          g.lineWidth = 1;
          g.strokeStyle = axisc;
          g.beginPath();
          g.moveTo(q.seg[0][0], q.seg[0][1]);
          g.lineTo(q.seg[1][0], q.seg[1][1]);
          g.stroke();
          continue;
        }
        g.lineWidth = 0.6;
        g.beginPath();
        g.moveTo(q.sp[0][0], q.sp[0][1]);
        for (let i = 1; i < 4; i++) g.lineTo(q.sp[i][0], q.sp[i][1]);
        g.closePath();
        if (wire) { g.strokeStyle = rgb(q.col, 1); g.stroke(); continue; }
        g.fillStyle = rgb(q.col, q.k);
        g.fill();
        g.strokeStyle = rgb(q.col, q.k * 0.7);
        g.stroke();
      }
      strokeEdges(false);
      if (!quads.length) {
        g.fillStyle = text;
        g.font = '12px system-ui, -apple-system, Segoe UI, sans-serif';
        g.textAlign = 'center';
        g.fillText(this.busy ? 'sampling…' : 'nothing to draw in this window', s.w / 2, s.h / 2);
      }
      this.quads = quads.length;
    }

    turn(da, de) {
      this.cam.azim += da;
      this.cam.elev = Math.max(-90, Math.min(90, this.cam.elev + de));
      this.draw();
    }
    zoom(f) { this.cam.zoom = Math.max(0.2, Math.min(8, this.cam.zoom * f)); this.draw(); }
    reset() { this.cam = Object.assign({}, this.home); this.draw(); }

    bind() {
      const c = this.c, pointers = new Map();
      let last = null, pinch = null;
      c.addEventListener('pointerdown', (e) => {
        c.focus({ preventScroll: true });
        c.setPointerCapture(e.pointerId);
        pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
        last = { x: e.clientX, y: e.clientY };
        if (pointers.size === 2) { const [a, b] = [...pointers.values()]; pinch = Math.hypot(a.x - b.x, a.y - b.y); }
        c.classList.add('dragging');
      });
      c.addEventListener('pointermove', (e) => {
        if (!pointers.has(e.pointerId)) return;
        pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
        if (pointers.size === 2 && pinch) {
          const [a, b] = [...pointers.values()], d = Math.hypot(a.x - b.x, a.y - b.y);
          if (d > 0) { this.zoom(d / pinch); pinch = d; }
          return;
        }
        if (last) { this.turn((e.clientX - last.x) * 0.5, (e.clientY - last.y) * 0.5); last = { x: e.clientX, y: e.clientY }; }
      });
      const up = (e) => {
        pointers.delete(e.pointerId);
        if (pointers.size < 2) pinch = null;
        if (!pointers.size) { last = null; c.classList.remove('dragging'); }
      };
      c.addEventListener('pointerup', up);
      c.addEventListener('pointercancel', up);
      c.addEventListener('wheel', (e) => {
        if (!e.ctrlKey && globalThis.document && document.activeElement !== c) return;
        e.preventDefault();
        this.zoom(Math.exp(-e.deltaY * (e.ctrlKey ? 0.01 : 0.0015)));
      }, { passive: false });
      c.addEventListener('dblclick', (e) => { e.preventDefault(); this.reset(); });
      c.addEventListener('keydown', (e) => {
        const k = e.key;
        if (k === '+' || k === '=') this.zoom(1.25);
        else if (k === '-' || k === '_') this.zoom(0.8);
        else if (k === '0') this.reset();
        else if (k === 'ArrowLeft') this.turn(-15, 0);
        else if (k === 'ArrowRight') this.turn(15, 0);
        else if (k === 'ArrowUp') this.turn(0, 10);
        else if (k === 'ArrowDown') this.turn(0, -10);
        else return;
        e.preventDefault();
        e.stopPropagation();
      });
    }
  }

  Object.assign(X, { Surface, viewMatrix, project3d: project, heat, mesh3d: mesh });
})(typeof module !== 'undefined' && module.exports ? module.exports : (globalThis.CalcNB = globalThis.CalcNB || {}));
