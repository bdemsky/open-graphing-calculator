#!/usr/bin/env python3
"""Render the front of the assembled calculator for the web simulator.

Front orthographic view (pyvista, off-screen) of the top shell with its face overlay
(the laser-printed sheet with the SHIFT/ALPHA labels, enclosure/overlay.py) and the
keycaps from enclosure/out/*.stl, with the display active area as dark glass.
Because the camera is orthographic with a known parallel scale, every CAD point
maps to an exact pixel:  px = W/2 + (X - CX) * S,  py = H/2 - (Y - CY) * S.

Outputs (--out, default firmware/web/assets/):
  calc_1x.webp / calc_2x.webp    the picture (transparent background)
  keymap.json                     image size, display active area and key rectangles (logical key
                                  names, legends from firmware/keys/layout.py)
  debug_overlay.png               1x picture with the computed rectangles drawn on it

Run with the repo venv:  ../.venv/bin/python web/tools/render_calc.py   (or `make web-image`)
Requires enclosure/out/*.stl (run enclosure/case.py first).
"""
import argparse
import json
import os
import sys

import numpy as np
import pyvista as pv
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../../.."))
ENC = os.path.join(ROOT, "enclosure")
sys.path.insert(0, ENC)
import case as cs  # noqa: E402  (enclosure/case.py: keys(), cad(), AA, OUT_W/OUT_H)
import overlay as ov  # noqa: E402  (enclosure/overlay.py: the overlay's printed colours)

pv.OFF_SCREEN = True

H1 = 900          # 1x image height in px
MARGIN = 2.0      # mm of transparent margin around the case outline
KL = cs.keys_layout   # firmware/keys/layout.py: logical keys, colour groups, filament colours
# the top shell is one colour; the overlay sheet (render_overlay.stl) and its labels
# (render_top_{shift,alpha}.stl) lie on its face, in the colours the laser prints
COLORS = {"top": KL.BODY, "overlay": ov.ink(ov.COLORS["background"]),
          "top_shift": ov.ink(KL.SHIFT_INK), "top_alpha": ov.ink(KL.ALPHA_INK),
          "panel": "#0c0d10", "glass": "#0b1a2a", "pcb": "#15171b"}
FILES = {"top": "top_shell.stl", "overlay": "render_overlay.stl",
         "top_shift": "render_top_shift.stl", "top_alpha": "render_top_alpha.stl"}
for _g in cs.GROUPS:
    COLORS["k_" + _g] = KL.PALETTE[_g]["cap"]
    COLORS["k_" + _g + "_legend"] = KL.PALETTE[_g]["legend"]
    FILES["k_" + _g] = f"render_keys_{_g}.stl"
    FILES["k_" + _g + "_legend"] = f"render_keys_{_g}_legend.stl"


def render(height_px, view_w, view_h, path):
    """Render the case front, viewing `view_w` x `view_h` mm centred on the case, to an RGBA
    image `height_px` tall (width from the aspect ratio, rounded to an even number so the
    2x picture halves exactly to 1x with the same centre); saves it to `path` and returns it.
    parallel_scale = view_h / 2 makes the scale exactly height_px / view_h px per mm."""
    width_px = 2 * int(round(height_px * view_w / view_h / 2))
    meshes = {k: pv.read(os.path.join(ENC, "out", f)) for k, f in FILES.items()}
    px, py, pw, ph = cs.board_rect(cs.PANEL)
    # Display: capacitive-touch panel. The cover glass (black mask outside the viewing area
    # case.VA) fills the panel outline; the viewing area is dark glass. The live canvas
    # covers the LCD active area case.AA, which lies inside VA.
    vx, vy, vw, vh = cs.board_rect(cs.VA)
    meshes["panel"] = pv.Box(bounds=(px - pw / 2, px + pw / 2, py - ph / 2, py + ph / 2,
                                     cs.PANEL_BACK, cs.CEIL - 0.05))
    meshes["glass"] = pv.Box(bounds=(vx - vw / 2, vx + vw / 2, vy - vh / 2, vy + vh / 2, cs.CEIL - 0.05, cs.CEIL))
    # the PCB under the shell, so openings in the top plate (LEDs, sensor) look dark, not see-through
    meshes["pcb"] = pv.Box(bounds=(-cs.OUT_W / 2 + cs.WALL, cs.OUT_W / 2 - cs.WALL, -cs.OUT_H / 2 + cs.WALL, cs.OUT_H / 2 - cs.WALL, -1.6, 0.0))
    p = pv.Plotter(off_screen=True, window_size=(width_px, height_px), lighting="three lights")
    for k, m in meshes.items():
        p.add_mesh(m, color=COLORS[k], smooth_shading=False, specular=0.15 if k.startswith("k_") else 0.05)
    p.camera_position = [(0, 0, 500), (0, 0, 0), (0, 1, 0)]
    p.enable_parallel_projection()
    p.camera.parallel_scale = view_h / 2.0
    p.enable_anti_aliasing("ssaa")
    img = p.screenshot(None, transparent_background=True, return_img=True)
    p.close()
    im = Image.fromarray(img)
    assert im.size == (width_px, height_px), (im.size, width_px, height_px)
    im.save(path)
    return im


