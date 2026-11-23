#!/bin/bash
# Slice enclosure parts for the Neptune 4 Pro (Klipper) in Elegoo Rapid PETG with the ElegooSlicer CLI.
#
#   slice.sh case   OUT.gcode PART.stl [PART.stl ...]   shells/doors: parts side by side, 20 mm apart,
#                                                       case profile (0.5 mm first-layer lines, 5 mm outer brim)
#   slice.sh keys   OUT.gcode BASE.stl LEGEND.stl       a key plate (keys_loose_*, the word test, the legend
#                                                       strip) with its legend colour swaps: two PAUSEs
#                                                       (base colour -> legend colour -> base colour)
#
# Key plates are sliced twice with Arachne (variable-width walls: thin legend strokes) at the same bed position:
# the whole plate on one filament (A), and only the cap tops (cut five layers under the legend, lowered onto the
# bed) with the legend as a second part on a second filament (B; Arachne crashes with "negative spacing" on the
# whole plate with a separate legend part, and classic walls blur the legends). splice.py takes A below the
# first filament change and B, raised back, from it on, and drops the slicer's prime after each PAUSE. Both
# filaments retract 0.8, wipe and hop on travel (PETG strings between the keys) and both print at 250. No brim:
# on a key plate it fills every gap between the first-layer pieces.
# The filament changes are manual: PAUSE parks the head; unload, load the other colour, purge until clean,
# remove the purge and resume. PLATE_XY="x y" places the plate (default the bed centre).
set -e
here=$(cd "$(dirname "$0")" && pwd)
py=${PYTHON:-$here/../../.venv/bin/python}
slicer=${ELEGOO_SLICER:-/Applications/ElegooSlicer.app/Contents/MacOS/ElegooSlicer}
mode=$1 out=$2; shift 2
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
run() {   # run OUTDIR PROCESS FILAMENTS [args...]
    local o=$1 p=$2 f=$3; shift 3; mkdir -p "$o"
    "$slicer" --datadir "$work/datadir" --load-settings "$here/machine_n4pro.json;$here/$p" \
        --load-filaments "$f" --slice 0 --outputdir "$o" "$@" > "$o.log" 2>&1 \
        || { grep -i -m3 "error\|negative" "$o.log"; exit 1; }
}
case $mode in
case)
    "$py" - "$work/plate.stl" "$@" <<'EOF'
import sys, trimesh
ms = [trimesh.load(p) for p in sys.argv[2:]]
x = 0.0
for m in ms:
    m.apply_translation([x - m.bounds[0][0], -m.bounds[0][1], -m.bounds[0][2]]); x += m.extents[0] + 20
trimesh.util.concatenate(ms).export(sys.argv[1])
EOF
    run "$work/o" process_case.json "$here/filament_rpetg.json" --arrange 1 "$work/plate.stl"
    cp "$work/o/plate_1.gcode" "$out" ;;
keys)
    # PLATE_XY="x y": where the plate goes (default the bed centre; the slicer adds about +2.5, +3)
    zcut=$("$py" - "$work" "$1" "$2" ${PLATE_XY:-112.5 112.5} <<'PYEOF'
import sys, trimesh
w, a, b = sys.argv[1], trimesh.load(sys.argv[2]), trimesh.load(sys.argv[3])
px, py = float(sys.argv[4]), float(sys.argv[5])
c = (a.bounds[0] + a.bounds[1]) / 2
for m in (a, b): m.apply_translation([px - c[0], py - c[1], -a.bounds[0][2]])
trimesh.util.concatenate([a, b]).export(f"{w}/merged.stl")
# B: only the cap tops, from five layers under the legend, lowered onto the bed
zc = round((b.bounds[0][2] - 1.0) / 0.2) * 0.2
box = trimesh.creation.box(extents=[400, 400, 50]); box.apply_translation([px, py, zc + 25])
top = trimesh.boolean.intersection([a, box], engine="manifold")
for m in (top, b): m.apply_translation([0, 0, -zc])
top.export(f"{w}/base.stl"); b.export(f"{w}/legend.stl")
print(f"{zc:.1f}")
PYEOF
)
    echo "legend layers: cap tops from z $zcut, sliced on two filaments"
    run "$work/A" process_keys.json "$here/filament_rpetg_c1_wipe.json" --arrange 0 "$work/merged.stl" &
    run "$work/B" process_keys.json "$here/filament_rpetg_c1_wipe.json;$here/filament_rpetg_c2.json" \
        --load-filament-ids "1,2" --assemble --arrange 0 "$work/base.stl" "$work/legend.stl" &
    wait
    # splice at B's first filament change, B raised by zcut
    "$py" "$here/splice.py" "$work/A/plate_1.gcode" "$work/B/plate_1.gcode" "$out" "$zcut"
    # a brim round a key plate's first-layer islands fills every gap between them
    ! grep -q "^;TYPE:Brim" "$out" || { echo "key plate has a brim"; exit 1; }
    # layer-by-layer check against the model (fused islands, stray or missing material, legend colours,
    # pauses, primes, Z moves); pictures in $VERIFY_PNG if set
    ok=0; "$py" "$here/verify_keys.py" "$out" "$1" "$2" ${VERIFY_PNG:+"$VERIFY_PNG"} > "$work/verify.log" || ok=1
    grep -E "^(FAIL|pauses|legend layer|verify_keys)| WARN" "$work/verify.log"
    [ $ok = 0 ] || { echo "key plate failed verification: $out"; exit 1; } ;;
*) sed -n 2,10p "$0"; exit 2 ;;
esac
grep -E "^; estimated printing time|^; total filament used" "$out" || true
