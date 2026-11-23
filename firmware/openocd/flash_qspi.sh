#!/usr/bin/env bash
# Program a binary into the W25Q128 QSPI flash (memory-mapped at 0x90000000).
#   openocd/flash_qspi.sh [file.bin] [offset]
# Default: build/modules/calc_qspi.bin, the QSPI module store image of the build (make modules:
# every content module, committed and activated; docs/QSPI_CONTENT.md) at offset 0. This replaces
# the module store only (0x000000..); the session log at 0xE00000 is not touched.
set -euo pipefail
source "$(dirname "$0")/_common.sh"
BIN="${1:-$FW/build/modules/calc_qspi.bin}"
OFF="${2:-0}"
[ -f "$BIN" ] || { echo "no $BIN" >&2; exit 1; }
if [ ! -s "$BIN" ]; then echo "$BIN is empty: no content modules in this configuration"; exit 0; fi
ADDR=$(printf '0x%08x' $((0x90000000 + OFF)))
ocd -c "init; reset init; flash probe 2; flash write_bank 2 {$BIN} $OFF; flash verify_bank 2 {$BIN} $OFF; reset run; shutdown"
echo "programmed $BIN at $ADDR"
