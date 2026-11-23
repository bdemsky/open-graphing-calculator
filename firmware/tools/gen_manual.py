#!/usr/bin/env python3
"""Build the user manual (docs/MANUAL.md, docs/manual.html) from the help sources.

    gen_manual.py [--extra build/gen/help_keys.txt]   write docs/MANUAL.md and docs/manual.html
    gen_manual.py --check                              fail if either file is out of date

The manual and the calculator's built-in help come from the same files,
firmware/help/*.txt, parsed and expanded by tools/gen_help.py (parse_files,
build): every page, command page and listing reads exactly as on the device,
so the two cannot drift apart. The help page "keys" is generated from
keys/layout.py (tools/gen_keydoc.py); it is read from --extra (the Makefile
passes $(BUILD)/gen/help_keys.txt) or generated in memory.

Structure (all decided here, nothing is inferred from file order):
  * Part "Getting started", "Keyboard" and "Apps and screens": the chapters
    of PARTS, each a list of free @page topics. A chapter whose title equals
    its first topic's title uses that topic as the chapter itself; the other
    topics are sections of it.
  * Part "Command reference": one chapter per CATALOG category (@category
    in help/input.txt, in source order), with the category's intro and then
    every @cmd page of the category (assign_categories in gen_help.py, so a
    command is where the CATALOG shows it), then a chapter listing the
    command groups (@group pages).
Every @page must appear in PARTS or in INTERNAL, so a new help page cannot
be left out of the manual by accident (the build fails and names it).
SCREENSHOTS maps topics to pictures in sim/screenshots/ (made by the UI
tests); a mapped picture that does not exist is an error. Images are linked
relative to docs/ (../sim/screenshots/, ../keys/keyboard.svg), not embedded.

Markup (see gen_help.py) is converted line by line: "## x" headings, "- x"
bullets, 4-space code lines, joined paragraphs, `code`, [[id|text]] links
(to the anchor of that topic; [[index]] goes to the table of contents) and
{KEY} key caps (<kbd>). The output is deterministic (no dates), so
`make test` can compare it with the checked-in files (make manual-check).
"""
import argparse
import glob
import html
import os
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
import gen_help  # noqa: E402

DOCS = os.path.join(FW, "docs")
OUT_MD = os.path.join(DOCS, "MANUAL.md")
OUT_HTML = os.path.join(DOCS, "manual.html")
SHOTS = "sim/screenshots"          # relative to firmware/, linked as ../sim/screenshots from docs/
KEYBOARD_SVG = "keys/keyboard.svg"
TITLE = "Open Graphing Calculator: User Manual"

