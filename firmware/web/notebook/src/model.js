/*
 * The notebook as the page shows it: cells in memory, .ipynb (nbformat 4.5)
 * reading and writing. No DOM: the page (app.js) and the node tests (test/)
 * share all of it. Connected, the cells are the calculator's (sync.js turns
 * them into these); not connected, a file opened here is shown read-only.
 *
 * Cell kinds, and how they are stored (the convention of the calculator's own
 * Notebook app, so a notebook moves between the two):
 *   python    code cell, source as is
 *   markdown  markdown cell ($...$ and $$...$$ math)
 *   raw       raw cell (kept, shown as text)
 *   math      code cell "import calc" + one calc.eval(r"...") per line, with
 *             metadata {"calculator": {"kind": "math", "input": "<the lines as typed>"}}
 *   graph     code cell "import calc" + calc.plot(r"f", xmin, xmax) per
 *             function, metadata {"calculator": {"kind": "graph", "input":
 *             "<one function of x per line>", "xmin", "xmax"[, "ymin", "ymax"]}}
 *   graph3d   a 3D graph cell of the calculator: metadata kind "graph" with its
 *             "spec" ("3d ...", app/gcell.c), the calculator's picture as image/png
 * Outputs are kept in nbformat's own form (stream, execute_result,
 * display_data, error). A graph's display_data carries image/png (drawn by the
 * page when saving) and, in metadata.calculator, the sampled values, so the
 * plot stays interactive when the notebook is opened again.
 */
