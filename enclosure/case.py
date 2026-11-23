#!/usr/bin/env python3
"""Calculator enclosure, loose keys and battery door (build123d).

Geometry comes from the PCB layout (hardware/gen/layout.py) so the board,
holes, display window and keys always agree. Coordinates: X right, Y up
(= -board y), Z up with the PCB top surface at Z = 0.

Inputs: hardware/gen/design.py + layout.py (board outline, mounting holes, panel and
key positions; layout.build() also needs hardware/build/calc.net for the placed parts
J2/J4/J5/J6/BT1/BT2), and firmware/keys/layout.py (keycap legends, SHIFT/ALPHA
labels for the overlay, keycap colour groups and filament colours).

Outputs (enclosure/out/), see main(). The default build writes the tested set:
  print files   top_shell_base.stl (the front: one colour, face-down; no printed legends: the
                SHIFT/ALPHA labels are an image transfer of a laser print, overlay.py),
                back_shell_base.stl (floor on the bed, which is also the assembled position),
                battery_door.stl (plate on the bed, also as assembled),
                keys_loose_{all,test,rest,single}_{base,legend}.stl (the loose keys: solid caps
                legend-up, the legend a second colour; loose_keys()), {top,back}_shell_ears.stl
                (optional anti-warp discs, load with the shell)
  artwork       overlay_{transfer,proof}.{pdf,svg} (overlay.py, 1:1 on US Letter)
  render files  top_shell.stl, render_*.stl in assembled position (render_overlay.stl is the
                overlay sheet, render_top_{shift,alpha}.stl its SHIFT/ALPHA labels,
                render_keys_<group>[_legend].stl the keys; used by enclosure/render.py and
                firmware/web/tools/render_calc.py)
  fit check     case_assembly.step (all parts assembled)
Options build only their own files (main()): --loose [--pick NAMES] the key plates, --loose-gaps
and --word-test the key tests, --door the battery door, --rev2 the rev2 back shell and door,
--clips / --low [--drop D] / --bar-clips the front variants of V3_THIN.md, --catch-test the
corner-catch test pieces.

Stack-up (mm above PCB top): switch plunger 1.5, folded display FPC ~3.0-4.37,
panel back 4.37 (snap-finger seats 4.22), panel front and shell ceiling 8.47 (CEIL; VHB
tape sits in a 0.25 mm recess above it), shell top 10.07 (TOP), keycap tops 11.27; loose keys
2.0-11.27 (LOOSE_GAP above the plunger) with their flanges against the ceiling. Below: PCB bottom
-1.6, holders to -12.99, back-shell floor inside -13.8, outside -15.4.

Printer constraints (Neptune 4 Pro, 0.4 mm nozzle, 0.2 mm layers, FDM): walls and plates are
multiples of nozzle widths / layer heights (WALL 2.0, PLATE 1.6, FLOOR 1.6, INLAY 2 layers);
every part is oriented so it prints without supports (shells face-down, keys legend-up,
door plate-down; printcheck.py checks); multi-colour legends (keycaps) are separate
solids that occupy the top INLAY of the surface and are subtracted from the body, so a
single-extruder printer can swap filament per layer. The top shell's face has no legends:
small text inlaid in the first layers of a face-down part prints as dots and smears, so the
SHIFT/ALPHA labels are on a laser-printed overlay stuck to the face (overlay.py).
Everything prints in PETG. The display snap fingers bend across the layer lines, PETG's weak
direction, so they are tapered with root fillets (finger_strain() 1.2 %). The battery door
(printed plate-down) snaps shut with a latch band in its plate that bends in the plate's own
plane, along the layers (latch_strain() 1.2 %); its hinge tabs and hooks have 0.4-0.6 mm
clearances, and a fingernail in the shell's pry notch pushes the band to open it. The screwless
top-right corner is held by two snap catches: leaves in the back shell's wall, bending across the
layers (catch_numbers() 0.8 %), click into undercuts in the front's wall. Bed-facing
edges and openings have 45-degree elephant-foot chamfers, small holes HOLE_COMP over size;
bosses and standoffs are 7 mm with root fillets and are tied to the walls
(enclosure/DFM_REVIEW.md).
"""
import functools
import math
import os
import sys

import numpy as np
from build123d import (Align, Axis, Box, Cylinder, Face, Locations, Mode, Plane, Polygon, Pos, Rot,
                       Sketch, Text, Wire, export_step, export_stl, fillet, Compound)
from build123d import BuildPart, BuildSketch, RectangleRounded, extrude, Rectangle, add

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "../hardware/gen"))
import design  # noqa: E402
import layout  # noqa: E402
import importlib.util  # noqa: E402

# Logical keyboard layout (legends, SHIFT/ALPHA functions, colour groups): firmware/keys/layout.py.
# Loaded by path: its module name clashes with hardware/gen/layout.py (the PCB placement).
_spec = importlib.util.spec_from_file_location("keys_layout", os.path.join(HERE, "../firmware/keys/layout.py"))
keys_layout = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(keys_layout)
keys_layout.check(design.LAYOUT)

OUT = os.path.join(HERE, "out")

# ------------------------------------------------------------ parameters ---
W, H, PCB_R, PCB_T = layout.W, layout.H, 4.0, 1.6
GAP = 0.5            # PCB-to-wall clearance (PETG shrinks ~0.3 %: 0.6 mm over the 194 mm shell)
WALL = 2.0
PLATE = 1.6          # top plate thickness
PANEL_T = 4.1        # EastRising ER-TFT032IPS-3.2 + ER-TPC032-3.1 CTP: 4.1 +-0.2 total
PANEL_BACK = 4.37    # panel back above the PCB; the folded FPC (touch IC side down) lies under it
CEIL = PANEL_BACK + PANEL_T   # 8.47, inside of the top plate = touch-glass front face
TOP = CEIL + PLATE   # 10.07
HEADER_POCKET_TOP = 9.05   # underside pockets over the header pins (tips at 8.54)
PANEL_CLR = 0.35     # panel pocket clearance per side (outline tolerance +-0.2)
TAPE = 0.25          # VHB tape (e.g. 3M 5909, 0.25 mm) between the bezel ledge and the touch glass
FLOOR_IN = -13.8
FLOOR = 1.6
BOT = FLOOR_IN - FLOOR
OUT_W, OUT_H = W + 2 * (GAP + WALL), H + 2 * (GAP + WALL)
OUT_R = PCB_R + GAP + WALL
KEY_CLR = 0.25       # keycap-to-hole clearance per side (the loose keys are also CAP_SLIM slimmer there)
RABBET = 1.2         # shells overlap this far: top shell's outer wall lap over the back's tongue
RAB_KEEP = 0.9       # back-shell tongue (inner part of its wall inside the lap): 2 perimeters
RAB_CLR = 0.15       # tongue-to-lap clearance per side (PETG slip fit); the lap is 0.95 mm
SEAM_GAP = 0.2       # the shells don't touch in Z (the PCB is clamped between boss and standoff)
INLAY = 0.4          # legend inlay depth = two 0.2 mm layers in the second colour
INSERT_D, INSERT_L = 3.2, 4.5   # hole for M2 x 3 mm heat-set inserts (OD ~3.5; vendor bore 3.2)
BOSS_R = 3.5         # top-shell insert bosses (7 mm: 1.9 mm of wall around the insert hole)
BOSS_LOW_R = 3.0     # the two short bosses under the ON / ENTER flanges (loose_cap() clears them)
BOSS_LOW = 6.8       # bosses under keycap flanges stop here (flange bottom 7.42 - travel - margin)
ROOT = 1.0           # 45-degree root fillet (cone) where bosses and standoffs meet plate or floor
STANDOFF_R = 3.5     # back-shell standoffs (7 mm), fused to the PCB ledge and wall, with gussets
GUSSET_T, GUSSET_L, GUSSET_H = 1.2, 3.0, 6.0    # standoff gussets: thickness, reach, height
# Print tolerances (PETG, 0.4 mm nozzle): round holes print ~0.1-0.2 mm under size, so small
# holes get HOLE_COMP on the diameter; bed-facing edges get an elephant-foot chamfer.
HOLE_COMP = 0.2
SCREW_CLR_D = 2.2 + HOLE_COMP   # M2 clearance bore (2.4 as modelled)
CBORE_D, CBORE_H = 4.0 + HOLE_COMP, 2.2   # M2 socket head 3.8 x 2.0: seats 0.2 below the floor
EF = 0.4             # elephant-foot chamfer on bed-facing hole and recess edges
EDGE_CH = 0.6        # 45-degree chamfer on the face edge (top) and the floor edge (back)
SEAM_CH = 0.3        # chamfers on both sides of the shell seam: a crisp V reveal hides any step
KEY_CH = 0.25        # 45-degree chamfer round each key opening (EF relief; KEY_CLR + KEY_CH = the old 0.5 label room)
WINDOW_R, WINDOW_CH = 0.5, 1.0   # display window corner radius and its face bevel

# Face artwork (overlay.py): the SHIFT/ALPHA labels are a gel-medium image transfer of a colour
# laser print, made straight onto the white face (no sheet, no pocket: the face stays flat, the
# bed side of the face-down print). OVERLAY_INSET bounds the area the labels may use (inside
# the face's edge chamfer); the paper is aligned by eye on marks round and inside the front.
OVERLAY_INSET = 1.0      # artwork area inside the shell's outer edge, per side
PAPER_MARGIN = 8.0       # transfer paper beyond the shell outline on every side (corner L marks, crop marks)
MIN_SKIN = 0.6           # thinnest plate skin left over the underside pockets (finger slots, headers)

# Keys: main-grid caps 12.6 x 7.6 mm (the 11.15 mm row pitch leaves 3.05 mm webs for the
# labels; the height was set by the earlier spring key mats, and the keys and artwork kept it);
# rows A and B use the same height. The 7.6 mm holds the 5 mm digits with 1.1 mm margins, the
# width the four-letter words. Caps have CAP_R corners, the openings
# CAP_R + KEY_CLR and a KEY_CH x 45-degree chamfer around their face edge (the opening is on
# the bed: the chamfer takes up the elephant foot, and the even bevel makes the gap around
# every cap read as one uniform line).
CAP_R = 0.8              # keycap corner radius (openings: + KEY_CLR = 1.05)


FONT_MAIN = "/System/Library/Fonts/Supplemental/Arial Black.ttf"
FONT_SYMBOLS = "/System/Library/Fonts/Apple Symbols.ttf"   # glyphs Arial Black lacks (⌫, ␣)
SYMBOL_SCALE = 1.6     # Apple Symbols draws ⌫ / ␣ small for their em size

# Legends are inlaid two layers deep (the keycaps' last two), so
# every stroke and every gap is drawn by the 0.4 mm nozzle. The slicer's variable-width walls
# (Arachne) can lay a bead down to about 0.34 mm; anything narrower drops out or smears. The
# keycap legends are large (cap_legend_sizes) and clear this easily; for smaller text, Arial
# Black's math signs (√ ∞ →) and small superscripts are thin: each glyph
# with a stroke, joint or diagonal thinner than MIN_STROKE is thickened (grown on all sides,
# up to MAX_GROW, see _grown) and the letter spacing grows with it; a glyph whose gaps are
# narrower than MIN_GAP (Arial Black's E, S, M at legend sizes) is slimmed a little instead. legendcheck.py measures the
# result.
MIN_STROKE = 0.36      # mm, bead every stroke must hold (a little over Arachne's 0.34 mm)
STROKE_EM = 0.09       # large legends: strokes are also grown to this share of the font size (the
                       # thin diagonal of a 5 mm √ to 0.45 mm), so they stay well above MIN_STROKE
MAX_GROW = 0.10        # mm per side, most a glyph with letter holes is thickened (more closes them)
MAX_GROW_SOLID = 0.16  # the same for glyphs without holes (_ √ ∞ →)
MAX_SLIM = 0.05        # mm per side, most a glyph with narrow gaps is slimmed (E S M B)
MIN_GAP = 0.34         # gaps and letter holes: the narrowest base-colour bead
TRACKING = 0.07        # extra letter spacing, in font sizes
SUP_SCALE = 0.65       # superscripts: 65 % size, raised 30 %
_fonts = {}            # font path -> (units per em, hmtx, cmap)
_glyphs = {}           # (char, font path) -> shapely outline at size 1, baseline at y = 0


def _font(path):
    if path not in _fonts:
        from fontTools.ttLib import TTFont
        f = TTFont(path)
        _fonts[path] = (f["head"].unitsPerEm, f["hmtx"], f.getBestCmap())
    return _fonts[path]


def _has_glyph(ch, font=None):
    """True if `font` (default FONT_MAIN, Arial Black) has a glyph for `ch`. Characters it lacks are
    drawn from FONT_SYMBOLS by text2d()."""
    return ord(ch) in _font(font or FONT_MAIN)[2]


def _wire_pts(wire, step=0.02):
    pts = []
    for e in wire.edges():
        for t in np.linspace(0, 1, max(2, int(e.length / step)), endpoint=False):
            p = e.position_at(t)
            pts.append((p.X, p.Y))
    return pts


def _glyph(ch, path):
    """Outline of `ch` in font `path` as a shapely geometry at size 1, the pen at (0, 0) on the
    baseline. Drawn by build123d at size 10 (its Text puts the baseline at a fixed offset,
    measured from 'H') and sampled every 0.002 em."""
    if (ch, path) not in _glyphs:
        from shapely.geometry import Polygon
        from shapely.ops import unary_union
        from shapely import affinity
        upm, hmtx, cmap = _font(path)
        base = Text("H", 10, font_path=path, align=(Align.MIN, None)).bounding_box().min.Y
        g = Text(ch, 10, font_path=path, align=(Align.MIN, None))
        lsb = hmtx[cmap[ord(ch)]][1] / upm * 10 if ord(ch) in cmap else 0.0
        poly = unary_union([Polygon(_wire_pts(f.outer_wire()), [_wire_pts(w) for w in f.inner_wires()]).buffer(0)
                            for f in g.faces()])
        dx = lsb - poly.bounds[0] if not poly.is_empty else 0.0
        _glyphs[(ch, path)] = affinity.scale(affinity.translate(poly, dx, -base), 0.1, 0.1, origin=(0, 0))
    return _glyphs[(ch, path)]


def _to_sketch(geom):
    """build123d Sketch from a shapely (Multi)Polygon, every face normal +Z (counter-clockwise
    outlines; shapely's own winding would point some faces down, and extrude follows it)."""
    from shapely.geometry.polygon import orient
    faces = []
    for p in getattr(geom, "geoms", [geom]):
        p = orient(p, 1.0)
        outer = Wire.make_polygon([(x, y) for x, y in p.exterior.coords[:-1]], close=True)
        holes = [Wire.make_polygon([(x, y) for x, y in r.coords[:-1]], close=True) for r in p.interiors]
        faces.append(Face(outer, holes))
    return Sketch() + faces


# Unicode legend (firmware/keys/layout.py) -> CAD markup: '^' starts a superscript that runs to
# the end or to the next '_' (back to the baseline). Superscript letters/signs Arial lacks are
# drawn as small raised text; the arrow glyphs use Arial's ◄ ►.
CAD_TEXT = {"xʸ": "x^y", "eˣ": "e^x", "10ˣ": "10^x", "x⁻¹": "x^−1", "sin⁻¹": "sin^−1",
            "cos⁻¹": "cos^−1", "tan⁻¹": "tan^−1", "×10ⁿ": "×10^n", "ʸ√x": "^y_√x", "∛": "^3_√",
            "◀": "◄", "▶": "►"}


def cad_text(legend):
    """CAD markup for a Unicode legend from firmware/keys/layout.py (unchanged if no mapping)."""
    return CAD_TEXT.get(legend, legend)


def _runs(markup):
    """[(text, superscript)] from CAD markup. A legend that is just "^" or "_" is literal text
    (the `markup != ch` test), so e.g. the "_" ALPHA letter on key 3 still prints."""
    out, sup, cur = [], False, ""
    for ch in markup:
        if ch in "^_" and markup != ch:
            if cur:
                out.append((cur, sup))
            cur, sup = "", ch == "^"
        else:
            cur += ch
    if cur:
        out.append((cur, sup))
    return out


def _shape(g):
    """(parts, holes) of a shapely geometry: growing or slimming a glyph must keep both."""
    parts = getattr(g, "geoms", [g])
    return len(parts), sum(len(p.interiors) for p in parts)


@functools.lru_cache(maxsize=None)
def _grown(ch, path, s):
    """(outline, d): glyph `ch` at size `s`, grown (d > 0) or slimmed (d < 0) per side by the d
    in 0.01 mm steps from -MAX_SLIM to MAX_GROW (MAX_GROW_SOLID without letter holes) that
    best keeps both its strokes at least MIN_STROKE (or STROKE_EM * s) wide and its gaps at least MIN_GAP: the
    least of max(share of ink a MIN_STROKE bead misses, share of ink a MIN_GAP gap would fill),
    preferring the smallest change. A d that changes the glyph's parts or holes (_shape) is
    never taken: a merged gap no longer shows up as a narrow one."""
    from shapely import affinity
    g0 = affinity.scale(_glyph(ch, path), s, s, origin=(0, 0))
    holes = any(p.interiors for p in getattr(g0, "geoms", [g0]))
    if path not in (FONT_MAIN, FONT_SYMBOLS):
        # a lighter font (DIN) is used as drawn: fitted glyph by glyph, letters without holes (V S T) would
        # thicken further than those with them (A R O) and a word turn patchy (legendcheck checks it)
        return g0, 0.0
    r, rg = max(MIN_STROKE, STROKE_EM * s) / 2, MIN_GAP / 2
    best = None
    top = int(round((MAX_GROW if holes else MAX_GROW_SOLID) / 0.01))
    for k in sorted(range(-int(round(MAX_SLIM / 0.01)), top + 1), key=abs):
        d = k * 0.01
        g = g0.buffer(d, join_style=1) if d else g0
        if _shape(g) != _shape(g0):
            continue        # the dots of ÷ or i merged, a hole closed or a stroke broke
        ink = g.difference(g.buffer(-r, join_style=1).buffer(r, join_style=1)).area / g.area
        gap = g.buffer(rg, join_style=1).buffer(-rg, join_style=1).difference(g).area / g.area
        score = max(ink, gap)
        if best is None or score < best[0] - 0.01:
            best = (score, g, d)
    return best[1], best[2]


