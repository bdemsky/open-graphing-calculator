// A minimal DOM and browser for running the built page's script under node (no browser):
// enough of document / elements / canvas / events / storage for src/app.js. The markup comes
// from the built page itself (a small parser for its own well-formed HTML).
'use strict';

class Ev {
  constructor(type, init = {}) { Object.assign(this, { bubbles: true }, init); this.type = type; this.defaultPrevented = false; }
  preventDefault() { this.defaultPrevented = true; }
  stopPropagation() { this.stopped = true; }
}

class Node0 {
  constructor() { this.listeners = {}; this.parentNode = null; this.childNodes = []; }
  addEventListener(t, f) { (this.listeners[t] = this.listeners[t] || []).push(f); }
  removeEventListener(t, f) { this.listeners[t] = (this.listeners[t] || []).filter((x) => x !== f); }
  dispatchEvent(e) {
    if (!e.target) e.target = this;
    for (let n = this; n; n = n.parentNode || (n.isBody ? n.ownerDocument : null)) {
      for (const f of (n.listeners[e.type] || []).slice()) f.call(n, e);
      if (e.stopped || !e.bubbles) break;
    }
    return !e.defaultPrevented;
  }
}

class Text0 extends Node0 {
  constructor(t) { super(); this.nodeType = 3; this.data = String(t); }
  get textContent() { return this.data; }
  remove() { if (this.parentNode) this.parentNode.removeChild(this); }
}