(function (X) {
  'use strict';
  const P = typeof require === 'function' && typeof module !== 'undefined' ? require('./proto.js') : globalThis.CalcNB;

  const KINDS = ['python', 'math', 'markdown', 'graph', 'graph3d', 'raw'];

  /* Calculator text -> Python, as the calculator's editor does for Python cells
     (modules/python/py_ui.c py_translate): ^ is a power (**), ² ³ ⁻¹ √ π ≤ ≥ ≠ × ÷ θ ∞ their
     Python spellings; strings and comments stay as typed. */
  const PY_TR = [['sin⁻¹', 'asin'], ['cos⁻¹', 'acos'], ['tan⁻¹', 'atan'], ['⁻¹', '**-1'], ['√', 'sqrt'], ['π', 'pi'],
    ['²', '**2'], ['³', '**3'], ['^', '**'], ['⁻', '-'], ['≤', '<='], ['≥', '>='], ['≠', '!='], ['→', '='], ['ᴇ', 'e'],
    ['∞', 'inf'], ['θ', 'theta'], ['×', '*'], ['÷', '/']];
  function pyTranslate(text) {
    let out = '', q = '', i = 0;
    while (i < text.length) {
      const ch = text[i];
      if (q) {
        if (ch === '\\' && i + 1 < text.length) { out += ch + text[i + 1]; i += 2; continue; }
        if (ch === q || ch === '\n') q = '';
        out += ch; i++; continue;
      }
      if (ch === "'" || ch === '"') { q = ch; out += ch; i++; continue; }
      if (ch === '#') { const e = text.indexOf('\n', i); const end = e < 0 ? text.length : e; out += text.slice(i, end); i = end; continue; }
      const t = PY_TR.find(([f]) => text.startsWith(f, i));
      if (t) { out += t[1]; i += t[0].length; } else { out += ch; i++; }
    }
    return out;
  }
  let idSource = null; // tests: a deterministic id generator

  function newId() {
    if (idSource) return idSource();
    const b = new Uint8Array(8);
    (globalThis.crypto || require('crypto').webcrypto).getRandomValues(b);
    return Array.from(b, (x) => x.toString(16).padStart(2, '0')).join('');
  }

  /* text, or nbformat's list of lines, -> one string */
  const joinText = (t) => (Array.isArray(t) ? t.join('') : t == null ? '' : String(t));
  /* nbformat's multiline string: lines with their newlines */
  function splitLines(s) {
    s = String(s);
    if (!s) return [];
    const out = s.split('\n').map((l, i, a) => (i < a.length - 1 ? l + '\n' : l));
    if (out[out.length - 1] === '') out.pop();
    return out;
  }

  function makeCell(kind, source, extra) {
    const c = {
      id: newId(), kind: KINDS.includes(kind) ? kind : 'python', source: source || '',
      outputs: [], execution_count: null, metadata: {},
      range: { xmin: -10, xmax: 10, ymin: null, ymax: null },
    };
    return Object.assign(c, extra || {});
  }

  class Notebook {
    constructor() {
      this.cells = [makeCell('python', '')];
      this.metadata = {};
      this.name = 'Untitled.ipynb';
      this.dirty = false;
    }
    index(cell) { return this.cells.indexOf(cell); }
    insert(at, kind, source) {
      const c = makeCell(kind, source);
      this.cells.splice(Math.max(0, Math.min(at, this.cells.length)), 0, c);
      this.dirty = true;
      return c;
    }
    remove(cell) {
      const i = this.index(cell);
      if (i < 0) return -1;
      this.cells.splice(i, 1);
      if (!this.cells.length) this.cells.push(makeCell('python', ''));
      this.dirty = true;
      return i;
    }
    move(cell, by) {
      const i = this.index(cell), j = i + by;
      if (i < 0 || j < 0 || j >= this.cells.length) return false;
      this.cells.splice(i, 1);
      this.cells.splice(j, 0, cell);
      this.dirty = true;
      return true;
    }
    setKind(cell, kind) {
      if (cell.kind === kind) return;
      cell.kind = kind;
      if (kind === 'markdown' || kind === 'raw') { cell.outputs = []; cell.execution_count = null; }
      this.dirty = true;
    }
    clearOutputs() {
      for (const c of this.cells) { c.outputs = []; c.execution_count = null; }
      this.dirty = true;
    }
  }

  /* ------------------------------------------------------------ CAS input */
  /* The calculator's display characters (a line typed on the calculator, as
   * its Notebook app stores it) -> CAS input. ∇ stays (the CAS reads it), and
   * so do the · and × right after it (∇·F divergence, ∇×F curl). */
  function toCas(s) {
    return String(s)
      .replace(/⁻¹/g, '^(-1)')
      .replace(/[−⁻]/g, '-').replace(/²/g, '^2').replace(/³/g, '^3')
      .replace(/(?<!∇ *)[×·]/g, '*').replace(/÷/g, '/').replace(/π/g, 'pi')
      .replace(/√/g, 'sqrt').replace(/≤/g, '<=').replace(/≥/g, '>=')
      .replace(/≠/g, '!=').replace(/θ/g, 'theta').replace(/→/g, '->');
  }

  /* A Python literal like the device writes: r"..." when that is exact. */
  function rawLiteral(s) {
    return !/["\n\r]/.test(s) && !/\\$/.test(s) ? 'r"' + s + '"' : P.pyStr(s);
  }

  const mathLines = (src) => String(src).split('\n').map((l) => l.trim()).filter((l) => l && !l.startsWith('#'));

  function mathSource(src) {
    return ['import calc'].concat(mathLines(src).map((l) => 'calc.eval(' + rawLiteral(toCas(l)) + ')')).join('\n');
  }

  const graphExprs = (src) => String(src).split('\n').map((l) => l.trim()).filter((l) => l && !l.startsWith('#'));

  function graphSource(cell) {
    const r = cell.range;
    return ['import calc'].concat(graphExprs(cell.source).map((e) =>
      'calc.plot(' + rawLiteral(toCas(e)) + ', ' + fmt(r.xmin) + ', ' + fmt(r.xmax) + ')')).join('\n');
  }
  const fmt = (v) => (Number.isFinite(v) ? String(v) : '0');

  /* "sin(x), -5, 5" (the calculator's one-line form) -> {expr, xmin, xmax} */
  function parseGraphLine(s) {
    let depth = 0;
    const cuts = [];
    for (let i = 0; i < s.length; i++) {
      const c = s[i];
      if ('([{'.includes(c)) depth++;
      else if (')]}'.includes(c)) depth--;
      else if (c === ',' && !depth) cuts.push(i);
    }
    if (cuts.length === 2) {
      const lo = parseFloat(toCas(s.slice(cuts[0] + 1, cuts[1]))), hi = parseFloat(toCas(s.slice(cuts[1] + 1)));
      if (Number.isFinite(lo) && Number.isFinite(hi) && lo < hi) return { expr: s.slice(0, cuts[0]).trim(), xmin: lo, xmax: hi };
    }
    return { expr: s.trim(), xmin: null, xmax: null };
  }

  /* ------------------------------------------------------------ .ipynb */
  function fromIpynb(json, name) {
    const d = typeof json === 'string' ? JSON.parse(json) : json;
    if (!d || typeof d !== 'object' || !Array.isArray(d.cells)) throw new Error('not a Jupyter notebook (no cells)');
    if (d.nbformat !== 4) throw new Error('nbformat ' + d.nbformat + ' is not supported (only version 4)');
    const nb = new Notebook();
    nb.name = name || 'Untitled.ipynb';
    nb.metadata = d.metadata && typeof d.metadata === 'object' ? JSON.parse(JSON.stringify(d.metadata)) : {};
    nb.cells = d.cells.map((c) => {
      const md = c.metadata && typeof c.metadata === 'object' ? JSON.parse(JSON.stringify(c.metadata)) : {};
      const cal = md.calculator && typeof md.calculator === 'object' ? md.calculator : null;
      const extra = { metadata: md };
      if (typeof c.id === 'string' && /^[a-zA-Z0-9_-]{1,64}$/.test(c.id)) extra.id = c.id;
      if (c.attachments) extra.attachments = c.attachments;
      let cell;
      if (c.cell_type === 'markdown') cell = makeCell('markdown', joinText(c.source), extra);
      else if (c.cell_type === 'raw') cell = makeCell('raw', joinText(c.source), extra);
      else if (c.cell_type === 'code' && cal && cal.kind === 'math' && typeof cal.input === 'string')
        cell = makeCell('math', cal.input, extra);
      else if (c.cell_type === 'code' && cal && cal.kind === 'graph' && typeof cal.spec === 'string' && /^3d[ \n]/.test(cal.spec)) {
        cell = makeCell('graph3d', typeof cal.input === 'string' ? cal.input : '', extra);
        cell.spec = cal.spec;
      } else if (c.cell_type === 'code' && cal && cal.kind === 'graph' && typeof cal.input === 'string') {
        cell = makeCell('graph', '', extra);
        const lines = graphExprs(cal.input);
        let xmin = num(cal.xmin), xmax = num(cal.xmax);
        if (lines.length === 1) { // the calculator's "f(x), a, b"
          const g = parseGraphLine(lines[0]);
          lines[0] = g.expr;
          if (xmin === null && g.xmin !== null) xmin = g.xmin, xmax = g.xmax;
        }
        cell.source = lines.join('\n');
        cell.range = {
          xmin: xmin !== null ? xmin : -10, xmax: xmax !== null && xmax > (xmin ?? -10) ? xmax : 10,
          ymin: num(cal.ymin), ymax: num(cal.ymax),
        };
      } else cell = makeCell('python', joinText(c.source), extra);
      if (c.cell_type === 'code') {
        cell.outputs = Array.isArray(c.outputs) ? c.outputs.map(normOutput) : [];
        cell.execution_count = Number.isInteger(c.execution_count) ? c.execution_count : null;
      }
      return cell;
    });
    if (!nb.cells.length) nb.cells.push(makeCell('python', ''));
    nb.dirty = false;
    return nb;
  }
  const num = (v) => (typeof v === 'number' && Number.isFinite(v) ? v : null);

  /* outputs as read: text fields joined (they are written back as line lists) */
  function normOutput(o) {
    o = JSON.parse(JSON.stringify(o || {}));
    if (o.output_type === 'stream') o.text = joinText(o.text);
    if (o.data) for (const k of Object.keys(o.data)) if (Array.isArray(o.data[k])) o.data[k] = o.data[k].join('');
    return o;
  }

  /* -> nbformat 4.5 object. opts.png(output) returns base64 PNG of a graph
   * output that has samples (the page draws them), opts.outputs = false
   * leaves outputs out. */
  function toIpynb(nb, opts = {}) {
    const withOut = opts.outputs !== false;
    const cells = nb.cells.map((c) => {
      const md = JSON.parse(JSON.stringify(c.metadata || {}));
      delete md.calculator;
      if (c.kind === 'markdown' || c.kind === 'raw') {
        const o = { cell_type: c.kind, id: c.id, metadata: md, source: splitLines(c.source) };
        if (c.attachments && c.kind === 'markdown') o.attachments = c.attachments;
        return o;
      }
      let source = c.source;
      if (c.kind === 'math') {
        md.calculator = { input: c.source, kind: 'math' };
        source = mathSource(c.source);
      } else if (c.kind === 'graph3d') { // (kept as the calculator wrote it: its spec, its picture)
        md.calculator = { input: c.source, kind: 'graph', spec: c.spec || (c.metadata.calculator || {}).spec || '' };
        source = 'import calc\n# 3D graph: ' + c.source.split('\n').filter(Boolean).map((e) => 'z = ' + e).join(', ');
      } else if (c.kind === 'graph') {
        const r = c.range;
        md.calculator = { input: c.source, kind: 'graph', xmax: r.xmax, xmin: r.xmin };
        if (r.ymin !== null && r.ymax !== null) md.calculator.ymin = r.ymin, md.calculator.ymax = r.ymax;
        source = graphSource(c);
      }
      return {
        cell_type: 'code', execution_count: withOut ? c.execution_count : null, id: c.id, metadata: md,
        outputs: withOut ? c.outputs.map((o) => writeOutput(o, opts)) : [], source: splitLines(source),
      };
    });
    const metadata = JSON.parse(JSON.stringify(nb.metadata || {}));
    metadata.kernelspec = metadata.kernelspec || { display_name: 'Python 3', language: 'python', name: 'python3' };
    metadata.language_info = metadata.language_info || { name: 'python' };
    metadata.calculator = Object.assign({}, metadata.calculator, { format: 1 }, opts.firmware ? { firmware: opts.firmware } : {});
    return { cells, metadata, nbformat: 4, nbformat_minor: 5 };
  }

  function writeOutput(o, opts) {
    o = JSON.parse(JSON.stringify(o));
    if (o.output_type === 'stream') o.text = splitLines(o.text);
    if (o.output_type === 'display_data' || o.output_type === 'execute_result') {
      o.metadata = o.metadata || {};
      const cal = o.metadata.calculator;
      if (cal && cal.series && opts.png) {
        const png = opts.png(o);
        if (png) o.data['image/png'] = png;
      }
      for (const k of Object.keys(o.data || {}))
        if (typeof o.data[k] === 'string' && (k.startsWith('text/') || k === 'image/svg+xml')) o.data[k] = splitLines(o.data[k]);
    }
    if (o.output_type === 'execute_result' && !Number.isInteger(o.execution_count)) o.execution_count = null;
    return o;
  }

  function stringify(nb, opts) { return JSON.stringify(toIpynb(nb, opts), null, 1) + '\n'; }

  Object.assign(X, {
    KINDS, Notebook, makeCell, fromIpynb, toIpynb, stringify, toCas, mathSource, graphSource,
    parseGraphLine, splitLines, joinText, rawLiteral, pyTranslate,
    setIdSource: (f) => { idSource = f; },
  });
})(typeof module !== 'undefined' && module.exports ? module.exports : (globalThis.CalcNB = globalThis.CalcNB || {}));