def main():
    """Render at 2x, downsample to 1x, compute the pixel rectangles and write the outputs."""
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(HERE, "..", "assets"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    # render once at 2x (supersampled) and downsample for 1x, so both pictures match exactly

    view_w, view_h = cs.OUT_W + 2 * MARGIN, cs.OUT_H + 2 * MARGIN
    tmp = os.path.join(args.out, "_render_2x.png")
    im2 = render(2 * H1, view_w, view_h, tmp)
    os.remove(tmp)
    w2, h2 = im2.size
    im1 = im2.resize((w2 // 2, h2 // 2), Image.LANCZOS)
    w1, h1 = im1.size
    s1 = h1 / view_h  # px per mm at 1x (exact: parallel scale = view_h / 2)

    def to_px(x_mm, y_mm):  # CAD (origin board centre, Y up) -> 1x pixel coords (top-left origin)
        return w1 / 2 + x_mm * s1, h1 / 2 - y_mm * s1

    def rect_px(cx, cy, w, h):
        x0, y0 = to_px(cx - w / 2, cy + h / 2)
        return [round(x0, 2), round(y0, 2), round(w * s1, 2), round(h * s1, 2)]

    ax, ay, aw, ah = cs.board_rect(cs.AA)
    keys = []
    for name, bx, by, w, h in cs.keys():
        cx, cy = cs.cad(bx, by)
        k = KL.BY_NAME[name]
        keys.append({"name": name, "group": cs.key_group(name), "rect": rect_px(cx, cy, w, h),
                     "legend": k["legend"], "shift": k["shift"] or "", "alpha": k["alpha"] or ""})
    mapping = {
        "image": {"width": w1, "height": h1, "scale_px_per_mm": round(s1, 5),
                  "files": {"1x": "calc_1x.webp", "2x": "calc_2x.webp"}},
        "note": "rects are [x, y, w, h] in 1x image pixels (top-left origin); 2x = double",
        "case_mm": [round(cs.OUT_W, 3), round(cs.OUT_H, 3)],
        "display": {"rect": rect_px(ax, ay, aw, ah), "pixels": [320, 240],
                    "viewing_area": rect_px(*cs.board_rect(cs.VA)),
                    # CAD rectangles [cx, cy, w, h] in mm (origin case centre, Y up): rect = active_mm
                    # at scale_px_per_mm, viewing_area = viewing_mm (the window, case.VA)
                    "active_mm": [round(v, 4) for v in (ax, ay, aw, ah)],
                    "viewing_mm": [round(v, 4) for v in cs.board_rect(cs.VA)]},
        "keys": keys,
    }
    with open(os.path.join(args.out, "keymap.json"), "w") as f:
        json.dump(mapping, f, indent=1, ensure_ascii=False)

    im1.save(os.path.join(args.out, "calc_1x.webp"), "WEBP", quality=90, method=6)
    im2.save(os.path.join(args.out, "calc_2x.webp"), "WEBP", quality=88, method=6)

    dbg = im1.convert("RGBA")
    bg = Image.new("RGBA", dbg.size, (255, 255, 255, 255))
    bg.alpha_composite(dbg)
    d = ImageDraw.Draw(bg)
    x, y, w, h = mapping["display"]["rect"]
    d.rectangle([x, y, x + w - 1, y + h - 1], outline=(255, 0, 255), width=1)
    x, y, w, h = mapping["display"]["viewing_area"]
    d.rectangle([x, y, x + w - 1, y + h - 1], outline=(0, 200, 255), width=1)
    for k in keys:
        x, y, w, h = k["rect"]
        d.rectangle([x, y, x + w - 1, y + h - 1], outline=(255, 40, 40), width=1)
    bg.convert("RGB").save(os.path.join(args.out, "debug_overlay.png"))
    for n in ("calc_1x.webp", "calc_2x.webp", "keymap.json"):
        print(f"{n}: {os.path.getsize(os.path.join(args.out, n))} bytes")
    print(f"image {w1}x{h1}, {s1:.4f} px/mm, display rect {mapping['display']['rect']}")


if __name__ == "__main__":
    main()
