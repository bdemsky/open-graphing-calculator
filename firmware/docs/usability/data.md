# Usability review: the DATA area

Scope: statistics (list editor, STAT / CALC / TESTS menus and forms, results, regressions, distributions,
stat plots), the spreadsheet, CSV import/export and the microSD tools. Method: the simulator driven with the
UI-test console, screenshots read as images, six realistic tasks walked end to end. Findings are ranked by
impact. Status: **fixed** (in this change), **cross-area** (needs a shared framework file, listed for the
navigation/framework owner), or **open** (low impact, not done).

## Tasks walked

| Task | Before | After |
|---|---|---|
| Mean and standard deviation of 5 values | APPS > Statistics > 1, type values, F2 calc, 1, F5: 7 keys + data. The result shows `Sx=` and `sigmax=` with no hint which is which. | Same keys; each line says what it is (`sample std. dev.`, `population std. dev.`). |
| Linear regression with a scatter plot | CALC > LinReg, DOWN×3, RIGHT (RegEQ→Y1), F5; CLEAR CLEAR; plots, 1, RIGHT (on), UP UP ENTER (ZoomStat): about 15 keys over 3 screens, and Plot1 was blue like Y1, so the points vanished under the line. | CALC > LinReg, F5, F2 (plot): the equation goes to the first empty Y=, a scatter plot of the same lists is turned on, ZoomStat, graph. Plots default to red/green/magenta. |
| T-test | Rows `Inpt:` and `mu: ≠mu0 <mu0 >mu0`; the result screen does not say which alternative was tested; "Draw" only via ENTER on the result. | `Input:`, `Ha: mu≠mu0 mu<mu0 mu>mu0`; the result title shows `Ha: mu>5`; soft keys draw (F1) / back; F4 draw on the form. |
| Normal probability | DISTR > normalcdf( inserts `normalcdf(` with no hint of the argument order; the user must type `)`. | Menu shows `normalcdf(lo,hi[,mu,sd])`; inserts `normalcdf()` with the cursor inside. |
| Spreadsheet: formula, fill-down, link, plot | fill↓ prompt is empty (user must know the last row); fill/link give no feedback; plotting linked columns = HOME, F1, SHIFT F1, 1, RIGHT, set lists, ZoomStat (≈10 keys). | fill↓ proposes the last row of the neighbouring data (F3 ENTER); "filled B1 to B4", "column B feeds L2" confirmations; SHIFT F3 plot graphs the linked columns in one key. |
| Import a CSV from microSD | After import the editor opened on L1; the imported named lists (after L6) were off screen. "microSD: no card." with no advice; messages broke words mid-line ("fi / le system"). | Editor opens on the first imported list; card errors add the fix ("Insert a FAT32 card."); message text is word-wrapped. |

## Findings

### High impact

1. **Regression to graph took ~15 keys and hid the data** (fixed). The fit and the scatter plot lived in two
   different places, and the default Plot1 colour was Y1's blue, so the points were drawn under the fitted
   line (`stat_scatter_linreg.png` before). Fix: soft key **F2 plot** on regression and LinRegTTest results
   (`reg_plot`, `stat_plot_show` in `stat_plot.c`): equation into RegEQ or the first *empty* Y= (never
   overwrites), scatter plot of the fitted lists (reuses a matching plot, else a free one), ZoomStat, graph.
   Plot defaults are now red, green, magenta. Screenshot: `sim/screenshots/stat_regplot.png`.
2. **Result values were unexplained** (fixed). `Sx`, `sigmax`, `Σx²`, `Med`, `p`, `df`, `ME`, `Sxp`, `p^`...
   are calculator jargon; the only way to learn them was the manual. Fix: each result line shows its
   meaning in grey at the right (labels unchanged: they are also the VARS names); the bottom line says
   where the values can be reused (`values: VARS > Statistics`). Screenshots: `stat_1var_described.png`,
   `stat_ttest_result.png`.
3. **The alternative hypothesis was cryptic and not shown with the result** (fixed). Rows labelled `mu:`,
   `prop:`, `p1:`, `sigma1:`, `beta:` with options `≠mu0`, `≠s2`, `≠0`. Fix: every test uses one row
   `Ha:` with spelled-out options (`mu≠mu0`, `p1<p2`, `sd1>sd2`, `slope≠0`); the result's title bar shows the
   tested hypothesis with the value filled in (`Ha: mu>5`), or the confidence level for intervals.
4. **Destructive actions without confirmation** (fixed). ClrList from the list editor (soft key SHIFT
   clrlist, STAT menu 6) emptied the current list at once; Restore session replaced the whole calculator
   state with one ENTER on the file name. Fix: No/Yes menus with **No first** (`Clear L1 (3 values)?`,
   `Restore BACKUP.SES?`). Screenshot: `sdtools_restore_confirm.png`.
5. **Errors did not say what to do** (fixed). `ERR: lists have different lengths`, `ERR: n: missing`,
   `list 'L4' is empty`, `microSD: no card.`, `invalid name (8.3)`. Fix: `Xlist has 10 values, Ylist 2`;
   `n: missing, type a value`; `List: L4 is empty, enter data first`; `List: no list named 'NOPE'`;
   `File: use a name like DATA.CSV (8.3)`; card errors append the remedy (`Insert a FAT32 card.`,
   `Format it as FAT32.`, `Delete files to make room.`) via `sdbk_card_error()`.
6. **Missing glyph in every small/large number** (fixed). `font_medium` has no `ᴇ`, so results, shaded
   drawings and sheet cells showed `p=5.73303143758?-7` and `1.5?-7`. Fix: `stat_ascii_e()` draws `E`
   there (the home screen and list entry line use the big font, which has the glyph). Cross-area note
   below: the font should get the glyph.

### Medium impact

7. **Stat plot form showed every field for every type** (fixed). Ylist for a histogram, Freq for a scatter
   plot, Mark for a plain box plot; the type names are abbreviated (`MBox`, `NPrb`) and colours were the
   digits 1..8. Fix: fields a type does not use are hidden (new `SF_ANYOF` condition mask), the Type row's
   hint line spells the type out (`Histogram: bars Xscl wide`), colours are swatches (`SF_COLOR`), labels
   `Xlist`/`Ylist`/`Freq` match the CALC forms (were `XList`/`Freq.`). Screenshot: `stat_plot1.png`.
8. **Forms gave no guidance** (fixed). Empty number fields were blank; nothing said that LEFT/RIGHT change a
   choice or that a list name can come from VARS. Fix: `(enter a value)` placeholder; a grey hint line
   (`◀ ▶ change the choice`, `a list name, or VARS > Lists`, `frequency list, 1 = none`; forms can supply
   their own through `sform_t.hint`). `Inpt:` is now `Input:`.
9. **Draw was hidden** (fixed). On test forms only Calculate had a soft key (F5); Draw was a row at the
   bottom; on the result screen only ENTER drew, announced by a text line. Fix: F4 runs a form's second
   action (draw on tests, graph on plot settings), the result screen has **F1 draw** (and F2 plot), with
   the soft-key bar built from the result.
10. **DISTR menu gave no argument order and left the parenthesis open** (fixed). Items now read
    `normalcdf(lo,hi[,mu,sd])`, `binompdf(n,p[,x])`, ...; selection inserts `name()` with the cursor inside.
    DRAW items say what they shade (`ShadeNorm (normal area)`).
11. **List editor: an empty first screen with no hint** (fixed). Nothing on screen said what to type; the
    new-list column header was `-----`. Fix: grey hints on the entry line (`type a value, ENTER`,
    `ENTER: the whole list, or a formula`, `type a name for a new list`, `list is full`); header `+ new`.
    Screenshot: `stat_edit_hints.png`.
12. **CSV import hid what it imported** (fixed). Named lists from a header row are appended after L6, but the
    editor opened on L1. Fix: it opens on the first imported column. Screenshot: `stat_csv_imported.png`.
13. **Spreadsheet fill↓ required knowing the last row; no feedback** (fixed). The prompt now proposes the last
    row of the data in the column to the left (or right); fill, copy and link confirm on the entry line in
    blue (errors stay red).
14. **Spreadsheet to plot took ~10 keys on three screens** (fixed). New **SHIFT F3 plot**: the selected
    linked column against the nearest linked column to its left (scatter), or a histogram when it is the
    only one; an unlinked column says `link the column first: SHIFT link`. Screenshots: `sheet_linked.png`,
    `sheet_plot.png`.
15. **STAT menu used TI names only** (fixed): `SortA (sort ascending)`, `ClrList (empty lists)`,
    `SetUpEditor (L1..L6)`; `CSV on SD card` is now `CSV on microSD`, matching the CSV menu title and the
    microSD tools.
16. **Message boxes split words** (fixed in my area). The box breaks every 26 characters, so card messages read
    `fi / le system`. `sdbk_wrap()` pads lines so words move whole; used by every microSD/CSV message.
    Cross-area: the box itself should word-wrap.

### Low impact (open)

17. Regression/test titles keep TI spellings (`2-SampTTest`, `1-PropZInt`, `Med-Med`); a subtitle could
    explain them, but the menus are dense and the help pages cover them.
18. The list editor shows only 3 columns of 104 px; with short values 4 would fit. Not changed: the column
    width matches the 13-character cells and touch targets.
19. Result screen: the RegEQ line is cut at the right edge for cubic/quartic fits; the full equation is in
    VARS > Statistics > RegEQ and Y=.
20. The spreadsheet's `$` and `:` soft keys take two of five primary keys for characters also reachable by
    typing; `$` is not on the keyboard, so they stay.
21. Form rows are 18 px and choice options ~30 px wide: below a comfortable 40 px touch target. A larger
    layout would push the T-test forms to scroll; left as is.

## Cross-area recommendations (framework files, not changed)

- **app/app.c (message box)**: word-wrap instead of a hard break at 26 code points, and let the text use 5
  lines when "press any key" is dropped (the soft key already says ok). `sdbk_wrap()` is a local workaround.
- **tools/gen_font.py**: add `ᴇ` to `font_medium` / `font_small` (fallback glyph), so every screen using the
  medium font can show the MODE exponent mark; `stat_ascii_e()` could then go.
- **app/softkeys.c / menu.c**: a standard confirmation helper (`ui_confirm(title, yes_label, fn)`) with No
  first, so ClrList, Restore, Clear sheet and future destructive actions look the same (each builds its own
  2-item menu today).
- **app/menu.c**: menus whose items are full sentences ("Yes, replace everything") still show the generic
  `▲ page ▼ page help back` bar; a two-item confirmation would read better with `no` / `yes` soft keys.
- **app/xform.c / form.c**: the hint line and `(enter a value)` placeholder added to the STAT forms would help
  the other apps' forms too (solver, finance).
- **keys/layout.py**: none needed.

## Changed files

`app/stat_form.c` (result descriptions, notes, draw/plot soft keys, hints, placeholders, colour swatches,
conditional masks, F4), `app/stat_menu.c` (Ha: rows, length errors, reg plot, ClrList confirmation, menu
labels, DISTR labels), `app/stat_plot.c` (hidden fields, type hints, colours, `stat_plot_show`),
`app/stat_edit.c` (hints, `+ new`, open on a column), `app/stat_csv.c` (open on imported list, errors),
`app/stat_draw.c` (E), `app/sheet_ui.c` (fill proposal, confirmations, SHIFT plot, E), `app/sdbk.c/.h`
(restore confirmation, card errors, wrapping), `app/stat.h`; help `stat.txt`, `sheet.txt`, `softkeys.txt`;
tests 13, 40-45, 65, 82, 92, 93 updated, new `46_stat_usability.txt`, `83_sheet_usability.txt`.
