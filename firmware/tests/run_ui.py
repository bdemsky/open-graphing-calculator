#!/usr/bin/env python3
"""Run scripted UI tests against the simulator.

Each tests/ui/*.txt file is a list of debug-console commands (see README.md,
"Console protocol"). A test fails if any command answers "ERR ..." or the
simulator exits non-zero (failed `expect`). Screenshots written by
`screenshot NAME.png` land in --shots.

A script may name the module versions of this build (versions.txt, which `make release` bumps):
{ver:NAME} is NAME's version ("0.1.0"), {mod:NAME} its release file name ("MANUA010.MOD"); such a
script runs from a substituted copy in a temporary directory.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import mkmodule  # noqa: E402
import release_version  # noqa: E402


def substitute(path, tmp):
    """path, or a copy of it with {ver:NAME} / {mod:NAME} replaced."""
    text = open(path).read()
    if "{ver:" not in text and "{mod:" not in text:
        return path
    v = release_version.release_version()  # (every module has the release's version)

    def rep(m):
        return release_version.fmt(v) if m.group(1) == "ver" else mkmodule.filename(m.group(2), list(v))
    out = os.path.join(tmp, os.path.basename(path))
    open(out, "w").write(re.sub(r"\{(ver|mod):(\w+)\}", rep, text))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", required=True)
    ap.add_argument("--shots", default="sim/screenshots")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("scripts", nargs="+")
    a = ap.parse_args()
    os.makedirs(a.shots, exist_ok=True)
    failed = 0
    tmp = tempfile.TemporaryDirectory(prefix="run_ui-")
    for path in a.scripts:
        cmds = [l.rstrip("\n") for l in open(path)]
        p = subprocess.run([a.sim, "--script", substitute(path, tmp.name), "--shots", a.shots],
                           capture_output=True, text=True, errors="replace", timeout=120)
        out = p.stdout.splitlines()
        if p.returncode == 0 and any(l.startswith("SKIP") for l in out):  # requirecfg: not for this build
            print(f"[SKIP] {os.path.basename(path)} ({next(l for l in out if l.startswith('SKIP'))[6:]})")
            continue
        errs = [l for l in out if l.startswith("ERR")]
        # map each response terminator back to its command for readable failures
        real = [c for c in cmds if c.strip() and not c.strip().startswith("#")]
        terms = [l for l in out if l == "OK" or l.startswith("ERR")]
        ok = p.returncode == 0 and not errs and len(terms) == len(real)
        status = "PASS" if ok else "FAIL"
        print(f"[{status}] {os.path.basename(path)} ({len(real)} commands)")
        if not ok or a.verbose:
            for c, t in zip(real, terms):
                if t != "OK" or a.verbose:
                    print(f"    {c!r} -> {t}")
            if len(terms) != len(real):
                print(f"    expected {len(real)} responses, got {len(terms)}")
            if p.returncode:
                print(f"    exit status {p.returncode}")
            if p.stderr.strip():
                print("    stderr:", p.stderr.strip()[:2000])
        failed += not ok
    print(f"{len(a.scripts) - failed}/{len(a.scripts)} UI tests passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