LINE_MID = 0.27        # valign "line": y = 0 this many font sizes above the baseline


@functools.lru_cache(maxsize=None)
def _text_geom(text, size, font=None):
    """Shapely outline of CAD markup `text` at font size `size` (mm) in `font` (default FONT_MAIN), the first pen position at
    (0, 0) on the baseline. Glyphs are placed one by one at their advance widths (plus
    TRACKING); a glyph grown by d per side (see _grown) moves its right neighbour by 2 d."""
    from shapely import affinity
    from shapely.ops import unary_union
    parts, x = [], 0.0
    for run, sup in _runs(text):
        sz = size * SUP_SCALE if sup else size
        dy = size * 0.3 if sup else 0.0
        for ch in run:
            main = _has_glyph(ch, font) or ch == " "
            path, s = (font or FONT_MAIN, sz) if main else (FONT_SYMBOLS, sz * SYMBOL_SCALE)
            upm, hmtx, cmap = _font(path)
            adv = hmtx[cmap[ord(ch)]][0] / upm * s if ord(ch) in cmap else 0.5 * s
            if ch == " ":
                x += adv
                continue
            g, d = _grown(ch, path, round(s, 4))
            parts.append(affinity.translate(g, x + (d if parts else 0.0), dy))
            x += adv + 2 * d + TRACKING * size
    return unary_union(parts).simplify(0.003)


def text_geom(text, size, halign=Align.CENTER, valign="box", font=None):
    """Shapely outline of `text` (see text2d) aligned at (0, 0)."""
    from shapely import affinity
    g = _text_geom(text, size, font)
    x0, y0, x1, y1 = g.bounds
    dx = {Align.MIN: -x0, Align.CENTER: -(x0 + x1) / 2, Align.MAX: -x1}[halign]
    dy = -(y0 + y1) / 2 if valign == "box" else -LINE_MID * size
    return affinity.translate(g, dx, dy)


def text2d(text, size, halign=Align.CENTER, valign="box", font=None):
    """Heavy text from CAD markup (see CAD_TEXT); characters Arial Black lacks come from Apple
    Symbols. `size` is the font size in mm. Returns a 2D Sketch aligned horizontally on x = 0
    per `halign`, and vertically centred on y = 0 (valign "box") or with its baseline LINE_MID
    sizes below y = 0 (valign "line": legends that share a line keep a common baseline)."""
    return _to_sketch(text_geom(text, size, halign, valign, font))


def text_width(text, size):
    """Rendered width (mm) of `text` at `size`."""
    x0, _, x1, _ = _text_geom(text, size).bounds
    return x1 - x0


PLACED = []            # (text, size, valign, font) of every legend made, for legendcheck.py


def label(text, size, cx, cy, z_top, depth=INLAY, halign=Align.CENTER, valign="box", font=None):
    """Legend solid occupying [z_top - depth, z_top], centred at CAD (cx, cy).

    The caller subtracts it from the body and exports it as a separate part in the legend
    colour, so the legend is inlaid (flush) rather than embossed."""
    PLACED.append((text, size, valign, font))
    solid = extrude(text2d(text, size, halign, valign, font), depth)
    bb = solid.bounding_box()
    assert abs(bb.min.Z) < 1e-6 and abs(bb.max.Z - depth) < 1e-6, f"legend {text!r} extruded the wrong way"
    return Pos(cx, cy, z_top - depth) * solid

# display (landscape = the datasheet's portrait view turned 90 deg clockwise, FPC on the left).
# ER-TFT032IPS-3.2 drawing: CTP outline 77.7 x 55.04; touch visual area 65.95 x 49.2 from
# 8.6 mm (FPC edge) / 2.92 (top); LCD active area 64.8 x 48.6 from 9.4 / 3.22.
PANEL = layout.PANEL
AA = (PANEL[0] + 9.4, PANEL[1] + 3.22, PANEL[0] + 9.4 + 64.8, PANEL[1] + 3.22 + 48.6)
VA = (PANEL[0] + 8.6, PANEL[1] + 2.92, PANEL[0] + 8.6 + 65.95, PANEL[1] + 2.92 + 49.2)
# Folded FPC (board coords): it wraps the LCD's left edge (bulging up to ~1.5 mm west of the
# glass), then its 43 mm-wide part with the touch controller (~1.2 mm of parts) lies against the
# panel back, then the 20.5 mm tail runs on and dips into J3.
FPC_FOLD_Y = (PANEL[1] + 6.5, PANEL[1] + 49.5)       # 43 mm section across the left edge
FPC_RELIEF_X = layout.PANEL_LCD_X0 - 1.5             # pocket relief reaches at least this far west


def cad(x, y):
    """board coords (mm, y down, origin top-left) -> CAD XY (origin board centre, y up)."""
    return x - W / 2, H / 2 - y


def board_rect(r):
    """CAD (cx, cy, w, h) of a board-coordinate rect (x0, y0, x1, y1)."""
    cx, cy = cad((r[0] + r[2]) / 2, (r[1] + r[3]) / 2)
    return cx, cy, r[2] - r[0], r[3] - r[1]


def rrect_prism(w, h, r, z0, z1, cx=0.0, cy=0.0):
    """Rounded-rectangle prism w x h (corner radius r) centred at (cx, cy), from z0 to z1."""
    with BuildPart() as p:
        with BuildSketch(Plane.XY.offset(z0)):
            with Locations((cx, cy)):
                RectangleRounded(w, h, r)
        extrude(amount=z1 - z0)
    return p.part


def box(cx, cy, w, h, z0, z1):
    """Axis-aligned box w x h centred at CAD (cx, cy), from z0 to z1."""
    return Pos(cx, cy, (z0 + z1) / 2) * Box(w, h, z1 - z0)


def frustum(w, h, r, z0, z1, grow, cx=0.0, cy=0.0):
    """Rounded-rectangle frustum: w x h (corner r) at z0, grown by `grow` per side at z1 (a
    45-degree chamfer when |grow| = |z1 - z0|; grow < 0 shrinks)."""
    from build123d import loft
    s0 = Plane.XY.offset(z0) * Pos(cx, cy) * RectangleRounded(w, h, r)
    s1 = Plane.XY.offset(z1) * Pos(cx, cy) * RectangleRounded(w + 2 * grow, h + 2 * grow, max(r + grow, 0.01))
    return loft([s0, s1])


def cone(cx, cy, r0, z0, r1, z1):
    """Truncated cone at CAD (cx, cy): radius r0 at z0, r1 at z1 (root fillets, hole chamfers)."""
    from build123d import Cone
    lo, hi = (r0, r1) if z0 < z1 else (r1, r0)
    return Pos(cx, cy, (z0 + z1) / 2) * Cone(lo, hi, abs(z1 - z0))


def chamfered_prism(w, h, r, z0, z1, c_lo=0.0, c_hi=0.0):
    """Rounded-rectangle prism from z0 to z1 with 45-degree chamfers c_lo on its bottom edge and
    c_hi on its top edge."""
    p = rrect_prism(w, h, r, z0 + c_lo, z1 - c_hi)
    if c_lo:
        p += frustum(w - 2 * c_lo, h - 2 * c_lo, r - c_lo, z0, z0 + c_lo, c_lo)
    if c_hi:
        p += frustum(w, h, r, z1 - c_hi, z1, -c_hi)
    return p


def flare_y(cx, y_face, z0, z1, w, c, inward=-1):
    """Outward 45-degree flare (chamfer) of a rectangular opening w wide, z0..z1 high, through a
    wall whose outer face is at y = y_face (the wall lies on the `inward` side of it)."""
    from build123d import Rectangle, loft
    zc = (z0 + z1) / 2
    pl = lambda y: Plane(origin=(cx, y, zc), x_dir=(1, 0, 0), z_dir=(0, 1, 0))
    s0 = pl(y_face - inward * 0.01) * Rectangle(w + 2 * c + 0.02, z1 - z0 + 2 * c + 0.02)
    s1 = pl(y_face + inward * c) * Rectangle(w, z1 - z0)
    return loft([s0, s1])


# ------------------------------------------------------------------ keys ---
# Legends, SHIFT functions, ALPHA letters and colour groups come from firmware/keys/layout.py
# (the logical layout); the PCB only fixes where each physical key is.
LEGEND = {k["name"]: k["legend"] for k in keys_layout.KEYS}
SHIFTED = {k["name"]: k["shift"] for k in keys_layout.KEYS if k["shift"]}   # above the key, on the overlay
# ALPHA letters as printed on the overlay (capitals; space shown as an open box)
ALPHA_L = {k["name"]: ("␣" if k["alpha"] == " " else k["alpha"].upper() if k["alpha"].isalpha() and
                       k["alpha"] != "θ" else k["alpha"])
           for k in keys_layout.KEYS if k["alpha"]}


DPAD_KEYS = ("UP", "DOWN", "LEFT", "RIGHT")


def key_size(name, y):
    """Keycap body (w, h) of key `name`. Rows (hardware/gen/layout.py): soft keys
    y 82.04, row A (SHIFT/ALPHA/⌫) 92.04, row B (HOME/APPS/MENU) 102.04, main grid from 112.84 at 11.15 mm pitch. Flanges add FLANGE_OVER
    (D-pad: FLANGE_OVER_DPAD) per side; all neighbouring flanges stay >= 0.4 mm apart."""
    if name in ("UP", "DOWN"):
        return 8.0, 5.0
    if name in ("LEFT", "RIGHT"):
        return 5.0, 7.0
    if y < layout.ROW_A - 5:
        return 12.0, 6.0          # soft-key row
    if y < layout.ROWS_MAIN[0] - 5:
        return 12.6, 7.6          # rows A and B (10 mm row pitch)
    return 12.6, 7.6          # main grid


def keys():
    """[(logical name, x, y, w, h)] in board coordinates; positions from the PCB layout."""
    pos = layout.key_positions(design.LAYOUT)
    return [(k, x, y) + key_size(k, y) for k, (x, y) in pos.items()]


# ------------------------------------------------------------- top shell ---
# Panel snap fingers: (edge, position along it in board mm). None on the left (FPC fold); all sit
# east of the folded FPC section (x < 23.2). Top-left finger at x 34, clear of D1 (to x 30.1).
PANEL_FINGERS = [("top", 34.0), ("top", 62.0), ("bottom", 30.0), ("bottom", 62.0), ("right", 38.0)]
# Fingers taper from FINGER_T at the root to FINGER_T_TIP at the lip (lower root strain for the
# same lip travel), with a FINGER_FILLET x 45-degree root fillet on the outer face; the slot
# frees them on all four sides (FINGER_SLOT, also on the panel side) right up to FINGER_ROOT.
FINGER_W, FINGER_T, FINGER_T_TIP, FINGER_LIP, FINGER_SLOT = 5.0, 0.9, 0.8, 0.25, 0.5
FINGER_FILLET = 0.4
FINGER_IN = 0.05     # finger inner face outside the nominal panel outline


def _finger_frame(side, t):
    """(x, y) of the finger's inner face centre, and the outward unit normal (CAD frame)."""
    if side == "top":
        return cad(t, PANEL[1])[0], cad(0, PANEL[1])[1], (0, 1)
    if side == "bottom":
        return cad(t, PANEL[3])[0], cad(0, PANEL[3])[1], (0, -1)
    return cad(PANEL[2], 0)[0], cad(0, t)[1], (1, 0)


def _edge_box(side, t, n0, n1, along, z0, z1):
    """Box spanning [n0, n1] outward from the panel edge and `along` wide along it."""
    x, y, (nx, ny) = _finger_frame(side, t)
    cx, cy = x + nx * (n0 + n1) / 2, y + ny * (n0 + n1) / 2
    w, h = (along, n1 - n0) if nx == 0 else (n1 - n0, along)
    return box(cx, cy, w, h, z0, z1)


FINGER_ROOT_IN = 0.8       # slots reach this far into the plate (0.8 mm skin left) for a longer cantilever
FINGER_ROOT = CEIL + FINGER_ROOT_IN
FINGER_FLAT = 0.1          # height of the lip's flat seat (set_front: one whole layer)
# lip seat 0.15 mm below the nominal panel back: the VHB holds the panel against the ceiling and
# the fingers only retain it, so a module at the +0.2 mm thickness limit still snaps in
FINGER_SEAT = PANEL_BACK - 0.15


def finger_slot(side, t):
    """Cut that frees a snap finger: FINGER_SLOT around it on all four sides (the panel side
    too, so the whole length bends: the old slot left the finger's inner face fused to the plate
    above CEIL + TAPE), from 3.6 mm below the ceiling up into the plate (FINGER_ROOT)."""
    n0 = FINGER_IN - FINGER_SLOT
    n1 = FINGER_IN + FINGER_T + FINGER_FILLET + FINGER_SLOT
    return _edge_box(side, t, n0, n1, FINGER_W + 2 * FINGER_SLOT, CEIL - 3.6, FINGER_ROOT)


def finger_strain(lip=FINGER_LIP):
    """Peak bending strain of a snap finger when its lip clears the panel edge: tapered
    cantilever (FINGER_T at the root, FINGER_T_TIP at the tip), fixed at FINGER_ROOT, tip
    deflection `lip` at the ramp top (FINGER_SEAT - FINGER_FLAT)."""
    L = FINGER_ROOT - (FINGER_SEAT - FINGER_FLAT)
    tip = FINGER_ROOT - (FINGER_SEAT - 0.9)
    x = np.linspace(0, L, 2000)
    t = FINGER_T + (FINGER_T_TIP - FINGER_T) * x / tip
    pe = lip / (12 * np.trapezoid((L - x) ** 2 / t ** 3, x))     # P / (E b)
    return float((6 * pe * (L - x) / t ** 2).max())


def panel_finger(side, t):
    """Cantilever hanging from the ceiling at the panel edge (inner face FINGER_IN outside the
    nominal outline) with a lip under the panel border: ramp below, flat seat at FINGER_SEAT.
    Tapered 0.9 -> 0.8 mm (never under two perimeters) with a 0.4 mm root fillet and a 0.25 mm
    lip (the VHB holds the panel; the lip only retains it); strain to snap over the panel
    finger_strain() = 1.2 % (was 1.9 % on paper, ~2.4 % with the fused inner face)."""
    z = FINGER_SEAT
    zt = z - 0.9                                    # finger tip
    x, y, (nx, ny) = _finger_frame(side, t)
    u0 = -FINGER_IN                                 # inner face (u = inward distance)

    def outer(zq):                                  # outer face, linear taper root -> tip
        return u0 - FINGER_T + (FINGER_T - FINGER_T_TIP) * (FINGER_ROOT - zq) / (FINGER_ROOT - zt)
    zf = FINGER_ROOT - FINGER_FILLET
    pts = [(u0, FINGER_ROOT + 0.05), (u0 - FINGER_T - FINGER_FILLET, FINGER_ROOT + 0.05),
           (u0 - FINGER_T - FINGER_FILLET, FINGER_ROOT), (outer(zf), zf), (outer(zt), zt),
           (u0, zt), (u0 + FINGER_LIP, z - FINGER_FLAT), (u0 + FINGER_LIP, z), (u0, z)]
    f = extrude(Polygon(*pts, align=None), FINGER_W / 2, both=True)   # sketch on XY: x = inward, y = z
    f = Rot(90, 0, 0) * f              # y -> z; extrusion now along -y/+y (both)
    # rotate so local +x (inward) points along -normal
    ang = {(0, 1): -90, (0, -1): 90, (1, 0): 180}[(nx, ny)]
    return Pos(x, y, 0) * Rot(0, 0, ang) * f


# Bar clips (--bar-clips, V3_THIN.md): instead of the snap fingers, one long bar beside each long
# edge of the panel (top and bottom), held only by a post at each end. It flexes sideways, in the
# print plane (along the layers, like the battery-door latch), and a short hook in its middle
# grabs the panel border BAR_GRAB deep, with a 45-degree lead-in under it. The bar hangs
# BAR_GAP_TOP below the ceiling and the plate under it is pocketed BAR_POCKET deep (MIN_SKIN
# left), so the bar's first printed layer (face-down) is a bridge from post to post over
# BAR_POCKET + BAR_GAP_TOP of air: a sagging bridge cannot fuse to the face.
# Local frame of a bar: u along the edge (0 at the bar centre), n outward from the nominal
# panel outline.
BAR_CLIPS = False        # set by --bar-clips; the default build keeps the snap fingers
BAR_X = 45.0             # board x of both bars' centre (posts clear of the FPC fold, D1, J2, bosses)
BAR_SIDES = ("top", "bottom")
BAR_L = 40.0             # clear span between the posts
BAR_T = 0.85             # bar thickness (bending direction): two 0.42 mm walls
BAR_CLR = 0.25           # bar inner face outside the nominal outline (panel +-0.2 overall)
BAR_GRAB = 1.5           # hook tip inside the nominal outline
BAR_HOOK_L = 4.0         # hook length along the bar (short: the hook section is ~rigid)
BAR_FLAT = 0.4           # hook tip height (two layers) under the seat
BAR_SEAT_DROP = 0.3      # seat below the nominal panel back: on a layer boundary of the face-down
#                          print (TOP - seat = 6.0), room for a +0.2 panel and a sagging hook tip;
#                          foam pads behind the panel take up the play (V3_THIN.md)
BAR_FILLET = 0.8         # in-plane fillet where the bar meets each post
BAR_POST, BAR_POST_N = 3.0, 2.5   # post length along the edge; reach beyond the bar's outer face
BAR_BOT = 3.0            # bar and post bottoms above the PCB (D1 2.5 under the top-left post)
BAR_GAP_TOP = 0.6        # bar top below the ceiling
BAR_POCKET = PLATE - MIN_SKIN   # plate pocket over the bar (1.0: MIN_SKIN left under it)
BAR_PART_CLR = 0.3       # least clearance over board parts under bars, posts, hooks and swing


