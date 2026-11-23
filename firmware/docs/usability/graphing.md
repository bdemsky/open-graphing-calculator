# Usability review: graphing

Scope: Y= editor, WINDOW, ZOOM, GRAPH, TRACE, CALC, TABLE / TBLSET, sliders, the graph modes (FUNC, PAR,
POL, SEQ, IMPL), stat-plot overlays on the graph, and 3D graphing.

Method: the simulator (`make sim`), driven by console scripts like the UI tests (`tests/ui/*.txt`), with a
screenshot after each step. The tasks were walked through twice: as a new student (no manual, guesses from
the screen) and as an engineer (knows what to compute, wants few key presses):

1. graph y = x² − 2 and find its roots;
2. graph a parametric circle;
3. compare two functions and find their intersection;
4. make a table, look up one value;
5. graph and trace a 3D surface.

Impact: **high** = a task fails or a feature cannot be found without the manual; **medium** = extra key
presses, an unclear message or a guess; **low** = polish. Status: *fixed* (in this change), *cross-area*
(needs a framework file owned by another reviewer; see the end), *open*.

## Findings, ranked by impact

### High

**H1. The graph modes cannot be found from Y=.** *Fixed.* The mode header (FUNC PAR POL SEQ IMPL) was
scrolled out of view: it appeared only after {▲} on the first entry. A student asked to "graph a
parametric circle" never saw PAR; the status bar shows `Y= PAR` only once you are already there. Seen in
the task: three attempts went to the wrong line because {▲} from the second line of a PAR entry does not
reach the header.
Fix: the header is always in view on arrival (the six FUNC rows are 30 px instead of 34 px so header and
rows fit); PAR / SEQ (12 lines) scroll as before. Tapping the header works as before.

**H2. Typing right after choosing a mode was silently lost.** *Fixed.* After {enter} on a mode the focus
stayed on the header, and every key except arrows and {enter} was ignored, so `cos(x)` typed next simply
vanished (and the next {▼} put the following expression in the wrong line).
Fix: choosing a mode (key, tap or menu) puts the focus on the first entry; any text key or {⌫} on the
header moves to the first entry and is applied there.

**H3. CALC prompts did not say how to answer.** *Fixed.* `Left bound?` gives no clue that the arrows,
a typed number or a tap answer it, or that {enter} confirms; `First curve?` needs {▲} {▼}, which nothing
says. Students pressed {enter} at once (bound = cursor at x ≈ 0), and then got `No zero between the
bounds` with no idea why.
Fix: the right end of the prompt band shows the answer keys in grey: `◀ ▶ or type, enter`, `▲ ▼ curve,
enter`, `type a number, enter` (value). The prompt text itself (console state `calc_prompt`) is unchanged.

**H4. Failed CALC searches did not say what to do.** *Fixed.* `No zero between the bounds`, `No value
between the bounds`, `No intersection in the window`, `No inflection point between the bounds`.
Fix: each names the next step: `...: widen them`, `...: move them`, `...: zoom out`.

### Medium

**M1. Intersect asked for both curves even when only two exist.** *Fixed.* With Y1 and Y2 on (the
common case) the user had to press {enter} twice for a choice that does not exist (and with one curve,
choosing it twice gave an endless `Choose a different second curve`).
Fix: with exactly two traceable functions intersect goes straight to `Guess?`, and the band names both
(`Guess?  Y1 and Y2`); with three or more the curve choice is as before; with one, a message says that
intersect needs two functions.

**M2. Y= entry options were hidden.** *Fixed.* Switching a function off, its colour and its line style
are reached only with {◀} past the start of the expression (to an unlabelled `=`, swatch, style mark).
{menu} listed only the soft keys.
Fix: {menu} on Y= opens `Y= OPTIONS`: `Y1 on/off`, `Y1 next colour`, `Y1 next line style`, `clear Y1`,
the five graph modes with a one-line description each (current one marked ◀), Help. The edit bar (undo,
redo, copy, paste, hist) stays on the soft keys while it is open, as on every screen with an editor. The
soft-key bar itself is the shared graph bar (copied at run time, so changes to it carry over).

**M3. An empty graph and an empty TRACE gave no feedback.** *Fixed.* GRAPH with nothing switched on drew
bare axes; {F4} then did nothing at all. The 3D graph already said `z= (F1): enter z=f(x,y) or a surface`.
Fix: the empty graph says `y= (F1): enter a function` (`a curve x(t), y(t)`, `a curve r(θ)`, `a
sequence`, `a relation` in the other modes), not when a stat plot or geometry overlay is drawn; {F4}
with nothing traceable says `Nothing to trace: switch on an entry in y=`.

**M4. WINDOW / TBLSET / slider errors said only `ERR: invalid range`.** *Fixed.* Which field, and what
range is valid, was left to guess (e.g. nMax < nMin in SEQ, Tstep = 0, ΔTbl = 0, grid 2 in 3D).
Fix: the message names the problem: `Xmin must be less than Xmax`, `Tstep must be more than 0`,
`nMax must not be less than nMin`, `PlotStep must be 1 or more`, `ΔTbl must not be 0`, `min must be
less than max`, `step must be 0 to max-min`; 3D window: `zmin must be less than zmax`, `grid must be 4 to
64`, `tsteps must be 8 to 1000`.

**M5. WINDOW field names are jargon.** *Fixed.* `Xscl`, `Tstep`, `PlotStart`, `PlotStep`, `nMin`, `ΔTbl`
are not self-explanatory to a student.
Fix: the free line at the bottom of WINDOW, TBLSET and the slider settings explains the selected field
(`distance between x tick marks (0: none)`, `step of t: smaller is smoother but slower`, `first n: the
initial values belong to it`, `draw every k-th term`, `step between rows (not 0; negative counts down)`,
`step of + − and of dragging (0: any value)`); an error replaces it.