# (part title, [(chapter title, [topic ids])])
PARTS = [
    ("Getting started", [
        ("Getting started", ["getting-started", "screen-layout", "apps-overview"]),
        ("Entering and editing", ["editing", "edit-bar", "undo", "clipboard", "history-search"]),
        ("Using help", ["help"]),
        ("Power, memory and touch", ["power", "mem", "touch", "errors"]),
        ("Installing the manual", ["install-content", "code-modules", "safe-mode"]),
        ("Connecting to a computer", ["usb"]),
        ("Use the calculator from a computer's browser", ["browser"]),
    ]),
    ("Keyboard", [
        ("Keyboard", ["keys"]),
        ("Soft keys", ["softkeys"]),
    ]),
    ("Apps and screens", [
        ("Home screen", ["home-screen", "home-notebook", "notebook-menu", "graph-cells", "result-actions", "forms-browser", "apps", "menu-cas", "menu-math", "nth-roots", "powers",
                         "menu-test", "menu-angle", "catalog", "vars"]),
        ("Textbook math", ["textbook", "textbook-templates"]),
        ("Settings", ["mode", "mode-angle", "mode-result", "mode-simplify", "mode-digits", "mode-format",
                      "mode-complex", "mode-graph", "mode-light", "mode-usb", "mode-display", "mode-preview", "mode-repeat",
                      "mode-theme", "mode-font"]),
        ("RPN mode", ["rpn", "rpn-stack"]),
        ("Graphing", ["graphing", "yeq", "window", "graph", "graph-trace", "zoom", "graph-modes", "graph-par",
                      "graph-pol", "graph-seq", "graph-impl", "graph-de", "graph-styles", "graph-calc", "graph-sliders",
                      "graph-touch", "table", "tblset"]),
        ("3D graphing", ["graph3d", "graph3d-eq", "graph3d-window", "graph3d-view", "graph3d-contours"]),
        ("Animation", ["animation", "animation-comet", "animation-3d", "animation-gif"]),
        ("Math Quest", ["mathquest"]),
        ("Statistics", ["stat", "stat-lists", "stat-edit", "stat-calc", "stat-regression", "stat-tests",
                        "stat-intervals", "stat-results", "stat-distr", "stat-plots", "stat-csv"]),
        ("Matrices", ["matrix-menu", "matrix-edit"]),
        ("Solver", ["solver", "polyroot", "linsys"]),
        ("Finance", ["finance", "tvm", "amortization", "cashflow", "interest-conv", "dbd"]),
        ("Base-n", ["basen"]),
        ("Units and constants", ["units", "constants", "uncertainty"]),
        ("Logical operators", ["logic-ops"]),
        ("Spreadsheet", ["sheet"]),
        ("microSD tools", ["sdtools"]),
        ("Geometry", ["geometry", "geometry-tools", "geometry-transform", "geometry-measure",
                      "geometry-files"]),
        ("Python", ["python", "python-calc"]),
        ("Notebook", ["notebook", "notebook-cells", "notebook-files"]),
    ]),
    ("Refresher", [
        ("Refresher", ["refresher"]),
        ("Calculus refresher", ["ref-calculus"]),
        ("Series refresher", ["ref-series"]),
        ("Vector calculus refresher", ["ref-vector"]),
        ("Linear algebra refresher", ["ref-linalg"]),
        ("ODE refresher", ["ref-ode"]),
        ("Transforms refresher", ["ref-transforms"]),
        ("Probability and statistics refresher", ["ref-prob"]),
        ("Complex numbers refresher", ["ref-complex"]),
        ("Number theory refresher", ["ref-numtheory"]),
        ("Boolean algebra refresher", ["ref-boolean"]),
        ("Optimization refresher", ["ref-optim"]),
    ]),
]
REFERENCE = "Command reference"
# pages that are deliberately not chapters: the help index (the manual has its own contents)
INTERNAL = {"index"}