def _bar_place(side, solid):
    """Local (u, n, z) bar solid -> CAD position at the panel's `side` edge."""
    x, y, (nx, ny) = _finger_frame(side, BAR_X)
    return Pos(x, y, 0) * Rot(0, 0, 0 if ny > 0 else 180) * solid


def _bar_seat():
    return PANEL_BACK - BAR_SEAT_DROP


def _bar_hook_bottom():
    return _bar_seat() - BAR_FLAT - (BAR_CLR + BAR_GRAB)


def barclip_numbers(defl=BAR_GRAB):
    """Bar clip mechanics at hook travel `defl`. The hook segment (the bar plus the hook's 2.5 mm
    deep section) is taken as rigid, so each half of the bar is a guided cantilever from its
    post fillet to the hook: strain = 3 d t / a^2, force = 12 E I d / a^3 per half. Returns
    (peak strain, lateral force on the hook in N)."""
    a = (BAR_L - BAR_HOOK_L) / 2 - BAR_FILLET / 2
    h = CEIL - BAR_GAP_TOP - BAR_BOT
    i = h * BAR_T ** 3 / 12
    return 3 * defl * BAR_T / a ** 2, 2 * 12 * PETG_E * i * defl / a ** 3


def barclip_cases():
    """{case: hook travel while the panel goes in}: nominal panel; panel 0.2 mm oversize (0.1 a
    side), centred; any panel pushed hard against this bar's inner face (BAR_GRAB + BAR_CLR, the
    geometric worst case)."""
    return {"nominal": BAR_GRAB, "oversize": BAR_GRAB + 0.1, "worst": BAR_GRAB + BAR_CLR}


def bar_clip(side):
    """The bar, its two posts (from the bar bottom up into the plate) and the hook."""
    c, t, L, P = BAR_CLR, BAR_T, BAR_L, BAR_POST
    nb = c + t + BAR_POST_N
    r = BAR_FILLET
    arc = lambda cu, s: [(cu + s * r * math.cos(a), c + t + r + r * math.sin(a))
                         for a in np.linspace(0, -math.pi / 2, 9)]
    right = arc(L / 2 - r, 1)                                    # (L/2, c+t+r) -> (L/2-r, c+t)
    left = [(-u, n) for u, n in reversed(right)]                 # (-L/2+r, c+t) -> (-L/2, c+t+r)
    pts = [(-L / 2 - P, c), (L / 2 + P, c), (L / 2 + P, nb), (L / 2, nb)] + right + left + \
          [(-L / 2, nb), (-L / 2 - P, nb)]
    z_top = CEIL - BAR_GAP_TOP
    body = Pos(0, 0, BAR_BOT) * extrude(Polygon(*pts, align=None), z_top - BAR_BOT)
    for s in (-1, 1):
        body += box(s * (L / 2 + P / 2), (c + nb) / 2, P, nb - c, BAR_BOT, CEIL + 0.05)
    zs, zb = _bar_seat(), _bar_hook_bottom()
    hook = [(c + t, zb), (c, zb), (-BAR_GRAB, zs - BAR_FLAT), (-BAR_GRAB, zs), (c + t, zs)]
    body += extrude(Plane.YZ * Polygon(*hook, align=None), BAR_HOOK_L / 2, both=True)
    return _bar_place(side, body)


def bar_pocket(side):
    """Plate pocket over the bar (BAR_POCKET deep), post to post."""
    n0, n1 = BAR_CLR - 0.2, BAR_CLR + BAR_T + BAR_FILLET + 0.4
    return _bar_place(side, box(0, (n0 + n1) / 2, BAR_L, n1 - n0, CEIL - 0.1, CEIL + BAR_POCKET))


def bar_rib_cut(side):
    """Where the bar and posts replace the panel pocket rib."""
    L = BAR_L + 2 * BAR_POST
    return _bar_place(side, box(0, 1.5, L, 3.0, CEIL - 3.7, CEIL + 0.01))


def bar_swing(side, margin=0.3):
    """The room the bar needs outside it (worst-case travel + margin), between the fillets."""
    d = max(barclip_cases().values()) + margin
    n0 = BAR_CLR + BAR_T + 0.05
    return _bar_place(side, box(0, n0 + d / 2, BAR_L - 2 * BAR_FILLET - 0.2, d, BAR_BOT, CEIL - 0.05))


def barclip_clearance():
    """[(clearance, side, ref, footprint, height)] of the top-side parts under a bar clip's posts,
    bar, swing and hook (bottoms BAR_BOT, and the hook's bottom under the hook), least first."""
    b = layout_board()
    rows = []
    d = max(barclip_cases().values()) + 0.3
    for side in BAR_SIDES:
        x, y, (nx, ny) = _finger_frame(side, BAR_X)
        def rect(u0, u1, n0, n1):            # local -> board-coordinate rect
            xs = (x + u0, x + u1)
            ys = sorted((y + ny * n0, y + ny * n1))
            return (xs[0] + W / 2, H / 2 - ys[1], xs[1] + W / 2, H / 2 - ys[0])
        L, P = BAR_L / 2 + BAR_POST, BAR_HOOK_L / 2
        zones = [(rect(-L, L, BAR_CLR, BAR_CLR + BAR_T + max(BAR_POST_N, d)), BAR_BOT),
                 (rect(-P, P, -BAR_GRAB, BAR_CLR + BAR_T + d), _bar_hook_bottom())]
        for ref, p in b.placed.items():
            if p.side != "F":
                continue
            r = p.rect()
            px0, py0, px1, py1 = r[0] - layout.OX, r[1] - layout.OY, r[2] - layout.OX, r[3] - layout.OY
            for (x0, y0, x1, y1), zb in zones:
                if px1 < x0 or px0 > x1 or py1 < y0 or py0 > y1:
                    continue
                h = next((v for k, v in PART_H if k in p.fp.lib_id), None)
                if h is None:
                    raise SystemExit(f"barclip_clearance: no height for {ref} {p.fp.lib_id}")
                rows.append((zb - h, side, ref, p.fp.lib_id.split(":")[1], h))
    return sorted(rows)


def usb_pocket():
    """J4 plug pocket through the wall (both shells): 12.6 x 6.8 mm around the receptacle
    (Z -1.77..5.03), so overmolded plugs reach the receptacle face."""
    r = layout_board().placed["J4"].rect()
    cx, _, _, _ = board_rect((r[0] - layout.OX, 0, r[2] - layout.OX, 1))
    w, z0, z1 = USB_POCKET
    cut = box(cx, OUT_H / 2 - CUT_DEPTH / 2 + 1, w, CUT_DEPTH + 2, z0, z1)
    return cut + flare_y(cx, OUT_H / 2, z0, z1, w, OPENING_CH)     # clean chamfered mouth


USB_POCKET = (12.6, -1.77, 5.03)   # width, z0, z1
SD_SCALLOP_R, SD_CARD_Z = 6.0, 0.6
OPENING_CH = 0.5     # 45-degree chamfer around the USB-C and microSD openings' outer edges
# the end-wall cuts reach through the wall and the PCB ledge behind it (a partial cut left a
# 0.2-0.3 mm sliver of ledge, printcheck.py)
CUT_DEPTH = WALL + GAP + 0.8 + 0.6


def sd_cutout(z0, z1):
    """microSD slot opening plus a 12 mm half-round finger scallop through the wall, centred on
    the card plane, so a fingertip/nail reaches the card (mouth ~1.2 mm inside the wall); both
    with an OPENING_CH chamfer on the outside."""
    r = layout_board().placed["J2"].rect()
    cx, _, w, _ = board_rect((r[0] - layout.OX, 0, r[2] - layout.OX, 1))
    cut = box(cx, OUT_H / 2 - CUT_DEPTH / 2 + 1, w + 3.0, CUT_DEPTH + 2, z0, z1)
    cut += flare_y(cx, OUT_H / 2, z0, z1, w + 3.0, OPENING_CH)
    cut += Pos(cx, OUT_H / 2 - CUT_DEPTH / 2 + 1, SD_CARD_Z) * Rot(90, 0, 0) * Cylinder(SD_SCALLOP_R, CUT_DEPTH + 2)
    from build123d import Cone
    c = OPENING_CH
    # cone axis along -y from the outer face: radius R + c at the face, R at c inside
    cut += Pos(cx, OUT_H / 2 - c / 2 + 0.005, SD_CARD_Z) * Rot(-90, 0, 0) * Cone(SD_SCALLOP_R, SD_SCALLOP_R + c + 0.01, c + 0.01)
    return cut


def top_shell():
    """Top shell (the front) in assembled position (Z from -RABBET to TOP). One colour: the face
    carries no legends (they are on the overlay, overlay.py)."""
    # outer body with an EDGE_CH chamfer round the face (on the bed: no elephant-foot lip, and a
    # crisp bevel instead of a sharp edge)
    shell = chamfered_prism(OUT_W, OUT_H, OUT_R, 0, TOP, c_hi=EDGE_CH)
    shell -= rrect_prism(OUT_W - 2 * WALL, OUT_H - 2 * WALL, OUT_R - WALL, 0, CEIL)
    # rabbet: the outer part of the wall continues down over the back shell's tongue
    # (RAB_KEEP), RAB_CLR clearance per side, so the lap is WALL - RAB_KEEP - RAB_CLR = 0.95 mm.
    # (Was 1.075 mm over a 1.0 mm tongue in a 2.0 mm wall: 0.075 mm interference per side.)
    # Its outer bottom edge has a SEAM_CH chamfer: with the back's, a V reveal along the seam.
    lap_in = WALL - RAB_KEEP - RAB_CLR
    lap = chamfered_prism(OUT_W, OUT_H, OUT_R, -RABBET, 0.0, c_lo=SEAM_CH)
    lap -= rrect_prism(OUT_W - 2 * lap_in, OUT_H - 2 * lap_in, OUT_R - lap_in, -RABBET - 0.1, 0.1)
    shell += lap
    # display window: the whole touch viewing area is exposed so a fingertip reaches the edges;
    # WINDOW_R corners and a WINDOW_CH 45-degree bevel on the face (prints from the bed: no
    # bridged step, no elephant-foot lip)
    ax, ay, aw, ah = board_rect((VA[0] - 0.3, VA[1] - 0.3, VA[2] + 0.3, VA[3] + 0.3))
    shell -= rrect_prism(aw, ah, WINDOW_R, CEIL - 0.1, TOP + 0.1, ax, ay)
    shell -= frustum(aw, ah, WINDOW_R, TOP - WINDOW_CH, TOP + 0.01, WINDOW_CH + 0.01, ax, ay)
    # VHB tape recess: the tape on the bezel ledge doesn't push the panel below PANEL_BACK
    c = PANEL_CLR
    px, py, pw, ph = board_rect((PANEL[0] - c, PANEL[1] - c, PANEL[2] + c, PANEL[3] + c))
    shell -= box(px, py, pw, ph, CEIL - 0.1, CEIL + TAPE)
    # panel pocket ribs: locate the panel against the ceiling
    rib = box(px, py, pw + 2.4, ph + 2.4, CEIL - 3.5, CEIL) - box(px, py, pw, ph, CEIL - 3.6, CEIL + 0.1)
    # FPC fold relief on the left: the rib is cut over the 43 mm FPC width (+1 mm each side) for
    # the module's full height, back to the side wall (>= 1.5 mm west of the LCD glass edge);
    # short rib stubs stay at both left corners to locate the panel
    _, fy, _, fh = board_rect((0, FPC_FOLD_Y[0] - 1.0, 1, FPC_FOLD_Y[1] + 1.0))
    rx0, rx1 = cad(-GAP - 0.1, 0)[0], cad(PANEL[0] - PANEL_CLR + 0.1, 0)[0]
    assert rx1 > cad(FPC_RELIEF_X, 0)[0]
    relief = box((rx0 + rx1) / 2, fy, rx1 - rx0, fh, PANEL_BACK - 2.0, CEIL + 0.1)
    rib -= relief
    # snap fingers retain the panel (seat FINGER_SEAT); slots free each finger from the rib (and
    # 0.8 mm into the plate) so it hangs from the plate
    # (--bar-clips: no fingers; a bar clip on each long edge instead, BAR_CLIPS)
    fingers = () if BAR_CLIPS else PANEL_FINGERS
    for side, t in fingers:
        rib -= finger_slot(side, t)
        shell -= finger_slot(side, t)
    for side in (BAR_SIDES if BAR_CLIPS else ()):
        rib -= bar_rib_cut(side)
        shell -= bar_pocket(side)
    shell += rib
    for side, t in fingers:
        shell += panel_finger(side, t)
    for side in (BAR_SIDES if BAR_CLIPS else ()):
        shell += bar_clip(side)
    # key holes: KEY_CLR per side around the cap body; the wider flange (FLANGE_OVER) below the
    # plate keeps the cap from falling out. Rounded openings with a KEY_CH chamfer on the face
    for name, x, y, w, h in keys():
        cx, cy = cad(x, y)
        r = CAP_R + KEY_CLR
        shell -= rrect_prism(w + 2 * KEY_CLR, h + 2 * KEY_CLR, r, CEIL - 0.1, TOP + 0.1, cx, cy)
        shell -= frustum(w + 2 * KEY_CLR, h + 2 * KEY_CLR, r, TOP - KEY_CH, TOP + 0.01, KEY_CH + 0.01, cx, cy)
    # bosses down onto the PCB with holes for M2 heat-set inserts (screws come from the back).
    # Every hole is 3.5 mm from a side edge, so each full-height boss (BOSS_R) is tied to the
    # side wall (and the top-left one to the top wall too) by a web as wide as the insert hole
    # plus 1 mm, and has a ROOT x 45-degree fillet cone where it meets the plate. The middle
    # pair (y 72.5) sits between the panel pocket (rib to y 69.6, which it merges with) and the
    # soft-key flanges (from y 78.0). The two bottom bosses sit under the ON / ENTER keycap
    # flanges: they stop at BOSS_LOW and are tied to the corner wall by a full-height gusset in
    # the outer quadrant instead.
    x_in, y_in = OUT_W / 2 - WALL, OUT_H / 2 - WALL          # inner wall faces (CAD)
    for hx, hy in layout.HOLES:
        cx, cy = cad(hx, hy)
        if hy > H - 10:            # bottom pair (board y within 10 mm of the bottom edge)
            sx = -1 if hx < W / 2 else 1                  # toward the side wall
            R = BOSS_LOW_R
            shell += Pos(cx, cy, BOSS_LOW / 2) * Cylinder(R, BOSS_LOW)
            quad = box(cx + sx * (0.5 + 5.0), cy - 0.5 - 5.0, 10.0, 10.0, 0, CEIL + 0.05)  # x outside 3.0, y outside hy + 0.5
            corner = (Pos(cx, cy, CEIL / 2) * Cylinder(R, CEIL) +
                      box(cx + sx * 4.0, cy - 4.0, 8.0, 8.0, 0, CEIL)) & quad
            shell += corner & rrect_prism(OUT_W - 0.5, OUT_H - 0.5, OUT_R - 0.25, -1, TOP)
        else:
            shell += Pos(cx, cy, CEIL / 2) * Cylinder(BOSS_R, CEIL)
            shell += cone(cx, cy, BOSS_R + ROOT, CEIL, BOSS_R, CEIL - ROOT)
            webs = []
            web_w = INSERT_D + 1.0
            sx = -1 if hx < W / 2 else 1
            webs.append(box((cx + sx * (x_in + 0.5)) / 2, cy, abs(sx * (x_in + 0.5) - cx), web_w, 1.2, CEIL + 0.05))
            if hy < 10:            # top-left: also to the top wall
                webs.append(box(cx, (cy + y_in + 0.5) / 2, web_w, y_in + 0.5 - cy, 1.2, CEIL + 0.05))
            for wb in webs:
                shell += wb
        # insert hole from the boss foot (Z 0) up INSERT_L; 3.2 mm bore for a ~3.5 mm OD insert
        # leaves material for the knurls to melt into; EF lead-in chamfer at the mouth
        shell -= Pos(cx, cy, INSERT_L / 2 - 0.05) * Cylinder(INSERT_D / 2, INSERT_L + 0.1)
        shell -= cone(cx, cy, INSERT_D / 2 + EF, -0.01, INSERT_D / 2, EF)
    # relieve the boss feet around top-side parts that sit next to them: 0.6 mm clearance, up to 2 mm above the PCB
    bb = layout_board()
    for hx, hy in layout.HOLES:
        for ref, p in bb.placed.items():
            if p.side != "F":
                continue
            r = p.rect(0.6)
            x0, y0, x1, y1 = r[0] - layout.OX, r[1] - layout.OY, r[2] - layout.OX, r[3] - layout.OY
            if math.hypot(max(x0 - hx, 0, hx - x1), max(y0 - hy, 0, hy - y1)) < BOSS_R + ROOT:
                cx, cy, w, h = board_rect((x0, y0, x1, y1))
                shell -= box(cx, cy, w, h, -0.1, 2.0)
    # top-edge openings: USB-C plug pocket and microSD slot + finger scallop
    shell -= usb_pocket() + sd_cutout(-0.1, 2.4)
    # the corner catches (CATCHES): the lap thickened into a lip, the pocket and the bump's undercut
    for side, t, w in CATCHES:
        shell += catch_lip(side, t, w)
        shell -= catch_pocket(side, t, w)
    b = layout_board()
    # Debug is done with the case off, so the headers stay hidden: the 2.54 mm pins (J5 DEBUG,
    # J6 EXPANSION) reach Z 8.54, just past the ceiling, so they get blind pockets in the
    # plate's underside (0.5 mm over the pins, ~1 mm skin left on top). J1 (1.27 mm SWD, top
    # at Z 4.0) clears the ceiling. The RESET / BOOT buttons are not exposed either (no
    # pinholes): firmware updates go over USB from the firmware (calcctl flash), and the buttons
    # are reachable with the case open.
    for ref in ("J5", "J6"):
        r = b.placed[ref].rect(0.6)
        cx, cy, w, h = board_rect((r[0] - layout.OX, r[1] - layout.OY, r[2] - layout.OX, r[3] - layout.OY))
        shell -= box(cx, cy, w, h, CEIL - 0.1, HEADER_POCKET_TOP)
    # the plate's underside pockets keep MIN_SKIN under the face
    assert TOP - max(FINGER_ROOT, HEADER_POCKET_TOP) >= MIN_SKIN - 1e-6
    return shell


