#!/usr/bin/env python3
"""JLCPCB fabrication + assembly outputs in hardware/fab/:

  calc-gerbers.zip   Gerbers + Excellon drill (upload as the PCB)
  calc-bom.csv       Comment, Designator, Footprint, LCSC Part #
  calc-cpl.csv       Designator, Mid X, Mid Y, Layer, Rotation

Only parts JLC places go in the BOM/CPL: parts without an LCSC number (test
points, solder jumpers) are left out and listed in hand_parts.txt.

Inputs: the routed kicad/calc.kicad_pcb and build/calc.net (for each part's
LCSC field and footprint id). Run by hand after the board is final; all the
exporting is done by kicad-cli. Gerbers and drill files use KiCad's absolute
origin, as does the position file, so JLC's placement overlay lines up.

CPL rotation convention (the error-prone part): JLC's rotation is relative to
its own EasyEDA part model, not to the KiCad footprint. For each part

    top:     rot = kicad_rot + ROTATION_FIX[footprint]
    bottom:  rot = (180 - kicad_rot) + ROTATION_FIX[footprint]

(mod 360). Pin headers are additionally re-anchored from pin 1 (KiCad) to the
centre of their pads (JLC).
"""
import csv
import os
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from kicad_pcb import load_netlist  # noqa: E402

KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
BOARD = os.path.join(HERE, "../kicad/calc.kicad_pcb")
FAB = os.path.join(HERE, "../fab")
LAYERS = "F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts"

# JLC's part models are not always oriented like KiCad's footprints. Corrections
# (degrees, added to the KiCad rotation) are checked against JLC's placement
# preview before ordering; keep this table in sync with what that preview shows.
# Keyed by the netlist footprint lib id. Derived by matching each KiCad footprint's
# pads against the EasyEDA/JLC footprint of the part's LCSC number (easyeda2kicad).
# calc:* footprints came from EasyEDA, so they are already JLC-oriented (0).
ROTATION_FIX = {
    "Package_QFP:LQFP-144_20x20mm_P0.5mm": 270,      # U1: JLC pin 1 bottom-left, row along bottom
    "Package_SO:SOIC-8_5.3x5.3mm_P1.27mm": 270,      # U3
    "Package_TO_SOT_SMD:SOT-23": 180,                # Q1-Q8 (AO3401A/AO3400A)
    "Package_TO_SOT_SMD:SOT-23-5": 180,              # U7 TLV9001
    "Package_TO_SOT_SMD:SOT-23-6": 270,              # U8 USBLC6-2SC6
    "LED_SMD:LED_0603_1608Metric": 180,              # D2: JLC pad 1 (cathode) is on +X
    "Crystal:Crystal_SMD_3215-2Pin_3.2x1.5mm": 180,  # Y2: non-polar, keeps preview pad numbers aligned
    # Pin headers (JLC through-hole assembly): KiCad's rows run along y from pin 1, JLC's
    # EasyEDA footprints (C492405, C22438120, C492423) run along x, centred. Unpolarized parts,
    # so 90 and 270 are equivalent.
    "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical": 90,
    "Connector_PinHeader_1.27mm:PinHeader_2x05_P1.27mm_Vertical": 90,
    "Connector_PinHeader_2.54mm:PinHeader_2x06_P2.54mm_Vertical": 90,
}


def pad_centroid(ref):
    """Centre of a footprint's through-hole pads in the pos-file frame (y up), from the board.

    Reads the footprint block of ref from the saved board text, averages its
    thru_hole pad positions (footprint-local), and rotates/translates that
    point into board coordinates; returns (x, y) in mm with y negated.
    """
    import math
    import re
    t = open(BOARD).read()
    i = t.index(f'(property "Reference" "{ref}"')
    s = t.rindex("(footprint ", 0, i)
    e = t.find("\n\t(footprint ", i)
    blk = t[s:e if e > 0 else len(t)]
    at = re.search(r"\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)", blk)
    fx, fy, rot = float(at.group(1)), float(at.group(2)), float(at.group(3) or 0)
    pts = [(float(m.group(1)), float(m.group(2))) for m in
           re.finditer(r'\(pad "[^"]*" thru_hole \w+\s+\(at ([-\d.]+) ([-\d.]+)', blk)]
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    a = math.radians(rot)   # KiCad: board = at + R(-rot) * local (y down)
    bx = fx + cx * math.cos(a) + cy * math.sin(a)
    by = fy - cx * math.sin(a) + cy * math.cos(a)
    return bx, -by          # pos files use y up


