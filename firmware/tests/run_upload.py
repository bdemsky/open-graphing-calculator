#!/usr/bin/env python3
"""End-to-end test of the console module upload (make upload-test).

    run_upload.py --sim build/sim/calcsim build/modules/MANUAL.MOD

Starts an empty simulator (--no-content: the QSPI holds no module, as on a
new board) and drives it exactly like tools/calcctl.py drives the hardware
over the UART:
  1. help falls back to the built-in pages;
  2. an upload cut off half-way (the link dies, then a reset) leaves nothing
     installed and the store usable;
  3. an upload with a damaged byte fails verification and is not committed;
  4. a complete upload installs the manual: help pages come from the module;
  5. it survives a reboot.
Exit status 1 on any failure.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import calcctl  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", required=True)
    ap.add_argument("module")
    a = ap.parse_args()
    link = calcctl.SimLink(a.sim, ["--no-content"])
    fails = []

    def cmd(c, want_ok=True):
        ok, lines = calcctl.command(link, c, 30.0, quiet=True)
        if ok != want_ok:
            fails.append(f"{c!r}: {'OK' if ok else 'ERR'} {' '.join(lines)}")
        return lines

    def state(key):
        lines = cmd("state " + key)
        return lines[0].split("=", 1)[1] if lines and "=" in lines[0] else ""

    def expect(key, val, what):
        got = state(key)
        if got != val:
            fails.append(f"{what}: {key}={got!r}, want {val!r}")

    data = open(a.module, "rb").read()
    expect("module_manual", "absent", "empty QSPI")
    cmd("manual graph")
    expect("help_missing", "1", "empty QSPI: help falls back")
    cmd("key CLEAR")

    # 2. half an upload, then the link dies and the calculator is reset
    cmd("module begin")
    for i in range(0, len(data) // 2, calcctl.UPLOAD_CHUNK):
        cmd("module put " + data[i:i + calcctl.UPLOAD_CHUNK].hex())
    cmd("reboot")
    expect("module_manual", "absent", "interrupted upload")

    # 3. one byte damaged in transit: verification fails, nothing is committed
    bad = bytearray(data)
    bad[len(bad) // 2] ^= 0x55
    cmd("module begin")
    for i in range(0, len(bad), calcctl.UPLOAD_CHUNK):
        cmd("module put " + bytes(bad[i:i + calcctl.UPLOAD_CHUNK]).hex())
    cmd("module end", want_ok=False)
    expect("module_manual", "absent", "damaged upload")

    # 4. the real thing, through the same function calcctl.py --upload-module uses
    ok, msg = calcctl.upload_module(link, a.module)
    if not ok:
        fails.append("upload: " + msg)
    expect("module_manual", "ok", "after the upload")
    cmd("manual graph")
    expect("help_source", "module", "page from the module")
    expect("help_missing", "0", "page from the module")
    cmd("key CLEAR")

    # 4b. again with 8 lines in flight, as calcctl.py does on the calculator's USB port
    ok, msg = calcctl.upload_module(link, a.module, window=8)
    if not ok:
        fails.append("pipelined upload: " + msg)
    expect("module_manual", "ok", "after the pipelined upload")

    # 5. still there after a reset
    cmd("reboot")
    expect("module_manual", "ok", "after reboot")
    link.write(b"quit\n")
    for f in fails:
        print("FAIL", f)
    print(f"upload-test: {'PASS' if not fails else 'FAILED'} ({os.path.basename(a.module)}, {len(data)} bytes)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
