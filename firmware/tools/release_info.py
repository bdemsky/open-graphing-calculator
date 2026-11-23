#!/usr/bin/env python3
"""Describe and sanity-check the release images (run by `make release`).

For each of calc / bringup: checks that the .bin is the flash-bank-1 image
starting at 0x08000000 (initial SP inside DTCM, reset vector = Reset_Handler
with the Thumb bit, both identical in .elf, .bin and .hex), that NAME_bank2.bin
is the flash-bank-2 image at 0x08100000 (section .flash2: CAS library, help
manual) when the ELF has one, that the .hex covers exactly the bytes of both
.bin files, that neither bank overflows, and that nothing else loadable lies
outside the two banks except the optional QSPI image. Prints a manifest with
the bank usage; exits non-zero on any mismatch.

    tools/release_info.py RELEASE_DIR CAS_IMPL

The manifest goes to stdout (make writes it to release/MANIFEST.txt). Why
these checks: the .bin files are flashed at fixed addresses by tools that
know nothing about the ELF (STM32CubeProgrammer, DFU), so a .bin that does
not start at the bank base, or whose first two words are not a valid
vector table (initial MSP at the top of DTCM, Reset_Handler | 1 for Thumb),
would not boot. Bank 2 is a separate .bin because the address gap between
the banks (0x08080000..0x080FFFFF, no flash on the 1 MB STM32H743ZG) would
otherwise be padded into a single image.
"""
import os
import struct
import subprocess
import sys
import time

ARM_BIN = os.environ.get("ARM_BIN", "/Applications/ArmGNUToolchain/15.3.rel2/arm-none-eabi/bin")
FLASH1 = (0x08000000, 0x08080000)  # [start, end) of flash bank 1, 512 KB
FLASH2 = (0x08100000, 0x08180000)  # bank 2, 512 KB
DTCM = (0x20000000, 0x20020000)    # 128 KB DTCM: holds the main stack (linker script)


def readelf(args, path):
    """stdout of arm-none-eabi-readelf ARGS PATH."""
    return subprocess.run([os.path.join(ARM_BIN, "arm-none-eabi-readelf")] + args + [path],
                          capture_output=True, text=True, check=True).stdout


def symbol(elf, name):
    """Value of symbol `name` from the ELF symbol table, or None."""
    for line in readelf(["-s", "-W"], elf).splitlines():
        f = line.split()
        if len(f) >= 8 and f[-1] == name:
            return int(f[1], 16)
    return None


def load_segments(elf):
    """[(load address, size)] of the non-empty LOAD segments: what an image
    must contain. Load (physical) addresses are used so .data initialisers
    count at their flash location."""
    segs = []
    for line in readelf(["-l", "-W"], elf).splitlines():
        f = line.split()
        if f and f[0] == "LOAD":
            # Type Offset VirtAddr PhysAddr FileSiz MemSiz Flg Align
            paddr, filesz = int(f[3], 16), int(f[4], 16)
            if filesz:
                segs.append((paddr, filesz))
    return segs


def parse_hex(path):
    """Read an Intel HEX file: ({address: byte}, start address or None).

    Handles record types 00 (data), 04 (extended linear address: upper 16
    address bits) and 05 (start linear address); every record's checksum
    (all bytes sum to 0 mod 256) is verified.
    """
    data, base, start = {}, 0, None
    for line in open(path):
        line = line.strip()
        if not line.startswith(":"):
            continue
        b = bytes.fromhex(line[1:])
        n, addr, typ = b[0], (b[1] << 8) | b[2], b[3]
        if sum(b) & 0xFF:
            raise SystemExit(f"{path}: bad checksum in {line}")
        if typ == 0:
            for i in range(n):
                data[base + addr + i] = b[4 + i]
        elif typ == 4:
            base = ((b[4] << 8) | b[5]) << 16
        elif typ == 5:
            start = struct.unpack(">I", b[4:8])[0]
    return data, start


