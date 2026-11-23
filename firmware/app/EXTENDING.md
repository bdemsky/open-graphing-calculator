# Extending the calculator application

How to add a feature (screen, app, menu, VARS entries, MODE row, saved state, help pages, tests) so that
several features can be developed in parallel with few merge conflicts. The rule of thumb: **put the feature
in new files** and add **one line per extension point in `app/registry.c`**, in the section reserved for
your feature (the sections are separated by blank lines and comments so parallel edits do not touch the
same lines). Everything below is portable C11 (no malloc, no OS); see `app/ui.h` for the shared API.

| Extension point | Type / function | Where it is registered |
|---|---|---|
| Screen | `screen_t` (`app/ui.h`) | nowhere: open it with `ui_switch(&scr_x)` |
| App in the APPS menu | `app_def_t` (`app/registry.h`) | `app_registry[]` in `app/registry.c` |
| Soft keys F1..F5 (labels at the bottom of the screen) | `softkeys_t` (`app/ui.h`) | `screen_t.softkeys` / `softkeys_fn`, `menu_def_t.softkeys` |
| Context menu (MENU key) | `softkeys_t.menu` | generated from the soft keys when NULL |
| What a key types / does (legends, SHIFT, ALPHA) | `keys/layout.py` (one row per key) | generated: `core/gen/keymap_gen.c` `key_info[]`; actions in `app/keys.c` |
| Menu (static or dynamic) | `menu_def_t` + `menu_open()` (`app/ui.h`) | nowhere |
| VARS category | `vars_provider_t` (`app/registry.h`) | `vars_registry[]` |
| MODE row | `mode_row_t` (`app/registry.h`) | `mode_registry[]` |
| Saved state (power-off / reboot) | `persist_def_t` (`app/session.h`) | `persist_registry[]` |
| Help pages | `help/*.txt` | new file; `screen_t.help`, `app_def_t.help`, `menu_*.help` point to topic ids |
| CATALOG entries | `@cmd` pages in `help/*.txt` | generated from the help files |
| Home replacement (RPN) | `ui_set_home(&scr_rpn)` | call it from your MODE row's `set()` |
| RPN stack level drawing (e.g. 2D math) | `rpn_set_renderer()` (`app/rpn.h`: `measure` + `draw` per level; `measure` -1 = built-in text for that level) | `app_init` installs `m2d_rpn_renderer` (`app/m2d_home.c`); NULL = built-in text |
| Evaluation like the home screen | `app_eval(cas_text, out, n)` (`app/numfmt.c`) | — |
| Number display with MODE format | `numfmt_double()`, `numfmt_apply()` | — |
| Large tables / cold code in flash bank 2 | `FLASH2_RODATA`, `FLASH2_TEXT` (`core/sections.h`) | or a pattern in the linker script |
| Console commands for tests | `console_cmd` rows | `app/app_console.c` (or a new table, see below) |
| Drawing into the GRAPH screen (stat plots) | `graph_overlay_t` (`app/graph_overlay.h`) | `graph_overlay_registry[]` |
| UI tests | `tests/ui/NN_name.txt` | picked up automatically |
| Colours that follow the theme (dark mode) | `TH_*` tokens (`app/theme.h`) | nothing: mapped in `core/gfx.c` |
| Large-font mode | `UI_FONT`, `UI_CW`, `UI_CH` (`app/theme.h`) | nothing |
| 2D (textbook) math in your screen | `m2d_parse()` + `bx_*()` (`app/m2d_box.h`) | — |
| Undo, clipboard, type-to-filter menus | `ed_change_hook`, `clip_put()`, `menu_def_t.filter_key` / `.query` (`app/input.h`) | nothing: see "Input comforts" |
| Time-boxed helper evaluation | `cas_set_poll()` (`cas/include/cas.h`); restore `app_cas_poll` afterwards | — |
| A feature as a code module in the QSPI (runs in place, internal flash keeps only hooks) | `modx_api_t` / `modx_desc_t` / `modx_ops_t` (`app/modx.h`); no static RW data, no pointers in constant tables, no library calls | `modules/NAME/*.c` + an image rule in the Makefile (as `animation`), `known[]` / `modx_host[]` in `app/modx.c`; see docs/QSPI_CONTENT.md 12 |
| A full-screen code module with its own screen, input and saved progress (games) | `modx_api_t` entries `softkey`, `leave`, `held_key`, `key_char`, `save_get` / `save_put` (blob `modx.save`), `file_open` / `file_read` / `dir_list`; events `MODX_EV_ENTER` / `KEY` / `TOUCH` | a host screen like `app/mquest_core.c` (Math Quest, docs/MATHQUEST.md) |
| Long blocking work (> ~1 s without returning to the main loop) | `hal_watchdog_kick()` in its loop (`include/hal.h`) | — (see "Watchdog") |
| Feedback, confirmation, undo of non-editor state | `ui_toast()`, `ui_confirm()` (`app/ui.h`), `input_state_guard()` (`app/input.h`) | — (see "Navigation conventions") |

Build configuration is Kconfig (`firmware/Kconfig`, `make menuconfig`, tiers in `configs/`; C code
sees `CONFIG_x` from the generated `autoconf.h`). A feature that is optional gets a Kconfig option
with help text; UI tests that only make sense in some tiers start with `requirecfg OPTION` or guard
lines with `ifcfg [!]OPTION COMMAND`. Large read-only content that does not have to be in internal
flash belongs in a QSPI module (`core/modstore.h`, `docs/QSPI_CONTENT.md`).

Run `make test` (unit + UI + help examples, every tier), `make BUILD=build/stub CAS=stub sim test`, `make fw`,
`make web-test` before you hand in.

## Code documentation standard

The source is meant to be read by students, so new code is documented as it is written:

- **File header.** Every `.c`/`.h` file starts with a block comment saying what the screen, app or module
  does; how the user drives it (keys, soft keys, touch); how it plugs into the framework (registry entries,
  `persist_def_t` blobs, soft keys, VARS/MODE rows, CAS calls); where its state lives (globals, arena
  carve-outs, saved session); and the design decisions a reader would otherwise have to reverse-engineer.
- **Function comments.** Every non-trivial function, `static` or not, gets a comment above it: purpose,
  parameters, return value, side effects (screen switches, CAS evaluation, persistence, redraws, graph
  invalidation), the context it runs in if it is not the main loop (e.g. a key ISR), who owns the buffers it
  takes or returns, and the invariants it relies on or keeps.
