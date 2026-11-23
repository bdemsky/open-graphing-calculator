#!/usr/bin/env python3
"""Render audit: group the findings of results.jsonl files (render_audit.js) across scenes.

    python3 docs/audit/render/tools/aggregate.py DIR/public/results.jsonl [DIR/private/results.jsonl ...]
            [--json out.json] [--top 25] [--check NAME]

A group is (check, element): an element is a sprite / picture id, or a text with its digits
generalised (prices and counts vary by scene). Each group lists how many captures show it, the
worst severity, the jobs it appears in and an example image (the worst capture). Animation
(frozen on the Mac while 1x moves) and the simulator comparison (sim_compare.py) come as their
own checks.
"""
import json
import re
import sys
from collections import defaultdict

SEV = {"high": 3, "med": 2, "low": 1}


def norm(el):
    if not el:
        return "-"
    el = re.sub(r"@-?\d+,-?\d+", "", el)  # a shape's position
    keep = re.findall(r"#[0-9a-f]{6}\b", el)
    el = re.sub(r"#[0-9a-f]{8}", "#hash", el)
    el = re.sub(r"#[0-9a-f]{6}\b", "#C", el)
    el = re.sub(r"\d+", "N", el)
    for k in keep:  # (colours stay)
        el = el.replace("#C", k, 1)
    return el[:90]


def durations(rows):
    """occlusions in the timelines (captures NAME_fNNN of the occl_ jobs): how long each pair lasts"""
    seq = defaultdict(dict)  # (label, job, base) -> {frame: set(pairs)}
    for r in rows:
        m = re.match(r"(.*)_f(\d+)$", r.get("capture", ""))
        if not m or "findings" not in r:
            continue
        pairs = {(f["check"], f.get("el"), f.get("by")) for f in r["findings"] if f["check"].startswith("occlusion") or f["check"] == "text-hidden-mac"}
        seq[(r.get("label"), r["job"], m.group(1))][int(m.group(2))] = (pairs, r["shot"])
    out = []
    for key, frames in seq.items():
        fs = sorted(frames)
        allp = set().union(*[frames[f][0] for f in fs])
        for p in allp:
            on = [f for f in fs if p in frames[f][0]]
            out.append({"label": key[0], "job": key[1], "moment": key[2], "check": p[0], "element": p[1], "by": p[2],
                        "frames": on, "from_ms": on[0] * 40, "to_ms": on[-1] * 40, "persistent": on[-1] == fs[-1],
                        "shot": frames[on[0]][1]})
    return out


def load(paths):
    for p in paths:
        for line in open(p):
            r = json.loads(line)
            yield r
        yield from overlays(p)


def overlays(results_path):
    """the page's own overlays (overlays.json next to results.jsonl): the corner menu button over the play
    area at a window size (the render audit's M10), and sprite / picture id collisions, as captures"""
    import os
    f = os.path.join(os.path.dirname(results_path), "overlays.json")
    if not os.path.exists(f):
        return
    d = json.load(open(f))
    label = os.path.basename(os.path.dirname(results_path))
    for w in d.get("windows", []):
        fl = []
        if w.get("menuOverPlay"):
            fl.append({"check": "menu-over-play", "sev": "med", "el": "menu button %dx%d" % tuple(w["size"][:2]),
                       "what": "the corner menu button sits on the play area"})
        yield {"label": label, "job": "overlays", "capture": "window_%dx%d" % tuple(w["size"][:2]), "shot": w.get("shot", ""),
               "findings": fl}
    col = d.get("collisions") or {}
    fl = [{"check": "id-collision", "sev": "high", "el": str(x), "what": "one id or hash names two things"}
          for k in ("ids", "hashes", "atlas") for x in col.get(k, [])]
    yield {"label": label, "job": "overlays", "capture": "collisions", "shot": "", "findings": fl}


