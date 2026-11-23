# Usability review: navigation and the framework

Scope: the keyboard layers (SHIFT/ALPHA), HOME/APPS/MENU, the soft-key bar and the edit bar, context menus,
APPS, CATALOG, history search, SETTINGS, help, the status bar, message boxes, first power-on and touch.
Method: the native simulator driven by console scripts, as in `tests/ui/*.txt`, with the screenshots read
as a first-time user would see them. Tasks: switch on and find graphing; find help; undo, copy and paste;
find "determinant"; switch to degrees, to RPN and back; recover from an error.

Conventions that came out of this review are in `app/EXTENDING.md`, under "Navigation conventions".
The test is `tests/ui/15_navigation.txt` and the screenshots are `sim/screenshots/nav_*.png`.

## Findings, ranked by impact

| # | Impact | Finding | Status |
|---|---|---|---|
| 1 | high | **SETTINGS dropped choices.** {◀} {▶} only moved the orange focus box. Leaving with `done`, {home} or {clear} kept the old value, with no warning (RIGHT then done on ANGLE stayed RADIAN). A tap applied the choice at once, so keys and touch did not behave alike. | fixed: arrows apply at once |
| 2 | high | **The context menu contradicted its own bar.** On a screen with an entry line, MENU listed `1:y= F1`, but F1 in that menu is *undo*, because the bar is the edit bar. Undo and copy were not in the list at all. | fixed: the edit actions come first with the keys that work now; screen soft keys follow without key names; `Help  SHIFT MENU` is last |
| 3 | high | **Edit actions gave no feedback.** Copy, cut, and paste with an empty clipboard, and undo or redo with nothing to do, changed nothing on screen and said nothing. | fixed: status-bar toast (`ui_toast`), e.g. "copied", "nothing to undo", "clipboard is empty: copy first" |
| 4 | high | **Double CLEAR wiped the home history for good.** The first CLEAR clears the line; the second, on an empty line, deletes every history item with no confirmation and no undo. | fixed: the history is snapshotted (`input_state_guard`), so MENU F1 brings it back. A toast says so. |
| 5 | high | **SHIFT CLEAR ("clear all") worked only on home**, cleared only the line and history, and did not ask first (user decision: make it a real "clear all"). | fixed: a global action on every screen, with a confirmation (No first) that clears the CAS variables, the entry line and the history. RPN keeps "clear the stack". |
| 6 | high | **Features without a command could not be found.** The CATALOG searched commands and some apps only: "undo", "rpn", "degree" and "copy" gave `(empty)`. | fixed: the search also lists matching help pages (`? RPN mode`); ENTER opens them. The angle page is now titled "Angle mode: degrees or radians". |
| 7 | medium | **First power-on is a blank screen.** Nothing says where help is. | partly fixed: on a calculator without a saved session, a toast reads "new? SHIFT then MENU: help" until the first key. Recommendation for home.c below. |
| 8 | medium | **SHIFT soft keys were invisible** until SHIFT was pressed (home: table, solve, factor, expand, graph; graph: plots, tblset, format, calc, table). | fixed: an amber corner on each label that has a SHIFT function |
| 9 | medium | **Filter menus did not say you can type.** The empty CATALOG search and history search showed nothing, and a failed search showed `(empty)`. | fixed: a grey "type to search" placeholder and "No match: backspace erases a letter." |
| 10 | medium | **Message boxes**: text was hard-wrapped in the middle of words, and every title was red, so "EXPORTED" looked like an error. | fixed: word wrap; red only for FAIL, ERR, NOT, INVALID titles |
| 11 | medium | **format (SHIFT F3 on graph screens)** opened SETTINGS at the top, with GRID and AXES below the fold. | fixed: `mode_open_at("GRID")` |
| 12 | medium | **ZOOM item names** (ZStandard, ZDecimal ...) are jargon, and ZoomStat was only in Stat plots. | fixed: each item says what it does ("ZSquare  round circles"), and ZoomStat is added |
| 13 | medium | **The generated settings MENU** listed both "help F4" and "Help". | fixed: a `help` soft key is not repeated |
| 14 | medium | **Form range errors** said only "invalid range". | fixed in the graphing review (form.c returns the rule) |
| 15 | low | Edit-bar label `hist` was unclear. | renamed `history` |
| 16 | low | Soft-key slots differ between list screens: the history search has help on SHIFT F5 (F4 is copy); help has `exit` on F4 and `back` (previous page) on F5. | documented as a convention (F4 help where room, F5 leaves); left as is |
| 17 | low | On the RPN screen, MENU lists "undo F1" (line undo) and the screen's own "undo" (stack undo): two different undos with the same name. | recommendation for rpn.c: label the stack one `undo stk` or merge them |
| 18 | low | CATALOG: pressing ALPHA by habit turns off the automatic ALPHA lock, and function keys then type `sin(` into the query. The `a-LK` badge shows it, but this is easy to miss. | not changed |
| 19 | low | The status bar shows angle, result and format, plus SHIFT/ALPHA/OVR and the battery. It has no indicator for COMPLEX=REAL or for RPN entry (the RPN screen's title says RPN). | not changed |

### Walkthrough results after the fixes

- **Find graphing**: {apps} opens APPS, with `1:Graph` first; on the home screen, F1 `y=` then F5 `graph` also works.
- **Find help**: SHIFT MENU works on every screen. The first-run toast and the last MENU item ("Help  SHIFT MENU") point to it.
- **Undo, copy and paste**: MENU shows undo, redo, copy, paste and history on the bar and as the first list items. Every action confirms itself in the status bar.
- **Find "determinant"**: in the CATALOG, `det` finds `det(`; `determinant` matches the summary.
- **Degrees and RPN**: in SETTINGS (SHIFT HOME), RIGHT on ANGLE takes effect at once. ENTRY is further down. Typing `degree` or `rpn` in the CATALOG opens the page that says where the setting is.
- **Recover from an error**: CAS errors stay in the history (`ERR:DOMAIN division by zero`), and the line is ready for new input. Form errors keep the old value and state the rule.

## Framework requests from the other reviews

| Request | From | Done |
|---|---|---|
| SETTINGS at the graph rows from `format` | graphing | `mode_open_at("GRID")` |
| ZOOM items named by what they do; ZoomStat | graphing | yes; box zoom left to the graph screen |
| No duplicate "help F4" + "Help" in the settings MENU | graphing | yes |
| One convention for entry options (2D Y= MENU vs 3D z= SHIFT keys) | graphing | MENU (`softkeys_t.menu`), in EXTENDING.md; z= should follow |
| A soft key "on" state (base-n input base, geometry tool) | apps | `softkey_t.on` |
| A soft key that does not apply must be blank or say why | apps | convention (blank or `ui_toast`) |
| A gentler CLEAR in forms | apps | form.c: CLEAR empties the field, a second CLEAR brings its value back, and it never leaves; the same convention is stated for xform |
| One hint-line mechanism for forms | apps, data | the existing `field_t.hint` / `xform_ops_t.hint` / `sform_t.hint` all draw a grey line under the form when there is no error; written down as one convention |
| Message box: word wrap, up to 5 lines | data | yes; `sdbk_wrap()` is removed |
| `ᴇ` in font_medium and font_small | data | tools/gen_font.py synthesizes a small capital E from each font's own E and x; `stat_ascii_e()` is removed |
| A shared No-first confirmation with yes/no soft keys | data | `ui_confirm()` (F1 yes, F5 no, "No" first); ClrList, Restore and Clear sheet can move to it |

## Recommendations needing approval or another owner

Keycaps (printed legends; not changed):

1. **SHIFT ⌫ = undo** (legend `undo` in place of `ins`). Undo is the most important hidden function. Insert/overwrite is rarely used and could move to SETTINGS or to SHIFT ALPHA. This would put undo on the keyboard itself.
2. **ALPHA letters are printed in upper case** (A..Z) but one ALPHA press types lower case. Print them in lower case, or make one press type the printed case.
3. Keep `clear all` as the SHIFT CLEAR legend (user decision); its meaning is now "clear variables + history, after asking".

Other owners:

- **home.c** (CAS-usability agent): with an empty history, draw a grey hint in the body, such as "type an expression, enter · menu: options · shift menu: help". The global `clearall` action now takes SHIFT CLEAR before home sees it, so the `mod == MOD_SHIFT` branch of `case KEY_CLEAR` in `home_key()` is dead and can be removed (one line, optional). No other home.c change is needed for the clear-all behaviour.
- **rpn.c**: see finding 17.
- **g3d_edit.c**: the z= editor puts entry options (type, on/off, colour) on SHIFT soft keys, while 2D Y= now has them in its MENU. Per the convention, also offer them in a MENU menu (`softkeys_t.menu`).
- **Box zoom** (graph reviewer): drag a rectangle on the graph to zoom. It fits the touch screen and would be the natural ninth ZOOM item.