def overlay_outline(grow=0.0):
    """(w, h, corner r) of the artwork area (CAD, centred on the case), grown by `grow`."""
    return (OUT_W - 2 * OVERLAY_INSET + 2 * grow, OUT_H - 2 * OVERLAY_INSET + 2 * grow,
            OUT_R - OVERLAY_INSET + grow)


def paper_rect():
    """(w, h) of the transfer paper, cut square-cornered: the shell plus PAPER_MARGIN all round."""
    return OUT_W + 2 * PAPER_MARGIN, OUT_H + 2 * PAPER_MARGIN


EAR_R, EAR_T = 8.0, 0.4    # optional mouse ears: 16 mm discs, two layers
LAYER_1 = 0.2              # first layer height (the ears' check slices its middle)
EAR_CLR = 0.5              # ears stay this far from every opening in the part's first layer


def bed_section(paths, z):
    """Shapely cross-section at height `z` (print position) of the STL files `paths` together
    (a part and its inlays): what the part's first layer prints."""
    import trimesh
    from shapely.geometry import MultiLineString, Polygon
    from shapely.ops import polygonize, unary_union
    parts = []
    for path in paths:
        mesh = trimesh.load(path, force="mesh")
        segs = trimesh.intersections.mesh_plane(mesh, (0, 0, 1), (0, 0, z))
        segs = np.round(segs[:, :, :2], 5)         # shared edge points meet exactly
        sec = None
        for face in polygonize(MultiLineString([tuple(map(tuple, sg)) for sg in segs if not (sg[0] == sg[1]).all()])):
            ring = Polygon(face.exterior)          # every closed loop of the section, once
            sec = ring if sec is None else sec.symmetric_difference(ring)     # even-odd: holes
        if sec is not None:
            parts.append(sec)
    return unary_union(parts)


def bed_openings(section):
    """The openings of a first-layer cross-section: the area inside its outlines that it does
    not print (screw counterbores, key openings, the display window, the battery opening, ...)."""
    from shapely.geometry import Polygon
    from shapely.ops import unary_union
    filled = unary_union([Polygon(p.exterior) for p in getattr(section, "geoms", [section])])
    return filled.difference(section)


def mouse_ears(section=None):
    """Optional anti-warp "mouse ears" for a shell printed with its outline on the bed: four
    EAR_T discs (Z 0 to EAR_T, print position) centred on the outline's corner arcs, less every
    opening of the shell's first layer (`section`, from bed_section(), in the same frame) grown
    by EAR_CLR: an ear must never seal a hole that opens onto the bed (the back shell's corner
    screw counterbores sit under the ears). Load the file into the same object as the shell
    (same filament); cut the discs off after printing."""
    from shapely.geometry import Point
    from shapely.ops import unary_union
    d = OUT_R / math.sqrt(2)
    discs = unary_union([Point(sx * (OUT_W / 2 - OUT_R + d), sy * (OUT_H / 2 - OUT_R + d)).buffer(EAR_R, quad_segs=32)
                         for sx in (-1, 1) for sy in (-1, 1)])
    if section is not None:
        discs = discs.difference(bed_openings(section).buffer(EAR_CLR, quad_segs=16))
    return extrude(_to_sketch(discs.simplify(0.005)), EAR_T)


def check_ears(part_paths, ears_path, z):
    """Assert that the ears in `ears_path` cover no opening of the first layer of the part
    `part_paths` (both STL files in print position, bed at `z`)."""
    openings = bed_openings(bed_section(part_paths, z + LAYER_1 / 2))
    ears = bed_section([ears_path], z + LAYER_1 / 2)
    over = ears.intersection(openings.buffer(EAR_CLR - 0.05)).area
    assert over < 1e-3, f"{os.path.basename(ears_path)} covers a bed-level opening ({over:.3f} mm2)"
    return openings


# ------------------------------------------------------------ back shell ---
LATCH_ROOM = 2.3    # latch end of the opening: BT1 to the opening edge (cantilever + 0.9 mm flex room)


def batt_opening():
    """Battery opening (board coords): the two holders' extent plus finger room; the latch end
    (board top, CAD +y) leaves room for the door's cantilever to flex away from BT1."""
    b = layout_board()
    rs = [b.placed[r].rect() for r in ("BT1", "BT2")]
    return (min(r[0] for r in rs) - layout.OX - 0.5, min(r[1] for r in rs) - layout.OY - LATCH_ROOM,
            max(r[2] for r in rs) - layout.OX + 0.5, max(r[3] for r in rs) - layout.OY + 1.5)


RAB_CLR_REV2 = 0.3     # rev2 back shell: tongue-to-lap clearance per side (same top shell)
RAB_IN_REV2 = 0.05     # rev2: the tongue grows this far inward (PCB-edge gap 0.45) to stay 0.8 mm


# Corner catches. The top-right corner has no screw (the microSD socket J2 fills it on the
# front), and the microSD slot and scallop interrupt the seam's lap next to it, so the shells,
# held only by the screws 70 mm away, sprang apart there. Two snap catches hold that corner:
# one in the right wall below it, one in the top wall between the USB-C and microSD openings.
# Each is a leaf split by slots from the inner part of the back shell's wall (the outer
# CATCH_SKIN of the wall stays), rising past the seam into a pocket in the inner part of the
# front's wall. A bump on its outer face clicks into an undercut in that pocket, over a lip: the
# front's lap, thickened inward to LIP_CLR from the leaf. Closing, the lip rides up the bump's
# CATCH_LEAD ramp and bends the leaf inward into the PCB-edge gap; pulling, the lip presses on
# the CATCH_BACK ramp, which is steep but not locking, so a firm pull opens the case.
# Local frame of a catch: u outward from the inner wall face, v along the wall, Z as assembled.
CATCHES = [("right", 12.0, 12.0), ("top", 54.25, 9.5)]   # wall, centre (board coord along it), leaf width
LEAF_IN = 0.05        # leaf inner face this far outside the inner wall face (GAP + LEAF_IN to the PCB edge)
LEAF_T = 0.8          # leaf thickness (bending direction, across the layers): two perimeters
LEAF_ROOT = -6.0      # leaf root (the slot bottoms), Z
LEAF_FILLET = 0.4     # 45-degree root fillet on both faces of the leaf
CATCH_SLOT = 0.6      # back shell: slot either side of the leaf (the slots outside and inside it are 0.55)
CATCH_SKIN = 0.6      # wall skin left outside the back's outer slot and outside the front's undercut
LIP_CLR = 0.1         # leaf outer face to the lip
BUMP_CLR = 0.1        # bump crest to the undercut's face, closed
BUMP_ZCLR = 0.15      # bump ramps to the undercut's ramps, in Z, closed (the corner's free play)
POCKET_CLR = 0.3      # leaf sides and tip to the front's pocket
BUMP_Z = 1.0          # bump bottom above the PCB top (the leaf bends less at the PCB edge)
BUMP_FLAT = 0.3       # crest height
CATCH_LEAD = 30.0     # lead-in ramp (top of the bump), degrees from the pull axis (Z)
CATCH_BACK = 50.0     # back ramp (under the bump): steeper; with friction mu it locks at atan(1/mu)
CATCH_MU = 0.4        # PETG on PETG, for the force estimate (0.3-0.5)
PCB_HOLE_PLAY = 0.1   # M2 screw in the PCB's 2.2 mm hole, per side


def catch_dims():
    """The catch profile in its local frame (u outward from the inner wall face, Z as assembled)."""
    u_lo = LEAF_IN
    u_hi = u_lo + LEAF_T
    u_lip = u_hi + LIP_CLR
    u_pocket = WALL - CATCH_SKIN
    u_crest = u_pocket - BUMP_CLR
    hb = u_crest - u_hi
    rb = hb / math.tan(math.radians(CATCH_BACK))
    rl = hb / math.tan(math.radians(CATCH_LEAD))
    zb = BUMP_Z
    return dict(u_lo=u_lo, u_hi=u_hi, u_lip=u_lip, u_pocket=u_pocket, u_crest=u_crest, bump=hb,
                engage=u_crest - u_lip, zb=zb, zc0=zb + rb, zc1=zb + rb + BUMP_FLAT,
                zt=zb + rb + BUMP_FLAT + rl, lap_in=RAB_KEEP + RAB_CLR)


def _catch_prism(pts, w):
    """Prism of the (u, Z) profile `pts`, w long along v (centred), in the local frame."""
    return Rot(90, 0, 0) * extrude(Polygon(*pts, align=None), w / 2, both=True)


def _catch_rect(u0, u1, z0, z1, w):
    return _catch_prism([(u0, z0), (u1, z0), (u1, z1), (u0, z1)], w)


def _catch_place(side, t, solid):
    """Local catch frame -> CAD: u = 0 on the inner face of the right or top wall, v = 0 at board
    coordinate t along it."""
    if side == "right":
        return Pos(W / 2 + GAP, H / 2 - t, 0) * solid
    return Pos(t - W / 2, H / 2 + GAP, 0) * Rot(0, 0, 90) * solid


def catch_leaf(side, t, w):
    """Back shell: the leaf with its bump and root fillets (fused to the slot bottoms)."""
    d, f = catch_dims(), LEAF_FILLET
    zr = LEAF_ROOT - 0.01
    pts = [(d["u_lo"] - f, zr), (d["u_hi"] + f, zr), (d["u_hi"], LEAF_ROOT + f), (d["u_hi"], d["zb"]),
           (d["u_crest"], d["zc0"]), (d["u_crest"], d["zc1"]), (d["u_hi"], d["zt"]),
           (d["u_lo"], d["zt"]), (d["u_lo"], LEAF_ROOT + f)]
    return _catch_place(side, t, _catch_prism(pts, w))


def catch_slots(side, t, w):
    """Back shell: what frees the leaf: from the PCB edge (the ledge stays under the board) to the
    outer skin, CATCH_SLOT beyond each side of the leaf, from the root up through the tongue."""
    d = catch_dims()
    return _catch_place(side, t, _catch_rect(-GAP, d["u_pocket"], LEAF_ROOT, 0.1, w + 2 * CATCH_SLOT))


def catch_pocket(side, t, w):
    """Front: the pocket the leaf rises into (open to the inside, LIP_CLR outside the leaf) and
    the undercut for the bump, out to CATCH_SKIN under the outer face. Its ramps are the bump's,
    BUMP_ZCLR higher (lead-in) and lower (back ramp), so the back ramp is a 50-degree overhang
    face-down."""
    d = catch_dims()
    wp = w + 2 * POCKET_CLR
    pocket = _catch_rect(-0.1, d["u_lip"], -0.1, d["zt"] + POCKET_CLR, wp)
    tb, tl = math.tan(math.radians(CATCH_BACK)), math.tan(math.radians(CATCH_LEAD))
    u0, u1, g = d["u_lip"] - 0.05, d["u_pocket"], BUMP_ZCLR
    under = _catch_prism([(u0, d["zb"] - g + (u0 - d["u_hi"]) / tb), (u1, d["zb"] - g + (u1 - d["u_hi"]) / tb),
                          (u1, d["zc1"] + g + (d["u_crest"] - u1) / tl),
                          (u0, d["zc1"] + g + (d["u_crest"] - u0) / tl)], wp)
    return _catch_place(side, t, pocket + under)


def catch_lip(side, t, w):
    """Front: the lap thickened inward to the lip face over the pocket's width."""
    d = catch_dims()
    return _catch_place(side, t, _catch_rect(d["u_lip"], d["lap_in"] + 0.01, -RABBET, 0.01, w + 2 * POCKET_CLR))


def _leaf_bend(defl):
    """Leaf as a cantilever from LEAF_ROOT (thickness LEAF_T plus the root fillets), loaded at the
    bump's crest (its lowest point: the shortest lever, the most strain) so that point moves
    `defl` inward. Returns (peak bending strain, force at the crest per mm of leaf width / E,
    deflection as a function of Z)."""
    d = catch_dims()
    a = d["zc0"] - LEAF_ROOT
    x = np.linspace(0, a, 4000)
    t = LEAF_T + 2 * np.clip(LEAF_FILLET - x, 0, None)
    pe = defl / (12 * np.trapezoid((a - x) ** 2 / t ** 3, x))      # P / (E b)
    strain = float((6 * pe * (a - x) / t ** 2).max())
    kappa = 12 * pe * (a - x) / t ** 3
    dx = x[1] - x[0]
    slope = np.concatenate([[0], np.cumsum((kappa[1:] + kappa[:-1]) / 2 * dx)])
    y = np.concatenate([[0], np.cumsum((slope[1:] + slope[:-1]) / 2 * dx)])

    def at(z):
        xq = z - LEAF_ROOT
        if xq <= a:
            return float(np.interp(xq, x, y))
        return float(y[-1] + slope[-1] * (xq - a))
    return strain, pe, at


def catch_shift(rev2=False):
    """Most the back shell can sit sideways from the front: the seam tongue's clearance, but no
    more than the M2 screws' play in the back's clearance bores (as modelled; they print smaller)."""
    return min(RAB_CLR_REV2 if rev2 else RAB_CLR, (SCREW_CLR_D - 2.0) / 2)


def catch_numbers(w, rev2=False):
    """Strain, engagement, clearances and forces of a catch with a leaf w wide, nominal and with
    the back shell shifted by catch_shift() toward / away from the wall (and the PCB by its hole
    play toward the leaf)."""
    d = catch_dims()
    s = catch_shift(rev2)
    e = d["engage"]
    tb, tl = math.tan(math.radians(CATCH_BACK)), math.tan(math.radians(CATCH_LEAD))
    out = dict(engage=e, engage_min=e - s, engage_max=e + s, shift=s)
    out["strain"], pe, bend = _leaf_bend(e)
    out["strain_max"] = _leaf_bend(e + s)[0]
    p = pe * PETG_E * w                                  # N at the crest, nominal snap deflection
    out["k"] = p / e
    out["pull"] = p * (tb + CATCH_MU) / (1 - CATCH_MU * tb)
    out["pull_min"] = out["pull"] * (e - s) / e
    out["push"] = p * (tl + CATCH_MU) / (1 - CATCH_MU * tl)
    out["lock_deg"] = math.degrees(math.atan(1 / CATCH_MU))
    gap = GAP + d["u_lo"]                                # leaf to PCB edge, closed, nominal
    out["pcb_closed"] = gap
    out["pcb_closed_min"] = gap - s - PCB_HOLE_PLAY
    # bent: the leaf's inner face is furthest in at the PCB's top face (Z 0) in the PCB band
    out["bend_pcb"] = bend(0.0)
    out["pcb_bent"] = gap - bend(0.0)
    out["pcb_bent_min"] = min(gap + sh - PCB_HOLE_PLAY - _leaf_bend(e + sh)[2](0.0) for sh in (-s, s))
    out["tip_bend"] = max(_leaf_bend(e + s)[2](d["zt"]), bend(d["zt"]))
    return out


