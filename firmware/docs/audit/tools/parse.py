#!/usr/bin/env python3
"""Pair every command of the audit scripts with its response; build records."""
import json, os, re, glob

FW = os.environ.get("AUDIT_FW") or os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SP = os.environ.get("AUDIT_SCRATCH") or "/tmp/sparkle_audit"  # (a scratch directory: set AUDIT_SCRATCH)
SIM = os.path.join(FW, "build", "sim", "calcsim")
SRC = os.path.join(FW, "modules", "mathquest", "mq_curric.c")


def pairs(name):
    cmds = [l.rstrip("\n") for l in open(f"{SP}/scripts/{name}.txt") if l.strip() and not l.startswith("#")]
    out = open(f"{SP}/scripts/{name}.out").read().split("\n---stderr---\n")[0].splitlines()
    res, cur, i = [], [], 0
    for l in out:
        if l == "OK" or l.startswith("ERR"):
            res.append((cmds[i] if i < len(cmds) else "?", cur, l))
            cur, i = [], i + 1
        else:
            cur.append(l)
    return res


recs = []
for f in sorted(glob.glob(f"{SP}/scripts/*.txt")):
    name = os.path.basename(f)[:-4]
    r = None
    for cmd, body, term in pairs(name):
        if cmd.startswith("mq skill"):
            _, _, s, d = cmd.split()
            r = {"script": name, "skill_id": int(s), "diff": int(d), "errs": []}
        if cmd.startswith("mq seed") and r is not None:
            r["seed"] = int(cmd.split()[2])
        if cmd.startswith("mq mech"):
            r["mech_req"] = cmd.split(" ", 2)[2].strip('"')
            r["fits"] = term == "OK"
        if cmd == "mq info" and body:
            r["info"] = body[-1]
        if cmd.startswith("state ") and body:
            k, _, v = body[-1].partition("=")
            if k == "mq_moment" and "moment" in r:
                r.setdefault("after", []).append(v)
            else:
                r[k] = v
            if k == "mq_moment":
                r["moment"] = v
        if term.startswith("ERR") and r is not None:
            r["errs"].append(f"{cmd} -> {term}")
        if cmd.startswith("screenshot") and r is not None:
            r.setdefault("shots", []).append(cmd.split()[1])
            if name.startswith("me"):
                recs.append(r)
                r = dict(r, shots=[], errs=[])
        if cmd.startswith("state mq_moment") and len(r.get("after", [])) == 2 and name.startswith("sk"):
            recs.append(r)
            r = None
json.dump(recs, open(f"{SP}/records.json", "w"), indent=1)
print(len(recs))
