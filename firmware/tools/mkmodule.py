#!/usr/bin/env python3
"""Package content modules and QSPI store images (format: core/modstore.h,
design: docs/QSPI_CONTENT.md).

    mkmodule.py pack  --payload FILE --name manual --version 0.1.0 --iface help --abi 1[-1]
                      --tier standard --kconfig MANUAL_FULL [--dep NAME:MIN-MAX ...]
                      [--built-with "0.1.0 abc1234"] --out MANUAL.MOD
    mkmodule.py store [--out calc_qspi.bin] [--c-out content_img.c] [--max BYTES] MOD... [--library MOD...] [--extra MOD...]
    mkmodule.py placeholder --name NAME --abi N [--load] --out FILE   (code module stand-in without a toolchain)
    mkmodule.py index --out INDEX.TXT MOD...
    mkmodule.py filename NAME VERSION          (prints the release file name, e.g. MANUA010.MOD)
    mkmodule.py check FILE...

pack:  a module file: the 512-byte header (CRC-protected), padding to 4 KB
       with the state words erased (0xFF: the installer writes commit and
       activation itself), then the payload. This is what goes to
       /CALC/MODULES on a microSD card or through `calcctl.py --upload-module`.
store: a whole QSPI store image (offset 0 of the QSPI) with the given
       modules one after the other, committed and activated: for OpenOCD
       (`flash write_image erase calc_qspi.bin 0x90000000`) and (--c-out,
       a C array content_image[]) for the simulators' factory content. With no
       module: an empty image (the CONTENT=internal build). --library adds the
       given module files to the C file as content_library[] (the release's
       microSD library, written to a simulated card by the console
       `module library`), so tests can install modules the factory content
       lacks.
placeholder: a code module payload that is only a descriptor (app/modx.h,
       entry 0): what the build packs when the ARM toolchain is missing. The
       simulators run the linked host code and only read the descriptor (--load:
       a code-load module's longer descriptor, modx_load_desc_t); the
       hardware refuses it (the entry is not inside the image).
index: one line per module: name version file size sha256 tier kconfig,
       sorted by name and version; refuses two files with the same name and
       version, or two names whose file-name stems collide.
filename: the release file name of a module version, in FAT 8.3 form so the
       firmware's FAT layer (8.3 names) and any card reader see the same name,
       and so that versions of a module sit side by side in /CALC/MODULES:
           STEM + M + m + p + ".MOD"
       STEM = the module name in upper case, letters and digits only, cut to
       5 characters; M, m, p = major, minor, patch as one base-36 digit each
       (0-9, A-Z = 10-35). manual 0.1.0 -> MANUA010.MOD, manual 1.12.3 ->
       MANUA1C3.MOD. A version field above 35 is refused. The firmware never
       relies on the name (it reads the headers), so a renamed file still
       installs; the name is for people and for keeping versions apart.
check: print the header of module files or store images; exit 1 if invalid.
"""
import argparse
import hashlib
import os
import struct
import sys
import zlib

MAGIC = 0x444F4D43       # "CMOD"
FMT = 1
HDR = 512
ACT_OFF, NACT = 0xF00, 60
COMMIT_OFF, REMOVE_OFF, COMMIT = 0xFF0, 0xFF4, 0x54494D43
PAYLOAD_OFF = 0x1000
MAX_DEPS = 8
REGION = 0xA00000        # QSPI store bytes (the notebook store and the session log above)
TIERS = {"core": 0, "standard": 1, "advanced": 2, "data": 3}
KINDS = {"data": 0, "code-xip": 1, "code-load": 2}


B36 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"


def stem(name):
    st = "".join(c for c in name.upper() if c.isalnum() and c.isascii())[:5]
    if not st:
        raise SystemExit(f"mkmodule: module name {name!r} has no letters or digits for a file name")
    return st


def filename(name, ver):
    """The 8.3 release file name of module `name` version `ver` (list of 3 ints)."""
    if any(v >= len(B36) for v in ver):
        raise SystemExit(f"mkmodule: version {'.'.join(map(str, ver))} of {name}: a field above 35 does not "
                         "fit the 8.3 file name (STEM + one base-36 digit per field)")
    return stem(name) + "".join(B36[v] for v in ver) + ".MOD"


