#!/usr/bin/env python3
"""The release version (README.md "Versions and releases").

    release_version.py get [NAME]                  the release version (every NAME has it)
    release_version.py describe [--release]        the firmware's version string for this build
    release_version.py bump --release DIR --fw-bin BIN... --modules MOD...
                                                   make release: PATCH + 1 when anything changed
    release_version.py manifest --fw-bin BIN... --modules MOD...
                                                   the "version" lines of release/MANIFEST.txt
    release_version.py library DEST MOD...         the release library: exactly these module files
                                                   (an existing file of the same version and payload kept)
    release_version.py tag --release DIR           the annotated tag vX.Y.Z of a committed release
    release_version.py selftest

One version per release: versions.txt (next to the Makefile) holds `release MAJOR.MINOR.PATCH`, and
the firmware and every module built for that release carry it (the module header's version and the
version digits of its file name, tools/mkmodule.py filename). MAJOR and MINOR are raised by hand
(edit versions.txt, or `make version-bump PART=minor|major`); `make release` raises PATCH by itself
when anything changed since the last release: the firmware's images (the build's version strings
masked out) or a module's payload. The last release is release/MANIFEST.txt: one line per item,
`version NAME X.Y.Z sha256`, its version the highest there (releases before one version per release
listed modules at their own, lower versions). A version in versions.txt above the released one is a
hand bump and is released as it is. Nothing changed: nothing is bumped. Every module is stamped
with the new version in a new release, changed or not (its file's bytes change with it).

Version strings (describe): a release build is "X.Y.Z"; any other build "X.Y.Z+N-gHASH", N commits
after the tag vX.Y.Z (or after the commit that last changed versions.txt when there is no tag), and
"-modified" when the git tree has uncommitted changes.
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.dirname(HERE)
VERSIONS = os.path.join(FIRMWARE, "versions.txt")
sys.path.insert(0, HERE)
import mkmodule  # noqa: E402

ARM_BIN = os.environ.get("ARM_BIN", "/Applications/ArmGNUToolchain/15.3.rel2/arm-none-eabi/bin")
# the build's own strings (core/version.h): fixed-size arrays, masked out of the firmware's content hash
VERSION_SYMBOLS = ("fw_version", "fw_build_date", "fw_build_time")


def parse(v):
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", v.strip())
    if not m:
        raise SystemExit(f"release_version: bad version {v!r} (want MAJOR.MINOR.PATCH)")
    return tuple(int(x) for x in m.groups())


def fmt(v):
    return "%d.%d.%d" % tuple(v)


def read_versions(path=VERSIONS):
    """[(name, (x, y, z))] in file order."""
    out = []
    for line in open(path):
        line = line.split("#", 1)[0].split()
        if len(line) == 2:
            out.append((line[0], parse(line[1])))
    return out


def write_versions(vers, path=VERSIONS):
    """Rewrite the versions in place (comments and order kept)."""
    d = dict(vers)
    lines = []
    for line in open(path):
        f = line.split("#", 1)[0].split()
        if len(f) == 2 and f[0] in d:
            line = re.sub(r"\d+\.\d+\.\d+", fmt(d[f[0]]), line, count=1)
        lines.append(line)
    open(path, "w").write("".join(lines))


def git(*args):
    try:
        return subprocess.run(["git", "-C", FIRMWARE] + list(args), capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def release_version(path=VERSIONS):
    """The release version (versions.txt `release X.Y.Z`): the firmware's and every module's."""
    v = dict(read_versions(path))
    if "release" not in v:
        raise SystemExit("release_version: versions.txt has no `release X.Y.Z` line")
    return v["release"]


def describe(release, path=VERSIONS):
    base = fmt(release_version(path))
    if release:
        return base
    head = git("rev-parse", "--short", "HEAD")
    if head is None:
        return base + "+unknown"
    n = git("rev-list", "--count", f"v{base}..HEAD")
    if n is None:  # no tag of this version yet: count from the commit that set it
        since = git("log", "-1", "--format=%H", "--", path)
        n = git("rev-list", "--count", f"{since}..HEAD") if since else "0"
    dirty = git("status", "--porcelain", "--untracked-files=no")
    s = base if n == "0" and not dirty and git("tag", "--points-at", "HEAD", "--list", f"v{base}") else \
        f"{base}+{n}-g{head}"
    return s + ("-modified" if dirty else "")


# ------------------------------------------------------------------ content of a release
def symbols(elf):
    """{name: (address, size)} of the version symbols in elf."""
    out = subprocess.run([os.path.join(ARM_BIN, "arm-none-eabi-nm"), "-S", elf], capture_output=True, text=True,
                         check=True).stdout
    syms = {}
    for line in out.splitlines():
        f = line.split()
        if len(f) == 4 and f[3] in VERSION_SYMBOLS:
            syms[f[3]] = (int(f[0], 16), int(f[1], 16))
    return syms


def firmware_hash(bins):
    """sha256 over the firmware images, each with the version strings of its ELF zeroed. bins: paths of
    .bin files; the ELF is NAME.elf next to it (calc_bank2.bin -> calc.elf) with the bank's base."""
    h = hashlib.sha256()
    for b in bins:
        data = bytearray(open(b, "rb").read())
        stem = os.path.basename(b)[:-4]
        bank2 = stem.endswith("_bank2")
        elf = os.path.join(os.path.dirname(b), (stem[:-6] if bank2 else stem) + ".elf")
        base = 0x08100000 if bank2 else 0x08000000
        if os.path.exists(elf):
            for addr, size in symbols(elf).values():
                if base <= addr < base + len(data):
                    data[addr - base:addr - base + size] = bytes(size)
        h.update(os.path.basename(b).encode() + b"\0" + bytes(data))
    return h.hexdigest()


