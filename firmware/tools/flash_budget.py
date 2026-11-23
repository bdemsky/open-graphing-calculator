#!/usr/bin/env python3
"""Report internal-flash usage per bank for the firmware ELF; fail on overflow.

    flash_budget.py calc.elf [--warn PCT] [--modules calc.map [--top N]]

--modules adds a table of the flash bytes per module (app/<file>,
core/<file>, cas/src/<file>, platform/<file>, libc/libm/libgcc, help data,
fonts) and per group, split by bank, parsed from the linker map
(`make flash-report`).

Bank 1 (0x08000000, 512 KB): vectors, code, rodata, and the load images of
.data/.itcm/.dtcm_data. Bank 2 (0x08100000, 512 KB): section .flash2 (CAS
library, help manual). Exit status 1 if either bank overflows.

Run by `make fw` after linking. Usage is measured from the ELF program
headers (readelf -l): for each bank, the end of the highest LOAD segment
whose physical (load) address falls in the bank, minus the bank base. Using
load addresses counts initialised data stored in flash and copied to RAM by
startup code. The linker normally fails first on overflow; this report
mainly shows the remaining headroom (--warn PCT, default 90).
"""
import os
import re
import subprocess
import sys

ARM_BIN = os.environ.get("ARM_BIN", "/Applications/ArmGNUToolchain/15.3.rel2/arm-none-eabi/bin")
BANKS = [("bank1", 0x08000000, 512 * 1024), ("bank2", 0x08100000, 512 * 1024)]


def segments(elf):
    """[(physical address, file size)] of the ELF's non-empty LOAD segments
    (columns 3 and 4 of `readelf -l -W`: PhysAddr, FileSiz)."""
    out = subprocess.run([os.path.join(ARM_BIN, "arm-none-eabi-readelf"), "-l", "-W", elf],
                         capture_output=True, text=True, check=True).stdout
    segs = []
    for line in out.splitlines():
        f = line.split()
        if f and f[0] == "LOAD":
            paddr, filesz = int(f[3], 16), int(f[4], 16)
            if filesz:
                segs.append((paddr, filesz))
    return segs


def usage(elf):
    """[(bank name, base, size, bytes used)]; used = highest segment end -
    base, so gaps between segments count as used."""
    segs = segments(elf)
    res = []
    for name, base, size in BANKS:
        inside = [(a, n) for a, n in segs if base <= a < base + size]
        end = max((a + n for a, n in inside), default=base)
        res.append((name, base, size, end - base))
    return res


def module_name(path):
    """Report row for an input file of the map: app/x, core/x, cas/src/x,
    platform/x, fonts, help data, or the library name (libc, libm, libgcc)."""
    m = re.search(r"/(lib\w+?)(?:_nano)?\.a\(", path)
    if m:
        return m.group(1)
    if re.search(r"/crt\w*\.o$", path):
        return "crt"
    p = re.sub(r"^.*?build[^/]*/fw/", "", path)
    p = re.sub(r"\.o$", "", p)
    if p == "core/gen/fonts_gen":
        return "fonts"
    if p == "build/gen/help_gen":
        return "help data"
    return re.sub(r"^platform/stm32h743/", "platform/", p)


def module_group(mod):
    """Coarse group of a module row: app, core, cas, platform, fonts, help, libraries."""
    if mod in ("fonts", "help data"):
        return mod
    if mod.startswith("lib") or mod == "crt":
        return "libc/libm/libgcc"
    return mod.split("/")[0]