def check_catch(top, backs):
    """The corner catches: strain, engagement (bump depth past the lip), least clearances closed
    and with the leaf bent to let the bump pass, forces; 3D: no interference closed (each catch's
    region of the front against each back shell), the bump hooks under the lip when the shells
    are pulled apart, the leaf clears the PCB and the board's parts. Returns the report lines."""
    d = catch_dims()
    b = layout_board()
    pcb = rrect_prism(W, H, PCB_R, -PCB_T, 0.0)
    lines = []
    for side, t, w in CATCHES:
        n1, n2 = catch_numbers(w), catch_numbers(w, rev2=True)
        region = _catch_place(side, t, _catch_rect(-3.0, WALL + 0.5, LEAF_ROOT - 1.0, d["zt"] + 1.5,
                                                   w + 2 * CATCH_SLOT + 2.0))
        tr = top & region
        for rev2, back in backs:
            br = back & region
            tag = "rev2" if rev2 else "rev1"
            v = (tr & br).volume
            if v > 0.01:
                raise SystemExit(f"catch {side}: front and {tag} back interfere closed ({v:.3f} mm3)")
            # pulled apart past the Z clearance with the leaf unbent, the bump must hit the lip
            hook = (tr & Pos(0, 0, -(BUMP_ZCLR + 0.2)) * br).volume
            free = (tr & Pos(0, 0, -(BUMP_ZCLR - 0.05)) * br).volume
            if hook < 0.05 or free > 0.01:
                raise SystemExit(f"catch {side} ({tag}): bump does not hook ({hook:.3f}, {free:.3f} mm3)")
        leaf = catch_leaf(side, t, w)
        lp = leaf.distance_to(pcb)
        if lp < GAP + LEAF_IN - 0.01:
            raise SystemExit(f"catch {side}: leaf {lp:.2f} mm from the PCB")
        # board parts across the leaf's span: the leaf's inner face (bent at the tip) to each part
        v0 = t - w / 2 - CATCH_SLOT
        v1 = t + w / 2 + CATCH_SLOT
        near = []
        for ref, p in b.placed.items():
            q = p.rect()
            x0, y0, x1, y1 = q[0] - layout.OX, q[1] - layout.OY, q[2] - layout.OX, q[3] - layout.OY
            lo, hi, edge = (y0, y1, W - x1) if side == "right" else (x0, x1, y0)
            if hi > v0 and lo < v1:
                near.append((edge, ref, p.side))
        part = min(near) if near else (99.0, "-", "")
        tip = n1["tip_bend"]
        if part[0] + GAP + d["u_lo"] - tip < 0.3:
            raise SystemExit(f"catch {side}: bent leaf within 0.3 mm of {part[1]}")
        lines.append(
            f"catch {side} (board {'y' if side == 'right' else 'x'} {t - w / 2:.1f}-{t + w / 2:.1f}): leaf {w} x "
            f"{LEAF_T} mm, {d['zt'] - LEAF_ROOT:.1f} tall (root Z {LEAF_ROOT}, bump {d['bump']:.2f} mm at Z "
            f"{d['zb']:.1f}-{d['zt']:.2f}, ramps {CATCH_LEAD:.0f}/{CATCH_BACK:.0f} deg); engages {n1['engage']:.2f} mm "
            f"({n1['engage_min']:.2f}-{n1['engage_max']:.2f} rev1, {n2['engage_min']:.2f}-{n2['engage_max']:.2f} rev2 "
            f"with the shells shifted {n1['shift']:.2f}/{n2['shift']:.2f}); strain {n1['strain'] * 100:.2f} % "
            f"(worst {n2['strain_max'] * 100:.2f} %)")
        lines.append(
            f"  closed: lip {LIP_CLR}, crest {BUMP_CLR}, ramps {BUMP_ZCLR} in Z, pocket {POCKET_CLR}; no interference "
            f"with either back; hooks when pulled. PCB edge {n1['pcb_closed']:.2f} mm (worst {n2['pcb_closed_min']:.2f}), "
            f"bent {n1['pcb_bent']:.2f} (worst {min(n1['pcb_bent_min'], n2['pcb_bent_min']):.2f}; the leaf "
            f"bends {n1['bend_pcb']:.2f} there); nearest part {part[1]} {part[0] + GAP + d['u_lo'] - tip:.2f} mm "
            f"from the bent tip")
        lines.append(
            f"  force: {n1['k']:.1f} N/mm at the bump; closes with about {n1['push']:.0f} N, holds about "
            f"{n1['pull']:.0f} N ({n1['pull_min']:.0f} N at the least engagement; mu {CATCH_MU}, locks only over "
            f"{n1['lock_deg']:.0f} deg)")
    return lines


def catch_test():
    """--catch-test: the top-right corner of the front and of the back shell with both catches
    (catch_test_top.stl face-down, catch_test_back.stl floor down), to try the click before
    printing whole shells. They mate exactly like the shells."""
    x0 = cad(CATCH_TEST_X, 0)[0]
    y0 = cad(0, CATCH_TEST_Y)[1]
    region = box((x0 + OUT_W / 2 + 1) / 2, (y0 + OUT_H / 2 + 1) / 2, OUT_W / 2 + 1 - x0, OUT_H / 2 + 1 - y0,
                 BOT - 1, TOP + 1)
    top, back = top_shell() & region, back_shell() & region
    v = (top & back).volume
    assert v < 0.01, f"catch test pieces interfere ({v:.3f} mm3)"
    for line in check_catch(top, [(False, back)]):
        print("  " + line)
    face_down = Pos(0, 0, TOP) * Rot(0, 180, 0)
    for name, part in (("catch_test_top.stl", face_down * top), ("catch_test_back.stl", Pos(0, 0, -BOT) * back)):
        export_stl(part, os.path.join(OUT, name))
        bb = part.bounding_box()
        print(f"  wrote {name}: {bb.size.X:.1f} x {bb.size.Y:.1f} x {bb.size.Z:.1f} mm, {part.volume / 1000:.1f} cm3")


CATCH_TEST_X, CATCH_TEST_Y = 46.0, 22.0    # the test pieces: board x from here to the right edge, y to here


def back_shell(rev2=False):
    """Back shell in assembled position (Z from BOT to 0; the PCB rests on its ledge at -PCB_T).

    One colour, with a plain outer face. rev2: the variant for new prints (battery_door(rev2)):
    a looser seam tongue (RAB_CLR_REV2 per side; the top shell is unchanged) and, at the door's
    latch end, a thumb scoop instead of the catch rib and pry notch."""
    # outer body: EDGE_CH chamfer round the floor (on the bed), SEAM_CH chamfer on the outer
    # wall's top edge (the V reveal with the top shell's lap), which stops SEAM_GAP below the
    # lap; the tongue (RAB_KEEP, inner part of the wall) runs up inside the lap to -SEAM_GAP
    shell = chamfered_prism(OUT_W, OUT_H, OUT_R, BOT, -RABBET - SEAM_GAP, c_lo=EDGE_CH, c_hi=SEAM_CH)
    t = WALL - RAB_KEEP + ((RAB_CLR_REV2 - RAB_CLR) if rev2 else 0.0)
    shell += rrect_prism(OUT_W - 2 * t, OUT_H - 2 * t, OUT_R - t, BOT + 1.0, -SEAM_GAP)
    shell -= rrect_prism(OUT_W - 2 * WALL, OUT_H - 2 * WALL, OUT_R - WALL, FLOOR_IN, 0.1)
    if rev2:   # tongue 2.0 - 1.25 + 0.05 = 0.8 mm: two perimeters
        ti = WALL + RAB_IN_REV2
        ring = rrect_prism(OUT_W - 2 * WALL + 0.02, OUT_H - 2 * WALL + 0.02, OUT_R - WALL + 0.01,
                           -RABBET - SEAM_GAP - 0.2, -SEAM_GAP)
        shell += ring - rrect_prism(OUT_W - 2 * ti, OUT_H - 2 * ti, OUT_R - ti, -RABBET - 1.0, 0.1)
    # PCB ledge along the walls: its top at -PCB_T supports the PCB's bottom face on a 0.8 mm
    # border (the cut-out is the board outline inset 0.8 mm per side)
    ledge = rrect_prism(OUT_W - 2 * WALL, OUT_H - 2 * WALL, OUT_R - WALL, FLOOR_IN, -PCB_T)
    ledge -= rrect_prism(W - 1.6, H - 1.6, PCB_R - 0.8, FLOOR_IN - 0.1, 0)
    shell += ledge
    # standoffs up to the PCB bottom: STANDOFF_R (7 mm), fused to the ledge and wall (every hole
    # is 3.5 mm from a board edge), a ROOT fillet cone at the floor and four GUSSET_T gussets
    # (clipped to the inside of the wall); SCREW_CLR_D M2 bore and CBORE_D x CBORE_H
    # counterbores from the outside for the socket heads, with an EF chamfer at their mouth
    inside = rrect_prism(OUT_W - 2 * WALL + 0.2, OUT_H - 2 * WALL + 0.2, OUT_R - WALL + 0.1, FLOOR_IN - 0.1, 0)
    for hx, hy in layout.HOLES:
        cx, cy = cad(hx, hy)
        shell += Pos(cx, cy, (FLOOR_IN + -PCB_T) / 2) * Cylinder(STANDOFF_R, -PCB_T - FLOOR_IN)
        shell += cone(cx, cy, STANDOFF_R + ROOT, FLOOR_IN - 0.01, STANDOFF_R, FLOOR_IN + ROOT)
        g = None
        for ang in (0, 90, 180, 270):
            r0, r1 = STANDOFF_R - 0.5, STANDOFF_R + GUSSET_L
            prof = Polygon((r0, FLOOR_IN - 0.01), (r1, FLOOR_IN - 0.01), (r0, FLOOR_IN + GUSSET_H), align=None)
            gus = Pos(cx, cy, 0) * Rot(0, 0, ang) * Rot(90, 0, 0) * extrude(prof, GUSSET_T / 2, both=True)
            g = gus if g is None else g + gus
        shell += g & inside
        shell -= Pos(cx, cy, (BOT - PCB_T) / 2) * Cylinder(SCREW_CLR_D / 2, -PCB_T - BOT + 0.2)
        shell -= Pos(cx, cy, BOT + CBORE_H / 2 - 0.05) * Cylinder(CBORE_D / 2, CBORE_H + 0.1)
        shell -= cone(cx, cy, CBORE_D / 2 + EF, BOT - 0.01, CBORE_D / 2, BOT + EF)
    # battery compartment opening with a 1 mm recess for the door (rounded corners, EF chamfer
    # round the recess edge on the bed)
    bx, by, bw, bh = board_rect(batt_opening())
    shell -= box(bx, by, bw, bh, BOT - 0.1, FLOOR_IN + 0.1)
    shell -= rrect_prism(bw + 3.0, bh + 3.0, RECESS_R, BOT - 0.1, BOT + 1.0, bx, by)
    shell -= frustum(bw + 3.0, bh + 3.0, RECESS_R, BOT + EF, BOT - 0.01, EF + 0.01, bx, by)
    edge = by + bh / 2
    if not rev2:
        # catch rib (the first door's hook rested on its top), with a buttress behind it up to
        # 1 mm under the rib top, and a pry notch in the door recess under it. The present
        # door's hooks catch the floor either side of the rib; its fingernail pocket is in the notch.
        shell += box(bx, edge + 0.7, 15.0, 1.4, FLOOR_IN - 0.05, RIB_Z + 0.15)
        shell += box(bx, edge + 2.0, 15.0, 1.4, FLOOR_IN - 0.05, RIB_Z - 1.0)
        shell -= box(bx, edge + 1.5, 10.0, 2.0, BOT - 0.1, BOT + 1.0)
    else:
        # thumb scoop beyond the latch end of the recess, as deep as the recess (0.6 mm floor
        # over it, like the lip), EF chamfer at its mouth: the door's thumb tab lies in it
        sy = edge + 1.5 + SCOOP_L / 2 - 0.5
        shell -= rrect_prism(SCOOP_W, SCOOP_L + 1.0, 1.0, BOT - 0.1, BOT + 1.0, bx, sy)
        shell -= frustum(SCOOP_W, SCOOP_L + 1.0, 1.0, BOT + EF, BOT - 0.01, EF + 0.01, bx, sy)
    # USB-C plug pocket, microSD slot and finger scallop at the top edge
    shell -= usb_pocket() + sd_cutout(-1.2, 0.1)
    # the corner catches' leaves (CATCHES), freed from the wall, the tongue and the PCB ledge
    for side, t, w in CATCHES:
        shell -= catch_slots(side, t, w)
        shell += catch_leaf(side, t, w)
    return shell


# ---------------------------------------------------------- battery door ---
# Retrofit-safe constants of the back shell's door seat (do not change: shells are printed)
RECESS_R = 1.0              # door recess corner radius
RIB_Z = FLOOR_IN + 8.5      # catch rib top - 0.15 (the first door's hook rested on it)
SCOOP_W, SCOOP_L = 16.0, 5.0    # rev2 thumb scoop beyond the latch end of the recess

# The door (battery_door): a flat plate in the recess with a plug into the opening, two rigid
# hinge tabs at the bottom end (-y) that go over the floor, and at the latch end (+y) a latch
# band: the plate's end strip, cut free by a slot and split in the middle, so it is two
# cantilevers lying in the plate. Each carries a hook that reaches over the floor's inner face
# past the opening edge. Pushing the band's middle toward the hinge end bends the cantilevers
# in the plate's own plane (along the printed layers, PETG's strong direction) and pulls the
# hooks in; the door then lifts out.
DOOR_CLR = 0.4              # plate to recess wall, per side
DOOR_R = RECESS_R - DOOR_CLR
PLUG_CLR = 0.4              # plug to opening edge, sides and latch end, per side
HINGE_GAP = 0.6             # plug to opening edge at the hinge end
TAB_X, TAB_W = 16.0, 10.0   # hinge tabs: centres at +-TAB_X, width
TAB_VC = 0.5                # hinge tab underside to the floor at the opening edge (vertical play)
TAB_L, TAB_T, TAB_P = 1.5, 0.6, 0.8   # tab reach past the opening edge, tip height, post width
HOOK_VC = 0.2               # hook underside to the floor at the opening edge (vertical play)
HOOK_PT, HOOK_T = 1.2, 0.4  # hook post width (y), hook tip height
SLOT_ROOT = 1.0             # band slot width at the cantilever roots (round end) ...
BAND_ROOT = 30.7            # ... which are at x = +-BAND_ROOT
SPLIT = 0.6                 # gap between the two cantilever tips at x = 0
NAIL_W, NAIL_T = 9.0, 0.5   # fingernail pocket width; plate left under it (and thumb tab thickness)
THUMB_W, THUMB_L = 12.0, 2.5    # rev2 thumb tab: width, reach past the recess edge into the scoop
LATCH = {   # per shell: hook centre x, width, reach past the opening edge; band width (plate
            # edge to slot), slot width at the tips (the stop)
    False: dict(hook_x=10.8, hook_w=5.0, hook_e=1.0, band_w=3.0, slot_tip=2.6),  # printed shell: hooks clear its rib (+-7.5)
    True: dict(hook_x=5.0, hook_w=5.0, hook_e=1.2, band_w=3.5, slot_tip=2.0),    # rev2: no rib, hooks near the tips
}
PETG_E = 2000.0             # MPa, for the latch force estimate


def _latch_geom(rev2=False):
    """Latch band numbers (CAD y): opening edge, post faces, band and plate edges, cantilever length."""
    bx, by, bw, bh = board_rect(batt_opening())
    ep = by + bh / 2
    y_pf = ep - PLUG_CLR                    # hook post outer face
    y_pb = y_pf - HOOK_PT                   # hook post inner face
    plate_edge = ep + 1.5 - DOOR_CLR
    y_bi = plate_edge - LATCH[rev2]["band_w"]     # band inner edge (slot edge)
    return dict(ep=ep, y_pf=y_pf, y_pb=y_pb, y_bi=y_bi, plate_edge=plate_edge, L=BAND_ROOT - SPLIT / 2,
                **LATCH[rev2])


def latch_numbers(rev2=False):
    """Latch band mechanics, each cantilever taken as a uniform in-plane beam loaded at its tip:
    the band section is the plate strip (1 mm) from the slot to the plate edge plus the plug
    layer (0.6 mm) from the slot to the hook posts' outer face. Returns a dict: tip travel to free the hooks
    (hook_e + 0.15 at the hook centre), strain there and at the slot stop, push force at the
    tips and the spring force per hook when freed (PETG_E)."""
    g = _latch_geom(rev2)
    layers = [(g["y_bi"], g["plate_edge"], 1.0), (g["y_bi"], g["y_pf"], FLOOR - 1.0)]
    A = sum((b - a) * h for a, b, h in layers)
    yc = sum((b - a) * h * (a + b) / 2 for a, b, h in layers) / A
    I = sum(h * (b - a) ** 3 / 12 + (b - a) * h * ((a + b) / 2 - yc) ** 2 for a, b, h in layers)
    c = max(g["plate_edge"] - yc, yc - g["y_bi"])
    L = g["L"]
    xh = BAND_ROOT - g["hook_x"]                       # root to hook centre
    ratio = xh ** 2 * (3 * L - xh) / (2 * L ** 3)       # hook / tip deflection
    free = g["hook_e"] + 0.15
    tip = free / ratio
    strain = lambda d: 3 * d * c / L ** 2
    k_tip = 3 * PETG_E * I / L ** 3
    return dict(tip=tip, strain=strain(tip), stop=g["slot_tip"], strain_stop=strain(g["slot_tip"]),
                hook_stop=g["slot_tip"] * ratio, push=2 * k_tip * tip, hook_force=k_tip * tip, I=I, c=c)


def latch_strain(rev2=False):
    """Peak bending strain of the door's latch band when its hooks just clear the floor edge."""
    return latch_numbers(rev2)["strain"]


def _yz_prism(pts, cx, w):
    """Prism of the (y, z) profile `pts`, w wide in x, centred on x = cx."""
    s = extrude(Polygon(*pts, align=None), w / 2, both=True)    # XY sketch: x = Y, y = Z
    return Pos(cx, 0, 0) * Rot(0, 0, 90) * Rot(90, 0, 0) * s