def module_info(path):
    """(name, version, payload sha256) from a module file's header."""
    i = mkmodule.parse_header(open(path, "rb").read(mkmodule.HDR))
    return i["name"], tuple(i["ver"]), i["sha256"]


def last_release(rel):
    """{name: (version, sha256)} of the last release: release/MANIFEST.txt's version lines; for a
    module not listed there, the newest version in release/MODULES (its file's payload hash)."""
    out = {}
    man = os.path.join(rel, "MANIFEST.txt")
    if os.path.exists(man):
        for line in open(man):
            f = line.split()
            if len(f) == 4 and f[0] == "version":
                out[f[1]] = (parse(f[2]), f[3])
    lib = os.path.join(rel, "MODULES")
    if os.path.isdir(lib):
        for fn in sorted(os.listdir(lib)):
            if fn.upper().endswith(".MOD"):
                try:
                    name, ver, sha = module_info(os.path.join(lib, fn))
                except ValueError:
                    continue
                if name not in out or (name not in manifest_names(man) and ver > out[name][0]):
                    out[name] = (ver, sha)
    return out


def manifest_names(man):
    if not os.path.exists(man):
        return set()
    return {l.split()[1] for l in open(man) if l.startswith("version ") and len(l.split()) == 4}


def plan(v, last, current):
    """The version of this release. v: the release version in versions.txt; last: {name: (version, sha)}
    of the last release; current: {name: sha of this build}. -> (version, why, [names that changed])."""
    changed = sorted(n for n, sha in current.items() if n not in last or last[n][1] != sha)
    if not last:
        return v, "first release", changed
    lv = max(x for x, _ in last.values())
    if v > lv:
        return v, "set by hand", changed
    if changed:
        return (lv[0], lv[1], lv[2] + 1), "changed: " + ", ".join(changed), changed
    return lv, "unchanged", changed


def current_hashes(fw_bins, mods):
    cur = {}
    if fw_bins:
        cur["firmware"] = firmware_hash(fw_bins)
    for m in mods:
        name, _, sha = module_info(m)
        cur[name] = sha
    return cur


def cmd_bump(rel, fw_bins, mods, path=VERSIONS):
    v = release_version(path)
    nv, why, _ = plan(v, last_release(rel), current_hashes(fw_bins, mods))
    print(f"release {fmt(nv)} ({why}" + (f", was {fmt(v)})" if nv != v else ")"))
    if nv != v:
        write_versions([("release", nv)], path)
        print("release_version: versions.txt updated")
    return 0


def cmd_manifest(fw_bins, mods, path=VERSIONS):
    v = release_version(path)
    for name, sha in current_hashes(fw_bins, mods).items():
        mv = v if name == "firmware" else module_info(next(m for m in mods if module_info(m)[0] == name))[1]
        if mv != v:
            raise SystemExit(f"release_version: {name} is {fmt(mv)}, not the release's {fmt(v)} (build it again)")
        print(f"version {name} {fmt(v)} {sha}")


