/*
 * The page: the calculator's notebook in a Jupyter-style view, over the USB
 * serial port (Web Serial: Chrome, Edge). The notebook is the one on the
 * calculator's home screen (unsaved or named); this page is a second view and
 * editor of it (sync.js): both always show the same notebook, edits and runs
 * from either side appear on the other, cells run once, on the calculator.
 * Connecting leaves the calculator on the screen it shows. Not
 * connected, the page shows the last state it saw, or a notebook file
 * opened here, read-only; without Web Serial (Safari, Firefox) it opens
 * notebooks to read them. It makes no network request (see the
 * Content-Security-Policy in page.html).
 *
 * Modes as in Jupyter: command mode (blue bar: keys act on cells) and edit
 * mode (green bar: typing into the cell). A cell being edited here is locked
 * on the calculator, and one edited on the calculator is read-only here. The
 * keys are listed in the help dialog and in the manual.
 */
(function () {
  'use strict';
  const N = globalThis.CalcNB;
  const $ = (s, r) => (r || document).querySelector(s);
  const h = (tag, attrs, ...kids) => {
    const e = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs || {})) {
      if (k === 'class') e.className = v;
      else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
      else if (v === true) e.setAttribute(k, '');
      else if (v !== false && v != null) e.setAttribute(k, v);
    }
    for (const k of kids.flat()) if (k != null) e.append(k.nodeType ? k : document.createTextNode(String(k)));
    return e;
  };
  const store = {
    get(k) { try { return localStorage.getItem('calcnb.' + k); } catch (e) { return null; } },
    set(k, v) { try { localStorage.setItem('calcnb.' + k, v); } catch (e) { /* private window */ } },
  };

  const KIND_LABEL = { python: 'Python', math: 'Math', markdown: 'Text', graph: 'Graph', graph3d: '3D graph', raw: 'Raw' };
  const isGraph = (k) => k === 'graph' || k === 'graph3d';
  const HAS_SERIAL = !!(navigator.serial && navigator.serial.requestPort);
  const HAS_FS = !!(window.showOpenFilePicker && window.showSaveFilePicker);

  const S = {
    nb: new N.Notebook(), sel: 0, mode: 'command', views: new Map(),
    link: null, client: null, port: null, writer: null, reader: null, wanted: false, reconnectTimer: null,
    version: '', clip: null, deleted: [], handle: null, lastKey: '', lastKeyAt: 0,
    live: false,          // the cells shown are the calculator's open notebook
    editing: null,        // the id of the cell this page edits (locked on the calculator)
    pushTimer: null, ops: Promise.resolve(), pendingSelect: null, recall: null,
    samples: new Map(), pictures: new Map(), logLines: [],
  };
  const doc = () => (S.client && S.client.doc) || null;
  const readOnly = () => !S.live || !doc();
  const docName = (d) => (d.open ? d.name : 'Unsaved');

  /* ------------------------------------------------------------ math */
  function tex(t, display) {
    try {
      return katex.renderToString(t, { displayMode: !!display, throwOnError: false, output: 'htmlAndMathml', trust: false, strict: 'ignore' });
    } catch (e) {
      return '<code>' + N.escapeHtml(t) + '</code>';
    }
  }
  const stripDollars = (s) => String(s).trim().replace(/^\$\$([\s\S]*)\$\$$/, '$1').replace(/^\$([\s\S]*)\$$/, '$1');
  const stripAnsi = (s) => String(s).replace(/\u001b\[[0-9;]*[A-Za-z]/g, '');

  /* ------------------------------------------------------------ status, toasts */
  function setStatus(state, text) {
    const c = $('#conn');
    c.dataset.state = state;
    $('#conntext').textContent = text;
    $('#btn-connect').textContent = state === 'off' ? 'Connect' : 'Disconnect';
    $('#btn-connect').disabled = !HAS_SERIAL;
    document.body.classList.toggle('connected', state === 'on' || state === 'busy');
  }
  function refreshStatus() {
    if (!S.link) return;
    const d = doc();
    if (!S.live) setStatus('wait', 'Connecting…');
    else if (d && d.running) setStatus('busy', 'Running on the calculator…');
    else setStatus('on', 'Connected' + (S.version ? ': calculator ' + S.version : ''));
  }
  let toastTimer = null;
  function toast(msg, ms) {
    const t = $('#toast');
    t.textContent = msg;
    t.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { t.hidden = true; }, ms || 3500);
  }
  function log(text) {
    S.logLines.push(text);
    if (S.logLines.length > 200) S.logLines.shift();
  }

  /* ------------------------------------------------------------ connection */
  async function connect() {
    if (!HAS_SERIAL) return;
    try {
      const port = await navigator.serial.requestPort({ filters: [{ usbVendorId: N.VID, usbProductId: N.PID }] });
      S.wanted = true;
      await openPort(port, true);
    } catch (e) {
      if (e && e.name !== 'NotFoundError') toast('Could not connect: ' + e.message);
    }
  }

  const isCalc = (p) => { const i = p.getInfo ? p.getInfo() : {}; return i.usbVendorId === N.VID && i.usbProductId === N.PID; };

  /* first: the user connected (or the page opened) */
  async function openPort(port, first) {
    if (S.link) return;
    setStatus('wait', 'Connecting…');
    try {
      await port.open({ baudRate: 115200, bufferSize: 65536 });
    } catch (e) {
      if (!/already open/i.test(e.message || '')) throw e;
    }
    try { await port.setSignals({ dataTerminalReady: true, requestToSend: true }); } catch (e) { /* not every driver */ }
    S.port = port;
    S.writer = port.writable.getWriter();
    const link = new N.CalcLink((bytes) => S.writer.write(bytes));
    link.onLog = (t) => log(t);
    S.link = link;
    readLoop(port, link);
    clearInterval(S.reconnectTimer);
    S.reconnectTimer = null;
    try {
      await S.writer.write(new Uint8Array([10])); // ends any half line on the calculator
      const v = await link.version();
      S.version = (v.version || '').split(' ')[0];
      await startSync(link, first);
    } catch (e) {
      log('connecting: ' + e.message);
    }
    refreshStatus();
    updateToolbar();
  }

  /* Follow the calculator's notebook (its home screen's): events, then all of it read. The calculator
   * stays where it is. */
  async function startSync(link, first) {
    const c = new N.NbClient(link);
    link.onLog = (t) => { if (!c.handleLog(t)) log(t); };
    c.on('doc', onDoc).on('cell', onCell).on('order', onOrder).on('run', onRun).on('meta', onMeta)
      .on('lock', onLock).on('draft', onDraft).on('dirty', () => { updateTitle(); markDirty(); })
      .on('list', () => { if ($('#notebooks').open) fillList(); })
      .on('error', (e) => log('notebook: ' + e.message));
    S.client = c;
    try {
      await c.start();
    } catch (e) {
      S.client = null;
      toast('The calculator has no Notebook: ' + e.message + ' (it needs the Python module: Memory > Modules)', 8000);
      return;
    }
    if (S.link !== link) return;
    S.live = true;
    onDoc();
  }

  async function readLoop(port, link) {
    let reader;
    try {
      reader = port.readable.getReader();
      S.reader = reader;
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value) link.feed(value);
      }
    } catch (e) {
      log('serial port: ' + e.message);
    } finally {
      try { reader && reader.releaseLock(); } catch (e) { /* gone */ }
      lost(port, link);
    }
  }

  async function lost(port, link) {
    if (S.link !== link) return;
    link.close('the calculator was disconnected');
    S.link = null;
    S.client = null;
    goOffline();
    try { S.writer && S.writer.releaseLock(); } catch (e) { /* gone */ }
    S.writer = null;
    S.reader = null;
    try { await port.close(); } catch (e) { /* gone */ }
    S.port = null;
    if (S.wanted) {
      setStatus('wait', 'Reconnecting…');
      startReconnect();
    } else setStatus('off', 'Not connected');
  }

  /* the last state stays, read-only */
  function goOffline() {
    S.live = false;
    S.editing = null;
    clearTimeout(S.pushTimer);
    S.pushTimer = null;
    for (const v of S.views.values()) { v.running = false; v.update(); }
    if (S.mode === 'edit') S.mode = 'command';
    updateToolbar();
    markSelection();
  }

  /* The calculator leaves the bus for a moment (the drive's refresh, a restart): permitted
   * ports come back by themselves; the notebook is then read again. */
  function startReconnect() {
    if (S.reconnectTimer) return;
    S.reconnectTimer = setInterval(tryReconnect, 500);
  }
  let reconnecting = false;
  async function tryReconnect() {
    if (S.link || !S.wanted || reconnecting) return;
    reconnecting = true;
    try {
      const ports = (await navigator.serial.getPorts()).filter(isCalc);
      for (const p of ports) {
        try { await openPort(p, false); log('reconnected'); break; } catch (e) { S.port = null; setStatus('wait', 'Waiting for the calculator…'); }
      }
    } finally { reconnecting = false; }
  }

  async function disconnect() {
    S.wanted = false;
    clearInterval(S.reconnectTimer);
    S.reconnectTimer = null;
    if (S.client) { await endEdit(); await Promise.race([S.client.stop(), new Promise((r) => setTimeout(r, 1000))]); }
    const r = S.reader;
    if (r) { try { await r.cancel(); } catch (e) { /* closed */ } } else setStatus('off', 'Not connected');
  }

  /* the keepalive: a missed event shows as another revision */
  setInterval(() => {
    if (S.client && S.live && S.link && !S.link.busy) S.client.poll().catch((e) => log('poll: ' + e.message));
  }, 2000);

  /* ------------------------------------------------------------ the calculator's notebook -> the page */
  /* A graph cell as the page edits it: its functions one a line (all of them, those switched off
   * too, so each keeps its colour and style), its window; a 3D cell with a parametric entry is
   * changed on the calculator only (graphRO). */
  function graphView(c, v) {
    const sp = N.parseSpec(c.spec);
    if (!sp) {
      if (S.editing !== c.id) v.source = c.src;
      v.range = { xmin: c.xmin, xmax: c.xmax, ymin: -10, ymax: 10 };
      v.graphRO = false;
      return;
    }
    if (S.editing !== c.id) v.source = sp.fns.map((f) => (sp.d3 ? f.parts.join(', ') : f.expr)).join('\n');
    const w = sp.w;
    v.range = { xmin: w[0], xmax: w[1], ymin: w[2], ymax: w[3] };
    if (sp.d3) Object.assign(v.range, { zmin: w[4], zmax: w[5] });
    v.graphRO = sp.d3 && sp.fns.some((f) => f.kind !== 0);
    v.anim = sp.anim; // (animated: its functions use t; the settings in its range row)
  }
  function toView(c, v) {
    const kind = N.kindOf(c);
    v = v || N.makeCell(kind, '');
    v.id = c.id;
    v.kind = kind;
    v.outputs = N.outputsOf(c);
    v.execution_count = c.count || null;
    v.lock = c.lock;
    v.spec = c.kind === 'graph' ? c.spec : '';
    if (c.kind === 'graph') { graphView(c, v); return v; }
    v.graphRO = false;
    v.anim = null;
    if (S.editing !== c.id) v.source = c.lock === 'calc' && S.client && S.client.drafts.has(c.id) ? S.client.drafts.get(c.id) : c.src;
    v.range = { xmin: c.xmin, xmax: c.xmax, ymin: null, ymax: null };
    return v;
  }

  function onDoc() {
    const d = doc();
    if (!d || !S.live) return;
    const selId = cur() ? cur().id : null;
    for (const v of S.views.values()) v.el.remove();
    S.views.clear();
    const nb = new N.Notebook();
    nb.name = docName(d) + '.ipynb';
    nb.cells = d.cells.map((c) => toView(c));
    S.nb = nb;
    if (S.editing && !d.cells.some((c) => c.id === S.editing)) S.editing = null;
    const i = nb.cells.findIndex((c) => c.id === selId);
    S.sel = i >= 0 ? i : Math.min(S.sel, Math.max(0, nb.cells.length - 1));
    if (!S.editing && S.mode === 'edit') S.mode = 'command';
    renderList();
    onRun(d.running);
    if (cur() && document.activeElement === document.body) select(cur(), S.mode, false);
    updateToolbar();
    snapshotSoon();
    if ($('#notebooks').open) fillList();
  }

  function viewCellOf(id) { return S.nb.cells.find((c) => c.id === id); }

  function onCell(c) {
    const v = viewCellOf(c.id);
    if (!v) return;
    toView(c, v);
    view(v).update();
    snapshotSoon();
  }

  function onOrder(cells) {
    const selId = cur() ? cur().id : null;
    const have = new Map(S.nb.cells.map((c) => [c.id, c]));
    S.nb.cells = cells.map((c) => toView(c, have.get(c.id)));
    let i = S.nb.cells.findIndex((c) => c.id === (S.pendingSelect ? S.pendingSelect.id : selId));
    if (i < 0 && selId === null) i = 0;
    if (i >= 0) S.sel = i;
    renderList();
    if (S.pendingSelect && i >= 0 && S.nb.cells[i].id === S.pendingSelect.id) {
      const p = S.pendingSelect;
      S.pendingSelect = null;
      select(S.nb.cells[i], p.mode);
    }
    snapshotSoon();
  }

  function onRun(id) {
    for (const c of S.nb.cells) {
      const v = view(c), on = !!id && c.id === id;
      if (v.running !== on) { v.running = on; v.renderPrompt(); v.el.classList.toggle('running', on); }
    }
    refreshStatus();
    updateToolbar();
  }

  function onMeta() { updateTitle(); markDirty(); }

  function onLock(id, who) {
    const v = viewCellOf(id);
    if (!v) return;
    v.lock = who;
    if (who === 'calc' && S.editing === id) { S.editing = null; select(v, 'command'); }
    if (!who) { const c = S.client && S.client.cell(id); if (c && S.editing !== id) toView(c, v); } // (a graph cell: its functions)
    view(v).update();
  }

  function onDraft(id, text) {
    const v = viewCellOf(id);
    if (!v || S.editing === id) return;
    v.source = text;
    view(v).update();
  }

  /* the last state, for a page opened again without the calculator */
  let snapTimer = null;
  function snapshotSoon() {
    clearTimeout(snapTimer);
    snapTimer = setTimeout(() => {
      try { store.set('last', JSON.stringify({ name: S.nb.name, nb: N.toIpynb(S.nb, {}) })); } catch (e) { /* too big */ }
    }, 2000);
  }

  /* ------------------------------------------------------------ the page -> the calculator */
  /* Changes go to the calculator one after the other (a long source is several commands). */
  function op(f) {
    const p = S.ops.then(() => (S.client ? f(S.client) : null));
    S.ops = p.catch((e) => { toast(e.message); });
    return p;
  }

  function connectedOrSay() {
    if (S.live && S.client) return true;
    toast(HAS_SERIAL ? 'Connect the calculator first (Connect, top right).' : 'Open this file in Chrome or Edge to connect to the calculator.');
    return false;
  }

  /* editing a cell here: it is locked on the calculator meanwhile */
  function beginEdit(cell) {
    if (cell.lock === 'calc') { toast('This cell is being edited on the calculator'); return false; }
    if (S.editing === cell.id) return true;
    S.editing = cell.id;
    const id = cell.id;
    op((c) => c.lock(id)).catch(() => {
      if (S.editing === id) { S.editing = null; select(cell, 'command'); }
    });
    return true;
  }

  function pushSoon(cell) {
    clearTimeout(S.pushTimer);
    S.pushTimer = setTimeout(() => pushNow(cell.id), 300);
  }
  function pushNow(id) {
    clearTimeout(S.pushTimer);
    S.pushTimer = null;
    const cell = viewCellOf(id);
    if (!cell) return Promise.resolve();
    return op((c) => {
      const cc = c.cell(id);
      if (cc && cc.kind === 'graph' && isGraph(cell.kind)) { // a graph cell: its spec (functions and window)
        const spec = specOf(cell, cc);
        if (spec === cc.spec) return null;
        cc.spec = spec;
        return c.setSpec(id, spec);
      }
      if (cc && cc.src === cell.source) return null;
      if (cc) cc.src = cell.source;
      return c.setSource(id, cell.source);
    });
  }
  /* the spec of a graph cell as the page has it (its text: the functions, its range inputs) */
  function specOf(cell, cc, w) {
    const r = cell.range, d3 = cell.kind === 'graph3d';
    w = w || (d3 ? [r.xmin, r.xmax, r.ymin, r.ymax, r.zmin ?? -5, r.zmax ?? 5] : [r.xmin, r.xmax, r.ymin ?? -10, r.ymax ?? 10]);
    return N.buildSpec(cc ? cc.spec : null, String(cell.source).split('\n'), w, d3);
  }
  /* the window of a graph cell changed here (its range, a pan or zoom of its plot): to the calculator */
  function pushWindow(id, w) {
    const cell = viewCellOf(id);
    if (!cell || !S.live) return Promise.resolve();
    return op((c) => {
      const cc = c.cell(id);
      if (!cc || cc.kind !== 'graph' || cc.lock === 'calc') return null;
      const sp = N.parseSpec(cc.spec);
      const spec = N.buildSpec(cc.spec, sp ? sp.fns.map((f) => (sp.d3 ? f.parts.join(', ') : f.expr)) : String(cell.source).split('\n'),
        sp && sp.d3 ? w.concat(sp.w.slice(w.length)) : w, !!(sp && sp.d3));
      if (spec === cc.spec) return null;
      cc.spec = spec;
      return c.setSpec(id, spec);
    });
  }

  /* an animated cell's settings (t from, to, seconds per sweep, mode) changed here: to the calculator */
  function pushAnim(id, a) {
    if (!S.live) return Promise.resolve();
    return op((c) => {
      const cc = c.cell(id), sp = cc && N.parseSpec(cc.spec);
      if (!sp || cc.kind !== 'graph' || cc.lock === 'calc') return null;
      const spec = N.buildSpec(cc.spec, sp.fns.map((f) => (sp.d3 ? f.parts.join(', ') : f.expr)), sp.w, sp.d3, a);
      if (spec === cc.spec) return null;
      cc.spec = spec;
      return c.setSpec(id, spec);
    });
  }

  /* the end of editing a cell: its text sent, the lock given back */
  function endEdit() {
    const id = S.editing;
    if (!id) return Promise.resolve();
    S.editing = null;
    S.recall = null;
    const p = S.pushTimer ? pushNow(id) : Promise.resolve();
    return p.then(() => op((c) => c.unlock(id)));
  }
  setInterval(() => { if (S.editing && S.live) op((c) => c.lock(S.editing)).catch(() => {}); }, 10000);

  /* ------------------------------------------------------------ running */
  function run(cells) {
    if (!connectedOrSay()) return;
    endEdit();
    for (const c of cells) {
      if (c.kind === 'markdown' || c.kind === 'raw') continue;
      const id = c.id;
      view(c).setQueued(true);
      op((cl) => cl.run(id)).finally(() => { const v = viewCellOf(id); if (v) view(v).setQueued(false); });
    }
  }
  function runAll() {
    if (!connectedOrSay()) return;
    endEdit();
    op((c) => c.runAll());
  }

  function interrupt() {
    if (!S.link) return;
    S.link.interrupt();
    toast('Interrupt sent');
  }

  async function restart(runAfter) {
    if (!connectedOrSay()) return;
    if (!confirm('Restart Python on the calculator? Its variables are lost.' + (runAfter ? ' Then every cell runs again.' : ''))) return;
    if (doc() && doc().running) S.link.interrupt();
    await op((c) => c.restart());
    toast('Python restarted');
    if (runAfter) runAll();
  }

  /* ------------------------------------------------------------ cell views */
  function view(cell) {
    let v = S.views.get(cell.id);
    if (!v || v.cell !== cell) { if (v) v.el.remove(); v = new CellView(cell); S.views.set(cell.id, v); }
    return v;
  }

  class CellView {
    constructor(cell) {
      this.cell = cell;
      this.plots = [];
      this.kept = new Map(); // plots kept across updates of the cell
      this.prompt = h('div', { class: 'prompt' });
      this.badge = h('span', { class: 'badge' });
      this.lockTag = h('span', { class: 'locktag' });
      this.ta = h('textarea', { class: 'src', spellcheck: 'false', rows: '1', autocomplete: 'off', autocapitalize: 'off',
        'aria-label': 'cell input' });
      this.ta.value = cell.source;
      this.range = h('div', { class: 'range' });
      this.md = h('div', { class: 'md' });
      this.outs = h('div', { class: 'outputs' });
      this.inbar = h('span', { class: 'refbar' });
      this.el = h('div', { class: 'cell', tabindex: '-1' }, this.prompt,
        h('div', { class: 'body' }, h('div', { class: 'cellhead' }, this.badge, this.lockTag),
          h('div', { class: 'inwrap' }, this.ta, this.inbar), this.range, this.md, this.outs));
      this.el.addEventListener('mousedown', (e) => {
        if (e.target.closest('.plot')) { select(this.cell, 'command', true); return; }
        const inEditor = e.target === this.ta;
        select(this.cell, inEditor ? 'edit' : 'command', !inEditor);
      });
      this.md.addEventListener('dblclick', () => { if (!readOnly()) select(this.cell, 'edit'); });
      this.ta.addEventListener('input', () => {
        if (this.ta.readOnly) return;
        if (this.cell.kind === 'python') {   /* ^ ² √ π ... become Python, as on the calculator */
          const v = this.ta.value, t = N.pyTranslate(v);
          if (t !== v) {
            const at = N.pyTranslate(v.slice(0, this.ta.selectionStart)).length;
            this.ta.value = t;
            this.ta.selectionStart = this.ta.selectionEnd = at;
          }
        }
        this.cell.source = this.ta.value;
        if (S.recall && !S.recall.setting && S.recall.id === this.cell.id) S.recall = null; // (typed: a new start)
        this.grow();
        if (S.editing === this.cell.id) pushSoon(this.cell);
      });
      this.ta.addEventListener('focus', () => { if (S.mode !== 'edit' || cur() !== this.cell) select(this.cell, 'edit', false); });
      this.ta.addEventListener('keydown', (e) => editKey(e, this));
      this.update();
    }
    grow() {
      this.ta.style.height = 'auto';
      this.ta.style.height = this.ta.scrollHeight + 2 + 'px';
    }
    update() {
      const c = this.cell;
      const calc = c.lock === 'calc';
      this.el.className = 'cell kind-' + c.kind + (this.queued ? ' queued' : '') + (this.running ? ' running' : '') +
        (calc ? ' locked' : '');
      this.badge.textContent = KIND_LABEL[c.kind];
      this.lockTag.textContent = calc ? 'editing on the calculator' : '';
      this.ta.readOnly = readOnly() || calc || !!c.graphRO; // (a parametric 3D graph: changed on the calculator)
      if (this.ta.value !== c.source) this.ta.value = c.source;
      this.ta.placeholder = S.live || !readOnly() ? { python: 'Python, e.g. solve(x**2-2, x)', math: 'CAS input, e.g. diff(sin(x)*x, x)',
        graph: 'functions of x, one a line, e.g. sin(x)/x', graph3d: 'z = f(x,y), e.g. sin(x)*cos(y)',
        markdown: 'Text: Markdown with $math$', raw: '' }[c.kind] : '';
      const isMd = c.kind === 'markdown';
      const showMd = isMd && S.editing !== c.id && !calc; // (rendered unless it is being edited)
      this.ta.hidden = showMd;
      this.md.hidden = !showMd;
      if (showMd) this.md.innerHTML = c.source.trim() ? N.renderMarkdown(c.source, { renderMath: tex, attachments: c.attachments })
        : '<p class="placeholder">Empty text cell' + (readOnly() ? '' : ': double-click to write') + '.</p>';
      this.renderRange();
      this.renderPrompt();
      this.renderOutputs();
      requestAnimationFrame(() => this.grow());
    }
    renderPrompt() {
      const c = this.cell;
      const code = c.kind !== 'markdown' && c.kind !== 'raw';
      this.prompt.textContent = !code ? '' : this.running || this.queued ? 'In [*]:' : 'In [' + (c.execution_count ?? ' ') + ']:';
    }
    setQueued(q) { if (this.queued !== q) { this.queued = q; this.update(); } }
    /* a graph cell of a notebook file (several functions and a window); the calculator's own
     * graph cells keep "f(x), xmin, xmax" in their text */
    renderRange() {
      const c = this.cell;
      this.range.hidden = !isGraph(c.kind);
      if (this.range.hidden) return;
      const r = c.range;
      const fmt = (x) => (Number.isFinite(x) ? String(+x.toPrecision(6)) : '');
      if (!S.live) {
        this.range.textContent = 'x from ' + r.xmin + ' to ' + r.xmax + (r.ymin !== null && r.ymax !== null ? ', y from ' + r.ymin + ' to ' + r.ymax : '');
        return;
      }
      // the window, editable: x, y (and z) from .. to ..; a change goes to the calculator
      const axes = c.kind === 'graph3d' ? ['x', 'y', 'z'] : ['x', 'y'];
      const key = axes.map((a) => fmt(r[a + 'min']) + ',' + fmt(r[a + 'max'])).join(';') + (readOnly() || c.lock === 'calc' || c.graphRO) +
        JSON.stringify(c.anim || null);
      if (this.rangeKey === key && this.range.children.length) return;
      this.rangeKey = key;
      this.range.textContent = '';
      for (const a of axes) {
        const inp = (which) => {
          const e = h('input', { class: 'num', type: 'text', inputmode: 'decimal', 'aria-label': a + which, value: fmt(r[a + which]) });
          e.value = fmt(r[a + which]);
          e.readOnly = readOnly() || c.lock === 'calc' || !!c.graphRO;
          e.addEventListener('change', () => this.rangeChanged(a, which, e));
          e.addEventListener('keydown', (ev) => { if (ev.key === 'Enter') { ev.preventDefault(); this.rangeChanged(a, which, e); } });
          return e;
        };
        this.range.append(h('span', {}, a + ' from'), inp('min'), h('span', {}, 'to'), inp('max'), h('span', { class: 'gap' }));
      }
      if (c.anim) this.renderAnim();
    }
    /* an animated cell: t from .. to .., seconds per sweep, loop / bounce / once (both sides) */
    renderAnim() {
      const c = this.cell, a = c.anim, ro = readOnly() || c.lock === 'calc' || !!c.graphRO;
      const fmt = (x) => String(+(+x).toPrecision(6));
      const num = (k, label) => {
        const e = h('input', { class: 'num', type: 'text', inputmode: 'decimal', 'aria-label': label, value: fmt(a[k]) });
        e.value = fmt(a[k]);
        e.readOnly = ro;
        const set = () => {
          const v = parseFloat(N.toCas(e.value)), n = Object.assign({}, c.anim, { [k]: v });
          if (!Number.isFinite(v) || !(n.tmin < n.tmax) || !(n.secs >= 0.1 && n.secs <= 3600)) {
            toast(k === 'secs' ? 'seconds: 0.1 to 3600' : 't: the lower end must be below the upper one');
            e.value = fmt(a[k]);
            return;
          }
          if (n[k] === c.anim[k]) return;
          c.anim = n;
          pushAnim(c.id, n);
        };
        e.addEventListener('change', set);
        e.addEventListener('keydown', (ev) => { if (ev.key === 'Enter') { ev.preventDefault(); set(); } });
        return e;
      };
      const mode = h('select', { 'aria-label': 'animation mode' }, ...N.ANIM_MODES.map((m, i) => h('option', { value: String(i) }, m)));
      mode.value = String(a.mode | 0);
      mode.disabled = ro;
      mode.addEventListener('change', () => { const n = Object.assign({}, c.anim, { mode: +mode.value }); c.anim = n; pushAnim(c.id, n); });
      this.range.append(h('span', {}, 't from'), num('tmin', 'tmin'), h('span', {}, 'to'), num('tmax', 'tmax'), h('span', {}, 'in'),
        num('secs', 'seconds'), h('span', {}, 's'), mode);
    }
    rangeChanged(a, which, e) {
      const c = this.cell, r = c.range, v = parseFloat(N.toCas(e.value));
      const lo = which === 'min' ? v : r[a + 'min'], hi = which === 'max' ? v : r[a + 'max'];
      if (!Number.isFinite(v) || !(lo < hi)) { toast(a + ': the lower end must be below the upper one'); e.value = String(r[a + which]); return; }
      if (r[a + which] === v) return;
      if (c.lock === 'calc') { toast('This cell is being edited on the calculator'); return; }
      r[a + which] = v;
      const w = c.kind === 'graph3d' ? [r.xmin, r.xmax, r.ymin, r.ymax, r.zmin, r.zmax] : [r.xmin, r.xmax, r.ymin, r.ymax];
      if (S.editing === c.id) pushSoon(c);
      else pushWindow(c.id, w);
    }
    renderOutputs() {
      this.renderPrompt();
      const keep = this.kept || new Map();
      this.kept = new Map();
      this.outs.textContent = '';
      for (const o of this.cell.outputs) this.outs.append(renderOutput(o, this, keep));
      for (const [k, x] of keep) if (!this.kept.has(k)) x.dispose();
      this.plots = [...this.kept.values()].map((x) => x.p);
      this.inbar.textContent = '';
      const code = this.cell.kind === 'math' || this.cell.kind === 'python';
      if (code && this.cell.source.trim()) this.inbar.append(...refButtons(this.cell, 'input'));
    }
    focus(where) {
      if (this.ta.hidden) this.update();
      this.ta.focus({ preventScroll: true });
      if (where === 'start') this.ta.setSelectionRange(0, 0);
      else if (where === 'end') this.ta.setSelectionRange(this.ta.value.length, this.ta.value.length);
      scrollIntoView(this.el);
    }
  }

  function renderOutput(o, v, keep) {
    const t = o.output_type;
    if (t === 'stream') return h('pre', { class: 'out stream' + (o.name === 'stderr' ? ' stderr' : '') }, stripAnsi(N.joinText(o.text)));
    if (t === 'error') return h('pre', { class: 'out error' }, stripAnsi((o.traceback && o.traceback.length ? o.traceback : [o.ename + ': ' + o.evalue]).join('\n')));
    if (t !== 'execute_result' && t !== 'display_data') return h('pre', { class: 'out' }, '[' + t + ']');
    const d = o.data || {}, md = o.metadata || {};
    const wrap = h('div', { class: 'out rich' });
    if (t === 'execute_result') wrap.append(h('div', { class: 'outprompt' }, 'Out[' + (o.execution_count ?? ' ') + ']:'));
    if (t === 'execute_result' && v && resultOf(v.cell)) wrap.append(h('span', { class: 'refbar' }, ...refButtons(v.cell, 'result')));
    const cal = md.calculator || {};
    if (cal.input) wrap.append(h('div', { class: 'echo' }, cal.input));
    let body;
    if (cal.empty) body = h('div', { class: 'graphempty' }, cal.d3 ? '3D graph: type z = f(x,y) in the cell (e.g. sin(x)*cos(y))'
      : 'Graph: type a function of x in the cell (one a line); the plot appears here and on the calculator');
    else if (cal.picture) body = pictureView(cal);
    else if (cal.surface && S.live) body = cal.anim ? animSurfaceView(cal, v, keep) : surfaceView(cal, v, keep);
    else if (cal.surface) body = pictureView(cal);
    else if (cal.fns && cal.cell && S.live && v) body = cal.anim ? animPlotView(cal, v, keep) : cellPlotView(cal, v, keep);
    else if (cal.series || cal.fns || (cal.cell && cal.plot)) body = plotView(o, v);
    else if (d['text/latex']) { body = h('div', { class: 'latex' }); body.innerHTML = tex(stripDollars(N.joinText(d['text/latex'])), true); body.title = N.joinText(d['text/plain'] || ''); }
    else if (d['image/png'] || d['image/jpeg'] || d['image/gif']) {
      const m = d['image/png'] ? 'image/png' : d['image/jpeg'] ? 'image/jpeg' : 'image/gif';
      body = h('img', { src: 'data:' + m + ';base64,' + N.joinText(d[m]).replace(/\s/g, ''), alt: N.joinText(d['text/plain'] || 'image') });
    } else if (d['image/svg+xml']) body = h('img', { src: 'data:image/svg+xml;base64,' + btoa(unescape(encodeURIComponent(N.joinText(d['image/svg+xml'])))), alt: 'image' });
    else if (d['text/markdown']) { body = h('div', { class: 'md' }); body.innerHTML = N.renderMarkdown(N.joinText(d['text/markdown']), { renderMath: tex }); }
    else if (d['text/plain'] !== undefined) body = h('pre', {}, stripAnsi(N.joinText(d['text/plain'])));
    else body = h('pre', { class: 'muted' }, '(' + Object.keys(d).join(', ') + ': not shown here)');
    wrap.append(body);
    if (cal.anim && !(S.live && (cal.surface || cal.fns))) // (a still picture of an animated cell)
      wrap.append(h('div', { class: 'animnote' }, 'animated (t from ' + +(+cal.anim.tmin).toPrecision(4) + ' to ' +
        +(+cal.anim.tmax).toPrecision(4) + ')' + (S.live ? ': the calculator plays it' : '')));
    return wrap;
  }

  /* ------------------------------------------------------------ earlier inputs and results
   * Out[n] is the result of the cell with execution count n (the [n] of In [n] / Out[n]), in Math
   * and Python cells alike; Ans the latest result (docs/NOTEBOOK.md). Hovering a cell's input or
   * result shows two buttons: insert its text, or insert the reference Out[n], where a cell is
   * being edited (at the cursor), else into a new cell at the end. */
  function resultOf(cell) {
    if (!cell || !Number.isInteger(cell.execution_count) || cell.outputs.some((o) => o.output_type === 'error')) return null;
    const o = cell.outputs.find((x) => x.output_type === 'execute_result' && x.data && x.data['text/plain'] !== undefined);
    return o ? N.joinText(o.data['text/plain']) : null;
  }
  function refButtons(cell, what) {
    const btn = (label, cls, title, f) => {
      const b = h('button', { class: 'refbtn ' + cls, type: 'button', title }, label);
      b.addEventListener('mousedown', (e) => { e.preventDefault(); e.stopPropagation(); }); // (the cell being edited keeps the cursor)
      b.addEventListener('click', (e) => { e.stopPropagation(); f(); });
      return b;
    };
    const out = [btn('insert text', 'ref-text', what === 'input' ? 'Insert this input where you type (or in a new cell)'
      : 'Insert this result where you type (or in a new cell)', () => insertText(what === 'input' ? cell.source : resultOf(cell) || '', cell.kind))];
    if (resultOf(cell)) {
      const ref = 'Out[' + cell.execution_count + ']';
      out.push(btn('insert ' + ref, 'ref-out', 'Insert ' + ref + ', the result of this cell, where you type (or in a new cell)',
        () => insertText(ref, cell.kind)));
    }
    return out;
  }
  /* text at the cursor of the cell being edited, else a new cell of `kind` at the end with it */
  function insertText(text, kind) {
    if (!connectedOrSay() || !text) return;
    const c = S.mode === 'edit' && S.editing ? viewCellOf(S.editing) : null;
    if (c) {
      const ta = view(c).ta;
      if (ta.readOnly) return;
      ta.setRangeText(text, ta.selectionStart, ta.selectionEnd, 'end');
      ta.dispatchEvent(new Event('input'));
      ta.focus();
      return;
    }
    insertAt(S.nb.cells.length, kind === 'python' ? 'python' : 'math', 'edit', (cl, id) => cl.setSource(id, text));
  }
  /* Alt+Up / Alt+Down while editing: the inputs of the cells above of the same kind, nearest first,
   * in place of the text (Alt+Down past the nearest one: the text as it was) */
  function recallInput(v, dir) {
    const c = v.cell;
    let r = S.recall;
    if (!r || r.id !== c.id) {
      const seen = new Set();
      const list = S.nb.cells.slice(0, S.nb.index(c)).filter((x) => x.kind === c.kind && x.source.trim()).map((x) => x.source)
        .reverse().filter((t) => !seen.has(t) && seen.add(t));
      r = S.recall = { id: c.id, orig: v.ta.value, k: -1, list, setting: false };
    }
    const k = r.k + dir;
    if (k < -1) return;
    if (k >= r.list.length) { toast(r.list.length ? 'No earlier input' : 'No earlier input of this kind'); return; }
    r.k = k;
    v.ta.value = k < 0 ? r.orig : r.list[k];
    v.ta.setSelectionRange(v.ta.value.length, v.ta.value.length);
    r.setting = true;
    v.ta.dispatchEvent(new Event('input'));
    r.setting = false;
  }

  /* The calculator samples a cell's function (its CAS; k: function k of a graph cell); kept by
   * function and window. */
  async function samples(cellId, plot, a, b, k) {
    const d = doc(), key = (d ? d.counter : 0) + '|' + plot + '|' + a + '|' + b; // (a calculation may change a variable)
    if (S.samples.has(key)) return S.samples.get(key);
    const s = await S.client.sample(cellId, 400, a, b, k);
    const ys = s.ys.map((y) => (Number.isFinite(y) ? +y.toPrecision(10) : null));
    if (S.samples.size > 40) S.samples.clear();
    S.samples.set(key, ys);
    return ys;
  }

  /* An interactive plot of a graph output; panning/zooming samples again on the calculator
   * when it is connected. A calculator cell's plot (cal.cell) is sampled when shown. */
  /* A 3D graph cell: the calculator's own picture of it (nb png), kept by its spec */
  function pictureView(cal) {
    const img = h('img', { class: 'picture', alt: '3D graph' });
    const show = (b64) => { img.src = 'data:image/png;base64,' + b64; };
    const key = cal.cell + '|' + cal.spec;
    if (S.pictures.has(key)) show(S.pictures.get(key));
    else if (S.client && S.live) {
      S.client.png(cal.cell).then((b64) => {
        if (S.pictures.size > 20) S.pictures.clear();
        S.pictures.set(key, b64);
        show(b64);
      }).catch((e) => log('graph: ' + e.message));
    }
    return img;
  }

  let plotSeq = 0;
  function plotView(o, v) {
    const cal = o.metadata.calculator;
    // a graph cell of the home screen: each of its functions (k), sampled on the calculator
    const fns = cal.fns || [{ k: 0, expr: cal.plot }];
    if (!cal.series) cal.series = fns.map((f) => ({ expr: f.expr, name: f.k ? 'y' + f.k : '', ys: [] }));
    const canvas = h('canvas', { class: 'plot', tabindex: '0', 'aria-label': 'graph: ' + cal.series.map((s) => s.expr).join(', ') });
    const box = h('div', { class: 'plotbox' }, canvas);
    const p = new N.Plot(canvas, { xmin: cal.xmin, xmax: cal.xmax, series: cal.series },
      { xmin: cal.xmin, xmax: cal.xmax, ymin: cal.ymin ?? null, ymax: cal.ymax ?? null }, { style: plotStyle() });
    const sampleAll = async (xmin, xmax) => {
      const out = [];
      for (const f of fns) out.push({ expr: f.expr, name: f.k ? 'y' + f.k : '', ys: await samples(cal.cell, f.k + ':' + f.expr, xmin, xmax, f.k) });
      return out;
    };
    const fetch = async (xmin, xmax) => {
      if (!cal.cell || !S.client || !S.live) return false;
      cal.series = await sampleAll(xmin, xmax);
      p.setData({ xmin, xmax, series: cal.series });
      return true;
    };
    if (cal.cell && cal.series.length && !cal.series[0].ys.length) {
      p.busy = true;
      fetch(cal.xmin, cal.xmax).catch((e) => log('graph: ' + e.message)).finally(() => { p.busy = false; p.draw(); });
    }
    let timer = null, seq = 0;
    p.onView = (vw) => {
      clearTimeout(timer);
      if (!S.client || !S.live || !cal.cell) return;
      timer = setTimeout(async () => {
        const my = ++seq;
        p.busy = true;
        p.draw();
        try {
          const series = await sampleAll(vw.xmin, vw.xmax);
          if (my === seq) p.setData({ xmin: vw.xmin, xmax: vw.xmax, series });
        } catch (e) { log('graph: ' + e.message); }
        p.busy = false;
        p.draw();
      }, 300);
    };
    const ro = new ResizeObserver(() => p.draw());
    ro.observe(canvas);
    p.dispose = () => ro.disconnect();
    v.kept.set('plot-' + (++plotSeq), { p, dispose: () => p.dispose() });
    const bar = h('div', { class: 'plotbar' },
      h('button', { type: 'button', onclick: () => p.reset(), title: 'Back to the window of the cell (double-click)' }, 'Reset view'),
      h('button', { type: 'button', onclick: () => p.zoom(0.8, 0.8), title: 'Zoom in (+)' }, '+'),
      h('button', { type: 'button', onclick: () => p.zoom(1.25, 1.25), title: 'Zoom out (-)' }, '−'),
      h('span', { class: 'muted' }, 'drag to pan; click, then wheel to zoom; pinch'));
    requestAnimationFrame(() => p.draw());
    return h('div', { class: 'plotwrap' }, box, bar);
  }
  /* A graph cell of the calculator's notebook: its functions (in their colours and styles) in its
   * window, each sampled on the calculator. The plot stays across the cell's updates (same
   * functions): a window changed on the calculator moves the view; a pan or zoom here goes to the
   * calculator's cell when the gesture is over (debounced), not per pointer move. */
  function cellPlotView(cal, v, keep) {
    const id = cal.cell;
    const sig = cal.fns.map((f) => f.k + ':' + f.color + ':' + f.style + ':' + f.expr).join('|');
    const win = { xmin: cal.xmin, xmax: cal.xmax, ymin: cal.ymin, ymax: cal.ymax };
    const old = keep && keep.get('plot');
    if (old && old.sig === sig) {
      keep.delete('plot');
      v.kept.set('plot', old);
      old.update(win);
      return old.el;
    }
    const series = () => cal.fns.map((f) => ({ expr: f.expr, name: 'y' + f.k, color: f.color, style: f.style, ys: [] }));
    const canvas = h('canvas', { class: 'plot', tabindex: '0', 'aria-label': 'graph: ' + cal.fns.map((f) => f.expr).join(', ') });
    const box = h('div', { class: 'plotbox' }, canvas);
    const p = new N.Plot(canvas, { xmin: win.xmin, xmax: win.xmax, series: series() }, win, { style: plotStyle() });
    let seq = 0, timer = null, pushTimer = null, sent = null;
    const sample = async (vw) => {
      const my = ++seq;
      p.busy = true;
      p.draw();
      try {
        const out = [];
        for (const f of cal.fns) out.push({ expr: f.expr, name: 'y' + f.k, color: f.color, style: f.style,
          ys: await samples(id, f.k + ':' + f.expr, vw.xmin, vw.xmax, f.k) });
        if (my === seq) p.setData({ xmin: vw.xmin, xmax: vw.xmax, series: out });
      } catch (e) { log('graph: ' + e.message); }
      if (my === seq) p.busy = false;
      p.draw();
    };
    const same = (a, b) => a && b && ['xmin', 'xmax', 'ymin', 'ymax'].every((k) => Math.abs(a[k] - b[k]) <= 1e-9 * Math.max(1, Math.abs(a[k])));
    p.onView = (vw) => {
      clearTimeout(timer);
      timer = setTimeout(() => sample(vw), 250);
      clearTimeout(pushTimer);
      const push = () => {
        if (canvas.classList.contains('dragging')) { pushTimer = setTimeout(push, 300); return; } // (the gesture goes on)
        if (same(vw, sent)) return;
        sent = Object.assign({}, vw);
        pushWindow(id, [vw.xmin, vw.xmax, vw.ymin, vw.ymax]);
      };
      pushTimer = setTimeout(push, 600);
    };
    const update = (w) => { // the cell's window now (an event): the view follows unless it is the one sent from here
      if (same(w, p.view)) return;
      if (same(w, sent)) return;
      sent = null;
      p.view = Object.assign({}, w);
      p.home = Object.assign({}, w);
      sample(w);
    };
    sample(win);
    const ro = new ResizeObserver(() => p.draw());
    ro.observe(canvas);
    const bar = h('div', { class: 'plotbar' },
      h('button', { type: 'button', onclick: () => p.reset(), title: 'Back to the window the cell had (double-click)' }, 'Reset view'),
      h('button', { type: 'button', onclick: () => p.zoom(0.8, 0.8), title: 'Zoom in (+)' }, '+'),
      h('button', { type: 'button', onclick: () => p.zoom(1.25, 1.25), title: 'Zoom out (-)' }, '−'),
      h('span', { class: 'muted' }, 'drag to pan; click, then wheel to zoom; pinch. The calculator\'s cell follows.'));
    const el = h('div', { class: 'plotwrap' }, box, bar);
    const kept = { sig, el, p, update, dispose: () => { ro.disconnect(); clearTimeout(timer); clearTimeout(pushTimer); } };
    v.kept.set('plot', kept);
    requestAnimationFrame(() => p.draw());
    return el;
  }

  /* A 3D graph cell, z = f(x,y): each surface sampled on the calculator (nb sample3d, one round
   * trip each, 40 x 40) and drawn here (plot3d.js), turned and zoomed in the page only. */
  const GRID3D = 40;
  async function samples3d(id, f, w) {
    const d = doc(), key = '3d|' + (d ? d.counter : 0) + '|' + f.k + ':' + f.expr + '|' + w.join(',');
    if (S.samples.has(key)) return S.samples.get(key);
    const s = await S.client.sample3d(id, GRID3D, GRID3D, f.k);
    if (S.samples.size > 40) S.samples.clear();
    S.samples.set(key, s);
    return s;
  }
  function surfaceView(cal, v, keep) {
    const sig = cal.spec;
    const old = keep && keep.get('surface');
    if (old && old.sig === sig) { keep.delete('surface'); v.kept.set('surface', old); return old.el; }
    const canvas = h('canvas', { class: 'plot plot3d', tabindex: '0', 'aria-label': '3D graph: ' + cal.fns.map((f) => 'z=' + f.expr).join(', ') });
    const sf = new N.Surface(canvas, { w: cal.w, view: cal.view, surfaces: [] }, { style: plotStyle() });
    sf.busy = true;
    (async () => {
      const out = [];
      for (const f of cal.fns) {
        const s = await samples3d(cal.cell, f, cal.w);
        out.push(Object.assign({ color: N.PLOT_COLORS[f.color % N.PLOT_COLORS.length] }, s));
      }
      sf.setData({ w: cal.w, view: cal.view, surfaces: out });
    })().catch((e) => log('3D graph: ' + e.message)).finally(() => { sf.busy = false; sf.draw(); });
    const ro = new ResizeObserver(() => sf.draw());
    ro.observe(canvas);
    const bar = h('div', { class: 'plotbar' },
      h('button', { type: 'button', onclick: () => sf.reset(), title: 'Back to the cell\'s view (double-click)' }, 'Reset view'),
      h('button', { type: 'button', onclick: () => sf.zoom(1.25), title: 'Zoom in (+)' }, '+'),
      h('button', { type: 'button', onclick: () => sf.zoom(0.8), title: 'Zoom out (-)' }, '−'),
      h('span', { class: 'muted' }, 'drag to turn; click, then wheel to zoom; pinch'));
    const el = h('div', { class: 'plotwrap' }, h('div', { class: 'plotbox' }, canvas), bar);
    v.kept.set('surface', { sig, el, p: sf, dispose: () => ro.disconnect() });
    requestAnimationFrame(() => sf.draw());
    return el;
  }

  /* ------------------------------------------------------------ animated graph cells
   * A cell whose functions use t: its frames come from the calculator in one round trip (nb frames:
   * every function that is on, at ANIM_N points, for ANIM_NF values of t over its range; 3D: nb
   * frames3d per surface, ANIM_NF3 frames of ANIM_G3 x ANIM_G3) and play here on their own: they
   * start at once (autoplay), the play / pause button, the t slider (scrub, between two frames the
   * values are interpolated) and the speed are this page's only (the calculator's cell does not
   * play, and its playing is not shown here). The window and the settings are the cell's: a pan or
   * zoom samples the frames again and goes to the calculator, like a still cell. */
  const ANIM_NF = 48, ANIM_N = 200, ANIM_NF3 = 24, ANIM_G3 = 32;
  /* what an animated cell's frames depend on: its spec but the seconds and the mode, t's range */
  const framesKey = (spec, a) => String(spec).split('\n').filter((l) => !l.startsWith('a ')).join('\n') + '|t' + a.tmin + ',' + a.tmax;
  async function framesOf(id, key, get) {
    const d = doc(), k = 'anim|' + (d ? d.counter : 0) + '|' + id + '|' + key;
    if (S.samples.has(k)) return S.samples.get(k);
    const r = await get();
    if (S.samples.size > 40) S.samples.clear();
    S.samples.set(k, r);
    return r;
  }
  /* the values of frame position f (0..nf-1, fractional: between two frames) of frames[][...] */
  function lerpFrame(frames, f, pick) {
    const n = frames.length, a = Math.max(0, Math.min(n - 1, Math.floor(f))), b = Math.min(n - 1, a + 1), u = f - a;
    const A = pick(frames[a]), B = pick(frames[b]);
    if (u <= 1e-9 || a === b) return A.slice();
    return A.map((y, i) => (Number.isFinite(y) && Number.isFinite(B[i]) ? y + (B[i] - y) * u : u < 0.5 ? y : B[i]));
  }

  /* The play state of one animated cell: phase 0..1 along t's range, advanced by the time passed
   * x speed / seconds per sweep: loop (around), bounce (back and forth), once (stops at the end). */
  class Player {
    constructor(anim, show) {
      this.anim = anim; this.show = show; this.phase = 0; this.dir = 1; this.speed = 1;
      this.playing = false; this.timer = null; this.last = 0; this.onState = null;
    }
    t() { return this.anim.tmin + this.phase * (this.anim.tmax - this.anim.tmin); }
    play() {
      if (this.playing) return;
      if (this.anim.mode === 2 && this.phase >= 1) this.phase = 0;
      this.playing = true;
      this.last = Date.now();
      this.timer = setTimeout(() => this.tick(), 40);
      if (this.onState) this.onState();
    }
    pause() {
      this.playing = false;
      clearTimeout(this.timer);
      this.timer = null;
      if (this.onState) this.onState();
    }
    toggle() { if (this.playing) this.pause(); else this.play(); }
    seek(p) { this.phase = Math.max(0, Math.min(1, p)); this.show(); if (this.onState) this.onState(); }
    advance(dt) {
      const d = dt * this.speed / (this.anim.secs > 0 ? this.anim.secs : 4);
      if (this.anim.mode === 1) {
        this.phase += this.dir * d;
        while (this.phase > 1 || this.phase < 0) {
          if (this.phase > 1) { this.phase = 2 - this.phase; this.dir = -1; } else { this.phase = -this.phase; this.dir = 1; }
        }
        return true;
      }
      if (this.anim.mode === 2) { this.phase = Math.min(1, this.phase + d); return this.phase < 1; }
      this.phase = (this.phase + d) % 1;
      return true;
    }
    tick() {
      this.timer = null;
      if (!this.playing) return;
      const now = Date.now(), more = this.advance(Math.min(0.25, (now - this.last) / 1000));
      this.last = now;
      this.show();
      if (this.onState) this.onState();
      if (more) this.timer = setTimeout(() => this.tick(), 40);
      else this.pause();
    }
    dispose() { this.pause(); this.onState = null; }
  }

  /* the bar under an animated plot: play / pause, the t slider, t's value, the speed */
  function animBar(pl) {
    const btn = h('button', { type: 'button', class: 'play', title: 'Play or pause (here only; the calculator plays its cell itself)' });
    const scrub = h('input', { type: 'range', class: 'scrub', min: '0', max: '1000', step: '1', 'aria-label': 't' });
    const tval = h('span', { class: 'tval' });
    const speed = h('select', { 'aria-label': 'speed', title: 'Speed' }, ...[0.25, 0.5, 1, 2, 4].map((s) => h('option', { value: String(s) }, s + '×')));
    speed.value = '1';
    btn.addEventListener('click', () => pl.toggle());
    scrub.addEventListener('input', () => { const to = +scrub.value / 1000; pl.pause(); pl.seek(to); });
    speed.addEventListener('change', () => { pl.speed = +speed.value || 1; });
    pl.onState = () => {
      btn.textContent = pl.playing ? '❚❚ pause' : '▶ play';
      scrub.value = String(Math.round(pl.phase * 1000));
      tval.textContent = 't = ' + +pl.t().toPrecision(4);
    };
    pl.onState();
    return h('div', { class: 'animbar' }, btn, scrub, tval, speed);
  }

  /* An animated 2D cell: its functions' frames over the view's x range, played here. */
  function animPlotView(cal, v, keep) {
    const id = cal.cell;
    const sig = 'anim|' + cal.fns.map((f) => f.k + ':' + f.color + ':' + f.style + ':' + f.expr).join('|') + '|' + cal.anim.tmin + ',' + cal.anim.tmax;
    const win = { xmin: cal.xmin, xmax: cal.xmax, ymin: cal.ymin, ymax: cal.ymax };
    const old = keep && keep.get('plot');
    if (old && old.sig === sig) {
      keep.delete('plot');
      v.kept.set('plot', old);
      old.update(win, cal.anim);
      return old.el;
    }
    const canvas = h('canvas', { class: 'plot', tabindex: '0', 'aria-label': 'animated graph: ' + cal.fns.map((f) => f.expr).join(', ') });
    const box = h('div', { class: 'plotbox' }, canvas);
    const empty = () => cal.fns.map((f) => ({ expr: f.expr, name: 'y' + f.k, color: f.color, style: f.style, ys: [] }));
    const p = new N.Plot(canvas, { xmin: win.xmin, xmax: win.xmax, series: empty() }, win, { style: plotStyle() });
    let fr = null, seq = 0, timer = null, pushTimer = null, sent = null, started = false;
    const show = () => {
      if (!fr || !fr.frames.length) return;
      const f = pl.phase * (fr.nf - 1);
      const series = cal.fns.map((fn) => {
        const q = fr.ks.indexOf(fn.k);
        return { expr: fn.expr, name: 'y' + fn.k, color: fn.color, style: fn.style, ys: q < 0 ? [] : lerpFrame(fr.frames, f, (x) => x[q]).map((y) => (Number.isFinite(y) ? y : null)) };
      });
      p.setData({ xmin: fr.xmin, xmax: fr.xmax, series });
      p.draw();
    };
    const pl = new Player(Object.assign({}, cal.anim), show);
    const load = async (vw) => {
      const my = ++seq;
      p.busy = true;
      p.draw();
      try {
        const r = await framesOf(id, framesKey(cal.spec, cal.anim) + '|' + vw.xmin + ',' + vw.xmax, () => S.client.frames(id, ANIM_NF, ANIM_N, vw.xmin, vw.xmax));
        if (my !== seq) return;
        fr = r;
        p.busy = false;
        show();
        if (!started) { started = true; pl.play(); } // (autoplay)
      } catch (e) { log('graph: ' + e.message); if (my === seq) { p.busy = false; p.draw(); } }
    };
    const same = (a, b) => a && b && ['xmin', 'xmax', 'ymin', 'ymax'].every((k) => Math.abs(a[k] - b[k]) <= 1e-9 * Math.max(1, Math.abs(a[k])));
    p.onView = (vw) => {
      clearTimeout(timer);
      timer = setTimeout(() => load(vw), 250);
      clearTimeout(pushTimer);
      const push = () => {
        if (canvas.classList.contains('dragging')) { pushTimer = setTimeout(push, 300); return; }
        if (same(vw, sent)) return;
        sent = Object.assign({}, vw);
        pushWindow(id, [vw.xmin, vw.xmax, vw.ymin, vw.ymax]);
      };
      pushTimer = setTimeout(push, 600);
    };
    const update = (w, anim) => { // the cell now (an event): new settings for the player; the window followed
      Object.assign(pl.anim, anim);
      if (pl.onState) pl.onState();
      if (same(w, p.view) || same(w, sent)) return;
      sent = null;
      p.view = Object.assign({}, w);
      p.home = Object.assign({}, w);
      load(w);
    };
    load(win);
    const ro = new ResizeObserver(() => p.draw());
    ro.observe(canvas);
    const bar = h('div', { class: 'plotbar' },
      h('button', { type: 'button', onclick: () => p.reset(), title: 'Back to the window the cell had (double-click)' }, 'Reset view'),
      h('button', { type: 'button', onclick: () => p.zoom(0.8, 0.8), title: 'Zoom in (+)' }, '+'),
      h('button', { type: 'button', onclick: () => p.zoom(1.25, 1.25), title: 'Zoom out (-)' }, '−'),
      h('span', { class: 'muted' }, 'drag to pan; click, then wheel to zoom; pinch. The calculator\'s cell follows.'));
    const el = h('div', { class: 'plotwrap' }, box, animBar(pl), bar);
    const kept = { sig, el, p, pl, update, dispose: () => { pl.dispose(); ro.disconnect(); clearTimeout(timer); clearTimeout(pushTimer); } };
    v.kept.set('plot', kept);
    requestAnimationFrame(() => p.draw());
    return el;
  }

  /* An animated 3D cell of surfaces z = f(x,y,t): each surface's frames (nb frames3d, one round trip
   * each), played here; turned and zoomed in the page like a still 3D cell. */
  function animSurfaceView(cal, v, keep) {
    const sig = 'anim|' + framesKey(cal.spec, cal.anim);
    const old = keep && keep.get('surface');
    if (old && old.sig === sig) {
      keep.delete('surface');
      v.kept.set('surface', old);
      Object.assign(old.pl.anim, cal.anim); // (seconds, mode: the player goes on)
      if (old.pl.onState) old.pl.onState();
      return old.el;
    }
    const canvas = h('canvas', { class: 'plot plot3d', tabindex: '0', 'aria-label': 'animated 3D graph: ' + cal.fns.map((f) => 'z=' + f.expr).join(', ') });
    const sf = new N.Surface(canvas, { w: cal.w, view: cal.view, surfaces: [] }, { style: plotStyle() });
    let frs = [];
    const show = () => {
      if (!frs.length) return;
      const surfaces = frs.map(({ f, r }) => ({ nx: r.nx, ny: r.ny, zs: lerpFrame(r.frames, pl.phase * (r.nf - 1), (x) => x), color: N.PLOT_COLORS[f.color % N.PLOT_COLORS.length] }));
      sf.setData({ w: cal.w, view: cal.view, surfaces });
      sf.draw();
    };
    const pl = new Player(Object.assign({}, cal.anim), show);
    sf.busy = true;
    (async () => {
      const out = [];
      for (const f of cal.fns)
        out.push({ f, r: await framesOf(cal.cell, framesKey(cal.spec, cal.anim) + '|' + f.k, () => S.client.frames3d(cal.cell, ANIM_NF3, ANIM_G3, ANIM_G3, f.k)) });

      frs = out;
      sf.busy = false;
      show();
      pl.play(); // (autoplay)
    })().catch((e) => { log('3D graph: ' + e.message); sf.busy = false; sf.draw(); });
    const ro = new ResizeObserver(() => sf.draw());
    ro.observe(canvas);
    const bar = h('div', { class: 'plotbar' },
      h('button', { type: 'button', onclick: () => sf.reset(), title: 'Back to the cell\'s view (double-click)' }, 'Reset view'),
      h('button', { type: 'button', onclick: () => sf.zoom(1.25), title: 'Zoom in (+)' }, '+'),
      h('button', { type: 'button', onclick: () => sf.zoom(0.8), title: 'Zoom out (-)' }, '−'),
      h('span', { class: 'muted' }, 'drag to turn; click, then wheel to zoom; pinch'));
    const el = h('div', { class: 'plotwrap' }, h('div', { class: 'plotbox' }, canvas), animBar(pl), bar);
    v.kept.set('surface', { sig, el, p: sf, pl, dispose: () => { pl.dispose(); ro.disconnect(); } });
    requestAnimationFrame(() => sf.draw());
    return el;
  }

  function plotStyle() {

    const cs = getComputedStyle(document.body);
    const v = (n) => cs.getPropertyValue(n).trim();
    return { bg: v('--plot-bg'), grid: v('--plot-grid'), axis: v('--plot-axis'), text: v('--plot-text') };
  }

  /* A graph output as base64 PNG (saving a notebook that is not the calculator's): 640 x 400 */
  function plotPng(o) {
    const cal = o.metadata.calculator;
    const c = document.createElement('canvas');
    const p = new N.Plot(c, { xmin: cal.xmin, xmax: cal.xmax, series: cal.series },
      { xmin: cal.xmin, xmax: cal.xmax, ymin: cal.ymin ?? null, ymax: cal.ymax ?? null },
      { static: true, style: { bg: '#ffffff', grid: '#e6e8ec', axis: '#6b7280', text: '#374151' } });
    p.draw(640, 400, 1);
    return c.toDataURL('image/png').replace(/^data:image\/png;base64,/, '');
  }

  /* ------------------------------------------------------------ the list */
  const cur = () => S.nb.cells[S.sel];

  function renderList() {
    const root = $('#cells');
    const want = S.nb.cells.map((c) => view(c).el);
    const keep = new Set(S.nb.cells.map((c) => c.id));
    for (const [id, v] of S.views) if (!keep.has(id)) { v.el.remove(); S.views.delete(id); }
    want.forEach((el, i) => { if (root.children[i] !== el) root.insertBefore(el, root.children[i] || null); });
    while (root.children.length > want.length) root.lastChild.remove();
    if (S.sel >= S.nb.cells.length) S.sel = S.nb.cells.length - 1;
    if (S.sel < 0) S.sel = 0;
    const d = doc();
    $('#empty').hidden = !(S.live && d && !d.cells.length);
    $('#emptytext').textContent = 'This notebook has no cells yet: calculations on the calculator\'s home screen and cells added here show up here.';
    markSelection();
    updateTitle();
  }

  function markSelection() {
    S.nb.cells.forEach((c, i) => {
      const el = view(c).el;
      el.classList.toggle('selected', i === S.sel);
      el.classList.toggle('editing', i === S.sel && S.mode === 'edit');
    });
    const k = cur() ? cur().kind : 'python';
    $('#kind').value = k;
  }

  function select(cell, mode, focusCell) {
    const i = S.nb.index(cell);
    if (i < 0) return;
    mode = readOnly() ? 'command' : mode || 'command';
    if (S.editing && (mode !== 'edit' || S.editing !== cell.id)) {
      const prev = viewCellOf(S.editing);
      endEdit();
      if (prev) view(prev).update();
    }
    if (mode === 'edit' && !beginEdit(cell)) mode = 'command';
    S.sel = i;
    S.mode = mode;
    markSelection();
    const v = view(cell);
    if (S.mode === 'edit') {
      if (v.ta.hidden || v.ta.readOnly) v.update();
      if (document.activeElement !== v.ta) v.focus();
    } else if (focusCell !== false) {
      if (document.activeElement && document.activeElement.blur && v.el.contains(document.activeElement) && document.activeElement !== v.el)
        document.activeElement.blur();
      v.el.focus({ preventScroll: true });
      scrollIntoView(v.el);
    }
  }

  function scrollIntoView(el) {
    const r = el.getBoundingClientRect(), top = $('#cells').getBoundingClientRect().top;
    if (r.top < top + 8 || r.bottom > window.innerHeight - 8) el.scrollIntoView({ block: 'nearest' });
  }

  /* a new cell at index `at`, chosen (in `mode`) once the calculator has it; then `then(id)` */
  function insertAt(at, kind, mode, then) {
    if (readOnly()) return Promise.resolve(null);
    return op(async (c) => {
      const id = await c.insert(at, N.KIND_OUT[kind] || 'python');
      if (!id) return null;
      if (kind === 'graph3d') await c.setSpec(id, N.buildSpec(null, [], [-5, 5, -5, 5, -5, 5], true));
      if (then) await then(c, id);
      const v = viewCellOf(id);
      if (v) select(v, mode || 'command');
      else S.pendingSelect = { id, mode: mode || 'command' };
      return id;
    });
  }
  /* Below (or above) the selected cell; with no cell selected, at the end (not the top, where a
     new cell in a long notebook would appear off-screen). Read-only (not connected): say so. */
  function insert(where, kind) {
    if (readOnly()) { toast('Connect the calculator to edit the notebook (Connect, top right).'); return; }
    const n = S.nb.cells.length, sel = S.sel >= 0 && S.sel < n ? S.sel : -1;
    insertAt(sel < 0 ? n : where === 'above' ? sel : sel + 1, kind || 'python', 'command');
  }
  /* "+ cell" under the last cell: a new cell at the end, ready to type into. */
  function appendCell() {
    if (readOnly()) { toast('Connect the calculator to edit the notebook (Connect, top right).'); return; }
    insertAt(S.nb.cells.length, 'python', 'edit');
  }

  function removeCell() {
    const c = cur();
    if (readOnly() || !c) return;
    if (c.lock === 'calc') { toast('This cell is being edited on the calculator'); return; }
    S.deleted.push({ kind: c.kind, source: c.source, at: S.sel });
    if (S.editing === c.id) { S.editing = null; clearTimeout(S.pushTimer); S.pushTimer = null; }
    const id = c.id;
    op((cl) => cl.del(id));
  }
  function undoDelete() {
    const d = S.deleted.pop();
    if (!d || readOnly()) return;
    insertAt(Math.min(d.at, S.nb.cells.length), d.kind, 'command', (c, id) => fill(c, id, d.kind, d.source));
  }
  function copyCell(cut) {
    const c = cur();
    if (!c) return;
    S.clip = { kind: c.kind, source: c.source };
    if (cut) removeCell();
    else toast('Cell copied');
  }
  function paste(above) {
    if (!S.clip || readOnly()) return;
    const clip = S.clip;
    insertAt(above ? S.sel : S.sel + 1, clip.kind, 'command', (c, id) => fill(c, id, clip.kind, clip.source));
  }
  /* a new cell's text (a graph cell: its functions, in a spec) */
  function fill(c, id, kind, source) {
    if (!source) return null;
    if (isGraph(kind)) {
      const d3 = kind === 'graph3d';
      return c.setSpec(id, N.buildSpec(null, source.split('\n'), d3 ? [-5, 5, -5, 5, -5, 5] : [-10, 10, -10, 10], d3));
    }
    return c.setSource(id, source);
  }
  function moveCell(by) {
    const c = cur();
    if (readOnly() || !c) return;
    const id = c.id;
    op((cl) => cl.move(id, by));
  }
  function setKind(kind) {
    const c = cur();
    if (readOnly() || !c || c.kind === kind) return;
    if (c.lock === 'calc') { toast('This cell is being edited on the calculator'); return; }
    if (kind === 'raw') kind = 'markdown';
    const id = c.id, was = c.kind;
    if (S.editing === id) pushNow(id);
    op(async (cl) => {
      if (N.KIND_OUT[was] !== N.KIND_OUT[kind]) await cl.kind(id, N.KIND_OUT[kind]);
      if (!isGraph(kind) || (kind === 'graph' && !isGraph(was))) return;
      // a 3D graph (or a 3D graph made a 2D one): its functions in a window of that kind
      const cc = cl.cell(id), src = isGraph(was) ? String(c.source) : '';
      const w = kind === 'graph3d' ? [-5, 5, -5, 5, -5, 5] : [-10, 10, -10, 10];
      await cl.setSpec(id, N.buildSpec(cc && isGraph(was) ? cc.spec : null, src.split('\n'), w, kind === 'graph3d'));
    });
  }

  function runSelected(after) {
    const c = cur();
    if (!c) return;
    if (c.kind === 'markdown') endEdit().then(() => view(c).update());
    else run([c]);
    if (after === 'advance') {
      if (S.sel === S.nb.cells.length - 1 && !readOnly()) insertAt(S.sel + 1, c.kind === 'markdown' ? 'python' : c.kind, 'edit');
      else if (S.sel < S.nb.cells.length - 1) { S.sel++; select(cur(), 'command'); }
    } else if (after === 'insert') {
      insertAt(S.sel + 1, 'python', 'edit');
    } else select(c, 'command');
  }

  /* ------------------------------------------------------------ keys */
  function editKey(e, v) {
    const mod = e.ctrlKey || e.metaKey;
    if (e.key === 'Enter' && (e.shiftKey || mod || e.altKey)) {
      e.preventDefault();
      runSelected(e.shiftKey ? 'advance' : e.altKey ? 'insert' : 'stay');
      return;
    }
    if (e.key === 'Enter' && S.live && v.cell.kind === 'math') {
      e.preventDefault(); // one line, as on the calculator: ENTER runs it
      runSelected('stay');
      return;
    }
    if (e.key === 'Escape') {
      e.preventDefault();
      select(v.cell, 'command');
      v.update();
      return;
    }
    if (mod && (e.key === 's' || e.key === 'S')) { e.preventDefault(); save(e.shiftKey); return; }
    if (e.target !== v.ta) return;
    const ta = v.ta;
    if ((e.key === 'ArrowUp' || e.key === 'ArrowDown') && e.altKey && !mod && !e.shiftKey) {
      e.preventDefault(); // earlier inputs, like the calculator's ENTRY recall
      if (!ta.readOnly) recallInput(v, e.key === 'ArrowUp' ? 1 : -1);
      return;
    }
    if (e.key === 'Tab' && !mod && !e.altKey) {
      e.preventDefault();
      if (!ta.readOnly) indent(ta, e.shiftKey);
      return;
    }
    if ((e.key === 'ArrowUp' || e.key === 'ArrowDown') && !e.shiftKey && !mod && !e.altKey && ta.selectionStart === ta.selectionEnd) {
      const before = ta.value.slice(0, ta.selectionStart);
      const up = e.key === 'ArrowUp';
      const edge = up ? !before.includes('\n') : !ta.value.slice(ta.selectionStart).includes('\n');
      const j = S.sel + (up ? -1 : 1);
      if (edge && j >= 0 && j < S.nb.cells.length) {
        e.preventDefault();
        const n = S.nb.cells[j];
        select(n, 'edit');
        if (S.mode === 'edit') view(n).focus(up ? 'end' : 'start');
      }
    }
  }

  function indent(ta, out) {
    const v = ta.value, a = ta.selectionStart, b = ta.selectionEnd;
    if (a === b && !out) { ta.setRangeText('    ', a, b, 'end'); ta.dispatchEvent(new Event('input')); return; }
    const ls = v.lastIndexOf('\n', a - 1) + 1, le = b > a && v[b - 1] === '\n' ? b - 1 : b;
    const block = v.slice(ls, le);
    const nb = block.split('\n').map((l) => (out ? l.replace(/^( {1,4}|\t)/, '') : '    ' + l)).join('\n');
    ta.setRangeText(nb, ls, le, 'preserve');
    ta.setSelectionRange(ls, ls + nb.length);
    ta.dispatchEvent(new Event('input'));
  }

  function chord(k) {
    const now = Date.now(), ok = S.lastKey === k && now - S.lastKeyAt < 600;
    S.lastKey = ok ? '' : k;
    S.lastKeyAt = now;
    return ok;
  }

  function commandKey(e) {
    const mod = e.ctrlKey || e.metaKey, k = e.key;
    if (mod && (k === 's' || k === 'S')) { e.preventDefault(); save(e.shiftKey); return; }
    if (mod && (k === 'o' || k === 'O')) { e.preventDefault(); openFile(); return; }
    if (k === 'Enter') {
      e.preventDefault();
      if (e.shiftKey) runSelected('advance');
      else if (mod) runSelected('stay');
      else if (e.altKey) runSelected('insert');
      else if (!readOnly() && cur()) select(cur(), 'edit');
      return;
    }
    if (mod || e.altKey) return;
    const act = {
      ArrowUp: () => S.sel > 0 && select(S.nb.cells[S.sel - 1], 'command'),
      k: () => S.sel > 0 && select(S.nb.cells[S.sel - 1], 'command'),
      ArrowDown: () => S.sel < S.nb.cells.length - 1 && select(S.nb.cells[S.sel + 1], 'command'),
      j: () => S.sel < S.nb.cells.length - 1 && select(S.nb.cells[S.sel + 1], 'command'),
      Home: () => S.nb.cells.length && select(S.nb.cells[0], 'command'),
      End: () => S.nb.cells.length && select(S.nb.cells[S.nb.cells.length - 1], 'command'),
      a: () => insert('above'), b: () => insert('below'),
      x: () => !readOnly() && copyCell(true), c: () => copyCell(false),
      v: () => paste(false), V: () => paste(true),
      d: () => chord('d') && removeCell(), z: () => undoDelete(),
      y: () => setKind('python'), m: () => setKind('markdown'), t: () => setKind('math'), g: () => setKind('graph'),
      G: () => setKind('graph3d'),
      i: () => chord('i') && interrupt(), 0: () => chord('0') && restart(false),
      s: () => save(false), h: () => showHelp(), o: () => toggleOutputs(),
      K: () => moveCell(-1), J: () => moveCell(1),
    }[k];
    if (act) { e.preventDefault(); act(); }
  }

  function toggleOutputs() {
    if (!cur()) return;
    const v = view(cur());
    v.outs.hidden = !v.outs.hidden;
  }

  document.addEventListener('keydown', (e) => {
    if ($('#help').open || $('#notebooks').open) return;
    const t = e.target;
    if (t.closest && (t.closest('#toolbar') || t.closest('header') || t.classList.contains('plot'))) {
      if ((e.ctrlKey || e.metaKey) && (e.key === 's' || e.key === 'S')) { e.preventDefault(); save(e.shiftKey); }
      return;
    }
    if (S.mode === 'edit' && (t.tagName === 'TEXTAREA' || t.tagName === 'INPUT')) return; // editKey handles it
    if (t.tagName === 'INPUT' || t.tagName === 'SELECT' || t.tagName === 'TEXTAREA') return;
    commandKey(e);
  });

  /* ------------------------------------------------------------ title, unsaved */
  function updateTitle() {
    const n = $('#nbname'), d = doc();
    const name = S.live && d ? (d.open ? d.name : '') : S.nb.name.replace(/\.ipynb$/i, '');
    if (document.activeElement !== n) n.value = name;
    n.placeholder = S.live && d && !d.open ? 'Unsaved (type a name to save it)' : '';
    n.readOnly = readOnly();
    const dirty = !!(S.live && d && d.open && d.dirty);
    document.title = (dirty ? '• ' : '') + (name || (S.live && d ? 'Unsaved' : 'No notebook')) + ' — Calculator notebook';
  }
  function markDirty() {
    const d = doc(), dirty = !!(S.live && d && d.open && d.dirty);
    const el = $('#dirty');
    el.hidden = !dirty;
    el.title = 'Changed since the calculator last saved this notebook (it saves when it is switched off, when another notebook is opened, and two minutes after the last change)';
  }

  /* ------------------------------------------------------------ files */
  /* a notebook shown read-only (a file opened here, or the last state seen) */
  function load(nb, handle) {
    for (const v of S.views.values()) v.el.remove();
    S.views.clear();
    S.nb = nb;
    S.handle = handle || null;
    S.sel = 0;
    S.mode = 'command';
    S.deleted = [];
    renderList();
    if (cur()) select(cur(), 'command');
    updateTitle();
    markDirty();
  }

  function viewText(text, name, handle) {
    try {
      load(N.fromIpynb(text, name), handle);
      toast('Opened ' + name + (HAS_SERIAL ? ' (read-only: not connected)' : ''));
    } catch (e) {
      alert('Cannot open ' + name + ': ' + e.message);
    }
  }

  /* a file from the computer: connected, it goes into the calculator's store and is opened there
   * (and so here); otherwise it is shown read-only */
  async function openText(text, name, handle) {
    if (!S.live) { viewText(text, name, handle); return; }
    try { N.fromIpynb(text, name); } catch (e) { alert('Cannot open ' + name + ': ' + e.message); return; }
    const nm = N.nameOf(name);
    const bytes = new TextEncoder().encode(text);
    try {
      const list = await S.client.list();
      if (list.some((x) => x.where === 'flash' && x.name.toLowerCase() === nm.toLowerCase()) &&
        !confirm('The calculator has a notebook named ' + nm + '. Replace it? (Its previous version is kept.)')) return;
      await endEdit();
      toast('Sending ' + nm + ' to the calculator…', 60000);
      await op((c) => c.importIpynb(nm, bytes, (f) => toast('Sending ' + nm + ' to the calculator: ' + Math.round(f * 100) + ' %', 60000)));
      S.handle = handle || null;
      toast('Opened ' + nm + ' on the calculator');
    } catch (e) {
      alert('Cannot open ' + name + ' on the calculator: ' + e.message);
    }
  }

  async function openFile() {
    if (HAS_FS) {
      try {
        const [fh] = await showOpenFilePicker({ types: [{ description: 'Jupyter notebook', accept: { 'application/x-ipynb+json': ['.ipynb'] } }] });
        const f = await fh.getFile();
        await openText(await f.text(), f.name, fh);
      } catch (e) { if (e.name !== 'AbortError') toast('Open: ' + e.message); }
    } else $('#file').click();
  }

  function newNotebook() {
    if (!connectedOrSay()) return;
    endEdit();
    op((c) => c.create());
  }

  /* the notebook as a file: the calculator's export when connected */
  async function serialize() {
    if (S.live && doc()) return S.client.exportJson();
    return N.stringify(S.nb, { png: plotPng, firmware: S.version || undefined });
  }
  const fileName = () => (S.live && doc() ? docName(doc()) + '.ipynb' : S.nb.name);

  async function save(as) {
    if (!HAS_FS) { download(); return; }
    try {
      await endEdit();
      const text = await serialize();
      if (as || !S.handle || S.handle.name !== fileName()) {
        S.handle = await showSaveFilePicker({ suggestedName: fileName(),
          types: [{ description: 'Jupyter notebook', accept: { 'application/x-ipynb+json': ['.ipynb'] } }] });
      }
      const w = await S.handle.createWritable();
      await w.write(text);
      await w.close();
      toast('Saved ' + S.handle.name + ' on the computer');
    } catch (e) {
      if (e.name === 'AbortError') return;
      toast('Saving failed (' + e.message + '): downloading instead');
      download();
    }
  }

  async function download() {
    try {
      await endEdit();
      const blob = new Blob([await serialize()], { type: 'application/x-ipynb+json' });
      const a = h('a', { href: URL.createObjectURL(blob), download: fileName() });
      document.body.append(a);
      a.click();
      setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
    } catch (e) { toast('Download: ' + e.message); }
  }

  /* ------------------------------------------------------------ the calculator's notebooks */
  async function fillList() {
    const ul = $('#nblist');
    ul.textContent = '';
    if (!S.live) { ul.append(h('li', { class: 'muted' }, 'Connect the calculator to see its notebooks.')); return; }
    let list;
    try { list = await S.client.list(); } catch (e) { ul.append(h('li', { class: 'muted' }, e.message)); return; }
    ul.textContent = '';
    const d = doc();
    if (!list.length) ul.append(h('li', { class: 'muted' }, 'No notebooks on the calculator yet.'));
    for (const it of list) {
      const card = it.where === 'card';
      const isOpen = d && d.open && d.name === it.name && (d.where === 'card') === card;
      ul.append(h('li', { class: isOpen ? 'open' : '' },
        h('span', { class: 'nbn' }, it.name), h('span', { class: 'muted' }, (card ? 'card, ' : '') + Math.ceil(it.size / 1024) + ' KB'),
        h('span', { class: 'acts' },
          h('button', { type: 'button', disabled: isOpen, onclick: () => { endEdit(); op((c) => c.open(it.name, card)); $('#notebooks').close(); } }, 'Open'),
          h('button', { type: 'button', onclick: () => {
            const to = prompt('New name for ' + it.name + ':', it.name);
            if (to && to.trim() && to.trim() !== it.name) op((c) => c.rename(it.name, to.trim(), card)).then(fillList);
          } }, 'Rename'),
          h('button', { type: 'button', onclick: () => {
            if (confirm('Delete the notebook ' + it.name + (card ? ' on the card' : '') + ' from the calculator?')) op((c) => c.remove(it.name, card)).then(fillList);
          } }, 'Delete'))));
    }
  }
  function showList() { $('#notebooks').showModal(); fillList(); }

  /* ------------------------------------------------------------ toolbar */
  function updateToolbar() {
    const on = S.live && !!S.client;
    const d = doc();
    for (const id of ['run', 'runall', 'restart', 'restartall', 'clear', 'new']) $('#btn-' + id).disabled = !on || !(id === 'new' || d);
    $('#btn-interrupt').disabled = !on || !(d && d.running);
    document.body.classList.toggle('readonly', readOnly());
    $('#offline').hidden = !HAS_SERIAL || S.live;
    $('#offline-text').textContent = S.link ? 'Reading the calculator’s notebook…' :
      'Not connected: this is the last state seen, read-only. Connect the calculator to edit and run.';
    markDirty();
  }

  function showHelp() { $('#help').showModal(); }

  function bindToolbar() {
    const b = (id, f) => $('#btn-' + id).addEventListener('click', f);
    b('connect', () => (S.link || S.reconnectTimer ? disconnect() : connect()));
    b('notebooks', showList);
    b('new', newNotebook);
    b('open', openFile);
    b('save', () => save(false));
    b('saveas', () => save(true));
    b('download', download);
    b('add', () => insert('below'));
    b('addcell', appendCell);
    b('cut', () => copyCell(true));
    b('copy', () => copyCell(false));
    b('paste', () => paste(false));
    b('up', () => moveCell(-1));
    b('down', () => moveCell(1));
    b('run', () => runSelected('advance'));
    b('runall', runAll);
    b('interrupt', interrupt);
    b('restart', () => restart(false));
    b('restartall', () => restart(true));
    b('clear', () => { if (connectedOrSay()) op((c) => c.clear()); });
    b('help', showHelp);
    b('emptylist', showList);
    b('emptynew', newNotebook);
    $('#kind').addEventListener('change', (e) => { setKind(e.target.value); if (cur()) select(cur(), 'command'); });
    $('#nbname').addEventListener('change', (e) => {
      const d = doc();
      if (readOnly() || !d) return;
      const to = e.target.value.trim(), from = d.open ? d.name : '';
      if (!to || to === from) { e.target.value = from; return; }
      // a name for the unsaved notebook saves it under that name (as MENU Save on the calculator)
      op((c) => (d.open ? c.rename(from, to, d.where === 'card') : c.saveAs(to))).catch(() => { e.target.value = from; });
    });
    $('#help-close').addEventListener('click', () => $('#help').close());
    $('#notebooks-close').addEventListener('click', () => $('#notebooks').close());
    $('#file').addEventListener('change', async (e) => {
      const f = e.target.files[0];
      if (f) await openText(await f.text(), f.name, null);
      e.target.value = '';
    });
    // drag and drop a notebook onto the page
    let depth = 0;
    document.addEventListener('dragenter', (e) => { if ([...e.dataTransfer.types].includes('Files')) { depth++; $('#drop').hidden = false; } });
    document.addEventListener('dragleave', () => { if (--depth <= 0) { depth = 0; $('#drop').hidden = true; } });
    document.addEventListener('dragover', (e) => e.preventDefault());
    document.addEventListener('drop', async (e) => {
      e.preventDefault();
      depth = 0;
      $('#drop').hidden = true;
      const item = [...e.dataTransfer.items].find((i) => i.kind === 'file');
      if (!item) return;
      let handle = null;
      if (item.getAsFileSystemHandle) { try { handle = await item.getAsFileSystemHandle(); } catch (err) { handle = null; } }
      const f = item.getAsFile();
      if (f) await openText(await f.text(), f.name, handle && handle.kind === 'file' ? handle : null);
    });
    window.addEventListener('beforeunload', () => { if (S.editing) endEdit(); });
  }

  /* ------------------------------------------------------------ start */
  function welcome() {
    const nb = new N.Notebook();
    nb.name = 'Calculator notebook.ipynb';
    nb.cells = [
      N.makeCell('markdown', '# Calculator notebook\n\nThis page shows the notebook on the calculator\'s **home screen**, and edits it: ' +
        'connect the calculator (**Connect**, top right) and both show the same notebook. Cells run **on the calculator**: ' +
        'Python cells in its Python, Math cells in its CAS (results typeset, e.g. $\\int_0^1 x^2\\,dx = \\frac13$), ' +
        'Graph cells are sampled on the calculator and drawn here.\n\n' +
        'Shift-Enter runs a cell; **H** (outside a cell) lists the keys.'),
    ];
    return nb;
  }

  function start() {
    bindToolbar();
    if (!HAS_SERIAL) {
      $('#banner').hidden = false;
      setStatus('off', 'No Web Serial');
    } else setStatus('off', 'Not connected');
    let nb = null;
    const last = store.get('last');
    if (last && HAS_SERIAL) { try { const j = JSON.parse(last); nb = N.fromIpynb(j.nb, j.name); } catch (e) { nb = null; } }
    load(nb || welcome());
    updateToolbar();
    if (HAS_SERIAL) {
      navigator.serial.addEventListener('connect', (e) => { if (S.wanted && !S.link && isCalc(e.target)) openPort(e.target, false).catch(() => startReconnect()); });
      navigator.serial.addEventListener('disconnect', (e) => { if (S.port && e.target === S.port && S.reader) S.reader.cancel().catch(() => {}); });
      // a port permitted before (this page, earlier): connect at once
      navigator.serial.getPorts().then((ps) => {
        const p = ps.find(isCalc);
        if (p) { S.wanted = true; openPort(p, true).catch(() => { setStatus('wait', 'Waiting for the calculator…'); startReconnect(); }); }
      });
    }
    const mq = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)');
    if (mq && mq.addEventListener) mq.addEventListener('change', () => { for (const v of S.views.values()) for (const p of v.plots) { p.opts.style = plotStyle(); p.draw(); } });
  }

  N.page = { link: () => S.link, client: () => S.client, state: S }; // (the tests look in)

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start); else start();
})();