def door_hooks(rev2=False, pushed=0.0):
    """The two latch hooks (posts on the band, each hook reaching over the floor past the opening
    edge), moved `pushed` mm toward the hinge end (a released latch, for the path checks).

    Profile: the post rises from the plate at PLUG_CLR inside the opening edge; the hook's
    underside is a 45-degree overhang that passes HOOK_VC above the floor's edge, so pulling on
    the door wedges the band in rather than binding it; a 45-degree lead-in on top meets the
    opening's outer edge when the door is pressed home."""
    g = _latch_geom(rev2)
    bx = board_rect(batt_opening())[0]
    ep, y_pf, y_pb, e = g["ep"] - pushed, g["y_pf"] - pushed, g["y_pb"] - pushed, g["hook_e"]
    zu = FLOOR_IN + HOOK_VC - (ep - y_pf)          # underside at the post face
    zt = FLOOR_IN + HOOK_VC + e + HOOK_T           # top of the hook tip
    top = zt + e + 0.2
    prof = [(y_pb, BOT + 1.0 - 0.01), (y_pf, BOT + 1.0 - 0.01), (y_pf, zu), (ep + e, FLOOR_IN + HOOK_VC + e),
            (ep + e, zt), (ep - 0.2, top), (y_pb, top)]
    return [_yz_prism(prof, bx + s * g["hook_x"], g["hook_w"]) for s in (-1, 1)]


def battery_door(rev2=False, pushed=0.0):
    """Battery door in assembled position (= print position: plate on the bed, hooks up). rev2:
    the door for back_shell(rev2) (hooks near the middle, thumb tab into the scoop); pushed:
    hooks moved that far toward the hinge (see door_hooks)."""
    bx, by, bw, bh = board_rect(batt_opening())
    g = _latch_geom(rev2)
    em, ep = by - bh / 2, g["ep"]
    # plate: DOOR_CLR per side in the recess, DOOR_R corners, EF chamfer on its bed edge
    door = Pos(bx, by, 0) * chamfered_prism(bw + 3.0 - 2 * DOOR_CLR, bh + 3.0 - 2 * DOOR_CLR, DOOR_R,
                                            BOT, BOT + 1.0, c_lo=EF)
    # plug into the opening: PLUG_CLR at the sides and latch end, HINGE_GAP at the hinge end
    y0, y1 = em + HINGE_GAP, g["y_pf"]
    door += box(bx, (y0 + y1) / 2, bw - 2 * PLUG_CLR, y1 - y0, BOT + 1.0 - 0.01, BOT + FLOOR)
    # hinge tabs: over the floor past the hinge-end edge, TAB_L reach; the 45-degree underside
    # passes TAB_VC over the floor's edge (pulling on that end wedges the door toward the latch
    # end, where the plate stops it), the post is TAB_P wide on the plug
    yp = em + HINGE_GAP
    zu = FLOOR_IN + TAB_VC - HINGE_GAP
    tt = FLOOR_IN + TAB_VC + TAB_L + TAB_T
    prof = [(yp + TAB_P, BOT + 1.0), (yp, BOT + 1.0), (yp, zu), (em - TAB_L, FLOOR_IN + TAB_VC + TAB_L),
            (em - TAB_L, tt), (yp + TAB_P - 0.5, tt), (yp + TAB_P, tt - 0.5)]   # 0.5 chamfer: clears BT2's cell
    for s in (-1, 1):
        door += _yz_prism(prof, bx + s * TAB_X, TAB_W)
    # thumb tab (rev2) reaching into the shell's scoop, NAIL_T thick
    if rev2:
        y0, y1 = g["plate_edge"] - EF - 0.2, ep + 1.5 + THUMB_L     # covers the plate's EF chamfer
        door += box(bx, (y0 + y1) / 2, THUMB_W, y1 - y0, BOT, BOT + NAIL_T)
    # latch band: tapered slot (SLOT_ROOT at the round-ended roots, slot_tip at the middle) and
    # the split between the tips
    yb, wt = g["y_bi"], g["slot_tip"]
    slot = Polygon((-BAND_ROOT, yb), (BAND_ROOT, yb), (BAND_ROOT, yb - SLOT_ROOT), (0, yb - wt),
                   (-BAND_ROOT, yb - SLOT_ROOT), align=None)
    cut = Pos(bx, 0, BOT + FLOOR / 2) * extrude(slot, FLOOR / 2 + 0.1, both=True)
    for s in (-1, 1):
        cut += Pos(bx + s * BAND_ROOT, yb - SLOT_ROOT / 2, BOT + FLOOR / 2) * Cylinder(SLOT_ROOT / 2, FLOOR + 0.2)
    ys0, ys1 = yb - wt - 0.1, ep + 1.5 + SCOOP_L + 1.0
    cut += box(bx, (ys0 + ys1) / 2, SPLIT, ys1 - ys0, BOT - 0.1, BOT + FLOOR + 0.1)
    door -= cut
    # fingernail pocket (printed shell): the plate is NAIL_T thick for NAIL_W under the pry notch,
    # leaving a gap under the floor lip for a fingernail to push the band and lift the door
    if not rev2:
        y0, y1 = ep - 0.1, g["plate_edge"] + 0.1
        door -= box(bx, (y0 + y1) / 2, NAIL_W, y1 - y0, BOT + NAIL_T, BOT + 1.1)
    for h in door_hooks(rev2, pushed):
        door += h
    return door


CELL_R = 5.25               # AAA, 10.5 mm max diameter
CELL_L = 44.5
CELL_Y, CELL_Z = 5.5, 1.3 + CELL_R   # cell axes in the holder model: y +-5.5, its cradle floor 1.3 mm up


@functools.lru_cache(maxsize=1)
def holders():
    """BT1 and BT2 as placed (CAD, below the PCB): the holder STEP model (hardware/lib) and its
    four AAA cells (CELL_R, axes over the cradle floor). Returns (solid, the BT courtyard boxes
    down to the holders' lowest point, for comparison)."""
    from build123d import import_step
    model = import_step(os.path.join(HERE, "../hardware/lib/calc.3dshapes/BAT-SMD_BH-AAA-B5AA001.step"))
    zlo = -PCB_T - model.bounding_box().max.Z
    b = layout_board()
    parts, rects = [], []
    for ref in ("BT1", "BT2"):
        r = b.placed[ref].rect()
        cx, cy, w, h = board_rect((r[0] - layout.OX, r[1] - layout.OY, r[2] - layout.OX, r[3] - layout.OY))
        parts.append(Pos(cx, cy, -PCB_T) * Rot(180, 0, 0) * model)      # bottom side: model z -> -z
        for s in (-1, 1):
            parts.append(Pos(cx, cy + s * CELL_Y, -PCB_T - CELL_Z) * Rot(0, 90, 0) * Cylinder(CELL_R, CELL_L))
        rects.append(box(cx, cy, w, h, zlo, -PCB_T))
    return Compound(parts), Compound(rects)


def check_door(back, rev2=False):
    """Fit and path checks of the battery door against the back shell, the holders with their
    cells (holders()) and the PCB: interference closed, the least clearances, the release
    (hooks pushed to the slot stop), the hinge swing with the hooks released and the slide that
    frees the tabs. Prints the results; returns a list of problems."""
    door = battery_door(rev2)
    hold, rects = holders()
    pcb = rrect_prism(W, H, PCB_R, -PCB_T, 0.0)
    bad = []

    def vol(a, b):
        try:
            return (a & b).volume
        except Exception:
            return 0.0

    for name, other in (("back shell", back), ("holders + cells", hold), ("PCB", pcb)):
        v = vol(door, other)
        if v > 1e-3:
            bad.append(f"door vs {name}: {v:.2f} mm3")
        print(f"  door closed vs {name}: {v:.3f} mm3", end="")
        if name == "back shell":     # the plate rests on the recess floor; gaps of what is above it
            bx, by, bw, bh = board_rect(batt_opening())
            above = door - box(bx, by, bw + 4, bh + 4, BOT - 1, BOT + 1.0 + 1e-3)
            print(f" (plate on the recess floor); plug, tabs and hooks: least gap {above.distance_to(back):.2f} mm")
        else:
            print(f", least gap {door.distance_to(other):.2f} mm")
    print(f"  door closed vs BT courtyard boxes (to the holders' lowest point): "
          f"{vol(door, rects):.3f} mm3, least gap {door.distance_to(rects):.2f} mm")
    n = latch_numbers(rev2)
    hooks = door_hooks(rev2)
    gap = min(h.distance_to(back) for h in hooks)
    print(f"  hooks: least gap to the shell {gap:.2f} mm (rib / floor); "
          f"vs holders + cells {min(h.distance_to(hold) for h in hooks):.2f} mm")
    pushed = door_hooks(rev2, n["hook_stop"])
    pv = sum(vol(h, hold) + vol(h, back) for h in pushed)
    pg = min(h.distance_to(hold) for h in pushed)
    print(f"  hooks at the slot stop ({n['hook_stop']:.2f} mm in): {pv:.3f} mm3, {pg:.2f} mm to holders/cells")
    if pv > 1e-3:
        bad.append("pushed hooks interfere")
    # release path: hooks freed by the band's travel (hook_e + 0.15), door swung out about the
    # plate's hinge-end top edge, then slid toward the latch end until the tabs are out
    bx, by, bw, bh = board_rect(batt_opening())
    free = battery_door(rev2, pushed=LATCH[rev2]["hook_e"] + 0.15)
    yp = by - bh / 2 - 1.5 + DOOR_CLR
    pose = lambda a, dy, dz=0.0: (Pos(0, dy, -dz) * Pos(0, yp, BOT + 1.0) * Rot(-a, 0, 0)
                                  * Pos(0, -yp, -(BOT + 1.0)) * free)
    clash = lambda *p: vol(pose(*p), back) + vol(pose(*p), hold)
    s_end = TAB_L
    path = [(dy, 0.0) for dy in np.arange(0.25, s_end + 0.01, 0.25)] + \
           [(s_end, dz) for dz in np.arange(0.5, 4.01, 0.5)]
    ok = []
    for a in np.arange(1.0, 15.1, 1.0):
        if clash(a, 0.0) > 1e-3:
            break                     # the swing itself is blocked from here on
        if all(clash(a, dy, dz) <= 1e-3 for dy, dz in path):
            ok.append(a)
    if not ok:
        bad.append("no clear release path (swing, slide, drop)")
        print("  release path: none found")
    else:
        print(f"  release path clear for a swing of {ok[0]:.0f}-{ok[-1]:.0f} deg about the plate's hinge edge, "
              f"a {s_end:.2f} mm slide toward the latch end and a {path[-1][1]:.0f} mm drop out")
    print("  door checks:", "OK" if not bad else bad)
    return bad


# --------------------------------------------------------------- keycaps ---
CAP_TOP = TOP + 1.2           # keycap top surface height when resting
FLANGE = 0.8                  # flange thickness
FLANGE_OVER = 1.0             # flange overhang per side (> KEY_CLR, so caps can't fall out)
FLANGE_OVER_DPAD = 0.7        # D-pad caps sit closer together


def flange_over(name):
    """Flange overhang per side of logical key `name`."""
    return FLANGE_OVER_DPAD if name in DPAD_KEYS else FLANGE_OVER


# Keycap legends: one legend per cap, as large as the cap's top face allows. Each legend is
# fitted into the face less CAP_MARGIN per side, no taller than CAP_CHAR_H; then legends of a
# kind share one size, so a row reads evenly: the digits set the size of every symbol legend
# (÷ × − + ( ) √ ...; a legend that does not fit at that size is smaller), and the words share
# one size per length (up to 3 letters, 4 and more; the soft keys F1-F5 apart). Words are centred on
# the middle of their lower-case letters (LINE_MID) so neighbouring words share a baseline.
CAP_MARGIN = 1.1       # mm of bare cap top around the legend, per side
CAP_CHAR_H = 5.0       # tallest legend: the digits are this tall
CAP_SIZE_MAX = 8.0     # font size limit (a lone − or . would otherwise grow to the cap width)


# Word keys print in DIN Alternate Bold capitals, the long words shortened: 3 mm Arial Black words closed up in
# PETG (their letter holes and gaps are under the ~0.7 mm a printed gap needs; legendcheck.py), and the word
# test (case.py --word-test) read cleanly in this style. Two sizes: four letters, and up to three.
FONT_DIN = "/System/Library/Fonts/Supplemental/DIN Alternate Bold.ttf"
CAP_WORDS = {"SHIFT": "SHFT", "ALPHA": "ALPH", "HOME": "HOME", "APPS": "APPS", "MENU": "MENU", "MATH": "MATH",
             "VARS": "VARS", "CLEAR": "CLR", "ENTER": "ENT", "SIN": "SIN", "COS": "COS", "TAN": "TAN", "LN": "LN",
             "LOG": "LOG", "ON": "ON", "CAS": "CAS"}


def cap_font(name):
    """Font of key `name`'s cap legend (None: FONT_MAIN)."""
    return FONT_DIN if name in CAP_WORDS else None


def _is_word(legend):
    return legend.isascii() and legend.isalnum() and any(ch.isalpha() for ch in legend)


def _cap_valign(legend):
    return "line" if _is_word(legend) and any(ch.islower() for ch in legend) else "box"


def _cap_fit(text, w, h, valign, font=None):
    """Largest font size (0.05 mm steps, <= CAP_SIZE_MAX) at which CAD markup `text` fits a
    w x h cap face less CAP_MARGIN per side and CAP_CHAR_H."""
    bw, bh = w - 2 * CAP_MARGIN, min(h - 2 * CAP_MARGIN, CAP_CHAR_H)

    def over(sz):        # > 1 if it does not fit
        x0, y0, x1, y1 = text_geom(text, sz, valign=valign, font=font).bounds
        tall = (y1 - y0) / bh if valign == "box" else max(-y0, y1) / (bh / 2)
        return max((x1 - x0) / bw, tall)
    size = min(CAP_SIZE_MAX, round(math.floor(3.0 / over(3.0) / 0.05) * 0.05, 2))
    while over(size) > 1:
        size = round(size - 0.05, 2)
    while size < CAP_SIZE_MAX and over(round(size + 0.05, 2)) <= 1:   # the guess used 3.0 mm growth
        size = round(size + 0.05, 2)
    return size


@functools.lru_cache(maxsize=None)
def cap_legend_sizes():
    """{key name: (CAD markup, font size, valign)} of every keycap legend (see above)."""
    nominal, cls = {}, {}
    for name, _, y, w, h in keys():
        if name in CAP_WORDS:
            t = CAP_WORDS[name]
            nominal[name] = (t, _cap_fit(t, w, h, "box", FONT_DIN), "box")
            cls[name] = ("din", len(t) > 3)
            continue
        leg = LEGEND.get(name, name)
        va = _cap_valign(leg)
        nominal[name] = (cad_text(leg), _cap_fit(cad_text(leg), w, h, va), va)
        kind = "digit" if leg.isdigit() else ("word", len(leg) > 3, h <= 6.0) if _is_word(leg) else "symbol"
        cls[name] = kind
    # the x key's lower-case x shared its size with sin, cos ... before they moved to CAP_WORDS; alone it
    # would grow to the whole cap (8 mm), so it keeps the printed size
    if "X" in nominal:
        nominal["X"] = (nominal["X"][0], min(nominal["X"][1], 4.45), nominal["X"][2])
    digit = min(s for n, (_, s, _) in nominal.items() if cls[n] == "digit")
    out = {}
    for n, (t, s, va) in nominal.items():
        if cls[n] in ("digit", "symbol"):
            s = min(s, digit)
        else:
            s = min(nominal[m][1] for m in nominal if cls[m] == cls[n])
        out[n] = (t, s, va)
    return out


def cap_legend(name, w, h):
    """Legend solid of key `name` on its cap top (inlaid INLAY deep at CAP_TOP), sized by
    cap_legend_sizes() (w, h are the cap's; the sizes were fitted to them)."""
    text, size, valign = cap_legend_sizes()[name]
    return label(text, size, 0, 0, CAP_TOP, valign=valign, font=cap_font(name))


GROUPS = tuple(keys_layout.PALETTE)        # fn, num, accent, alpha


def key_group(name):
    """Keycap colour group of logical key `name` (firmware/keys/layout.py PALETTE)."""
    return keys_layout.BY_NAME[name]["group"]


# Loose keys: each key is a solid cap that stands on its whole flat bottom, printed legend-up with the
# legend a second colour (two filament swaps). Its flange is wider than its hole and holds it in from the
# inside; at rest the flange lies against the ceiling and the flat bottom is LOOSE_GAP above the switch
# plunger, which is all the free play there is. A press closes the gap, then moves the plunger T_ACT to the
# click; after SW_H - SW_COVER of plunger travel the bottom lands on the switch cover.
# Key switch: XKB TS-1187A-B-A-B (hardware/ARCHITECTURE.md BOM, LCSC C318884; XKB drawing: operating force
# code B = 160 gf, height code A = 1.5 mm, travel 0.2 +-0.1 mm). Its 3D model: a 5.2 mm cover up to 1.2 mm
# and a 2.0 mm round plunger up to 1.5 mm.
SW_H = 1.5                    # switch height (plunger top) above the PCB
SW_COVER = 1.2                # switch cover top
SW_HALF = 2.6                 # switch cover 5.2 x 5.2
T_ACT = 0.2                   # nominal plunger travel to the click (+-0.1)
LOOSE_GAP = 0.5               # key bottom above the plunger at rest. Gap test (--loose-gaps): 0.15 held the
                              # switch pressed in the closed case, 0.5 clicks cleanly
CAP_SLIM = 0.15               # keys this much slimmer per side from just under the ceiling up: 0.4 mm a side
                              # in the key holes (with KEY_CLR alone the keys rubbed on the printed front)