def check(rel, name, errors):
    """Check the NAME.{elf,bin,hex} (+ NAME_bank2.bin) set in `rel`.

    Appends problems to `errors` and returns the manifest lines; returns
    nothing if NAME.elf does not exist.
    """
    elf, binf, hexf = (os.path.join(rel, f"{name}.{e}") for e in ("elf", "bin", "hex"))
    bin2f = os.path.join(rel, f"{name}_bank2.bin")
    out = []
    if not os.path.exists(elf):
        return out
    binb = open(binf, "rb").read()
    sp, reset = struct.unpack("<II", binb[:8])  # vector table words 0 and 1
    rh = symbol(elf, "Reset_Handler")
    segs = load_segments(elf)
    flash_segs = [s for s in segs if FLASH1[0] <= s[0] < FLASH1[1]]
    bank2_segs = [s for s in segs if FLASH2[0] <= s[0] < FLASH2[1]]
    other = [s for s in segs if not (FLASH1[0] <= s[0] < FLASH1[1]) and not (FLASH2[0] <= s[0] < FLASH2[1])]
    if not flash_segs:
        errors.append(f"{name}: ELF has no flash bank 1 data")
        return out
    lo = min(s[0] for s in flash_segs)
    hi = max(s[0] + s[1] for s in flash_segs)
    if lo != 0x08000000:
        errors.append(f"{name}: first flash byte at {lo:#x}, not 0x08000000")
    if hi - lo != len(binb):
        errors.append(f"{name}: .bin is {len(binb)} bytes, ELF bank-1 span is {hi - lo}")
    if hi > FLASH1[1]:
        errors.append(f"{name}: flash bank 1 overflow ({hi - FLASH1[0]} bytes)")
    if not (DTCM[0] < sp <= DTCM[1]):
        errors.append(f"{name}: initial SP {sp:#x} not in DTCM")
    if rh is None or reset != (rh | 1):
        errors.append(f"{name}: reset vector {reset:#x} != Reset_Handler {rh!r}|1")
    for paddr, size in other:
        if not (0x90000000 <= paddr < 0x91000000):  # memory-mapped QSPI NOR (16 MB)
            errors.append(f"{name}: loadable data at {paddr:#x} (+{size}) outside flash/QSPI")
    bin2 = b""
    if bank2_segs:
        lo2 = min(s[0] for s in bank2_segs)
        hi2 = max(s[0] + s[1] for s in bank2_segs)
        if lo2 != FLASH2[0]:
            errors.append(f"{name}: bank 2 starts at {lo2:#x}, not {FLASH2[0]:#x}")
        if hi2 > FLASH2[1]:
            errors.append(f"{name}: flash bank 2 overflow ({hi2 - FLASH2[0]} bytes)")
        if not os.path.exists(bin2f):
            errors.append(f"{name}: ELF has bank-2 data but {os.path.basename(bin2f)} is missing")
        else:
            bin2 = open(bin2f, "rb").read()
            if len(bin2) != hi2 - lo2:
                errors.append(f"{name}: {os.path.basename(bin2f)} is {len(bin2)} bytes, ELF bank-2 span is "
                              f"{hi2 - lo2}")
    elif os.path.exists(bin2f) and os.path.getsize(bin2f):
        errors.append(f"{name}: {os.path.basename(bin2f)} exists but the ELF has no bank-2 data")
    # The .hex holds both banks; it must span exactly the .bin files and agree
    # with them byte for byte (it may omit padding the .bin contains)
    hdata, hstart = parse_hex(hexf)
    h1 = sorted(a for a in hdata if a < FLASH2[0])
    h2 = sorted(a for a in hdata if a >= FLASH2[0])
    if not h1:
        errors.append(f"{name}: .hex has no bank-1 data")
        return out
    if h1[0] != 0x08000000 or h1[-1] != 0x08000000 + len(binb) - 1:
        errors.append(f"{name}: .hex bank 1 spans {h1[0]:#x}..{h1[-1]:#x}")
    mism = sum(1 for a in h1 if binb[a - 0x08000000] != hdata[a])
    if bin2:
        if not h2 or h2[0] != FLASH2[0] or h2[-1] != FLASH2[0] + len(bin2) - 1:
            errors.append(f"{name}: .hex bank 2 does not match {os.path.basename(bin2f)}")
        mism += sum(1 for a in h2 if a - FLASH2[0] < len(bin2) and bin2[a - FLASH2[0]] != hdata[a])
    elif h2:
        errors.append(f"{name}: .hex has bank-2 data but there is no bank-2 image")
    if mism:
        errors.append(f"{name}: .hex and .bin differ in {mism} bytes")
    gaps = (h1[-1] - h1[0] + 1) - len(h1)  # bytes the .bin fills with 0 padding
    out.append(f"{name}.bin  {len(binb):7d} bytes  load 0x08000000-{0x08000000 + len(binb) - 1:#010x} "
               f"({100.0 * len(binb) / (512 * 1024):.1f} % of flash bank 1)")
    if bin2:
        out.append(f"{name}_bank2.bin {len(bin2):7d} bytes  load {FLASH2[0]:#010x}-{FLASH2[0] + len(bin2) - 1:#010x} "
                   f"({100.0 * len(bin2) / (512 * 1024):.1f} % of flash bank 2: CAS library, help manual)")
    else:
        out.append(f"{name}: nothing in flash bank 2")
    out.append(f"    vectors: SP={sp:#010x} Reset_Handler={reset:#010x}  hex start record "
               f"{'none' if hstart is None else hex(hstart)}  hex/bin gap bytes={gaps}")
    if other:
        out.append("    extra loadable segments: " + ", ".join(f"{p:#x}+{s}" for p, s in other))
    return out


def main():
    """Print the manifest for calc and bringup; 1 if any check failed."""
    rel, cas = sys.argv[1], sys.argv[2]
    errors = []
    print(f"Calculator firmware release ({time.strftime('%Y-%m-%d %H:%M')}, CAS={cas})")
    try:
        rev = subprocess.run(["git", "-C", rel, "rev-parse", "--short", "HEAD"], capture_output=True,
                             text=True).stdout.strip()
    except OSError:
        rev = ""
    print(f"source revision: {rev or 'unknown'} (plus uncommitted changes, if any)")
    for name in ("calc", "calc_core", "bringup"):
        if name == "calc_core" and not os.path.exists(os.path.join(rel, name + ".elf")):
            continue
        if name == "calc_core":
            print("calc_core: core-tier rollback firmware (everything in internal flash, no QSPI content)")
        for line in check(rel, name, errors):
            print(line)
    q = os.path.join(rel, "calc_qspi.bin")
    print(f"calc_qspi.bin: {os.path.getsize(q)} bytes, QSPI module store image for 0x90000000 (OpenOCD) "
          f"(docs/QSPI_CONTENT.md)" if os.path.exists(q)
          else "calc_qspi.bin: none (no content modules in this configuration)")
    for e in errors:
        print("ERROR:", e)
    print("checks:", "PASS" if not errors else f"{len(errors)} FAILED")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