def cmd_library(dest, mods):
    """The release library (release/MODULES) holds this release's module files and nothing else: the
    files of earlier releases are removed (a calculator keeps its own previous version of each module
    for Revert; the card needs only the current ones). A file already there with the same version and
    payload stays as it is (its bytes, and so SHA256SUMS, do not change for another build's header)."""
    os.makedirs(dest, exist_ok=True)
    keep = {os.path.basename(m).upper() for m in mods}
    for fn in os.listdir(dest):
        if fn.upper().endswith(".MOD") and fn.upper() not in keep:
            os.remove(os.path.join(dest, fn))
    for m in mods:
        d = os.path.join(dest, os.path.basename(m))
        if os.path.exists(d):
            try:
                if module_info(d)[1:] == module_info(m)[1:]:
                    continue
            except ValueError:
                pass
        shutil.copyfile(m, d)


def cmd_tag(rel):
    v = fmt(release_version())
    rv = last_release(rel).get("firmware")
    if not rv or fmt(rv[0]) != v:
        raise SystemExit(f"release_version: release/MANIFEST.txt is not release {v} (run make release first)")
    if git("status", "--porcelain", "--untracked-files=no", "--", rel, VERSIONS):
        raise SystemExit("release_version: commit the release (release/, versions.txt) before tagging it")
    if git("tag", "--list", f"v{v}"):
        raise SystemExit(f"release_version: tag v{v} exists already")
    if git("tag", "-a", f"v{v}", "-m", f"Release {v}") is None:
        raise SystemExit("release_version: git tag failed")
    print(f"tagged v{v}; publish it with the release commit: git push --follow-tags")


