/*
 * The calculator's console protocol (firmware/README.md "Console protocol";
 * the same logic as tools/calcctl.py), independent of the transport: the
 * page feeds it the bytes of the USB serial port (Web Serial), the tests the
 * simulator's stdout (node, test/sim.test.js).
 *
 *   request   one line: a command and arguments, "double quoted" with \" \\ \n
 *   reply     output lines, then exactly one terminator: OK or ERR <message>
 *   "# " lines are asynchronous log messages, never part of a reply (the
 *   notebook's events among them: sync.js)
 *   BEGIN ... END blocks (the `screen` dump) are data: a base64 line there may
 *   start with "ERR" or be anything else
 *
 * Commands run one at a time in order (the queue below). A Ctrl-C byte (0x03,
 * interrupt()) is sent past the queue: the calculator takes it as the ON key's
 * break (KeyboardInterrupt in Python, "interrupted" in the CAS) and the
 * console drops it.
 */
(function (X) {
  'use strict';

  const VID = 0x1209, PID = 0x0001; // include/usb_ids.h
  const LINE_MAX = 383; // the console's line buffer: 384 bytes with the NUL (core/console.c)
  const PY_CODE_MAX = 399; // pyexec's code buffer (modules/python/py_entry.c)
  const CHUNK = 300; // escaped bytes per `py buf` line

  const enc = new TextEncoder();
  const utf8len = (s) => enc.encode(s).length;

  /* A console argument: "..." with \\ \" \n escapes; CR and other control
   * characters (a Ctrl-C would be a break) are left out, tabs kept. */
  function quote(s) {
    let o = '"';
    for (const ch of String(s)) {
      const c = ch.codePointAt(0);
      if (ch === '\\') o += '\\\\';
      else if (ch === '"') o += '\\"';
      else if (ch === '\n') o += '\\n';
      else if ((c < 0x20 && ch !== '\t') || c === 0x7f) continue;
      else o += ch;
    }
    return o + '"';
  }

  /* A Python string literal for any text (JSON's escapes are Python's too). */
  const pyStr = (s) => JSON.stringify(String(s));

  /* ------------------------------------------------------------ the link */
  class CalcLink {
    /* write(Uint8Array) sends bytes (may return a promise) */
    constructor(write) {
      this.write = write;
      this.queue = [];
      this.cur = null;
      this.partial = '';
      this.dec = new TextDecoder('utf-8');
      this.onLog = null; // (text) for "# " lines
      this.onStray = null; // (line) outside any reply
      this.closed = false;
    }

    /* Bytes from the device (Uint8Array, or a string in tests). */
    feed(data) {
      this.partial += typeof data === 'string' ? data : this.dec.decode(data, { stream: true });
      let i;
      while ((i = this.partial.indexOf('\n')) >= 0) {
        const line = this.partial.slice(0, i).replace(/\r$/, '');
        this.partial = this.partial.slice(i + 1);
        this._line(line);
      }
    }

    _line(l) {
      const c = this.cur;
      if (c && c.block) { // inside BEGIN ... END: data
        c.lines.push(l);
        if (l === 'END') c.block = false;
        return;
      }
      if (l.startsWith('# ') || l === '#') {
        if (this.onLog) this.onLog(l.slice(2));
        return;
      }
      if (!c) {
        if (l && this.onStray) this.onStray(l);
        return;
      }
      if (l === 'OK') return this._finish(true, null);
      if (l.startsWith('ERR')) return this._finish(false, l.replace(/^ERR\s?/, ''));
      if (l.startsWith('BEGIN ')) c.block = true;
      c.lines.push(l);
      if (c.onLine) c.onLine(l);
    }

    _finish(ok, err) {
      const c = this.cur;
      this.cur = null;
      if (c.timer) clearTimeout(c.timer);
      if (!c.abandoned) c.resolve({ ok, lines: c.lines, error: err });
      this._next();
    }

    _next() {
      if (this.cur || !this.queue.length || this.closed) return;
      const c = this.queue.shift();
      this.cur = c;
      if (c.timeout) {
        c.timer = setTimeout(() => { // the reply may still come: it is then dropped
          c.abandoned = true;
          c.reject(new Error('no answer from the calculator (' + c.cmd.split(' ')[0] + ')'));
        }, c.timeout);
      }
      Promise.resolve()
        .then(() => this.write(enc.encode(c.cmd + '\n')))
        .catch((e) => { if (this.cur === c) { this.cur = null; c.timer && clearTimeout(c.timer); c.reject(e); this._next(); } });
    }

    /* One command: {ok, lines, error}. opts: timeout (ms, 0 = none), onLine. */
    command(cmd, opts = {}) {
      if (this.closed) return Promise.reject(new Error('not connected'));
      if (utf8len(cmd) > LINE_MAX) return Promise.reject(new Error('command longer than a console line'));
      return new Promise((resolve, reject) => {
        this.queue.push({ cmd, lines: [], resolve, reject, timeout: opts.timeout || 0, onLine: opts.onLine });
        this._next();
      });
    }

    /* Ctrl-C: the ON key's break, past the queue. */
    interrupt() {
      if (!this.closed) return this.write(new Uint8Array([3]));
    }

    get busy() { return !!this.cur || this.queue.length > 0; }

    /* The device went away: every waiting command fails. */
    close(why) {
      this.closed = true;
      const e = new Error(why || 'disconnected');
      const all = (this.cur ? [this.cur] : []).concat(this.queue);
      this.cur = null;
      this.queue = [];
      for (const c of all) {
        if (c.timer) clearTimeout(c.timer);
        if (!c.abandoned) c.reject(e);
      }
    }

    /* ---- commands ---- */
    async kv(cmd, opts) {
      const r = await this.command(cmd, opts);
      const out = {};
      for (const l of r.lines) {
        const k = l.indexOf('=');
        if (k > 0) out[l.slice(0, k)] = l.slice(k + 1);
      }
      if (!r.ok) out.error = r.error;
      return out;
    }

    version() { return this.kv('version', { timeout: 3000 }); }

    /* Run a Python cell (pyexec; a cell too long for one console line goes
     * through `py buf` + `py cell`). onOutput(text) gets the output as it
     * arrives. Returns parseExec()'s result. */
    async pyexec(code, onOutput) {
      code = String(code).replace(/\r\n?/g, '\n');
      const onLine = onOutput ? (l) => {
        if (l.startsWith('out=')) onOutput(l.slice(4) + '\n');
        else if (l.startsWith('out+=')) onOutput(l.slice(5));
      } : undefined;
      const one = 'pyexec ' + quote(code);
      let r;
      if (utf8len(one) <= LINE_MAX && utf8len(code) <= PY_CODE_MAX) {
        r = await this.command(one, { onLine });
      } else {
        for (const piece of chunks(code)) {
          const b = await this.command('py buf ' + quote(piece));
          if (!b.ok) {
            await this.command('py clear').catch(() => {});
            return { stdout: '', value: null, latex: null, error: deviceError(b.error) };
          }
        }
        r = await this.command('py cell', { onLine });
      }
      if (!r.ok) return { stdout: '', value: null, latex: null, error: deviceError(r.error) };
      return parseExec(r.lines);
    }

    /* The CAS directly (no LaTeX): {ok, result, error} */
    async evalCas(expr) {
      const r = await this.command('eval ' + quote(expr));
      if (!r.ok) return { ok: false, error: r.error.replace(/^cas status \d+: /, '') };
      const l = r.lines.find((x) => x.startsWith('result='));
      return { ok: true, result: l ? l.slice(7) : '' };
    }

    key(...names) { return this.command('key ' + names.join(' ')); }
    type(text) { return this.command('type ' + quote(text)); }
  }

  /* Pieces of code whose quoted form stays under CHUNK bytes. */
  function chunks(code) {
    const out = [];
    let cur = '', n = 2;
    for (const ch of code) {
      const k = utf8len(quote(ch)) - 2;
      if (n + k > CHUNK) { out.push(cur); cur = ''; n = 2; }
      cur += ch; n += k;
    }
    if (cur || !out.length) out.push(cur);
    return out;
  }

  function deviceError(msg) {
    const m = String(msg || 'error');
    let hint = m;
    if (/unknown command|python module|not installed|no python/i.test(m))
      hint = m + ' (Python needs the Python module: Memory > Modules on the calculator)';
    return { ename: 'CalculatorError', evalue: hint, traceback: ['CalculatorError: ' + hint] };
  }

  /* pyexec's reply lines -> {stdout, value, latex, error: {ename, evalue, traceback[]} | null}
   * out=TEXT ends a line, out+=TEXT is a piece without the newline; value+= /
   * latex+= are pieces of a long value; error=TYPE after a traceback. */
  function parseExec(lines) {
    let out = '', value = null, latex = null, ename = null, vp = '', lp = '';
    for (const l of lines) {
      if (l.startsWith('out=')) out += l.slice(4) + '\n';
      else if (l.startsWith('out+=')) out += l.slice(5);
      else if (l.startsWith('value+=')) vp += l.slice(7);
      else if (l.startsWith('value=')) { value = vp + l.slice(6); vp = ''; }
      else if (l.startsWith('latex+=')) lp += l.slice(7);
      else if (l.startsWith('latex=')) { latex = lp + l.slice(6); lp = ''; }
      else if (l.startsWith('error=')) ename = l.slice(6);
      else out += l + '\n';
    }
    let error = null;
    if (ename !== null) {
      const tb = 'Traceback (most recent call last):\n';
      let at = out.lastIndexOf(tb);
      while (at > 0 && out[at - 1] !== '\n') at = out.lastIndexOf(tb, at - 1);
      let trace = '';
      if (at >= 0) { trace = out.slice(at); out = out.slice(0, at); }
      const tl = trace.replace(/\n$/, '').split('\n').filter((x) => x !== '');
      const last = tl.length ? tl[tl.length - 1] : ename;
      const evalue = last.startsWith(ename + ': ') ? last.slice(ename.length + 2) : last === ename ? '' : last;
      error = { ename, evalue, traceback: tl.length ? tl : [ename] };
    }
    return { stdout: out, value, latex, error };
  }

  /* ------------------------------------------------------------ bytes */
  const CRC_TABLE = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      t[n] = c >>> 0;
    }
    return t;
  })();
  function crc32(bytes, crc = 0) {
    crc = ~crc >>> 0;
    for (let i = 0; i < bytes.length; i++) crc = CRC_TABLE[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8);
    return ~crc >>> 0;
  }

  function b64encode(bytes) {
    const A = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    let s = '';
    for (let i = 0; i < bytes.length; i += 3) {
      const v = (bytes[i] << 16) | ((bytes[i + 1] || 0) << 8) | (bytes[i + 2] || 0);
      s += A[(v >> 18) & 63] + A[(v >> 12) & 63] + (i + 1 < bytes.length ? A[(v >> 6) & 63] : '=') +
        (i + 2 < bytes.length ? A[v & 63] : '=');
    }
    return s;
  }

  Object.assign(X, {
    VID, PID, LINE_MAX, quote, pyStr, utf8len, CalcLink, parseExec, chunks, crc32, b64encode,
  });
})(typeof module !== 'undefined' && module.exports ? module.exports : (globalThis.CalcNB = globalThis.CalcNB || {}));
