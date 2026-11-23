# The Notebook

The home screen is a notebook: a column of cells with text, math, Python and graphs, saved as a
Jupyter notebook (`.ipynb`, nbformat 4.5) so that a computer opens it in Jupyter or VS Code. There is
one notebook view on the calculator, the home screen (calculations, graph cells made by graphing
from the home screen, text and Python cells, the notebook menu), and one in the browser page
(`CALCULATOR.HTML`); both show the same document, the one the home screen has loaded, named or not.
The Python module adds two helper screens: the list of notebooks (MENU Open, APPS > Notebook) and the
editor of a cell of several lines (ENTER on a Python cell).
User documentation: the help pages `home-notebook`, `notebook-menu`, `graph-cells`, `notebook`,
`notebook-cells`, `notebook-files`, `browser`
(docs/MANUAL.md, "Notebook"). This file is the design: where the code lives, saving, the storage,
the file mapping, the USB drive, the browser page (one notebook in two views), the tests and what
is left for later.

## One document: the home screen's notebook

The calculator has one open notebook and one document model for it, `nb_t` (`app/nbdoc.h`). It is
core code: the document (about 18 KB), the arena its strings live in (256 KB) and a second copy of
both for `stash` live in the SDRAM (carved by `app_init` through `nbdoc_mem_init`), so the home
screen works without the Python module (the core tier, the module not installed or `modx off`).

| | Where | What |
|---|---|---|
| Document, arena, cells, stash | `app/nbdoc.c` | `nbd`, `nbd_alloc` / `nbd_free` (first fit, neighbours merged, the free tail given back), insert / delete / clear outputs, ids, `nbd_changed` (a named notebook: dirty, `nb touched` to the module) |
| Home screen | `app/home.c` | math cells (input = `src`, result = `val`, an error = `ename` CASError + `evalue` "ERR:..."), selection, recall, text / Python / edit-in-place lines, the NOTEBOOK menu (`MENU` on the home screen), the name prompt `scr_nbname`, the title bar ("Notebook: Unsaved", "Notebook: *Lab 1"), graphing into the notebook (below), the session blob "home" v2 |
| Home screen, 2D view | `app/m2d_home.c` | blocks of every kind of cell (heights cached in `nb_cell_t.hh`), the entry tag "text" / "python" / "edit" |
| Graph cells | `app/gcell.c` | the spec (`NBF_GRAPH`), drawing through the graph app's plot code, the image cache, pan / zoom / pinch / resize, binding to the graph app |
| The module's view | `modules/python/nb_*.c` | `NB` is `*nb_docp`, the core's document (`py_core_t.doc`, `nbdoc_api_t`); saving, opening, `.ipynb`, the store, the card, USB, Python cells, the browser page |

**Unsaved and named.** `NB.open` means "a named notebook" (from the store or the card). Without a
name the notebook is the home screen's calculations, kept in the session (blob "home", like the
history before it) and never written to the store by the module (`nb_save` returns at once). Either
way it is *the* notebook: the browser page follows it and edits it (`nb doc` etc. answer for the
unsaved one too, with `name` "" and `open` 0), and the title bar shows its name ("Unsaved" without
one, a star while a named one has changes not yet stored). A named
notebook is saved by the module as before (power-off, low battery, 2 minutes idle,
opening another, an export); the session keeps a copy of it too (the newest cells within 60 KB), so
the home screen shows it right after a reboot without the module. Opening another notebook over
unsaved calculations first stores them as "Notebook N" (`nb_keep_scratch`, a toast says so): they
are never lost. Closing a notebook (New, `nb close`, a delete) leaves an empty unsaved one.