# topic id -> [(file in sim/screenshots, caption)]; shown after the topic's first paragraph
SCREENSHOTS = {
    "mathquest": [("mathquest_map.png", "Math Quest: the map of the worlds"), ("mathquest_feedback.png", "A mistake explained")],
    "getting-started": [("home.png", "The home screen")],
    "apps-overview": [("apps.png", "The APPS menu")],
    "edit-bar": [("edit_bar.png", "MENU: the edit keys on the soft-key bar")],
    "clipboard": [("clips.png", "Recent clips")],
    "history-search": [("history_search.png", "History search")],
    "help": [("help_home.png", "A help page")],
    "home-screen": [("home_full_history.png", "Home screen with history")],
    "home-notebook": [("home_notebook_python.png", "Calculations and a Python cell on the home screen")],
    "notebook-menu": [("nav_context_menu.png", "MENU on the home screen: the notebook menu")],
    "graph-cells": [("graph_flow_yeq.png", "y=, a function, graph: a graph cell on the home screen"),
                    ("graph_cell_active.png", "Panning and zooming in place"),
                    ("graph3d_cell_turned.png", "A 3D graph cell")],
    "result-actions": [("rewrite_menu.png", "The REWRITE menu (SHIFT F4)")],
    "forms-browser": [("forms_textbook.png", "Alternate forms of x^4-1")],
    "menu-cas": [("cas_menu.png", "The CAS menu")],
    "catalog": [("catalog_search.png", "Searching the catalog"), ("catalog_categories.png", "Catalog categories")],
    "vars": [("vars.png", "The VARS menu")],
    "textbook": [("textbook_entry.png", "Textbook entry"), ("textbook_history.png", "Textbook history")],
    "mode": [("mode.png", "Settings")],
    "mode-format": [("format_eng.png", "ENG number format")],
    "mode-preview": [("preview.png", "Live preview")],
    "mode-repeat": [("repdec.png", "Repeating decimals")],
    "mode-theme": [("dark_home.png", "Dark theme: home"), ("dark_graph.png", "Dark theme: graph")],
    "mode-font": [("dark_large_home.png", "Large font")],
    "rpn": [("rpn_basic.png", "RPN stack"), ("rpn_textbook.png", "RPN with textbook display")],
    "yeq": [("yeq.png", "Y= functions")],
    "window": [("window.png", "WINDOW")],
    "graph": [("graph.png", "GRAPH")],
    "graph-trace": [("trace.png", "TRACE")],
    "zoom": [("graph_ztrig.png", "ZTrig")],
    "graph-par": [("par_graph.png", "A parametric curve")],
    "graph-pol": [("pol_graph.png", "A polar curve")],
    "graph-seq": [("seq_time.png", "Sequence, time plot"), ("seq_web.png", "Sequence, web plot")],
    "graph-impl": [("impl_graph.png", "Implicit relation")],
    "graph-de": [("de_added.png", "Slope field of y'=x-y with solution curves"), ("de_circles.png", "y'=-x/y: curves end at vertical tangents")],
    "graph-styles": [("style_above.png", "Shading above"), ("style_dotted.png", "Dotted style")],
    "graph-calc": [("calc_zero.png", "CALC zero"), ("calc_integ.png", "CALC integral")],
    "graph-sliders": [("slider_graph.png", "A slider on the graph")],
    "graph-touch": [("graph_label.png", "Labels"), ("graph_snap.png", "Snapping to a point")],
    "table": [("table.png", "TABLE")],
    "graph3d": [("g3d_saddle.png", "A shaded surface"), ("g3d_multi.png", "Several surfaces")],
    "graph3d-eq": [("g3d_entries.png", "3D entries")],
    "graph3d-view": [("g3d_wire.png", "Wireframe"), ("g3d_height.png", "Colour by height")],
    "graph3d-contours": [("g3d_contours_saddle.png", "Contours on a saddle"),
                         ("g3d_contour_map_sincos.png", "Contour map of sin(x)*cos(y)")],
    "stat": [("stat_menu.png", "The STAT menu")],
    "stat-edit": [("stat_edit.png", "List editor")],
    "stat-calc": [("stat_1var.png", "1-Var statistics")],
    "stat-regression": [("stat_linreg.png", "Linear regression")],
    "stat-tests": [("stat_tests_menu.png", "Tests menu"), ("stat_ztest.png", "Z-test result")],
    "stat-distr": [("stat_distr.png", "Distributions"), ("stat_shadenorm.png", "Shaded normal distribution")],
    "stat-plots": [("stat_scatter_linreg.png", "Scatter plot with regression line"),
                   ("stat_boxplot.png", "Box plot"), ("stat_histogram.png", "Histogram")],
    "stat-csv": [("stat_csv_menu.png", "CSV files")],
    "matrix-menu": [("matrix_menu.png", "The MATRIX menu")],
    "matrix-edit": [("matrix_edit.png", "Matrix editor")],
    "solver": [("solver_eq.png", "Equation solver")],
    "polyroot": [("solver_poly.png", "Polynomial roots")],
    "linsys": [("linsys_grid.png", "Linear system: the coefficients"), ("solver_lin.png", "Linear system: the solution")],
    "tvm": [("finance_tvm.png", "TVM Solver")],
    "amortization": [("finance_amort.png", "Amortization")],
    "basen": [("basen.png", "Base-n")],
    "units": [("units_menu.png", "The UNITS menu")],
    "uncertainty": [("uncertainty_home.png", "Uncertainty propagation"),
                    ("unc_sheet.png", "Uncertainties in the spreadsheet")],
    "sheet": [("sheet_grid.png", "Spreadsheet"), ("sheet_edit.png", "Editing a cell"),
              ("sheet_statplot.png", "Two linked columns as a stat plot on GRAPH")],
    "sdtools": [("sdtools_menu.png", "microSD tools")],
    "geometry": [("geo_triangle.png", "A construction"), ("geo_on_graph.png", "A construction on GRAPH")],
    "geometry-measure": [("geo_drag.png", "Measurements follow a drag")],
    "geometry-tools": [("geo_tangents.png", "Tangents to a circle")],
    "geometry-transform": [("geo_locus.png", "A locus"), ("geo_hexagon.png", "Rotations")],
    "geometry-files": [("geo_graphs.png", "Constructions with Y= graphs")],
    "python": [("python_start.png", "The Python console"), ("python_traceback.png", "A block, a function and a traceback"),
               ("python_chars.png", "F3 chars: what the keyboard lacks")],
    "python-calc": [("python_calc.png", "The calc library")],
    "notebook": [("notebook_python.png", "Editing a Python cell of several lines"), ("notebook_list.png", "The list of notebooks")],
    "notebook-cells": [("home_notebook_text.png", "A text cell on the home screen"), ("home_notebook_python.png", "A Python cell on the home screen")],
    "notebook-files": [("notebook_card_list.png", "A notebook in the calculator and its copy on the card")],
}

