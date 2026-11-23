#!/usr/bin/env python3
"""Check and extract a code module image (app/modx.h, modules/modx_image.ld).

    modlink.py --readelf arm-none-eabi-readelf --elf anim.elf --bin anim.bin [--name animation]

The image was linked at address 0 with -q (--emit-relocs), so the ELF still
lists every relocation the link resolved. A position-independent image may
only contain PC-relative ones; absolute addresses would be wrong once the
image runs from its place in the QSPI window. The single exception is the
descriptor at offset 0 (modx_desc_t: its `init` and `image_size` words hold
link addresses, which are offsets because the base is 0). This script fails
on any other absolute or GOT relocation, checks the descriptor in the raw
binary (magic, ABI, name, init inside the image with the Thumb bit) and
prints the image size.
"""
import argparse
import re
import struct
import subprocess
import sys

MAGIC = 0x584D4443
DESC = struct.Struct("<IHH16sIIII")     # modx_desc_t
ALLOWED_ABS = {28, 32}                   # offsets of desc.init and desc.image_size
ABSOLUTE = re.compile(r"R_ARM_(ABS(32|16|8)|TARGET1|(THM_)?MOV[WT]_ABS(_NC)?|GOT32|GOT_BREL|GOT_PREL|"
                      r"GOT_ABS|GLOB_DAT|JUMP_SLOT|RELATIVE)\b")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--readelf", required=True)
    ap.add_argument("--elf", required=True)
    ap.add_argument("--bin", required=True)
    ap.add_argument("--name")
    ap.add_argument("--abi", type=int, default=1)
    a = ap.parse_args()
    out = subprocess.run([a.readelf, "-rW", a.elf], capture_output=True, text=True, check=True).stdout
    bad = []
    nrel = 0
    section = ""
    for line in out.splitlines():
        m = re.match(r"Relocation section '([^']+)'", line)
        if m:
            section = m.group(1)
            continue
        f = line.split()
        if "debug" in section or len(f) < 3 or not re.fullmatch(r"[0-9a-f]{8}", f[0]):
            continue
        nrel += 1
        off, typ = int(f[0], 16), f[2]
        if ABSOLUTE.search(typ) and not (typ == "R_ARM_ABS32" and off in ALLOWED_ABS):
            bad.append(line.strip())
    if bad:
        print(f"modlink: {a.elf}: absolute relocations in a position-independent image "
              "(pointers in constant tables? non-hidden symbols?):", file=sys.stderr)
        for b in bad[:20]:
            print("  " + b, file=sys.stderr)
        sys.exit(1)
    img = open(a.bin, "rb").read()
    if len(img) < DESC.size:
        sys.exit(f"modlink: {a.bin}: too short")
    magic, abi, dsize, name, rw, init, size, apisize = DESC.unpack_from(img, 0)
    name = name.rstrip(b"\0").decode()
    problems = []
    if magic != MAGIC:
        problems.append("no descriptor at offset 0")
    if abi != a.abi:
        problems.append(f"ABI {abi}, expected {a.abi}")
    if a.name and name != a.name:
        problems.append(f"name {name!r}, expected {a.name!r}")
    if not (init & 1) or init >= size:
        problems.append(f"entry 0x{init:x} is not a Thumb address inside the image")
    if size != len(img):
        problems.append(f"image_size {size} != file size {len(img)}")
    if problems:
        sys.exit(f"modlink: {a.bin}: " + "; ".join(problems))
    print(f"modlink: {a.bin}: module {name}, {len(img)} bytes, entry 0x{init:x}, RW {rw} bytes, "
          f"API {apisize} bytes, {nrel} relocations (position-independent)")


if __name__ == "__main__":
    main()
