#!/usr/bin/env python3
"""Face artwork: the SHIFT and ALPHA labels, put on the white top shell as an image transfer.

The top shell is printed in one colour (white). Its labels used to be inlaid in the first two
layers of the face-down print, where 1.5-2.3 mm text comes out as dots and smears; a colour
laser print draws them crisply. They go onto the face as a gel-medium image transfer: acrylic
gel medium is brushed on the face only, the mirrored print is pressed on toner-down, and once
dry the paper is rubbed off with water, leaving the toner in a thin clear film. Areas without
toner end up clear (the white plastic shows), so the artwork is only the labels: no
background, no frames, nothing over the key holes or the display window (the paper over the
openings is not glued to anything and simply washes away). Everything comes from the same
sources as the shell: firmware/keys/layout.py (SHIFT functions, ALPHA letters, colours) and
case.py (key openings, display window, outline), so the artwork always matches both.

Labels sit on the webs above each key: the SHIFT function left-aligned with the keycap's left
edge in the SHIFT colour (blue), the ALPHA letter right-aligned with its right edge in the
ALPHA colour (black), fitted (place_labels) so every label keeps TEXT_MARGIN from every
opening's chamfer. A small name line above the display is the only other ink.

Registration, by eye: the paper is cut square along a hairline rectangle (case.PAPER_MARGIN
outside the shell all round; crop marks at its corners) and laid toner-up on a table; the front
goes on it face-down, its corners in L marks just outside them, and is slid until an outline
inside every key hole (with a centre cross) and L marks inside the display window's corners sit
evenly in their openings, seen through the holes from the back. Everything printed outside the
labels lies past the front's edge or over an opening, so none of it transfers; check_marks()
asserts that no mark touches the face.

Outputs (enclosure/out/), each a 1:1 vector PDF and SVG on US Letter (215.9 x 279.4 mm):
  overlay_transfer   MIRRORED, the print that is transferred (colour laser, plain 90-100 g/m2
                     laser paper)
  overlay_proof      not mirrored: the same labels with the shell outline and the openings as
                     hairlines, to check the printer's scale and the fit on the printed shell
Every page has two 100 mm scale bars, a "print at 100 %" note and the steps.

Usage: overlay.py [--preview DIR]   (case.py runs it too; --preview also
writes PNGs of the pages, using pdftoppm if it is installed)
"""
import math
import os
import subprocess
import sys

from shapely import affinity
from shapely.geometry import LineString, Point, box as sbox
from shapely.geometry.polygon import orient
from shapely.ops import unary_union

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import case as cs  # noqa: E402

kl = cs.keys_layout
FONT = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"        # labels: open, legible when small
FONT_REGULAR = "/System/Library/Fonts/Supplemental/Arial.ttf"      # page notes
FONT_FALLBACK = "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"   # glyphs Arial lacks (␣)

PAGE_W, PAGE_H = 215.9, 279.4      # US Letter, mm
ART_X0 = 12.0                      # left edge of the paper's cut rectangle on the page
LABEL_SIZE = 2.8                   # font size the labels get where there is room
LABEL_MIN = 1.2                    # smallest font size a label may be fitted down to
LABEL_GAP = 0.6                    # least space between a SHIFT label and its ALPHA letter
SPILL = 0.6                        # a label may reach this far past its keycap's edges
MAX_RISE = 3.0                     # a label's baseline search range above its key hole
CENTRE_CAP = 0.35                  # a label sits at most this far above the lowest spot it fits
TEXT_MARGIN = 0.2                  # labels to any opening's face edge (the outside of its chamfer)
EDGE_MARGIN = 1.5                  # labels to the artwork area's edge (case.overlay_outline)
TRACKING = 0.03                    # extra letter spacing, in font sizes
SUP_SCALE, SUP_RISE = 0.65, 0.3    # superscripts
UPPER_WORDS = True                 # SHIFT words in capitals ("SETTINGS"): no descenders, so larger
HAIR = 0.08                        # hairline width (cut rectangle, proof outlines), mm
CROP_GAP, CROP_LEN = 2.0, 5.0      # crop marks: gap from the cut rectangle's corner, length
MARK_W = 0.15                      # alignment mark line width (L marks, key-hole outlines and crosses)
WIN_INSET, WIN_LEG = 0.8, 3.0      # window L marks: inside the window aperture's edge, leg length
KEY_INSET, KEY_CROSS = 0.6, 1.0    # key-hole marks: outline this far inside each key aperture, centre cross arm
EDGE_OUT, EDGE_LEG = 0.6, 6.0      # outline marks: L marks this far outside the front's corners, leg length
MARK_CLEAR = 0.3                   # window marks keep this far inside the window aperture
OVERLAY_T = 0.03                   # transferred film thickness in the renders
PAPER_LIGHT = 0.85                 # colours lighter than this (relative luminance) get no toner
COLORS = {
    "background": kl.BODY,         # the shell's colour: unprinted (the plastic shows)
    "shift": kl.SHIFT_INK,         # = the SHIFT key's colour
    "alpha": kl.ALPHA_INK,         # black, as the legends on the light caps
    "minor": "#80868e",            # the name line above the display
    "cut": "#9aa0a8",              # paper cut rectangle and crop marks (never on the face)
    "mark": "#000000",             # alignment marks (never on the face)
    "guide": "#9aa0a8",            # proof page: shell outline and openings
}