INLINE_RE = re.compile(r"(`[^`]*`)|\[\[([A-Za-z0-9_.-]+)(?:\|([^\]]*))?\]\]|\{([^{}\s]+)\}")


class ManualError(Exception):
    pass


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


# ------------------------------------------------------------------ document model
# blocks: ("h", level, text, anchor) | ("p", text) | ("label", text) (parse_markup also yields ("sub", text)) | ("ul", [text]) | ("code", [line]) | ("img", [(src, cap)])
def parse_markup(text):
    """Help markup of one topic -> list of blocks. The leading "# Title" line
    is dropped (the manual draws its own heading), and so is a repeat of it
    (the generated "keys" page has its title in the body as well)."""
    lines = text.split("\n")
    if lines and lines[0].startswith("# "):
        title = lines[0]
        lines = lines[1:]
        while lines and (not lines[0].strip() or lines[0] == title):
            lines = lines[1:]
    blocks, para = [], []

    def flush():
        if para:
            blocks.append(("p", " ".join(s.strip() for s in para)))
            para.clear()

    for ln in lines:
        if ln.startswith("    "):
            flush()
            if blocks and blocks[-1][0] == "code":
                blocks[-1][1].append(ln[4:])
            else:
                blocks.append(("code", [ln[4:]]))
        elif not ln.strip():
            flush()
        elif ln.startswith("## "):
            flush()
            blocks.append(("sub", ln[3:].strip()))
        elif ln.startswith("# "):
            flush()
            blocks.append(("sub", ln[2:].strip()))
        elif ln.startswith("- "):
            flush()
            if blocks and blocks[-1][0] == "ul":
                blocks[-1][1].append(ln[2:].strip())
            else:
                blocks.append(("ul", [ln[2:].strip()]))
        else:
            para.append(ln)
    flush()
    return blocks