**One document, one view on the calculator.** The home screen is the notebook: a calculation typed
there is a math cell, graphing started there ends in a graph cell, a cell the browser page adds or
runs shows there at once (`home_tick` redraws when `nbd->gen` moved). The module's Notebook screen
no longer shows a page of cells of its own: it keeps the list of notebooks (opening one goes back to
the home screen, which shows it; the list's New makes an empty named notebook there) and the editor
of a cell of several lines (`nb editcell ID` from ENTER on a Python cell, or on a text cell of
several lines; F1 run / F5 done / F4 cancel go back home). Both live in the QSPI module, which costs
no internal flash. The document lives in the core so that the home screen works without the module;
the `.ipynb` reader and writer, the store and Python stayed in the module. Without the module the
notebook menu says that saving, opening, exporting and Python cells need it.

**Graphing into the notebook** (`app/home.c`, "graphing into the notebook"). The graph key (F1
"graph") and table (SHIFT F1) on the home screen open the graph screens in the *home flow*
(`gflow`). The graph key opens the graph editor, 2D first: Y= (`scr_yeq`, its mode header for
PAR/POL/SEQ/IMPL); its 2D/3D switch (`graph_editor_switch`, `app/yeq.c`: F1 "3D" on Y=, F1 "2D" on
the 3D Z= editor `scr_g3d_eq`, and the "2D 3D" chips at the right of both headers, which a tap
switches) changes between the two editors within the flow (the editor of a bound cell stays of the
cell's kind: a toast). Graph and 3D graph are no longer in the APPS list (`app/apps.c` leaves them
out); their full screens are reached from graph cells (ENTER) and the graph key, and CATALOG and the
console's `app graph` / `app graph3d` still open them (from the home screen: `home_graph_new`). In
the flow the graph key (the shared bar's "graph", a ZOOM, the WINDOW's
graph, the 3D editors' graph) calls `home_graph_embed`: a new graph cell of the current functions
and window after the last cell (or, when the screens are bound to a cell, that cell written back)
and the home screen with it selected (`gsel`, so `m2d_home` scrolls it into view). Entering the full
graph screen in the flow (trace, calc) makes the cell first and binds the graph app to it
(`home_graph_enter` from `graph_enter` / `g3_enter`), as ENTER on a cell does; HOME writes the app's
state back (`gcell_unbind` in `home_enter`, which also selects the cell). The flow ends on any
screen that is not a graph screen (`home_graph_flow_check` from `ui_switch`), so graphs started
elsewhere (a stat plot, Python's `calc.plot`, the animation) are the plain graph app, as is the
console's `app graph` from another screen; with nothing to graph (Y= empty) the graph key shows the empty graph
screen, and a cell left without a function is removed when the home screen comes back. Graph cells
are FUNC mode: in the other graph modes the graph key shows the graph app.

**Module side.** `py_core_t` grew one member, `doc` (size 3; `PY_CORE_ABI` stays 1, a module refuses
an older core by the size): `doc()`, `alloc` / `free` / `used`, `reset`, `graph_draw` (a graph cell
drawn into the module's page), `graph_image` (its pixels, for the `.ipynb` PNG), `graph_height`,
`touched` (the module changed the document: the home screen redraws), `stash` / `unstash` (the
open document kept aside while the module reads another notebook to copy, rename or import it; a
plain copy of the document and the used part of the arena, since the strings come back at the same
addresses). The core calls the module through its console op (`py_nb_call`): `nb touched`,
`nb runcell ID`, `nb saveas [NAME]`, `nb savenow`, `nb renameto NAME`, `nb export`, `nb listview`;
a failure leaves its reason in `NB.msg`.

## Where the code lives

| Part | Where | Notes |
|---|---|---|
| The document (core) | `app/nbdoc.c`, `app/home.c`, `app/gcell.c`, `app/m2d_home.c` | see above |
| Screens (list, page, editing, menus, Markdown, touch) | `modules/python/nb_ui.c` | drawn by the module on the core's screen `scr_notebook` |
| The document, `.ipynb` reading and writing, running cells, saving, the console `nb`, USB | `modules/python/nb_doc.c` | |
| JSON writer and streaming parser | `modules/python/nb_json.c` | plain C (unit tested) |
| PNG writer, base64 | `app/png.c` (core; the module's image compiles the same file) | plain C (unit tested) |
| The QSPI notebook store | `modules/python/nb_store.c` | plain C (unit tested with power cuts) |
| The browser page's side: events, its commands, locks; when the notebook is saved | `modules/python/nb_sync.c` | |
| Graph cells: the plot image | `modules/python/nb_plot.c` | the CAS's compiled function, evaluated per column |
| Core side | `app/py_core.c` (`scr_notebook`, `nb_io`, `py_tick`: `nb tick` every 100 ms, `py_nb_flush`: `nb flush` from `app/session.c`'s power-off save, `app/app.c`'s low battery and `app/modx.c`'s unbinding), `app/usb_app.c` (NOTEBOOKS folder, imports), `core/vdrive.c` (subdirectory, long names), `core/fat.c` (long names on the card), `app/m2d_home.c` (`m2d_math_box`) | internal flash |

The Notebook screens (the list, the cell editor) and the saving, opening and Python of the home
screen's notebook are part of the
Python module (MicroPython, `docs/QSPI_CONTENT.md` 12.8): Python cells need its interpreter, the
console and the notebook share it, and the internal flash has little room left. The module is a
code-load module: its image (code, constants, data) and bss (the editor's buffer, the parser's string
buffer of 64 KB, two plot images and the graph cells' PNG pixels, the store's tables; the notebook
arena is the core's now) are copied into the 1 MB SDRAM code area when it binds. The core tier has
no Python: its home screen is still a notebook (calculations, text and graph cells, kept in the
session); APPS > Notebook says it is not part of the firmware.

The core gives the module one service entry, `py_core_t.nb_io(op, a, p, n)` (`app/py_core.h`,
appended: the table grows, `PY_CORE_ABI` stays 1): the store region (read, erase, write; refused
while a CAS extension runs in place from the QSPI), the current screen, soft-key labels, display
text <-> CAS text (the entry editor's tokens), textbook layout (`m2d_math_box`: measure and draw a
result in 2D), the card (create a long-named file, remove, list with long names), the cursor of the
entry line, and the USB drive (host present, read the dropped file, add a file to NOTEBOOKS,
report an import, ask for a refresh).

## Cells and execution

| Kind | What it is | Runs with |
|---|---|---|
| Text | Markdown: `#`/`##`/`###` headings, paragraphs, `**bold**`, `*italic*`, code spans, lists, `>` quotes, code fences, `$...$` and `$$...$$` math in textbook form (calculator syntax or the common LaTeX commands) | - |
| Math | one CAS line as typed on the home screen | the calculator's CAS session (`cas_eval_quiet`: the home screen's variables and settings, the CAS's `ans` unchanged; `Ans` and `Out[n]` are the notebook's results, below) |
| Python | code | the module's interpreter, one per open notebook (restarted when a notebook is opened): output, the value of the last expression (repr; LaTeX for a `calc.Expr`), errors with their traceback; `calc.plot()` in a cell draws the plot under it |
| Graph | `f(x)` or `f(x), xmin, xmax` | compiled by the CAS (`cas_compile_vars`), evaluated per column; the y range fitted (the extreme 2 % left out near a pole) |

One execution counter for all cells. ON stops a running cell (KeyboardInterrupt in Python, the
CAS's interrupt in math cells); run all stops at the first error. Long outputs are collapsed to
8 lines (a "more" line expands them); errors show their last line until expanded.

### Ans and Out[n]

The counter is the document's (`nb_t.counter`): the home screen (`app/home.c`) and the module
(`nb_run`) number every run with it, so the [n] by a result on the home screen, `count` in the
sync protocol and In [n] / Out[n] in the page are one number. A Python restart and opening a
notebook go on from the notebook's highest count (only clearing every output starts again at 1),
so `Out[n]` names one cell.

- `Out[n]` (or `out[n]`) is the result of the cell of count n; `Ans` (any case: `ans`, the ANS
  key's `(ans)`) the latest result: the cell of the highest count with a value (a math result or a
  Python cell's last expression; errors and cells without a value do not count).
- Math cells: the core rewrites the CAS text before it is evaluated (`nbd_refs`, `app/nbdoc.c`),
  on the home screen (and its live preview) and, through `NBIO_REFS`, for cells the module runs
  (from the page, Run all of the notebook menu): each reference becomes the stored result as CAS
  text in parentheses (`nbd_to_cas`: a result as the CAS printed it is used as it is, a displayed
  one goes through the editor's tokens). The rewrite is done before the cell's own result is
  cleared. When the CAS's `ans` still holds the result of the home screen's last calculation
  (`nbd_ans_mark` and `cas_ans_serial`), that result is `(ans)` itself, so no digits are lost. A
  missing n is an error: `CASError` / `ERR:UNDEFINED Out[7]: no such result`. Without any result
  in the notebook `Ans` is left to the CAS (its last value).
- Python cells: before each cell runs the module sets the globals `Ans` (None: no result) and `Out`,
  a read-only mapping (`[n]`, `in`, `get`, `keys`, `values`, `items`, `len`, iteration). A math
  result is a `calc.Expr` of its CAS text (`NBIO_VALUE_CAS`). A Python value is the object itself
  for cells run since the interpreter started (`Out` keeps count -> (cell id, value), checked
  against the cell); otherwise it is rebuilt from the stored text: a `calc.Expr` when the cell had
  LaTeX (the value was one), else a Python literal (no names but True, False, None, so nothing
  runs); anything else raises `KeyError: Out[n]: its value is not kept: run that cell again`.
- Outputs are stored with the notebook (`.ipynb`, the session blob), so references work after a
  reboot or reopening.

On the home screen a selected math cell's soft keys are input, result, Out[n], edit, ≈ (a Python
cell's: edit, result, Out[n]); SHIFT ENTER inserts the selected cell's result. The page shows
"insert text" / "insert Out[n]" on a hovered input or result (at the cursor of the cell being
edited, else in a new cell at the end) and recalls the inputs above with Alt+Up / Alt+Down.

## Saving

Changes (an edit, a run, a cell added, moved, deleted or changed, from the calculator or the browser
page) are kept in memory: `NB.dirty`, a star before the name and "not saved" in the header, a dot on
the page. The notebook is written to where it came from (the store or the card) only:

- at power-off: `platform_hibernate_save` (`app/session.c`, the Standby entry on hardware, also the
  low-battery shutdown, and the simulators' power-off) calls `py_nb_flush` before the session save,
  while the QSPI is still up;
- when the battery is low (below `HAL_BATT_WARN_V`, at each 30 s reading, `app/app.c`);
- when another notebook is opened, created or imported, or the notebook is closed (and when the
  list of notebooks or the cell editor is left: `nb tick` sees the screen change);
- before an export (`nb json`), before the USB drive lists the notebooks (`nb usb`) and before the
  modules are unbound (a module install);
- `NB_IDLE_SAVE_MS` (2 minutes) after the last change: the safety net for a crash, a watchdog reset or
  batteries pulled while on.

Not per keystroke or per command: the store is NOR flash, a ring of 4 KB sectors, and a page that sends
a cell's text as it is typed would otherwise write a new version every few hundred milliseconds. With
batteries fitted, unplugging USB is not a power loss (the cells take over within microseconds).
`nb flush` / `nb save` save at once; `nb idle MS` shortens the idle time (tests).

## Storage

### In the calculator: the notebook store

QSPI 0xA00000-0xDFFFFF (4 MB; the module store is 10 MB below it, the session log above it).
1024 sectors of 4 KB; a notebook's version is a chain of sectors, each a 64-byte header and 4032
bytes of the `.ipynb`:

| Offset | Field |
|---|---|
| 0 | magic `NBK1` |
| 4 | sequence number of the version (store-wide, increasing) |
| 8 | index of the sector in the version (u16), 10: flags (bit 0: the last sector) |
| 12 | bytes in this sector; 16: their CRC-32 |
| 20 | size of the whole file |
| 24 | the notebook's name (35 characters and a NUL) |
| 60 | CRC-32 of bytes 0-59 |

- **Mount** reads the 1024 headers (64 KB) and collects the versions; a version is complete when
  sectors 0..last are all there and the last is marked. No directory to keep consistent.
- **Write** (power-loss safe): for each sector erase, program the bytes, read them back against the
  CRC, program the header; the last sector's header goes last and commits the version. A cut
  anywhere leaves the version before as the notebook (an incomplete version's sectors are free).
- **Wear**: free sectors are taken in ring order from where the last write ended.
- **Versions**: the newest complete version of a name (compared without regard to case) is the
  notebook; older ones are kept until their sectors are needed (oldest first; those listed on the
  USB drive last) and give "previous version". Delete: the first word of each version's last
  sector is programmed to zero.
- **Limits**: 256 KB a notebook (about 63 sectors), 200 cells, names of 1-35 printable characters
  without `/ \ : * ? " < > |`; 4 MB in all, so about 15 notebooks of the largest size or hundreds
  of ordinary ones (a notebook of a few cells with a plot is 5-10 KB).
- A sector that holds a module extent's header (from when the module store was 14 MB) is never
  used.

### On the microSD card

`CALC/NOTEBOOKS/NAME.ipynb`, written through the module API's one-file slot. `core/fat.c` now
creates long names: a name that is not 8.3 gets VFAT entries before a short alias (`NAME~1.EXT`
for the first four, then two letters and a hash, as Windows does). A notebook opened from the card
is saved there; the list and the page menus copy a notebook between the card and the calculator.

### Example notebooks (built into the module)

Six worked notebooks ship with the firmware: Conservative field and potential, Diagonalize a matrix,
Fit and plot data, Fourier and Laplace pairs, Linear program, Verify an ODE solution (math, text and
graph cells for readers who know the math and want a recipe; help page `refresher` has the formula
sheets). The mechanism, chosen so that they need no card, no factory store image and no first-boot
writes (a wiped store, as in the tests, does not bring them back as real notebooks):

- **Source**: `firmware/notebooks/NAME.ipynb`, ordinary nbformat 4.5 files *as the calculator writes
  them*, with their outputs (math results and LaTeX, graph PNGs), so they also read well in Jupyter
  or on GitHub. `make nb-examples` (`tools/gen_nb_examples.py --refresh`) drops each file on the
  simulator's CALC drive, opens it, clears and runs every cell, saves it and copies it back: the
  outputs are exactly the calculator's, and a cell that ends in an error fails the step. The
  `metadata.calculator.firmware` field is set to the release version (no build hash), so the files
  only change when their cells do. Edit them anywhere (the calculator, the browser page, Jupyter:
  math cells keep `metadata.calculator.input`), then run `make nb-examples`.
- **Built in**: `modules/python/nb_examples.c` (generated, checked in) holds the files as C strings
  (about 50 KB in the Python module's image, which lives in the QSPI and is copied to SDRAM; no
  internal flash). `make test` runs `nb-examples-check`, which fails when the C file is not that of
  the notebooks; `nb-validate` checks the files with nbformat.
- **Listed**: `nb_list_all()` (`nb_doc.c`) is `nb_list()` plus the examples whose name is not a
  notebook of the store, with `where` = `NB_EXAMPLE` (2); the list (MENU Open, APPS > Notebook)
  shows them last, marked "example". The console keeps `nb list` as it was (the browser page parses
  it) and adds `nb examples` (`nbex=NAME SIZE [saved]`) and `nb open NAME example`.
- **Opened**: `nb_load(name, NB_EXAMPLE)` reads the string like a stored file (`read_doc` from
  memory) as a notebook *of the store* (`where` 0) that is not written yet; the first change (an
  edit, a run) makes it dirty and the usual saving writes it to the store under its name, after which
  the list shows the store's copy instead of the example. Deleting that copy shows the original
  again. Rename and delete are refused for an example ("Examples cannot be renamed / deleted"); F5
  more copies one to the card. The UI state `nb_items` counts the notebooks of the store and the
  card, `nb_examples` the examples listed.
- Tests: `tests/ui/125_notebook_examples.txt` (the list, open, change and save, the stored copy
  replacing the example and deleted again, refusals, copy to the card, every example runs without an
  error).

## The .ipynb mapping

| Calculator | nbformat 4.5 |
|---|---|
| notebook | `metadata`: `kernelspec` python3, `language_info` python, `calculator`: `firmware`, `format` 1 |
| every cell | an `id` (8 hex digits; an imported cell keeps its own when it is valid and unique) |
| Text cell | `markdown` cell (an imported `raw` cell stays raw; an unknown type becomes text) |
| Python cell | `code` cell, source verbatim |
| Math cell | `code` cell `import calc` / `calc.eval(r"CAS input")`, metadata `{"calculator": {"kind": "math", "input": "<as typed>"}}`; the calculator restores the line as typed from `input` |
| Graph cell | `code` cell `import calc` / `calc.plot(r"f(x)", xmin, xmax)`, metadata `kind` "graph", `input`, `xmin`, `xmax` |
| Graph cell of the home screen | the same, plus metadata `functions` (each as typed), `height`, `spec` (the cell's state, below), `ymin`, `ymax`; the output's PNG is the calculator's own plot of the cell (the graph app's drawing code, 288 x 132, indexed colour) |
| printed text | `stream` output, `name` stdout, `text` as lines |
| the value | `execute_result`: `text/plain` (repr, or the CAS's linear result) and `text/latex` (`$...$`) when there is LaTeX |
| an error | `error` output: `ename`, `evalue`, `traceback` (lines) |
| a plot | `display_data`: `image/png` (drawn on the calculator: 288 x 132, indexed colour), `text/plain`, metadata `{"calculator": {"plot": f, "xmin", "xmax"}}` so the calculator draws it again |
| execution count | `execution_count` (null before a run) |

Reading takes any nbformat 4 file (4.0-4.5): sources as one string or a list of lines, cells
without ids (an id is made), any metadata (ignored), stream text as string or lines, ANSI colour
codes in tracebacks (removed), images that are not the calculator's plots (left out; the cell says
so). The parser is streaming: strings of any length (a large base64 image) pass in pieces, nesting
is limited to 24 levels, a file to 2 MB (USB) or 4 MB (card); a text longer than 64 KB is cut.
Not JSON, not an object, nbformat 3 or older: refused with the reason ("bad JSON (line 6): expected
',' or '}'").

A short example (the plot's PNG shortened):

```json
{
 "cells": [
  {
   "cell_type": "code",
   "execution_count": 1,
   "id": "f53fe6c3",
   "metadata": {
    "calculator": {
     "input": "integrate(x^2,x)",
     "kind": "math"
    }
   },
   "outputs": [
    {
     "data": {
      "text/latex": "$\\frac{x^{3}}{3}$",
      "text/plain": [
       "x^3/3"
      ]
     },
     "execution_count": 1,
     "metadata": {},
     "output_type": "execute_result"
    }
   ],
   "source": [
    "import calc\n",
    "calc.eval(r\"integrate(x^2,x)\")"
   ]
  },
  {
   "cell_type": "code",
   "execution_count": 2,
   "id": "23920574",
   "metadata": {},
   "outputs": [
    {
     "name": "stdout",
     "output_type": "stream",
     "text": [
      "printed\n"
     ]
    },
    {
     "data": {
      "text/latex": "$\\left(x+1\\right)^{2}$",
      "text/plain": [
       "(x+1)^2"
      ]
     },
     "execution_count": 2,
     "metadata": {},
     "output_type": "execute_result"
    }
   ],
   "source": [
    "print('printed')\n",
    "from calc import x\n",
    "(x+1)**2"
   ]
  },
  {
   "cell_type": "markdown",
   "id": "305e2526",
   "metadata": {},
   "source": [
    "# Heading\n",
    "Some **bold** text, $x^2$ and a list:\n",
    "- one\n",
    "- two"
   ]
  },
  {
   "cell_type": "code",
   "execution_count": 5,
   "id": "686ce2f9",
   "metadata": {
    "calculator": {
     "input": "sin(x), -5, 5",
     "kind": "graph",
     "xmax": 5,
     "xmin": -5
    }
   },
   "outputs": [
    {
     "data": {
      "image/png": "iVBORw0KGgoAAAANSUhEUgAA...",
      "text/plain": "<plot of sin(x) for -5 ≤ x ≤ 5>"
     },
     "metadata": {
      "calculator": {
       "plot": "sin(x)",
       "xmax": 5,
       "xmin": -5
      }
     },
     "output_type": "display_data"
    }
   ],
   "source": [
    "import calc\n",
    "calc.plot(r\"sin(x)\", -5, 5)"
   ]
  }
 ],
 "metadata": {
  "calculator": {
   "firmware": "0.2.0",
   "format": 1
  },
  "kernelspec": {
   "display_name": "Python 3",
   "language": "python",
   "name": "python3"
  },
  "language_info": {
   "name": "python"
  }
 },
 "nbformat": 4,
 "nbformat_minor": 5
}
```

The PNG: 8-bit indexed colour, one zlib stream with a single deflate block of fixed Huffman codes;
the only matches searched are runs (distance 1) and the row above (distance = row bytes), which is
what a plot is made of: 2-3 KB for a plot, base64 in the JSON.

### Graph cells (the spec)

A graph cell of the home screen keeps its state as text in the cell field `NBF_GRAPH` (`spec` in
`nb get` / `nb doc` and in the `.ipynb` metadata), one item a line:

```
w XMIN XMAX YMIN YMAX XSCL YSCL      the window
o GRID AXES HEIGHT                   grid and axes (0/1), the height on the home screen in pixels
f ON COLOR STYLE EXPR                a function y(x) as typed (display text), up to six
```

A 3D graph cell (MENU Insert 3D graph) has the spec of the 3D graph's state (`g3_state_t`,
drawn by `g3_render` into the cell's image with the 3D graph's meshes and z-buffer, the 3D state
swapped for the moment like the Y= state):

```
3d AZIM ELEV ZOOM STYLE COLORMODE AXES BOX GRID TSTEPS
w XMIN XMAX YMIN YMAX ZMIN ZMAX
p UMIN UMAX VMIN VMAX TMIN TMAX
o 0 0 HEIGHT
c CONTOURS MAP LEVELS                     contours on (0/1), the contour map (0/1), levels (1..30)
f ON COLOR KIND EXPR[TAB EXPR TAB EXPR]   z(x,y); or x, y, z of (u, v) or of t
```

The `c` line is written only when it differs from `c 0 0 9` (older readers skip it; the page keeps
it as it is).

Arrows and a drag turn it, + − and a pinch zoom it, ENTER opens the 3D graph bound to it.

### Animated graph cells

A graph cell whose functions use t is animated (`app/gcell.c`, help "graph-cells"): a function that
is on names the identifier `t` (letters, digits and _ from a letter; not a call `t(`), in 3D in a
surface z(x,y) or a parametric surface (a space curve's t is its own variable). Both specs then end
with one more line, written by the calculator only for an animated cell and kept from what it read:

```
a TMIN TMAX SECS MODE                t's range, seconds per sweep, 0 loop / 1 bounce / 2 once
```

(defaults 0 2π 4 0; a new cell made from the graph app takes the settings of its animation of t,
`anim_cfg('t')`). The page uses the same rule (`usesT`, `parseSpec` in `sync.js`), so both sides
agree on which cells are animated; `buildSpec` keeps the old "a" line and writes the default one for
a cell that becomes animated in the page.

**On the calculator** an animated cell does not play by itself (battery). It is drawn at a value of
t: the start of its range, or for the one cell played or stepped (`A` in `gcell.c`, one at a time)
that cell's t, which the picture cache key includes (the frames reuse the cell's own cache slot).
2D: t is a slider parameter of the plotted Y= state, set after the scan of the functions and put
back after the plot; 3D: `g3_anim_set("t", ...)` around the render (`g3_anim_keep` saves and restores
the 3D app's own value). Selected: F5 "play" (`anim_cell_play`, `app/anim_core.c`) plays it through
the Animation module, whose timing (loop / bounce / once, speed, 2-minute time-out, any key pauses)
now has a target on the home screen: `ANIM_SCR_CELL` / `ANIM_T_CELL` (`anim_target` gives the cell's
settings, `anim_apply` sets t and draws the cell's block again, `home_anim_frame` with
`m2d_home_box`; the home screen's tick drives `anim_tick`). The caption ends with a badge ("anim",
"anim t=1.571", "▶ anim t=1.571" while playing); paused, LEFT / RIGHT step t by 1/50 of the range
(`gcell_anim_step`) until + or − makes the arrows pan. ENTER binds the graph app as for any cell and
sets its animation of t (`anim_cfg('t')`) to the cell's settings and its t slider (2D, selected and
stored in t) or 3D value to the cell's frame; HOME writes the settings back (`an_from_app`). Without
the module the cell shows its first frame with the note "still frame: animation needs the Animation
module", F5 says how to get it (the core tier: not in this firmware), and the arrows pan.
State keys: `gcell_anim`, `gcell_t`, `gcell_anim_cfg`; the module's `anim_kind` is `cell`.

**In the browser page** an animated cell plays at once (`animPlotView`, `animSurfaceView` in
`app.js`): its frames come from the calculator in one round trip (`nb frames ID 48 200 XMIN XMAX`,
3D `nb frames3d ID 24 32 32 K` per surface) and the page plays them with its own timer, between two
frames linearly interpolated (`lerpFrame`), with play / pause, a t slider (scrub: pauses) and a speed
menu (`Player`, `animBar`). Its settings are inputs in the range row (t from, to, seconds, mode:
`pushAnim` sends the spec); a pan or zoom samples the frames again for the new window and goes to
the calculator like a still cell's. Settings go both ways in the spec (`nb spec`, the `cell` event);
playing does not (each side plays on its own). A parametric 3D cell of t shows the calculator's
picture and "animated (t from … to …)"; so does a page that is not connected.

**Files.** The `.ipynb` keeps an animated cell as a graph cell: its spec (with the "a" line) in the
metadata, also `"animation": {"mode", "seconds", "tmax", "tmin", "variable": "t"}`, and the PNG of
one frame (the cell's current one). `NOTEBOOK.HTML` shows that frame and "animated (t from … to …)"
after the caption.

A 2D cell has `src` = "first function, xmin, xmax", `plot` = its CAS text, so the older reading of a graph cell
still works. Every graph cell has a spec: one made in the browser page (`nb insert AT graph`, `nb kind ID
graph`, `nb set` of a graph cell's text) gets one from its text (`gcell_set_spec(i, NULL)`: a function a
line, "f(x), xmin, xmax" sets the range), and so does a graph cell of an older notebook when it is read.
The core plots a cell with the graph app's own code (`graph_plot_view` in the view `gr_vp`,
`gr_plot.c`): its functions, window and style are swapped into the Y= state for the moment of the
plot, drawn offscreen (`gfx_target`) and kept in a cache of six images (SDRAM). The cache key is
everything the picture depends on: the spec, the size, the theme, the MODE settings (`g.cs`: the angle
unit, ...) and, when a function names anything but its variables (`a*x`, `f(x)`), the CAS's variables
(`cas_ans_serial`, which every calculation changes): a picture is never stale.

**On the home screen** a graph cell is a caption line, the functions as the cell plots them, each in
its curve's colour (dim when switched off), compact (`x^2` as x², `sqrt` as √, `*` as ·) and cut with
an ellipsis at the right edge ("y1=x²  y2=sin(x)", 3D "z1=..."), and the plot below it. A graph cell
without a function (made in the page and not filled yet, or every function switched off) draws no
plot: one dim line "graph: enter a function" (3D: "3D graph: enter z=f(x,y)"), 20 pixels high. Only a
cell the graph flow made and left without a function is removed when the home screen is entered
again (y= then HOME); a cell made elsewhere stays. NOTEBOOK.HTML shows the same caption in the same
colours above the plot.

## USB: the NOTEBOOKS folder

- At every generation of the CALC drive (`app/usb_app.c` `generate`) the core adds the
  subdirectory `NOTEBOOKS` (`vd_add_dir`, a long name) and asks the module (`nb usb` through its
  console op) for the stored notebooks; each comes back as `NBIO_USB_FILE` with its size and the
  QSPI offsets of its sectors, and the drive reads the file straight from them (`rd_nb`). Those
  versions are pinned (reclaimed last) while the computer may read them.
- While module code runs (a cell, a program; the USB stack is polled from it) the module cannot be
  asked: the folder is generated empty and the drive regenerated from the main loop.
- After the store changes the drive is regenerated (a media change: the computer may say the drive
  was not ejected properly), not while the list or the editor is shown, nor while the browser page
  follows (`nb_sync_defer_refresh`: after it).
- `NOTEBOOK.HTML` (`app/nbhtml.c`) is the home screen's notebook, unsaved too, built at every
  generation. `usb_app_tick` (`html_refresh`) generates the drive again when the notebook changed
  since (`nbd->gen`) and the calculator has been left alone for 10 s (no key or touch:
  `app_last_activity`; no module code running: `modx_busy`; the host has not read or written the
  drive for 2 s: `usbdisk_last_io`), at most every 30 s; at once when another notebook is loaded
  (its name or named state changed: opened, created, saved under a name); never while the browser
  page follows (`nb following`), whose serial link would drop with the device; after it.
- A `.ipynb` dropped on the drive (root or NOTEBOOKS, any write order) is found by `vd_poll` like a
  `.MOD`, and imported from the main loop (`nb import NAME`: the module reads it through
  `NBIO_USB_READ`, parses it whole, stores it under the file's name, reports with
  `NBIO_USB_RESULT`: a toast and `RESULT.TXT`), then the drive is regenerated. Not while the list or editor is
  open ("Leave Notebook to import it"). A notebook of the same name is replaced; its previous
  version is kept.
- `core/vdrive.c`: the staging clusters now come first on the volume and the generated files after
  them, so a computer that replaces a generated file (deleting it frees its clusters) still writes
  the new copy where the drive keeps it; the subdirectory's entries live in a buffer the host can
  write, and the host may extend its chain into the staging region.

## The browser page: one notebook, two views

The browser page (`web/notebook`, README "Browser notebook") shows and edits the notebook of the home
screen, named or not. The calculator is the single source of truth: the document is `NB`; the page
holds a copy kept up to date by events and changes it only through console commands, so both always
show the same notebook, and a cell runs once, here. Connecting does not change the calculator's
screen. A notebook the page opens or creates (`nb open`, `nb new`, `nb put`) is the home screen's
(the unsaved calculations are stored first as "Notebook N"), and one the calculator opens is the
page's (a `doc` event); a name the page types for the unsaved notebook saves it (`nb saveas`). The
home screen's own changes (`nbd_changed` -> `nb touched`, named or not) go out as `doc` events.

**Events.** Every change of the open notebook goes through `nb_note()` (`nb_sync.c`): the document
revision goes up by one and, while a page follows (`nb sync on`), a line goes out on the console:

```
# nb1 REV DIRTY TYPE [ARGS]
```

`# ` makes it a log line, which the console protocol keeps out of command replies (it may come at any
time, also between the lines of a reply); `nb1` is the protocol's version; `REV` the revision after the
change; `DIRTY` 1 while there are unsaved changes. Types:

| Type | Meaning | The page reads |
|---|---|---|
| `doc` | another notebook (opened, new, closed, imported, reverted) or every cell changed | `nb doc` |
| `cell ID` | that cell changed (source, kind, outputs, count) | `nb get ID` |
| `order` | cells added, deleted or moved | `nb ids` (and `nb get` for new ones) |
| `run ID` / `run -` | the cell runs / no cell runs | - |
| `meta` | name, counter, saved | `nb head` |
| `list` | the list of notebooks changed | `nb list` (when shown) |
| `lock ID calc\|page` / `unlock ID` | a cell is being edited on that side / no longer | - |
| `draft ID` | the calculator's edit of the cell, as typed so far | `nb draft` |

The page keeps the revision it is at. An event at or below it is already in what it read; the next one
is applied; a gap (a line lost, a restart) and it reads everything again (`nb doc`). `nb rev` (every
2 s, the keepalive) says `rev=`, `sync=` and `dirty=`: another revision than the page's with no event
pending, or `sync=0` (the calculator restarted, or stopped following after 15 s without a command from
the page), and it reads everything again. Each reconnection does the same.

**Reading.** `nb doc` prints `head={...}` and one `cell={...}` per cell, one JSON object a line:
head `name` ("" unsaved), `where`, `running` (cell id or ""), `note`, `open` (1 named), `rev`, `dirty`,
`counter`, `cells`; cell `id`, `kind` (text, math, python, graph), `lock` ("", calc, page), `src`,
`out`, `val`, `tex`, `ename`, `evalue`, `tb`, `plot`, `spec` (a home graph cell's), `xmin`, `xmax`,
`count`, `ran`: the fields of `nb_cell_t`. The page shows every cell the calculator has. It draws a
graph cell from its `spec`: every function that is on, in its window, in the calculator's colours and
line styles, each sampled with `nb sample ID N A B K` (K: the function's `f` line); a 3D cell of
surfaces z = f(x,y) from `nb sample3d ID NX NY K` (below), turned and zoomed in the page; anything it
cannot draw itself (a parametric 3D entry, a graph cell without a spec) as the calculator's picture,
`nb png ID` (`png=` lines of base64 PNG); a graph cell without a function as a line saying what to
type, no plot. `nb head`, `nb get ID`, `nb ids` (`ids=` in order), `nb draft` (`draft="..."`, `id=`)
read parts.

`nb frames ID NF N [XMIN XMAX]` (an animated 2D cell) evaluates every function that is on, in x and t
(compiled by the CAS with both), at N points of the cell's x range (or XMIN..XMAX) for NF values of t
evenly over its range, both ends included (2 <= NF <= 120, 2 <= N <= 400, NF x N x functions <=
48000), in one reply: `frames=NF N XMIN XMAX TMIN TMAX K...` (K: the functions' `f` lines), then `ys=`
lines of 16 values (`%.6g`, `n` where there is no real value), frame after frame, in each frame
function after function. `nb frames3d ID NF NX NY [K]` does the same for a 3D cell's surface K (as
`nb sample3d` chooses it): `grid=K NX NY XMIN XMAX YMIN YMAX ZMIN ZMAX`, `frames=NF TMIN TMAX`, then
`zs=` lines, frame after frame, each row by row (NF x NX x NY <= 48000).

`nb sample3d ID NX NY [K]` evaluates function K (its `f` line; default: the first z = f(x,y) that is
on) of a 3D cell on an NX x NY grid (2 to 64; the page asks for 40 x 40) over the cell's x and y range,
in one reply: `grid=K NX NY XMIN XMAX YMIN YMAX ZMIN ZMAX`, then `zs=` lines of 16 values (`%.6g`),
row by row (y outer, x inner), `n` where there is no real value. A parametric entry answers `ERR` (the
page shows the picture).

**Writing.** Cells are named by id (or, for scripts, by number). `nb set ID TEXT` (a text longer than a
console line: `py buf` pieces, then `nb setbuf ID`), `nb insert AT KIND` (0-based position; `id=`
back), `nb del ID`, `nb move ID -1|1`, `nb kind ID KIND`, `nb clear [ID]`, `nb run ID`, `nb runall`,
`nb restart`, `nb list`, `nb new [NAME]`, `nb open NAME [card]`, `nb rename A B [card]`,
`nb delete NAME [card]`, `nb close`. `nb spec ID TEXT` (or `py buf` pieces, then `nb specbuf ID`) sets a
graph cell's spec: the page edits a graph cell's functions (its text, one a line), its range (the x
and y, for 3D also z, fields under it) and its window by panning and zooming its plot (sent once the
gesture is over, not per pointer move); the core normalises the spec (`gcell_set_spec`) and a `cell`
event follows. A function keeps the colour and style of the one it replaces. `3d ...` makes it a 3D
cell: the page's "3D graph" kind (Shift-G, the kind menu) is a graph cell with a 3D spec; the
calculator draws it, ENTER opens the 3D graph on it. `nb sample ID N [XMIN XMAX]` evaluates the cell's plotted function
(`plot`, compiled by the CAS) at N points: `plot=`, `range=`, then `ys=` lines of 12 values (`n`: no
real value). `nb json` exports the notebook (saved first). `nb put begin NAME SIZE`, `nb put BASE64`
(lines of up to 300 characters), `nb put end` (or `abort`) import a `.ipynb`: the bytes go straight
into the store as a temporary version (`~upload`, no buffer of the file's size in memory), which the
streaming reader then parses; the notebook is saved under NAME (an older one of that name stays as the
previous version) and opened. 256 KB at most; 10 s without a piece and the upload is dropped.

**Locks.** A cell edited on one side is not edited on the other. The calculator's editor holds its cell
(`lock ID calc` when editing starts, `unlock` when it ends, `draft` events while it is typed, so the
the page shows the typing); the graph app (2D or 3D) bound to a graph cell (ENTER on it, the graph
flow) holds it the same way until the home screen is entered again; `nb set`, `nb setbuf`, `nb spec`,
`nb kind`, `nb del` and `nb lock` of that cell answer `ERR editing on the calculator`. A graph cell the
page edits does not open in the graph app ("This cell is being edited in the browser"). The page takes a cell with `nb lock ID` (one at a time) while its
textarea is in edit mode and gives it back with `nb unlock` (Esc, another cell, a run); the
calculator then refuses to edit, retype, change the kind of or delete it ("This cell is being edited in
the browser") and marks it "being edited in the browser". The page's lock ends by itself 30 s after
the page's last command (the page renews it every 10 s) and when the page stops following; another
notebook being opened ends both. When the page opens another notebook while the calculator edits a
cell, the calculator's edit is committed first.

**The calculator while the page drives it.** The home screen follows every change at once; a cell
the page edits cannot be edited there ("This cell is being edited in the browser"), and a cell the
home screen edits (in place, `nb calcedit ID`, or in the editor) is locked for the page. The
calculator may show any other app meanwhile: the notebook stays loaded and the page keeps working;
the drive's refreshes wait until the page stops following (they make the computer see the device
leave and return). The core's own commands (`touched`, `calcedit`, `following`, ...) do not count as
the page's keepalive.

**Running.** A run from either side runs the cell once, in the open notebook's interpreter (Python) and
the home screen's CAS session (Math), the outputs stored in the cell: the calculator draws them its way
(textbook math, the plot image), the page its own (KaTeX for `tex`, a canvas plot sampled with
`nb sample`). While a cell runs the console is not read: the page's commands wait (a long run blocks
its edits), the events go out from inside the run, and the page's Ctrl-C byte is the ON key's break.

## Tests

- `tests/ui/110_home_notebook.txt`: calculations as cells, recall, a text cell, edit in place, run
  all, re-run, delete, ENTRY recall, a power cycle and a reboot, New, CLEAR and its undo.
- `tests/ui/111_graph_cells.txt`: GRAPH, arrows and + − in place, pinch, drag, the bottom edge, the
  graph app on a cell and back, two cells scrolled, an empty Y=, a power cycle.
- `tests/ui/116_graph_flow.txt`: graphing from the home screen: the graph key then graph, a ZOOM,
  WINDOW, trace, table, SHIFT F5, the 3D graph (`app graph3d`); ENTER on a cell (the graph app's own
  keys stay in it); a graph started elsewhere is the plain app.
- `tests/ui/121_graph_key.txt`: the graph key (F1 "graph"), the editor's 2D/3D switch (F1, the header
  taps), 2D and 3D cells from it, ENTER on each, a bound editor stays of its kind, APPS without Graph
  and 3D graph (every tier).
- `tests/ui/122_graph_cell_anim.txt`: animated cells with the Animation module: the badge, no
  autoplay, stepping t, play / pause, the graph app on the cell (its animation of t), settings back,
  bounce, a 3D cell of t.
- `tests/ui/123_graph_cell_anim_nomod.txt`: an animated cell without the module: the still frame and
  its note, F5's message, the arrows pan, NOTEBOOK.HTML's "animated (t from … to …)" (every tier).
- `tests/ui/112_home_notebook_files.txt`: Save (the name prompt), the title bar, the list of
  notebooks, New, Open (the unsaved calculations stored first), a Python cell, Rename, a power cycle.
- `tests/ui/113_home_notebook_nopy.txt`: without the Python module (every tier).
- `tests/ui/114_notebook_html.txt`: NOTEBOOK.HTML on the drive (`tools/nbhtml_check.py` checks the copy),
  regenerated after a change and 10 s left alone, at once for a notebook saved under a name.
- `tests/ui/115_graph3d_cells.txt`: 3D graph cells (insert, turn, zoom, drag, pinch, the 3D graph and back).
- `tests/ui/120_graph_captions.txt`: the caption (colours, off functions, compact text, the ellipsis,
  3D), a cell without a function (one line, no plot; kept when the home screen is entered), the
  picture drawn again when a variable it uses changes, NOTEBOOK.HTML's caption.
- `web/notebook/test/graph.test.js`: graph cells in both views (a cell made a graph in the page,
  without a function, filled in the page; the calculator's pan and zoom followed, the page's pan sent
  once, the range typed, the graph app's lock and edits, the page's lock; a 3D graph made in the page
  and on the calculator, sampled with `nb sample3d`, turned in the page only; cells made while the page
  did not follow).
- `web/notebook/test/anim.test.js`: animated cells in both views: a cell of t made on the calculator
  plays in the page at once from one batch of frames, the t slider and the speed, the settings both
  ways (the page's range, the graph app's settings back), t-functions typed in a Graph and a 3D graph
  cell of the page animated on both sides, a 3D one turned while it plays. `spec.test.js`: the "a"
  line and the rule for t.

- `tests/ui/118_home_refs.txt`: `Ans` and `Out[n]` in calculations, a missing n, the [n] by each
  result (Textbook and Classic), a cell edited in place, the selection's soft keys and SHIFT ENTER,
  a reboot (every tier, no Python needed).
- `tests/ui/119_home_refs_python.txt`: `Ans` and `Out` in Python cells (calc.Expr, kept objects, a
  KeyError), the ANS key on a Python line, values rebuilt after a reboot, the numbering going on.
- `tests/unit/test_nbdoc.c`: the arena, cells, ids, the stash, the session blob (v2, the budget, v1),
  `nbd_refs` (the latest result, counts, errors skipped, whole words, strings, a missing n).
- `web/notebook/test/refs.test.js`: one counter in the page and on the calculator, `Ans` and `Out[n]`
  run from the page in Math and Python cells, the hover buttons, Alt+Up / Alt+Down.

- `tests/ui/100_notebook.txt`: the list of notebooks (new, rename, open, delete) and the cell editor
  from the home screen (several lines, the chars picker, cancel, run), errors, ON, a power cycle.
- `tests/ui/101_notebook_usb.txt`: the NOTEBOOKS folder, imports of `tests/notebooks/*.ipynb`
  (written by Jupyter, by hand, broken, nbformat 3) in macOS and Windows order, into the root and
  the folder, a replacement, an import waiting for the app to be left, the refresh after a change.
- `tests/ui/102_notebook_card.txt`: the card (copy, open, save there, a new one, a damaged file).
- `tests/ui/103_notebook_sync.txt`: the calculator's side of the browser page (following leaves the
  home screen; the unsaved notebook; the page's insertions, edits and runs on the home screen; a
  graph cell sampled and its picture; refusals of a page-locked cell, the calculator's lock;
  switching notebooks from either side; no drive refresh while followed) and when the notebook is
  saved (idle, export, switching, power-off).
- `web/notebook/test/*.test.js` (`make notebook-test`): the page and its notebook client against the
  simulator (README "Browser notebook").
- `tests/unit/test_notebook.c`: JSON writer and parser, PNG (inflated in the test), base64, the
  store (versions, reclaim, CRC, wear, a power cut at every device write of a save).
- `tests/unit/test_vdrive.c`, `tests/unit/test_fat.c`: the subdirectory and long names.
- `make nb-validate` (in `make test`): notebooks exported by the simulator are validated with the
  `nbformat` package and converted with `jupyter nbconvert --to markdown` (a virtualenv in
  `build/nbvenv`, made on first use; skipped when it cannot be made).

## Later (phase 3)

- A `calc` package for laptops (pip-installable, SymPy or a port of the CAS behind the same
  `calc.eval`, `calc.plot` and `calc.Expr` with `_repr_latex_`), so a calculator notebook re-runs
  in Jupyter cell for cell; the cells already call it.
- Reactive cells: a slider cell (`a` from 0 to 5) that re-runs the cells that use `a`, and graph
  cells with parameters drawn live.
- Editing comforts: the cursor moving across lines, selection and copy of several lines, undo of
  a whole cell, syntax colours, search; a notebook outline (the headings) to jump to.
- Images from Python (`calc.image`, matplotlib-like plots of data), kept and shown on the
  calculator; notebook attachments.
- Live text sync inside a cell (the page's edits arrive as whole texts after a pause; character-level
  merging would let both sides type in one cell).
