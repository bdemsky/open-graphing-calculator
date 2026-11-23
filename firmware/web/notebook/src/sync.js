/*
 * The calculator's notebook, followed through the console (docs/NOTEBOOK.md
 * "The browser page"). The notebook is the one on the calculator's home screen
 * (unsaved, name "", or a named one); this is the page's copy of it, kept up
 * to date from the calculator's event lines, and the page's edits sent as
 * commands. No DOM: the page (app.js) and the node tests share it.
 *
 *   events   "# nb1 REV DIRTY TYPE [ARGS]" log lines (CalcLink.onLog): REV is
 *            the document revision, one more per event; a gap (a lost line, a
 *            restart) and the page reads the whole notebook again (resync)
 *   types    doc | cell ID | order | run ID|- | meta | list | lock ID calc|page |
 *            unlock ID | draft ID
 *   reading  nb doc (head= and one cell= line per cell, JSON), nb get ID,
 *            nb ids, nb head, nb draft, nb rev (also the keepalive: a page that
 *            says nothing for 15 s is no longer followed), nb sample ID N A B K
 *            (function K of a graph cell), nb sample3d ID NX NY K (a 3D cell's surface K on a
 *            grid), nb png ID (a graph cell's picture), nb frames ID NF N A B (an animated
 *            cell's functions at NF values of t), nb frames3d ID NF NX NY K (its surface K)
 *   writing  nb set ID TEXT (or py buf pieces + nb setbuf ID), nb spec ID TEXT (or
 *            nb specbuf ID: a graph cell's functions and window), nb insert AT KIND,
 *            nb del ID, nb move ID -1|1, nb kind ID KIND, nb lock ID, nb unlock,
 *            nb run ID, nb runall, nb restart, nb clear [ID], nb sample ID N A B,
 *            nb list, nb open NAME [card], nb new [NAME], nb rename A B,
 *            nb delete NAME [card], nb json (export), nb put begin NAME SIZE /
 *            nb put BASE64 / nb put end (import)
 *
 * Cells as the calculator has them: {id, kind: text|math|python|graph, lock:
 * ''|calc|page, src, out, val, tex, ename, evalue, tb, plot, spec, xmin, xmax,
 * count, ran}; spec: a graph cell of the home screen (its functions, window;
 * "3d ..." a 3D graph cell).
 */
