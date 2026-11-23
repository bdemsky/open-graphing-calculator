#!/usr/bin/env python3
"""Specctra DSN export for Freerouting and SES import back into the board.

Every component gets its own image with pads already transformed into board
orientation (placed at rotation 0, front), so there is no rotation or
mirroring ambiguity between KiCad and Freerouting. Units: micrometres, Y up.

pcb_build.py calls export_dsn() on the placed board (with the pre-placed fanout
copper), runs Freerouting headless on the .dsn, and reads the resulting .ses
back with import_ses(). The same file also defines the net-class sets used by
project.py (KiCad rules), finish.py and fanout.py, and the pad geometry helpers
(pad_shape, pad_layers) every geometric stage shares, so all stages agree on
where copper is.

DSN layout produced (Specctra design language as read by Freerouting):
  structure  4 copper layers (In1.Cu as a GND power plane), board boundary,
             keepouts (power islands on In2, board slots, mounting holes),
             the single via type and the default rule
  placement  one component per reference, at its board position, front, 0 deg
  library    one image per reference (pads pre-rotated/mirrored) and the
             deduplicated padstacks
  network    every net with its pins, and five classes (clk, batt, sig, rail,
             pwr) carrying the width/clearance rules
  wiring     existing tracks/vias; the first n_protected are "protect" (fixed)
KiCad's Y axis points down and DSN's up, hence the negated Y everywhere.
"""
import math
import re

from kicad_sch import find, parse

U = 1000.0  # mm -> um

# Battery path: up to ~0.8 A from 4xAAA near end of life runs the length of the board, much of
# it possibly on 0.5 oz In2 copper; 0.8 mm keeps the whole path to roughly 0.2 R.
BATT_NETS = {"BAT_RAW", "BAT_MID", "BT1_MID", "BT2_MID", "BAT_P", "BATSW_S", "VSYS_IN"}
POWER_NETS = {"VBUS", "BUCK_SW", "LCD_A", "LCD_K", "BL_SENSE", "GND"}
RAIL_NETS = {"3V3", "3V3_BUS", "3V3_SD", "VSYS", "VDDA"}
# Clocks keep extra distance from other nets (crosstalk onto SDCLK, aggressors near the crystals).
CLOCK_NETS = {"FMC_SDCLK", "SDRAM_CLK", "QSPI_CLK", "SD_CK", "SD_CK_C", "HSE_IN", "HSE_OUT",
              "LSE_IN", "LSE_OUT"}
BATT_W, CLOCK_CLR = 800, 250  # um: battery-path width; clock clearance to other nets
PLANES = {"GND": "In1.Cu"}  # In2 is a signal layer (the 3V3 pour on it is added after routing)
SIGNAL_LAYERS = ["F.Cu", "In2.Cu", "B.Cu"]
# Clearances carry a 15 um margin over the KiCad rules (0.1 mm): Freerouting's geometry
# differs from KiCad's by up to ~0.015 mm, which otherwise shows up as DRC violations.
SIG_W, SIG_CLR, PWR_W, PWR_CLR = 100, 115, 300, 115  # um (JLC 4-layer: 0.09 mm min)
ALL_CU = ["F.Cu", "In1.Cu", "In2.Cu", "B.Cu"]
VIA = "Via[0-3]_600:300_um"   # through via, 0.6 mm pad / 0.3 mm drill, all four layers


def q(s):
    """Quote a DSN string (double quotes inside are replaced by single quotes)."""
    return '"' + s.replace('"', "'") + '"'


def short(net):
    """Net name without its sheet path: "/Power/VSYS" -> "VSYS"."""
    return net.rsplit("/", 1)[-1]


