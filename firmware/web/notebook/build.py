#!/usr/bin/env python3
"""Build the browser notebook: one self-contained HTML file.

    build.py --out build/gen/calculator_notebook.html [--check FILE]

Inlines into src/page.html: src/style.css after KaTeX's stylesheet (its
@font-face rules rewritten to base64 woff2 data URLs for the fonts vendored in
third_party/katex/fonts, the others dropped: KaTeX then uses a local font for
those rare alphabets), KaTeX's script, the page's scripts (proto, model,
sync, markdown, plot, app), the help (src/help.html) and the license notice. The
result makes no network request (no URL in it is fetched; its
Content-Security-Policy forbids it anyway). Python only, no third-party
modules, and byte-for-byte reproducible (no dates, sorted inputs), so the
Makefile builds it for the "webui" module. --check FILE: exit 1 unless FILE
is what would be built.
"""
import argparse
import base64
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.normpath(os.path.join(HERE, "..", ".."))
KATEX = os.path.join(FW, "third_party", "katex")
SCRIPTS = ["proto.js", "model.js", "sync.js", "markdown.js", "plot.js", "plot3d.js", "app.js"]


def read(*p):
    with open(os.path.join(*p), encoding="utf-8") as f:
        return f.read()


def katex_css():
    css = read(KATEX, "katex.min.css")
    fonts = os.path.join(KATEX, "fonts")
    have = set(os.listdir(fonts))

    def face(m):
        rule = m.group(0)
        f = re.search(r"url\(fonts/(KaTeX_[A-Za-z0-9-]+)\.woff2\)", rule)
        if not f or f.group(1) + ".woff2" not in have:
            return ""  # not vendored: the browser falls back to a local font
        data = base64.b64encode(open(os.path.join(fonts, f.group(1) + ".woff2"), "rb").read()).decode()
        return re.sub(r"src:[^;}]*", f'src:url(data:font/woff2;base64,{data}) format("woff2")', rule)

    css = re.sub(r"@font-face\{[^}]*\}", face, css)
    if "url(" in css.replace("url(data:", ""):
        raise SystemExit("build.py: a URL other than data: is left in KaTeX's CSS")
    return css


def script(text, name):
    if re.search(r"</script", text, re.I):
        text = re.sub(r"</(script)", r"<\\/\1", text, flags=re.I)
    if "<!--" in text:
        text = text.replace("<!--", "<\\!--")
    return f"/* ---- {name} ---- */\n{text}\n"


def calc_help():
    """The `calc` library's reference for the page's help: the calculator's own help page
    "python-calc" (help/python.txt, the single source), its markup turned into HTML: ```
    blocks, "- " lists, `code`, [[links]] (plain text), {key} (keyboard keys)."""
    src = read(FW, "help", "python.txt")
    m = re.search(r"^@page python-calc [^\n]*\n(.*?)(?=^@page |\Z)", src, re.S | re.M)
    if not m:
        raise SystemExit("build.py: no @page python-calc in help/python.txt")
    keys = {"on": "ON (Interrupt in this page)", "enter": "Enter"}

    def inline(t):
        out = []
        for i, part in enumerate(t.split("`")):   # odd pieces are `code`: kept verbatim
            part = part.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            if i % 2:
                out.append("<code>" + part + "</code>")
                continue
            part = re.sub(r"\[\[([^\]]+)\]\]", lambda k: {"python": "the Python console", "graph": "graph"}.get(k.group(1), k.group(1)), part)
            out.append(re.sub(r"\{([a-z0-9]+)\}", lambda k: "<kbd>" + keys.get(k.group(1), k.group(1)) + "</kbd>", part))
        return "".join(out)

    out, items, code = ["<h3>The calc library (Python cells)</h3>"], [], None
    for line in m.group(1).strip().split("\n"):
        if line.startswith("```"):
            if code is None:
                code = []
            else:
                body = "\n".join(code).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
                out.append("<pre>" + body + "</pre>")
                code = None
            continue
        if code is not None:
            code.append(line)
        elif line.startswith("- "):
            items.append("<li>" + inline(line[2:]) + "</li>")
        else:
            if items:
                out.append("<ul>" + "".join(items) + "</ul>")
                items = []
            if line.strip():
                out.append("<p>" + inline(line) + "</p>")
    if items:
        out.append("<ul>" + "".join(items) + "</ul>")
    out.append("<p>In a notebook, <code>plot(...)</code> draws the graph in the cell (here and on the calculator) "
               "instead of switching the calculator to its graph screen.</p>")
    return "\n".join(out)


def build():
    page = read(HERE, "src", "page.html")
    css = katex_css() + "\n" + read(HERE, "src", "style.css")
    js = script(read(KATEX, "katex.min.js"), "KaTeX 0.16.22 (MIT License, third_party/katex/LICENSE)")
    for s in SCRIPTS:
        js += script(read(HERE, "src", s), s)
    lic = read(KATEX, "LICENSE").strip()
    esc = lic.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    helptext = read(HERE, "src", "help.html").replace("<!--@KATEX_LICENSE@-->", esc)
    if helptext.count("<!--@CALC_HELP@-->") != 1:
        raise SystemExit("build.py: <!--@CALC_HELP@--> must occur once in help.html")
    helptext = helptext.replace("<!--@CALC_HELP@-->", calc_help())
    notice = ("<!-- Calculator notebook. Includes KaTeX 0.16.22, MIT License:\n" + lic.replace("--", "- -") + "\n-->")
    parts = {"<!--@LICENSES@-->": notice, "/*@CSS@*/": css, "/*@JS@*/": js, "<!--@HELP@-->": helptext}
    for k, v in parts.items():
        if page.count(k) != 1:
            raise SystemExit(f"build.py: {k} must occur once in page.html")
        page = page.replace(k, v)
    return page.encode("utf-8")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out")
    ap.add_argument("--check")
    a = ap.parse_args()
    data = build()
    if a.check:
        try:
            same = open(a.check, "rb").read() == data
        except OSError:
            same = False
        if not same:
            print(f"build.py: {a.check} is not up to date (run build.py --out {a.check})")
            return 1
        return 0
    out = a.out or os.path.join(HERE, "calculator_notebook.html")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out + ".tmp", "wb") as f:
        f.write(data)
    os.replace(out + ".tmp", out)
    print(f"notebook: {out} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