def crc32(b):
    """CRC-32 as core/rle.c crc32_update(0, ...)."""
    return zlib.crc32(b) & 0xFFFFFFFF


def parse_ver(s):
    v = [int(x) for x in s.split(".")]
    if len(v) != 3 or any(not 0 <= x < 65536 for x in v):
        raise SystemExit(f"mkmodule: bad version {s!r} (want MAJOR.MINOR.PATCH)")
    return v


def fixed(s, n):
    b = s.encode()
    if len(b) > n:
        raise SystemExit(f"mkmodule: {s!r} longer than {n} bytes")
    return b.ljust(n, b"\0")


def header(name, ver, kind, iface, abi_min, abi_max, tier, deps, kconfig, payload, built_with):
    """The 512-byte module header (layout = mod_hdr_t in core/modstore.h)."""
    h = struct.pack("<IHH", MAGIC, FMT, HDR) + fixed(name, 16) + struct.pack("<3HH", *ver, kind)
    h += fixed(iface, 8) + struct.pack("<HHBBH", abi_min, abi_max, tier, len(deps), 0) + fixed(kconfig, 24)
    h += struct.pack("<II", len(payload), crc32(payload)) + hashlib.sha256(payload).digest()
    for dn, dmin, dmax in deps:
        h += fixed(dn, 16) + struct.pack("<3H3H", *dmin, *dmax)
    h += b"\0" * 28 * (MAX_DEPS - len(deps))
    h += fixed(built_with[:24], 24)
    h += b"\0" * (HDR - 4 - len(h))
    h += struct.pack("<I", crc32(h))
    assert len(h) == HDR
    return h


def parse_header(b):
    """dict of a module header, or raise ValueError."""
    if len(b) < HDR:
        raise ValueError("too short for a module header")
    magic, fmt, hb = struct.unpack_from("<IHH", b, 0)
    if magic != MAGIC or fmt != FMT or hb != HDR:
        raise ValueError("not a module (magic/format)")
    if struct.unpack_from("<I", b, HDR - 4)[0] != crc32(b[:HDR - 4]):
        raise ValueError("header CRC mismatch")
    name = b[8:24].rstrip(b"\0").decode()
    ver = struct.unpack_from("<3H", b, 24)
    kind = struct.unpack_from("<H", b, 30)[0]
    iface = b[32:40].rstrip(b"\0").decode()
    abi_min, abi_max, tier, ndeps, _ = struct.unpack_from("<HHBBH", b, 40)
    kconfig = b[48:72].rstrip(b"\0").decode()
    plen, pcrc = struct.unpack_from("<II", b, 72)
    sha = b[80:112].hex()
    deps = []
    for k in range(ndeps):
        o = 112 + 28 * k
        deps.append((b[o:o + 16].rstrip(b"\0").decode(), struct.unpack_from("<3H", b, o + 16),
                     struct.unpack_from("<3H", b, o + 22)))
    built = b[112 + 28 * MAX_DEPS:112 + 28 * MAX_DEPS + 24].rstrip(b"\0").decode()
    return dict(name=name, ver=ver, kind=kind, iface=iface, abi=(abi_min, abi_max), tier=tier, deps=deps,
                kconfig=kconfig, payload_len=plen, payload_crc=pcrc, sha256=sha, built_with=built)


def pack(a):
    payload = open(a.payload, "rb").read()
    lo, _, hi = a.abi.partition("-")
    deps = []
    for d in a.dep:
        dn, _, rng = d.partition(":")
        dmin, _, dmax = rng.partition("-")
        deps.append((dn, parse_ver(dmin), parse_ver(dmax or "65535.65535.65535")))
    if len(deps) > MAX_DEPS:
        raise SystemExit("mkmodule: too many dependencies")
    h = header(a.name, parse_ver(a.version), KINDS[a.kind], a.iface, int(lo), int(hi or lo), TIERS[a.tier], deps,
               a.kconfig, payload, a.built_with)
    data = h + b"\xff" * (PAYLOAD_OFF - HDR) + payload
    write(a.out, data)
    info = parse_header(data)
    print(f"module: {a.out}: {info['name']} {'.'.join(map(str, info['ver']))} ({a.tier}, {a.iface} ABI "
          f"{info['abi'][0]}-{info['abi'][1]}), payload {len(payload)} B, file {len(data)} B")


