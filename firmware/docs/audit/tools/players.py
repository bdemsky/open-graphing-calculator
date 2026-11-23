#!/usr/bin/env python3
"""Simulated new players through the placement and three sessions, engine on."""
import os, json, random, re, subprocess, sys

FW = os.environ.get("AUDIT_FW") or os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SP = os.environ.get("AUDIT_SCRATCH") or "/tmp/sparkle_audit"  # (a scratch directory: set AUDIT_SCRATCH)
SIM = os.path.join(FW, "build", "sim", "calcsim")
SRC = os.path.join(FW, "modules", "mathquest", "mq_curric.c")

src = open(SRC).read()
tab = src[src.index("const mq_skill_t mq_skill[MQ_NSKILL]"):]
tab = tab[:tab.index("};")]
SK = {m.group(1): (m.group(2), int(m.group(3))) for m in re.finditer(r'\{"([^"]+)", (G_\w+), (\d)', tab)}
FRAC = {"G_BAKERY", "G_STAR"}
MEAS = {"G_CLOCK", "G_SHOP", "G_ART", "G_FAIR"}


def p_right(profile, name, diff):
    world, grade = SK.get(name, ("?", 3))
    if profile == "facts_strong_fractions_weak":
        base = {2: .95, 3: .9, 4: .7, 5: .3}[grade]
        if world in FRAC:
            base = {2: .6, 3: .35, 4: .15, 5: .05}[grade]
    elif profile == "typical_g3":
        base = {2: .9, 3: .75, 4: .35, 5: .1}[grade]
    else:  # struggling
        base = {2: .7, 3: .45, 4: .15, 5: .05}[grade]
    return max(.02, base - .07 * diff)


class Sim:
    def __init__(s):
        s.p = subprocess.Popen([SIM, "--shots", SP + "/players"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               text=True, errors="replace", bufsize=1)
        s.p.stdout.readline()

    def cmd(s, c):
        s.p.stdin.write(c + "\n")
        s.p.stdin.flush()
        out = []
        while True:
            l = s.p.stdout.readline()
            if not l:
                raise RuntimeError("sim died at " + c)
            l = l.rstrip("\n")
            if l == "OK" or l.startswith("ERR"):
                return out, l
            out.append(l)

    def st(s, k):
        out, _ = s.cmd("state " + k)
        return out[-1].partition("=")[2] if out else ""


def run(profile, seed, sessions=3, rounds=4):
    random.seed(seed)
    s = Sim()
    for c in ["fs insert 64", "fs format", "module library extra", "module install /CALC/MODULES/MATHQ010.MOD",
              "key CLEAR", "app mathquest", f"mq seed {seed}", "mq new Kid 2", "mq scene off"]:
        s.cmd(c)
    log = []
    places = [(0, 0), (2, 0), (1, 0), (3, 0), (0, 2), (4, 0), (5, 0), (2, 3), (6, 0), (1, 3), (0, 5), (3, 2)]
    pi = 0
    for sess in range(sessions):
        s.cmd(f"mq day {sess + 1}")
        for rnd in range(rounds):
            w, l = places[pi % len(places)]
            pi += 1
            s.cmd(f"mq level {w} {l}")
            guard = 0
            last = None
            while guard < 200:
                guard += 1
                s.cmd("mq step 60")
                scene = s.st("mq_scene")
                if scene != "play":
                    break
                mom = s.st("mq_moment")
                if mom != "play":
                    s.cmd("mq next")
                    continue
                name, q, ans, mech = s.st("mq_skill"), s.st("mq_q"), s.st("mq_answer"), s.st("mq_mech")
                placing, diff, speed = s.st("mq_placing"), s.st("mq_diff"), s.st("mq_speed")
                info = s.cmd("mq info")[0]
                key = (name, q, ans)
                if key == last:  # back after help: she solves it with the friend
                    s.cmd("mq correct")
                    continue
                last = key
                pr = p_right(profile, name, int(diff or 0))
                ok = random.random() < pr
                s.cmd("mq correct" if ok else "mq wrong")
                log.append(dict(session=sess + 1, round=rnd + 1, skill=name, grade=SK.get(name, ("?", 0))[1],
                                diff=int(diff or 0), placing=placing, mech=mech, q=info[0] if info else q,
                                answer=ans, right=ok, p=round(pr, 2), speed=speed))
        s.cmd("state mq_start_grade")
    out, _ = s.cmd("mq log")
    strands = s.st("mq_strands")
    sg = s.st("mq_start_grade")
    s.p.stdin.close()
    return dict(profile=profile, seed=seed, log=log, coach=out, strands=strands, start_grade=sg)


if __name__ == "__main__":
    res = []
    for prof in ["facts_strong_fractions_weak", "typical_g3", "struggling"]:
        for seed in (1, 2):
            res.append(run(prof, seed))
            print(prof, seed, len(res[-1]["log"]), "orders", file=sys.stderr)
    json.dump(res, open(SP + "/players.json", "w"), indent=1)