def ink(color):
    """Printed colour: `color`, or white (no toner) if it is lighter than PAPER_LIGHT."""
    r, g, b = (int(color[i:i + 2], 16) / 255 for i in (1, 3, 5))
    return "#ffffff" if 0.2126 * r + 0.7152 * g + 0.0722 * b > PAPER_LIGHT else color


# ------------------------------------------------------------------ text ---
def _has(path, ch):
    return ord(ch) in cs._font(path)[2]


def text_geom(markup, size):
    """Shapely outline of CAD markup `markup` (case.cad_text: '^' starts a superscript) at font
    size `size` (mm) in FONT, the pen starting at (0, 0) on the baseline. Plain outlines: a
    laser prints any stroke, so nothing is grown."""
    parts, x = [], 0.0
    for run, sup in cs._runs(markup):
        sz, dy = (size * SUP_SCALE, size * SUP_RISE) if sup else (size, 0.0)
        for ch in run:
            path = FONT if _has(FONT, ch) or ch == " " else FONT_FALLBACK
            upm, hmtx, cmap = cs._font(path)
            adv = hmtx[cmap[ord(ch)]][0] / upm * sz if ord(ch) in cmap else 0.5 * sz
            if ch != " ":
                g = affinity.scale(cs._glyph(ch, path), sz, sz, origin=(0, 0))
                parts.append(affinity.translate(g, x, dy))
            x += adv + TRACKING * size
    return unary_union(parts).simplify(0.004) if parts else Point(0, 0).buffer(0)


def shift_markup(legend):
    """CAD markup of a SHIFT legend as printed on the overlay (words in capitals)."""
    if UPPER_WORDS and len(legend) >= 3 and all(c.isascii() and (c.isalpha() or c in " -") for c in legend):
        legend = legend.upper()
    return cs.cad_text(legend)


# -------------------------------------------------------------- geometry ---
def rrect(cx, cy, w, h, r):
    """Shapely rounded rectangle w x h centred at (cx, cy)."""
    return sbox(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2).buffer(-r).buffer(r, quad_segs=8)


def _hole_rect(name, x, y, w, h):
    """Face edge of key `name`'s opening (CAD box x0, y0, x1, y1): the outside of its chamfer."""
    cx, cy = cs.cad(x, y)
    m = cs.KEY_CLR + cs.KEY_CH
    return cx - w / 2 - m, cy - h / 2 - m, cx + w / 2 + m, cy + h / 2 + m


def geometry():
    """The face in CAD coordinates (mm, origin at the case centre, y up):
    {"outline": the artwork area (case.overlay_outline), "shell": the shell's outer outline,
    "holes": [polygon] (the keys' openings at the face, then the window, all with their
    chamfers), "obstacles": [(x0, y0, x1, y1)] the holes as boxes, "window": the window's box}."""
    ow, oh, orr = cs.overlay_outline()
    outline = rrect(0, 0, ow, oh, orr)
    shell = rrect(0, 0, cs.OUT_W, cs.OUT_H, cs.OUT_R)
    holes, obstacles = [], []
    for name, x, y, w, h in cs.keys():
        r = _hole_rect(name, x, y, w, h)
        rad = cs.CAP_R + cs.KEY_CLR + cs.KEY_CH
        holes.append(rrect((r[0] + r[2]) / 2, (r[1] + r[3]) / 2, r[2] - r[0], r[3] - r[1], rad))
        obstacles.append(r)
    # display window with its bevel (VA + 0.3 + WINDOW_CH per side)
    m = 0.3 + cs.WINDOW_CH
    wx, wy, ww, wh = cs.board_rect((cs.VA[0] - m, cs.VA[1] - m, cs.VA[2] + m, cs.VA[3] + m))
    holes.append(rrect(wx, wy, ww, wh, cs.WINDOW_R + cs.WINDOW_CH))
    window = (wx - ww / 2, wy - wh / 2, wx + ww / 2, wy + wh / 2)
    obstacles.append(window)
    return {"outline": outline, "shell": shell, "holes": holes, "obstacles": obstacles, "window": window}


