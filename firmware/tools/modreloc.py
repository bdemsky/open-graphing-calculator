#!/usr/bin/env python3
"""Make the payload of a code-load module (app/modx.h modx_load_desc_t,
modules/modx_load.ld) from its ELF.

    modreloc.py --elf python.elf --out python.bin [--name python] [--abi 1]

The image was linked at address 0 with -q (--emit-relocs), so the ELF still
lists every relocation the link resolved. The loader (app/modx.c load_image)
copies the image to its SDRAM code area and adds the load address to every
32-bit word that holds a link address; this script lists those words:
R_ARM_ABS32 (and R_ARM_TARGET1) against anything but an absolute symbol or an
undefined weak one. PC-relative relocations (calls, branches, literal-pool
offsets) need nothing. Any other absolute kind (MOVW/MOVT pairs, GOT entries
the loader does not fill) stops the build. The descriptor's own words (the
entry and the image size) stay offsets.

Payload: the bytes of .text and .data from address 0 (image_size), then the
relocation offsets (u32, ascending) at reloc_off; the descriptor's bss_size,
reloc_off and reloc_count are filled in here. Prints the sizes.
"""
import argparse
import struct
import sys

MAGIC = 0x584D4443
DESC_SIZE = 56          # sizeof(modx_load_desc_t)
SHT_REL, SHT_RELA, SHT_NOBITS = 9, 4, 8
SHF_ALLOC = 2
ABS = {2, 38}           # R_ARM_ABS32, R_ARM_TARGET1
PCREL = {0, 3, 10, 11, 28, 29, 30, 40, 42, 51, 52, 102, 103, 41, 50, 46, 49, 53, 54, 55, 56, 57}
# 0 NONE, 3 REL32, 10 THM_CALL, 11 THM_PC8, 28 CALL, 29 JUMP24, 30 THM_JUMP24, 40 V4BX, 41 TARGET2 (none
# expected), 42 PREL31, 46 MOVW_PREL_NC, 49 THM_MOVW_PREL_NC, 50 THM_MOVT_PREL, 51 THM_JUMP19, 52 THM_JUMP6,
# 53 THM_ALU_PREL_11_0, 54 THM_PC12, 55 ABS32_NOI (none), 56 REL32_NOI, 57 ALU_PC_G0_NC, 102 THM_JUMP11,
# 103 THM_JUMP8
NAMES = {47: "R_ARM_THM_MOVW_ABS_NC", 48: "R_ARM_THM_MOVT_ABS", 43: "R_ARM_MOVW_ABS_NC", 44: "R_ARM_MOVT_ABS",
         26: "R_ARM_GOT_BREL", 96: "R_ARM_GOT_PREL", 21: "R_ARM_GLOB_DAT", 22: "R_ARM_JUMP_SLOT", 23: "R_ARM_RELATIVE"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--name")
    ap.add_argument("--abi", type=int, default=1)
    a = ap.parse_args()
    elf = open(a.elf, "rb").read()
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        sys.exit(f"modreloc: {a.elf}: not a 32-bit little-endian ELF")
    shoff, = struct.unpack_from("<I", elf, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", elf, 0x2E)
    secs = []
    for i in range(shnum):
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from("<10I", elf, shoff + i * shentsize)
        secs.append(dict(name=name, type=typ, flags=flags, addr=addr, off=off, size=size, link=link, info=info,
                         entsize=entsize))
    strtab = secs[shstrndx]

    def sname(s):
        o = strtab["off"] + s["name"]
        return elf[o:elf.index(b"\0", o)].decode()

    by = {sname(s): s for s in secs}
    text, data, bss = by.get(".text"), by.get(".data"), by.get(".bss")
    if not text or text["addr"] != 0:
        sys.exit("modreloc: no .text at address 0")
    end = data["addr"] + data["size"] if data and data["size"] else text["addr"] + text["size"]
    img = bytearray(end)
    for s in (text, data):
        if s and s["size"] and s["type"] != SHT_NOBITS:
            img[s["addr"]:s["addr"] + s["size"]] = elf[s["off"]:s["off"] + s["size"]]
    bss_size = bss["size"] if bss else 0
    if bss and bss["size"] and bss["addr"] < end:
        sys.exit("modreloc: .bss overlaps the image")
    relocs, bad = set(), []
    for s in secs:
        if s["type"] == SHT_RELA:
            sys.exit("modreloc: RELA relocations are not expected on ARM")
        if s["type"] != SHT_REL:
            continue
        target = secs[s["info"]]
        if not target["flags"] & SHF_ALLOC or target["type"] == SHT_NOBITS:
            continue  # debug information
        symtab = secs[s["link"]]
        for k in range(s["size"] // 8):
            r_off, r_info = struct.unpack_from("<II", elf, s["off"] + 8 * k)
            typ, sym = r_info & 0xFF, r_info >> 8
            if typ in PCREL:
                continue
            if typ not in ABS:
                bad.append(f"0x{r_off:x} {NAMES.get(typ, typ)}")
                continue
            st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from(
                "<IIIBBH", elf, symtab["off"] + 16 * sym)
            if st_shndx == 0xFFF1:  # SHN_ABS: a constant address, not part of the image
                continue
            if st_shndx == 0 and (st_info >> 4) == 2 and st_value == 0:  # undefined weak: stays 0
                continue
            if st_shndx == 0:
                bad.append(f"0x{r_off:x} against an undefined symbol")
                continue
            if r_off < DESC_SIZE:
                continue  # the descriptor's entry and image size are offsets
            if r_off + 4 > end or r_off & 3:
                bad.append(f"0x{r_off:x} outside the image or unaligned")
                continue
            relocs.add(r_off)
    if bad:
        print(f"modreloc: {a.elf}: relocations the loader cannot apply:", file=sys.stderr)
        for b in bad[:20]:
            print("  " + b, file=sys.stderr)
        sys.exit(1)
    magic, abi, dsize, name, rw, init, size, apisize = struct.unpack_from("<IHH16sIIII", img, 0)
    name = name.rstrip(b"\0").decode()
    problems = []
    if magic != MAGIC:
        problems.append("no descriptor at offset 0")
    if dsize != DESC_SIZE:
        problems.append(f"descriptor size {dsize}, expected {DESC_SIZE}")
    if abi != a.abi:
        problems.append(f"ABI {abi}, expected {a.abi}")
    if a.name and name != a.name:
        problems.append(f"name {name!r}, expected {a.name!r}")
    if size != end:
        problems.append(f"image_size {size} != {end}")
    if not (init & 1) or init >= size:
        problems.append(f"entry 0x{init:x} is not a Thumb address inside the image")
    if problems:
        sys.exit(f"modreloc: {a.elf}: " + "; ".join(problems))
    rel = sorted(relocs)
    struct.pack_into("<III", img, 40, bss_size, end, len(rel))
    out = bytes(img) + struct.pack(f"<{len(rel)}I", *rel)
    with open(a.out, "wb") as f:
        f.write(out)
    print(f"modreloc: {a.out}: module {name}, code+data {end} bytes, bss {bss_size}, {len(rel)} relocations, "
          f"payload {len(out)} bytes, RW {rw} bytes, entry 0x{init:x}")


if __name__ == "__main__":
    main()
