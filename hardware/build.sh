#!/bin/bash
# Regenerate the schematic from hardware/gen/design.py and check it.
#   ./build.sh          generate + ERC + netlist
#   ./build.sh import   also re-import JLC parts with easyeda2kicad first
set -euo pipefail
cd "$(dirname "$0")"
PY=../.venv/bin/python
KICAD_CLI=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
OUT=build
mkdir -p "$OUT"

[ "${1:-}" = import ] && $PY -I lib/import_parts.py
/opt/local/bin/python3.13 -I pinmap/pinmap.py >/dev/null
$PY -I gen/design.py

$KICAD_CLI sch erc --format report --severity-all -o "$OUT/erc.rpt" kicad/calc.kicad_sch 2>/dev/null | grep -v '^Saved'
$KICAD_CLI sch export netlist -o "$OUT/calc.net" kicad/calc.kicad_sch 2>/dev/null
grep -E '^\[' "$OUT/erc.rpt" | sed 's/\].*/]/' | sort | uniq -c | sort -rn || true
$PY -I gen/check_netlist.py "$OUT/calc.net"