def store_image(paths, max_bytes):
    """Modules back to back from offset 0, committed, activated 1, 2, ..."""
    img = bytearray()
    for seq, p in enumerate(paths, 1):
        m = bytearray(open(p, "rb").read())
        info = parse_header(bytes(m))
        if len(m) != PAYLOAD_OFF + info["payload_len"]:
            raise SystemExit(f"mkmodule: {p}: size does not match the header")
        m[COMMIT_OFF:COMMIT_OFF + 4] = struct.pack("<I", COMMIT)
        m[ACT_OFF:ACT_OFF + 4] = struct.pack("<I", seq)
        img += m
        img += b"\xff" * ((-len(img)) % 4096)
    if len(img) > max_bytes:
        raise SystemExit(f"mkmodule: store image of {len(img)} bytes exceeds {max_bytes}")
    return bytes(img)


def c_bytes(name, data):
    lines = [f"const uint8_t {name}[{max(len(data), 1)}] = {{"]
    for i in range(0, len(data), 24):
        lines.append("    " + ",".join(str(x) for x in data[i:i + 24]) + ",")
    if not data:
        lines.append("    0,")
    return lines + ["};"]


def c_array(img, library=(), extra=()):
    lines = ["/* Generated by tools/mkmodule.py: the QSPI module store of this build, preloaded by",
             " * the simulators as factory content (platform/hostdev), and the release's module",
             " * library (console `module library`). Not linked into the firmware. */",
             "#include <stddef.h>", "#include <stdint.h>", '#include "hostdev.h"']
    nlib = len(library)
    library = list(library) + list(extra)  # extras: written by `module library extra` only
    lines += c_bytes("content_image", img)
    lines += [f"const uint32_t content_image_size = {len(img)};"]
    for k, p in enumerate(library):
        lines += ["static " + l if j == 0 else l for j, l in enumerate(c_bytes(f"lib{k}", open(p, "rb").read()))]
    lines += ["const struct content_file content_library[] = {"]
    for k, p in enumerate(library):
        lines.append(f'    {{"{os.path.basename(p)}", lib{k}, {os.path.getsize(p)}}},')
    if not library:
        lines.append("    {NULL, NULL, 0},")
    lines += ["};", f"const int content_library_n = {nlib};", f"const int content_library_all = {len(library)};", ""]
    return "\n".join(lines)


def placeholder(a):
    """descriptor-only code module payload (modx_desc_t of app/modx.h)"""
    if a.load:  # code-load (modx_load_desc_t): an empty image, no relocations
        d = struct.pack("<IHH16sIIII4I", 0x584D4443, a.abi, 56, fixed(a.name, 16), 0, 1, 56, 0, 0, 56, 0, 0)
    else:
        d = struct.pack("<IHH16sIIII", 0x584D4443, a.abi, 40, fixed(a.name, 16), 0, 0, 40, 0)
    write(a.out, d)


def write(path, data, mode="wb"):
    os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
    with open(path + ".tmp", mode) as f:
        f.write(data)
    os.replace(path + ".tmp", path)