def main():
    a = sys.argv[1:]
    opt = {}
    for k in ("--json", "--top", "--check"):
        if k in a:
            i = a.index(k)
            opt[k] = a[i + 1]
            del a[i:i + 2]
    top = int(opt.get("--top", 25))
    groups = defaultdict(lambda: {"n": 0, "sev": 0, "jobs": set(), "labels": set(), "ex": None, "exsev": -1, "what": "", "metric": []})
    caps = errors = 0
    allrows = list(load(a))
    for r in allrows:
        if "error" in r:
            errors += 1
            continue
        caps += 1
        fl = list(r["findings"])
        for fz in (r.get("anim") or {}).get("frozen", []):
            fl.append({"check": "anim-frozen", "sev": "med" if fz["n"] >= 40 else "low", "el": fz.get("el"), "box": fz["box"],  # (under 40 px: a glyph's edge)
                       "what": "moves in 1x, still on the Mac (%d px)" % fz["n"]})
        seen = set()
        for f in fl:
            key = (f["check"], norm(f.get("el")) + (" BY " + norm(f["by"]) if f.get("by") else ""))
            if key in seen:
                continue
            seen.add(key)
            g = groups[key]
            g["n"] += 1
            s = SEV.get(f["sev"], 0)
            g["sev"] = max(g["sev"], s)
            g["jobs"].add(r["job"])
            g["labels"].add(r.get("label", "?"))
            m = f.get("bad") or f.get("dL") or f.get("n") or 0
            g["metric"].append(abs(m) if isinstance(m, (int, float)) else 0)
            if s > g["exsev"] or (s == g["exsev"] and (abs(m) if isinstance(m, (int, float)) else 0) > max(g["metric"][:-1] or [0])):
                g["exsev"], g["ex"], g["what"] = s, "%s/%s (%s)" % (r.get("label"), r["shot"], r["capture"]), f["what"]
    rows = sorted(groups.items(), key=lambda kv: (-kv[1]["sev"], -kv[1]["n"]))
    if "--check" in opt:
        rows = [kv for kv in rows if kv[0][0] == opt["--check"]]
    print("%d captures, %d errors, %d groups" % (caps, errors, len(rows)))
    bycheck = defaultdict(int)
    for (c, _), g in rows:
        bycheck[c] += 1
    print("groups by check:", dict(bycheck))
    shown = defaultdict(int)
    for (c, el), g in rows:
        if shown[c] >= top:
            continue
        shown[c] += 1
        inv = {v: k for k, v in SEV.items()}
        jobs = sorted(g["jobs"])
        print("%-5s %-18s n=%-4d %-60s | %s | jobs %s%s | ex %s" % (inv.get(g["sev"], "?"), c, g["n"], el[:60], g["what"][:110],
                                                                 ",".join(jobs[:4]), "..+%d" % (len(jobs) - 4) if len(jobs) > 4 else "",
                                                                 g["ex"]))
    du = durations(allrows)
    if "--durations" in a or True:
        print("\noccluders in the timelines (enter / act, then 0..8 s of real frames):")
        for d in sorted(du, key=lambda d: (d["label"], d["job"], d["moment"])):
            if d["check"] == "occlusion-particle":
                continue
            print("  %-8s %-12s %-10s %-24s %s BY %s: %d-%d ms%s" % (d["label"], d["job"], d["moment"], d["check"], (d["element"] or "")[:40],
                                                               (d["by"] or "")[:44], d["from_ms"], d["to_ms"], " (still there at 8 s)" if d["persistent"] else ""))
    if "--json" in opt:
        out = [{"check": c, "element": el, "count": g["n"], "severity": {3: "high", 2: "med", 1: "low"}.get(g["sev"], "low"),
                "labels": sorted(g["labels"]), "jobs": sorted(g["jobs"]), "example": g["ex"], "what": g["what"]} for (c, el), g in rows]
        json.dump({"groups": out, "occlusion_timelines": du}, open(opt["--json"], "w"), indent=1)


if __name__ == "__main__":
    main()