class El extends Node0 {
  constructor(tag, doc) {
    super();
    this.nodeType = 1;
    this.tagName = tag.toUpperCase();
    this.ownerDocument = doc;
    this.attrs = {};
    this.style = {};
    this.dataset = {};
    this.hidden = false;
    this.value = '';
    this.disabled = false;
    this.readOnly = false;
    this.checked = false;
    this._html = null;
    const self = this;
    this.classList = {
      _set() { return new Set((self.className || '').split(/\s+/).filter(Boolean)); },
      add(...c) { const s = this._set(); c.forEach((x) => s.add(x)); self.className = [...s].join(' '); },
      remove(...c) { const s = this._set(); c.forEach((x) => s.delete(x)); self.className = [...s].join(' '); },
      contains(c) { return this._set().has(c); },
      toggle(c, on) { const has = this.contains(c); if (on === undefined) on = !has; if (on) this.add(c); else this.remove(c); return on; },
    };
    this.className = '';
    this.selectionStart = this.selectionEnd = 0;
    if (this.tagName === 'CANVAS') { this.width = 300; this.height = 150; }
  }
  setAttribute(k, v) {
    this.attrs[k] = String(v);
    if (k === 'class') this.className = String(v);
    else if (k === 'id') this.id = String(v);
    else if (k === 'hidden') this.hidden = true;
    else if (k === 'value') this.value = String(v);
    else if (k === 'href' || k === 'src' || k === 'download' || k === 'type' || k === 'placeholder' || k === 'title') this[k] = String(v);
    else if (k === 'width' || k === 'height') this[k] = +v;
  }
  getAttribute(k) { return k in this.attrs ? this.attrs[k] : null; }
  get children() { return this.childNodes.filter((n) => n.nodeType === 1); }
  get firstChild() { return this.childNodes[0] || null; }
  get lastChild() { return this.childNodes[this.childNodes.length - 1] || null; }
  append(...ns) { for (const n of ns) this.appendChild(typeof n === 'string' ? new Text0(n) : n); }
  appendChild(n) { if (n.parentNode) n.parentNode.removeChild(n); n.parentNode = this; this.childNodes.push(n); this._html = null; return n; }
  insertBefore(n, ref) {
    if (!ref) return this.appendChild(n);
    if (n.parentNode) n.parentNode.removeChild(n);
    n.parentNode = this;
    this.childNodes.splice(this.childNodes.indexOf(ref), 0, n);
    return n;
  }
  removeChild(n) { const i = this.childNodes.indexOf(n); if (i >= 0) this.childNodes.splice(i, 1); n.parentNode = null; return n; }
  remove() { if (this.parentNode) this.parentNode.removeChild(this); }
  get textContent() { return this._html !== null ? this._html.replace(/<[^>]*>/g, '') : this.childNodes.map((n) => n.textContent).join(''); }
  set textContent(t) { for (const c of this.childNodes) c.parentNode = null; this.childNodes = []; this._html = null; if (t !== '') this.append(String(t)); }
  get innerHTML() { return this._html !== null ? this._html : this.textContent; }
  set innerHTML(h) { this.textContent = ''; this._html = String(h); }
  contains(n) { for (; n; n = n.parentNode) if (n === this) return true; return false; }
  matches(sel) {
    return sel.split(',').some((s) => {
      s = s.trim();
      if (s[0] === '#') return this.id === s.slice(1);
      if (s[0] === '.') return this.classList.contains(s.slice(1));
      return this.tagName === s.toUpperCase();
    });
  }
  closest(sel) { for (let n = this; n && n.nodeType === 1; n = n.parentNode) if (n.matches(sel)) return n; return null; }
  querySelectorAll(sel) {
    const out = [];
    const walk = (n) => { for (const c of n.children) { if (c.matches(sel)) out.push(c); walk(c); } };
    walk(this);
    return out;
  }
  querySelector(sel) { return this.querySelectorAll(sel)[0] || null; }
  focus() { this.ownerDocument.activeElement = this; this.dispatchEvent(new Ev('focus', { bubbles: false })); }
  blur() { if (this.ownerDocument.activeElement === this) this.ownerDocument.activeElement = this.ownerDocument.body; }
  click() { if (!this.disabled) this.dispatchEvent(new Ev('click')); }
  getBoundingClientRect() { return { left: 0, top: 0, width: 600, height: 340, right: 600, bottom: 340 }; }
  scrollIntoView() {}
  get scrollHeight() { return 20; }
  setSelectionRange(a, b) { this.selectionStart = a; this.selectionEnd = b; }
  setRangeText(t, a, b) { this.value = this.value.slice(0, a) + t + this.value.slice(b); this.selectionStart = this.selectionEnd = a + t.length; }
  showModal() { this.open = true; }
  close() { this.open = false; }
  setPointerCapture() {}
  getContext() {
    const el = this;
    return new Proxy({ draws: 0 }, {
      get(t, k) {
        if (k in t) return t[k];
        if (k === 'measureText') return (s) => ({ width: String(s).length * 6 });
        if (k === 'createImageData') return (w, h) => ({ width: w, height: h, data: new Uint8ClampedArray(w * h * 4) });
        if (k === 'putImageData') return (img) => { el.lastImage = img; };
        if (k === 'stroke') return () => { t.draws++; el.strokes = (el.strokes || 0) + 1; };
        if (k === 'fill') return () => { t.draws++; el.fills = (el.fills || 0) + 1; };
        return () => {};
      },
      set(t, k, v) { t[k] = v; return true; },
    });
  }
  toDataURL() { return 'data:image/png;base64,iVBORw0KGgo='; }
}

const VOID = new Set(['input', 'meta', 'br', 'hr', 'img', 'link']);

/* the built page -> {document, script} */
function parsePage(html) {
  const body = html.slice(html.indexOf('<body>') + 6, html.indexOf('<script>'));
  const script = html.slice(html.indexOf('<script>') + 8, html.lastIndexOf('</script>'));
  return { body: body.replace(/<!--[\s\S]*?-->/g, ''), script };
}

