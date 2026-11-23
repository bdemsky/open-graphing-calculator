/*
 * A small Markdown renderer for text cells: headings (# and underlined),
 * paragraphs, **bold** / *italic* / _italic_ / ~~strike~~, `code`, fenced and
 * indented code blocks, block quotes, nested bullet and numbered lists (task
 * boxes too), tables, horizontal rules, links and images, and math: $...$ and
 * \(...\) inline, $$...$$ and \[...\] as blocks (renderMath, KaTeX in the
 * page). Raw HTML is shown as text, never interpreted; links only open
 * http(s), mailto and in-page targets; images only show data: URLs and the
 * cell's attachments (a notebook file can never make the page fetch anything).
 */
(function (X) {
  'use strict';

  const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

  /* opts: renderMath(tex, display) -> html; attachments {name: {mime: b64}} */
  function render(src, opts = {}) {
    const math = [];
    const mathHtml = (tex, display) => {
      const h = opts.renderMath ? opts.renderMath(tex, display) : '<span class="math">' + esc(tex) + '</span>';
      math.push(display ? '<div class="math-block">' + h + '</div>' : h);
      return '\u0000M' + (math.length - 1) + '\u0000';
    };
    // code first (math inside code stays code), then math, both into placeholders
    const code = [];
    let text = String(src).replace(/\r\n?/g, '\n');
    text = text.replace(/^( {0,3})(```+|~~~+)([^\n]*)\n([\s\S]*?)(?:\n\1\2[ \t]*(?=\n|$)|(?![\s\S]))/gm, (m, ind, fence, info, body) => {
      code.push('<pre><code' + (info.trim() ? ' class="language-' + esc(info.trim().split(/\s/)[0]) + '"' : '') + '>' +
        esc(body) + '</code></pre>');
      return '\u0000C' + (code.length - 1) + '\u0000';
    });
    text = text.replace(/(`+)([^`]|[^`][\s\S]*?[^`])\1(?!`)/g, (m, t, body) => {
      code.push('<code>' + esc(body.trim()) + '</code>');
      return '\u0000c' + (code.length - 1) + '\u0000';
    });
    text = text.replace(/\$\$([\s\S]+?)\$\$/g, (m, t) => '\n\n' + mathHtml(t.trim(), true) + '\n\n')
      .replace(/\\\[([\s\S]+?)\\\]/g, (m, t) => '\n\n' + mathHtml(t.trim(), true) + '\n\n')
      .replace(/\\\(([\s\S]+?)\\\)/g, (m, t) => mathHtml(t.trim(), false))
      .replace(/(^|[^\\$])\$(?!\s)((?:\\\$|[^$\n])+?)(?<!\s)\$(?!\d)/g, (m, pre, t) => pre + mathHtml(t, false));

    let html = blocks(text.split('\n'), opts);
    html = html.replace(/\u0000([CcM])(\d+)\u0000/g, (m, k, i) => (k === 'M' ? math[+i] : code[+i]));
    return html;
  }

  function blocks(lines, opts) {
    const out = [];
    let i = 0;
    const para = [];
    const flush = () => {
      if (para.length) out.push('<p>' + inline(para.join('\n'), opts) + '</p>');
      para.length = 0;
    };
    while (i < lines.length) {
      const l = lines[i];
      let m;
      if (/^\s*$/.test(l)) { flush(); i++; continue; }
      if (/^\u0000[CM]\d+\u0000\s*$/.test(l.trim())) { flush(); out.push(l.trim()); i++; continue; }
      if ((m = /^ {0,3}(#{1,6})\s+(.*?)\s*#*\s*$/.exec(l))) {
        flush();
        const n = m[1].length;
        out.push('<h' + n + ' id="' + slug(m[2]) + '">' + inline(m[2], opts) + '</h' + n + '>');
        i++;
        continue;
      }
      if (para.length && /^ {0,3}(=+|-+)\s*$/.test(l)) { // setext heading
        const n = l.trim()[0] === '=' ? 1 : 2, t = para.join(' ');
        para.length = 0;
        out.push('<h' + n + ' id="' + slug(t) + '">' + inline(t, opts) + '</h' + n + '>');
        i++;
        continue;
      }
      if (/^ {0,3}([-*_])(\s*\1){2,}\s*$/.test(l)) { flush(); out.push('<hr>'); i++; continue; }
      if (/^ {0,3}>/.test(l)) {
        flush();
        const q = [];
        while (i < lines.length && /^ {0,3}>/.test(lines[i])) q.push(lines[i++].replace(/^ {0,3}> ?/, ''));
        out.push('<blockquote>' + blocks(q, opts) + '</blockquote>');
        continue;
      }
      if (/^ {0,3}([-*+]|\d{1,9}[.)])\s+/.test(l)) {
        flush();
        const r = list(lines, i, opts);
        out.push(r.html);
        i = r.next;
        continue;
      }
      if (/^( {4}|\t)/.test(l) && !para.length) {
        const c = [];
        while (i < lines.length && (/^( {4}|\t)/.test(lines[i]) || (/^\s*$/.test(lines[i]) && i + 1 < lines.length && /^( {4}|\t)/.test(lines[i + 1]))))
          c.push(lines[i++].replace(/^( {4}|\t)/, ''));
        out.push('<pre><code>' + esc(c.join('\n')) + '</code></pre>');
        continue;
      }
      if (l.includes('|') && i + 1 < lines.length && /^\s*\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?\s*$/.test(lines[i + 1])) {
        flush();
        const r = table(lines, i, opts);
        out.push(r.html);
        i = r.next;
        continue;
      }
      para.push(l);
      i++;
    }
    flush();
    return out.join('\n');
  }

  function list(lines, i, opts) {
    const first = /^( *)([-*+]|\d{1,9}[.)])\s+/.exec(lines[i]);
    const indent = first[1].length, ordered = /\d/.test(first[2]);
    const start = ordered ? parseInt(first[2], 10) : 1;
    const items = [];
    while (i < lines.length) {
      const m = /^( *)([-*+]|\d{1,9}[.)])\s+(.*)$/.exec(lines[i]);
      if (!m || m[1].length !== indent || /\d/.test(m[2]) !== ordered) break;
      const body = [m[3]];
      const inner = m[1].length + m[2].length + 1;
      i++;
      while (i < lines.length) {
        const l = lines[i];
        if (/^\s*$/.test(l)) {
          if (i + 1 < lines.length && /^\s+/.test(lines[i + 1]) && lines[i + 1].search(/\S/) > indent) { body.push(''); i++; continue; }
          break;
        }
        const k = l.search(/\S/);
        if (k <= indent && /^ *([-*+]|\d{1,9}[.)])\s+/.test(l)) break;
        if (k <= indent && !/^\s/.test(l) && body.length > 1 && body[body.length - 1] === '') break;
        body.push(l.slice(Math.min(k, inner)));
        i++;
      }
      items.push(body);
    }
    const tag = ordered ? 'ol' : 'ul';
    const html = items.map((b) => {
      let first = b[0], box = '';
      const t = /^\[([ xX])\]\s+/.exec(first);
      if (t) { box = '<input type="checkbox" disabled' + (t[1] !== ' ' ? ' checked' : '') + '> '; first = first.slice(t[0].length); }
      const rest = [first].concat(b.slice(1));
      const loose = rest.includes('');
      let h = blocks(rest, opts);
      if (!loose) h = h.replace(/^<p>([\s\S]*?)<\/p>/, '$1');
      return '<li>' + box + h + '</li>';
    }).join('');
    return { html: '<' + tag + (ordered && start !== 1 ? ' start="' + start + '"' : '') + '>' + html + '</' + tag + '>', next: i };
  }

  function table(lines, i, opts) {
    const cells = (l) => l.trim().replace(/^\|/, '').replace(/\|$/, '').split(/(?<!\\)\|/).map((c) => c.trim());
    const head = cells(lines[i]);
    const align = cells(lines[i + 1]).map((c) => (/^:-+:$/.test(c) ? 'center' : /-:$/.test(c) ? 'right' : /^:-/.test(c) ? 'left' : ''));
    const st = (k) => (align[k] ? ' style="text-align:' + align[k] + '"' : '');
    let h = '<table><thead><tr>' + head.map((c, k) => '<th' + st(k) + '>' + inline(c, opts) + '</th>').join('') + '</tr></thead><tbody>';
    i += 2;
    while (i < lines.length && lines[i].includes('|') && !/^\s*$/.test(lines[i])) {
      const r = cells(lines[i++]);
      h += '<tr>' + head.map((x, k) => '<td' + st(k) + '>' + inline(r[k] || '', opts) + '</td>').join('') + '</tr>';
    }
    return { html: h + '</tbody></table>', next: i };
  }

  function slug(s) {
    return String(s).toLowerCase().replace(/\u0000\w\d+\u0000/g, '').replace(/[^\w\- ]+/g, '').trim().replace(/\s+/g, '-');
  }

  function safeUrl(u) {
    u = u.trim();
    return /^(https?:|mailto:|#)/i.test(u) ? u : null;
  }

  function inline(s, opts) {
    // escapes first, so \* stays a star
    const keep = [];
    s = s.replace(/\\([\\`*_{}[\]()#+\-.!|~$<>])/g, (m, c) => { keep.push(esc(c)); return '\u0000K' + (keep.length - 1) + '\u0000'; });
    s = esc(s);
    s = s.replace(/!\[([^\]]*)\]\(([^)\s]+)(?:\s+&quot;[^&]*&quot;)?\)/g, (m, alt, url) => {
      let src = null;
      const a = /^attachment:(.+)$/.exec(url);
      if (a && opts.attachments && opts.attachments[a[1]]) {
        const att = opts.attachments[a[1]], mime = Object.keys(att).find((k) => k.startsWith('image/'));
        if (mime) src = 'data:' + mime + ';base64,' + String(att[mime]).replace(/\s/g, '');
      } else if (/^data:image\/(png|jpeg|gif|webp|svg\+xml);base64,/i.test(url)) src = url;
      return src ? '<img alt="' + alt + '" src="' + src + '">' : '<span class="noimg">[image: ' + (alt || url) + ']</span>';
    });
    s = s.replace(/\[([^\]]+)\]\(([^)\s]+)(?:\s+&quot;[^&]*&quot;)?\)/g, (m, t, url) => {
      const u = safeUrl(url.replace(/&amp;/g, '&'));
      return u ? '<a href="' + esc(u) + '"' + (u[0] === '#' ? '' : ' target="_blank" rel="noopener noreferrer"') + '>' + t + '</a>' : t;
    });
    s = s.replace(/&lt;(https?:\/\/[^\s&]+)&gt;/g, (m, u) => '<a href="' + u + '" target="_blank" rel="noopener noreferrer">' + u + '</a>');
    s = s.replace(/(\*\*|__)(?=\S)([\s\S]*?\S)\1/g, '<strong>$2</strong>')
      .replace(/(^|[^\w*])\*(?=\S)([\s\S]*?\S)\*(?!\*)/g, '$1<em>$2</em>')
      .replace(/(^|[^\w])_(?=\S)([\s\S]*?\S)_(?!\w)/g, '$1<em>$2</em>')
      .replace(/~~(?=\S)([\s\S]*?\S)~~/g, '<del>$1</del>')
      .replace(/ {2,}\n|\\\n/g, '<br>\n');
    return s.replace(/\u0000K(\d+)\u0000/g, (m, i) => keep[+i]);
  }

  Object.assign(X, { renderMarkdown: render, escapeHtml: esc });
})(typeof module !== 'undefined' && module.exports ? module.exports : (globalThis.CalcNB = globalThis.CalcNB || {}));
