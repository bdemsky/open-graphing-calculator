#!/usr/bin/env python3
"""Sparkle Town's vector art: the high-resolution set of the Mac page (web/dist/sparkle.html).

    gen_mq_vector.py --out modules/mathquest/art_vector     write every SVG and index.json
    gen_mq_vector.py --check modules/mathquest/art_vector   fail if the directory is not what this writes
    gen_mq_vector.py --list                                 the ids

The calculator draws Sparkle Town in 1x pixel art (tools/gen_mq_art.py, gen_mq_items.py, shapes in
the module); the Mac page draws this second set instead, at the screen's real resolution: SVG,
resolution free, in one kawaii style (mqvec/svg.py: soft gradients, round plum outlines, sparkly
eyes, blush, glossy shines). Every picture is keyed by an id the page knows:

  cast/<name>_<face>      the nine friends, nine faces each (40 x 40 design units; mqvec/cast.py)
  sticker/<kind>          the sixteen stickers (12 x 12)
  item/<key>_<frame>      every home item frame (its art pixels; mqvec/items.py)
  money/<coin|bill|purse> the coins, bills and her purse (mqvec/money.py)
  model/<piece>           the pieces of the ways' hands-on models (mqvec/models.py)
  town/..., bg/..., game/..., stage/..., pat/...   scenes and things the module draws from shapes,
                          named by the module through modx_api_t.hr (MODX_HR_SPRITE; mqvec/scenes.py)

The cast, stickers and items are found by the page through the hash of the pixel picture the
module draws (web/tools/build_sparkle_html.py maps each id to the hash of its current pixel art,
so the mapping follows the pixel art when it changes); the rest by the id the module names.
Standard library only.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from mqvec import items as VI  # noqa: E402

try:
    from mqvec import cast as VC  # noqa: E402
except ImportError:  # (while the set is being drawn)
    VC = None
try:
    from mqvec import scenes as VS  # noqa: E402
except ImportError:
    VS = None
from mqvec import money as VM  # noqa: E402
from mqvec import models as VMD  # noqa: E402


def all_art():
    """[(id, Svg or SVG text)]"""
    out = []
    if VC:
        out += VC.cast()
        out += VC.stickers()
    out += VI.items()
    if VS:
        out += VS.scenes()
    out += VM.money()
    out += VMD.models()
    return out


def render_all(meta=None):
    """{id: svg text}; meta (a dict): {id: [vb x, y, w, h, design w, h]}"""
    out = {}
    for i, s in all_art():
        out[i] = s.svg()
        if meta is not None:
            meta[i] = [round(v, 3) for v in list(s.vb) + [s.w, s.h]]
    return out


def write(d):
    meta = {}
    arts = render_all(meta)
    os.makedirs(d, exist_ok=True)
    have = set()
    for root, _, files in os.walk(d):
        for fn in files:
            if fn.endswith(".svg"):
                have.add(os.path.relpath(os.path.join(root, fn), d)[:-4])
    for i, s in arts.items():
        p = os.path.join(d, i + ".svg")
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w") as fh:
            fh.write(s + "\n")
    for stale in have - set(arts):
        os.remove(os.path.join(d, stale + ".svg"))
    index = {"version": 1, "count": len(arts), "bytes": sum(len(s) + 1 for s in arts.values()),
             "meta": {i: meta[i] for i in sorted(arts)}}
    with open(os.path.join(d, "index.json"), "w") as fh:
        json.dump(index, fh, indent=0)
        fh.write("\n")
    return arts


def check(d):
    arts = render_all()
    bad = []
    for i, s in arts.items():
        p = os.path.join(d, i + ".svg")
        if not os.path.exists(p) or open(p).read() != s + "\n":
            bad.append(i)
    if bad:
        sys.exit("%s is out of date (%d pictures, e.g. %s): run make sparkle-vector" % (d, len(bad), ", ".join(bad[:5])))
    print("%s: all %d vector pictures up to date" % (d, len(arts)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out")
    ap.add_argument("--check")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    if a.out:
        arts = write(a.out)
        print("wrote %d vector pictures to %s (%d bytes)" % (len(arts), a.out, sum(len(s) for s in arts.values())))
    if a.check:
        check(a.check)
    if a.list:
        for i, _ in all_art():
            print(i)


if __name__ == "__main__":
    main()