class Manual:
    def __init__(self, paths):
        topics, groups, order = gen_help.parse_files(paths)
        gen_help.check_keycaps(topics)
        texts, _catalog, _ex, categories = gen_help.build(topics, groups, order)
        self.topics, self.order, self.texts, self.categories = topics, order, texts, categories
        _cats, self.member = gen_help.assign_categories(topics, order)
        self.toc = []          # (level, title, anchor)
        self.blocks = []       # ("h", level, title, anchor) and content blocks
        self.anchor = {}       # topic id -> anchor
        self.check_coverage()
        self.assign_anchors()
        self.layout()

    def check_coverage(self):
        listed = [t for _, chs in PARTS for _, ts in chs for t in ts]
        seen = set()
        for t in listed:
            if t not in self.topics:
                raise ManualError(f"gen_manual.py PARTS: unknown topic {t!r}")
            if self.topics[t].kind != "page":
                raise ManualError(f"gen_manual.py PARTS: {t!r} is not an @page")
            if t in seen:
                raise ManualError(f"gen_manual.py PARTS: {t!r} listed twice")
            seen.add(t)
        missing = [t for t in self.order if self.topics[t].kind == "page" and t not in seen and t not in INTERNAL]
        if missing:
            raise ManualError("help pages missing from the manual (add them to PARTS in tools/gen_manual.py): "
                              + ", ".join(f"{t} ({self.topics[t].where})" for t in missing))
        for t, shots in SCREENSHOTS.items():
            if t not in self.topics:
                raise ManualError(f"gen_manual.py SCREENSHOTS: unknown topic {t!r}")
            for f, _ in shots:
                if not os.path.exists(os.path.join(FW, SHOTS, f)):
                    raise ManualError(f"gen_manual.py SCREENSHOTS: {SHOTS}/{f} does not exist")

    def assign_anchors(self):
        for t in self.topics:
            self.anchor[t] = t
        self.anchor["index"] = "contents"

    def heading(self, level, title, anchor):
        self.blocks.append(("h", level, title, anchor))
        if level <= 2:
            self.toc.append((level, title, anchor))

    def topic(self, tid, level, as_heading=True):
        """Append topic tid: its heading (unless the chapter heading is the
        topic's) and its content; subheadings one level below."""
        t = self.topics[tid]
        if as_heading:
            self.heading(level, t.title, self.anchor[tid])
        shots = SCREENSHOTS.get(tid)
        first_para = True
        for b in parse_markup(self.texts[tid]):
            if b[0] == "sub" and t.kind == "cmd":
                self.blocks.append(("label", b[1]))      # Syntax / Examples: a run-in label
            elif b[0] == "sub":
                self.blocks.append(("h", level + 1, b[1], None))
            else:
                self.blocks.append(b)
            if shots and b[0] == "p" and first_para:
                self.blocks.append(("img", [(f"../{SHOTS}/{f}", c) for f, c in shots]))
                shots = None
            first_para = first_para and b[0] != "p"
        if shots:
            self.blocks.append(("img", [(f"../{SHOTS}/{f}", c) for f, c in shots]))

    def layout(self):
        for part, chapters in PARTS:
            self.heading(1, part, "part-" + slug(part))
            for title, tids in chapters:
                first = self.topics[tids[0]]
                if first.title == title:
                    self.heading(2, title, self.anchor[tids[0]])
                    if tids[0] == "keys":
                        self.blocks.append(("img", [(f"../{KEYBOARD_SVG}", "The keyboard")]))
                    self.topic(tids[0], 2, as_heading=False)
                    rest = tids[1:]
                else:
                    self.heading(2, title, "ch-" + slug(title))
                    rest = tids
                for tid in rest:
                    self.topic(tid, 3)
        # reference: categories in CATALOG order, commands A-Z inside
        self.heading(1, REFERENCE, "part-" + slug(REFERENCE))
        self.blocks.append(("p", "Every command of the computer algebra system, grouped as in the "
                                 "[[catalog|catalog]] ({shift} {apps}). Each entry shows the syntax, a "
                                 "description and examples; the line after ▶ is the result."))
        for k, (cid, title, apps) in enumerate(self.categories):
            self.heading(2, title, self.anchor[cid] if cid else "cat-other")
            if cid:
                for b in parse_markup("\n".join(gen_help.strip_blank(self.topics[cid].body))):
                    self.blocks.append(("h", 3, b[1], None) if b[0] == "sub" else b)
            cmds = sorted((c for c, m in self.member.items() if m == k), key=lambda c: c.lower())
            for c in cmds:
                self.topic(c, 3)
        self.heading(2, "Command groups", "ch-command-groups")
        self.blocks.append(("p", "The same commands listed by group, as on the help pages of the calculator."))
        for tid in self.order:
            if self.topics[tid].kind == "group":
                self.topic(tid, 3)


# ------------------------------------------------------------------ inline rendering
MD_ESC = re.compile(r"([\\*_\[\]<>|])")


def inline(text, man, fmt):
    """Render inline markup for fmt 'md' or 'html'."""
    out, pos = [], 0

    def plain(s):
        if fmt == "md":
            return MD_ESC.sub(r"\\\1", s)
        return html.escape(s, quote=False)

    for m in INLINE_RE.finditer(text):
        out.append(plain(text[pos:m.start()]))
        pos = m.end()
        code, link, ltext, key = m.groups()
        if code:
            body = code[1:-1]
            if fmt == "md":
                tick = "``" if "`" in body else "`"
                pad = " " if body.startswith("`") or body.endswith("`") or not body.strip() else ""
                out.append(f"{tick}{pad}{body}{pad}{tick}")
            else:
                out.append(f"<code>{html.escape(body, quote=False)}</code>")
        elif link:
            if link not in man.topics:
                raise ManualError(f"link to unknown topic {link!r}")
            label = ltext if ltext else man.topics[link].title
            a = man.anchor[link]
            if fmt == "md":
                out.append(f"[{plain(label)}](#{a})")
            else:
                out.append(f'<a href="#{a}">{html.escape(label, quote=False)}</a>')
        else:
            out.append(f"<kbd>{html.escape(key, quote=False)}</kbd>")
    out.append(plain(text[pos:]))
    return "".join(out)