BOSS_CLR = 0.4                # ON / ENTER keep this far from the low corner bosses (one nozzle width)
LOOSE_TEST = ["ON", "ENTER", "SHIFT", "APPS", "8", "F1", "UP", "LEFT"]
LOOSE_GAP_XY = 4.0             # between neighbouring flanges on the plate
LOOSE_ROW = {"single": 75.0, "test": 75.0, "rest": 125.0, "all": 135.0, "pick": 75.0}   # row length: compact plates round the bed centre
_CAPS = {}


def key_dims():
    """{key name: (x, y, w, h)} in board coordinates (keys())."""
    return {n: (x, y, w, h) for n, x, y, w, h in keys()}


def _low_bosses(x, y, w, h):
    """Offsets (cap frame, CAD orientation) of the low corner bosses (BOSS_LOW, under the ON / ENTER flanges)
    that reach into the key at board (x, y)."""
    out = []
    for hx, hy in layout.HOLES:
        if hy > H - 10 and abs(hx - x) < w / 2 + BOSS_LOW_R + BOSS_CLR and abs(hy - y) < h / 2 + BOSS_LOW_R + BOSS_CLR:
            out.append((hx - x, -(hy - y)))
    return out


def loose_cap(name, gap=LOOSE_GAP):
    """Loose key `name` in its own frame (centred, at its assembled height): solid, from a flat bottom `gap`
    above the switch plunger up to CAP_TOP, CAP_R corners, CAP_SLIM slimmer from just under the ceiling up.
    The flange (FLANGE thick, flange_over() a side) touches the ceiling and has a 45-degree chamfer under it
    (a printable overhang, legend-up). ON and ENTER are cut clear of the low corner bosses right up to the
    ceiling, so they go in with a slight tip (only the visible top's corner passes over the boss). Returns
    (body with the legend subtracted, legend solid); the legend is inlaid INLAY deep at CAP_TOP."""
    if (name, gap) in _CAPS:
        return _CAPS[(name, gap)]
    x, y, w, h = key_dims()[name]
    z0 = SW_H + gap
    over = flange_over(name)
    fl_bot = CEIL - FLANGE
    cap = rrect_prism(w, h, CAP_R, z0, CAP_TOP)
    if CAP_SLIM:                # the part in and above the key hole: more clearance in the hole
        cap -= rrect_prism(w + 4, h + 4, CAP_R, CEIL - 0.6, CAP_TOP + 0.1) - \
            rrect_prism(w - 2 * CAP_SLIM, h - 2 * CAP_SLIM, CAP_R, CEIL - 0.7, CAP_TOP + 0.2)
    cap += box(0, 0, w + 2 * over, h + 2 * over, fl_bot, CEIL)
    with BuildPart() as ch:
        with BuildSketch(Plane.XY.offset(fl_bot + 0.01)):
            Rectangle(w + 2 * over, h + 2 * over)
        extrude(amount=over + 0.01, dir=(0, 0, -1), taper=45)
    cap += ch.part
    for bx, by in _low_bosses(x, y, w, h):
        z1 = CEIL + 0.01
        cap -= Pos(bx, by, (z0 - 0.1 + z1) / 2) * Cylinder(BOSS_LOW_R + BOSS_CLR, z1 - z0 + 0.1)
    legend = cap_legend(name, w, h)
    _CAPS[(name, gap)] = (cap - legend, legend)
    return _CAPS[(name, gap)]


def keys_assembled():
    """[(name, body, legend)] of every loose key in its place in the assembled case, at rest (keys() order)."""
    out = []
    for n, x, y, w, h in keys():
        body, legend = loose_cap(n)
        cx, cy = cad(x, y)
        out.append((n, Pos(cx, cy, 0) * body, Pos(cx, cy, 0) * legend))
    return out


def switches():
    """Every key switch's cover and plunger (SW_HALF, SW_COVER, SW_H), as one compound in assembled position."""
    sw = []
    for _, x, y, _, _ in keys():
        cx, cy = cad(x, y)
        sw.append(Pos(cx, cy, SW_COVER / 2) * Box(2 * SW_HALF, 2 * SW_HALF, SW_COVER))         # cover
        sw.append(Pos(cx, cy, (SW_COVER + SW_H) / 2) * Box(2.0, 2.0, SW_H - SW_COVER))          # plunger
    return Compound(sw)


def check_keys(top, asm=None):
    """3D check of the loose keys at rest (keys_assembled(), or `asm`) against the front `top` and the
    switches: prints and returns the interferences over 0.05 mm^3."""
    bodies = Compound([b for _, b, _ in (asm or keys_assembled())])
    bad = []
    for what, other in (("front", top), ("switches", switches())):
        v = (bodies & other).volume
        if v > 0.05:
            bad.append(f"keys vs {what}: {v:.2f} mm3")
    print("  loose keys vs the front and the switches:", "no interference" if not bad else bad)
    return bad


def key_stack():
    """Print the loose keys' stack-up (mm above the PCB)."""
    z0 = SW_H + LOOSE_GAP
    print(f"  loose keys: {CAP_TOP - z0:.2f} tall, bottom {z0:.2f} ({LOOSE_GAP} over the plunger), flange "
          f"{CEIL - FLANGE:.2f}-{CEIL:.2f}, top {CAP_TOP:.2f}; click after {LOOSE_GAP + T_ACT:.2f}, bottom on the "
          f"switch cover after {LOOSE_GAP + SW_H - SW_COVER:.2f}")


def loose_keys(pick=None, top=None, tag=""):
    """The loose keys legend-up on plates, after check_keys() against the front `top` (default top_shell())
    and the switches: keys_loose{tag}_{single,test,rest,all}_{base,legend}.stl (all 50 keys, a short test
    plate, the rest, one key); with a `tag` (a front variant) only the all plate; with `pick` (names, for
    reprints) only those keys, on keys_loose_pick_{base,legend}.stl. Returns keys_assembled()."""
    asm = keys_assembled()
    if check_keys(top if top is not None else top_shell(), asm):
        raise SystemExit("loose keys: 3D interference")
    dims = key_dims()
    zb = SW_H + LOOSE_GAP
    names_all = sorted(dims)
    plates = (("single", ["APPS"]), ("test", LOOSE_TEST),
              ("rest", [n for n in names_all if n not in LOOSE_TEST]), ("all", names_all))
    if tag:
        plates = (("all", names_all),)
    if pick:          # --pick: only these caps, on keys_loose_pick_* (reprints)
        plates = (("pick", pick),)
    for plate, names in plates:
        bodies, legends, x, y, row_h = [], [], 0.0, 0.0, 0.0
        for n in sorted(names, key=lambda n: (-dims[n][2], n)):
            w, h = dims[n][2:4]
            over = flange_over(n)
            fw, fh = w + 2 * over, h + 2 * over
            if x and x + fw > LOOSE_ROW[plate]:
                x, y, row_h = 0.0, y + row_h + LOOSE_GAP_XY, 0.0
            place = Pos(x + fw / 2, y + fh / 2, -zb)
            body, legend = loose_cap(n)
            bodies.append(place * body)
            legends.append(place * legend)
            x += fw + LOOSE_GAP_XY
            row_h = max(row_h, fh)
        stem = f"keys_loose{tag}_{plate}"
        export_stl(Compound(bodies), os.path.join(OUT, f"{stem}_base.stl"))
        export_stl(Compound(legends), os.path.join(OUT, f"{stem}_legend.stl"))
        print(f"  {stem}: {len(names)} caps, plate {x:.0f} x {y + row_h:.0f} mm")
    return asm


LOOSE_GAP_TEST = (0.5, 0.7, 0.9)   # gap-test keys: bottom-to-plunger gap; engraved 5, 7, 9 (tenths of a mm)


FONT_DIN_COND = "/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf"
# Word-legend test: the word keys whose 3 mm Arial Black legends closed up, in three styles, sized as
# a family would be (one size per style and kind). (style, key, text, font, kind)
# A: full words in DIN Condensed (their letter holes are ~0.5 mm: legendcheck expects them to close up; three
# keys to check that), B: DIN Alternate capitals, short forms of the 5-letter words, C: symbols.
WORD_TEST = [("A", n, t, FONT_DIN_COND, "word") for n, t in [("SHIFT", "SHIFT"), ("ENTER", "ENTER"), ("HOME", "HOME")]] + \
            [("B", n, t, FONT_DIN, "word") for n, t in
             [("SHIFT", "SHFT"), ("ALPHA", "ALPH"), ("MENU", "MENU"), ("HOME", "HOME"), ("APPS", "APPS"),
              ("VARS", "VARS"), ("MATH", "MATH")]] + \
            [("B", n, t, FONT_DIN, "short") for n, t in [("ENTER", "ENT"), ("CLEAR", "CLR"), ("SIN", "SIN")]] + \
            [("C", n, t, FONT_DIN, "symbol") for n, t in [("ALPHA", "α"), ("ENTER", "↵"), ("MENU", "≡"), ("CLEAR", "C")]] + \
            [("R", n, None, None, "reprint") for n in ("0", "4", "F1")]     # as in the full set (blobbed there)


def word_test():
    """keys_loose_words_{base,legend}.stl: WORD_TEST as usable loose keys (0.5 mm gap), legend-up, rows by
    style (told apart by their legends, compared in place in the case)."""
    dims = key_dims()
    zb = SW_H + LOOSE_GAP
    fits = {}
    for st, n, t, f, kind in WORD_TEST:
        if kind == "reprint":
            continue
        w, h = dims[n][2:4]
        fits.setdefault((st, kind), []).append(_cap_fit(t, w, h, "box", f))
    fits[("A", "word")] += [_cap_fit(t, 12.6, 7.6, "box", FONT_DIN_COND) for t in ("ALPHA", "CLEAR", "MENU")]   # the whole family's size
    digit = cap_legend_sizes()["8"][1]
    bodies, legends, x, y, row = [], [], 0.0, 0.0, None
    for st, n, t, f, kind in WORD_TEST:
        w, h = dims[n][2:4]
        fw, fh = w + 2 * flange_over(n), h + 2 * flange_over(n)
        if row is not None and (st != row or x + fw > 90):
            x, y = 0.0, y + fh + LOOSE_GAP_XY
        row = st
        body, leg = loose_cap(n)
        if kind == "reprint":
            print(f"  {st} {n:6s} as in the full set")
        else:
            size = min(fits[(st, kind)]) if kind != "symbol" else min(_cap_fit(t, w, h, "box", f), digit)
            body, leg = body + leg, label(t, size, 0, 0, CAP_TOP, font=f)
            body -= leg
            print(f"  {st} {n:6s} {t!r:8s} size {size:.2f}")
        place = Pos(x + fw / 2, y + fh / 2, -zb)
        bodies.append(place * body); legends.append(place * leg)
        x += fw + LOOSE_GAP_XY
    export_stl(Compound(bodies), os.path.join(OUT, "keys_loose_words_base.stl"))
    export_stl(Compound(legends), os.path.join(OUT, "keys_loose_words_legend.stl"))


def loose_gap_test():
    """keys_loose_gaps.stl: three APPS-size loose keys 0.5 / 0.7 / 0.9 mm above the plunger (the 0.15 mm key
    held its switch pressed in the closed case), each with its gap in tenths engraved 0.4 deep in the top
    (one colour), legend-up on one plate."""
    size = cap_legend_sizes()["8"][1]
    parts, x = [], 0.0
    for g in LOOSE_GAP_TEST:
        body, leg = loose_cap("APPS", g)
        mark = label(f"{round(g * 10)}", size, 0, 0, CAP_TOP)
        w, h = key_dims()["APPS"][2:4]
        fw = w + 2 * flange_over("APPS")
        parts.append(Pos(x + fw / 2, 0, -(SW_H + g)) * ((body + leg) - mark))
        print(f"  gap {g}: key {CAP_TOP - SW_H - g:.2f} tall, engraved {round(g * 10)}")
        x += fw + LOOSE_GAP_XY
    export_stl(Compound(parts), os.path.join(OUT, "keys_loose_gaps.stl"))

_board = None


def clean_out():
    """Remove STL files from OUT that no build writes any more: the retired spring key mats (keymat_*,
    keys_test_*, the *_easy* fronts), their peg-free front top_shell_loose_base.stl (now top_shell_base.stl),
    the old loose caps (keycaps_*), the transfer jig and its shims, the old top-shell legends and the files
    of earlier designs ("sk_", overlay cut template and films, back logo). render_keys_* are rewritten by
    the default build. Other files (sliced G-code) are left alone."""
    import shutil
    for f in os.listdir(OUT):
        p = os.path.join(OUT, f)
        if f == "coupon" and os.path.isdir(p):
            shutil.rmtree(p)
            continue
        if not f.endswith(".stl"):
            continue
        if (f.startswith(("keycaps_", "keymat_", "render_keys_", "keys_test_", "jig_shim_", "sk_",
                          "overlay_cut_template", "overlay_vinyl", "overlay_film", "overlay_paper_test"))
                or "_easy" in f
                or f in ("top_shell_loose_base.stl", "transfer_jig.stl", "top_shell_shift.stl", "top_shell_alpha.stl",
                         "back_shell_logo.stl", "overlay_jig.stl")):
            os.remove(p)


def layout_board():
    """The placed PCB (hardware/gen/layout.build from hardware/build/calc.net), built once and
    cached. Its part rectangles are in absolute board coordinates: subtract layout.OX / OY
    to get the board-relative coordinates cad() expects."""
    global _board
    if _board is None:
        _board = layout.build(os.path.join(HERE, "../hardware/build/calc.net"), design.LAYOUT)
    return _board


# ------------------------------------------------------ front variants ---
# Variants of the top shell for new prints (enclosure/V3_THIN.md). Both fit the printed back
# shells (rev1 and rev2) unchanged: the seam, the lap, the bosses and the insert holes do not
# depend on the ceiling height. The default build never calls set_front().
#   clips: stronger display snap fingers: lip 0.25 -> 0.5 mm, 0.9/0.8 -> 1.0/0.8 mm thick,
#          5 -> 7 mm wide, the slot 0.2 mm deeper into the plate (MIN_SKIN left), a whole
#          layer of flat seat, and a second finger on the right edge (six in all).
#   low:   clips, plus the display 1.2 mm lower (panel back 3.17 over the PCB: the folded FPC
#          still clears the tallest parts under it) and everything above it with it: ceiling
#          7.27, face 8.87, loose keys 1.2 mm shorter (same bottom and gap over the plunger).
#          The 2.54 mm headers J5/J6 must be cut to 7.7 mm above the PCB.
LOW_DROP = 1.2                   # low: display, ceiling, face and caps this much lower
CLIP_FINGERS = [("top", 35.0), ("top", 57.0), ("bottom", 30.0), ("bottom", 62.0),
                ("right", 24.0), ("right", 56.0)]
# Estimated maximum body heights (mm over the PCB) of the parts under the display, by
# footprint (datasheet maxima where known): for the low variant's clearance report.
PART_H = [("R_0402", 0.40), ("C_0402", 0.55), ("R_0603", 0.55), ("C_0603", 0.95), ("LED_0603", 0.80),
          ("C_0805", 1.45), ("R_2512", 0.70), ("SOT-23", 1.25), ("WSON", 0.80), ("MSOP", 1.10),
          ("SOIC-8_5.3", 2.16), ("LQFP", 1.60), ("TSOP-54", 1.20), ("3225", 1.00), ("3215", 0.90),
          ("L_1008", 1.20), ("TestPoint", 0.05), ("FPC-SMD", 2.00), ("TF-SMD", 1.85), ("D_SMA", 2.50),
          ("SW-SMD", 1.50), ("USB-C", 3.30), ("PinHeader_1.27", 4.00), ("PinHeader_2.54", 8.54)]
FPC_STACK = 1.37                 # folded FPC section with the touch IC (parts down): 3.0-4.37 today
TAPE_ISO = 0.15                  # polyimide tape on the panel's metal back
# Measured on the first printed case: with the panel pushed back onto the board (FPC and parts
# under it), its front sits PANEL_PLAY below the bezel seat. A --drop beyond the footprint-based
# estimate above is accepted up to PANEL_PLAY - DROP_MARGIN (margin for assembly and for the
# foam that clamps the panel forward).
PANEL_PLAY, DROP_MARGIN = 2.5, 0.5


def set_front(low=False):
    """Switch the module to a front variant (see above). Call before building any part."""
    g = globals()
    g.update(FINGER_W=7.0, FINGER_T=1.0, FINGER_T_TIP=0.8, FINGER_LIP=0.5, FINGER_FLAT=0.2,
             FINGER_ROOT_IN=PLATE - MIN_SKIN, PANEL_FINGERS=list(CLIP_FINGERS))
    if low:
        g["PANEL_BACK"] = PANEL_BACK - LOW_DROP
        g["CEIL"] = g["PANEL_BACK"] + PANEL_T
        g["TOP"] = g["CEIL"] + PLATE
        g["CAP_TOP"] = g["TOP"] + 1.2
        g["BOSS_LOW"] = BOSS_LOW - LOW_DROP
        g["HEADER_POCKET_TOP"] = g["TOP"] - MIN_SKIN
    g["FINGER_ROOT"] = g["CEIL"] + g["FINGER_ROOT_IN"]
    g["FINGER_SEAT"] = g["PANEL_BACK"] - 0.15


