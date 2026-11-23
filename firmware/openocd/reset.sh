#!/usr/bin/env bash
# Reset the target and let it run.   openocd/reset.sh [halt]
set -euo pipefail
source "$(dirname "$0")/_common.sh"
if [ "${1:-}" = halt ]; then
  ocd -c "init; reset halt; shutdown"
else
  ocd -c "init; reset run; shutdown"
fi
