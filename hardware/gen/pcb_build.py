#!/usr/bin/env python3
"""Place, autoroute (Freerouting) and write the board.

  pcb_build.py place     placement only (fast preview)
  pcb_build.py route     placement + Freerouting + zones
  pcb_build.py finish    like route, but reuses the last Freerouting session
                         (build/calc.ses) instead of running it again

Input: build/calc.net (the netlist build.sh exports from the generated
schematic). Output: kicad/calc.kicad_pcb, plus DSN/SES files, Freerouting logs
and DRC reports in build/. Needs Java + tools/freerouting.jar and kicad-cli.
It is run by hand (not by build.sh) and the routed board is committed.

Routing pipeline (route/finish):
  1. layout.build(): placement, power islands, crystal GND patches.
  2. fanout: bridge same-net neighbour pads, then pre-place GND vias for every
     SMD GND pad and 3V3/3V3_BUS vias for the MCU, SDRAM and their bottom-side
     caps. This copper is exported as protected wiring.
  3. Freerouting (PASSES env var, default 40 passes) on the DSN from route.py;
     import the session, drop vias in SMD pads.
  4. finish.finish(): grid A* joins any net still split into islands. If some
     remain, up to three Freerouting "continuation" runs (12 passes each) on
     the current copper, each followed by the finisher.
  5. Remove dangling routed vias, add GND stitching vias, add the zones
     (In1 GND plane, In2 3V3 pour + 3V3/3V3_BUS islands + GND under the
     crystals, F/B GND pours) and write the board.
  6. Fill-aware finishing: KiCad refills the zones and saves; the poured
     polygons of non-GND nets are read back as copper and the finisher runs
     again, until it has nothing left to join (max 3 rounds).
  7. DRC repair (max 4 rounds): rip up tracks in clearance/short violations
     and route each reported unconnected pair, until DRC is clean.
  8. cleanup.py (stray vias, solid zone connection on big mechanical pads) and
     a final DRC with refill and save (build/drc_final.rpt).

Documentation-only changes to the routed board (footprint values, title block)
are applied by annotate.py instead: it edits the committed board in place, so
the ordered copper is never re-routed.
"""
import math
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import design  # noqa: E402
import fanout  # noqa: E402
import finish  # noqa: E402
import layout  # noqa: E402
import route  # noqa: E402

BUILD = os.path.join(HERE, "../build")
BOARD = os.path.join(HERE, "../kicad/calc.kicad_pcb")
FREEROUTING = os.path.join(HERE, "../../tools/freerouting.jar")


def outline_points(x0, y0, x1, y1, r, n=6):
    """Closed polygon for a rounded rectangle (mm), each corner arc as n segments.

    Used for the DSN routing boundary and as the outline of the full-board zones.
    """
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts + [pts[0]]


