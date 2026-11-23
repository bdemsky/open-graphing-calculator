#!/usr/bin/env python3
"""Logical keyboard layout: the single source of truth for what every key is.

The PCB fixes where the 49 matrix keys + ON are and how they are wired:
hardware/gen/design.py LAYOUT lists them by name (rows top to bottom, wired
row-major into the 7x7 matrix; the name is also the key switch's value in the
schematic). This file says what each key is:

  name     firmware key code KEY_<name>, console `key NAME`, web simulator name,
           position in design.py LAYOUT, schematic switch value
  legend   keycap legend (Unicode; enclosure/case.py turns it into CAD text)
  text     what the key types into an editor (display form, app/editor.c tokens)
  suffix   text inserted after the cursor together with `text` (e.g. a closing ")")
  action   global action instead of text (resolved in app/keys.c)
  shift    SHIFT legend (printed above the key), shift_text / shift_suffix /
           shift_action as above
  alpha    ALPHA character (printed above the key, right; lower case in the editor
           after one ALPHA, upper case after two)
  group    keycap colour group (enclosure/case.py, PALETTE below)
  aliases  extra console names
  kbd      browser keyboard shortcuts (KeyboardEvent.key values) in the web simulator
  doc      one-line explanation for the generated key table (README, help "keys");
           shift_doc the same for the SHIFT function

Generated from this file:
  firmware/tools/gen_keymap.py  -> firmware/core/gen/keymap_gen.{c,h} (codes, matrix, legends, texts)
  firmware/tools/gen_keydoc.py  -> the key table in firmware/README.md, the help page "keys"
                                   ($(BUILD)/gen/help_keys.txt) and firmware/keys/keyboard.svg
  firmware/web/tools/build_html.py -> keyboard shortcuts + legends of the web simulator
  enclosure/case.py             -> keycap legends, SHIFT/ALPHA legends on the top shell, colours
Keys with no legend for a layer (shift=None) do nothing special in that layer
unless the current screen handles them (e.g. SHIFT arrows).

F1..F5 are soft keys: the current screen labels them in the soft-key bar at
the bottom of the display (app/ui.h softkeys_t); SHIFT F1..F5 are the
screen's secondary soft keys.
"""

# Filaments: one entry per spool the printed set uses. Remap a colour here and every group,
# legend and label that uses it follows (enclosure/case.py, render scripts, keyboard.svg).
FILAMENTS = {
    "white":     "#eef0f2",    # white: shells and door, number keys; legends on the blue caps
    "beige":     "#d8c9a8",    # beige: function keys; every key of the printed (one-colour) set
    "black":     "#1f2023",    # black: legends on the white and beige caps; ALPHA letters
    "blue":      "#2366c0",    # cobalt blue: SHIFT, ALPHA, ENTER, ÷ × − +; SHIFT labels
}

# Keycap colour groups -> filaments (cap body and its inlaid legend): the colour scheme of the
# renders and the web simulator. The printed loose keys are all beige with black legends.
_GROUPS = {
    "fn":     ("beige", "black", "function keys"),
    "num":    ("white", "black", "number keys"),
    "accent": ("blue", "white", "SHIFT and the operators ÷ × − + enter"),
    "alpha":  ("blue", "white", "ALPHA"),
}
PALETTE = {g: {"cap": FILAMENTS[c], "legend": FILAMENTS[l], "cap_filament": c, "legend_filament": l,
               "name": f"{c} (legends {l}): {what}"} for g, (c, l, what) in _GROUPS.items()}
BODY_FILAMENT, SHIFT_FILAMENT, ALPHA_FILAMENT = "white", "blue", "black"
BODY = FILAMENTS[BODY_FILAMENT]            # top/back shell
SHIFT_INK = FILAMENTS[SHIFT_FILAMENT]      # SHIFT legends of the face artwork (= the SHIFT key)
ALPHA_INK = FILAMENTS[ALPHA_FILAMENT]      # ALPHA letters of the face artwork (black, as the key legends)


def K(name, legend, *, text=None, suffix=None, action=None, shift=None, shift_text=None,
      shift_suffix=None, shift_action=None, alpha=None, group="fn", aliases=(), kbd=(), doc="",
      shift_doc=""):
    """One key record (a plain dict with every field of the module docstring).

    `name` is the key name (also its id in hardware/gen/design.py LAYOUT); every other
    field is optional and keyword-only. `group` defaults to the beige "fn" caps."""
    return dict(name=name, legend=legend, text=text, suffix=suffix, action=action, shift=shift,
                shift_text=shift_text, shift_suffix=shift_suffix, shift_action=shift_action, alpha=alpha,
                group=group, aliases=list(aliases), kbd=list(kbd), doc=doc, shift_doc=shift_doc)


