#!/usr/bin/env python3
"""Reference views of the assembled board, for checking photos of a real board (BRINGUP.md Phase 0).

  assembly_views.py [--out DIR] [--dpi N] [--no-render]

For each side it writes:
  {top,bottom}_silk.png         solder-mask openings (= pads) + silkscreen (designators, pin-1 marks,
                                polarity marks) + outline, as the bare board looks; bottom is mirrored
                                (seen from below, like a photo of the back)
  {top,bottom}_<region>.png     the same, cropped to the regions to photograph close up
  {top,bottom}_3d.png           KiCad's 3D render (parts with their 3D models), unless --no-render
The SVGs are kept next to them. Positions and rotations are also in hardware/fab/calc-cpl.csv.

Needs kicad-cli (KiCad 8+; $KICAD_CLI or /Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli)
and PyMuPDF to rasterise the SVGs (the repo's .venv has it: ../../.venv/bin/python). Without
PyMuPDF only the SVGs are written. Every external command has a timeout.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
PCB = os.path.join(REPO, "hardware", "kicad", "calc.kicad_pcb")
BOARD_W, BOARD_H = 82.0, 188.8  # mm, from the Edge.Cuts outline (origin at the top-left corner)

# Close-up regions in board mm (x0, y0, x1, y1), measured from the top-left corner seen from the
# top (USB-C / header edge at the top). Bottom regions use the same top-view coordinates and are
# mirrored here, so they match a photo of the back.
REGIONS = {
    "top": {
        "headers_usb": (0, 0, 82, 16),
        "power": (0, 8, 38, 46),
        "testpads_j3": (8, 28, 52, 64),
        "fets_backlight": (0, 48, 40, 72),
        "mcu_sdram": (38, 14, 82, 74),
        "keypad": (0, 74, 82, 188.8),
    },
    "bottom": {
        "decoupling_mcu": (48, 20, 76, 48),
        "decoupling_sdram": (48, 50, 74, 68),
        "battery_holders": (0, 112, 82, 182),
    },
}


def kicad_cli():
    for c in (os.environ.get("KICAD_CLI"), shutil.which("kicad-cli"),
              "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"):
        if c and os.path.exists(c):
            return c
    sys.exit("kicad-cli not found (install KiCad 8+ or set $KICAD_CLI)")


def run(argv, timeout):
    print("$ " + " ".join(argv), flush=True)
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    except subprocess.TimeoutExpired:
        print(f"  timed out after {timeout} s")
        return False
    if p.returncode:
        print(p.stdout.decode("utf-8", "replace")[-2000:])
    return p.returncode == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(REPO, "firmware", "build", "assembly-views"))
    ap.add_argument("--dpi", type=int, default=400, help="raster resolution of the silk views (400 = ~16 px/mm)")
    ap.add_argument("--no-render", action="store_true", help="skip the 3D renders")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    k = kicad_cli()
    try:
        import pymupdf
    except ImportError:
        try:
            import fitz as pymupdf
        except ImportError:
            pymupdf = None
            print("PyMuPDF not available: writing SVGs only (run with the repo's .venv/bin/python for PNGs)")
    ok = True
    mm = 72.0 / 25.4
    for side, F in (("top", "F"), ("bottom", "B")):
        svg = os.path.join(a.out, f"{side}_silk.svg")
        argv = [k, "pcb", "export", "svg", "--layers", f"{F}.Mask,{F}.SilkS,Edge.Cuts", "--mode-single",
                "--fit-page-to-board", "--exclude-drawing-sheet", "-o", svg, PCB]
        if side == "bottom":
            argv.insert(-1, "--mirror")
        ok &= run(argv, 120)
        if pymupdf and os.path.exists(svg):
            page = pymupdf.open(svg)[0]
            sx, sy = page.rect.width / (BOARD_W * mm), page.rect.height / (BOARD_H * mm)
            page.get_pixmap(dpi=a.dpi).save(os.path.join(a.out, f"{side}_silk.png"))
            for name, (x0, y0, x1, y1) in REGIONS[side].items():
                if side == "bottom":  # mirrored about the vertical centre line
                    x0, x1 = BOARD_W - x1, BOARD_W - x0
                clip = pymupdf.Rect(x0 * mm * sx, y0 * mm * sy, x1 * mm * sx, y1 * mm * sy)
                page.get_pixmap(dpi=a.dpi * 2, clip=clip).save(os.path.join(a.out, f"{side}_{name}.png"))
        if not a.no_render:
            ok &= run([k, "pcb", "render", "--side", side, "--quality", "basic", "-w", "1600", "-h", "3684",
                       "--background", "opaque", "-o", os.path.join(a.out, f"{side}_3d.png"), PCB], 300)
    print("wrote " + ", ".join(sorted(f for f in os.listdir(a.out) if f.endswith((".png", ".svg")))))
    print(f"in {a.out}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
