#!/usr/bin/env python3
"""Quick look at a render-audit results.jsonl: one line a capture (or --check NAME: the findings of one kind).

    python3 docs/audit/render/tools/summary.py DIR/public/results.jsonl [--check tint] [--job twist_]
"""
import json
import sys


def main():
    a = sys.argv[1:]
    path = a[0]
    check = a[a.index("--check") + 1] if "--check" in a else None
    job = a[a.index("--job") + 1] if "--job" in a else ""
    for line in open(path):
        r = json.loads(line)
        if job not in r.get("job", ""):
            continue
        if "error" in r:
            print(r["job"], "ERROR", r["error"][:200])
            continue
        st = r.get("state") or {}
        if check:
            for f in r["findings"]:
                if f["check"] == check:
                    print(r["job"], r["capture"], f["sev"], f.get("el"), "|", f["what"][:160])
            continue
        cnt = {}
        for f in r["findings"]:
            cnt[f["check"] + ":" + f["sev"]] = cnt.get(f["check"] + ":" + f["sev"], 0) + 1
        print(r["job"], r["capture"], st.get("mq_mech", ""), st.get("mq_q", "")[:40], "desync", r["stats"].get("desync"),
              r["stats"].get("desyncWhere") or "", cnt, "anim-frozen" if r.get("anim", {}).get("frozen") else "")


if __name__ == "__main__":
    main()
