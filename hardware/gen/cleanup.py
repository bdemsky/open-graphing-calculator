#!/usr/bin/env python3
"""Final board cleanup on the saved KiCad file, driven by KiCad's DRC report.

  - removes vias that DRC reports as dangling and that no track comes near (rip-up
    leftovers). Dangling track stubs are left: removing one can split copper that only
    overlaps (KiCad then reports an unconnected item); they are harmless warnings.
  - connects large mechanical GND pads (USB-C shell, battery tabs) to the pours solidly:
    thermal spokes on them are starved by the surrounding pads, and these parts are
    machine-soldered anyway
Run kicad-cli DRC with --refill-zones --save-board afterwards.
Usage: cleanup.py board.kicad_pcb drc.json

Called by pcb_build.py at the end of the build, with the JSON DRC report of the
saved board. It edits the .kicad_pcb text directly (by paren matching on the
top-level blocks) rather than through kicad_pcb.Board, because by then the
file has been re-saved by KiCad ("kicad-cli pcb upgrade") and only small
surgical changes are wanted.
"""
import json
import math
import re
import sys

SOLID_REFS = {"J4", "BT1", "BT2", "TP9", "TP10"}  # + the GND test points (probe clips)


def blocks(text, head):
    """(start, end) of every top-level-in-board '(head ...' block, by paren matching.

    Top-level board items are written by KiCad at one tab of indentation, so
    "\\n\\t(head" finds them without matching nested items (e.g. a footprint's
    own pads). start is the index of the preceding newline; end is just past
    the closing paren. Parentheses inside quoted strings are not special-cased.
    """
    for m in re.finditer(r"\n\t\(" + head + r"\b", text):
        i, depth = m.start() + 2, 0
        for j in range(i, len(text)):
            depth += {"(": 1, ")": -1}.get(text[j], 0)
            if depth == 0:
                yield m.start(), j + 1
                break


def xy(s, key):
    """First '(key X Y' coordinate pair in s as floats (mm), or None."""
    m = re.search(r"\(" + key + r" ([-\d.]+) ([-\d.]+)", s)
    return (float(m.group(1)), float(m.group(2))) if m else None


def main(path, drc_json):
    """Apply both clean-ups to the board file at path in place (see module doc)."""
    text = open(path).read()
    d = json.load(open(drc_json))
    vias = [(it["pos"]["x"], it["pos"]["y"]) for v in d["violations"] if v["type"] == "via_dangling"
            for it in v["items"]]
    cut = []    # (start, end) text spans to delete
    # every track endpoint on the board, to tell orphan vias from connected ones
    ends = [p for s, e in blocks(text, "segment") for p in (xy(text[s:e], "start"), xy(text[s:e], "end"))]
    for s, e in blocks(text, "via"):
        at = xy(text[s:e], "at")
        # a via can be "dangling" (copper on one layer only) yet join tracks that merely
        # overlap its pad: only vias with no track near them at all are removed
        if any(math.dist(at, p) < 0.002 for p in vias) and \
                not any(math.dist(at, q) < 0.6 for q in ends):
            cut.append((s, e))
    # (dangling track stubs are deliberately left in place: see the module docstring)
    # delete back to front so earlier spans keep their offsets
    for s, e in sorted(cut, reverse=True):
        text = text[:s] + text[e:]
    # solid zone connection on the big mechanical parts: a footprint-level
    # "(zone_connect 2)" (2 = solid) inserted after the footprint's first line
    # overrides the zone's thermal-relief setting for all its pads
    n_solid = 0
    for s, e in sorted(blocks(text, "footprint"), reverse=True):
        body = text[s:e]
        ref = re.search(r'\(property "Reference" "([^"]+)"', body)
        if ref and ref.group(1) in SOLID_REFS and "(zone_connect" not in body:
            k = body.index("\n", 1)
            body = body[:k] + "\n\t\t(zone_connect 2)" + body[k:]
            text = text[:s] + body + text[e:]
            n_solid += 1
    open(path, "w").write(text)
    print(f"cleanup: removed {len(cut)} of {len(vias)} dangling vias; "
          f"solid zone connection on {n_solid} footprints")


if __name__ == "__main__":
    main(*sys.argv[1:])