def pad_shape(pl, pad):
    """Return (kind, data) of a pad in board coords: ('poly', pts) | ('circle', (x, y, d)).

    Every non-circular shape (rect, roundrect, oval, custom) is approximated by
    its bounding rectangle, rotated by pad + footprint angle (on the bottom
    side the pad angle is mirrored); custom pads use the extent of their
    primitives computed in kicad_pcb.Footprint. pts are the 4 corners in order.
    """
    ang = pad["rot"] + pl.rot
    if pl.side == "B":
        ang = -pad["rot"] + pl.rot
    r0 = math.radians(pad["rot"])  # custom pads: copper centre is offset in the pad frame
    lx = pad["x"] + pad["ox"] * math.cos(r0) + pad["oy"] * math.sin(r0)
    ly = pad["y"] - pad["ox"] * math.sin(r0) + pad["oy"] * math.cos(r0)
    cx, cy = pl.to_board(lx, ly)
    w, h = pad["w"], pad["h"]
    shape = pad["node"][3]
    if shape == "circle":
        return "circle", (cx, cy, w)
    r = math.radians(ang)
    pts = []
    for sx, sy in ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)):
        pts.append((cx + sx * math.cos(r) + sy * math.sin(r), cy - sx * math.sin(r) + sy * math.cos(r)))
    return "poly", pts


def pad_layers(pl, pad):
    """Copper layers a placed pad occupies: all four for (NP)TH pads, otherwise
    the footprint's copper layers with F/B swapped for bottom-side parts."""
    kind = pad["kind"]
    if kind in ("thru_hole", "np_thru_hole"):
        return ALL_CU
    layers = [str(x) for x in find(pad["node"], "layers")[0][1:]]
    cu = [l for l in layers if l.endswith(".Cu")]
    if pl.side == "B":
        cu = ["B.Cu" if l == "F.Cu" else "F.Cu" if l == "B.Cu" else l for l in cu]
    return cu


