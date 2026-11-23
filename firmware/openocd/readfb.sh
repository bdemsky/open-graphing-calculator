#!/usr/bin/env bash
# Read the framebuffer (320x240 RGB565 at 0xD0000000) over SWD without
# stopping the firmware, and convert it to PNG.
#   openocd/readfb.sh [out.png]
# The framebuffer's MPU region is write-through, so SDRAM always holds the
# current pixels even though SWD reads bypass the D-cache.
set -euo pipefail
source "$(dirname "$0")/_common.sh"
OUT="${1:-fb.png}"
RAW="$(mktemp -t calcfb).bin"
ocd -c "init; dump_image {$RAW} 0xD0000000 153600; shutdown"
python3 "$FW/tools/fb2png.py" "$RAW" "$OUT"
rm -f "$RAW"
