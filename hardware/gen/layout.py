#!/usr/bin/env python3
"""Board placement. Board coordinates below are relative to the board's top-left
corner (OX, OY are added when writing). See ARCHITECTURE.md §9 for the floor plan:

  y   0-12   top strip: USB-C (top edge), microSD (top-right, card from the top),
             debug headers, RESET/BOOT
  y  13-63   display panel (landscape, raised on a printed frame). Underneath:
             MCU, SDRAM, flash, power, backlight driver. The panel FPC exits the
             left edge and folds under into J3; its path is kept clear.
  y  66-186  keypad. Battery holders on the bottom side.

Input: the KiCad netlist exported by build.sh (build/calc.net) and the
physical key layout (design.LAYOUT, by key name; each key switch carries its
key name as value). Output: a kicad_pcb.Board with every
footprint placed, the outline, mounting holes and the extra attributes the
routing stages use (mount_holes, power_islands, gnd_patches, route_keepouts,
decoupling_b, unplaced). pcb_build.py calls build(); run directly, this module
writes a placement-only kicad/calc.kicad_pcb.

Placement strategy, in order:
  1. Fixed positions for everything whose position is a design decision:
     connectors, MCU, SDRAM, flash, crystals, the hand-tuned power path
     (following the TPS62162 datasheet layout guidance), every key switch, batteries.
  2. MCU/SDRAM decoupling on the bottom side directly under each supply pin
     (under_pad), so the top-side perimeter stays free for escape routing and
     each cap is a via pair from its pin through the In2 power island.
  3. Everything else by group(): each part goes to the free spot closest to
     the group's target point inside the group's region (near() searches
     outward in rings, 0.5 mm steps by default, on a 0.05 mm grid).
Parts are kept 0.2-0.25 mm apart (courtyard-ish bounding boxes) and 0.6 mm
inside the board edge. Units are mm; Y grows downward (KiCad).

Several parts are found by their netlist reference (e.g. "R16", "D2"), which
is stable because design.py's references are pinned in refs.json.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from kicad_pcb import Board  # noqa: E402

OX, OY = 100.0, 50.0                      # board origin on the KiCad sheet, mm
W, H = 82.0, 188.8                        # board outline, mm (corner radius 4 mm)
# EastRising ER-TFT032IPS-3.2 + CTP, landscape: 77.7 x 55.04 mm (CTP outline), centred.
PANEL = (2.15, 13.0, 79.85, 68.04)        # panel outline, landscape
PANEL_LCD_X0 = 2.25                       # LCD glass left edge (77.5 mm LCD outline), FPC exit
# The FPC folds flat behind the panel; its tail dips into J3 just west of J3's opening, so no
# tall parts there (the folded 43 mm section itself lies against the panel back, ~3 mm up).
FPC_KEEPOUT = (31.0, 29.5, 36.2, 51.6)
KEYS_TOP = 70.0
# M2 mounting holes (the enclosure uses the same list). The panel now spans the full width
# from y 13 to 68, so the middle pair sits just below it; there is no room for a top-right hole
# (microSD + panel).
HOLES = [(3.5, 3.5), (3.5, H - 3.5), (W - 3.5, H - 3.5), (3.5, 72.5), (W - 3.5, 72.5)]

# ---------------------------------------------------------------- keypad ---
COLS = [W / 2 + (i - 2) * 15.0 for i in range(5)]            # 5-column grid at 15 mm
# The keypad starts 7 mm below the panel: the strip in between holds power/backlight parts.
# 3.2" panel: everything moved down 5.04 mm, main rows tightened 11.5 -> 11.15 mm.
ROW_SOFT, ROW_A, ROW_B = 82.04, 92.04, 102.04                 # soft keys, row A, row B (left of the D-pad)
ROWS_MAIN = [112.84 + 11.15 * i for i in range(7)]            # CAS .. ON rows
DPAD = (63.5, 97.04, 7.0, 8.5)                                # centre x, y; half-spacing y, x


def key_positions(layout):
    """Key-switch centres {key name: (x, y)} (board-relative mm) for design.LAYOUT.

    Rows 0-2 fill the 5-column grid from the left (rows 1-2 have three keys);
    row 3 is the arrow diamond around DPAD; the remaining rows map onto
    ROWS_MAIN, and a 4-key row (the bottom one) starts in column 1 with ON in
    column 0.
    """
    pos = {}
    for k, x in zip(layout[0], COLS):
        pos[k] = (x, ROW_SOFT)
    for k, x in zip(layout[1], COLS):
        pos[k] = (x, ROW_A)
    for k, x in zip(layout[2], COLS):
        pos[k] = (x, ROW_B)
    cx, cy, dy, dx = DPAD
    pos.update({"UP": (cx, cy - dy), "DOWN": (cx, cy + dy), "LEFT": (cx - dx, cy), "RIGHT": (cx + dx, cy)})
    for row, y in zip(layout[4:], ROWS_MAIN):
        xs = COLS if len(row) == 5 else COLS[1:]
        if len(row) == 4:
            pos["ON"] = (COLS[0], y)
        for k, x in zip(row, xs):
            pos[k] = (x, y)
    return pos


# ------------------------------------------------------------- placement ---
class Placer:
    """Occupancy bookkeeping for placement: axis-aligned rectangles per side.

    Arguments are board-relative (OX/OY added here); the stored rectangles are
    absolute. Parts that cannot be placed are recorded in failed and removed
    from the board.
    """

    def __init__(self, board):
        self.b = board
        self.rects = []        # occupied rects, top side (board coords incl. OX/OY)
        self.rects_b = []      # occupied rects, bottom side
        self.keepouts = []     # top side only (FPC fold area, latch room)
        self.keepouts_b = []   # bottom side only (was the touch FPC path; unused since the 3.2" panel)
        self.keepouts_all = [] # both sides (holes, slot)
        self.failed = []

    def fixed(self, ref, x, y, rot=0, side="F", center=False):
        """Place by footprint origin, or (center=True) by the centre of its extent.

        Fixed parts are not checked against occupancy; their rectangle (plus
        0.25 mm) is reserved for the parts placed after them.
        """
        p = self.b.place(ref, OX + x, OY + y, rot, side)
        if center:   # place once to measure, then shift so the extent's centre lands on (x, y)
            r = p.rect()
            p = self.b.place(ref, 2 * (OX + x) - (r[0] + r[2]) / 2, 2 * (OY + y) - (r[1] + r[3]) / 2, rot, side)
        (self.rects if side == "F" else self.rects_b).append(p.rect(0.25))
        return p

    def free(self, r, side="F"):
        """True if absolute rect r is 0.6 mm inside the board and overlaps nothing on side."""
        x0, y0, x1, y1 = r
        if x0 < OX + 0.6 or y0 < OY + 0.6 or x1 > OX + W - 0.6 or y1 > OY + H - 0.6:
            return False
        mine = self.rects + self.keepouts if side == "F" else self.rects_b + self.keepouts_b
        for o in mine + self.keepouts_all:
            if x0 < o[2] and x1 > o[0] and y0 < o[3] and y1 > o[1]:
                return False
        return True

    def near(self, ref, tx, ty, rot=0, step=0.5, max_r=25.0, region=None, side="F"):
        """Place ref at the free spot closest to (tx, ty) (board-relative).

        Candidates lie on rings of radius 0, step, 2*step, ... (< max_r), with
        about one candidate per step of circumference, snapped to a 0.05 mm
        grid; the first one whose rect (+0.2 mm) is free, and inside region
        (board-relative x0, y0, x1, y1) if given, wins. Rotation is fixed.
        Returns the Placed, or None (and records the failure).
        """
        for rr in [0.0] + [step * i for i in range(1, int(max_r / step))]:
            n = max(1, int(2 * math.pi * rr / step))
            for i in range(n):
                a = 2 * math.pi * i / n
                x, y = tx + rr * math.cos(a), ty + rr * math.sin(a)
                x, y = round(x * 20) / 20, round(y * 20) / 20
                p = self.b.place(ref, OX + x, OY + y, rot, side)
                r = p.rect(0.2)
                if region and not (r[0] >= OX + region[0] and r[2] <= OX + region[2]
                                   and r[1] >= OY + region[1] and r[3] <= OY + region[3]):
                    continue
                if self.free(r, side):
                    (self.rects if side == "F" else self.rects_b).append(r)
                    return p
        self.failed.append(ref)
        del self.b.placed[ref]
        return None

    def at_pad(self, ref, owner, pad, gap=1.2, region=None):
        """Place a 2-pin part just outside `owner`'s pad, rotated to face it.

        The outward direction is the dominant axis from the owner's origin to
        the pad; the part's centre target is gap + 1 mm beyond the pad.
        """
        o = self.b.placed[owner]
        px, py = o.pad_pos(pad)[0]
        cx, cy = o.x, o.y
        dx, dy = px - cx, py - cy
        if abs(dx) > abs(dy):
            ux, rot = (1 if dx > 0 else -1), 0
            tx, ty = px + ux * (gap + 1.0), py
        else:
            uy, rot = (1 if dy > 0 else -1), 90
            tx, ty = px, py + uy * (gap + 1.0)
        return self.near(ref, tx - OX, ty - OY, rot, step=0.25, max_r=8.0, region=region)

    def under_pad(self, ref, owner, pad, inward=3.0):
        """Decoupling cap on the BOTTOM side, under `owner`, `inward` mm in from the pad,
        radial, with pad 1 (the supply side) toward the pin and its inward supply via."""
        o = self.b.placed[owner]
        px, py = o.pad_pos(pad)[0]
        dx, dy = o.x - px, o.y - py
        if abs(dx) > abs(dy):            # left/right edge: horizontal cap
            ux = 1 if dx > 0 else -1
            tx, ty, rot = px + ux * inward, py, (0 if ux > 0 else 180)
        else:                            # top/bottom edge: vertical cap
            uy = 1 if dy > 0 else -1
            tx, ty, rot = px, py + uy * inward, (270 if uy > 0 else 90)
        return self.near(ref, tx - OX, ty - OY, rot, step=0.2, max_r=4.0, side="B")


def build(netlist, layout):
    """Place every footprint of the netlist; return the Board (see module doc).

    layout is design.LAYOUT (key names; the switches are found by their value
    field, which is the key name). The caller writes the board.
    """
    b = Board(netlist, os.path.join(HERE, "../lib/calc.pretty"))
    P = Placer(b)
    comps = b.comps
    by_value = {}
    for ref, c in comps.items():
        by_value.setdefault((c["sheetname"], c["value"]), []).append(ref)

    def refs(sheet, value):
        """References on a sheet with a given value, in numeric order (R2 before R10)."""
        return sorted(by_value[(f"/{sheet}/", value)], key=lambda r: int("".join(ch for ch in r if ch.isdigit())))

    b.outline_rounded_rect(OX, OY, OX + W, OY + H, 4.0)
    P.keepouts.append((OX + FPC_KEEPOUT[0], OY + FPC_KEEPOUT[1], OX + FPC_KEEPOUT[2], OY + FPC_KEEPOUT[3]))
    # Mounting-hole keepouts; the holes themselves are written as board-only M2
    # MountingHole footprints by Board.write() from b.mount_holes
    holes = HOLES
    for hx, hy in holes:
        P.keepouts_all.append((OX + hx - 2.6, OY + hy - 2.6, OX + hx + 2.6, OY + hy + 2.6))
    b.mount_holes = [(OX + hx, OY + hy) for hx, hy in holes]

    # ---- top edge connectors
    P.fixed("J4", W / 2, 5.0, 180)                 # USB-C, mouth at the top edge
    P.fixed("J2", W - 11.0, 9.8, 180)              # microSD, card enters from the top edge
    # DEBUG (SWD + UART) sits above the MCU's top-right corner, where SWCLK/SWDIO/PA9/PA10 come
    # out; EXPANSION (slow debug signals) takes the top-left position.
    # (the three through-hole headers are fitted by JLC; see ARCHITECTURE.md §9)
    P.fixed("J5", 54.4, 2.9, 90, center=True)      # DEBUG 1x6
    P.fixed("J1", 30.0, 3.2, 90, center=True)      # SWD 2x5 1.27 mm
    P.fixed("J6", 16.0, 3.4, 90, center=True)      # EXPANSION 2x6
    P.fixed("SW1", 10.5, 9.4, 0)                   # RESET
    P.fixed("SW2", 18.5, 9.4, 0)                   # BOOT
    # ---- core
    # MCU and SDRAM sit right of centre: the panel FPC tail ends near the board centre (J3).
    P.fixed("U1", 61.0, 35.0, 0)                   # STM32H743, LQFP-144
    P.fixed("U2", 61.0, 59.4, 0)                   # SDRAM below the MCU (FMC pins face it), with a
                                                   # ~7 mm escape channel between the two pin rows
    # LCD FPC: opening faces the fold (west), pad 1 north. Flat, the FPC reaches 41.5 +-0.5 mm
    # from the LCD edge; folded around the ~2.5 mm module edge (~3.1 mm) and dipped ~3 mm into the
    # connector its end lands at x ~= 38.6-40.8 (the FPC leaves the glass ledge 2.4-3.0 mm above
    # the panel back and the touch-IC stiffener starts 1.8 mm out, so the U-turn costs 3.1-4.7 mm,
    # plus 0.3-0.5 for the dip, +-0.5 length and +-0.35 panel position; final review). J3 needs the
    # end at x >= 37.9-38.2 for full insertion (stop 2.1 mm inside the front face), so it sits at
    # 39.5: no short case; any extra length goes into the fold loop. Pins 1..40: y 13+17.77..37.27.
    P.fixed("J3", 39.5, 40.52, 90)
    # room behind J3 for the flip latch (opens upward over the solder tails)
    P.keepouts.append((OX + 41.6, OY + 28.0, OX + 43.6, OY + 53.0))
    P.fixed("U3", 35.6, 19.5, 90)                  # QSPI flash, upper left of the MCU
    P.fixed("Y1", 46.6, 40.6, 90)                  # HSE next to PH0/PH1, between J3 and U1
    P.fixed("Y2", 46.4, 25.0, 0)                   # LSE above J3, ~5 mm from PC14/PC15
    # ---- power path, hand-placed (TPS62162 datasheet layout guidance: CIN at VIN/PGND, L off SW,
    # COUT beside L, short VOS): VSYS_IN -> C1 -> shunt R5 -> VSYS -> C2/C3 -> U4 -> L1 -> C4 = 3V3.
    # The INA226 sits under the shunt so its Kelvin sense lines are a few mm long.
    P.fixed("R5", 6.0, 16.4, 0)        # 0.1 R shunt: pad 1 VSYS_IN (x 3.0), pad 2 VSYS (x 9.0)
    P.fixed("C1", 2.1, 21.6, 90)       # 22 uF on VSYS_IN
    P.fixed("U9", 6.4, 22.4, 0)        # INA226: VIN+/VIN-/VBUS on the row facing the shunt
    P.fixed("U4", 14.6, 16.0, 0)       # buck: VIN/EN on its left, SW/VOS on its right
    P.fixed("C2", 11.7, 17.0, 270)     # CIN 10 uF: pad 1 (VSYS) level with VIN/EN, pad 2 to GND
    P.fixed("C3", 12.6, 14.1, 90)      # 100 nF on VIN, above CIN
    P.fixed("L1", 18.4, 15.5, 0)       # 2.2 uH straight off SW
    P.fixed("C4", 21.5, 16.4, 270)     # COUT 22 uF beside the inductor, pad 1 at the L1 output
    P.fixed("D1", 26.6, 11.0, 0)       # USB input diode, top strip between SW2 and the USB parts
    P.fixed("U10", 26.6, 20.4, 0)     # VBUS-detect comparator, its divider placed around it
    P.fixed("C5", 23.6, 19.3, 90)     # its VBUS bypass
    P.fixed("R3", 23.6, 21.5, 90)     # divider top (VBUS -> VBUS_DIV)
    P.fixed("R6", 28.8, 20.4, 90)     # divider bottom
    # INA226 sense filter resistors in the gap between the shunt and U9, pad 1 directly under
    # the shunt pad it senses (Kelvin tap at the pad, not along the current path)
    for net, x in (("PMON_INP", 3.0), ("PMON_INN", 9.0)):
        r = next(r for r in comps if r.startswith("R") and net in [n.rsplit("/", 1)[-1] for n in comps[r]["nets"].values()])
        P.fixed(r, x, 18.9, 0)
    # ---- keypad
    pos = key_positions(layout)
    for ref in comps:
        if ref.startswith("SW") and comps[ref]["sheetname"] == "/Keypad/":
            # switch values are the key names of design.LAYOUT (firmware/keys/layout.py)
            x, y = pos[comps[ref]["value"]]
            P.fixed(ref, x, y, 0)
    # ---- batteries on the back, under the keypad
    P.fixed("BT1", W / 2, 133.7, 0, side="B")
    P.fixed("BT2", W / 2, 160.7, 0, side="B")

    # ---- MCU decoupling: one 100 nF per VDD pin, VCAPs, VDDA
    u1 = b.placed["U1"]
    # every 3V3 pin except PDR_ON (143, a static tie) gets a cap: VDD, VBAT (6), VDD33_USB (95)
    vdd_pads = [p["num"] for p in u1.fp.pads if comps["U1"]["nets"].get(p["num"]) == "3V3" and p["num"] != "143"]
    caps = refs("MCU", "100nF")
    caps = [c for c in caps if comps[c]["nets"]["1"] == "3V3"]
    # decoupling on the BOTTOM, under the package next to each pin's inward supply via: this
    # keeps the top-side perimeter free for the MCU's escape routing
    for cap, pad in zip(caps, vdd_pads):
        P.under_pad(cap, "U1", pad)
    for cap, net in (("VCAP1", "VCAP1"), ("VCAP2", "VCAP2")):
        ref = next(r for r in refs("MCU", "2.2uF") if comps[r]["nets"]["1"] == net)
        pad = next(p for p, n in comps["U1"]["nets"].items() if n == net)
        P.under_pad(ref, "U1", pad)
    vdda_pad = next(p for p, n in comps["U1"]["nets"].items() if n == "VDDA")
    for ref in [r for r in comps if comps[r]["sheetname"] == "/MCU/" and comps[r]["nets"].get("1", "").endswith("VDDA")]:
        P.under_pad(ref, "U1", vdda_pad, inward=3.6)
    P.under_pad("FB1", "U1", vdda_pad, inward=5.6)
    nrst = next(p for p, n in comps["U1"]["nets"].items() if n == "NRST")
    P.under_pad(next(r for r in refs("MCU", "100nF") if comps[r]["nets"]["1"].endswith("NRST")), "U1", nrst)
    for net, xpos, ypos in (("HSE_IN", 46.6, 38.0), ("HSE_OUT", 46.6, 43.2), ("LSE_IN", 44.6, 23.3),
                            ("LSE_OUT", 48.2, 23.3)):
        ref = next(r for r in comps if comps[r]["sheetname"] == "/MCU/" and r.startswith("C")
                   and comps[r]["nets"]["1"].endswith(net))
        P.near(ref, xpos, ypos, 0, step=0.25, max_r=2)
    P.near(next(r for r in refs("MCU", "4.7uF")), 72.0, 22.0, 90)

    # ---- SDRAM decoupling next to its supply pins
    u2 = comps["U2"]["nets"]
    sd_caps = [r for r in refs("Memory", "100nF") if comps[r]["nets"]["1"] == "3V3_BUS"]
    sd_pads = [p for p in sorted(u2, key=int) if u2[p] == "3V3_BUS"]
    for cap, pad in zip(sd_caps, sd_pads):
        P.under_pad(cap, "U2", pad, inward=2.6)
    P.near(next(r for r in refs("Memory", "10uF") if comps[r]["nets"]["1"] == "3V3_BUS"), 73.5, 57.0, 90)
    P.near("R16", 74.3, 33.75, 0, step=0.25, max_r=3)   # SDCLK source termination right at U1 pin 93 (PG8)

    # ---- the rest: grouped into regions, each part as close as possible to a target
    def group(refs_, region, target, rot=0):
        """near() each part toward target, confined to region; earlier groups win space."""
        for r in refs_:
            P.near(r, target[0], target[1], rot, region=region)

    # sheet(): still-unplaced parts of a schematic sheet; has(): part touches any of the nets
    sheet = lambda name: [r for r, c in comps.items() if c["sheetname"] == f"/{name}/" and r not in b.placed]
    has = lambda r, *names: any(n in names for n in comps[r]["nets"].values())
    # LCD series resistors where the LCD branch leaves the SDRAM data bus (left end of
    # the SDRAM, next to the bottom of the FPC connector): two columns of eight.
    lcd_r = sorted([r for r in sheet("Display") if r.startswith("R")
                    and comps[r]["nets"].get("2", "").rsplit("/", 1)[-1].startswith("LCD_D")],
                   key=lambda r: int(comps[r]["nets"]["2"].split("LCD_D")[1]))
    for i, r in enumerate(lcd_r):
        P.near(r, 43.4 + 2.4 * (i // 8), 55.4 + 1.1 * (i % 8), 0, step=0.25, max_r=2.5,
               region=(40.5, 54.2, 48.2, 66))
    # top strip: USB support parts, debug LED
    group(sheet("USB"), (30, 6.5, 50, 12.8), (41, 10.5))
    group(["D2", "R15"], (20, 7, 33, 12.8), (24, 10))
    # upper-left under the panel: power input, buck, power monitor
    pin = [r for r in sheet("Power") if has(r, "VBUS", "VBUS_DET", "VBUS_DIV", "VBUS_REF", "VSYS_IN", "VSYS", "BUCK_SW")
           or (has(r, "3V3") and not has(r, "3V3_BUS", "3V3_SD", "BUS_EN", "SD_EN"))]
    group(pin, (0.5, 12.5, 24.5, 27), (12, 20))
    # INA226 sense filter resistors right at the shunt pads (R5 pad 1 x=3.0, pad 2 x=9.0)
    group([r for r in sheet("Debug and test") if r.startswith(("U", "R54", "R55", "R56", "C49"))],
          (0.5, 12.5, 32, 27), (18, 24))
    # right of the MCU: the 3V3_SD load switch (upper) and 3V3_BUS load switch (lower);
    # lower-left under the panel: the display/backlight parts, then the remaining power
    # parts (battery switch, gauge)
    group([r for r in sheet("Power") if has(r, "SD_EN", "3V3_SD_ISET") or comps[r]["value"] == "SY6280AAC"
           and has(r, "3V3_SD")] + [r for r in sheet("Power") if has(r, "3V3_SD")], (69, 13, 81.5, 32), (74, 22))
    group([r for r in sheet("Power") if has(r, "BUS_EN", "3V3_BUS_ISET", "3V3_BUS")], (69, 40, 81.5, 68), (75, 52))
    group(sheet("Display"), (0.5, 49.5, 48, 70), (22, 60))
    group(sheet("Power"), (0.5, 49.5, 40, 70), (10, 58))
    group([r for r in sheet("Memory") if has(r, "3V3_SD", "SD_CK", "SD_CK_C")], (69, 13, 81.5, 32), (75, 18))
    group(sheet("Memory"), (28, 13, 41, 26), (34, 24))
    # right of the MCU: microSD passives, debug monitors, test points
    group(sheet("MCU"), (69, 13, 81.5, 48), (73, 30))
    group([r for r in sheet("Debug and test") if not r.startswith("TP")], (69, 13, 81.5, 48), (75, 35))
    # rail test points near their rails: 3V3_SD and VDDA on the right, the rest (VSYS, VSYS_IN,
    # BAT_P, VBUS, 3V3, 3V3_BUS, GND) in the free area left of J3 (under the folded panel FPC; reachable with the case open)
    group([r for r in sheet("Debug and test") if r.startswith("TP") and has(r, "3V3_SD", "VDDA")],
          (69, 13, 81.5, 68), (78, 44))
    group(sheet("Debug and test"), (2, 28, 30, 50), (16, 39))
    group(sheet("Keypad"), (0.5, 162, 81, H), (10, 168))
    # silkscreen at J3: pin 1 and the FPC direction (tail folds in from the left, contacts up)
    j3 = b.placed["J3"]
    p1 = j3.pad_pos("1")[0]
    b.text("1", p1[0] + 1.6, p1[1], size=0.8)
    b.text("<- FPC from panel fold, contacts down", j3.x - 7.0, j3.y, size=0.7, rot=90)
    # 3V3 plane island on In2 under the MCU body: the MCU's inward 3V3 vias and the bottom-side
    # decoupling caps' vias all land on it (no signal routing on In2 there).
    u1 = b.placed["U1"]
    b.power_islands = [("3V3", "In2.Cu", (u1.x - 9.2, u1.y - 9.2, u1.x + 9.2, u1.y + 9.2))]
    # 3V3_BUS island under the SDRAM body: every VDD/VDDQ pin and its bottom-side cap drop a
    # via onto it, so each cap is a via pair away from its pin instead of 4-10 mm of trace.
    u2 = b.placed["U2"]
    ys = [p[1] for pad in u2.fp.pads for p in [u2.pad_pos(pad["num"])[0]]]
    b.power_islands.append(("3V3_BUS", "In2.Cu", (u2.x - 10.4, min(ys) + 1.6, u2.x + 10.4, max(ys) - 1.6)))
    # Crystals: no foreign routing on the inner/bottom layers under the crystal and its caps,
    # and GND copper on In2 there.
    b.route_keepouts, b.gnd_patches = [], []
    for y_ref, caps in (("Y1", ("HSE_IN", "HSE_OUT")), ("Y2", ("LSE_IN", "LSE_OUT"))):
        pts = [b.placed[y_ref].pad_pos(p["num"])[0] for p in b.placed[y_ref].fp.pads]
        for r in comps:
            if r.startswith("C") and comps[r]["sheetname"] == "/MCU/" and comps[r]["nets"]["1"].rsplit("/", 1)[-1] in caps:
                pts += [b.placed[r].pad_pos(p["num"])[0] for p in b.placed[r].fp.pads]
        rect = (min(p[0] for p in pts) - 0.6, min(p[1] for p in pts) - 0.6,
                max(p[0] for p in pts) + 0.6, max(p[1] for p in pts) + 0.6)
        # (no inner-layer routing keepout: In1 GND already shields the F.Cu crystal from In2/B,
        # and the crystals sit in the escape channel between J3 and the MCU)
        b.gnd_patches.append(rect)
    b.decoupling_b = sorted(r for r, p in b.placed.items() if p.side == "B" and r.startswith("C"))
    b.unplaced = [r for r in comps if r not in b.placed]
    return b


if __name__ == "__main__":
    sys.path.insert(0, os.path.join(HERE))
    import design  # noqa: E402  (for the key layout)
    board = build(os.path.join(HERE, "../build/calc.net"), design.LAYOUT)
    board.write(os.path.join(HERE, "../kicad/calc.kicad_pcb"), design.TITLE_BLOCK)
    print(f"placed {len(board.placed)} footprints; unplaced: {board.unplaced or 'none'}")
    sys.exit(1 if board.unplaced else 0)