# Rows top to bottom, as on the keyboard (the same order as design.py LAYOUT).
KEYS = [
    # ---- soft keys (labels on the display) ----
    K("F1", "F1", kbd=["F1"], doc="soft key 1 (label on the screen)"),
    K("F2", "F2", kbd=["F2"], doc="soft key 2"),
    K("F3", "F3", kbd=["F3"], doc="soft key 3"),
    K("F4", "F4", kbd=["F4"], doc="soft key 4"),
    K("F5", "F5", kbd=["F5"], doc="soft key 5"),
    # ---- row A (left of the D-pad) ----
    K("SHIFT", "shift", group="accent", kbd=["["],
      doc="next key: its SHIFT function (blue legend)"),
    K("ALPHA", "alpha", group="alpha", shift="A-lock", kbd=["]"],
      doc="next key: its letter (black legend); twice: capital", shift_doc="letters until ALPHA or CLEAR"),
    K("DEL", "⌫", shift="ins", aliases=["BACKSPACE", "BKSP", "DELETE"], kbd=["Backspace", "Delete"],
      doc="delete the character before the cursor", shift_doc="insert / overwrite"),
    # ---- row B ----
    K("HOME", "home", action="home", shift="settings", shift_action="settings", kbd=["Home"],
      aliases=["QUIT"], doc="calculator (home or RPN) screen", shift_doc="settings (mode, format, display)"),
    K("APPS", "apps", action="apps", shift="catalog", shift_action="catalog", kbd=["a"],
      doc="app launcher", shift_doc="every command A-Z"),
    K("MENU", "menu", action="menu", shift="help", shift_action="help", kbd=["u"],
      doc="options of the current screen", shift_doc="help for the current screen or menu item"),
    # ---- D-pad ----
    K("UP", "▲", shift_action="brighter", kbd=["ArrowUp"], doc="", shift_doc="backlight brighter"),
    K("LEFT", "◀", kbd=["ArrowLeft"], doc="", shift_doc="start of the line"),
    K("RIGHT", "▶", kbd=["ArrowRight"], doc="", shift_doc="end of the line"),
    K("DOWN", "▼", shift_action="dimmer", kbd=["ArrowDown"], doc="", shift_doc="backlight dimmer"),
    # ---- main grid ----
    K("CAS", "CAS", action="cas", shift="const", shift_action="const", alpha="a", kbd=["k"],
      doc="CAS menu (factor, solve, d/dx, ∫, limit ...)", shift_doc="physical constants"),
    K("MATH", "math", action="math", shift="matrix", shift_action="matrix", alpha="b", kbd=["m"],
      doc="MATH menu (abs, round, mod, !, roots ...)", shift_doc="matrix menu"),
    K("VARS", "vars", action="vars", shift="units", shift_action="units", alpha="c", kbd=["v"],
      doc="variables, functions, lists, results", shift_doc="units and conversions"),
    K("X", "x", text="x", shift="π", shift_text="π", alpha="d", aliases=["VAR"],
      kbd=["x", "X"], doc="the variable: x (t, θ or n in the other graph modes)", shift_doc="pi"),
    K("CLEAR", "clear", shift="clear all", shift_action="clearall", aliases=["CLR"], kbd=["Escape"],
      doc="clear the line / leave the screen",
      shift_doc="asks, then clears the variables, entry line and history (RPN: the stack)"),

    K("SIN", "sin", text="sin(", shift="sin⁻¹", shift_text="sin⁻¹(", alpha="e", kbd=["s"]),
    K("COS", "cos", text="cos(", shift="cos⁻¹", shift_text="cos⁻¹(", alpha="f", kbd=["c"]),
    K("TAN", "tan", text="tan(", shift="tan⁻¹", shift_text="tan⁻¹(", alpha="g", kbd=["t"]),
    K("LN", "ln", text="ln(", shift="eˣ", shift_text="e^(", alpha="h", kbd=["n"]),
    K("LOG", "log", text="log(", shift="10ˣ", shift_text="10^(", alpha="i", kbd=["l"]),

    K("SQR", "x²", text="²", shift="x³", shift_text="³", alpha="j", aliases=["X^2", "SQUARE"],
      kbd=["q"], doc="square", shift_doc="cube"),
    K("POW", "xʸ", text="^", shift="ʸ√x", shift_text="^(1/", shift_suffix=")", alpha="k",
      aliases=["^", "CARET"], kbd=["^"], doc="power", shift_doc="root: x SHIFT xʸ y = y-th root of x"),
    K("LPAREN", "(", text="(", shift="[", shift_text="[", alpha="l", aliases=["("], kbd=["("]),
    K("RPAREN", ")", text=")", shift="]", shift_text="]", alpha="m", aliases=[")"], kbd=[")"]),
    K("DIV", "÷", text="/", shift="x⁻¹", shift_text="⁻¹", alpha="n", group="accent", aliases=["/"], kbd=["/"],
      shift_doc="reciprocal"),

    K("SQRT", "√", text="√(", shift="∛", shift_text="(", shift_suffix=")^(1/3)", alpha="o",
      aliases=["ROOT"], kbd=["r"], doc="square root", shift_doc="cube root"),
    K("7", "7", text="7", shift="e", shift_text="e", alpha="p", group="num", kbd=["7"],
      shift_doc="Euler's number"),
    K("8", "8", text="8", shift="{", shift_text="{", alpha="q", group="num", kbd=["8"],
      shift_doc="list"),
    K("9", "9", text="9", shift="}", shift_text="}", alpha="r", group="num", kbd=["9"]),
    K("MUL", "×", text="*", shift="×10ⁿ", shift_text="ᴇ", alpha="s", group="accent", aliases=["*"],
      kbd=["*"], shift_doc="exponent: 6.02 SHIFT × 23"),

    K("COMMA", ",", text=",", shift="angle", shift_action="angle", alpha="t", aliases=[","], kbd=[","],
      shift_doc="angle units (° ʳ)"),
    K("4", "4", text="4", shift="<", shift_text="<", alpha="u", group="num", kbd=["4"]),
    K("5", "5", text="5", shift="=", shift_text="=", alpha="v", group="num", kbd=["5"]),
    K("6", "6", text="6", shift=">", shift_text=">", alpha="w", group="num", kbd=["6"]),
    K("SUB", "−", text="-", shift="|x|", shift_text="abs(", shift_suffix=")", alpha="x",
      group="accent", aliases=["-", "MINUS"], kbd=["-"], shift_doc="absolute value"),

    K("ASSIGN", "→", text="→", shift=":=", shift_text=":=", alpha="y",
      kbd=[">"], doc="store: 5 → a", shift_doc="define: f(x):=x²"),
    K("1", "1", text="1", shift="≤", shift_text="≤", alpha="z", group="num", kbd=["1"]),
    K("2", "2", text="2", shift="≠", shift_text="≠", alpha="θ", group="num", kbd=["2"]),
    K("3", "3", text="3", shift="≥", shift_text="≥", alpha="_", group="num", kbd=["3"],
      doc="", shift_doc=""),
    K("ADD", "+", text="+", shift="Σ", shift_text="sum(", shift_suffix=",k,1,n)", alpha='"',
      group="accent", aliases=["+", "PLUS"], kbd=["+"], shift_doc="summation"),

    K("ON", "on", shift="off", shift_action="off", kbd=["End"],
      doc="on; stops a long computation", shift_doc="off (the session is kept)"),
    K("0", "0", text="0", shift="∞", shift_text="∞", alpha=" ", group="num", kbd=["0"],
      shift_doc="infinity"),
    K("DOT", ".", text=".", shift="i", shift_text="i", alpha=":", group="num", aliases=["."], kbd=["."],
      shift_doc="imaginary unit"),
    K("NEG", "(−)", text="⁻", shift="ans", shift_text="Ans", group="num", aliases=["(-)", "NEGATE"],
      kbd=["~"], doc="negative sign", shift_doc="last answer"),
    K("ENTER", "enter", shift="entry", group="accent", aliases=["RET", "EXE"], kbd=["Enter"],
      doc="evaluate / select", shift_doc="recall earlier entries"),
]