def modules(mapfile, elf):
    """{module: [bank1 bytes, bank2 bytes]} from the linker map: every input
    section of an output section that occupies flash (not NOBITS), assigned
    to the bank of its output section's load address. Alignment fill is the
    row "(fill)", string literals (merged across objects) the row "(merged strings)"."""
    out = subprocess.run([os.path.join(ARM_BIN, "arm-none-eabi-readelf"), "-S", "-W", elf],
                         capture_output=True, text=True, check=True).stdout
    nobits = set(re.findall(r"\]\s+(\S+)\s+NOBITS", out))
    lines = open(mapfile, encoding="utf-8", errors="replace").read().split("\n")
    start = next(i for i, l in enumerate(lines) if l.startswith("Linker script and memory map"))
    hexn = r"0x([0-9a-fA-F]+)"
    rows, bank, pending = {}, None, None
    for i in range(start, len(lines)):
        line = lines[i]
        m = re.match(r"^(\.\S+)(?:\s+%s\s+%s(?:\s+load address %s)?)?\s*$" % (hexn, hexn, hexn), line)
        if m:                                       # output section header
            name, vma = m.group(1), m.group(2)
            if vma is None and i + 1 < len(lines):  # address on the next line
                m2 = re.match(r"^\s+%s\s+%s(?:\s+load address %s)?" % (hexn, hexn, hexn), lines[i + 1])
                vma, lma = (m2.group(1), m2.group(3)) if m2 else (None, None)
            else:
                lma = m.group(4)
            addr = int(lma or vma or "0", 16)
            bank = None
            if name not in nobits:
                for k, (_, base, size) in enumerate(BANKS):
                    if base <= addr < base + size:
                        bank = k
            pending = None
            continue
        if bank is None:
            continue
        m = re.match(r"^ (\S+)?\s+%s\s+%s\s+(\S.*)?$" % (hexn, hexn), line)
        if m and (m.group(1) or pending):
            size = int(m.group(3), 16)
            sec = m.group(1) or pending
            if sec == "*fill*":
                mod = "(fill)"
            elif ".str1." in sec:
                # string literals: the linker merges them across objects into
                # the first object's section, so they cannot be split per module
                mod = "(merged strings)"
            else:
                mod = module_name(m.group(4)) if m.group(4) else None
            if mod and size:
                rows.setdefault(mod, [0, 0])[bank] += size
            pending = None
        elif re.match(r"^ \S+$", line) and not line.startswith(" *"):
            pending = line.strip()                  # long section name, numbers follow
    return rows


def print_modules(rows, top):
    """Per-module table (largest first, `top` rows) and the per-group totals."""
    def table(title, items):
        print(f"{title:<28} {'bank1':>8} {'bank2':>8} {'total':>8}")
        for k, (a, b) in items:
            print(f"{k:<28} {a:8d} {b:8d} {a + b:8d}")
    items = sorted(rows.items(), key=lambda kv: -sum(kv[1]))
    table("module", items[:top] if top else items)
    groups = {}
    for k, (a, b) in rows.items():
        g = groups.setdefault(module_group(k), [0, 0])
        g[0] += a
        g[1] += b
    print()
    table("group", sorted(groups.items(), key=lambda kv: -sum(kv[1])))
    print(f"{'total':<28} {sum(a for a, _ in rows.values()):8d} {sum(b for _, b in rows.values()):8d}")


QSPI_STORE = 0xA00000                             # QSPI module store bytes (notebooks, session log above)

# What to turn off or move when a bank is over budget (Kconfig options, make menuconfig).
HINTS = {
    "bank1": "bank 1 holds the app: move app modules to bank 2 (linker script .flash2 patterns) "
             "or turn off features in make menuconfig",
    "bank2": "bank 2 holds the CAS and the manual: select CONFIG_CONTENT_QSPI (the manual, ~110 KB, "
             "becomes a QSPI module) or CONFIG_MANUAL_ESSENTIALS (make menuconfig), or move CAS code "
             "into an extension module (CONFIG_CAS_EXT)",
}


def arg(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def main():
    """Print one line per bank; return 1 if any bank overflows or is above
    --budget PCT. With --modules MAP [--top N] also print the flash bytes per
    module/group; with --store FILE the QSPI module store image of the build."""
    elf = sys.argv[1]
    warn = float(arg("--warn", 90.0))
    budget = float(arg("--budget", 100.0))
    if "--modules" in sys.argv:
        top = int(sys.argv[sys.argv.index("--top") + 1]) if "--top" in sys.argv else 0
        print_modules(modules(sys.argv[sys.argv.index("--modules") + 1], elf), top)
        print()
    bad = 0
    msgs = []
    for name, base, size, used in usage(elf):
        pct = 100.0 * used / size
        over = pct > budget
        flag = ("OVERFLOW" if used > size else f"OVER BUDGET ({budget:.0f} %)" if over else
                "WARNING: above %.0f %%" % warn if pct > warn else "ok")
        print(f"flash {name} @ {base:#010x}: {used:7d} / {size} bytes ({pct:5.1f} %)  {flag}")
        if used > size or over:
            bad += 1
            msgs.append(f"flash_budget: {name} is {pct:.1f} % full (budget {budget:.0f} %, "
                        f"CONFIG_FLASH_BUDGET_PCT): {HINTS.get(name, '')}")
    store = arg("--store")
    if store:
        n = os.path.getsize(store) if os.path.exists(store) else 0
        if n:
            print(f"qspi module store @ 0x90000000: {n:7d} / {QSPI_STORE} bytes ({100.0 * n / QSPI_STORE:5.1f} %)  "
                  f"{store}")
        else:
            print("qspi module store: empty (this build carries its content in internal flash)")
    for m in msgs:
        print(m, file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