def _fit_y(x0, x1, yb0, yb1, y_lo, obstacles, inner):
    """Baseline y for a label whose box is [x0, x1] x [y + yb0, y + yb1], at or above y_lo:
    the lowest gap (up to MAX_RISE) that keeps TEXT_MARGIN from every obstacle box, and
    `inner` (the outline less EDGE_MARGIN); the label sits at its middle but at most
    CENTRE_CAP above its bottom. None if there is no room."""
    if x0 < inner[0] or x1 > inner[2]:
        return None
    m = TEXT_MARGIN
    bad = [(oy0 - m - yb1, oy1 + m - yb0) for ox0, oy0, ox1, oy1 in obstacles
           if ox0 - m < x1 and ox1 + m > x0]
    bad += [(inner[3] - yb1, math.inf), (-math.inf, inner[1] - yb0)]
    y, top = y_lo, y_lo + MAX_RISE
    for b0, b1 in sorted(bad):
        if b1 <= y:
            continue
        if b0 >= y:
            return y + min((b0 - y) / 2, CENTRE_CAP) if y <= top else None
        y = b1
    return None


def _place(name, x, y, w, h, size, obstacles, inner):
    """(SHIFT pen x, ALPHA pen x, baseline y) of key `name`'s labels at font `size`, or None
    if they do not fit (layouts in order of preference, see place_labels)."""
    s_txt = shift_markup(cs.SHIFTED[name]) if name in cs.SHIFTED else None
    a_txt = cs.ALPHA_L.get(name)
    _, _, _, hy1 = _hole_rect(name, x, y, w, h)
    cx, _ = cs.cad(x, y)
    left, right = cx - w / 2, cx + w / 2
    sb = text_geom(s_txt, size).bounds if s_txt else None
    ab = text_geom(a_txt, size).bounds if a_txt else None
    sw = sb[2] - sb[0] if sb else 0.0             # text is placed by its ink box
    aw = ab[2] - ab[0] if ab else 0.0
    yb0 = min(b[1] for b in (sb, ab) if b)
    yb1 = max(b[3] for b in (sb, ab) if b)
    both = sw + aw + (LABEL_GAP if sb and ab else 0.0)
    if sb and ab:
        layouts = [(left, right - aw)] if both <= right - left else []
        layouts += [(left, left + sw + LABEL_GAP), (right - both, right - aw),
                    (cx - both / 2, cx - both / 2 + sw + LABEL_GAP)]
    else:
        layouts = [(cx - both / 2, cx - both / 2), (left, left), (right - both, right - both)]
    for sx, ax in layouts:
        x0, x1 = (sx if sb else ax), (ax + aw if ab else sx + sw)
        if x0 < left - SPILL or x1 > right + SPILL:
            continue
        yy = _fit_y(x0, x1, yb0, yb1, hy1 - yb0, obstacles, inner)
        if yy is not None:
            return (sx - sb[0] if sb else None, ax - ab[0] if ab else None, yy)
    return None


_LABELS = []