def check(paths):
    status = 0
    for p in paths:
        b = open(p, "rb").read()
        try:
            off, n = 0, 0
            while off + PAYLOAD_OFF <= len(b):
                if struct.unpack_from("<I", b, off)[0] != MAGIC:
                    off += 4096
                    continue
                i = parse_header(b[off:off + HDR])
                payload = b[off + PAYLOAD_OFF:off + PAYLOAD_OFF + i["payload_len"]]
                ok = crc32(payload) == i["payload_crc"] and len(payload) == i["payload_len"]
                commit = struct.unpack_from("<I", b, off + COMMIT_OFF)[0]
                print(f"{p}@0x{off:06x}: {i['name']} {'.'.join(map(str, i['ver']))} iface {i['iface']} "
                      f"ABI {i['abi'][0]}-{i['abi'][1]} tier {i['tier']} kconfig {i['kconfig']} "
                      f"payload {i['payload_len']} B crc {'ok' if ok else 'BAD'} "
                      f"{'committed' if commit == COMMIT else 'uncommitted'} built with {i['built_with']!r}")
                if not ok:
                    status = 1
                n += 1
                off += (PAYLOAD_OFF + i["payload_len"] + 4095) // 4096 * 4096
            if not n:
                raise ValueError("no module found")
        except (ValueError, struct.error) as e:
            print(f"{p}: INVALID: {e}")
            status = 1
    return status


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack")
    p.add_argument("--payload", required=True)
    p.add_argument("--name", required=True)
    p.add_argument("--version", required=True)
    p.add_argument("--iface", required=True)
    p.add_argument("--abi", required=True)
    p.add_argument("--tier", choices=sorted(TIERS), default="standard")
    p.add_argument("--kind", choices=sorted(KINDS), default="data")
    p.add_argument("--kconfig", default="")
    p.add_argument("--dep", action="append", default=[])
    p.add_argument("--built-with", default="")
    p.add_argument("--out", required=True)
    s = sub.add_parser("store")
    s.add_argument("--out")
    s.add_argument("--c-out")
    s.add_argument("--max", type=int, default=REGION)
    s.add_argument("mods", nargs="*")
    s.add_argument("--library", nargs="*", default=[])
    s.add_argument("--extra", nargs="*", default=[])  # modules outside the release (console `module library extra`)
    ph = sub.add_parser("placeholder")
    ph.add_argument("--name", required=True)
    ph.add_argument("--abi", type=int, default=1)
    ph.add_argument("--load", action="store_true", help="a code-load module's descriptor")
    ph.add_argument("--out", required=True)
    x = sub.add_parser("index")
    x.add_argument("--out", required=True)
    x.add_argument("mods", nargs="+")
    f = sub.add_parser("filename")
    f.add_argument("name")
    f.add_argument("version")
    c = sub.add_parser("check")
    c.add_argument("files", nargs="+")
    a = ap.parse_args()
    if a.cmd == "pack":
        pack(a)
    elif a.cmd == "placeholder":
        placeholder(a)
    elif a.cmd == "store":
        img = store_image(a.mods, a.max)
        if a.out:
            write(a.out, img)
            if img:
                print(f"store: {a.out}: {len(img)} bytes ({len(a.mods)} modules, "
                      f"{100.0 * len(img) / REGION:.1f} % of the QSPI store)")
        if a.c_out:
            write(a.c_out, c_array(img, a.library, a.extra), "w")
    elif a.cmd == "filename":
        print(filename(a.name, parse_ver(a.version)))
    elif a.cmd == "index":
        rows, seen, stems = [], {}, {}
        for m in a.mods:
            b = open(m, "rb").read()
            i = parse_header(b)
            key = (i["name"], tuple(i["ver"]))
            if key in seen:
                raise SystemExit(f"mkmodule: {m} and {seen[key]} are both {i['name']} {'.'.join(map(str, i['ver']))}")
            seen[key] = m
            st = stem(i["name"])
            if stems.setdefault(st, i["name"]) != i["name"]:
                raise SystemExit(f"mkmodule: modules {stems[st]!r} and {i['name']!r} share the file-name stem {st}")
            rows.append((i["name"], i["ver"], f"{i['name']} {'.'.join(map(str, i['ver']))} {os.path.basename(m)} "
                         f"{len(b)} {hashlib.sha256(b).hexdigest()} {i['tier']} {i['kconfig']}"))
        rows.sort(key=lambda r: (r[0], r[1]))
        lines = ["# name version file bytes sha256(file) tier kconfig"] + [r[2] for r in rows]
        write(a.out, "\n".join(lines) + "\n", "w")
    else:
        return check(a.files)
    return 0


if __name__ == "__main__":
    sys.exit(main())