def export_dsn(board, path, outline_pts, n_protected=None):
    """n_protected: how many of board.tracks / board.vias (from the start of each list) are
    fixed pre-placed copper; the rest is exported as ordinary, re-routable wiring.

    Writes the Specctra DSN file at path. outline_pts is the routing boundary
    polygon (mm, KiCad coordinates), already inset from the board edge by the
    caller to keep copper away from it."""
    nt_fixed, nv_fixed = n_protected or (len(board.tracks), len(board.vias))
    lines = ["(pcb calc.dsn",
             "  (parser (string_quote \") (space_in_quoted_tokens on) (host_cad \"calcgen\") (host_version \"1\"))",
             "  (resolution um 10)", "  (unit um)", "  (structure"]
    for l in ALL_CU:
        kind = "power" if l in PLANES.values() else "signal"
        lines.append(f"    (layer {l} (type {kind}))")
    lines.append("    (boundary (path pcb 0 " + " ".join(f"{x * U:.1f} {-y * U:.1f}" for x, y in outline_pts) + "))")
    for net, layer in PLANES.items():
        lines.append(f"    (plane {q(net)} (polygon {layer} 0 " +
                     " ".join(f"{x * U:.1f} {-y * U:.1f}" for x, y in outline_pts) + "))")
    for net, layer, (x0, y0, x1, y1) in getattr(board, "power_islands", []):   # plane islands: no routing
        lines.append(f"    (keepout \"\" (polygon {layer} 0 {x0 * U:.1f} {-y0 * U:.1f} {x1 * U:.1f} {-y0 * U:.1f} "
                     f"{x1 * U:.1f} {-y1 * U:.1f} {x0 * U:.1f} {-y1 * U:.1f} {x0 * U:.1f} {-y0 * U:.1f}))")
    for layer, (x0, y0, x1, y1) in getattr(board, "route_keepouts", []):   # e.g. under the crystals
        lines.append(f"    (keepout \"\" (polygon {layer} 0 {x0 * U:.1f} {-y0 * U:.1f} {x1 * U:.1f} {-y0 * U:.1f} "
                     f"{x1 * U:.1f} {-y1 * U:.1f} {x0 * U:.1f} {-y1 * U:.1f} {x0 * U:.1f} {-y0 * U:.1f}))")
    for x0, y0, x1, y1 in getattr(board, "slots", []):   # board cut-outs (none since the touch FPC slot went)
        x0, y0, x1, y1 = x0 - 0.3, y0 - 0.3, x1 + 0.3, y1 + 0.3
        lines.append(f"    (keepout \"\" (polygon signal 0 {x0 * U:.1f} {-y0 * U:.1f} {x1 * U:.1f} {-y0 * U:.1f} "
                     f"{x1 * U:.1f} {-y1 * U:.1f} {x0 * U:.1f} {-y1 * U:.1f} {x0 * U:.1f} {-y0 * U:.1f}))")
    for hx, hy in getattr(board, "mount_holes", []):   # 4.4 mm keepout disc around each M2 hole
        lines.append(f"    (keepout \"\" (circle signal {4.4 * U:.1f} {hx * U:.1f} {-hy * U:.1f}))")
    lines.append(f"    (via {q(VIA)})")
    lines.append(f"    (rule (width {SIG_W}) (clearance {SIG_CLR}))")
    lines.append("  )")
    lines.append("  (placement")
    for ref, pl in sorted(board.placed.items()):
        lines.append(f"    (component {q('IMG_' + ref)} (place {q(ref)} {pl.x * U:.1f} {-pl.y * U:.1f} front 0))")
    lines.append("  )")
    lines.append("  (library")
    # Pad shapes are emitted relative to the pad centre, so identical pads share a
    # padstack; pins are placed relative to the component origin. Pads with a
    # repeated number (e.g. several GND/EP pads numbered alike) get "num@k" names,
    # since DSN pin names must be unique within an image.
    padstacks = {}
    pin_names = {}
    for ref, pl in sorted(board.placed.items()):
        lines.append(f"    (image {q('IMG_' + ref)}")
        seen = {}
        for i, pad in enumerate(pl.fp.pads):
            layers = pad_layers(pl, pad)
            if not layers:
                continue
            kind, data = pad_shape(pl, pad)
            if kind == "circle":
                x, y, d = data
                shapes = tuple(f"(circle {l} {d * U:.1f})" for l in layers)
                ox, oy = x, y
            else:
                ox = sum(p[0] for p in data) / 4
                oy = sum(p[1] for p in data) / 4
                shapes = tuple("(polygon {} 0 {})".format(
                    l, " ".join(f"{(px - ox) * U:.1f} {-(py - oy) * U:.1f}" for px, py in data)) for l in layers)
            ps = padstacks.setdefault(shapes, f"PS{len(padstacks)}")
            num = pad["num"] or f"NP{i}"
            seen[num] = seen.get(num, 0) + 1
            name = num if seen[num] == 1 else f"{num}@{seen[num]}"
            pin_names[(ref, i)] = name
            if pad["kind"] == "np_thru_hole" or not pad["num"]:   # NPTH: keepout (hole + 0.3 mm), no pin
                lines.append(f"      (keepout \"\" (circle signal {pad['w'] * U + 300:.1f} "
                             f"{(ox - pl.x) * U:.1f} {-(oy - pl.y) * U:.1f}))")
                continue
            lines.append(f"      (pin {ps} {q(name)} {(ox - pl.x) * U:.1f} {-(oy - pl.y) * U:.1f})")
        lines.append("    )")
    for shapes, name in padstacks.items():
        lines.append(f"    (padstack {name} " + " ".join(f"(shape {s})" for s in shapes) + " (attach off))")
    lines.append(f"    (padstack {q(VIA)} " + " ".join(f"(shape (circle {l} 600))" for l in ALL_CU) + " (attach off))")
    lines.append("  )")
    lines.append("  (network")
    nets = {}
    for ref, pl in board.placed.items():
        for i, pad in enumerate(pl.fp.pads):
            net = pl.comp["pads"].get(pad["num"])
            if net and (ref, i) in pin_names and pad["kind"] != "np_thru_hole":
                nets.setdefault(net, []).append(f"{ref}-{pin_names[(ref, i)]}")
    for net, pins in sorted(nets.items()):
        lines.append(f"    (net {q(net)} (pins {' '.join(pins)}))")  # Freerouting wants bare pin refs
    power = [n for n in nets if short(n) in POWER_NETS]
    rails = [n for n in nets if short(n) in RAIL_NETS]
    batt = [n for n in nets if short(n) in BATT_NETS]
    clocks = [n for n in nets if short(n) in CLOCK_NETS]
    signal = [n for n in nets if n not in power + rails + batt + clocks]
    lines.append(f"    (class clk {' '.join(q(n) for n in clocks)} (circuit (use_via {q(VIA)})) "
                 f"(rule (width {SIG_W}) (clearance {CLOCK_CLR})))")
    lines.append(f"    (class batt {' '.join(q(n) for n in batt)} (circuit (use_via {q(VIA)})) "
                 f"(rule (width {BATT_W}) (clearance {PWR_CLR})))")
    lines.append(f"    (class sig {' '.join(q(n) for n in signal)} (circuit (use_via {q(VIA)})) "
                 f"(rule (width {SIG_W}) (clearance {SIG_CLR})))")
    lines.append(f"    (class rail {' '.join(q(n) for n in rails)} (circuit (use_via {q(VIA)})) "
                 f"(rule (width 200) (clearance {SIG_CLR})))")
    lines.append(f"    (class pwr {' '.join(q(n) for n in power)} (circuit (use_via {q(VIA)})) "
                 f"(rule (width {PWR_W}) (clearance {PWR_CLR})))")
    lines.append("  )")
    # pre-placed copper (ground fanout) goes in as protected wiring
    lines.append("  (wiring")
    for i, (net, layer, a, b, w) in enumerate(board.tracks):
        kind = "protect" if i < nt_fixed else "route"
        lines.append(f"    (wire (path {layer} {w * U:.1f} {a[0] * U:.1f} {-a[1] * U:.1f} {b[0] * U:.1f} {-b[1] * U:.1f})"
                     f" (net {q(net)}) (type {kind}))")
    for i, (net, at, size, drill) in enumerate(board.vias):
        kind = "protect" if i < nv_fixed else "route"
        lines.append(f"    (via {q(VIA)} {at[0] * U:.1f} {-at[1] * U:.1f} (net {q(net)}) (type {kind}))")
    lines.append("  )")
    lines.append(")")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def import_ses(board, path, min_width=0.1):
    """Add Freerouting wires and vias to the board (skipping copies of pre-placed
    items, clamping necked-down widths). Returns (n_tracks, n_vias).

    The session's protected wiring comes back too, so tracks already on the
    board (matched by endpoints rounded to 0.01 mm, either direction) and vias
    within 0.05 mm of an existing via are skipped. Coordinates are scaled by the
    session's (resolution unit n) and Y is negated back to KiCad's Y-down. Wire
    paths with several points become one segment per point pair. Imported vias
    use the default 0.6/0.3 mm size."""
    have_v = [at for _, at, _, _ in board.vias]
    have_t = {(round(a[0], 2), round(a[1], 2), round(b[0], 2), round(b[1], 2)) for _, _, a, b, _ in board.tracks}

    def dup_via(at):  # tolerance, not rounding: SES coordinates can straddle a rounding boundary
        return any(abs(at[0] - v[0]) < 0.05 and abs(at[1] - v[1]) < 0.05 for v in have_v)
    text = open(path).read()
    root = parse(text)[0]
    routes = find(root, "routes")[0]
    res = find(routes, "resolution")[0]
    scale = {"um": 1e-3, "mm": 1.0, "mil": 0.0254, "inch": 25.4}[res[1]] / float(res[2])
    nt = nv = 0
    for net in find(find(routes, "network_out")[0], "net"):
        name = str(net[1])
        for w in find(net, "wire"):
            p = find(w, "path")[0]
            layer, width = str(p[1]), float(p[2]) * scale
            co = [float(v) * scale for v in p[3:] if not isinstance(v, list)]
            pts = [(co[i], -co[i + 1]) for i in range(0, len(co) - 1, 2)]
            for a, b in zip(pts, pts[1:]):
                key = (round(a[0], 2), round(a[1], 2), round(b[0], 2), round(b[1], 2))
                if key in have_t or key[2:] + key[:2] in have_t:
                    continue
                board.track(name, layer, a, b, round(max(width, min_width), 4))
                nt += 1
        for v in find(net, "via"):
            at = (float(v[2]) * scale, -float(v[3]) * scale)
            if dup_via(at):
                continue
            board.via(name, at)
            nv += 1
    return nt, nv