(function (X) {
  'use strict';
  const P = typeof require === 'function' && typeof module !== 'undefined' ? require('./proto.js') : globalThis.CalcNB;

  const PROTO = 'nb1';
  const MAX_UPLOAD = 256 * 1024; // NBS_MAX_BYTES (modules/python/nb.h)

  /* "nb1 12 1 cell 0a1b2c3d" (a log line without its "# ") -> {rev, dirty, type, args} | null */
  function parseEvent(text) {
    const w = String(text).split(' ');
    if (w[0] !== PROTO || w.length < 4) return null;
    const rev = +w[1];
    if (!Number.isInteger(rev)) return null;
    return { rev, dirty: w[2] === '1', type: w[3], args: w.slice(4) };
  }

  /* the JSON after "key=" in reply lines */
  function jsonOf(lines, key) {
    const out = [];
    for (const l of lines) if (l.startsWith(key + '=')) out.push(JSON.parse(l.slice(key.length + 1)));
    return out;
  }

  /* a valid calculator notebook name from a file name (1-35 printable characters, no / \ : * ? " < > |) */
  function nameOf(file) {
    let n = String(file).replace(/\.ipynb$/i, '').replace(/[^\x20-\x7e]|[\\/:*?"<>|]/g, '_').trim().replace(/^\.+/, '');
    n = n.slice(0, 35).trim();
    return n || 'Imported';
  }

  class NbClient {
    constructor(link) {
      this.link = link;
      this.doc = null;      // {name ('' unsaved), where, open (named), rev, dirty, counter, running, note, cells: []}
      this.rev = -1;        // the revision this copy is at
      this.queue = [];
      this.pumping = false;
      this.handlers = {};
      this.drafts = new Map(); // id -> the calculator's edit as typed so far
      this.resyncs = 0;
    }

    on(name, f) { (this.handlers[name] = this.handlers[name] || []).push(f); return this; }
    emit(name, ...a) { for (const f of this.handlers[name] || []) { try { f(...a); } catch (e) { /* the page's */ } } }

    async cmd(c, opts) {
      const r = await this.link.command(c, opts);
      if (!r.ok) throw Object.assign(new Error(r.error), { calc: true });
      return r.lines;
    }

    /* the page follows the notebook: events from now on, then everything read */
    async start() {
      await this.cmd('nb sync on');
      await this.resync();
    }

    async stop() { try { await this.cmd('nb sync off'); } catch (e) { /* gone */ } }

    async resync() {
      this.resyncs++;
      const lines = await this.cmd('nb doc');
      const h = jsonOf(lines, 'head')[0];
      h.cells = jsonOf(lines, 'cell');
      this.doc = h;
      this.rev = this.doc.rev;
      this.drafts.clear();
      this.emit('doc', this.doc);
    }

    cell(id) { return this.doc && this.doc.cells.find((c) => c.id === id); }
    index(id) { return this.doc ? this.doc.cells.findIndex((c) => c.id === id) : -1; }

    /* a log line from the link: true when it was an event */
    handleLog(text) {
      const ev = parseEvent(text);
      if (!ev) return false;
      this.queue.push(ev);
      this.pump();
      return true;
    }

    async pump() {
      if (this.pumping) return;
      this.pumping = true;
      try {
        while (this.queue.length) {
          const ev = this.queue.shift();
          if (ev.rev <= this.rev) continue; // (already in what was read)
          if (ev.rev !== this.rev + 1 || !this.doc) { await this.resync(); continue; } // a missed event
          await this.apply(ev);
          this.rev = Math.max(this.rev, ev.rev);
          if (this.doc) this.doc.dirty = ev.dirty;
          this.emit('dirty', ev.dirty);
        }
      } catch (e) {
        this.emit('error', e);
      } finally {
        this.pumping = false;
      }
    }

    async apply(ev) {
      const d = this.doc, id = ev.args[0];
      switch (ev.type) {
        case 'doc': await this.resync(); return;
        case 'cell': {
          const c = jsonOf(await this.cmd('nb get ' + id).catch(() => []), 'cell')[0];
          const i = this.index(id);
          if (c && i >= 0) { d.cells[i] = c; this.emit('cell', c); }
          return;
        }
        case 'order': {
          const ids = ((await this.cmd('nb ids')).find((l) => l.startsWith('ids=')) || 'ids=').slice(4).split(' ').filter(Boolean);
          const have = new Map(d.cells.map((c) => [c.id, c]));
          const cells = [];
          for (const k of ids) {
            let c = have.get(k);
            if (!c) c = jsonOf(await this.cmd('nb get ' + k), 'cell')[0];
            if (c) cells.push(c);
          }
          d.cells = cells;
          this.emit('order', cells);
          return;
        }
        case 'run': d.running = id === '-' ? '' : id; this.emit('run', d.running); return;
        case 'meta': {
          const h = jsonOf(await this.cmd('nb head'), 'head')[0];
          if (h && h.open && d.open && h.name !== d.name) { await this.resync(); return; }
          Object.assign(d, { name: h.name, where: h.where, open: h.open, counter: h.counter, note: h.note });
          this.emit('meta', d);
          return;
        }
        case 'list': this.emit('list'); return;
        case 'lock':
        case 'unlock': {
          const c = this.cell(id);
          if (c) c.lock = ev.type === 'lock' ? ev.args[1] : '';
          if (ev.type === 'unlock') this.drafts.delete(id);
          this.emit('lock', id, c ? c.lock : '');
          return;
        }
        case 'draft': {
          const r = await this.cmd('nb draft');
          const l = r.find((x) => x.startsWith('draft='));
          if (l) { this.drafts.set(id, JSON.parse(l.slice(6))); this.emit('draft', id, this.drafts.get(id)); }
          return;
        }
        default: return; // (a newer calculator: an event this page does not know)
      }
    }

    /* the keepalive (every 2 s): a revision the events did not bring, or a calculator that
     * stopped following (restarted): read everything again */
    async poll() {
      const r = await this.link.kv('nb rev', { timeout: 5000 });
      if (r.error) return;
      if (r.sync === '0') { await this.cmd('nb sync on'); this.queue = []; await this.resync(); return; }
      if (!this.pumping && !this.queue.length && +r.rev !== this.rev) await this.resync();
      if (this.doc && r.dirty !== undefined && this.doc.dirty !== (r.dirty === '1') && !this.pumping) {
        this.doc.dirty = r.dirty === '1';
        this.emit('dirty', this.doc.dirty);
      }
    }

    /* ---- the list of notebooks ---- */
    async list() {
      return (await this.cmd('nb list')).filter((l) => l.startsWith('nb=')).map((l) => {
        const m = /^nb=(.*) (\d+) (flash|card) (\d+)$/.exec(l);
        return m ? { name: m[1], size: +m[2], where: m[3], versions: +m[4] } : null;
      }).filter(Boolean);
    }
    open(name, card) { return this.cmd('nb open ' + P.quote(name) + (card ? ' card' : '')); }
    create(name) { return this.cmd('nb new' + (name ? ' ' + P.quote(name) : '')); }
    rename(from, to, card) { return this.cmd('nb rename ' + P.quote(from) + ' ' + P.quote(to) + (card ? ' card' : '')); }
    saveAs(name) { return this.cmd('nb saveas ' + P.quote(name)); } // (the unsaved notebook gets a name)
    remove(name, card) { return this.cmd('nb delete ' + P.quote(name) + (card ? ' card' : '')); }
    close() { return this.cmd('nb close'); }

    /* ---- cells ---- */
    async setSource(id, text) {
      text = String(text).replace(/\r\n?/g, '\n');
      const one = 'nb set ' + id + ' ' + P.quote(text);
      if (P.utf8len(one) <= P.LINE_MAX && !/[\x00-\x08\x0b-\x1f\x7f]/.test(text)) return this.cmd(one);
      await this.cmd('py clear');
      for (const piece of P.chunks(text)) await this.cmd('py buf ' + P.quote(piece));
      return this.cmd('nb setbuf ' + id);
    }
    async insert(at, kind) {
      const lines = await this.cmd('nb insert ' + (at | 0) + ' ' + kind);
      const l = lines.find((x) => x.startsWith('id='));
      return l ? l.slice(3) : null;
    }
    del(id) { return this.cmd('nb del ' + id); }
    move(id, by) { return this.cmd('nb move ' + id + ' ' + (by < 0 ? -1 : 1)); }
    kind(id, k) { return this.cmd('nb kind ' + id + ' ' + k); }
    lock(id) { return this.cmd('nb lock ' + id); }
    unlock(id) { return this.cmd('nb unlock' + (id ? ' ' + id : '')); }
    run(id) { return this.cmd('nb run ' + id); }
    runAll() { return this.cmd('nb runall'); }
    restart() { return this.cmd('nb restart'); }
    clear(id) { return this.cmd('nb clear' + (id ? ' ' + id : '')); }

    /* the cell's plotted function (k: function k of a graph cell's spec) at n points of [a, b] (the calculator's CAS) */
    async sample(id, n, a, b, k) {
      const lines = await this.cmd('nb sample ' + id + ' ' + (n | 0) + (a !== undefined ? ' ' + a + ' ' + b + (k ? ' ' + k : '') : ''));
      const ys = [];
      let xmin = a, xmax = b, plot = '';
      for (const l of lines) {
        if (l.startsWith('ys=')) for (const t of l.slice(3).split(' ')) ys.push(t === 'n' ? NaN : parseFloat(t));
        else if (l.startsWith('range=')) [xmin, xmax] = l.slice(6).split(' ').map(Number);
        else if (l.startsWith('plot=')) plot = l.slice(5);
      }
      return { plot, xmin, xmax, ys };
    }

    /* a graph cell's spec (functions, window, style; "3d ..." a 3D graph), as text */
    async setSpec(id, text) {
      const one = 'nb spec ' + id + ' ' + P.quote(text);
      if (P.utf8len(one) <= P.LINE_MAX) return this.cmd(one);
      await this.cmd('py clear');
      for (const piece of P.chunks(text)) await this.cmd('py buf ' + P.quote(piece));
      return this.cmd('nb specbuf ' + id);
    }

    /* a 3D cell's surface z = f(x,y) (its function k) on an nx x ny grid over its x, y range:
     * {k, nx, ny, w: [xmin, xmax, ymin, ymax, zmin, zmax], zs: [row by row, NaN: none],
     *  edges: {node index: [x, y, z]} (nodes with no value moved onto the edge of the domain)} */
    async sample3d(id, nx, ny, k) {
      const lines = await this.cmd('nb sample3d ' + id + ' ' + (nx | 0) + ' ' + (ny | 0) + (k ? ' ' + k : ''));
      const r = { k: 0, nx: 0, ny: 0, w: null, zs: [], edges: {} };
      for (const l of lines) {
        if (l.startsWith('zs=')) for (const t of l.slice(3).split(' ')) r.zs.push(t === 'n' ? NaN : parseFloat(t));
        else if (l.startsWith('edge=')) {
          const v = l.slice(5).split(' ').map(Number);
          if (v.length === 4 && v.every(Number.isFinite)) r.edges[v[0]] = [v[1], v[2], v[3]];
        }
        else if (l.startsWith('grid=')) {
          const v = l.slice(5).split(' ').map(Number);
          [r.k, r.nx, r.ny] = v;
          r.w = v.slice(3, 9);
        }
      }
      return r;
    }

    /* An animated 2D cell (its functions use t): the functions that are on at n points of [a, b]
     * for nf values of t over its range (both ends), one round trip:
     * {nf, n, xmin, xmax, tmin, tmax, ks: [function k...], frames: [frame][function][n] (NaN: none)} */
    async frames(id, nf, n, a, b) {
      const lines = await this.cmd('nb frames ' + id + ' ' + (nf | 0) + ' ' + (n | 0) + (a !== undefined ? ' ' + a + ' ' + b : ''),
        { timeout: 60000 });
      const r = { nf: 0, n: 0, xmin: a, xmax: b, tmin: 0, tmax: 2 * Math.PI, ks: [], frames: [] };
      const vals = [];
      for (const l of lines) {
        if (l.startsWith('ys=')) for (const t of l.slice(3).split(' ')) vals.push(t === 'n' ? NaN : parseFloat(t));
        else if (l.startsWith('frames=')) {
          const v = l.slice(7).split(' ').map(Number);
          [r.nf, r.n, r.xmin, r.xmax, r.tmin, r.tmax] = v;
          r.ks = v.slice(6);
        }
      }
      const nk = r.ks.length;
      for (let f = 0; f < r.nf; f++) {
        const fr = [];
        for (let q = 0; q < nk; q++) fr.push(vals.slice((f * nk + q) * r.n, (f * nk + q + 1) * r.n));
        r.frames.push(fr);
      }
      return r;
    }

    /* An animated 3D cell's surface z = f(x,y,t) (its function k) on an nx x ny grid for nf values
     * of t: {k, nx, ny, w, tmin, tmax, nf, frames: [frame][zs row by row]} */
    async frames3d(id, nf, nx, ny, k) {
      const lines = await this.cmd('nb frames3d ' + id + ' ' + (nf | 0) + ' ' + (nx | 0) + ' ' + (ny | 0) + (k ? ' ' + k : ''),
        { timeout: 60000 });
      const r = { k: 0, nx: 0, ny: 0, w: null, nf: 0, tmin: 0, tmax: 2 * Math.PI, frames: [] };
      const vals = [];
      for (const l of lines) {
        if (l.startsWith('zs=')) for (const t of l.slice(3).split(' ')) vals.push(t === 'n' ? NaN : parseFloat(t));
        else if (l.startsWith('grid=')) {
          const v = l.slice(5).split(' ').map(Number);
          [r.k, r.nx, r.ny] = v;
          r.w = v.slice(3, 9);
        } else if (l.startsWith('frames=')) [r.nf, r.tmin, r.tmax] = l.slice(7).split(' ').map(Number);
      }
      const m = r.nx * r.ny;
      for (let f = 0; f < r.nf; f++) r.frames.push(vals.slice(f * m, (f + 1) * m));
      return r;
    }

    /* a graph cell's picture as the calculator draws it: base64 PNG */
    async png(id) {
      return (await this.cmd('nb png ' + id)).filter((l) => l.startsWith('png=')).map((l) => l.slice(4)).join('');
    }

    /* ---- files ---- */
    async exportJson() {
      const lines = await this.cmd('nb json', { timeout: 60000 });
      return lines.join('\n') + '\n';
    }

    /* an .ipynb (bytes) into the calculator's store as `name`, then opened there */
    async importIpynb(name, bytes, onProgress) {
      if (bytes.length > MAX_UPLOAD) throw new Error('too large for the calculator (the limit is 256 KB)');
      await this.cmd('nb put begin ' + P.quote(name) + ' ' + bytes.length);
      try {
        const step = 225; // 300 base64 characters a line
        for (let i = 0; i < bytes.length; i += step) {
          await this.cmd('nb put ' + P.b64encode(bytes.subarray(i, i + step)));
          if (onProgress) onProgress(Math.min(1, (i + step) / bytes.length));
        }
      } catch (e) {
        await this.cmd('nb put abort').catch(() => {});
        throw e;
      }
      await this.cmd('nb put end', { timeout: 60000 });
    }
  }

  /* A graph cell's spec (app/gcell.c; docs/NOTEBOOK.md "Graph cells"): one item a line.
   *   2D  w XMIN XMAX YMIN YMAX XSCL YSCL / o GRID AXES HEIGHT / f ON COLOR STYLE EXPR (up to six)
   *   3D  3d AZIM ELEV ZOOM STYLE COLORMODE AXES BOX GRID TSTEPS / w XMIN XMAX YMIN YMAX ZMIN ZMAX /
   *       p UMIN UMAX VMIN VMAX TMIN TMAX / o 0 0 HEIGHT / f ON COLOR KIND EXPR[TAB EXPR TAB EXPR]
   *       (KIND 0: z = f(x,y); 1: x, y, z of (u, v); 2: x, y, z of t) / c CONTOURS MAP LEVELS
   *       (contours and the contour map, drawn by the calculator only: kept as they are)
   *   both  a TMIN TMAX SECS MODE: an animated cell (a function that is on uses t; 3D: not a
   *       curve's own t): t's range, seconds per sweep, MODE 0 loop, 1 bounce, 2 once
   * -> {d3, w: [...], scl, o, view, p, fns: [{k, on, color, style | kind, expr, parts}],
   *     a: the "a" line's settings or null, anim: the settings when animated (else null)}; null: none */
  function parseSpec(spec) {
    if (!spec) return null;
    const d3 = /^3d[ \n]/.test(spec) || spec === '3d';
    const r = { d3, w: null, scl: [1, 1], o: d3 ? [0, 0, 120] : [0, 1, 120], view: null, p: null, fns: [], a: null, anim: null };
    let k = 0;
    const nums = (w) => w.map(Number).filter((x) => Number.isFinite(x));
    for (const l of spec.split('\n')) {
      const w = l.split(' ');
      if (w[0] === '3d') { const v = nums(w.slice(1)); if (v.length >= 9) r.view = v.slice(0, 9); }
      else if (w[0] === 'w') { const v = nums(w.slice(1)); if (v.length >= 4) { r.w = v.slice(0, d3 ? 6 : 4); if (!d3 && v.length >= 6) r.scl = v.slice(4, 6); } }
      else if (w[0] === 'p') { const v = nums(w.slice(1)); if (v.length >= 6) r.p = v.slice(0, 6); }
      else if (w[0] === 'o') { const v = nums(w.slice(1)); if (v.length >= 3) r.o = v.slice(0, 3); }
      else if (w[0] === 'c' && d3) r.c = l;
      else if (w[0] === 'a') {
        const v = nums(w.slice(1));
        if (v.length >= 4 && v[0] < v[1] && v[2] >= 0.1 && v[2] <= 3600) r.a = { tmin: v[0], tmax: v[1], secs: v[2], mode: v[3] >= 0 && v[3] < 3 ? Math.floor(v[3]) : 0 };
      }
      else if (w[0] === 'f' && w.length > 4) {
        k++;
        const expr = w.slice(4).join(' '), f = { k, on: w[1] === '1', color: +w[2], expr };
        if (d3) { f.kind = +w[3]; f.parts = expr.split('\t'); } else f.style = +w[3];
        r.fns.push(f);
      }
    }
    if (!r.w) r.w = d3 ? [-5, 5, -5, 5, -5, 5] : [-10, 10, -10, 10];
    if (d3 && !r.view) r.view = [-60, 25, 1, 1, 0, 1, 1, 24, 200];
    if (d3 && !r.p) r.p = [0, 2 * Math.PI, 0, 2 * Math.PI, 0, 2 * Math.PI];
    if (r.fns.some((f) => f.on && !(d3 && f.kind === 2) && usesT(f.expr))) r.anim = Object.assign({}, r.a || ANIM_DEFAULT);
    return r;
  }

  /* An expression (as typed) uses t, as the calculator decides (app/gcell.c names_t): an identifier
   * (a letter or _, then letters, digits, _) that is just "t" and is not called */
  function usesT(expr) {
    for (const m of String(expr).matchAll(/[A-Za-z_][A-Za-z0-9_]*/g)) if (m[0] === 't' && expr[m.index + 1] !== '(') return true;
    return false;
  }
  const ANIM_DEFAULT = { tmin: 0, tmax: 2 * Math.PI, secs: 4, mode: 0 };
  const ANIM_MODES = ['loop', 'bounce', 'once'];

  const g15 = (v) => (Number.isFinite(v) ? String(+v.toPrecision(15)) : '0');

  /* A spec from the page's edit of a graph cell: `old` (the spec it had, or null), its functions
   * one a line (`exprs`, empty lines skipped), the window `w` (2D: 4 numbers, 3D: 6), d3: a 3D graph,
   * anim: animation settings {tmin, tmax, secs, mode} (default: the old spec's, else 0..2π, 4 s, loop;
   * written when a function uses t). A function keeps the colour and style of the one it replaces;
   * a new one gets the next colour. */
  function buildSpec(old, exprs, w, d3, anim) {
    const o = old && parseSpec(old);
    const same = o && o.d3 === !!d3 ? o : null;
    const fns = exprs.map((e) => String(e).replace(/\t/g, ' ').trim()).filter(Boolean).slice(0, 6);
    const lines = [];
    if (d3) {
      const v = same ? same.view : [-60, 25, 1, 1, 0, 1, 1, 24, 200];
      lines.push('3d ' + v.map(g15).join(' '));
      lines.push('w ' + w.slice(0, 6).map(g15).join(' '));
      lines.push('p ' + (same ? same.p : [0, 2 * Math.PI, 0, 2 * Math.PI, 0, 2 * Math.PI]).map(g15).join(' '));
      lines.push('o 0 0 ' + (o ? o.o[2] : 120));
      if (same && same.c) lines.push(same.c);
      fns.forEach((e, i) => {
        const f = same && same.fns[i];
        lines.push('f ' + (f ? (f.on ? 1 : 0) + ' ' + f.color : '1 ' + (i % 8)) + ' 0 ' + e);
      });
    } else {
      lines.push('w ' + w.slice(0, 4).map(g15).join(' ') + ' ' + (same ? same.scl : [1, 1]).map(g15).join(' '));
      lines.push('o ' + (same ? same.o : [0, 1, o ? o.o[2] : 120]).join(' '));
      fns.forEach((e, i) => {
        const f = same && same.fns[i];
        lines.push('f ' + (f ? (f.on ? 1 : 0) + ' ' + f.color + ' ' + (f.style || 0) : '1 ' + (i % 8) + ' 0') + ' ' + e);
      });
    }
    const sp = parseSpec(lines.join('\n'));
    if (sp && sp.anim) {
      const a = anim || (o && o.a) || ANIM_DEFAULT;
      lines.push('a ' + [a.tmin, a.tmax, a.secs].map(g15).join(' ') + ' ' + (a.mode | 0));
    }
    return lines.join('\n');
  }

  /* A calculator cell -> the page's cell (model.js: kind, source, nbformat outputs) */
  const KIND_IN = { text: 'markdown', math: 'math', python: 'python', graph: 'graph' };
  const KIND_OUT = { markdown: 'text', math: 'math', python: 'python', graph: 'graph', graph3d: 'graph', raw: 'text' };
  /* the page's kind of a calculator cell (a graph cell with a 3D spec: graph3d) */
  const kindOf = (c) => (c.kind === 'graph' && c.spec && parseSpec(c.spec).d3 ? 'graph3d' : KIND_IN[c.kind] || 'markdown');
  function outputsOf(c) {
    const o = [];
    if (c.out) o.push({ output_type: 'stream', name: 'stdout', text: c.out });
    const sp = c.kind === 'graph' ? parseSpec(c.spec) : null;
    if (c.kind === 'graph') {
      const on = sp ? sp.fns.filter((f) => f.on && f.expr) : [];
      if (!sp || !on.length) { // no spec (an older cell): the calculator's picture; no function: no plot on either side
        const empty = !sp ? !String(c.src || '').trim() : true;
        o.push({ output_type: 'display_data', data: { 'text/plain': empty ? '<graph: no function>' : '<graph of ' + c.src + '>' },
          metadata: { calculator: empty ? { empty: true, d3: !!(sp && sp.d3), cell: c.id } : { picture: true, cell: c.id, spec: c.src } } });
        return o;
      }
      if (sp.d3) { // z = f(x,y) surfaces drawn here (sampled on the calculator); anything else: its picture
        const surf = on.filter((f) => f.kind === 0);
        const meta = surf.length === on.length ? { surface: true, fns: surf, w: sp.w, view: sp.view, cell: c.id, spec: c.spec, anim: sp.anim }
          : { picture: true, cell: c.id, spec: c.spec, anim: sp.anim };
        o.push({ output_type: 'display_data', data: { 'text/plain': '<3D graph of ' + on.map((f) => 'z=' + f.parts.join(', ')).join(', ') + '>' },
          metadata: { calculator: meta } });
        return o;
      }
      // a graph cell of the home screen: its functions in its window, each sampled
      o.push({ output_type: 'display_data', data: { 'text/plain': '<graph of ' + on.map((f) => f.expr).join(', ') + '>' },
        metadata: { calculator: { plot: c.plot, fns: on, xmin: sp.w[0], xmax: sp.w[1], ymin: sp.w[2], ymax: sp.w[3], cell: c.id, spec: c.spec, anim: sp.anim } } });
      return o;
    }
    if (c.plot && !c.ename) {
      o.push({ output_type: 'display_data', data: { 'text/plain': '<plot of ' + c.plot + ' for ' + c.xmin + ' ≤ x ≤ ' + c.xmax + '>' },
        metadata: { calculator: { plot: c.plot, xmin: c.xmin, xmax: c.xmax, ymin: null, ymax: null, cell: c.id } } });
    }
    if (c.ename) {
      const tb = c.tb ? c.tb.replace(/\n$/, '').split('\n') : [c.ename + (c.evalue ? ': ' + c.evalue : '')];
      o.push({ output_type: 'error', ename: c.ename, evalue: c.evalue, traceback: tb });
    } else if (c.val) {
      const data = { 'text/plain': c.val };
      if (c.tex) data['text/latex'] = '$' + c.tex + '$';
      o.push({ output_type: 'execute_result', execution_count: c.count || null, data, metadata: {} });
    }
    return o;
  }

  Object.assign(X, { NbClient, parseEvent, jsonOf, nameOf, outputsOf, parseSpec, buildSpec, usesT, kindOf, KIND_IN, KIND_OUT, MAX_UPLOAD,
    ANIM_DEFAULT, ANIM_MODES });

})(typeof module !== 'undefined' && module.exports ? module.exports : (globalThis.CalcNB = globalThis.CalcNB || {}));