# ------------------------------------------------------------------ Markdown
def render_md(man):
    o = [f"# {TITLE}", "",
         "<!-- Generated by tools/gen_manual.py from firmware/help/*.txt (the built-in help). "
         "Do not edit: change the help files and run `make manual`. -->", "",
         "<a id=\"contents\"></a>", "", "## Contents", ""]
    for level, title, a in man.toc:
        o.append(("  " * (level - 1)) + f"- [{inline(title, man, 'md')}](#{a})")
    o.append("")
    for b in man.blocks:
        k = b[0]
        if k == "h":
            _, level, title, a = b
            if a:
                o += [f'<a id="{a}"></a>', ""]
            o += [f"{'#' * min(level + 1, 6)} {inline(title, man, 'md')}", ""]
        elif k == "p":
            o += [inline(b[1], man, "md"), ""]
        elif k == "label":
            o += [f"**{inline(b[1], man, 'md')}**", ""]
        elif k == "ul":
            o += [f"- {inline(x, man, 'md')}" for x in b[1]] + [""]
        elif k == "code":
            o += ["```"] + b[1] + ["```", ""]
        elif k == "img":
            o += [" ".join(f"![{c}]({src})" for src, c in b[1]), ""]
    while o and not o[-1]:
        o.pop()
    return "\n".join(o) + "\n"


# ------------------------------------------------------------------ HTML
CSS = """
:root { --fg: #1a1a1a; --dim: #555; --rule: #ccc; --code: #f3f3f3; --link: #0645ad; --kbd: #fafafa; }
* { box-sizing: border-box; }
body { margin: 0 auto; max-width: 52rem; padding: 2rem 1rem 4rem; color: var(--fg); background: #fff;
       font: 11pt/1.5 "Charter", "Georgia", serif; }
h1, h2, h3, h4, h5 { font-family: "Helvetica Neue", Arial, sans-serif; line-height: 1.25; }
h1.title { font-size: 2rem; margin: 0 0 2rem; }
h1 { font-size: 1.7rem; border-bottom: 2px solid var(--fg); padding-bottom: .2rem; margin-top: 3rem; }
h2 { font-size: 1.35rem; border-bottom: 1px solid var(--rule); padding-bottom: .15rem; margin-top: 2.2rem; }
h3 { font-size: 1.1rem; margin-top: 1.6rem; }
h4, h5 { font-size: 1rem; margin-top: 1.2rem; }
a { color: var(--link); text-decoration: none; }
code, pre { font-family: "SF Mono", Menlo, Consolas, monospace; font-size: .88em; }
code { background: var(--code); padding: 0 .2em; border-radius: 3px; }
pre { background: var(--code); padding: .5rem .8rem; border-radius: 4px; overflow-x: auto; line-height: 1.35; }
kbd { font-family: "Helvetica Neue", Arial, sans-serif; font-size: .82em; border: 1px solid #999;
      border-bottom-width: 2px; border-radius: 4px; padding: 0 .3em; background: var(--kbd); white-space: nowrap; }
figure { display: inline-block; margin: .4rem 1rem .8rem 0; vertical-align: top; }
figure img { width: 320px; max-width: 100%; border: 1px solid var(--rule); image-rendering: pixelated; }
figure.wide img { width: 420px; max-width: 100%; image-rendering: auto; border: none; }
figure.wide { display: block; margin-right: 0; }
figcaption { font-size: .85em; color: var(--dim); }
nav.toc ul { list-style: none; padding-left: 1.2rem; margin: .2rem 0; }
nav.toc > ul { padding-left: 0; }
nav.toc li.l1 { font-weight: bold; margin-top: .6rem; }
nav.toc li.l2 { font-weight: normal; }
.cols { columns: 2; column-gap: 2rem; }
p.label { font-family: "Helvetica Neue", Arial, sans-serif; font-weight: bold; font-size: .9em;
          margin: .6rem 0 .2rem; }
p.label + pre { margin-top: .2rem; }
@media print {
  @page { size: A4; margin: 18mm 16mm; }
  body { max-width: none; padding: 0; font-size: 10pt; }
  h1 { break-before: page; }
  h1.title { break-before: avoid; }
  h2.chapter { break-before: page; }
  h1 + h2.chapter { break-before: avoid; }
  h1, h2, h3, h4, h5 { break-after: avoid; }
  pre, figure, li { break-inside: avoid; }
  p.label { break-after: avoid; }
  a { color: inherit; }
  nav.toc { break-after: page; }
}
"""