def place_labels():
    """[(key, role, markup, size, x, y)] for every SHIFT label (role "shift") and ALPHA
    letter (role "alpha"); (x, y) is the pen start on the baseline (CAD).

    The labels sit above their key hole, in one of these layouts (the first that fits): SHIFT
    left-aligned with the keycap's left edge and ALPHA right-aligned with its right edge; both
    together on the left; both on the right; both centred (a key without a letter: its SHIFT
    label centred, left, right); never more than SPILL past the keycap's edges, so neighbours
    keep apart. Each key gets the largest size <= LABEL_SIZE (0.05 mm steps, down to
    LABEL_MIN) that fits, and no more than the median of its row, so a row reads evenly
    (only a label that cannot fit that size, e.g. one with brackets or descenders, is smaller)."""
    if _LABELS:
        return _LABELS[0]
    LS, LM = LABEL_SIZE, LABEL_MIN
    geo = geometry()
    obstacles = geo["obstacles"]
    inner = [v + d for v, d in zip(geo["outline"].bounds, (EDGE_MARGIN, EDGE_MARGIN, -EDGE_MARGIN, -EDGE_MARGIN))]
    best = {}
    ks = [k for k in cs.keys() if k[0] in cs.SHIFTED or k[0] in cs.ALPHA_L]
    for name, x, y, w, h in ks:
        size = LS
        while _place(name, x, y, w, h, size, obstacles, inner) is None:
            size = round(size - 0.05, 2)
            if size < LM - 1e-9:
                raise SystemExit(f"overlay: no room for the labels of {name}")
        best[name] = size
    rows = {}
    for name, x, y, w, h in ks:
        rows.setdefault(round(y), []).append(best[name])
    row = {r: sorted(v)[(len(v) - 1) // 2] for r, v in rows.items()}     # the (lower) median
    out = []
    for name, x, y, w, h in ks:
        size = min(best[name], row[round(y)])
        sx, ax, yy = _place(name, x, y, w, h, size, obstacles, inner)
        if sx is not None:
            out.append((name, "shift", shift_markup(cs.SHIFTED[name]), size, sx, yy))
        if ax is not None:
            out.append((name, "alpha", cs.ALPHA_L[name], size, ax, yy))
    _LABELS.append(out)
    return out


def label_geoms(labels=None):
    """{"shift": geometry, "alpha": geometry} of the placed labels (CAD)."""
    labels = labels if labels is not None else place_labels()
    out = {"shift": [], "alpha": []}
    for name, role, txt, size, x, y in labels:
        out[role].append(affinity.translate(text_geom(txt, size), x, y))
    return {k: unary_union(v) for k, v in out.items()}


def decorations(geo):
    """[(geometry, colour role)]: a small name line above the display."""
    out = []
    t = text_geom("GRAPHING  CALCULATOR", 2.0)
    x0, y0, x1, y1 = t.bounds
    win_top, ot = geo["window"][3], geo["outline"].bounds[3]
    cx = cs.cad((cs.VA[0] + cs.VA[2]) / 2, 0)[0]
    out.append((affinity.translate(t, cx - (x0 + x1) / 2, (win_top + ot) / 2 - (y0 + y1) / 2 + 1.0), "minor"))
    return out


def inked(geo=None):
    """[(geometry, colour)] of everything that gets toner on the face (CAD, not mirrored)."""
    geo = geo or geometry()
    lg = label_geoms()
    items = [(g, ink(COLORS[role])) for g, role in decorations(geo)]
    items += [(lg["shift"], ink(COLORS["shift"])), (lg["alpha"], ink(COLORS["alpha"]))]
    return [(g, c) for g, c in items if c != "#ffffff"]


def check_clear(geo=None):
    """Assert that no toner lands on an opening (with TEXT_MARGIN) or outside the artwork area:
    the paper over the holes and the window must carry nothing. Returns the inked area (mm2)."""
    geo = geo or geometry()
    toner = unary_union([g for g, _ in inked(geo)])
    holes = unary_union(geo["holes"]).buffer(TEXT_MARGIN - 0.01)
    over = toner.intersection(holes).area
    assert over < 1e-6, f"toner over an opening ({over:.4f} mm2)"
    out = toner.difference(geo["outline"]).area
    assert out < 1e-6, f"toner outside the artwork area ({out:.4f} mm2)"
    return toner.area


# ------------------------------------------------------------------ PDF ---
MM = 72 / 25.4      # points per mm


def _patch(geom, **kw):
    """matplotlib PathPatch of a shapely (Multi)Polygon (outer rings counter-clockwise, holes
    clockwise, so the nonzero fill rule leaves the holes empty)."""
    from matplotlib.patches import PathPatch
    from matplotlib.path import Path
    verts, codes = [], []
    for p in getattr(geom, "geoms", [geom]):
        if p.is_empty:
            continue
        p = orient(p, 1.0)
        for ring in [p.exterior] + list(p.interiors):
            c = list(ring.coords)
            verts += c
            codes += [Path.MOVETO] + [Path.LINETO] * (len(c) - 2) + [Path.CLOSEPOLY]
    return PathPatch(Path(verts, codes), **kw)


def _lines(geom, color, width, ax):
    """Stroke `geom` (polygons: their rings; lines: as they are), mm line width."""
    from matplotlib.patches import PathPatch
    from matplotlib.path import Path
    for p in getattr(geom, "geoms", [geom]):
        rings = [p.exterior] + list(p.interiors) if p.geom_type == "Polygon" else [p]
        for ring in rings:
            c = list(ring.coords)
            closed = ring.geom_type == "LinearRing"
            codes = [Path.MOVETO] + [Path.LINETO] * (len(c) - 1)
            if closed:
                codes[-1] = Path.CLOSEPOLY
            ax.add_patch(PathPatch(Path(c, codes), facecolor="none", edgecolor=color, linewidth=width * MM,
                                   joinstyle="miter", capstyle="butt"))


def placement():
    """CAD -> page offset: the paper rectangle's centre on the page."""
    pw, _ = cs.paper_rect()
    return ART_X0 + pw / 2, PAGE_H / 2


def window_aperture():
    """The display window's narrowest opening through the bare shell (VA + 0.3 a side, below
    its bevel), CAD polygon: open to the table, so it is lit from below."""
    m = 0.3
    wx, wy, ww, wh = cs.board_rect((cs.VA[0] - m, cs.VA[1] - m, cs.VA[2] + m, cs.VA[3] + m))
    return rrect(wx, wy, ww, wh, cs.WINDOW_R)


def _marks(geo):
    """{"cut": cut rectangle, "crops": crop marks, "window": L marks inside the window aperture's
    corners, "keys": an outline inside every key aperture and a centre cross, "edge": L marks just
    outside the front's corners}, CAD, as lines. No mark is on the face (check_marks)."""
    pw, ph = cs.paper_rect()
    cut = sbox(-pw / 2, -ph / 2, pw / 2, ph / 2)
    crops = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            x, y = sx * pw / 2, sy * ph / 2
            crops.append(LineString([(x + sx * CROP_GAP, y), (x + sx * (CROP_GAP + CROP_LEN), y)]))
            crops.append(LineString([(x, y + sy * CROP_GAP), (x, y + sy * (CROP_GAP + CROP_LEN))]))
    x0, y0, x1, y1 = window_aperture().bounds
    win = []
    for sx, xe in ((1, x0), (-1, x1)):
        for sy, ye in ((1, y0), (-1, y1)):
            cx, cy = xe + sx * WIN_INSET, ye + sy * WIN_INSET
            win.append(LineString([(cx + sx * WIN_LEG, cy), (cx, cy), (cx, cy + sy * WIN_LEG)]))
    # the front laid face-down on the paper: an outline inside every key hole and a centre cross, seen through
    # the holes from the back, and L marks round the front's corners
    keys = []
    for name, x, y, w, h in cs.keys():
        cx, cy = cs.cad(x, y)
        a = key_aperture(name, x, y, w, h)
        keys.append(LineString(a.buffer(-KEY_INSET).exterior.coords))
        keys += [LineString([(cx - KEY_CROSS, cy), (cx + KEY_CROSS, cy)]),
                 LineString([(cx, cy - KEY_CROSS), (cx, cy + KEY_CROSS)])]
    edge = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            x, y = sx * (cs.OUT_W / 2 + EDGE_OUT), sy * (cs.OUT_H / 2 + EDGE_OUT)
            edge.append(LineString([(x - sx * EDGE_LEG, y), (x, y), (x, y - sy * EDGE_LEG)]))
    return {"cut": cut, "crops": unary_union(crops), "window": win, "keys": keys, "edge": edge}


def key_aperture(name, x, y, w, h):
    """Key `name`'s narrowest opening through the bare shell (under its chamfer), CAD polygon."""
    cx, cy = cs.cad(x, y)
    return rrect(cx, cy, w + 2 * cs.KEY_CLR, h + 2 * cs.KEY_CLR, cs.CAP_R + cs.KEY_CLR)


def _stroke(lines, w):
    """The area that `lines` (LineStrings) cover when drawn w wide."""
    return unary_union([g.buffer(w / 2, cap_style="flat", join_style="mitre") for g in lines])


def check_marks(geo=None):
    """Assert that no alignment mark lands on the face: the window L marks and the key-hole marks
    lie MARK_CLEAR inside their apertures, over the openings; the corner L marks lie outside the
    front, on the paper. Returns the least distance (mm) from any mark to the face."""
    geo = geo or geometry()
    mk = _marks(geo)
    win = _stroke(mk["window"], MARK_W)
    ap = window_aperture()
    assert ap.buffer(-MARK_CLEAR).contains(win), "a window mark is not inside the window aperture"
    keys = _stroke(mk["keys"], MARK_W)
    aps = unary_union([key_aperture(*k) for k in cs.keys()])
    assert aps.buffer(-MARK_CLEAR).contains(keys), "a key-hole mark is not inside its key aperture"
    edge = _stroke(mk["edge"], MARK_W)
    assert edge.distance(geo["shell"]) >= EDGE_OUT - MARK_W, "an outline mark is on the front"
    assert mk["cut"].contains(edge), "an alignment mark is off the paper"
    face = geo["shell"].difference(ap).difference(aps)
    marks = win.union(keys).union(edge)
    assert marks.intersection(face).area < 1e-9, "an alignment mark lands on the face"
    return marks.distance(face)


def art(variant):
    """[(geometry, fill colour or None, edge colour or None, edge width)] of a variant on the
    page (mm, origin bottom-left), mirrored for "transfer"."""
    geo = geometry()
    items = [(g, c, None, 0) for g, c in inked(geo)]
    mk = _marks(geo)
    items += [(mk["cut"], None, COLORS["cut"], HAIR), (mk["crops"], None, "#000000", 0.15)]
    items += [(unary_union(mk[k]), None, COLORS["mark"], MARK_W)
              for k in ("window", "keys", "edge")]
    if variant == "proof":
        items.append((geo["shell"], None, COLORS["guide"], HAIR))
        for g in geo["holes"]:
            items.append((g, None, COLORS["guide"], HAIR))
    ox, oy = placement()
    out = []
    for g, fc, ec, w in items:
        if variant == "transfer":
            g = affinity.scale(g, -1, 1, origin=(0, 0))
        out.append((affinity.translate(g, ox, oy), fc, ec, w))
    return out


NOTES = {
    "transfer": ("Face artwork – image transfer (MIRRORED)", [
        "Colour laser printer, plain 90-100 g/m2 laser paper, best quality.",
        "Toner goes face-down onto the gel medium: the print reads backwards.",
        "Cut along the grey rectangle. Only the labels transfer: the marks",
        "lie over the key holes, the window or past the front's edge.",
        "Align by eye: the L marks round the front's corners, and through",
        "the holes from the back an outline evenly inside every key hole",
        "and the L marks inside the window.",
    ]),
    "proof": ("Face artwork – proof (not mirrored)", [
        "Plain paper. Check the printer's scale and the fit before the",
        "transfer print: measure both scale bars, cut along the grey",
        "rectangle and lay it face-up under the front, face down on it.",
        "Look through the holes from the back: an outline evenly inside",
        "every key hole, the L marks evenly inside the window, the corner",
        "L marks just outside the front, every label above its key.",
    ]),
}
STEPS = [
    "1. Sand the face with 400-600 grit (wet), wash, dry, wipe with IPA.",
    "2. Brush a thin, even coat of gel medium on the face only, brush",
    "   strokes one way.",
    "3. Lay the print toner-up on a flat table. Lower the front onto it",
    "   face-down, its corners in the corner L marks. Look through the",
    "   holes from the back and slide it gently until every key outline",
    "   sits evenly in its hole. Press down; holding paper and front",
    "   together, turn them over; smooth from the middle out with a",
    "   brayer or fingers, face only; push out every bubble.",
    "4. Dry 24 h, paper on. Then wet the paper, wait 1-2 min, rub it off",
    "   with a fingertip in light circles; dry, repeat until no fibres",
    "   are left (2-4 passes). The paper over the openings washes away.",
    "5. Seal with 2-3 thin coats of water-based polyurethane (satin).",
]


def _scale_bars(ax, color="#000000"):
    """Two 100 mm scale bars (horizontal at the bottom, vertical at the right) with 10 mm ticks."""
    from matplotlib.font_manager import FontProperties
    fp = FontProperties(fname=FONT_REGULAR, size=7)
    lw = 0.2 * MM
    x0, y0 = 15.0, 10.0
    ax.plot([x0, x0 + 100], [y0, y0], color=color, lw=lw, solid_capstyle="butt")
    for i in range(11):
        t = 2.5 if i % 5 == 0 else 1.5
        ax.plot([x0 + 10 * i, x0 + 10 * i], [y0, y0 + t], color=color, lw=lw)
    ax.text(x0 + 102, y0, "100 mm", fontproperties=fp, va="center", color=color)
    vx, vy = 205.0, 30.0
    ax.plot([vx, vx], [vy, vy + 100], color=color, lw=lw, solid_capstyle="butt")
    for i in range(11):
        t = 2.5 if i % 5 == 0 else 1.5
        ax.plot([vx, vx - t], [vy + 10 * i, vy + 10 * i], color=color, lw=lw)
    ax.text(vx + 1.5, vy + 50, "100 mm", fontproperties=fp, va="center", ha="left", rotation=90, color=color)


def figure(variant):
    """matplotlib figure of one page: US Letter, axes in mm (1 unit = 1 mm)."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.font_manager import FontProperties
    fig = plt.figure(figsize=(PAGE_W / 25.4, PAGE_H / 25.4))
    ax = fig.add_axes((0, 0, 1, 1))
    ax.set_xlim(0, PAGE_W)
    ax.set_ylim(0, PAGE_H)
    ax.axis("off")
    for g, fc, ec, w in art(variant):
        if fc:
            ax.add_patch(_patch(g, facecolor=fc, edgecolor="none", linewidth=0))
        if ec:
            _lines(g, ec, w, ax)
    # notes column, never mirrored
    title, lines = NOTES[variant]
    fb, fr = FontProperties(fname=FONT, size=9), FontProperties(fname=FONT_REGULAR, size=6.5)
    fh = FontProperties(fname=FONT, size=7)
    pw, ph = cs.paper_rect()
    x, y = ART_X0 + pw + 12.0, PAGE_H - 22.0
    ax.text(x, y, title, fontproperties=fb, va="top")
    y -= 7
    ax.text(x, y, "PRINT AT 100 % / ACTUAL SIZE", fontproperties=fh, va="top", color="#c00000")
    y -= 3.6
    ax.text(x, y, "turn off “fit to page”; both scale bars = 100 mm", fontproperties=fr, va="top",
            color="#c00000")
    y -= 6
    for line in lines:
        ax.text(x, y, line, fontproperties=fr, va="top")
        y -= 3.4
    if variant == "transfer":
        y -= 3
        ax.text(x, y, "Transfer", fontproperties=fh, va="top")
        y -= 4.5
        for line in STEPS:
            ax.text(x, y, line, fontproperties=fr, va="top")
            y -= 3.4
    y -= 3
    for line in ("Colours: SHIFT functions blue (the SHIFT key),", "ALPHA letters black; no toner elsewhere.",
                 "Source: firmware/keys/layout.py."):
        ax.text(x, y, line, fontproperties=fr, va="top")
        y -= 3.4
    ax.text(x, 44.0, f"Paper {pw:.1f} x {ph:.1f} mm (shell + {cs.PAPER_MARGIN:.0f} mm all round).",
            fontproperties=fr, va="top")
    ax.text(x, 40.6, "Generated by enclosure/overlay.py.", fontproperties=fr, va="top")
    _scale_bars(ax)
    return fig


VARIANTS = ("transfer", "proof")     # the page variants


def write(out_dir=cs.OUT):
    """Write overlay_<variant>.pdf and .svg for every variant; returns the PDF paths."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    matplotlib.rcParams["pdf.fonttype"] = 42      # embed TrueType (notes stay text)
    matplotlib.rcParams["svg.fonttype"] = "path"
    os.makedirs(out_dir, exist_ok=True)
    paths = []
    for v in VARIANTS:
        fig = figure(v)
        for ext in ("pdf", "svg"):
            p = os.path.join(out_dir, f"overlay_{v}.{ext}")
            fig.savefig(p, dpi=600)
            if ext == "pdf":
                paths.append(p)
        plt.close(fig)
    return paths


def check_pdf(path):
    """Read the PDF back: page size must be US Letter and the cut rectangle must measure the
    paper's size (case.paper_rect). Returns (page w, page h, rect w, rect h) in mm."""
    import pypdf
    box = pypdf.PdfReader(path).pages[0].mediabox
    pw, ph = float(box.width) / MM, float(box.height) / MM
    assert abs(pw - PAGE_W) < 0.05 and abs(ph - PAGE_H) < 0.05, (path, pw, ph)
    rw, rh = cs.paper_rect()
    try:
        import pymupdf
    except ImportError:
        return pw, ph, None, None
    doc = pymupdf.open(path)
    rects = [d["rect"] for d in doc[0].get_drawings() if d.get("color") and d.get("fill") is None]
    big = max(rects, key=lambda r: r.width * r.height)
    mw, mh = big.width / MM, big.height / MM
    # the stroke is HAIR wide: its bounding box is up to one width larger
    assert abs(mw - rw) < 0.2 and abs(mh - rh) < 0.2, (path, mw, mh, rw, rh)
    assert rh + 2 * (CROP_GAP + CROP_LEN) < PAGE_H - 10 and ART_X0 - CROP_GAP - CROP_LEN >= 5 \
        and ART_X0 + rw < PAGE_W - 85, "paper (with its crop marks) does not fit the page"
    return pw, ph, mw, mh


def render_solids():
    """(film, SHIFT labels, ALPHA letters) as build123d solids in assembled position: the clear
    film OVERLAY_T thick on the face's artwork area less the openings, the labels just on top
    of it (renders only)."""
    from build123d import Pos, extrude
    geo = geometry()
    sheet = geo["outline"].difference(unary_union(geo["holes"]))
    lg = label_geoms()
    out = [Pos(0, 0, cs.TOP) * extrude(cs._to_sketch(sheet.simplify(0.01)), OVERLAY_T)]
    for k in ("shift", "alpha"):
        out.append(Pos(0, 0, cs.TOP + OVERLAY_T) * extrude(cs._to_sketch(lg[k].simplify(0.005)), 0.03))
    return tuple(out)


def preview(png, px_per_mm=12):
    """PNG of the artwork on the shell with the keycaps (renders/; no marks or notes)."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    geo = geometry()
    pad = 3.0
    w, h = cs.OUT_W + 2 * pad, cs.OUT_H + 2 * pad
    fig = plt.figure(figsize=(w / 25.4, h / 25.4))
    ax = fig.add_axes((0, 0, 1, 1))
    ax.set_xlim(-w / 2, w / 2)
    ax.set_ylim(-h / 2, h / 2)
    ax.axis("off")
    fig.patch.set_facecolor("#d9dde2")
    ax.add_patch(_patch(geo["shell"], facecolor=kl.BODY, edgecolor="#b5bac1", linewidth=0.6))
    gaps = unary_union(geo["holes"][:-1])        # key openings with their chamfers
    ax.add_patch(_patch(gaps, facecolor="#9aa0a8", edgecolor="none"))
    holes = unary_union([rrect((r[0] + r[2]) / 2, (r[1] + r[3]) / 2, r[2] - r[0] - 2 * cs.KEY_CH,
                               r[3] - r[1] - 2 * cs.KEY_CH, cs.CAP_R + cs.KEY_CLR) for r in geo["obstacles"][:-1]])
    ax.add_patch(_patch(holes, facecolor="#2a2d31", edgecolor="none"))      # gaps around the caps
    sizes = cs.cap_legend_sizes()
    for name, x, y, kw, kh in cs.keys():          # keycaps and their legends in their colours
        cx, cy = cs.cad(x, y)
        pal = kl.PALETTE[cs.key_group(name)]
        ax.add_patch(_patch(rrect(cx, cy, kw, kh, cs.CAP_R), facecolor=pal["cap"], edgecolor="#8d939b",
                            linewidth=0.3))
        t, sz, va = sizes[name]
        ax.add_patch(_patch(affinity.translate(cs.text_geom(t, sz, valign=va), cx, cy),
                            facecolor=pal["legend"], edgecolor="none"))
    ax.add_patch(_patch(geo["holes"][-1], facecolor="#9aa0a8", edgecolor="none"))   # window bevel
    m = 0.3
    wx, wy, ww, wh = cs.board_rect((cs.VA[0] - m, cs.VA[1] - m, cs.VA[2] + m, cs.VA[3] + m))
    ax.add_patch(_patch(rrect(wx, wy, ww, wh, cs.WINDOW_R), facecolor="#0b1a2a", edgecolor="none"))
    for g, c in inked(geo):
        ax.add_patch(_patch(g, facecolor=c, edgecolor="none"))
    fig.savefig(png, dpi=px_per_mm * 25.4, facecolor=fig.get_facecolor())
    plt.close(fig)


def report(labels=None):
    """Print every label with its size and the room it got."""
    labels = labels if labels is not None else place_labels()
    sizes = sorted({lab[3] for lab in labels})
    print(f"overlay: {len(labels)} labels, font sizes {sizes[0]:.2f}-{sizes[-1]:.2f} mm (Arial Bold; "
          f"cap height {0.716 * sizes[0]:.2f}-{0.716 * sizes[-1]:.2f} mm)")
    rows = {}
    for n, r, t, s, *_ in labels:
        rows.setdefault(s, []).append(t)
    for s in sorted(rows, reverse=True):
        print(f"  {s:.2f} mm: " + " ".join(rows[s]))



def main(argv):
    labels = place_labels()
    report(labels)
    area = check_clear()
    print(f"  toner {area:.0f} mm2 in all, none over an opening or outside the artwork area")
    d = check_marks()
    print(f"  alignment marks: 4 corner L marks, {len(cs.keys())} key-hole outlines, 4 window L marks; "
          f"none on the face ({d:.2f} mm clear)")
    for p in write():
        pw, ph, mw, mh = check_pdf(p)
        meas = f", cut rectangle {mw:.2f} x {mh:.2f} mm" if mw else ""
        print(f"  wrote {os.path.relpath(p, HERE)} ({pw:.1f} x {ph:.1f} mm{meas}) + .svg")
    if "--preview" in argv:
        d = argv[argv.index("--preview") + 1]
        os.makedirs(d, exist_ok=True)
        for v in VARIANTS:
            src = os.path.join(cs.OUT, f"overlay_{v}.pdf")
            try:
                subprocess.run(["pdftoppm", "-r", "100", "-png", "-singlefile", src, os.path.join(d, f"overlay_{v}")],
                               check=True)
            except (OSError, subprocess.CalledProcessError) as e:
                print("  pdftoppm failed:", e)
        preview(os.path.join(d, "overlay_on_shell.png"))
        print("  previews in", d)


if __name__ == "__main__":
    main(sys.argv[1:])
