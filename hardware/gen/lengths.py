#!/usr/bin/env python3
"""SDRAM bus length report: routed length of every FMC/SDRAM net and its skew
against the SDRAM clock path, from the routed board file.

Budget (100 MHz SDR, 10 ns period): ~6.5 ps/mm on FR4 microstrip, so 10 mm of
skew costs ~65 ps against ~1-2 ns of setup/hold margin. We flag nets more than
SKEW_LIMIT_MM from the clock path, or longer than LENGTH_LIMIT_MM overall.

The clock path is FMC_SDCLK (MCU pin to the 22 R termination) plus SDRAM_CLK
(resistor to the SDRAM). Lengths are summed segment and arc lengths per net;
vias are counted but their barrel length is not added.
Exit status is 1 if any net is flagged or no SDRAM nets are routed.

Usage: lengths.py [board.kicad_pcb]   (default kicad/calc.kicad_pcb)
"""
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BOARD = os.path.join(HERE, "../kicad/calc.kicad_pcb")
SKEW_LIMIT_MM = 25.0
LENGTH_LIMIT_MM = 60.0
PS_PER_MM = 6.5     # propagation delay used for the ps column


def arc_length(a, m, b):
    """Length of the circular arc from a through m to b (chord if collinear)."""
    ax, ay = a[0] - m[0], a[1] - m[1]
    bx, by = b[0] - m[0], b[1] - m[1]
    d = 2 * (ax * by - ay * bx)
    if abs(d) < 1e-12:
        return math.dist(a, b)
    # circle centre relative to m (perpendicular bisectors of m-a and m-b)
    ux = (by * (ax * ax + ay * ay) - ay * (bx * bx + by * by)) / d
    uy = (ax * (bx * bx + by * by) - bx * (ax * ax + ay * ay)) / d
    r = math.hypot(ux, uy)
    # inscribed angle: the arc a-m-b subtends 2 * (pi - angle at m)
    ang_m = math.atan2(abs(ax * by - ay * bx), ax * bx + ay * by)
    return r * 2 * (math.pi - ang_m)


def net_lengths(path):
    """({net: routed length mm}, {net: via count}) from a .kicad_pcb file.

    Nets are referenced by number in older board files and by name in newer
    ones; both forms are accepted.
    """
    t = open(path).read()
    ids = {m.group(1): m.group(2) for m in re.finditer(r'\(net (\d+) "([^"]*)"\)', t)}
    lengths, vias = {}, {}
    for m in re.finditer(r'\((segment|arc)\s(.*?)\(uuid', t, re.S):
        body = m.group(2)
        a = re.search(r'\(start ([-\d.]+) ([-\d.]+)\)', body)
        b = re.search(r'\(end ([-\d.]+) ([-\d.]+)\)', body)
        n = re.search(r'\(net (?:(\d+)|"([^"]*)")', body)
        if not (a and b and n):
            continue
        net = ids.get(n.group(1), n.group(1)) if n.group(1) else n.group(2)
        pa, pb = (float(a.group(1)), float(a.group(2))), (float(b.group(1)), float(b.group(2)))
        mid = re.search(r'\(mid ([-\d.]+) ([-\d.]+)\)', body) if m.group(1) == "arc" else None
        d = arc_length(pa, (float(mid.group(1)), float(mid.group(2))), pb) if mid else math.dist(pa, pb)
        lengths[net] = lengths.get(net, 0.0) + d
    for m in re.finditer(r'\(via\s(.*?)\(uuid', t, re.S):
        n = re.search(r'\(net (?:(\d+)|"([^"]*)")', m.group(1))
        if n:
            net = ids.get(n.group(1), n.group(1)) if n.group(1) else n.group(2)
            vias[net] = vias.get(net, 0) + 1
    return lengths, vias


def main(path=BOARD):
    """Print the report for the SDRAM command/address/data nets; return the exit status."""
    L, V = net_lengths(path)
    short = {k.rsplit("/", 1)[-1]: k for k in L}
    clk = L.get(short.get("FMC_SDCLK", ""), 0.0) + L.get(short.get("SDRAM_CLK", ""), 0.0)
    pat = re.compile(r"^(FMC_(D\d+|A\d+|BA\d|NBL\d|SDNWE|SDNCAS|SDNRAS|SDNE1|SDCKE1))$")
    rows = sorted((L[full], s, V.get(full, 0)) for s, full in short.items() if pat.match(s))
    if not rows or clk == 0:
        print("no routed SDRAM nets found")
        return 1
    print(f"SDRAM clock path {clk:.1f} mm; {len(rows)} bus nets {rows[0][0]:.1f}-{rows[-1][0]:.1f} mm")
    bad = []
    for length, name, vias in rows:
        skew = length - clk
        flag = abs(skew) > SKEW_LIMIT_MM or length > LENGTH_LIMIT_MM
        if flag:
            bad.append(name)
        print(f"  {name:12s} {length:6.1f} mm  skew {skew:+6.1f} mm ({skew * PS_PER_MM:+5.0f} ps)  vias {vias}"
              + ("   <-- check" if flag else ""))
    print("note: FMC_D* lengths include the LCD branch up to its series resistors")
    print(f"{len(bad)} net(s) outside limits: {bad or 'none'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
