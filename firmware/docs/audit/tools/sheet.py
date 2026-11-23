#!/usr/bin/env python3
import os, sys
from PIL import Image, ImageDraw
FW = os.environ.get("AUDIT_FW") or os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SP = os.environ.get("AUDIT_SCRATCH") or "/tmp/sparkle_audit"  # (a scratch directory: set AUDIT_SCRATCH)
SIM = os.path.join(FW, "build", "sim", "calcsim")
SRC = os.path.join(FW, "modules", "mathquest", "mq_curric.c")
SH = SP + "/shots"
OUT = SP + "/sheets"
os.makedirs(OUT, exist_ok=True)


def sheet(name, cells, cols=3):
    rows = (len(cells) + cols - 1) // cols
    im = Image.new("RGB", (cols * 324, rows * 256), "white")
    d = ImageDraw.Draw(im)
    for i, (lab, f) in enumerate(cells):
        x, y = (i % cols) * 324, (i // cols) * 256
        p = os.path.join(SH, f)
        if os.path.exists(p):
            im.paste(Image.open(p).convert("RGB"), (x + 2, y + 14))
        d.text((x + 4, y + 1), lab, fill="black")
    im.save(os.path.join(OUT, name))


for s in range(86):
    c = [("d0r1 stage", f"s{s:02d}_d0_r1_b_stage.png"), ("d0r1 play", f"s{s:02d}_d0_r1_c_play.png"),
         ("d0r1 1 miss", f"s{s:02d}_d0_r1_d_dud.png"), ("d0r1 2 misses", f"s{s:02d}_d0_r1_e_dud2.png"),
         ("d1r1 play", f"s{s:02d}_d1_r1_c_play.png"), ("d2r2 play", f"s{s:02d}_d2_r2_c_play.png"),
         ("d3r3 play", f"s{s:02d}_d3_r3_c_play.png"), ("d0r2 play", f"s{s:02d}_d0_r2_c_play.png"),
         ("d0r3 play", f"s{s:02d}_d0_r3_c_play.png")]
    sheet(f"sk{s:02d}.png", [(f"skill {s} {l}", f) for l, f in c])

for d, frame in ((0, "c_play"), (3, "c_play"), (0, "d_dud")):
    for k in range(0, 86, 12):
        c = [(f"s{s} d{d} {frame}", f"s{s:02d}_d{d}_r1_{frame}.png") for s in range(k, min(k + 12, 86))]
        sheet(f"ov_d{d}_{frame}_{k:02d}.png", c, cols=4)