def panel_clearance():
    """Top-side parts under the display: (ref, footprint, height, clearance to the panel back, or
    to the folded FPC section's underside where the FPC lies), least first."""
    b = layout_board()
    x0, y0, x1, y1 = PANEL
    rows = []
    for ref, p in b.placed.items():
        if p.side != "F":
            continue
        r = p.rect()
        px0, py0, px1, py1 = r[0] - layout.OX, r[1] - layout.OY, r[2] - layout.OX, r[3] - layout.OY
        if px1 < x0 or px0 > x1 or py1 < y0 or py0 > y1:
            continue
        h = next((v for k, v in PART_H if k in p.fp.lib_id), None)
        if h is None:
            raise SystemExit(f"panel_clearance: no height for {ref} {p.fp.lib_id}")
        under_fpc = px0 < 23.2 and py1 > FPC_FOLD_Y[0] and py0 < FPC_FOLD_Y[1]
        ceil = PANEL_BACK - (FPC_STACK if under_fpc else 0.0)
        rows.append((ceil - h, ref, p.fp.lib_id.split(":")[1], h, "FPC" if under_fpc else "panel"))
    return sorted(rows)


def front_variant(low):
    """--clips / --low: the variant front, checked against both printed back shells and against the loose
    keys at the variant's height. --low also writes those keys (1.2 mm or --drop shorter) on one plate,
    keys_loose_low[D]_all_{base,legend}.stl."""
    set_front(low)
    tag = "_low" + ("" if LOW_DROP == 1.2 else f"{LOW_DROP:g}".replace(".", "p")) if low else "_clips"
    print(f"  front variant {tag[1:]}: panel back {PANEL_BACK:.2f}, ceiling {CEIL:.2f}, face {TOP:.2f}, "
          f"caps to {CAP_TOP:.2f}; case {TOP - BOT:.2f} mm thick ({CAP_TOP - BOT:.2f} over the caps)")
    print(f"  snap fingers: {len(PANEL_FINGERS)} x {FINGER_W} mm, {FINGER_T}/{FINGER_T_TIP} mm, lip {FINGER_LIP} "
          f"(engages {FINGER_LIP - FINGER_IN:.2f} past the nominal outline), strain {finger_strain(FINGER_LIP) * 100:.2f} % "
          f"(panel 0.2 mm oversize: {finger_strain(FINGER_LIP - FINGER_IN + 0.1) * 100:.2f} %)")
    if low:
        rows = panel_clearance()
        for c, ref, fp, h, where in rows[:8]:
            print(f"    {ref:5s} {fp:40s} {h:4.2f} high, {c:4.2f} mm under the {where}")
        if rows[0][0] < 0.15:
            assert LOW_DROP <= PANEL_PLAY - DROP_MARGIN, \
                f"--drop {LOW_DROP} leaves less than {DROP_MARGIN} mm of the measured {PANEL_PLAY} mm panel play"
            print(f"  (footprint estimate exceeded; accepted on the measured panel play: {PANEL_PLAY - LOW_DROP:.2f} mm "
                  f"left for foam and assembly)")
        key_stack()
    top = top_shell()
    for rev2 in (False, True):
        seam = (top & back_shell(rev2)).volume
        assert seam < 0.01, f"variant top and {'rev2' if rev2 else 'rev1'} back shells interfere ({seam:.2f} mm3)"
    print("  seam: no interference with the rev1 or the rev2 back shell")
    if low:
        loose_keys(top=top, tag=tag)
    elif check_keys(top):
        raise SystemExit("loose keys vs the variant front: 3D interference")
    path = os.path.join(OUT, f"top_shell{tag}_base.stl")
    export_stl(Pos(0, 0, TOP) * Rot(0, 180, 0) * top, path)
    export_stl(top, os.path.join(OUT, f"render_top{tag}.stl"))
    print(f"  wrote {os.path.basename(path)}")


def barclip_test_box(side="bottom"):
    """The test piece's cut: one bar clip with its posts, 3 mm of frame beyond each post, from
    just inside the display window's edge to 7 mm outside the panel, full height."""
    L = BAR_L / 2 + BAR_POST + 3.0
    win = VA[3] + 0.3 - PANEL[3] if side == "bottom" else PANEL[1] - (VA[1] - 0.3)   # window edge, n < 0
    n0, n1 = win - 1.0, 7.0
    return _bar_place(side, box(0, (n0 + n1) / 2, 2 * L, n1 - n0, -0.1, TOP + 0.1))


def barclip_dummy(side="bottom"):
    """A printed stand-in for the panel's edge (PANEL_T thick, as long as the test piece, 10 mm
    deep), in assembled position under the test piece's clip."""
    L = BAR_L / 2 + BAR_POST + 2.0
    return _bar_place(side, box(0, -5.0, 2 * L, 10.0, PANEL_BACK, CEIL))


def barclip_front():
    """--bar-clips: the default front with a bar clip on each long edge of the display instead of
    the snap fingers (BAR_CLIPS): top_shell_barclip_base.stl. Also the test piece barclip_test.stl
    (one clip and its frame, face-down) and barclip_dummy.stl (a printed panel edge). Checked
    against both back shells, the loose keys, the board parts under the clips and the swing room."""
    globals()["BAR_CLIPS"] = True
    top = top_shell()
    zs, zb = _bar_seat(), _bar_hook_bottom()
    print(f"  bar clips: {len(BAR_SIDES)} x span {BAR_L} mm between posts, {BAR_T} mm thick, "
          f"{CEIL - BAR_GAP_TOP - BAR_BOT:.2f} tall (Z {BAR_BOT}-{CEIL - BAR_GAP_TOP:.2f}); hook {BAR_HOOK_L} mm long, "
          f"grabs {BAR_GRAB} mm (worst panel {BAR_GRAB - 0.1 - BAR_CLR - 0.1:.2f}), seat {zs:.2f}, hook bottom {zb:.2f}")
    print(f"  bridge: {BAR_L} mm post to post over {BAR_POCKET + BAR_GAP_TOP:.1f} mm of air "
          f"(plate pocket {BAR_POCKET}, bar top {BAR_GAP_TOP} under the ceiling)")
    for name, d in barclip_cases().items():
        e, f = barclip_numbers(d)
        print(f"    {name:8s} hook travel {d:.2f} mm: strain {e * 100:.2f} %, {f:.1f} N on the hook")
    assert barclip_numbers(max(barclip_cases().values()))[0] < 0.02, "bar clip strain over 2 %"
    rows = barclip_clearance()
    for c, side, ref, fp, h in rows[:5]:
        print(f"    {side:6s} {ref:5s} {fp:36s} {h:4.2f} high, {c:4.2f} mm under the clip")
    assert rows[0][0] >= BAR_PART_CLR, f"bar clip over {rows[0][2]}: {rows[0][0]:.2f} mm"
    for side in BAR_SIDES:
        v = (top & bar_swing(side)).volume
        assert v < 0.01, f"{side} bar clip: no room to swing ({v:.2f} mm3 in the way)"
    # the folded FPC section (x < 23.2) only spans FPC_FOLD_Y, inside the panel's long edges; the
    # left corner rib stubs stay
    reach = BAR_CLR + BAR_T + BAR_POST_N
    assert PANEL[1] - reach > 0 and FPC_FOLD_Y[0] > PANEL[1] and FPC_FOLD_Y[1] < PANEL[3], "bar clip over the FPC"
    assert BAR_X - BAR_L / 2 - BAR_POST > PANEL[0] + 5.0, "bar clip post at the FPC wrap / corner stub"
    print("  swing room clear; posts clear of the FPC fold, the bosses and the key area")
    for rev2 in (False, True):
        seam = (top & back_shell(rev2)).volume
        assert seam < 0.01, f"bar-clip top and {'rev2' if rev2 else 'rev1'} back shells interfere ({seam:.2f} mm3)"
    print("  seam: no interference with the rev1 or the rev2 back shell")
    if check_keys(top):
        raise SystemExit("bar-clip front vs the loose keys: 3D interference")
    face_down = Pos(0, 0, TOP) * Rot(0, 180, 0)
    name = "top_shell_barclip_base.stl"
    export_stl(face_down * top, os.path.join(OUT, name))
    export_stl(top, os.path.join(OUT, "render_top_barclip.stl"))
    piece = top & barclip_test_box()
    dummy = barclip_dummy()
    assert (piece & dummy).volume < 0.01
    export_stl(face_down * piece, os.path.join(OUT, "barclip_test.stl"))
    export_stl(Pos(0, 0, -PANEL_BACK) * dummy, os.path.join(OUT, "barclip_dummy.stl"))
    print(f"  wrote {name}, barclip_test.stl ({piece.volume / 1000:.1f} cm3), barclip_dummy.stl")


def door_parts(rev2=False):
    """Only the battery door (--door: battery_door.stl and render_door.stl, for the printed back
    shell) or the rev2 back shell and door (--rev2: back_shell_rev2_{base,ears}.stl and
    battery_door_rev2.stl, after the seam check against the unchanged top shell). Both run
    check_door(); other files in OUT are left alone."""
    back, door = back_shell(rev2), battery_door(rev2)
    n = latch_numbers(rev2)
    print(f"  door latch: {n['strain'] * 100:.2f} % strain to free the hooks, {n['strain_stop'] * 100:.2f} % "
          f"at the slot stop (in the layer plane); push {n['push']:.1f} N, {n['hook_force']:.1f} N per hook")
    if check_door(back, rev2):
        raise SystemExit("battery door: fit or path check failed")
    if not rev2:
        export_stl(door, os.path.join(OUT, "battery_door.stl"))   # as assembled: plate on the bed
        export_stl(door, os.path.join(OUT, "render_door.stl"))
        return
    seam = (top_shell() & back).volume
    assert seam < 0.01, f"top and rev2 back shells interfere ({seam:.2f} mm3)"
    print(f"  rev2 seam: no interference with the top shell; tongue {WALL + RAB_IN_REV2 - (WALL - RAB_KEEP + RAB_CLR_REV2 - RAB_CLR):.2f} mm, "
          f"{RAB_CLR_REV2:.2f} mm clearance per side")
    path = os.path.join(OUT, "back_shell_rev2_base.stl")
    export_stl(back, path)
    ears = os.path.join(OUT, "back_shell_rev2_ears.stl")
    export_stl(Pos(0, 0, BOT) * mouse_ears(bed_section([path], BOT + LAYER_1 / 2)), ears)
    n = len(getattr(check_ears([path], ears, BOT), "geoms", [None]))
    print(f"  back_shell_rev2_ears.stl: clear of the {n} openings in the shell's first layer")
    export_stl(door, os.path.join(OUT, "battery_door_rev2.stl"))


def main(argv=None):
    """Build the tested set and write the print, render and STEP files to OUT (see the module
    docstring): the front, the back shell, the battery door, the loose keys and the face artwork,
    after the 3D checks (the front against both back shells, the keys against the front and the
    switches, the door's fit and path). Each option builds only its own files."""
    import argparse
    ap = argparse.ArgumentParser(
        description="Build the calculator enclosure. With no option: the tested set (front top_shell_base.stl, "
                    "back_shell_base.stl, battery_door.stl, the loose keys keys_loose_{all,test,rest,single}_"
                    "{base,legend}.stl, the face artwork overlay_{transfer,proof}.pdf, render files and "
                    "case_assembly.step) after its 3D checks. Each option builds only its own files.")
    ap.add_argument("--loose", action="store_true",
                    help="build only the loose keys (checked against the front and the switches): "
                         "keys_loose_{all,test,rest,single}_{base,legend}.stl")
    ap.add_argument("--pick", metavar="NAMES",
                    help="with --loose: only these keys (comma-separated key names) on keys_loose_pick_{base,legend}.stl")
    ap.add_argument("--loose-gaps", action="store_true",
                    help="build only the loose-key gap test: keys_loose_gaps.stl (three keys, 0.5/0.7/0.9 mm gap)")
    ap.add_argument("--word-test", action="store_true",
                    help="build only the word-legend test: keys_loose_words_{base,legend}.stl (three styles)")
    ap.add_argument("--door", action="store_true",
                    help="build and check only the battery door for the printed back shell: battery_door.stl, "
                         "render_door.stl")
    ap.add_argument("--rev2", action="store_true",
                    help="build only the rev2 back shell and door (looser seam tongue, thumb-scoop latch): "
                         "back_shell_rev2_{base,ears}.stl, battery_door_rev2.stl")
    ap.add_argument("--clips", action="store_true",
                    help="build only the front with stronger display snap fingers (V3_THIN.md): "
                         "top_shell_clips_base.stl")
    ap.add_argument("--low", action="store_true",
                    help="build only the 1.2 mm lower front (V3_THIN.md): top_shell_low_base.stl and its "
                         "shorter loose keys keys_loose_low_all_{base,legend}.stl; same back shells")
    ap.add_argument("--bar-clips", action="store_true",
                    help="build only the front with bar clips for the display instead of the snap fingers "
                         "(V3_THIN.md): top_shell_barclip_base.stl, the test piece barclip_test.stl and "
                         "barclip_dummy.stl")
    ap.add_argument("--drop", type=float, default=None,
                    help=f"with --low: how far the front drops (default {LOW_DROP}; up to PANEL_PLAY - DROP_MARGIN "
                         f"= {PANEL_PLAY - DROP_MARGIN}); writes top_shell_low<drop>_base.stl and "
                         f"keys_loose_low<drop>_all_*.stl")
    ap.add_argument("--catch-test", action="store_true",
                    help="build only the corner-catch test pieces (the top-right corner of the front and of the "
                         "back shell, checked): catch_test_top.stl, catch_test_back.stl")
    args = ap.parse_args(argv)
    os.makedirs(OUT, exist_ok=True)
    if args.drop is not None:
        globals()["LOW_DROP"] = args.drop
    if args.clips or args.low:
        return front_variant(low=args.low)
    if args.bar_clips:
        return barclip_front()
    if args.word_test:
        return word_test()
    if args.loose_gaps:
        return loose_gap_test()
    if args.loose:
        loose_keys(args.pick.split(",") if args.pick else None)
        return
    if args.catch_test:
        return catch_test()
    if args.door or args.rev2:
        return door_parts(rev2=args.rev2)
    clean_out()
    # top surface on the bed, flipped about Y so the USB end (+y) stays at the back of the bed:
    # with seam position "back" the outer-wall seam runs up the USB end, among its openings
    face_down = Pos(0, 0, TOP) * Rot(0, 180, 0)
    top, back, door = top_shell(), back_shell(), battery_door()
    back2 = back_shell(rev2=True)
    for rev2, b in ((False, back), (True, back2)):
        seam = (top & b).volume
        assert seam < 0.01, f"top and {'rev2' if rev2 else 'rev1'} back shells interfere ({seam:.2f} mm3)"
    for line in check_catch(top, [(False, back), (True, back2)]):
        print("  " + line)
    print(f"  shells: no interference (rev1 and rev2 back shells); snap fingers {finger_strain() * 100:.2f} % "
          f"strain, door latch {latch_strain() * 100:.2f} % (in the layer plane)")
    if check_door(back):
        raise SystemExit("battery door: fit or path check failed")
    # print files: load the bodies of one plate as one object (legends = colour 2)
    export_stl(face_down * top, os.path.join(OUT, "top_shell_base.stl"))     # one colour
    export_stl(back, os.path.join(OUT, "back_shell_base.stl"))       # floor on the bed
    # optional anti-warp discs at each shell's bed level, clear of the openings in its first layer
    top_files = [os.path.join(OUT, "top_shell_base.stl")]
    back_files = [os.path.join(OUT, "back_shell_base.stl")]
    export_stl(mouse_ears(bed_section(top_files, LAYER_1 / 2)), os.path.join(OUT, "top_shell_ears.stl"))
    export_stl(Pos(0, 0, BOT) * mouse_ears(bed_section(back_files, BOT + LAYER_1 / 2)),
               os.path.join(OUT, "back_shell_ears.stl"))
    for files, ears, z in ((top_files, "top_shell_ears.stl", 0.0), (back_files, "back_shell_ears.stl", BOT)):
        n = len(getattr(check_ears(files, os.path.join(OUT, ears), z), "geoms", [None]))
        print(f"  {ears}: clear of the {n} openings in the shell's first layer")
    export_stl(door, os.path.join(OUT, "battery_door.stl"))   # as assembled: plate on the bed, latch up
    # loose keys: plates legend-up, and the assembled keys by colour group for the renders
    key_stack()
    render = {}
    for name, body, legend in loose_keys(top=top):
        g = key_group(name)
        render.setdefault(g, []).append(body)
        render.setdefault(g + "_legend", []).append(legend)
    # face overlay: print-ready PDFs/SVGs, and the sheet with its labels as thin solids on the
    # face for the renders (render_top_{shift,alpha}.stl keep the names the renders load)
    import overlay
    overlay.main([])
    sheet, ov_shift, ov_alpha = overlay.render_solids()
    export_step(Compound([top, back, door] + [p for v in render.values() for p in v]),
                os.path.join(OUT, "case_assembly.step"))
    # assembled parts for renders
    export_stl(top, os.path.join(OUT, "top_shell.stl"))
    export_stl(sheet, os.path.join(OUT, "render_overlay.stl"))
    export_stl(ov_shift, os.path.join(OUT, "render_top_shift.stl"))
    export_stl(ov_alpha, os.path.join(OUT, "render_top_alpha.stl"))
    for g, parts in render.items():
        export_stl(Compound(parts), os.path.join(OUT, f"render_keys_{g}.stl"))
    export_stl(door, os.path.join(OUT, "render_door.stl"))
    for name, part in (("top", top), ("back", back), ("door", door)):
        bb = part.bounding_box()
        print(f"{name:5s} {bb.size.X:6.1f} x {bb.size.Y:6.1f} x {bb.size.Z:5.1f} mm, volume {part.volume / 1000:6.1f} cm3")


if __name__ == "__main__":
    main()