# ------------------------------------------------------------------ self test
def selftest():
    fails = []

    def check(c, what):
        if not c:
            fails.append(what)
    with tempfile.TemporaryDirectory() as t:
        vpath = os.path.join(t, "versions.txt")
        open(vpath, "w").write("# test\nrelease 0.2.0  # the release\n")
        rel = os.path.join(t, "release")
        os.makedirs(os.path.join(rel, "MODULES"))

        def mod(name, ver, payload, out):
            data = mkmodule.header(name, parse(ver), 0, "help", 1, 1, 1, [], "X", payload, "test") + \
                b"\xff" * (mkmodule.PAYLOAD_OFF - mkmodule.HDR) + payload
            open(out, "wb").write(data)
            return out
        m1 = mod("manual", "0.1.0", b"pages" * 100, os.path.join(rel, "MODULES", "MANUA010.MOD"))
        p1 = mod("python", "0.1.0", b"code" * 100, os.path.join(rel, "MODULES", "PYTHO010.MOD"))
        # no MANIFEST version lines yet: the library's files are the last release; the firmware is new
        last = last_release(rel)
        check(last["manual"][0] == (0, 1, 0) and "firmware" not in last, "last release from the library")
        cur = {"firmware": "aaaa", "manual": module_info(m1)[2], "python": module_info(p1)[2]}
        v, why, ch = plan((0, 1, 0), last, cur)
        check(v == (0, 1, 1) and ch == ["firmware"], "the firmware new: PATCH")
        # a release before one version per release: the firmware 0.2.0, modules at their own 0.1.x
        open(os.path.join(rel, "MANIFEST.txt"), "w").write(
            "version firmware 0.2.0 aaaa\nversion manual 0.1.1 %s\nversion python 0.1.0 %s\n"
            % (module_info(m1)[2], module_info(p1)[2]))
        last = last_release(rel)
        # nothing changed: nothing is bumped (the release is the highest version of the last one)
        v, why, _ = plan(release_version(vpath), last, cur)
        check(v == (0, 2, 0) and why == "unchanged", "unchanged: no bump")
        # a module's payload changed: the release's PATCH (every module gets it)
        m2 = mod("manual", "0.2.0", b"pages" * 101, os.path.join(t, "MANUA020.MOD"))
        v, _, ch = plan((0, 2, 0), last, dict(cur, manual=module_info(m2)[2]))
        check(v == (0, 2, 1) and ch == ["manual"], "module change: PATCH")
        # the firmware changed: PATCH
        v, _, ch = plan((0, 2, 0), last, dict(cur, firmware="bbbb"))
        check(v == (0, 2, 1) and ch == ["firmware"], "firmware change: PATCH")
        # a hand bump (MINOR) is released as it is, changed or not
        v, why, _ = plan((0, 3, 0), last, dict(cur, firmware="bbbb"))
        check(v == (0, 3, 0) and why == "set by hand", "hand bump kept")
        # versions.txt rewritten in place, comments kept
        write_versions([("release", (0, 2, 1))], vpath)
        txt = open(vpath).read()
        check("release 0.2.1  # the release" in txt and txt.startswith("# test"), "versions.txt rewritten in place")
        check(release_version(vpath) == (0, 2, 1), "the release version read back")
        # the library keeps a file whose payload is the same; a changed payload replaces it
        os.makedirs(os.path.join(t, "x"))
        m3 = os.path.join(t, "x", "MANUA010.MOD")
        payload = b"pages" * 100  # the same payload from another build: only the header differs
        open(m3, "wb").write(mkmodule.header("manual", (0, 1, 0), 0, "help", 1, 1, 1, [], "X", payload, "other")
                             + b"\xff" * (mkmodule.PAYLOAD_OFF - mkmodule.HDR) + payload)
        before = open(m1, "rb").read()
        cmd_library(os.path.join(rel, "MODULES"), [m3, p1])
        check(open(m1, "rb").read() == before, "same payload: library file kept")
        # a new release: its files only (the earlier versions' files removed)
        p2 = mod("python", "0.2.1", b"code" * 100, os.path.join(t, "PYTHO021.MOD"))
        m4 = mod("manual", "0.2.1", b"pages" * 100, os.path.join(t, "MANUA021.MOD"))
        cmd_library(os.path.join(rel, "MODULES"), [m4, p2])
        check(sorted(os.listdir(os.path.join(rel, "MODULES"))) == ["MANUA021.MOD", "PYTHO021.MOD"],
              "the library holds the release's files only")
        check(open(os.path.join(rel, "MODULES", "MANUA021.MOD"), "rb").read() == open(m4, "rb").read(),
              "the new version copied")
        # the firmware's content hash ignores nothing but the version symbols (no ELF next to it: whole file)
        b1 = os.path.join(t, "calc.bin")
        open(b1, "wb").write(b"\1\2\3")
        h1 = firmware_hash([b1])
        open(b1, "wb").write(b"\1\2\4")
        check(firmware_hash([b1]) != h1, "firmware hash follows the image")
    # version strings
    check(describe(True) == fmt(release_version()), "release string is X.Y.Z")
    d = describe(False)
    check(re.fullmatch(r"\d+\.\d+\.\d+(\+\d+-g[0-9a-f]+)?(-modified)?|\d+\.\d+\.\d+\+unknown", d) is not None,
          f"development string {d!r}")
    for f in fails:
        print("release_version selftest FAILED:", f)
    print(f"release_version selftest: {'ok' if not fails else 'FAILED'}")
    return 1 if fails else 0


def main():
    a = sys.argv[1:]
    if not a:
        raise SystemExit(__doc__)

    def opt(name, many=False):
        if name not in a:
            return [] if many else None
        i = a.index(name) + 1
        if not many:
            return a[i]
        out = []
        while i < len(a) and not a[i].startswith("--"):
            out.append(a[i])
            i += 1
        return out
    if a[0] == "get":  # (any NAME: the firmware and every module have the release's version)
        print(fmt(release_version()))
    elif a[0] == "describe":
        print(describe("--release" in a))
    elif a[0] == "bump":
        return cmd_bump(opt("--release"), opt("--fw-bin", True), opt("--modules", True))
    elif a[0] == "manifest":
        cmd_manifest(opt("--fw-bin", True), opt("--modules", True))
    elif a[0] == "library":
        cmd_library(a[1], a[2:])
    elif a[0] == "tag":
        cmd_tag(opt("--release"))
    elif a[0] == "set":  # set PART: a hand bump of the release (make version-bump)
        x, y, z = release_version()
        v = {"major": (x + 1, 0, 0), "minor": (x, y + 1, 0), "patch": (x, y, z + 1)}[a[-1]]
        write_versions([("release", v)])
        print(f"release {fmt(v)}")
    elif a[0] == "selftest":
        return selftest()
    else:
        raise SystemExit(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main())