def main(mode):
    """Run the pipeline for mode "place", "route" or "finish" (see module doc)."""
    board = layout.build(os.path.join(BUILD, "calc.net"), design.LAYOUT)
    if board.unplaced:
        sys.exit(f"unplaced: {board.unplaced}")
    x0, y0, x1, y1, r = board.outline
    # zone outline 0.3 mm inside the edge (JLC copper-to-edge minimum)
    inset = outline_points(x0 + 0.3, y0 + 0.3, x1 - 0.3, y1 - 0.3, max(r - 0.3, 0.5))
    if mode in ("route", "finish"):
        print(f"bridged same-net neighbour pads: {fanout.bridge_adjacent(board)}")
        n, failed = fanout.prefanout(board)
        print(f"ground fanout: {n} vias; no room for: {failed or 'none'}")
        n, failed = fanout.prefanout(board, "3V3", only=("U1",) + tuple(board.decoupling_b))  # into the In2 island
        print(f"MCU 3V3 fanout: {n} vias; no room for: {failed or 'none'}")
        n, failed = fanout.prefanout(board, "3V3_BUS", only=("U2",) + tuple(board.decoupling_b))
        print(f"SDRAM 3V3_BUS fanout: {n} vias; no room for: {failed or 'none'}")
        dsn, ses = os.path.join(BUILD, "calc.dsn"), os.path.join(BUILD, "calc.ses")
        # keep copper >= 0.3 mm from the edge: boundary inset 0.2 + routing clearance
        route.export_dsn(board, dsn, outline_points(x0 + 0.2, y0 + 0.2, x1 - 0.2, y1 - 0.2, r - 0.2))
        if mode == "route" and os.path.exists(ses):
            os.remove(ses)
        cmd = ["java", "-jar", FREEROUTING, "-de", dsn, "-do", ses, "-mp", os.environ.get("PASSES", "40"),
               "--gui.enabled=false"]
        if mode == "route":   # "finish" reuses the last Freerouting session
            with open(os.path.join(BUILD, "freerouting.log"), "w") as log:
                subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=False)
        if not os.path.exists(ses):
            sys.exit("Freerouting produced no session; see build/freerouting.log")
        fixed = (len(board.tracks), len(board.vias))    # pre-fanout copper stays protected
        nt, nv = route.import_ses(board, ses)
        print(f"imported {nt} track segments, {nv} vias; vias in pads removed: {finish.drop_vias_in_pads(board)}")
        joined, failed = finish.finish(board)
        print(f"finishing router: joined {joined or 'nothing'}; failed {failed or 'none'}")
        for attempt in range(3):
            if not failed:
                break
            # Freerouting continuation: everything but the pre-fanout is re-routable, so its
            # rip-up-and-retry can make room for the stragglers.
            route.export_dsn(board, dsn, outline_points(x0 + 0.2, y0 + 0.2, x1 - 0.2, y1 - 0.2, r - 0.2),
                             n_protected=fixed)
            cont_ses = os.path.join(BUILD, f"calc_cont{attempt}.ses")
            if os.path.exists(cont_ses):
                os.remove(cont_ses)
            with open(os.path.join(BUILD, f"freerouting_cont{attempt}.log"), "w") as log:
                subprocess.run(["java", "-jar", FREEROUTING, "-de", dsn, "-do", cont_ses, "-mp", "12",
                                "--gui.enabled=false"], stdout=log, stderr=subprocess.STDOUT, check=False)
            if not os.path.exists(cont_ses):
                print("continuation produced no session")
                break
            del board.tracks[fixed[0]:]
            del board.vias[fixed[1]:]
            nt, nv = route.import_ses(board, cont_ses)
            finish.drop_vias_in_pads(board)
            joined, failed = finish.finish(board)
            print(f"continuation {attempt}: {nt} segments, {nv} vias; finisher joined {joined or 'nothing'}; "
                  f"failed {failed or 'none'}")
        print(f"dangling vias removed: {finish.drop_dangling_vias(board, first=fixed[1])}")
        print(f"stitching vias: {fanout.stitch(board)}")
    # GND plane on In1; GND pours on the other layers fill what routing leaves. GND pads join
    # the pours through thermal reliefs (even heating of 0402s, solderable hand-fitted headers).
    board.zone("GND", "In1.Cu", inset, name="GND plane", solid=False)
    for i, (x0_, y0_, x1_, y1_) in enumerate(getattr(board, "gnd_patches", [])):   # In2 under the crystals
        board.zone("GND", "In2.Cu", [(x0_, y0_), (x1_, y0_), (x1_, y1_), (x0_, y1_)], priority=3,
                   clearance=0.2, name=f"GND under crystal {i}", solid=False)
    board.zone("3V3", "In2.Cu", inset, priority=0, name="3V3 pour In2.Cu")
    for net, layer, (ix0, iy0, ix1, iy1) in getattr(board, "power_islands", []):
        board.zone(net, layer, [(ix0, iy0), (ix1, iy0), (ix1, iy1), (ix0, iy1)], priority=2,
                   clearance=0.2, name=f"{net} island {layer}")
    for layer in ("F.Cu", "B.Cu"):
        board.zone("GND", layer, inset, priority=0, name=f"GND pour {layer}", solid=False)
    board.write(BOARD, design.TITLE_BLOCK)
    print(f"wrote {os.path.relpath(BOARD)}")
    if mode in ("route", "finish"):
        # close the loop with KiCad's real zone fills: the finisher can't see how pours
        # fragment around vias, so fill, read the polygons back, and finish again
        kicad = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
        for it in range(3):
            subprocess.run([kicad, "pcb", "upgrade", BOARD], capture_output=True)
            subprocess.run([kicad, "pcb", "drc", "--refill-zones", "--save-board", "-o",
                            os.path.join(BUILD, "drc_loop.rpt"), BOARD], capture_output=True)
            board.fills = [f for f in finish.read_fills(BOARD) if f[0].rsplit("/", 1)[-1] != "GND"]
            joined, failed = finish.finish(board)
            print(f"fill-aware finish {it}: {len(board.fills)} fill polygons; joined {joined or 'nothing'}; "
                  f"failed {failed or 'none'}")
            board.fills = []
            board.write(BOARD, design.TITLE_BLOCK)
            if not joined:
                break
        # final repairs driven by KiCad's own DRC
        for it in range(4):
            # "pcb upgrade" re-saves the board in the installed KiCad's own format
            # before each DRC run
            subprocess.run([kicad, "pcb", "upgrade", BOARD], capture_output=True)
            dj = os.path.join(BUILD, "drc_repair.json")
            subprocess.run([kicad, "pcb", "drc", "--refill-zones", "--format", "json", "-o", dj, BOARD],
                           capture_output=True)
            import json as _json
            d = _json.load(open(dj))
            bad = [v for v in d["violations"] if v["type"] in ("clearance", "shorting_items")]
            print(f"DRC repair {it}: {len(d['unconnected_items'])} unconnected, {len(bad)} clearance/short")
            if not d["unconnected_items"] and not bad:
                break
            # (this DRC run does not --save-board, so the file read here holds the
            # fills of whatever was last saved by KiCad, if any)
            board.fills =[f for f in finish.read_fills(BOARD) if f[0].rsplit("/", 1)[-1] != "GND"]
            print("   " + finish.repair_from_drc(board, dj, first_track=fixed[0]))
            board.fills = []
            board.write(BOARD, design.TITLE_BLOCK)
        # solid zone connection on the big mechanical pads, stray via removal (gen/cleanup.py)
        subprocess.run([kicad, "pcb", "upgrade", BOARD], capture_output=True)
        dj = os.path.join(BUILD, "drc_cleanup.json")
        subprocess.run([kicad, "pcb", "drc", "--refill-zones", "--severity-all", "--format", "json", "-o", dj,
                        BOARD], capture_output=True)
        subprocess.run([sys.executable, os.path.join(HERE, "cleanup.py"), BOARD, dj], check=True)
        subprocess.run([kicad, "pcb", "drc", "--refill-zones", "--save-board", "-o",
                        os.path.join(BUILD, "drc_final.rpt"), BOARD], capture_output=True)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "place")