- **Inside long or tricky functions.** Explain the steps, UI state machines, layout math (pixel geometry),
  numeric methods (root finding, quadrature, regression fits, distributions, marching squares, TVM, ...: name
  the method and cite the formula or source), edge cases and why special cases exist.
- **Data structures and tables.** Say what each field means and how the table is used.
- **No noise.** Do not restate the code, do not leave TODO spam, keep the existing terse style
  (`/* ... */`, sentences without filler), and describe only behaviour you have verified.

A documentation-only pass must not change code. Check it before committing with
`python3 firmware/tools/check_comment_only.py` from the repository root (it compares every changed `.c`/`.h`
file with `HEAD` after stripping comments and whitespace; it must report 0 files with non-comment changes),
then run the usual tests. Bugs found while documenting are reported, not fixed in the same change.

## Screens

```c
/* app/stat.c */
#include "ui.h"
static void stat_enter(void) { if (ui_returning) return; /* reset per-visit state */ }
static void stat_draw(void)  { gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE); ... }
static void stat_key(int key, int mod) { ... ; stat_draw(); }
static editor_t *stat_editor(void) { return &cell_editor; }   /* menus insert here; or NULL */
static void stat_touch(const touch_event_t *ev) { ... }        /* or NULL */
const screen_t scr_stat = {"stat", "STAT", stat_enter, stat_draw, stat_key, NULL, stat_editor, stat_touch,
                           "stat"};
```

Fields: `name` (for `state screen` / `expect screen`), `title` (status bar), `enter`, `draw`, `key`, `tick`
(called every main-loop pass with the time in ms), `editor` (where MATH/CAS/CATALOG/VARS insert),
`touch`, `help` (topic id for SHIFT HELP; NULL means the topic named like `name`), `softkeys` (the soft-key
bar, see below) and `softkeys_fn` (a function returning the bar, for screens whose soft keys depend on their
state). Positional initializers may stop early: the build uses `-Wno-missing-field-initializers`, so new
trailing fields never break existing definitions.

A screen that scrolls must also scroll by touch: handle `TEV_DRAG` in `touch` with the helpers of
`app/touch_scroll.c` (`ui_drag_list` for a plain list with a selection, `ui_drag_scroll` / `ui_drag_focus`
for forms and grids; declared in `app/ui.h`) rather than accumulating `ev->dy` yourself, so every screen
scrolls the same way (the content follows the finger, one row per row height, the focus pulled into view).
Act on `TEV_TAP` only, never on `TEV_DOWN` / `TEV_UP`, so a drag cannot also act as a tap. The menus,
`xform` / `xgrid` (app/xform.h) and the forms in app/form.c already do this.

Swipes: a fast, straight one-finger drag (at least `TOUCH_SWIPE_PX` = 48 px, released within
`TOUCH_SWIPE_MS` = 500 ms, at `TOUCH_SWIPE_SPEED` = 200 px/s or more, main axis at least twice the
other) also yields `TEV_SWIPE` after its `TEV_UP`; `touch_swipe_dir(ev)` is `SWIPE_LEFT/RIGHT/UP/DOWN`,
`ev->scale` the speed. The drags of that gesture were already delivered, so a screen that scrolls by
dragging keeps working; a screen that uses swipes simply ignores those drags (it acts at the SWIPE). A
swipe is never a tap. Use swipes only where they are natural and keep a key for the same action (the help
viewer: right = back, left = next page). Code modules get `MODX_EV_SWIPE` (`MODX_SWIPE(dir, x0, y0)`,
app/modx.h); the console `touch swipe X Y L|R|U|D [DIST]` injects one for tests.

- Draw only into the body (`BODY_Y` .. `UI_BOTTOM`, height `BODY_H`); the status bar belongs to
  `ui_draw_status()`, the soft-key bar (`SOFT_Y`, `SOFT_H`) to `app/softkeys.c`. `gfx_reset_clip()` clips to
  the body, so a screen cannot draw over the bar by accident.
- `ui_returning` is true while a menu, message or help page returns to your screen: keep your state then.
- Keys reach `key(key, mod)` after the global actions (`app/keys.c`). `mod` is `MOD_NONE`, `MOD_SHIFT` or
  `MOD_ALPHA` (keys without an ALPHA letter never get `MOD_ALPHA`). Key codes are semantic (`KEY_SIN`,
  `KEY_X`, `KEY_ASSIGN`, `KEY_F1`..`KEY_F5`, ...), generated from `keys/layout.py`. Text keys: `ui_key_text()`
  / `ui_key_suffix()`; editors: `ui_editor_key()` (arrows, ⌫ = backspace, SHIFT ⌫ = insert mode),
  `ui_draw_editor_line()`.
- CLEAR from a screen usually does `ui_switch(ui_home())` (never `&scr_home` directly: RPN mode can
  replace the home screen with `ui_set_home()`). HOME does that globally.
- Persistent screen-local statics: on hardware a power-on is a reset; in the simulators `app_resume()`
  re-runs `app_init()` without clearing your statics (so does a session restore from the microSD card on
  every platform), so restore them from your `persist_def_t.load` / defaults rather than relying on
  zero-initialisation (or reset them in `enter`). `session_load()` calls every registered `reset()` before
  loading, so a `reset()` gives your state its defaults whenever the snapshot has no blob for it.
- Add `xxx_state(kv, user)` key=value pairs to `app_state_foreach()` in `app/app.c` (one line) if your
  tests need them, or prefer the generic `screen`, `menu`, `menu_item`, `help_topic` keys.

## Soft keys (F1..F5) and the MENU key

The top row of the keyboard is five soft keys. Each screen declares what they do; `app/softkeys.c` draws
their labels in the bar at the bottom of the display (the SHIFT row while SHIFT is pending), dispatches the
keys and taps on the labels, and builds the MENU key's context menu.

```c
static void st_calc(void) { stat_calc_open(); }
static const softkeys_t stat_soft = {
    /* F1..F5: label, action, or a screen to switch to (highlighted while shown) */
    {{"edit", NULL, &scr_stat_edit}, {"calc", st_calc}, {NULL}, {NULL}, {"draw"}},
    /* SHIFT F1..F5 */
    {{"sortA", stat_sorta}},
    NULL, /* MENU: a menu_def_t of your own, or NULL: a menu listing the soft keys above + Help */
};
const screen_t scr_stat = {"stat", "STAT", stat_enter, stat_draw, stat_key, NULL, stat_editor, stat_touch,
                           "stat", &stat_soft};
```

