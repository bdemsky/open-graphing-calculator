#!/usr/bin/env python3
"""Assemble the self-contained web simulator page (no external requests).

Inlines the Emscripten module (SINGLE_FILE: the .wasm is already base64 inside
it), web/page/calc_api.js, web/page/calc_page.js, web/assets/keymap.json and the
calculator picture (web/assets/calc_{1x,2x}.webp as data URIs) and the
keyboard layout (keys/layout.py: legends, keyboard shortcuts) into
web/page/calculator.html.in. Standard library only.

Pipeline (`make web`): emcc builds the firmware for the web platform (platform/web) into
one MODULARIZE + SINGLE_FILE .js; render_calc.py (`make web-image`, run rarely) produces
the picture and keymap.json from the enclosure CAD; this script fills the {{...}}
placeholders of the template and writes a single offline HTML file (--out).

  build_html.py --module $(BUILD)/web/calc_module.js --out web/dist/calculator.html
"""
import argparse
import base64
import html
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(WEB, "..", "keys"))
import layout  # noqa: E402  (keys/layout.py: names, legends, keyboard shortcuts)

# KeyboardEvent.key values -> short labels for the shortcut help line (HTML-escaped where needed)
KBD_NAMES = {"Backspace": "Bksp", "Delete": "Del", "Escape": "Esc", "ArrowUp": "↑", "ArrowDown": "↓",
             "ArrowLeft": "←", "ArrowRight": "→", ">": "&gt;", "<": "&lt;"}


def keys_json():
    """Per-key data for the page script ({{KEYS_JSON}}): name, legends and browser shortcuts."""
    return [{"name": k["name"], "legend": k["legend"], "shift": k["shift"] or "", "alpha": k["alpha"] or "",
             "kbd": k["kbd"]} for k in layout.KEYS]


def kbd_help():
    """The page's shortcut line, generated from layout.py (digits and arrows summarized)."""
    parts = ["<kbd>0</kbd>–<kbd>9</kbd>", "<kbd>←↑→↓</kbd>", "<kbd>F1</kbd>–<kbd>F5</kbd>=soft keys"]
    skip = set("0123456789") | {"ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "F1", "F2", "F3", "F4", "F5"}
    for k in layout.KEYS:
        ks = [c for c in k["kbd"] if c not in skip]
        if not ks:
            continue
        cap = "".join(f"<kbd>{KBD_NAMES.get(c, html.escape(c))}</kbd>" for c in ks)
        parts.append(f"{cap}={html.escape(k['legend'])}")
    return " ".join(parts)


def read(path, mode="r"):
    """Whole file as str ("r") or bytes ("rb")."""
    with open(path, mode) as f:
        return f.read()


def js_safe(src):
    # an inline <script> ends at the first "</script"; "<\/" means the same thing inside JS strings
    return re.sub(r"</(script)", r"<\\/\1", src, flags=re.I)


def main():
    """Substitute every placeholder (each must occur in the template) and write --out."""
    ap = argparse.ArgumentParser()
    ap.add_argument("--module", required=True, help="Emscripten output (MODULARIZE + SINGLE_FILE)")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    assets = os.path.join(WEB, "assets")
    keymap = json.loads(read(os.path.join(assets, "keymap.json")))
    files = keymap["image"]["files"]
    img1 = read(os.path.join(assets, files["1x"]), "rb")
    img2 = read(os.path.join(assets, files["2x"]), "rb")
    uri = lambda b: "data:image/webp;base64," + base64.b64encode(b).decode()
    w1 = keymap["image"]["width"]
    ratio = keymap["image"]["width"] / keymap["image"]["height"]
    # displayed width: the page fits the picture to the viewport height (minus help line) or width
    sizes = f"min(calc((100vh - 64px) * {ratio:.4f}), calc(100vw - 32px))"

    page = read(os.path.join(WEB, "page", "calculator.html.in"))
    module_js = read(args.module)
    subs = {
        "{{IMG_1X}}": uri(img1), "{{IMG_2X}}": uri(img2),
        "{{IMG_1X_W}}": str(w1), "{{IMG_2X_W}}": str(2 * w1), "{{IMG_SIZES}}": sizes,
        "{{CALC_MODULE_JS}}": js_safe(module_js),
        "{{CALC_API_JS}}": js_safe(read(os.path.join(WEB, "page", "calc_api.js"))),
        "{{CALC_PAGE_JS}}": js_safe(read(os.path.join(WEB, "page", "calc_page.js"))),
        "{{KEYMAP_JSON}}": js_safe(json.dumps(keymap, ensure_ascii=False, separators=(",", ":"))),
        "{{KEYS_JSON}}": js_safe(json.dumps(keys_json(), ensure_ascii=False, separators=(",", ":"))),
        "{{KBD_HELP}}": kbd_help(),
    }
    # one pass over the template, so inserted text is never searched for placeholders
    for k in subs:
        assert k in page, k
    page = re.sub("|".join(map(re.escape, subs)), lambda m: subs[m.group(0)], page)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as f:
        f.write(page)
    print(f"wrote {args.out} ({len(page.encode())} bytes)")


if __name__ == "__main__":
    main()
