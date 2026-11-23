#!/usr/bin/env python3
"""Audit driver: every skill x difficulty x seed, frames of the stage, play and a dud."""
import json, os, subprocess, sys, re
from concurrent.futures import ThreadPoolExecutor

FW = os.environ.get("AUDIT_FW") or os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SP = os.environ.get("AUDIT_SCRATCH") or "/tmp/sparkle_audit"  # (a scratch directory: set AUDIT_SCRATCH)
SIM = os.path.join(FW, "build", "sim", "calcsim")
SRC = os.path.join(FW, "modules", "mathquest", "mq_curric.c")
OUT = SP + "/shots"
SCR = SP + "/scripts"
os.makedirs(OUT, exist_ok=True)
os.makedirs(SCR, exist_ok=True)

HEAD = """requirecfg CONTENT_QSPI
fs insert 64
fs format
module library extra
module install /CALC/MODULES/MATHQ010.MOD
key CLEAR
app mathquest
"""
MECHS = ["Bubble Pop", "Cloud Catch", "Gate Dash", "Kart Race", "Peekaboo", "Sort Slide", "Hop Along", "Sky Fling",
         "Pie Shop", "Build It", "Clock Tower", "Shop Till", "Treasure Dig", "Treasure Heist", "Cake Kitchen", "Pet Shop"]

KEYS = ["mq_skill", "mq_q", "mq_answer", "mq_mech", "mq_moment", "mq_diff"]


def probe():
    lines = ["mq info"] + [f"state {k}" for k in KEYS]
    return "\n".join(lines) + "\n"


_RUNGS = {}


def rungs(s):
    """the skill's ladder length (`mq rung S`; pass 5 audited difficulties 0-3, the ladders have 2-8 rungs)"""
    if not _RUNGS:
        p = os.path.join(SP, "rungs.txt")
        open(p, "w").write(HEAD + "mq new Mia 2\n" + "".join(f"mq rung {k}\n" for k in range(86)))
        out = subprocess.run([SIM, "--script", p], capture_output=True, text=True, errors="replace").stdout
        k, n = 0, 0
        for l in out.splitlines():
            if re.match(r"^[ *] \d+: ", l):
                n += 1
            elif l == "OK" and n:
                _RUNGS[k], k, n = n, k + 1, 0
    return _RUNGS.get(s, 4)


def skill_script(s, seeds=(1, 2, 3)):
    t = HEAD + "mq new Mia 2\nmq engine stop\nmq grant bosses\n"
    for d in range(rungs(s)):
        for sd in seeds:
            tag = f"s{s:02d}_d{d}_r{sd}"
            # a fresh player each time so the stage plays at full length (first time) and the demo shows
            t += f"mq new Mia{sd} 2\nmq engine stop\nmq seed {sd}\nmq skill {s} {d}\n"
            t += f"mq step 1\nscreenshot {tag}_a_open.png\n"
            t += f"mq step 22\nscreenshot {tag}_b_stage.png\n"
            t += f"mq step 60\n" + probe() + f"screenshot {tag}_c_play.png\n"
            t += f"mq try\nmq step 10\nstate mq_moment\nscreenshot {tag}_d_dud.png\n"
            t += f"mq try\nmq step 30\nstate mq_moment\nscreenshot {tag}_e_dud2.png\n"
    return t


def mech_script(s):
    t = HEAD + "mq new Mia 2\nmq engine stop\nmq grant bosses\nmq scene off\n"
    for m in MECHS:
        tag = f"m_s{s:02d}_{m.replace(' ', '')}"
        t += f"mq seed 5\nmq skill {s} 1\nmq mech \"{m}\"\nmq step 40\n" + probe() + f"screenshot {tag}.png\n"
    return t


def run(name, text):
    p = os.path.join(SCR, name + ".txt")
    open(p, "w").write(text)
    r = subprocess.run([SIM, "--script", p, "--shots", OUT], capture_output=True, text=True, errors="replace",
                       timeout=900)
    open(os.path.join(SCR, name + ".out"), "w").write(r.stdout + "\n---stderr---\n" + r.stderr)
    return name, r.returncode


if __name__ == "__main__":
    which = sys.argv[1]
    jobs = []
    if which in ("skills", "all"):
        jobs += [(f"sk{s:02d}", skill_script(s)) for s in range(86)]
    if which in ("mechs", "all"):
        jobs += [(f"me{s:02d}", mech_script(s)) for s in range(86)]
    if which.startswith("one"):
        s = int(which[3:])
        jobs = [(f"sk{s:02d}", skill_script(s))]
    with ThreadPoolExecutor(8) as ex:
        for name, rc in ex.map(lambda j: run(*j), jobs):
            if rc:
                print(name, "rc", rc)
    print("done")