- A soft key with a label and neither `fn` nor `screen` (`{"draw"}` above) is delivered to the screen's
  `key()` as `KEY_F5` (`KEY_IS_SOFT(k)`, `KEY_SOFT_INDEX(k)`), with `MOD_SHIFT` for the SHIFT row.
- `NULL` labels are blank and do nothing. Labels are short: the bar has 64 px per key in `font_small`
  (≤ 9 characters).
- Soft keys that depend on the screen's state: set `softkeys_fn` (e.g. RPN's interactive stack). The bar is
  redrawn after every key and touch event when its labels change.
- Menus (`menu_def_t.softkeys`) can have their own bar; the default is page up, page down, help, back.
- The graph screens share `softkeys_graph` (y= window zoom trace graph / plots tblset format calc table):
  a new graph-like screen (3D graphing, geometry) can reuse it or declare its own; a spreadsheet would declare
  its own (e.g. edit, fill, sort, graph).
- The help page `softkeys` (`help/softkeys.txt`) lists every screen's soft keys: add yours.

### Wording conventions

- Soft-key labels are lowercase words or short abbreviations (`amort`, `constr`, `categ`); list screens
  (menus, CATALOG, history search, help) page with F1 ▲ page / F2 ▼ page and go back with F5 `back`.
- Message boxes (`ui_message`): an UPPERCASE title naming the action (`EXPORTED`, `SAVE FAILED`) and a
  sentence that starts with a capital letter (or a path / `microSD`) and ends with a period. microSD
  errors read `microSD: <fs_strerror text>.`.
- Status lines inside a screen (sheet, list editor, forms) are short lowercase phrases without a period.
- Errors of a CAS result are `ERR:NAME detail` (NAME = SYNTAX, DOMAIN, MEMORY, ...: home, RPN, sheet
  cells); errors of a form or editor field are `ERR: detail`.
- Every screen has a help page that {SHIFT} {MENU} opens (`screen_t.help`); `tests/ui/98_help_coverage`
  checks them.