def render_html(man):
    o = ["<!doctype html>", '<html lang="en">', "<head>", '<meta charset="utf-8">',
         '<meta name="viewport" content="width=device-width, initial-scale=1">',
         f"<title>{html.escape(TITLE)}</title>",
         "<!-- Generated by tools/gen_manual.py from firmware/help/*.txt. Do not edit: run `make manual`. -->",
         f"<style>{CSS}</style>", "</head>", "<body>",
         f'<h1 class="title">{html.escape(TITLE)}</h1>',
         '<nav class="toc" id="contents"><h2>Contents</h2>', "<ul>"]
    open_sub = False
    for level, title, a in man.toc:
        if level == 1:
            if open_sub:
                o.append("</ul></li>")
            o.append(f'<li class="l1"><a href="#{a}">{inline(title, man, "html")}</a><ul class="cols">')
            open_sub = True
        else:
            o.append(f'<li class="l2"><a href="#{a}">{inline(title, man, "html")}</a></li>')
    if open_sub:
        o.append("</ul></li>")
    o += ["</ul>", "</nav>"]
    for b in man.blocks:
        k = b[0]
        if k == "h":
            _, level, title, a = b
            tag = f"h{min(level, 5)}"
            cls = ' class="chapter"' if level == 2 else ""
            ida = f' id="{a}"' if a else ""
            o.append(f"<{tag}{cls}{ida}>{inline(title, man, 'html')}</{tag}>")
        elif k == "p":
            o.append(f"<p>{inline(b[1], man, 'html')}</p>")
        elif k == "label":
            o.append(f'<p class="label">{inline(b[1], man, "html")}</p>')
        elif k == "ul":
            o.append("<ul>" + "".join(f"<li>{inline(x, man, 'html')}</li>" for x in b[1]) + "</ul>")
        elif k == "code":
            o.append("<pre>" + html.escape("\n".join(b[1]), quote=False) + "</pre>")
        elif k == "img":
            for src, c in b[1]:
                cls = ' class="wide"' if src.endswith(".svg") else ""
                o.append(f'<figure{cls}><img src="{html.escape(src)}" alt="{html.escape(c)}">'
                         f"<figcaption>{html.escape(c)}</figcaption></figure>")
    o += ["</body>", "</html>"]
    return "\n".join(o) + "\n"


# ------------------------------------------------------------------ main
def sources(extra):
    paths = sorted(glob.glob(os.path.join(FW, "help", "*.txt")))
    if extra:
        return paths + extra, None
    import gen_keydoc
    tmp = tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False, encoding="utf-8")
    tmp.write(gen_keydoc.help_page())
    tmp.close()
    return paths + [tmp.name], tmp.name


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--extra", action="append", default=[], help="generated help sources (help_keys.txt)")
    ap.add_argument("--check", action="store_true", help="fail if docs/MANUAL.md or docs/manual.html is stale")
    a = ap.parse_args()
    paths, tmp = sources(a.extra)
    try:
        man = Manual(paths)
        md, ht = render_md(man), render_html(man)
    except (gen_help.HelpError, ManualError) as e:
        print("gen_manual:", e, file=sys.stderr)
        return 1
    finally:
        if tmp:
            os.unlink(tmp)
    outs = [(OUT_MD, md), (OUT_HTML, ht)]
    if a.check:
        bad = [os.path.relpath(p, FW) for p, s in outs
               if not os.path.exists(p) or open(p, encoding="utf-8").read() != s]
        if bad:
            print("gen_manual: out of date (run `make manual`): " + ", ".join(bad), file=sys.stderr)
            return 1
        print("manual up to date")
        return 0
    os.makedirs(DOCS, exist_ok=True)
    for p, s in outs:
        with open(p, "w", encoding="utf-8") as f:
            f.write(s)
    print(f"wrote {os.path.relpath(OUT_MD, FW)} and {os.path.relpath(OUT_HTML, FW)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
