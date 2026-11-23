# Usability review: the apps

Scope: matrix editor and MATRIX menu, equation solver, polynomial roots, linear systems, finance, Base-n,
the UNITS menu and conversions, uncertainty entry (±), and the geometry app. Method: the simulator
(`make sim`) driven by console scripts like `tests/ui/*.txt`, with a screenshot of every step. Each of
these tasks was done from a fresh start:

1. define a 3×3 matrix and invert it;
2. solve x^3-2x=1;
3. solve a system of 3 linear equations;
4. compute a loan payment;
5. convert 0xFF to binary and do a bitwise AND;
6. convert 60 mph to m/s;
7. enter 9.81±0.02 and propagate it;
8. in geometry, build a triangle and its circumcircle, and measure an angle.

The findings below are ranked by impact: high means a task fails, loses data or needs knowledge the
screen does not give; medium means friction or unclear wording; low means polish. Status
**fixed** means fixed in this change, with the files named. **Cross-area** means the fix belongs to
the framework (soft keys, menus, form.c, layout) and is listed as a recommendation.

## High impact

| # | Screen | Finding | Status |
|---|---|---|---|
| H1 | Matrix editor, linear system (size line) | A digit typed on the size line was applied at once. Typing `12` rows first resized the matrix to **1 row**, which deleted rows 2 and up, and then grew it to 12 rows of zeros. A user who typed 12 to make room lost their data. | **Fixed** (`xform.c`): a number that could still get a second digit within the maximum stays pending, shown as `1_`, until the next digit or key (ENTER, an arrow, a tap). CLEAR drops it. A number that cannot grow (3 when the maximum is 20, any number in the 6×7 linear grid) is still applied at once. |
| H2 | Equation solver | Solving `x^3-2x=1`: after typing the equation, F5 on the `eq:` line answered "Highlight the unknown variable", even though `x` was the only variable. | **Fixed** (`solver.c`): with one variable, F5 solves from any line. With several, the message says what to do: "Select the unknown with ▲▼, then F5". |
| H3 | Equation solver | The roots of `x^3-2x=1` were joined into one line, `roots:-1,-0.6180339887,1.618033`, cut off at the screen edge, so the third root could not be read. | **Fixed**: each root has its own line (`root1=`, `root2=`, ...). F5 on a root line puts that root into the unknown, so you can pick another solution without guessing a start value. |
| H4 | Solver, finance footers | The hints were cut off at the screen edge: "F5 (solve): solve for the highlighted variabl", "Coefficients from the highest power; roots be". The footer also showed the same hint on every line. | **Fixed**: each line has its own hint (an optional `xform_ops_t.hint`). Examples: "F5: solve for x (its value is the guess)" and "PV: present value (received +, paid -); F5 solves". Footer text that does not fit in the 7 px font is drawn in the 6 px font (`x_footer_text`). |
| H5 | Base-n | Bitwise AND on a result: after `FF` ENTER, BITS > and gave `and 0f`, which failed with "ERR: syntax error". The home screen inserts Ans when an entry starts with an operator; Base-n did not, and Base-n had no Ans of its own. | **Fixed** (`basen.c`): an entry that starts with `+ - * / ^`, or with `and`/`or` from BITS, starts with `Ans`. In Base-n, Ans is the app's own last result. |
| H6 | Base-n | With HEX input there was no hint on how to type the digits A to F (ALPHA CAS, math, vars, x, sin, cos). The empty screen did not say what to do. | **Fixed**: grey hints below the results: "type a number or expression, ENTER" or "start with + - and or ... to use the result", and in HEX "a-f: alpha + CAS math vars x sin cos". |
| H7 | Matrix | Inverting a matrix took about 12 keys: leave the editor, open MATRIX > MATH, scroll to `inv(` (item 15, too far down for a number key), then MATRIX > NAMES > [A]. Nothing on screen pointed to the x⁻¹ key. | **Fixed** (`matrix.c`): F5 `→home` in the editor goes home with `[A]` typed at the cursor, so F5, x⁻¹, ENTER inverts the matrix. MATRIX MATH now starts with det(, inv( ("inverse, also [A]⁻¹"), transpose(, rref(, identity(, dim(. |
| H8 | UNITS menu | "Convert 60 mph to m/s" needs `convert(`. It was the 19th of 23 items, after all 17 categories, labelled `convert( q,_unit)`. | **Fixed** (`units_menu.c`): `convert( value_unit,_unit)` is item 2 (SHIFT vars 2). The other commands say what they return ("usimplify( to SI units", "uvalue( number part", ...). |

## Medium impact

| # | Screen | Finding | Status |
|---|---|---|---|
| M1 | Linear system | `[A|b] 3×4` is the size of the augmented matrix. A user with 3 equations and 3 unknowns has to know to enter 4 columns, and no cell was labelled as x1, x2, x3 or b. | **Fixed**: grey column names (`x1 x2 x3 = b`) above the cells, and the size in words, "3 equations, 3 unknowns" (`xgrid_t.colname`, `xgrid_t.note`). |
| M2 | Matrix editor, linear system | The editor opens on the size line, but nothing said that digits set the size. | **Fixed**: the bottom line explains the size line: "size: type the rows, ▶ columns, ▼ cells". While a number is pending it shows "ENTER or an arrow sets it, CLEAR cancels". |
| M3 | MATRIX MATH | Labels mixed TI names with command names (`T  transpose(`, `Fill( fill(v,M)`, `*row+( mRowAdd(`) and gave no argument order for the row operations. | **Fixed**: the command name comes first, then its arguments and the TI name (`mRowAdd( k,M,i,j)  *row+(`, `fill( v,M)  Fill(`). |
| M4 | TVM | The sign convention (received +, paid -) was only in the help page, but it decides whether N and I% can be solved. The error "ERR: no solution" did not say why. | **Fixed** (`finance.c`): per-field hints that state the sign, and "ERR: no solution: check the signs (paid < 0)". F5 on P/Y, C/Y or END/BEGIN shows "Select N, I%, PV, PMT or FV, then F5". |
| M5 | Amortization | The payment table had no header, only the footer note "table: int prn bal", and its columns were not aligned. | **Fixed**: a header line (`pmt:interest princ.   balance`) and 9-character columns. |
| M6 | Finance (amort, cash flows, interest, days) | F5 is labelled "solve" on every finance screen but did nothing on four of them. | **Fixed**: on those screens F5 says "results update as you enter values". The soft key bar is shared by the five screens, so the label stays. |
| M7 | Solver | The footer word `left-rt` (left minus right) is HP jargon. "ERR: no sign change found" did not say what to do. | **Fixed**: "x solved: 3 roots, left-right=0" and "ERR: no root found: try another guess or bounds". |
| M8 | Polynomial roots | The coefficient labels `x^2:1`, `x:  0`, `1:  -1` did not show the polynomial being entered. | **Fixed**: the footer shows the polynomial (`p(x)=x^3-2x-1`). On the degree line it shows "degree 1..10, then the coefficients from x^n down". |
| M9 | Base-n | The rows HEX/DEC/OCT/BIN could not be tapped. The screen had no touch handler, unlike every other app screen. | **Fixed**: a tap on a row makes that base the input base. |
| M10 | Geometry | The prompt bar of multi-step tools did not show progress or what had been picked ("Angle: vertex"). "point, vertex, point" did not say which point goes where. | **Fixed** (`geo_ui.c`): "Angle: vertex (step 2/3, picked B)". The angle and angle-bisector steps are "point on 1st side, vertex, point on 2nd side". The polygon tool says "next vertex, or A again to close". |
| M11 | Uncertainty | `nominal(`, `uncert(` and `ucov(` were only in the CATALOG. The UNITS menu, which types `±`, did not list them. | **Fixed**: they are at the end of the UNITS menu, with short descriptions. |
| M12 | Base-n errors | "ERR: digit not in BIN" did not say how to change the input base. | **Fixed**: "ERR: digit not in BIN (▲▼: input base)". |
| M13 | All forms | When a field has its value, the first typed key replaces the whole value, and that includes the solver's `eq:` line. Pressing UP to the equation and typing a guess wiped the equation. | Not changed. This is the rule of every form (`xform`, `form.c`). A per-row "append, don't replace" flag would make the rule inconsistent. The new per-row hint on `eq:` makes the row's purpose clear. |
| M14 | Soft keys | The active choice is not marked on soft keys that set a state: Base-n hex/dec/oct/bin, the geometry tool. The body shows it (highlighted row, prompt bar), but the soft key bar does not. | **Cross-area** (softkeys.c): see R1. |

## Low impact

| # | Screen | Finding | Status |
|---|---|---|---|
| L1 | MATRIX > EDIT | Undefined matrices [B]..[J] are listed without a marker, so they look the same as defined ones. NAMES shows the size of defined ones only. | Not changed; the size works as the marker. |
| L2 | Polynomial roots | Root rows are named `x1=`, `x2=`, ... while the equation solver uses `root1=` (its variable may be called x). | Not changed: in POLY ROOTS the variable has no name, and existing users and tests know `x1=`. |
| L3 | Geometry | Info messages ("Saved") and errors ("No intersection") have the same red colour in the prompt bar. | Not changed. |
| L4 | Linear system result | The status line ("unique solution") is drawn as the selected row. | Not changed: the scrolling list needs a selection. |
| L5 | Base-n | BITS has 13 items, so word sizes and signed/unsigned need letters or scrolling. | Not changed. The status bar shows the word size, and the order follows the operators. |
| L6 | Units | The UNITS submenus describe unusual units (`nmi nautical mile`) but not common ones (`m`, `mph`). | Not changed. |

## Cross-area recommendations (framework, not changed here)

- **R1. Soft keys that show state** (`softkeys.c`, `ui.h`): add an optional `bool (*on)(void)` to `softkey_t`,
  so a key without a screen can still be highlighted. Base-n would mark the input base, geometry the tool's
  group (move / points / draw / constr / measure), and VIEW toggles could appear as soft keys.
- **R2. Per-screen soft keys in shared bars**: the finance bar's F5 "solve" appears on screens where it does
  nothing. The bar is shared by the five screens. `softkeys_fn` could return a bar without F5 there, but
  `softkeys.c` should make that the norm. A blank label is better than an action that does nothing.
- **R3. CLEAR on an empty field leaves the app** (all forms): one CLEAR too many, after clearing a field,
  exits the solver or finance screen. The values are kept, so nothing is lost, but users are surprised. A
  framework rule could be: CLEAR on an empty field restores the field's previous value, and CLEAR again
  leaves.
- **R4. `form.c` hints**: the WINDOW and other `form.c` screens could use the same per-row hint callback as
  `xform_ops_t.hint` (added here). The two form widgets would then explain their fields the same way.
- **R5. A conversion shortcut**: a "convert to…" action on the home screen (for example SHIFT on a unit in
  the UNITS submenu, or a result action) would turn `60_mph` into `convert(60_mph,_m/s)` without typing
  `convert(`. This needs `menu.c` (shift-select) or the result-actions owner.
- **R6. Matrix x⁻¹ discoverability on the home screen**: `[A]⁻¹` works but is not mentioned where users
  look. The CAS-usability owner could show "x⁻¹ inverts a matrix" in the result actions for a matrix result.

## Walkthrough notes

- Task 1 (3×3 inverse): APPS > Matrix > EDIT > [A], type the 9 cells (ENTER moves right and wraps),
  F5 (→home), SHIFT ÷ (x⁻¹), ENTER. That is 4 keys after the entries; it was about 12 before.
- Task 2 (x^3-2x=1): APPS > Solver > 1, type the equation, ENTER, F5: all three roots, one per line.
  F5 on root 3 makes it the value of x.
- Task 3 (3 equations): Linear system, ▲ to the size line, `3` ▶ `4`. The header shows x1 x2 x3 = b and
  "3 equations, 3 unknowns"; type 12 numbers, F5.
- Task 4 (loan payment): TVM, N=360, I%=6.5, PV=250000, then PMT and F5 gives -1580.170059. The footer
  explains each field and its sign while you move.
- Task 5 (0xFF and 0x0F): Base-n, F1 (hex), `ff` (ALPHA ALPHA cos cos, as the hint says) ENTER, then BITS
  > and, which types `Ans and `, then `0f` ENTER.
- Task 6 (60 mph to m/s): SHIFT vars 2 types `convert(`, then `60`, SHIFT vars 7 3 (mph), `,`,
  SHIFT vars 7 1 (m/s), ENTER gives `26.8224_m/s`.
- Task 7 (9.81±0.02): `9.81` SHIFT vars 1 `0.02` ENTER, store it with →, then compute with it. The
  propagation works, no friction found. `nominal(` and `uncert(` are now in the same menu.
- Task 8 (triangle, circumcircle, angle): draw > Polygon (4) and three taps, then the first again. Then
  draw > Circle (3 points) (7) on the three vertices, then measure > Angle (2): the bar names each step.

Screenshots: `sim/screenshots/solver_roots.png`, `linsys_grid.png`, `basen_ans.png`, `matrix_size.png`
(new), and `solver_eq.png`, `solver_poly.png`, `finance_amort.png`, `units_menu.png`, `matrix_edit.png`
(updated). The new behaviour is tested in `tests/ui/56_apps_usability.txt`.