**M6. TABLE could not jump to a value.** *Fixed.* To see f(3.7) the user had to go to TBLSET, change
TblStart, come back, and later change it back. The graph's trace already accepts a typed x.
Fix: typing a number on TABLE (digits, `.`, (−), π; menus insert too) shows `go to X=...` in the readout
line; {enter} makes it the first row (expressions such as 2π work; SEQ rounds and keeps n ≥ nMin); {clear}
cancels. TblStart is not changed. A grey `type a number: go there` hint shows in the readout line when
there is room.

**M7. The 3D trace cursor disappeared on shaded surfaces.** *Fixed.* A thin cross in the text colour with
a 3×3 dot in the surface's own colour: on a blue surface it was barely visible.
Fix: longer arms and a ring with a 1 px background-colour outline, so it shows on any surface colour and
in both themes.

**M8. Y= touch targets are small.** *Fixed in part.* The `=` toggle is 10 px wide and the swatch 11 px
(≈ 2 mm on the 3.2" panel). Both now accept a few pixels of slack (swatch 12 px, `=` from 4 px left of
it to 3 px into the expression). A real fix (wider swatch column) would shift the editor and needs a
layout change across Y= and the 3D z= editor: *open*.

**M9. `format` (SHIFT F3) opens the whole settings screen.** *Cross-area.* The graph-relevant rows
(GRID, AXES, GRAPH) are below the fold, after angle, result, digits, ... A student looking for "grid"
does not find it. Recommendation: open settings scrolled to the graph rows, or a small graph-format
screen (grid, axes, coordinates on/off, labels).

**M10. ZOOM item names are TI jargon.** *Cross-area* (`app/menu.c`). `ZStandard`, `ZDecimal`, `ZSquare`,
`ZTrig`, `ZoomFit` mean nothing to a new student. Recommendation: keep the numbers and order, add the
effect: `Standard  x,y −10..10`, `Decimal  0.1 per pixel`, `Square  round circles`, `Trig  x −2π..2π`,
`Fit y to the curves`; consider `ZoomStat` and a box zoom in the same menu.

### Low

**L1.** The trace / free cursor starts at the centre pixel, x = 0.03134796 in the standard window, not
x = 0 (pixel columns are (Xmax − Xmin)/319 apart). TI does the same; ZDecimal gives round steps. *Open.*

**L2.** After a CALC result the point label (`Intersection (−1.30278, −0.30278)`) repeats the result band
and is clipped at the left edge for points near it. *Open.*

**L3.** A PAR entry with only X1T filled shows `ERR` in every TABLE cell; the readout explains it
(`both x(t) and y(t) are needed`). An empty cell would read better. *Open.*

**L4.** Typing `x^2+y^2=9` into a SEQ entry (mode not switched) gives `undefined variable in function`
without naming the variable or suggesting IMPL. *Open* (message comes from the CAS compiler).

**L5.** WINDOW, TBLSET: {clear} empties the field (TI-like), while on Y=, GRAPH and TABLE it leaves the
screen. Consistent within forms everywhere, so kept.

**L6.** The table drew column separators for unused columns. *Fixed*: separators only next to used columns.

## What worked well

- One shared soft-key bar on every graph screen: y= window zoom trace graph / plots tblset format calc
  table; the current screen is highlighted.
- Compile errors are shown at the top of the graph in red with the entry name.
- CALC results go to `ans`, are labelled on the graph, and value then {▲} {▼} evaluates the other
  curves at the same x.
- Long-press snapping to zeros / extrema / intersections; tap on the coordinates for the table there.
- Sliders appear automatically for free letters; their settings screen says how to get back.
- 3D: the empty screen says what to do; trace band with full coordinates; the z= editor has labelled
  SHIFT soft keys for the entry kind, on/off and colour.

## Cross-area recommendations (framework files, not changed here)

1. `softkeys.c` / settings: `format` should land on the graph rows of settings (or a graph-format
   screen) — M9.
2. `menu.c`: descriptive ZOOM labels; add ZoomStat (it exists for stat plots) and a box zoom — M10.
3. `menu.c` / `softkeys.c`: the generated MENU context menu lists `help F4` and `Help` on the settings
   screen (seen with SHIFT F3 then MENU); one entry is enough.
4. The 2D Y= editor and the 3D z= editor solve the same problem differently: 3D has labelled SHIFT soft
   keys (type, on/off, color), 2D now has a MENU. A shared "entry list" pattern (both, in both) would make
   them consistent; it needs the shared graph bar (`softkeys_graph`) to leave room on Y=.

## Verification

- New UI test `tests/ui/36_graph_usability.txt` covers every fix above (header, typing on it, the Y=
  MENU, empty graph / TRACE messages, CALC hints and messages, intersect with one / two / three curves,
  WINDOW and TBLSET hints and errors, TABLE go-to).
- Updated tests: `07_touch`, `33_graph_touch_styles` (Y= rows moved down by the header), `30_graph_modes`,
  `31_graph_calc` (no {▼} after choosing a mode; intersect with two curves skips the choice), `12_app_fixes`,
  `35_graph3d` (specific range messages).
- Screenshots: `sim/screenshots/yeq.png`, `yeq_menu.png`, `graph_empty.png`, `calc_hint.png`,
  `window_hint.png`, `table_goto.png`, `g3d_trace.png`.
