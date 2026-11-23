#!/usr/bin/env python3
"""Run Python test programs in the simulator's Python module and compare their output.

    run_python.py --sim build/sim/calcsim [--list] FILE.py ...

Each FILE.py runs as a program (console `py reset`, `py add LINE` per line,
`py run`) in one simulator session, in a fresh interpreter. Its output (the
out= lines) must equal FILE.py.exp. Two kinds of tests use this:

  tests/python/basics/   a subset of MicroPython's own tests/basics (v1.29.0),
                         the ones that run in the calculator's configuration;
                         the .exp files are CPython's output (or MicroPython's
                         own .exp where upstream has one)
  tests/python/*.py      tests of the calc library (Expr arithmetic,
                         conversions, errors, the CAS functions)

Lines longer than the console allows are refused (the test is reported).
Exit status 1 if any test fails. --list prints the per-test result; --update writes
the .exp files from the output (for new tests: read them before committing).
"""
import argparse
import os
import subprocess
import sys

LINE_MAX = 380  # the console's line buffer (core/console.c LINE_MAX 384) minus the command


def quote(line):
    return '"' + line.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", required=True)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--update", action="store_true", help="write FILE.py.exp from the output (check it!)")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    cmds, runs, bad = [], [], []
    for f in a.files:
        src = open(f, encoding="utf-8").read().splitlines()
        lines = [f"py add {quote(l)}" for l in src]
        if any(len(l) > LINE_MAX for l in lines):
            bad.append((f, "a line is too long for the console"))
            continue
        cmds += ["py reset bare", "py clear"] + lines
        runs.append((f, len(cmds)))
        cmds.append("py run")
    script = "\n".join(cmds) + "\n"
    p = subprocess.run([a.sim], input=script.encode(), capture_output=True, timeout=600)
    out = p.stdout.decode(errors="replace").split("\n")  # (not splitlines: programs print \v and \f)
    blocks, cur = [], []
    for line in out:
        if line.startswith("# "):
            continue
        if line == "OK" or line.startswith("ERR"):
            blocks.append((cur, line))
            cur = []
        else:
            cur.append(line)
    if len(blocks) != len(cmds):
        print(f"run_python: {len(blocks)} responses for {len(cmds)} commands (exit {p.returncode})")
        print(p.stderr.decode(errors="replace")[-2000:])
        return 1
    fails = list(bad)
    for f, k in runs:
        body, term = blocks[k]
        # out=TEXT is a line, out+=TEXT a piece without its newline (long lines, the last piece)
        got = "".join(l[4:] + "\n" if l.startswith("out=") else l[5:] for l in body if l.startswith(("out=", "out+=")))
        err = [l[6:] for l in body if l.startswith("error=")]
        if a.update and term == "OK" and not err:
            with open(f + ".exp", "w", encoding="utf-8") as w:
                w.write(got)
        want = open(f + ".exp", encoding="utf-8").read()
        ok = term == "OK" and got == want
        if not ok:
            fails.append((f, f"error {err[0]}" if err else "output differs"))
            if len(fails) <= 5:
                print(f"--- {f}: got\n{got}--- expected\n{want}---")
        if a.list:
            print(("ok   " if ok else "FAIL ") + f)
    if b"runtime error" in p.stderr or b"ERROR: AddressSanitizer" in p.stderr:
        print(p.stderr.decode(errors="replace")[-3000:])
        fails.append(("(simulator)", "sanitizer report"))
    for f, why in fails:
        print(f"FAIL {f}: {why}")
    print(f"python tests: {len(a.files) - len(fails)} of {len(a.files)} passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
