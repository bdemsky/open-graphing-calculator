#!/usr/bin/env python3
"""Assemble the standalone Sparkle Town page (web/dist/sparkle.html, `make sparkle-web`).

Inlines the -DWEB_SPARKLE Emscripten module (SINGLE_FILE), web/page/calc_api.js,
web/page/sparkle_audio.js, web/page/sparkle_page.js and the high-resolution art
(web/assets/sparkle_art.webp as a data URI, its index sparkle_art.json) into
web/page/sparkle.html.in: one offline HTML file. Standard library only.

The page has a marker <!--SPARKLE_EXTRA--> before its scripts where a local build may add
window.SPARKLE_CONFIG (files for the virtual card, a theme pack to choose, the localStorage key)
and window.SPARKLE_EXTRA_ART (more pictures: [{json, uri}]).

  build_sparkle_html.py --module build/web-sparkle/sparkle_module.js --out web/dist/sparkle.html
"""
import argparse
import base64
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.dirname(HERE)
FW = os.path.dirname(WEB)


def vector_id(name):
    """the vector art id of a built-in picture (tools/gen_mq_art_hires.py names): cast:Dot/idle/40 ->
    cast/dot_idle, item:apron/0 -> item/apron_0, sticker:heart -> sticker/heart"""
    kind, rest = name.split(":", 1)
    if kind == "cast":
        who, face, _ = rest.split("/")
        return "cast/%s_%s" % (who.lower(), face)
    if kind == "item":
        key, fr = rest.split("/")
        return "item/%s_%s" % (key, fr)
    return "sticker/" + rest


def vector_pack(d):
    """{svg: {id: text}, meta: {id: [...]}, hash: {hex: id}} of the vector art in d; the hashes are
    those of the pixel pictures the module draws now (the cast, stickers, items), so the mapping
    follows the pixel art"""
    import sys
    sys.path.insert(0, os.path.join(FW, "tools"))
    import gen_mq_art_hires
    index = json.loads(read(os.path.join(d, "index.json")))
    meta = index["meta"]
    svg = {i: read(os.path.join(d, i + ".svg")).strip() for i in meta}
    hashes = {}
    for name, hv, _, _, _, _ in gen_mq_art_hires.pictures(hires=False):
        vid = vector_id(name)
        if vid in svg:
            hashes["%08x" % hv] = vid
    return {"svg": svg, "meta": meta, "hash": hashes}


def read(path, mode="r"):
    with open(path, mode) as f:
        return f.read()


def js_safe(src):
    return re.sub(r"</(script)", r"<\\/\1", src, flags=re.I)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--module", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--art", default=os.path.join(WEB, "assets", "sparkle_art"), help="PREFIX of the .webp / .json")
    ap.add_argument("--vector", default=os.path.join(FW, "modules", "mathquest", "art_vector"), help="the vector art directory")
    a = ap.parse_args()
    art = json.loads(read(a.art + ".json"))
    keep = {"sprites": art["sprites"], "width": art["width"], "height": art["height"]}
    page = read(os.path.join(WEB, "page", "sparkle.html.in"))
    vector = vector_pack(a.vector)
    subs = {
        "{{VECTOR_JSON}}": js_safe(json.dumps(vector, separators=(",", ":"))),
        "{{SPARKLE_VECTOR_JS}}": js_safe(read(os.path.join(WEB, "page", "sparkle_vector.js"))),
        "{{SPARKLE_RIGS_JS}}": js_safe(read(os.path.join(WEB, "page", "sparkle_rigs.js"))),
        "{{ART_JSON}}": js_safe(json.dumps(keep, separators=(",", ":"))),
        "{{ART_URI}}": "data:image/webp;base64," + base64.b64encode(read(a.art + ".webp", "rb")).decode(),
        "{{CALC_MODULE_JS}}": js_safe(read(a.module)),
        "{{CALC_API_JS}}": js_safe(read(os.path.join(WEB, "page", "calc_api.js"))),
        "{{SPARKLE_AUDIO_JS}}": js_safe(read(os.path.join(WEB, "page", "sparkle_audio.js"))),
        "{{SPARKLE_VOICE_JS}}": js_safe(read(os.path.join(WEB, "page", "sparkle_voice.js"))),
        "{{SPARKLE_PAGE_JS}}": js_safe(read(os.path.join(WEB, "page", "sparkle_page.js"))),
    }
    for k in subs:
        assert k in page, k
    page = re.sub("|".join(map(re.escape, subs)), lambda m: subs[m.group(0)], page)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w") as f:
        f.write(page)
    print(f"wrote {a.out} ({len(page.encode())} bytes)")


if __name__ == "__main__":
    main()