BY_NAME = {k["name"]: k for k in KEYS}    # key name -> key record


def check(design_layout=None):
    """Consistency checks (also against hardware/gen/design.py LAYOUT if given).

    Asserts: 50 unique key names, unique ALPHA characters covering a-z, unique browser
    shortcuts, at most one of text/action per layer, a known colour group, and (with
    `design_layout`) that the names are exactly the PCB's keys plus ON."""
    names = [k["name"] for k in KEYS]
    assert len(names) == len(set(names)) == 50, "50 unique logical keys"
    alphas = [k["alpha"] for k in KEYS if k["alpha"]]
    assert len(alphas) == len(set(alphas)), "duplicate ALPHA characters"
    assert set("abcdefghijklmnopqrstuvwxyz") <= set(alphas), "every letter needs a key"
    kb = [s for k in KEYS for s in k["kbd"]]
    assert len(kb) == len(set(kb)), "duplicate keyboard shortcut"
    for k in KEYS:
        assert not (k["text"] and k["action"]) and not (k["shift_text"] and k["shift_action"]), k["name"]
        assert k["group"] in PALETTE, k["name"]
    if design_layout is not None:
        pcb = [p for row in design_layout for p in row] + ["ON"]
        assert sorted(pcb) == sorted(BY_NAME), "key names must match hardware/gen/design.py LAYOUT"


def rows(design_layout):
    """Key names in design.py LAYOUT row order. ON is not in LAYOUT; it is inserted at
    column 0 of the bottom row (the 4-key row starting with "0"), where it sits on the PCB."""
    out = []
    for row in design_layout:
        names = list(row)
        if len(row) == 4 and row[0] == "0":
            names = ["ON"] + names
        out.append(names)
    return out


if __name__ == "__main__":
    check()
    for k in KEYS:
        print(f"{k['name']:7s} {k['legend']:6s} {k['shift'] or '':10s} {k['alpha'] or ''}")
