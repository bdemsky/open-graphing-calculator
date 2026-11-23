"""Pick JLCPCB Basic passives by value.

Lookups hit the local JLC parts database (tools/jlcdb) and are cached in
parts_cache.json, which is committed so the schematic regenerates
identically without the 300 MB database.

Used by design.py (its R() and C() helpers) to attach an LCSC part number,
manufacturer part and KiCad footprint to every resistor and capacitor. Only
"Basic" library parts are considered: JLC charges a per-part-type loading fee
for "Extended" parts, and the board has dozens of passive values. The local
sqlite database is a snapshot and a subset of JLC's live catalogue, so a value
missing here may still exist at JLC; stock numbers are those of the snapshot.

Cache keys are "kind|value|pkg,pkg,...|min_v", e.g. "C|100nF|0402,0603,0805|6.3".
Delete an entry from parts_cache.json to force a fresh lookup.
"""
import json
import os
import re
import sqlite3

HERE = os.path.dirname(os.path.abspath(__file__))
DB = os.path.join(HERE, "../../tools/jlcdb/jlcpcb-components.sqlite3")
CACHE = os.path.join(HERE, "parts_cache.json")

FOOTPRINTS = {
    ("C", "0402"): "Capacitor_SMD:C_0402_1005Metric",
    ("C", "0603"): "Capacitor_SMD:C_0603_1608Metric",
    ("C", "0805"): "Capacitor_SMD:C_0805_2012Metric",
    ("R", "0402"): "Resistor_SMD:R_0402_1005Metric",
    ("R", "0603"): "Resistor_SMD:R_0603_1608Metric",
    ("R", "0805"): "Resistor_SMD:R_0805_2012Metric",
}

_cache = None    # parts_cache.json contents, loaded lazily on the first pick()


def _load():
    """Return the cache dict, reading parts_cache.json on first use."""
    global _cache
    if _cache is None:
        _cache = json.load(open(CACHE)) if os.path.exists(CACHE) else {}
    return _cache


def save():
    """Write the cache back (sorted keys, so the committed file diffs cleanly).

    Called by design.py after the schematic is generated; a no-op if nothing
    was ever looked up.
    """
    if _cache is not None:
        with open(CACHE, "w") as f:
            json.dump(_cache, f, indent=1, sort_keys=True)


def _si(text):
    """'4.7uF' / '100nF' / '6.8k' / '1' -> float in base units."""
    m = re.fullmatch(r"([\d.]+)\s*([pnuµmkM]?)", text.replace("Ω", "").replace("F", ""))
    if not m:
        raise ValueError(f"bad value {text!r}")
    mult = {"": 1, "p": 1e-12, "n": 1e-9, "u": 1e-6, "µ": 1e-6, "m": 1e-3, "k": 1e3, "M": 1e6}
    return float(m.group(1)) * mult[m.group(2)]


def _volts(text):
    """'6.3V' -> 6.3; an empty/missing rating -> 0.0 (fails any min_v > 0)."""
    return float(text.rstrip("V")) if text else 0.0


def _query(kind, value, pkgs, min_v):
    """Search the JLC database for a Basic part; return its cache record.

    kind is "C" (MLCC) or "R" (chip resistor); value is a string such as
    "4.7uF" or "6.8k" and must match exactly (to 1 ppm). Capacitors must also be
    rated >= min_v volts and use a class-I/II dielectric (X5R/X7R/C0G/...): Y5V
    and Z5U lose most of their capacitance with bias and temperature.

    Among the matches the earliest package in pkgs wins (so the caller states
    its size preference, smallest first by default), then the highest stock.
    Returns {"lcsc", "mfr", "package", "footprint"}; raises LookupError when
    nothing qualifies.
    """
    db = sqlite3.connect(DB)
    if kind == "C":
        sub, attr = "Multilayer Ceramic%", "Capacitance"
    else:
        sub, attr = "Chip Resistor%", "Resistance"
    rows = db.execute(
        "select lcsc, mfr, package, stock, attributes from jlc_components "
        "where library_type='base' and present=1 and subcategory like ? and package in (%s)"
        % ",".join("?" * len(pkgs)),
        (sub, *pkgs),
    ).fetchall()
    want = _si(value)
    best = None
    for lcsc, mfr, pkg, stock, attrs in rows:
        a = json.loads(attrs)
        try:
            v = _si(a.get(attr, ""))
        except ValueError:
            continue
        if abs(v - want) > want * 1e-6:
            continue
        if kind == "C":
            if _volts(a.get("Voltage Rating", "")) < min_v:
                continue
            if a.get("Temperature Coefficient", "") not in ("X5R", "X7R", "C0G", "NP0", "X7S", "X6S"):
                continue
        key = (pkgs.index(pkg), -stock)    # preferred package first, then most stock
        if best is None or key < best[0]:
            best = (key, {"lcsc": f"C{lcsc}", "mfr": mfr, "package": pkg,
                          "footprint": FOOTPRINTS[(kind, pkg)]})
    if best is None:
        raise LookupError(f"no Basic {kind} {value} >= {min_v}V in {pkgs}")
    return best[1]


def pick(kind, value, pkgs=("0402", "0603", "0805"), min_v=0.0):
    """Cached _query(): the Basic part for (kind, value, packages, min voltage).

    The database is only opened on a cache miss, so a fully cached design
    builds without tools/jlcdb present.
    """
    key = f"{kind}|{value}|{','.join(pkgs)}|{min_v}"
    cache = _load()
    if key not in cache:
        cache[key] = _query(kind, value, list(pkgs), min_v)
    return cache[key]
