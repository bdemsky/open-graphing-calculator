// The console protocol layer (src/proto.js) without a device: framing, replies, log lines.
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const zlib = require('node:zlib');
const P = require('../src/proto.js');

/* a fake calculator: answers each written line with the lines of reply(cmd) */
function fake(reply) {
  const sent = [];
  let link;
  link = new P.CalcLink((bytes) => {
    const s = Buffer.from(bytes).toString('utf8');
    sent.push(s);
    if (s === '\x03') return;
    for (const cmd of s.split('\n').filter(Boolean)) {
      const out = reply(cmd);
      if (out !== null) setImmediate(() => link.feed(out));
    }
  });
  return { link, sent };
}

test('quote escapes like the console tokenizer expects', () => {
  assert.equal(P.quote('a"b\\c\nd'), '"a\\"b\\\\c\\nd"');
  assert.equal(P.quote('x\r\n\x03y\tz'), '"x\\ny\tz"'); // CR and Ctrl-C dropped, tab kept
  assert.equal(P.quote('π'), '"π"');
});

test('chunks keep every quoted piece under the limit and rebuild the text', () => {
  const code = 'print("x")\n'.repeat(80) + 'π\\"';
  const parts = P.chunks(code);
  assert.ok(parts.length > 1);
  for (const p of parts) assert.ok(P.utf8len(P.quote(p)) <= 302);
  assert.equal(parts.join(''), code);
});

test('parseExec: output pieces, value, latex, traceback', () => {
  let r = P.parseExec(['out=1', 'out+=par', 'out=tial', 'value+=ab', 'value=cd', 'latex=x^{2}']);
  assert.deepEqual(r, { stdout: '1\npartial\n', value: 'abcd', latex: 'x^{2}', error: null });
  r = P.parseExec(['out=before', 'out=Traceback (most recent call last):', 'out=  File "<stdin>", line 1, in <module>',
    'out=ZeroDivisionError: divide by zero', 'error=ZeroDivisionError']);
  assert.equal(r.stdout, 'before\n');
  assert.equal(r.error.ename, 'ZeroDivisionError');
  assert.equal(r.error.evalue, 'divide by zero');
  assert.equal(r.error.traceback.length, 3);
  r = P.parseExec(['out=Traceback (most recent call last):', 'out=  File "<stdin>", line 1, in <module>', 'out=KeyboardInterrupt: ', 'error=KeyboardInterrupt']);
  assert.equal(r.error.ename, 'KeyboardInterrupt');
  assert.equal(r.stdout, '');
});

test('CalcLink: one command at a time, log lines aside, ERR', async () => {
  const logs = [];
  const { link, sent } = fake((cmd) => {
    if (cmd === 'version') return '# boot\nversion=0.1.0 (x)\nplatform=sim\nOK\n';
    if (cmd.startsWith('eval')) return 'ERR cas status 2: division by zero\n';
    return 'OK\n';
  });
  link.onLog = (t) => logs.push(t);
  const [v, e] = await Promise.all([link.version(), link.evalCas('1/0')]);
  assert.equal(v.version, '0.1.0 (x)');
  assert.equal(v.platform, 'sim');
  assert.deepEqual(e, { ok: false, error: 'division by zero' });
  assert.deepEqual(logs, ['boot']);
  assert.equal(sent[0], 'version\n');
});

test('CalcLink: replies split anywhere, CRLF, multi-byte characters', async () => {
  const link = new P.CalcLink(() => {});
  const p = link.command('x');
  const bytes = Buffer.from('out=π ≤ 4\r\nOK\r\n');
  for (let i = 0; i < bytes.length; i++) link.feed(bytes.subarray(i, i + 1));
  const r = await p;
  assert.deepEqual(r.lines, ['out=π ≤ 4']);
});

test('CalcLink: a BEGIN..END block is data even when a line looks like ERR', async () => {
  const link = new P.CalcLink(() => {});
  const p = link.command('screen');
  link.feed('BEGIN SCREEN w=1 h=1\nERRxABCD\nOK\nEND\nOK\n');
  const r = await p;
  assert.ok(r.ok);
  assert.deepEqual(r.lines, ['BEGIN SCREEN w=1 h=1', 'ERRxABCD', 'OK', 'END']);
});

test('CalcLink: timeout leaves the queue in order, close fails what waits', async () => {
  const link = new P.CalcLink(() => {});
  const a = link.command('slow', { timeout: 30 });
  const b = link.command('next');
  await assert.rejects(a, /no answer/);
  link.feed('OK\n'); // the late answer of "slow": dropped
  link.feed('done=1\nOK\n');
  assert.deepEqual((await b).lines, ['done=1']);
  const c = link.command('never');
  link.close('gone');
  await assert.rejects(c, /gone/);
  await assert.rejects(link.command('after'), /not connected/);
});

test('interrupt sends Ctrl-C past the queue', async () => {
  const { link, sent } = fake(() => null);
  link.command('pyexec "while True: pass"').catch(() => {});
  await new Promise((r) => setImmediate(r));
  link.interrupt();
  assert.equal(sent[sent.length - 1], '\x03');
  link.close();
});

test('pyexec: long code goes through py buf + py cell', async () => {
  const cmds = [];
  const { link } = fake((cmd) => { cmds.push(cmd); return cmd === 'py cell' ? 'out=ok\nvalue=3\nOK\n' : 'OK\n'; });
  const r = await link.pyexec('x = 1\n' + '# comment\n'.repeat(60) + 'x + 2');
  assert.equal(r.value, '3');
  assert.equal(r.stdout, 'ok\n');
  assert.ok(cmds.filter((c) => c.startsWith('py buf ')).length >= 2);
  assert.equal(cmds[cmds.length - 1], 'py cell');
  for (const c of cmds) assert.ok(Buffer.byteLength(c) <= P.LINE_MAX);
});

test('pyexec: no Python module', async () => {
  const { link } = fake(() => 'ERR unknown command: pyexec\n');
  const r = await link.pyexec('1+1');
  assert.equal(r.error.ename, 'CalculatorError');
  assert.match(r.error.evalue, /Python module/);
});

test('crc32 and base64', () => {
  const b = Buffer.from('hello!? π');
  assert.equal(P.crc32(b), (zlib.crc32 ? zlib.crc32(b) : P.crc32(b)) >>> 0);
  assert.equal(P.b64encode(b), b.toString('base64'));
  assert.equal(P.b64encode(Buffer.from('ab')), 'YWI=');
});