def run(*args):
    """Run kicad-cli with args; raises on failure."""
    subprocess.run([KICAD_CLI, *args], check=True, capture_output=True)


def main():
    """Write the Gerber zip, CPL, BOM and hand-parts list into hardware/fab/."""
    os.makedirs(FAB, exist_ok=True)
    gdir = os.path.join(FAB, "gerbers")
    os.makedirs(gdir, exist_ok=True)
    for f in os.listdir(gdir):   # start clean so stale layers never end up in the zip
        os.remove(os.path.join(gdir, f))
    # no X2 attributes (plain RS-274X) and mask subtracted from silkscreen; plated and
    # non-plated holes in separate Excellon files
    run("pcb", "export", "gerbers", "--layers", LAYERS, "--subtract-soldermask", "--no-x2",
        "-o", gdir + "/", BOARD)
    run("pcb", "export", "drill", "--format", "excellon", "--drill-origin", "absolute",
        "--excellon-units", "mm", "--excellon-separate-th", "-o", gdir + "/", BOARD)
    with zipfile.ZipFile(os.path.join(FAB, "calc-gerbers.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(gdir)):
            z.write(os.path.join(gdir, f), f)

    comps, _ = load_netlist(os.path.join(HERE, "../build/calc.net"))
    pos_csv = os.path.join(FAB, "pos.csv")
    # Same absolute frame as the Gerbers/drill (no aux origin), so JLC's overlay lines up.
    run("pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both",
        "-o", pos_csv, BOARD)
    rows = list(csv.DictReader(open(pos_csv)))
    placed, hand = [], []
    for r in rows:
        ref = r["Ref"]
        c = comps.get(ref)
        if not c:
            continue
        lcsc = c["fields"].get("LCSC", "")
        if not lcsc:
            hand.append(f"{ref}\t{c['value']}\t{lcsc or '-'}\t{r['Side']}")
            continue
        placed.append((r, c, lcsc))
    with open(os.path.join(FAB, "calc-cpl.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r, c, lcsc in placed:
            rot = float(r["Rot"])
            if r["Side"] != "top":
                # KiCad reports the board-file angle (seen from the top) for flipped parts;
                # JLC wants the angle seen from the bottom (X mirrored): 180 - angle. The
                # library correction is then added unchanged. X/Y stay un-mirrored.
                rot = 180 - rot
            rot = (rot + ROTATION_FIX.get(c["footprint"], 0)) % 360
            x, y = float(r["PosX"]), float(r["PosY"])
            if c["footprint"].startswith("Connector_PinHeader"):
                x, y = pad_centroid(r["Ref"])   # KiCad anchors pin headers at pin 1; JLC uses the centre
            w.writerow([r["Ref"], f"{x:.3f}mm", f"{y:.3f}mm",
                        "Top" if r["Side"] == "top" else "Bottom", f"{rot:g}"])
    groups = {}
    for r, c, lcsc in placed:
        key = lcsc  # one BOM line per LCSC part (the 52 keys were 52 lines before)
        groups.setdefault(key, []).append(r["Ref"])
    with open(os.path.join(FAB, "calc-bom.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for lcsc, refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
            c = comps[refs[0]]
            # mixed values under one LCSC part (the 52 key switches): use the part name
            mixed = len({comps[x]["value"] for x in refs}) > 1
            value = (c["fields"].get("MPN") or c["footprint"].split(":")[-1]) if mixed else c["value"]
            w.writerow([value, ",".join(refs), c["footprint"].split(":")[-1], lcsc])
    with open(os.path.join(FAB, "hand_parts.txt"), "w") as f:
        f.write("ref\tvalue\tLCSC\tside\n" + "\n".join(hand) + "\n")
    os.remove(pos_csv)
    print(f"gerbers: {len(os.listdir(gdir))} files; BOM {len(groups)} lines / {len(placed)} placements; "
          f"hand/unplaced: {len(hand)}")


if __name__ == "__main__":
    main()