function makeDocument(bodyHtml) {
  const doc = new Node0();
  doc.nodeType = 9;
  doc.readyState = 'complete';
  doc.compatMode = 'CSS1Compat';
  doc.title = '';
  doc.createElement = (t) => new El(t, doc);
  doc.createTextNode = (t) => new Text0(t);
  const body = new El('body', doc);
  body.isBody = true;
  doc.body = body;
  doc.activeElement = body;
  const stack = [body];
  const re = /<(\/?)([a-zA-Z][a-zA-Z0-9]*)((?:\s+[^\s=>]+(?:="[^"]*")?)*)\s*(\/?)>/g;
  let m, last = 0;
  const ent = (t) => t.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#x([0-9a-f]+);/gi,
    (x, hx) => String.fromCodePoint(parseInt(hx, 16))).replace(/&amp;/g, '&');
  while ((m = re.exec(bodyHtml))) {
    const text = bodyHtml.slice(last, m.index);
    last = re.lastIndex;
    if (text.trim()) stack[stack.length - 1].appendChild(new Text0(ent(text)));
    const [, close, tag, attrs] = m;
    const t = tag.toLowerCase();
    if (close) {
      while (stack.length > 1) { const top = stack.pop(); if (top.tagName === t.toUpperCase()) break; }
      continue;
    }
    const el = new El(t, doc);
    for (const a of attrs.matchAll(/([^\s=]+)(?:="([^"]*)")?/g)) el.setAttribute(a[1], a[2] === undefined ? '' : a[2].replace(/&amp;/g, '&'));
    stack[stack.length - 1].appendChild(el);
    if (!VOID.has(t)) stack.push(el);
  }
  doc.querySelector = (sel) => body.querySelector(sel);
  doc.querySelectorAll = (sel) => body.querySelectorAll(sel);
  doc.getElementById = (id) => body.querySelector('#' + id);
  return doc;
}

/* a window-like global for vm.createContext */
function makeWindow(doc, extra = {}) {
  const timers = new Set();
  const store = new Map();
  const w = {
    document: doc, console, TextEncoder, TextDecoder, Uint8Array, Uint16Array, Uint32Array, Uint8ClampedArray, Float64Array,
    Promise, Map, Set, JSON, Math, Date, Error, Array, Object, String, Number, RegExp, Symbol, Proxy, Reflect,
    ReadableStream, WritableStream, crypto: globalThis.crypto, btoa, atob, encodeURIComponent, unescape, escape: globalThis.escape,
    setTimeout: (f, t) => { const id = setTimeout(() => { timers.delete(id); f(); }, t); timers.add(id); return id; },
    clearTimeout: (id) => { clearTimeout(id); timers.delete(id); },
    setInterval: (f, t) => { const id = setInterval(f, t); timers.add(id); return id; },
    clearInterval: (id) => { clearInterval(id); timers.delete(id); },
    requestAnimationFrame: (f) => { const id = setTimeout(() => { timers.delete(id); f(); }, 0); timers.add(id); return id; },
    localStorage: { getItem: (k) => (store.has(k) ? store.get(k) : null), setItem: (k, v) => store.set(k, String(v)), removeItem: (k) => store.delete(k) },
    getComputedStyle: () => ({ getPropertyValue: () => '' }),
    matchMedia: () => ({ matches: false, addEventListener() {} }),
    ResizeObserver: class { observe() {} disconnect() {} },
    Event: Ev, Blob, URL: { createObjectURL: (b) => { w.blobs.push(b); return 'blob:' + w.blobs.length; }, revokeObjectURL() {} },
    confirm: () => true, alert: (m) => w.alerts.push(m), prompt: (q, d) => (w.promptAnswer !== undefined ? w.promptAnswer : d),
    innerHeight: 800, devicePixelRatio: 1,
    navigator: {},
    blobs: [], alerts: [],
    stopTimers() { for (const id of timers) { clearTimeout(id); clearInterval(id); } timers.clear(); },
  };
  w.window = w;
  w.self = w;
  w.globalThis = w;
  w.addEventListener = (t, f) => doc.addEventListener(t, f);
  return Object.assign(w, extra);
}

module.exports = { Ev, El, parsePage, makeDocument, makeWindow };
