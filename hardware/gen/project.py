#!/usr/bin/env python3
"""Write kicad/calc.kicad_pro: design rules and net classes (JLCPCB 4-layer capable).

Run by hand (no arguments) whenever the rules change; the file it writes is
committed. KiCad's DRC (run by pcb_build.py) checks the board against these
rules. The Freerouting DSN export (route.py) does not read this file: it takes
the same net-name sets (imported below) and writes its own widths, so the two
must be kept in step.

Rules are the JLCPCB 4-layer standard capabilities (0.1 mm / 0.1 mm
track/space, 0.3 mm drill, 0.5 mm via). Net classes are assigned by wildcard
pattern from the net-name sets in route.py, so a net on a hierarchical sheet
("/Power/VSYS") still matches ("*VSYS"):

  Battery  0.8 mm  the cell/battery-switch path, which carries the full system
                   current at the lowest voltage (IR drop matters most there)
  Power    0.3 mm  VBUS, buck switch node, backlight anode/cathode and sense, GND
  Rail     0.2 mm  regulated 3V3 rails, VSYS, VDDA
  Default  0.1 mm  everything else (signals)

The "priority" field orders the classes when more than one could apply;
Default carries INT_MAX, the value KiCad itself writes for it.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from route import BATT_NETS, POWER_NETS, RAIL_NETS  # noqa: E402


def netclass(name, track, clearance, priority):
    """One KiCad 8 net-class entry (all dimensions in mm).

    Only name/track/clearance/priority vary; via 0.6/0.3 mm is JLC's standard
    via size, and the diff-pair and schematic fields are KiCad defaults that
    must be present for the project file to load cleanly.
    """
    return {"name": name, "clearance": clearance, "track_width": track, "via_diameter": 0.6,
            "via_drill": 0.3, "microvia_diameter": 0.3, "microvia_drill": 0.1, "diff_pair_width": 0.2,
            "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25, "wire_width": 6, "bus_width": 12,
            "line_style": 0, "pcb_color": "rgba(0, 0, 0, 0.000)",
            "schematic_color": "rgba(0, 0, 0, 0.000)", "priority": priority}


pro = {
    "meta": {"filename": "calc.kicad_pro", "version": 3},
    "board": {"design_settings": {
        "rules": {"min_clearance": 0.1, "min_track_width": 0.1, "min_via_diameter": 0.5,
                  "min_via_annular_width": 0.1, "min_through_hole_diameter": 0.3, "min_hole_clearance": 0.25,
                  "min_hole_to_hole": 0.25, "min_copper_edge_clearance": 0.3, "min_silk_clearance": 0.0,
                  "min_text_height": 0.6, "min_text_thickness": 0.1, "min_microvia_diameter": 0.2,
                  "min_microvia_drill": 0.1, "solder_mask_to_copper_clearance": 0.0},
        "rule_severities": {"silk_over_copper": "ignore", "silk_overlap": "ignore",
                            "silk_edge_clearance": "ignore", "lib_footprint_issues": "ignore",
                            "lib_footprint_mismatch": "warning"},
    }},
    "net_settings": {
        "meta": {"version": 4},
        "classes": [netclass("Default", 0.1, 0.1, 2147483647), netclass("Rail", 0.2, 0.1, 1),
                    netclass("Power", 0.3, 0.1, 0), netclass("Battery", 0.8, 0.1, 0)],
        "netclass_patterns": ([{"netclass": "Power", "pattern": f"*{n}"} for n in sorted(POWER_NETS)] +
                              [{"netclass": "Battery", "pattern": f"*{n}"} for n in sorted(BATT_NETS)] +
                              [{"netclass": "Rail", "pattern": f"*{n}"} for n in sorted(RAIL_NETS)]),
    },
}
with open(os.path.join(HERE, "../kicad/calc.kicad_pro"), "w") as f:
    json.dump(pro, f, indent=2)
print("wrote kicad/calc.kicad_pro")
