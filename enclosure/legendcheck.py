#!/usr/bin/env python3
"""Legend printability check and legend test strip.

Every printed legend (the keycaps'; the top shell's SHIFT/ALPHA labels are on the
laser-printed overlay, overlay.py) is inlaid two layers deep, so the 0.4 mm nozzle draws
each stroke (in the legend colour) and each gap and letter hole (in the base colour). For every
keycap legend, at the size it is placed at, this reports its
font size and ink box (mm), the widest bead that still draws 95 % of it (its stroke width,
compare MIN_STROKE), and:
  ink   the share of the legend narrower than a bead: what a morphological opening with a
        disc of that width removes (a sharp corner alone costs a few per cent);
  gap   the area of gaps and letter holes narrower than a bead, as a share of the ink (a
        closing with the same disc).
at 0.34 mm (the narrowest bead the slicer's variable-width walls lay down) and at 0.4 mm.
Legends with more than WARN_INK of ink or WARN_GAP of gap below 0.34 mm are flagged, and so are legends
with more than WARN_GAP_PRINT of gap below 0.7 mm: calibrated on the printed loose keys (PETG, Arachne,
two-layer inlay): every legend that came out closed up is over it (the 3 mm Arial Black words, 0.14-0.34)
and every legend under it read cleanly (CAS 0.10, on 0.11, log 0.05). It errs cautious: ln (0.14) and F5
(0.13) printed cleanly too, while vars (0.14) did not.

`--strip` also writes out/legend_test_{base,legend}.stl: a 1.2 mm plate printed legend-up like
the keys (the legend in its last two layers), with the thinnest keycap legends at their
real sizes. Print it in two colours (about 10 minutes) before a key plate to check the colour
swap and how the legends come out.

Usage: legendcheck.py [--strip]
"""
import os
import sys

from build123d import Align, Box, Pos, export_stl

import case

WARN_INK, WARN_GAP = 0.10, 0.10
WARN_GAP_PRINT, BEAD_PRINT = 0.12, 0.7   # gaps narrower than BEAD_PRINT close up in print
# legends over a limit that have printed cleanly on keys (the limits are cautious; DIN tolerates more than
# Arial Black): reported, not flagged
PROVEN = {"MATH", "MENU", "F5"}
STRIP_T = 1.2           # plate thickness (six layers; the legend fills the first two)


def thin(geom, w):
    """(ink lost, gap filled) of shapely `geom` for a bead `w` mm wide, as shares of its area."""
    r = w / 2
    opened = geom.buffer(-r, join_style=1).buffer(r, join_style=1)
    closed = geom.buffer(r, join_style=1).buffer(-r, join_style=1)
    return geom.difference(opened).area / geom.area, closed.difference(geom).area / geom.area


def legends():
    """Every (text, size, valign) the parts use, without repeats, in the order made."""
    case.PLACED.clear()
    for name, _, _, w, h in case.keys():
        case.cap_legend(name, w, h)
    seen, out = set(), []
    for item in case.PLACED:
        if item not in seen:
            seen.add(item)
            out.append(item)
    return out


def stroke(geom, keep=0.95):
    """Widest bead (0.02 mm steps) whose opening keeps `keep` of the ink: the legend's stroke."""
    w = 0.2
    while thin(geom, w + 0.02)[0] < 1 - keep:
        w += 0.02
    return w


def report():
    """Print the table; returns [(score, text, size, valign, ...)] worst first."""
    rows = []
    for text, size, valign, font in legends():
        g = case.text_geom(text, size, valign=valign, font=font)
        i34, g34 = thin(g, 0.34)
        i40, g40 = thin(g, 0.40)
        x0, y0, x1, y1 = g.bounds
        g70 = thin(g, BEAD_PRINT)[1]
        rows.append((max(i34 / WARN_INK, g34 / WARN_GAP, g70 / WARN_GAP_PRINT), text, size, valign, i34, g34, i40,
                     g40, x1 - x0, y1 - y0, stroke(g), g70, font))
    rows.sort(key=lambda r: -r[0])
    print(f"{'legend':24s} {'size':>5s} {'box (mm)':>12s} {'stroke':>6s}   ink/gap @0.34   ink/gap @0.40   gap @0.7")
    for score, text, size, valign, i34, g34, i40, g40, bw, bh, st, g70, _ in rows:
        flag = ("  (over, printed cleanly)" if text in PROVEN else "  <-- thin") if score > 1 else ""
        print(f"{text!r:24s} {size:5.2f} {bw:5.2f} x {bh:4.2f} {st:6.2f}   {i34:5.2f} {g34:5.2f}     "
              f"{i40:5.2f} {g40:5.2f}     {g70:5.2f}{flag}")
    bad = sum(1 for r in rows if r[0] > 1 and r[1] not in PROVEN)
    cap_set = set(case.cap_legend_sizes().values())
    caps = [r for r in rows if (r[1], r[2], r[3]) in cap_set]
    print(f"{len(rows)} legends, {bad} flagged (ink > {WARN_INK:.0%} or gap > {WARN_GAP:.0%} at 0.34 mm, "
          f"gap > {WARN_GAP_PRINT:.0%} at {BEAD_PRINT} mm); "
          f"keycap legends: font {min(r[2] for r in caps):.2f}-{max(r[2] for r in caps):.2f} mm, "
          f"strokes >= {min(r[10] for r in caps):.2f} mm (MIN_STROKE {case.MIN_STROKE})")
    return rows


def strip(rows, n=14):
    """Legend test strip: the n thinnest keycap legends plus a few reference ones, in rows of up
    to 80 mm, legend-up like the keys (the legend in the top INLAY). Returns (base, legend)."""
    cap_set = set(case.cap_legend_sizes().values())
    caps = sorted([r for r in rows if (r[1], r[2], r[3]) in cap_set], key=lambda r: r[10])
    pick = [(r[1], r[2], r[3], r[-1]) for r in caps[:n]]
    for ref in [r for r in caps if r[1] in ("ENT", "SIN", "7", "F1")]:
        if (ref[1], ref[2], ref[3], ref[-1]) not in pick:
            pick.append((ref[1], ref[2], ref[3], ref[-1]))
    parts, x, y, row_h = [], 0.0, 0.0, 0.0
    for text, size, valign, font in pick:
        x0, y0, x1, y1 = case.text_geom(text, size, halign=Align.MIN, valign=valign, font=font).bounds
        if x + x1 - x0 > 80:
            x, y, row_h = 0.0, y - row_h - 2.0, 0.0
        g = case.text2d(text, size, halign=Align.MIN, valign=valign, font=font)
        parts.append(Pos(x - x0, y - y1, STRIP_T - case.INLAY) * case.extrude(g, case.INLAY))
        x += x1 - x0 + 2.5
        row_h = max(row_h, y1 - y0)
    legend = parts[0].fuse(*parts[1:]) if len(parts) > 1 else parts[0]
    bb = legend.bounding_box()
    plate = Pos((bb.min.X + bb.max.X) / 2, (bb.min.Y + bb.max.Y) / 2, STRIP_T / 2) * \
        Box(bb.max.X - bb.min.X + 6, bb.max.Y - bb.min.Y + 6, STRIP_T)
    return plate.cut(legend), legend


def main(argv):
    rows = report()
    if "--strip" in argv:
        base, legend = strip(rows)
        export_stl(base, os.path.join(case.OUT, "legend_test_base.stl"))
        export_stl(legend, os.path.join(case.OUT, "legend_test_legend.stl"))
        print("wrote out/legend_test_base.stl, out/legend_test_legend.stl")


if __name__ == "__main__":
    main(sys.argv[1:])
