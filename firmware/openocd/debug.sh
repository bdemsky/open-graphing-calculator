#!/usr/bin/env bash
# Start an OpenOCD GDB server and attach arm-none-eabi-gdb.
#   openocd/debug.sh            attach to the running firmware (no reset)
#   openocd/debug.sh load       reset, load the ELF, stop at main
#   openocd/debug.sh server     only run the GDB server (port 3333, telnet 4444)
set -euo pipefail
source "$(dirname "$0")/_common.sh"
MODE="${1:-attach}"
if [ "$MODE" = server ]; then exec "$OPENOCD" -f "$CFG"; fi
"$OPENOCD" -f "$CFG" >/tmp/calc-openocd.log 2>&1 &
OCD_PID=$!
trap 'kill $OCD_PID 2>/dev/null || true' EXIT
sleep 1
GDB_ARGS=(-ex "target extended-remote :3333")
if [ "$MODE" = load ]; then
  GDB_ARGS+=(-ex "monitor reset halt" -ex "load" -ex "break main" -ex "continue")
fi
"$ARM_BIN/arm-none-eabi-gdb" -q "${GDB_ARGS[@]}" "$ELF"
