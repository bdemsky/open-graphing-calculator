#!/usr/bin/env python3
"""Alignment check: paste an LCD screenshot into the display rectangle of the
calculator picture, using the same keymap.json the page uses.

  composite.py SCREEN.png OUT.png [--scale 2]

SCREEN.png is a 320 x 240 framebuffer image (e.g. from the simulator); it is scaled with
nearest-neighbour to keymap.json's display rect (LCD active area) at the chosen picture
scale and flattened onto the page background colour. `make web-test` uses it to produce
web/test/composite_graph.png, a visual check that render_calc.py's rectangles line up.
"""
import argparse
import json
import os

from PIL import Image

ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("screen")
    ap.add_argument("out")
    ap.add_argument("--scale", type=int, default=2, choices=(1, 2))
    a = ap.parse_args()
    km = json.load(open(os.path.join(ASSETS, "keymap.json")))
    img = Image.open(os.path.join(ASSETS, km["image"]["files"][f"{a.scale}x"])).convert("RGBA")
    x, y, w, h = (v * a.scale for v in km["display"]["rect"])
    # same placement rule as a CSS box at fractional pixels: cover [x, x+w) x [y, y+h)
    x0, y0, x1, y1 = round(x), round(y), round(x + w), round(y + h)
    lcd = Image.open(a.screen).convert("RGBA").resize((x1 - x0, y1 - y0), Image.NEAREST)
    img.alpha_composite(lcd, (x0, y0))
    bg = Image.new("RGBA", img.size, (233, 236, 241, 255))
    bg.alpha_composite(img)
    bg.convert("RGB").save(a.out)
    print(f"wrote {a.out}: screen at ({x0},{y0})-({x1},{y1}) in a {img.size[0]}x{img.size[1]} picture")


if __name__ == "__main__":
    main()