- Message titles that contain `FAIL`, `ERR`, `NOT ` or `INVALID` are drawn red, all others blue, so
  name failures `... FAILED`. A message that reports an error says how to fix it ("No stat plot with
  data is on: switch one on in Stat plots."). Form `validate()` returns the rule ("Xmin must be less than
  Xmax"), not just "invalid".

### Navigation conventions (docs/usability/navigation.md)

The framework applies most of these by itself; follow them in new screens so every screen works alike.

- **Fixed keys.** {SHIFT} {MENU} is help on every screen (never take it). {HOME} goes home. {CLEAR}
  clears what is being typed, then leaves the menu, form or screen one level. {SHIFT} {CLEAR} is the
  global "clear all" (asks, then clears the CAS variables, the home entry line and history:
  `mem_clear_all()`); only the RPN screen keeps its own meaning (clear the stack). {MENU} is the context
  menu; on screens with an editor its soft keys are the edit bar.
- **Fixed soft-key slots on list and modal screens** (menus, CATALOG, history search, help, settings,
  messages, results): F1/F2 page, F4 `help` when there is room, F5 leaves (`back`, `done`, `ok`).
  App screens use all five for their own functions; do not put "help" or "back" there.
- **SHIFT row.** A SHIFT soft key is a variant or a less frequent action; put a variant in the column of
  the key it varies (`copy` / SHIFT `copy res`). The bar marks keys that have a SHIFT function with an
  amber corner automatically.
- **Context menu.** Leave `softkeys_t.menu` NULL unless the screen has options that are not soft keys:
  the generated menu lists the edit actions (screens with an editor), every soft key and Help. Options
  of the highlighted item (on/off, colour, style of an entry) go into the screen's own MENU menu
  (`softkeys_t.menu`, as Y= does), not into soft keys that change with the selection.
- **Feedback.** An action that changes nothing visible (copy, undo with nothing to undo, a setting
  stored elsewhere) reports it with `ui_toast("copied")` (status bar, until the next key); use
  `ui_message` only for what must be read. Destructive actions ask first with
  `ui_confirm("Delete all lists?", fn)` (No is the default, so a repeated key never confirms) or keep
  an undo step (`input_state_guard()` snapshots a persist blob; the home history uses it for CLEAR).
- **Settings take effect at once** ({◀} {▶} or a tap; ENTER is not needed). Open SETTINGS at the rows
  that matter with `mode_open_at("GRID")`.
- **Searchable.** Name help pages so that a user's word finds them: the CATALOG search also lists help
  pages whose title or id contains the query (`? Angle mode: degrees or radians`). Put the user's
  words (degrees, undo, copy) in the title.
- **Empty lists** say why: filter menus show "type to search" and "No match: ..." automatically.
- **Soft keys that pick a mode** (base-n input base, geometry tool) show the current choice highlighted:
  set `softkey_t.on` to a function returning true for the active one (`{"hex", sk_hex, NULL, is_hex}`).
- **A soft key that does not apply** in the current state is blank (`NULL` label, via `softkeys_fn`) or
  says why with `ui_toast("select a cell first")`; it never silently does nothing.
- **Forms** (form.c `field_t.hint`, xform `xform_ops_t.hint`, stat forms `sform_t.hint`): the line under
  the form shows the error if there is one, else a grey hint for the selected field (what it means, its
  unit or range). {clear} empties the field and a second {clear} brings its value back (form.c); a
  form screen is left with its soft keys or {home}, so a stray {clear} never loses a value or the screen.
- **Confirmations** use `ui_confirm()`: "No" is the first item, F1 `yes`, F5 `no`, {clear} no.
  Feature-specific yes/no prompts (ClrList, Restore session, Clear sheet) should move to it.

## Apps (APPS menu)

```c
static void stat_open(void) { ui_switch(&scr_stat); }
const app_def_t app_stat = {"stat", "Statistics", stat_open, {{0}}, "stat"};
```

Register it in `app/registry.c`: add `extern const app_def_t app_stat;` to your section of the extern
block and `&app_stat,` to your section of `app_registry[]`. The console command `app ID` opens an app
(`app list`). `keys` (up to 3 {key, mod} pairs) are optional global hot keys, checked before the key
actions; every printed key function is already taken, so prefer soft keys on your screens and the APPS menu.

## The keyboard (`keys/layout.py`)

The logical layout is data: `keys/layout.py` gives every physical key (named as in
`hardware/gen/design.py` LAYOUT, which matches the schematic switch values) a name, its keycap legend, what it types in each layer
(`text`, `shift_text`, `alpha`, optional `suffix`es for templates such as `abs(` `)`), or a global `action` /
`shift_action`, its keycap colour group and its keyboard shortcuts in the web simulator. It generates the
firmware key codes and `key_info[]` (`tools/gen_keymap.py`), the case legends and colours
(`enclosure/case.py`), the web simulator's labels and shortcuts (`web/tools/build_html.py`), the README key
table, `keys/keyboard.svg` and the help page `keys` (`tools/gen_keydoc.py`; `make keydoc`, checked by
`make test`). Action names are resolved in `app/keys.c` (`actions[]`; the generator fails on unknown names).
Changing a legend or a colour means reprinting keycaps: rebuild the case (`enclosure/case.py`, `render.py`)
and run `make web-image`.

## Menus

Static menu:

```c
static const menu_item_t items[] = {
    /* label, insert (before cursor), suffix (after cursor), action, help topic (NULL: from insert) */
    {"mean(", "mean(", ")", 0, NULL},
    {"Sort A", NULL, NULL, 1, "sorta"},
};
static void on_select(const menu_item_t *it) { ... }  /* NULL: insert into the screen below */
const menu_def_t menu_stat = {"STAT", items, 2, on_select, false, "stat"};
menu_open(&menu_stat);
```

Dynamic menu (items computed when shown; used by CATALOG, VARS, APPS): `items = NULL` and set
`count()` / `get(i, &item)`; strings returned by `get` must stay valid until the next `get`:

```c
static int n_lists(void) { return nlists; }
static void get_list(int i, menu_item_t *out) {
    static char name[8]; snprintf(name, sizeof name, "L%d", i + 1);
    *out = (menu_item_t){name, name, NULL, i, "lists"};
}
const menu_def_t menu_lists = {"LISTS", NULL, 0, NULL, false, "lists", n_lists, get_list};
```

Number keys 1–9, 0 choose the first ten items; `alpha_jump = true` makes letter keys jump (CATALOG).
Opening a menu from a menu keeps the original screen as the insertion target (`menu_origin()`). SHIFT HELP
on a highlighted item opens its help: `item.help`, else the topic named like `insert` up to `(`
(`"mean("` → `mean`), else the menu's `help`.

## VARS categories

```c
static int stat_vars_count(void) { return have_results ? 4 : 0; }   /* 0 hides the category */
static void stat_vars_get(int i, menu_item_t *out) { static const char *n[] = {"x̄", ...}; ... }
const vars_provider_t vars_stat = {"Statistics", stat_vars_count, stat_vars_get};
```

Register `&vars_stat,` in your section of `vars_registry[]`. Selecting an item inserts `insert`/`suffix`
into the editor below the menu. Whatever you insert must evaluate: either it is CAS syntax, or you define
it in the CAS. Built in: **Variables** and **Functions** (`cas_vars()`), **Y-VARS** (`Y1(`..`Y6(`, defined
as CAS functions `Y1(x):=...` by `yvars_sync()` before each home evaluation, with `cas_eval_quiet` so
`ans` is untouched). Use the same technique for app variables (`cas_eval_quiet(g.cas, "xbar:=3.5", ...)`).

## MODE rows

```c
static int rpn_get(void) { return rpn_on; }
static void rpn_set(int c) { rpn_on = c; ui_set_home(c ? &scr_rpn : NULL); }
const mode_row_t mode_rpn = {"ENTRY", {"ALG", "RPN"}, 2, rpn_get, rpn_set, "rpn"};
```

Register `&mode_rpn,` in your section of `mode_registry[]`. Labels ≤ 8 characters, up to 12 options; the
option spacing shrinks to fit 320 px and the screen scrolls when there are more rows than fit. After
`set()` the screen calls `app_apply_settings()`. The row's value is **not** saved automatically: save it
in your `persist_def_t`. Built-in settings (`g.cs` angle/exact/digits, `g.fmt`, `g.fix`, `g.cplx`, grid,
axes, backlight, insert mode) are saved by `persist_settings` in `app/mode.c`.

## Saved state (persistence)

Everything the user would expect to survive switching off is saved: on power-off
(`platform_hibernate_save()`, called by every platform's `hal_power_off()` / Standby entry), automatically
one minute after the last key (`session_tick`), from MEM > "Save session now", and with the console
`session save`. It is restored at boot (`app_init` → `session_load`). On hardware a wake from Standby is a
reset; the simulators emulate exactly that (wake = `app_init()` + restore), and `reboot` restarts the app
without saving (like a reset/crash).

```c
#include "session.h"
static void stat_save(pw_t *w) {
    pw_u8(w, nlists);
    for (int i = 0; i < nlists; i++) { pw_u16(w, len[i]); for (...) pw_f64(w, v[i][k]); }
    pw_str(w, title);
}
static void stat_load(pr_t *r, unsigned version) {      /* only called if the blob exists */
    unsigned n = pr_u8(r);
    ...
    if (r->err) { stat_defaults(); return; }              /* truncated / corrupt: keep defaults */
}
static void stat_reset(void) { stat_defaults(); }        /* MEM > Reset all (optional) */
const persist_def_t persist_stat = {"stat", 1, stat_save, stat_load, stat_reset};
```

Register `&persist_stat,` in your section of `persist_registry[]` (keys must be unique, ≤ 15 chars).

- Writer: `pw_u8/u16/u32/f64/str/bytes`, little-endian, bounds-checked. Reader: `pr_*` return 0 / "" past
  the end and set `r->err`; `pr_left()`.
- **Versioning**: bump `version` when the layout changes; `load` receives the stored version and must
  accept older ones (or ignore them). Blobs with unknown keys are skipped, missing blobs keep defaults,
  so firmware up/downgrades are safe. Never reorder fields of an existing version.
- **Size**: the whole session is ≤ `SESSION_MAX` (256 KB). A blob that does not fit is dropped (it keeps its
  defaults after the next boot; `state session_dropped` names it). Keep blobs compact: store values, not
  caches; store expressions as text. The CAS blob (`persist_cas` in `app/vars.c`) is last and drops single
  records that do not fit.
- **Speed**: save is serialize + CRC + (only if changed) erase/program of the needed 4 KB sectors (~50 ms
  per 4 KB on the W25Q128). Do not do slow work in `save`.
- Load runs before the first screen is shown, after the CAS exists, before `app_apply_settings()`.
- Storage: `core/pstore.c`, a log of snapshots in the QSPI range 0xE00000–0xFFEFFF (2 MB − 4 KB, see
  `app/session.h`); each snapshot = header (magic, sequence number, length, CRC-32s) + payload, written
  erase → payload → header, so a power loss keeps the previous snapshot; writes walk the region (wear
  levelling). Unit-tested with power cuts at every byte (`tests/unit/test_pstore.c`). The rest of the QSPI
  (0–0xDFFFFF) is free for future use (files).
- CAS state is saved as text records through `cas_save()` / `cas_restore()` (`cas/include/cas.h`).
  Hidden variables (names starting with `_`) are not part of it; a feature that must keep their values
  saves them in its own blob with `cas_save_prefix(g.cas, "_xyz", writer, user)` and reads them back with
  `cas_restore()` (the RPN stack does this for its levels `_rpnN`, `app/rpn.c`). The records keep floats
  with 17 digits and uncertainty sources (`_uncN`), so values stay correlated with the user's variables.

## Themes and fonts

MODE > THEME (Light/Dark) and MODE > FONT (Normal/Large) are in `app/theme.c` (saved by `persist_theme`).

- **Colours.** Draw with the light palette. `app/theme.h` names the roles: `TH_BG` (background), `TH_FG`
  (text, lines), `TH_DIM` (secondary text, hints, previews), `TH_LABEL`, `TH_PANEL` (title bars, panels),
  `TH_SEL_BG` / `TH_SEL_FG` (selection), `TH_ERR`, `TH_FOCUS`, `TH_LINK`, `TH_GRID`, `TH_AXES`,
  `TH_STATUS_BG`. The tokens *are* the light colours (`TH_BG` is `C_WHITE`), so Light mode is unchanged
  pixel for pixel; prefer them over `C_*` in new code so the intent is clear. Dark mode is not done per
  screen: `theme_set(THEME_DARK)` installs a colour map in the graphics layer (`gfx_color_map`,
  `core/gfx.h`) through which every fill, line, pixel and text colour passes, so **every screen is themed
  without code of its own**, including screens that use plain `C_*` colours. Greys are inverted
  (white → near-black, black → light grey), dark colours are lightened with their hue kept, `C_SEL_BG` and
  `C_STATUS_BG` are kept; text ink is chosen against what is under it (white text on a selection stays
  white, black text on a yellow badge stays black). Code that writes the framebuffer directly must map its
  colours with `gfx_map(c)`; anything cached as pixels (like the graph cache) is invalidated by
  `theme_set()` through `g.graph_valid`. Check a dark screenshot of your screen (`tests/ui/23_theme_font.txt`
  shows how: `key SHIFT HOME` (settings), DOWN to THEME, `RIGHT ENTER`).
- **Large font.** `ui_font` is `FONT` (10×20) normally and `font_xlarge` (14×28, 7×14 doubled with
  Scale2x smoothing, in flash bank 2) in large-font mode. Screens whose layout can take a bigger font use
  `UI_FONT` / `UI_CW` / `UI_CH` instead of `FONT` / `CW` / `CH` (the home screen and the generic menus do);
  the others keep `FONT`. `theme_math_font(level)` gives the 2D math fonts for nesting levels 0..2.

## 2D math (textbook display, Textbook entry, preview, repeating decimals)

Files `app/m2d_*.c` (all in flash bank 2 by a pattern in the linker script), header `app/m2d.h`, help
`help/textbook.txt`. The home screen (`app/home.c`) keeps its history and editor and calls the 2D view
(`m2d_home_draw/key/touch/tick`) when Textbook, the preview, repeating decimals or the large font is on;
with all of them off the original Classic drawing code runs.

- **Box layout** (`app/m2d_box.h`): `m2d_parse(pool, linear_text, 0)` turns linear math (CAS output after
  `app_eval`, or entry text) into a box tree (fractions, roots, exponents, fences, Σ/∫/lim, matrices,
  `×10ⁿ`, repeating decimals); `bx_measure`, `bx_place(x, baseline)`, `bx_draw(color)` lay it out and draw
  it. Pools are arrays of `bx_t` on the caller's stack (400 boxes ≈ 16 KB is enough for any home entry);
  `m2d_parse` returns -1 when it does not understand the text: show the text then. Draw with
  `gfx_textlog_enable(false)` and record the linear text with `gfx_textlog_note()` so `expect text` works.
- **Textbook templates** are marker tokens in the normal token editor (`OPEN slot SEP slot ... CLOSE`,
  interned by `m2d_init()`), so session save (`ed_serialize`) keeps them. `ed_display()` / `ed_cas()` call
  `ed_struct_hook` (`app/editor.h`) for lines with templates: `ed_display` gives Classic, re-enterable text
  (`(x+1)/2`, `x^2`, `√(2)`, `log(8,2)`), `ed_cas` the CAS input. Other editors (Y=, forms) never get
  templates: the FRAC/FUNC/MTRX menus insert the linear form there. `m2d_insert_linear()` converts linear
  text back to templates (history recall/paste).
- **Live preview** (`app/m2d_preview.c`): `cas_eval_quiet` from the home screen's tick after 300 ms of idle,
  never for assignments or slow/side-effect commands, time-boxed with `cas_set_poll()` (150 ms, or a key in
  the queue). The clock is virtual in the simulators, so previews appear only after `wait`.
- **Repeating decimals** (`app/m2d_repdec.c`): `repdec_rewrite()` turns `0.(3)` into `((3-0)/9)` in
  `app_eval()` input; `repdec_apply()` shows decimals with a short period as `0.(142857)` (display only; the
  history keeps the CAS text). Use them in your own home-like screens.
- Console/`state` keys: `textbook`, `preview_on`, `preview`, `repeat`, `theme`, `font`, `hscroll`.

## Evaluation and number formatting

- `app_eval(cas_text, out, n)`: `cas_eval` + complex mode (REAL: error on non-real results, r∠θ: polar)
  + number format (FIX/SCI/ENG) + display prettifying (`√`, `π`). Returns the `cas_status`; on error
  `out` is `ERR:...`. Use it for any home-like entry (RPN stack, solver results).
- `app_eval_result(cas_text, out, n)`: `app_eval` plus MODE > SIMPLIFY AUTO (the CAS `autosimplify` pass,
  time-boxed, skipped when the input asks for a form such as `factor(`; `app/result_actions.c`). Only for
  results the user sees on the home screen (RPN levels and the preview use `autosimp_var` / `autosimp_text`);
  helper evaluations of graphs, solvers and apps keep `app_eval` / `cas_eval_quiet`.
- Result actions: `result_action(fn, extra)` applies a command to the current home/RPN result (the REWRITE
  menu `menu_rewrite`, home SHIFT F3/F4, RPN SHIFT F4/F5). `rewrite_open(value)` picks the item list
  from the value: matrix, unit, or `items_expr`, whose last two items (exp→trig, trig→exp) appear only
  when the value has a trig function or `exp`.
- `numfmt_double(v, buf, n)` formats a double with the MODE format; `numfmt_apply(in, out, n)` reformats
  the floats inside CAS output text. Tables/trace still use the width-limited `fmt_num()`.
- `cas_eval_quiet()` evaluates without changing `ans` (helper computations, defining app variables).
- `cas_set_poll(ctx, fn, user)`: `fn` is called during long evaluations; returning nonzero stops it with
  `CAS_ERR_INTERRUPTED` (time boxes, abandoning work when a key is pressed). Afterwards restore the app's
  default hook, `cas_set_poll(g.cas, app_cas_poll, NULL)` (never NULL: it feeds the watchdog during long
  evaluations), and call `hal_watchdog_kick()` in your own callback.
- `app_eval()` accepts repeating decimals (`0.(3)` is 1/3).
- `g.fmt` (`FMT_NORMAL/SCI/ENG`), `g.fix` (−1 = FLOAT, 0–9), `g.cplx` (`CPLX_REAL/RECT/POLAR`).
  While FIX/SCI/ENG is active the CAS computes with 15 digits (`app_apply_settings`).

## Watchdog

The hardware resets the calculator when the main loop has not run for ~2 s (`HAL_WATCHDOG_MS`, IWDG1;
the simulators model it on their virtual clock), and that counts towards safe mode like a crash
(`docs/QSPI_CONTENT.md` 6.1). The main loop, the CAS (through `app_cas_poll`), the console dispatcher,
QSPI erase/program, the microSD and session-save paths already feed it. A feature that blocks for
longer than ~1 s in one go (a long loop over compiled functions, a big file transfer, a busy wait on
a device) calls `hal_watchdog_kick()` inside the loop; do not kick from an interrupt handler or from
`hal_delay_ms`-only waits, which would hide a real hang. Test with `wdg hang` (`tests/ui/85_watchdog.txt`).

## Help pages

Content is plain text in `firmware/help/*.txt`, compiled by `tools/gen_help.py` at build time into
`$(BUILD)/gen/help_gen.c` (topics sorted by id, each compressed separately with LZ77 + one static
canonical Huffman code; decoded on the device into the 128 KB help workspace taken from the arena;
the tables are placed in flash bank 2). Where the compressed pages go depends on the build tier
(`docs/QSPI_CONTENT.md`): the core tier keeps all of them in flash; the standard tier keeps only the
pages named by the `@essentials` line (`help/content.txt`) and puts the full manual into the
`manual` module in the QSPI (`build/modules/MANUA010.MOD`: the file name carries the version). A page that is not built in and not
installed shows "Manual not installed: see Memory > Install content", so pages never break; add a page
to `@essentials` only if a calculator without its manual needs it (keep the list small: it costs bank-2
flash). Add **your own file** (e.g. `help/stat.txt`); the build fails on duplicate ids, broken links,
unknown fields and characters the font cannot draw.

```
%% comment
@group stat-cmds Statistics commands        listing page (topic id stat-cmds) of all @cmd with group: stat-cmds
Intro paragraph of the listing page.

@page stat Statistics app                   free page: topic id "stat", title "Statistics app"
Markup body until the next @ line.

@cmd mean                                   command page: topic id "mean" (case-sensitive)
group: stat-cmds                            required
summary: Arithmetic mean of a list          required, one line, no final period
syntax: mean(list)                          1 or more
insert: mean(                               CATALOG inserts this (default "NAME(")
suffix: )                                   ... and this after the cursor (optional)
label: mean(                                CATALOG label (default = insert)
catalog: no                                 keep it out of the CATALOG (optional)
example: mean([1,2,3,4])                    0+ examples, each optionally followed by
result: 5/2                                 the exact CAS output (checked by make help-test)
see: median, stddev                         related topics

Description body (markup), after exactly one blank line.
```

Markup: `# Title`, `## Subheading`, `- bullet` (one line each), lines indented 4 spaces = code,
paragraphs separated by blank lines (lines are joined and word-wrapped), inline `` `code` ``,
`[[id]]` (shows the topic's title) or `[[id|text]]` links, `{KEY}` key caps such as `{shift} {home}` (the legend printed on the key, keys/layout.py; the build fails on
unknown key caps, so put other braces such as `{1,2,3}` in backticks).
Matrices in prose must be inside backticks (`` `[[1,2],[3,4]]` ``). Allowed characters: printable ASCII and
`θπ√²³⁻¹→←↑↓▶◀▲▼≤≥≠∫∂∞ΣΔ×÷−·°±≈ⁿ█▒ᴇʳⁱˣ∠½∛∜∓Π` (the font's glyphs; tools/gen_font.py EXTRA) and the math
notation of `EXTRA_MATH` there (`∇`, subscript digits `₀…₉`, superscripts `⁰⁴…⁹⁺⁽⁾`, Greek
`αβγδελμνσφχωρτΦΩ`, `∮∈∀∃¬∧∨⊕⇒⇔≡∝‖⟨⟩∪∩ℝℂℤℕ∘⊥⌊⌋⌈⌉…`). A topic is at
most 24 KB of text; a code line shows 43 characters (longer ones wrap). Required page: `index` (link your app's page from it: one line in `help/index.txt`).

In any body, `example: INPUT` / `result: OUTPUT` lines (at the start of a line) show as the code
lines `INPUT` and `▶ OUTPUT` and are checked like the `@cmd` fields; a page's examples share one CAS
session. A `@cmd` body that starts with `## Refresher` puts that section (a short reminder of the
math: definition, facts, conventions, a checked example) above the Syntax; it ends at a line
`## Syntax`, after which the usual description follows (help/refresher.txt has the formula sheets).

Wiring: `screen_t.help`, `app_def_t.help`, `menu_def_t.help`, `menu_item_t.help`, `mode_row_t.help` are
topic ids. SHIFT HELP ({shift} {menu}) resolves: menu item → its topic (or `insert` name) → menu topic; MODE → the
highlighted row's topic; any other screen → `screen.help` or `screen.name`; fallback `index`. From code:
`help_open("id")`, `help_show_text(title, markup)` (generated pages such as About), `help_exists()`.
Every `example:`/`result:` pair runs against the real CAS in `make help-test` (each `@cmd` page starts with
a clean CAS: `casreset`); examples of one page may build on each other.

The printed user manual (`docs/MANUAL.md`, `docs/manual.html`) is generated from the same files by
`tools/gen_manual.py` (`make manual`; `make test` fails when it is out of date). A new `@page` must be added
to a chapter in its `PARTS` table (the build names pages that are missing); `SCREENSHOTS` there maps topics
to pictures in `sim/screenshots/`. `@cmd` pages go to the reference automatically, by CATALOG category.

Console: `manual ID` opens a topic, `manual list` lists them; `state help_topic`, `help_top`, `help_link`,
`help_history`.

## Flash budget

`make fw` / `make release` print bank usage (`tools/flash_budget.py`) and fail on overflow; the linker
also fails. Bank 1 (vectors, app, core, drivers) is ~74 % full; bank 2 (CAS library, help, 2D math, large font) ~75 %
(keep both under the 90 % budget, `CONFIG_FLASH_BUDGET_PCT`). `make flash-report` lists the bytes per module (`app/<file>`, `cas/src/<file>`, libraries,
help data, fonts) and per group, split by bank, from the map file. Initialised statics go to `.data`, whose image
is stored in bank 1: a struct that holds an editor or a buffer should be zero-initialised and set up at run time. The
two banks are not contiguous, so the release has `calc.bin` (bank 1) and `calc_bank2.bin` (bank 2); the
`.elf`/`.hex` contain both (see `release/FLASHING.md`). To move big code to bank 2, tag functions with
`FLASH2_TEXT` / tables with `FLASH2_RODATA` (`core/sections.h`), or add an object pattern such as
`*app/stat_*.o(.text .text.* .rodata .rodata.*)` to the `.flash2` section in
`platform/stm32h743/stm32h743zg.ld`. RAM: avoid large static buffers (AXI SRAM is ~90 % allocated
including the SDRAM-failure fallback arena); carve big work areas from the arena in `app_init()` like the
session buffer and help workspace, or allocate them in the CAS-free part of a screen's own state.

## Graph overlays (statistics plots)

Scatter, xyLine, histogram and box plots (or anything else in graph coordinates) are drawn into the GRAPH
screen through an overlay: same WINDOW, cached with the plot, traced with {TRACE} ({▲}{▼} go through the
functions, then through the point sets of every active overlay), included in ZoomFit.

```c
/* app/stat_plot.c */
#include "graph_overlay.h"
static bool sp_active(void) { return plot_on[0] || plot_on[1]; }
static void sp_draw(void)                 /* clip = plot area; use the gov_* helpers */
{
    for (int i = 0; i < n; i++)
        gov_mark(xs[i], ys[i], GOV_BOX, C_BLUE);   /* GOV_DOT, GOV_BOX, GOV_CROSS, GOV_PLUS */
    gov_rect(q1, 0.2, q3, 0.8, C_BLUE, false);     /* box plot: rectangle in graph units */
    gov_line(x0, y0, x1, y1, C_BLUE);
}
static int sp_nsets(void) { return 1; }
static int sp_npoints(int set) { return n; }
static bool sp_point(int set, int i, double *x, double *y) { *x = xs[i]; *y = ys[i]; return true; }
static const char *sp_label(int set, char *b, size_t m) { snprintf(b, m, "P1:L1,L2"); return b; }
static uint16_t sp_color(int set) { return C_BLUE; }
static bool sp_bounds(double *x0, double *x1, double *y0, double *y1) { ...; return n > 0; }
const graph_overlay_t graph_overlay_stat = {"stat", sp_active, sp_draw, sp_nsets, sp_npoints, sp_point,
                                            sp_label, sp_color, sp_bounds};
```

Register `&graph_overlay_stat,` in the statistics section of `graph_overlay_registry[]` (`app/registry.c`;
`graph_overlay_stub` is an inactive placeholder that can stay or go). Every callback except `draw` may be
NULL. When the data or the plot settings change call `graph_invalidate()` (re-plot on the next visit);
`graph_zoom_overlays()` sets the window to the overlays' bounds (ZoomStat: call it from your own menu item,
then `ui_switch(&scr_graph)`). Helpers: `gov_px/gov_py` (screen coordinates of a graph point),
`gov_window()`. Draw only inside the plot (`BODY_Y`..`LCD_HEIGHT`; the clip is set).

Graphing internals for other code (`app/graph.h`): graph mode `gx.mode` (`GM_FUNC/PAR/POL/SEQ/IMPL/DE`),
`gx_compile()`, evaluation `func_eval()`, `gx_par()`, `gx_seq()`, `gx_impl()`, DE solution curves `gx_de_solve()`; numeric methods `gn_root`,
`gn_root_near`, `gn_min`, `gn_deriv`, `gn_integrate` (Brent, Gauss-Kronrod; usable by any app). The CAS
compiles expressions in several variables with `cas_compile_vars()` / `cas_fn_eval_vars()` (`cas.h`; the
graph uses it for x/y relations and for slider parameters, which are inputs of the compiled code).

## Console commands and UI tests

Tests are console scripts in `tests/ui/NN_name.txt` (run by `make ui-test` with the native simulator and
by `make web-test` in WebAssembly, which must produce identical screenshots). Useful commands: `key`,
`type`, `touch`, `state [KEY]`, `expect screen|text|notext|state|statecontains|pixel|color|eval`,
`screenshot`, `wait`, `ifcas src|stub CMD` (lines that need the real CAS), `power off` / `key SHIFT ON` +
`key ON` (power cycle = restart from the saved session), `reboot` (restart without saving), `session
info|save|clear`, `app ID`, `manual ID`, `casreset`. **To change a MODE setting in a test, use `setmode ROW OPTION` (e.g. `setmode ENTRY RPN`, `setmode DECIMAL 2`)** rather than counting DOWN presses on the MODE screen: other features add rows, and the screen remembers its last row. Add commands for your feature to
`app_console_cmds[]` in `app/app_console.c` (append at the end of the table) or ask for a second table.
Tests must pass with `CAS=stub` too (guard CAS-specific lines with `ifcas src`). Every test starts with
an empty flash, so saved state never leaks between tests; to test persistence use `key SHIFT ON`,
`key ON` and `reboot` in one script.

## Files on the microSD card (`core/fat.h`)

A small FAT16/FAT32 layer (8.3 names when writing, one open file at a time) over the platform block
device (`hal.h`: `hal_sd_present/open/read/write/close`, 512-byte blocks; hardware: SDMMC1 4-bit polled,
`platform/stm32h743/sd.c`; simulators: `calcsim --sd IMAGE`, or a RAM card from the console
`fs insert MB` + `fs format`, also in the web build). Reuse it for screenshots / session backups:

```c
#include "fat.h"
fs_file_t f;
int r = fs_mkdir("/CALC");                                /* ok if it exists */
if (!r) r = fs_open(&f, "/CALC/SHOT001.PNG", FS_WRITE);   /* replaces an existing file */
if (!r && fs_write(&f, data, n) != (int)n) r = FS_EIO;    /* fs_write drops the file on error */
if (!r) r = fs_close(&f);                                 /* publishes it (one directory-sector write) */
fs_unmount();                                             /* powers the card down: after each operation */
if (r) ui_message("MICROSD", fs_strerror(r));
```

Reading: `fs_open(&f, path, FS_READ)`, `fs_read()` until it returns 0, `fs_close()`. `fs_list(dir, cb,
user)` walks a directory (long names are shown), `fs_remove()`, `fs_info()` (size, free space).
Mounting is implicit. A written file goes to fresh clusters and replaces the old version only in
`fs_close()`, so a card pulled mid-write keeps the old file (`tests/unit/test_stat.c` cuts the card at
every write of an update). Errors are `FS_E*` (< 0). Console: `fs ls [DIR]`, `fs cat F`, `fs expect F
TEXT`, `fs put F TEXT`, `fs rm F`, `fs mkdir D`, `fs info`.

## Statistics lists and plots (`app/stat.h`)

- Lists: `sl_find("L1")` / `sl_create("NAME")` give an `slist_t` (`v[]`, `len`, capacity `sl_cap()`);
  after changing values call `sl_changed(l)` (recomputes column formulas, invalidates the graph) or use
  `sl_set(l, values, n)`. While the home screen evaluates, list names are CAS variables (`app_eval`
  calls `stat_pre_eval` / `stat_post_eval`).
- Computations without UI: `app/stat_math.h` (`st_onevar`, `st_regress`, tests, intervals) and
  `cas/include/cas_stat.h` (distributions in double precision; also linked in the stub CAS build).
- Graph overlay: `stat_plots_draw(xmin, xmax, ymin, ymax, gx, gy, gw, gh)` draws the enabled stat plots;
  the graph screen calls it through the overlay `graph_overlay_stat` (`app/stat_overlay.c`, registered
  in `graph_overlay_registry[]`, see "Graph overlays" above). `stat_zoomstat()` fits the window to the plots.
- Console: `stat lists | list N | set N v,v,... | expect N v,v,... | formula N EXPR | last | import F |
  export F L1,L2`.

## Input comforts (`app/input.h`)

Undo/redo, the clipboard, the CATALOG search and the history search work on every screen without code of its
own: MENU opens the context menu, whose soft keys are then the edit bar (F1 undo, F2 redo, F3 copy, F4 paste,
F5 history search; SHIFT F1 cut, SHIFT F2 clips).

- **Undo** records every `editor_t` change (`ed_change_hook` in `app/editor.h`) made while app.c dispatches a
  key or touch event: one step per event and editor, letters/digits typed in a row coalesce. Changes outside
  events (session load, ticks) are not steps. If your `screen_t.editor()` has side effects (starts an edit),
  that is fine: the edit bar calls it with recording paused.
- **Copy/paste** use the screen's editor; the home history selection, `xgrid`/`xform` widgets
  (`xgrid_shown`, `xform_shown`) and the list editor (`stat_edit_clip_text`) are special-cased in
  `app/input_clip.c`. Other code can put text on the clipboard with `clip_put()`.
- **CATALOG categories** are `@category` blocks in `help/input.txt` (`groups:`, `commands:`, `apps:`); new
  `@cmd` pages land in the category of their group or in "Other". Add your group or app id to a category.
- **Filter menus**: set `menu_def_t.filter_key` (keys not used by the menu; return true when the list
  changed) and `.query` (text shown in the title line); `query_t` / `query_key()` and `fuzzy_score()` do the
  typing and ranking.
- `state` keys: `clip`, `clips`, `undo_n`, `redo_n`, `catalog_query`, `catalog_cat`, `catalog_n`,
  `hist_query`, `hist_n`.

## Spreadsheet, screenshots, session backup

- Spreadsheet model without UI: `app/sheet.h` (`sheet_set`, `sheet_get`, `sheet_fill_down`, `sheet_link`,
  `sheet_csv_export/import`); each cell is the hidden CAS variable `_sA1`, so other code can read a cell's
  exact value with `cas_eval_quiet(g.cas, "_sA1", ...)` after `sheet_ensure()`. Console: `sheet ...`.
  The RPN stack keeps its levels the same way, as hidden variables `_rpnN` (`app/rpn.c`); refer to such a
  variable as `1*_rpnN` inside a command, since commands like `subs` and `quo` do not evaluate a bare name
  given after their first argument.
- Screenshots and backups: `app/sdbk.h` (`sdbk_screenshot`, `sdbk_backup`, `sdbk_restore`); ALPHA then MENU
  saves `/CALC/SHOTnnn.PNG` from any screen. A backup is the session payload (`session_snapshot()`, every
  `persist_registry` blob), so a new feature's saved state is backed up automatically. Console: `sdtool ...`.
- Uncertainty is CAS syntax (`9.81±0.02`, `pm(v,s)`; `cas/src/unc.c`): results print as `v±s`, and
  `numfmt_apply` leaves the two numbers of `v±s` unformatted.
