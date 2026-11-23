/*
 * The graph plotter: functions sampled on the calculator, drawn on a canvas
 * at the screen's own resolution (sharp on high-DPI displays): grid, axes
 * with 1-2-5 ticks, a legend, several functions, gaps where a function is
 * undefined and at poles (a jump across the whole view with a sign change).
 *   drag            pan               trackpad pinch           zoom at the pointer
 *   wheel / two-finger scroll: zoom (vertical) and pan (horizontal) once the
 *                   plot has been clicked; before that they scroll the page
 *   shift + wheel   zoom x only       alt + wheel              zoom y only
 *   double click    back to the cell's window
 *   + / - / 0 / arrows when the plot has the focus
 *   touch: one finger pans, two fingers zoom
 * onView(view) is called (debounced by the page) after the view changed, so
 * the page can sample the functions again for the new x range.
 * The pure parts (ticks, segments, autoRange) are tested under node.
 */
(function (X) {
  'use strict';

  // the calculator's function colours (func_colors, app/app.c): a graph cell's functions keep theirs
  const COLORS = ['#0050dc', '#dc1e1e', '#000000', '#c800a0', '#00963c', '#f08200', '#8c501e', '#000080'];

  /* "Nice" tick values covering [a, b] about every (b-a)/count */
  function ticks(a, b, count) {
    if (!(b > a) || !Number.isFinite(a) || !Number.isFinite(b)) return { step: 1, values: [] };
    const raw = (b - a) / Math.max(1, count), p = Math.pow(10, Math.floor(Math.log10(raw))), f = raw / p;
    const step = (f < 1.5 ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10) * p;
    const values = [];
    for (let v = Math.ceil(a / step) * step, k = 0; v <= b + step * 1e-9 && k < 1000; v += step, k++)
      values.push(Math.abs(v) < step * 1e-9 ? 0 : v);
    return { step, values };
  }

  function label(v, step) {
    if (v === 0) return '0';
    const d = Math.max(0, -Math.floor(Math.log10(step) + 1e-9));
    const a = Math.abs(v);
    if (a >= 1e6 || a < 1e-4) return v.toExponential(Math.min(6, Math.max(0, Math.round(Math.log10(a / step))))).replace('e+', 'e');
    return v.toFixed(Math.min(d, 10));
  }

  /* Polylines of the samples (xs evenly from x0 to x1) in view: a break at a
   * NaN, and between two points that jump more than the view's height with a
   * sign change (a pole: tan x, 1/x). */
  function segments(x0, x1, ys, view) {
    const n = ys.length, segs = [];
    if (n < 2) return segs;
    const h = (x1 - x0) / (n - 1), span = view.ymax - view.ymin;
    let cur = [];
    for (let i = 0; i < n; i++) {
      const y = ys[i] === null ? NaN : ys[i];
      if (!Number.isFinite(y)) { if (cur.length > 1) segs.push(cur); cur = []; continue; }
      if (cur.length) {
        const py = cur[cur.length - 1][1];
        if (Math.abs(y - py) > span && py * y < 0) { if (cur.length > 1) segs.push(cur); cur = []; }
      }
      cur.push([x0 + i * h, y]);
    }
    if (cur.length > 1) segs.push(cur);
    else if (cur.length === 1 && segs.length === 0 && n === 1) segs.push(cur);
    return segs;
  }

  /* A y range that shows the functions (ZoomFit without the spikes of a pole):
   * the 2nd to 98th percentile of the finite values, widened by 10 %. */
  function autoRange(seriesYs) {
    const all = [];
    for (const ys of seriesYs) for (const y of ys) if (y !== null && Number.isFinite(y)) all.push(y);
    if (!all.length) return { ymin: -10, ymax: 10 };
    all.sort((a, b) => a - b);
    const q = (p) => all[Math.min(all.length - 1, Math.max(0, Math.round(p * (all.length - 1))))];
    let lo = q(0.02), hi = q(0.98);
    if (all.length < 20) lo = all[0], hi = all[all.length - 1];
    if (!(hi > lo)) { const m = Math.abs(lo) || 1; lo -= m; hi += m; }
    const pad = (hi - lo) * 0.1;
    return { ymin: lo - pad, ymax: hi + pad };
  }

  class Plot {
    /* canvas: an HTMLCanvasElement; data: {xmin, xmax, series: [{expr, ys, error}]};
     * view: {xmin, xmax, ymin, ymax} (y null: automatic) */
    constructor(canvas, data, view, opts = {}) {
      this.c = canvas;
      this.opts = opts;
      this.home = Object.assign({}, view);
      this.setData(data);
      this.view = this.fullView(view);
      this.home = Object.assign({}, this.view);
      this.onView = opts.onView || null;
      if (!opts.static) this.bind();
    }
    setData(data) { this.data = data; }
    fullView(v) {
      const r = v.ymin === null || v.ymax === null || !(v.ymax > v.ymin) ? autoRange(this.data.series.map((s) => s.ys)) : v;
      return { xmin: v.xmin, xmax: v.xmax, ymin: r.ymin, ymax: r.ymax };
    }

    size() {
      const r = this.c.getBoundingClientRect ? this.c.getBoundingClientRect() : { width: this.c.width, height: this.c.height };
      return { w: Math.max(50, r.width || this.c.width), h: Math.max(50, r.height || this.c.height) };
    }

    /* draw at css size w x h with device pixel ratio dpr (export: fixed size) */
    draw(w, h, dpr) {
      const s = w ? { w, h } : this.size();
      dpr = dpr || (globalThis.devicePixelRatio || 1);
      const W = Math.round(s.w * dpr), H = Math.round(s.h * dpr);
      if (this.c.width !== W || this.c.height !== H) { this.c.width = W; this.c.height = H; }
      const g = this.c.getContext('2d');
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      const css = (k, d) => (this.opts.style && this.opts.style[k]) || d;
      const bg = css('bg', '#ffffff'), grid = css('grid', '#e6e8ec'), axis = css('axis', '#6b7280'), text = css('text', '#374151');
      g.fillStyle = bg;
      g.fillRect(0, 0, s.w, s.h);
      const v = this.view;
      const sx = (x) => ((x - v.xmin) / (v.xmax - v.xmin)) * s.w;
      const sy = (y) => s.h - ((y - v.ymin) / (v.ymax - v.ymin)) * s.h;
      const tx = ticks(v.xmin, v.xmax, Math.max(2, s.w / 80)), ty = ticks(v.ymin, v.ymax, Math.max(2, s.h / 60));
      g.lineWidth = 1;
      g.strokeStyle = grid;
      g.beginPath();
      for (const x of tx.values) { const p = Math.round(sx(x)) + 0.5; g.moveTo(p, 0); g.lineTo(p, s.h); }
      for (const y of ty.values) { const p = Math.round(sy(y)) + 0.5; g.moveTo(0, p); g.lineTo(s.w, p); }
      g.stroke();
      // axes (at the edge when 0 is out of view)
      const ax = Math.round(Math.min(s.h - 1, Math.max(0, sy(0)))) + 0.5, ay = Math.round(Math.min(s.w - 1, Math.max(0, sx(0)))) + 0.5;
      g.strokeStyle = axis;
      g.beginPath();
      if (v.ymin <= 0 && v.ymax >= 0) { g.moveTo(0, ax); g.lineTo(s.w, ax); }
      if (v.xmin <= 0 && v.xmax >= 0) { g.moveTo(ay, 0); g.lineTo(ay, s.h); }
      g.stroke();
      g.fillStyle = text;
      g.font = '11px system-ui, -apple-system, Segoe UI, sans-serif';
      g.textAlign = 'center';
      g.textBaseline = 'top';
      const ly = Math.min(s.h - 14, Math.max(2, ax + 3));
      for (const x of tx.values) if (x !== 0 || !(v.xmin <= 0 && v.xmax >= 0)) g.fillText(label(x, tx.step), sx(x), ly);
      g.textAlign = 'left';
      g.textBaseline = 'middle';
      const lx = ay + 4 > s.w - 40 ? s.w - 40 : ay + 4;
      for (const y of ty.values) if (y !== 0) g.fillText(label(y, ty.step), Math.max(2, lx), sy(y));
      // functions
      const clampY = (y) => Math.max(-1e4, Math.min(s.h + 1e4, sy(y)));
      g.lineWidth = 2;
      g.lineJoin = 'round';
      const d = this.data;
      const dark = /^#[0-3]/.test(bg); // (black and navy curves in the page's own text colour on a dark page)
      const color = (ser, k) => {
        const c = COLORS[(Number.isInteger(ser.color) ? ser.color : ser.name && /^y\d$/.test(ser.name) ? +ser.name[1] - 1 : k) % COLORS.length];
        return dark && (c === '#000000' || c === '#000080') ? text : c;
      };
      d.series.forEach((ser, k) => {
        g.strokeStyle = color(ser, k);
        g.lineWidth = ser.style === 1 ? 3.5 : ser.style === 2 ? 1 : 2; // (the calculator's thick and thin styles)
        if (g.setLineDash) g.setLineDash(ser.style === 3 ? [2, 4] : []);
        g.beginPath();
        for (const seg of segments(d.xmin, d.xmax, ser.ys, v)) {
          g.moveTo(sx(seg[0][0]), clampY(seg[0][1]));
          for (let i = 1; i < seg.length; i++) g.lineTo(sx(seg[i][0]), clampY(seg[i][1]));
        }
        g.stroke();
      });
      if (g.setLineDash) g.setLineDash([]);
      // legend
      g.font = '12px system-ui, -apple-system, Segoe UI, sans-serif';
      let y = 10;
      d.series.forEach((ser, k) => {
        const t = (ser.name || 'y' + (k + 1)) + ' = ' + ser.expr + (ser.error ? '  (' + ser.error + ')' : '');
        const w = Math.min(s.w - 20, g.measureText(t).width + 26);
        g.fillStyle = bg;
        g.globalAlpha = 0.85;
        g.fillRect(s.w - w - 8, y - 8, w, 17);
        g.globalAlpha = 1;
        g.fillStyle = color(ser, k);
        g.fillRect(s.w - w - 2, y - 1, 14, 3);
        g.fillStyle = text;
        g.textAlign = 'left';
        g.fillText(t, s.w - w + 16, y, w - 20);
        y += 18;
      });
      if (this.busy) {
        g.fillStyle = text;
        g.textAlign = 'left';
        g.fillText('sampling…', 8, s.h - 12);
      }
    }

    changed() {
      this.draw();
      if (this.onView) this.onView(Object.assign({}, this.view));
    }

    zoom(fx, fy, cx, cy) {
      const v = this.view;
      const px = cx === undefined ? (v.xmin + v.xmax) / 2 : cx, py = cy === undefined ? (v.ymin + v.ymax) / 2 : cy;
      const nv = { xmin: px - (px - v.xmin) * fx, xmax: px + (v.xmax - px) * fx, ymin: py - (py - v.ymin) * fy, ymax: py + (v.ymax - py) * fy };
      if (nv.xmax - nv.xmin < 1e-12 || nv.ymax - nv.ymin < 1e-12 || nv.xmax - nv.xmin > 1e12 || nv.ymax - nv.ymin > 1e12) return;
      this.view = nv;
      this.changed();
    }
    pan(dxFrac, dyFrac) {
      const v = this.view, w = v.xmax - v.xmin, h = v.ymax - v.ymin;
      this.view = { xmin: v.xmin - dxFrac * w, xmax: v.xmax - dxFrac * w, ymin: v.ymin + dyFrac * h, ymax: v.ymax + dyFrac * h };
      this.changed();
    }
    reset() { this.view = Object.assign({}, this.home); this.changed(); }

    bind() {
      const c = this.c;
      const pos = (e) => {
        const r = c.getBoundingClientRect();
        const v = this.view;
        return { fx: (e.clientX - r.left) / r.width, fy: (e.clientY - r.top) / r.height, r,
          x: v.xmin + ((e.clientX - r.left) / r.width) * (v.xmax - v.xmin), y: v.ymax - ((e.clientY - r.top) / r.height) * (v.ymax - v.ymin) };
      };
      c.addEventListener('wheel', (e) => {
        // the wheel scrolls the notebook until the plot is clicked (focus); a pinch always zooms
        if (!e.ctrlKey && globalThis.document && document.activeElement !== c) return;
        e.preventDefault();
        const p = pos(e);
        let dy = e.deltaY * (e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? 400 : 1);
        if (!e.ctrlKey && !e.shiftKey && !e.altKey && Math.abs(e.deltaX) > Math.abs(e.deltaY) && e.deltaMode === 0) {
          this.pan(-e.deltaX / p.r.width, 0); // trackpad: two-finger horizontal scroll pans
          return;
        }
        const f = Math.exp(dy * (e.ctrlKey ? 0.01 : 0.0015)); // ctrl: a trackpad pinch
        this.zoom(e.altKey ? 1 : f, e.shiftKey ? 1 : f, p.x, p.y);
      }, { passive: false });
      const pointers = new Map();
      let last = null, pinch = null;
      c.addEventListener('pointerdown', (e) => {
        c.focus({ preventScroll: true });
        c.setPointerCapture(e.pointerId);
        pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
        last = { x: e.clientX, y: e.clientY };
        if (pointers.size === 2) {
          const [a, b] = [...pointers.values()];
          pinch = { d: Math.hypot(a.x - b.x, a.y - b.y) };
        }
        c.classList.add('dragging');
      });
      c.addEventListener('pointermove', (e) => {
        if (!pointers.has(e.pointerId)) return;
        pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
        const r = c.getBoundingClientRect();
        if (pointers.size === 2 && pinch) {
          const [a, b] = [...pointers.values()];
          const d = Math.hypot(a.x - b.x, a.y - b.y);
          if (d > 0 && pinch.d > 0) {
            const p = pos({ clientX: (a.x + b.x) / 2, clientY: (a.y + b.y) / 2 });
            this.zoom(pinch.d / d, pinch.d / d, p.x, p.y);
            pinch.d = d;
          }
          return;
        }
        if (last) {
          this.pan((e.clientX - last.x) / r.width, (e.clientY - last.y) / r.height);
          last = { x: e.clientX, y: e.clientY };
        }
      });
      const up = (e) => {
        pointers.delete(e.pointerId);
        if (pointers.size < 2) pinch = null;
        if (!pointers.size) { last = null; c.classList.remove('dragging'); }
      };
      c.addEventListener('pointerup', up);
      c.addEventListener('pointercancel', up);
      c.addEventListener('dblclick', (e) => { e.preventDefault(); this.reset(); });
      c.addEventListener('keydown', (e) => {
        const k = e.key;
        if (k === '+' || k === '=') this.zoom(0.8, 0.8);
        else if (k === '-' || k === '_') this.zoom(1.25, 1.25);
        else if (k === '0') this.reset();
        else if (k === 'ArrowLeft') this.pan(0.1, 0);
        else if (k === 'ArrowRight') this.pan(-0.1, 0);
        else if (k === 'ArrowUp') this.pan(0, 0.1);
        else if (k === 'ArrowDown') this.pan(0, -0.1);
        else return;
        e.preventDefault();
        e.stopPropagation();
      });
    }
  }

  Object.assign(X, { Plot, ticks, tickLabel: label, segments, autoRange, PLOT_COLORS: COLORS });
})(typeof module !== 'undefined' && module.exports ? module.exports : (globalThis.CalcNB = globalThis.CalcNB || {}));
