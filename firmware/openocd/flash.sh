#!/usr/bin/env bash
# Program the internal flash (both banks) from the ELF, verify, and reset.
#   PROBE=stlink openocd/flash.sh [path/to/calc.elf]
set -euo pipefail
source "$(dirname "$0")/_common.sh"
ELF="${1:-$ELF}"
[ -f "$ELF" ] || { echo "no $ELF (run 'make fw')" >&2; exit 1; }
ocd -c "program {$ELF} verify reset exit"
