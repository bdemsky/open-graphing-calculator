# Shared settings for the OpenOCD helper scripts (sourced, not executed).
# PROBE=cmsisdap (Raspberry Pi Debug Probe, default) | stlink
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW="$(cd "$HERE/.." && pwd)"
OPENOCD="${OPENOCD:-/opt/local/bin/openocd}"
ARM_BIN="${ARM_BIN:-/Applications/ArmGNUToolchain/15.3.rel2/arm-none-eabi/bin}"
PROBE="${PROBE:-cmsisdap}"
case "$PROBE" in
  cmsisdap) CFG="$HERE/calc-cmsisdap.cfg" ;;
  stlink)   CFG="$HERE/calc-stlink.cfg" ;;
  *) echo "PROBE must be cmsisdap or stlink" >&2; exit 2 ;;
esac
ELF="${ELF:-$FW/build/fw/calc.elf}"
ocd() { "$OPENOCD" -f "$CFG" "$@"; }
