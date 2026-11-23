#!/usr/bin/env python3
"""Render audit: the simulator's 1x screen vs the web page's 1x framebuffer, scene by scene.

    python3 docs/audit/render/tools/sim_compare.py DIR/simscripts DIR/public/fb [--sim build/sim/calcsim] [--out DIR/sim]

render_audit.js --simscripts writes each job as a simulator script (the same console lines, a
`screenshot JOB__CAPTURE.png` where the page captured). This runs them in the native simulator
(scale 1: no hr hooks) and compares each screenshot with the page's framebuffer at that point (the
module drawing at display scale S, divided down). A difference is the module drawing differently
at scale > 1, or the two builds' state drifting apart (both reported; the images are kept).
Writes OUT/sim_compare.json. Needs Pillow (the repository's .venv) -- or falls back to a pure
PNG reader.
"""
import json
import os
import subprocess
import sys
import zlib
import struct
from concurrent.futures import ThreadPoolExecutor


def read_png(path):
    """RGB rows of an 8-bit RGB / RGBA PNG (no Pillow needed)"""
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n"
    p, idat, w = 8, b"", 0
    while p < len(d):
        n, t = struct.unpack(">I4s", d[p:p + 8])
        c = d[p + 8:p + 8 + n]
        if t == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", c[:10])
            ch = {2: 3, 6: 4}[ct]
        elif t == b"IDAT":
            idat += c
        p += 12 + n
    raw = zlib.decompress(idat)
    stride = w * ch
    out, prev, i = [], bytearray(stride), 0
    for y in range(h):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - ch] if x >= ch else 0
            b = prev[x]
            cc = prev[x - ch] if x >= ch else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - cc), abs(a - cc), abs(a + b - 2 * cc)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else cc)) & 255
        out.append(bytes(line[x * ch:x * ch + 3]) for x in range(w))
        out[-1] = [bytes(line[x * ch:x * ch + 3]) for x in range(w)]
        prev = line
    return w, h, out


def diff(a, b):
    wa, ha, ra = read_png(a)
    wb, hb, rb = read_png(b)
    if (wa, ha) != (wb, hb):
        return None
    n, x0, y0, x1, y1 = 0, wa, ha, 0, 0
    for y in range(18, 224):  # (the core's status and soft-key bars differ by platform: not the module's)
        for x in range(wa):
            pa, pb = ra[y][x], rb[y][x]
            if sum(abs(pa[i] - pb[i]) for i in range(3)) > 24:
                n += 1
                x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
    return n, [x0, y0, x1 + 1, y1 + 1] if n else None


def main():
    a = sys.argv[1:]
    scripts, fbdir = a[0], a[1]
    fw = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
    sim = a[a.index("--sim") + 1] if "--sim" in a else os.path.join(fw, "build", "sim", "calcsim")
    out = a[a.index("--out") + 1] if "--out" in a else os.path.join(os.path.dirname(scripts.rstrip("/")), "sim")
    os.makedirs(out, exist_ok=True)

    def run(f):
        r = subprocess.run([sim, "--script", os.path.join(scripts, f), "--shots", out], capture_output=True, text=True,
                           errors="replace", timeout=1800)
        return f, r.returncode

    files = sorted(f for f in os.listdir(scripts) if f.endswith(".txt"))
    with ThreadPoolExecutor(8) as ex:
        for f, rc in ex.map(run, files):
            print("sim %s: %s" % (f, "ok" if rc == 0 else "exit %d" % rc), flush=True)
    res = []
    for f in sorted(os.listdir(fbdir)):
        s = os.path.join(out, f)
        if not os.path.exists(s):
            res.append({"shot": f, "missing": True})
            continue
        d = diff(s, os.path.join(fbdir, f))
        res.append({"shot": f, "diff_px": d[0] if d else -1, "box": d[1] if d else None})
    json.dump(res, open(os.path.join(out, "sim_compare.json"), "w"), indent=1)
    bad = [r for r in res if r.get("diff_px", 0) > 30]
    print("%d scenes compared, %d missing, %d differ (> 30 px)" % (len(res), sum(1 for r in res if r.get("missing")), len(bad)))
    for r in sorted(bad, key=lambda r: -r["diff_px"])[:40]:
        print("  %-60s %6d px  %s" % (r["shot"], r["diff_px"], r["box"]))


if __name__ == "__main__":
    main()
