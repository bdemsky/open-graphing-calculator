#!/usr/bin/env python3
"""Sanity checks on the exported netlist, beyond KiCad's ERC.

- every net has at least two pins
- every MCU pin in pinmap.py lands on the intended net
- power rails reach the parts that need them
Exit status is non-zero on any failure.

Run by build.sh as the last step, on the KiCad netlist exported from the
generated schematic (build/calc.net). It closes the loop between the two
sources of truth for the MCU pin-out: pinmap.py (validated against ST's pin
data) and design.py (which draws the schematic). The netlist is itself an
s-expression, so the schematic parser from kicad_sch.py reads it.

Usage: check_netlist.py build/calc.net
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "../pinmap"))
import pinmap  # noqa: E402
from kicad_sch import find, parse  # noqa: E402


def load(path):
    """Parse a KiCad netlist (.net) file.

    Returns (comps, nets): comps maps ref -> {"value", "footprint"}; nets maps
    the net name (leading "/" of root-sheet nets stripped) -> list of
    (ref, pin number, pin function) tuples, one per connected pin.
    """
    root = parse(open(path).read())[0]
    comps = {}
    for c in find(find(root, "components")[0], "comp"):
        ref = find(c, "ref")[0][1]
        comps[ref] = {"value": find(c, "value")[0][1],
                      "footprint": (find(c, "footprint") or [[None, ""]])[0][1]}
    nets = {}
    for n in find(find(root, "nets")[0], "net"):
        name = find(n, "name")[0][1].lstrip("/")
        nodes = [(find(x, "ref")[0][1], find(x, "pin")[0][1],
                  (find(x, "pinfunction") or [[None, ""]])[0][1]) for x in find(n, "node")]
        nets[name] = nodes
    return comps, nets


def main(path):
    """Run all checks on the netlist at path; print a summary and any errors.

    Returns the process exit status (0 = all checks passed).
    """
    comps, nets = load(path)
    errors = []
    for name, nodes in nets.items():
        if len(nodes) < 2 and not name.startswith("unconnected-"):
            errors.append(f"net {name} has a single pin: {nodes}")

    # MCU pin name (e.g. "PD7") -> net it is on, compared with what pinmap.py
    # assigned (ASSIGN plus the keypad matrix rows/columns)
    pin_net = {}
    for name, nodes in nets.items():
        for ref, pin, func in nodes:
            if ref == "U1":
                # KiCad reports pinfunction as "<name>_<number>"
                pin_net[func.removesuffix(f"_{pin}")] = name
    want = {pin: net for net, (pin, _s, _n) in pinmap.ASSIGN.items()}
    want.update({pin: f"KEY_ROW{i}" for i, pin in enumerate(pinmap.KEY_ROWS)})
    want.update({pin: f"KEY_COL{i}" for i, pin in enumerate(pinmap.KEY_COLS)})
    for pin, net in want.items():
        got = pin_net.get(pin)
        if got is None or got.rsplit("/", 1)[-1] != net:  # local nets are "Sheet/NET"
            errors.append(f"U1 {pin}: expected net {net}, got {got}")

    def refs_on(net):
        return {ref for ref, _p, _f in nets.get(net, [])}

    # U1 MCU, U2 SDRAM, U3 QSPI flash must sit on their supply rails (catches a
    # renamed or mistyped rail label in design.py)
    rails = {"3V3": ["U1", "U3"], "3V3_BUS": ["U2"], "GND": ["U1", "U2", "U3"]}
    for net, need in rails.items():
        missing = [r for r in need if r not in refs_on(net)]
        if missing:
            errors.append(f"{net} does not reach {missing}")

    sizes = sorted(((len(v), k) for k, v in nets.items()), reverse=True)[:6]
    print(f"{len(comps)} components, {len(nets)} nets; biggest: " +
          ", ".join(f"{k}({n})" for n, k in sizes))
    for e in errors:
        print("ERROR:", e)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
