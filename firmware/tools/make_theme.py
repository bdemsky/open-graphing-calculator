#!/usr/bin/env python3
"""Build a Math Quest theme pack (.THM) from a PNG sprite sheet and a manifest.

    make_theme.py --sheet sheet.png --manifest theme.json --out /Volumes/CARD/CALC/THEMES/PASTEL.THM
    make_theme.py --info PASTEL.THM          print what a pack contains
    make_theme.py --selftest                 build and parse a pack (make test)

The sheet is a grid of equal cells (cell width x height, at most 48 x 48), one
character per cell, transparent background (alpha < 128 is transparent; a
sheet without alpha can name a "key" colour). The manifest (JSON, or YAML when
PyYAML is installed) says which cell is which cast member:

    {
      "title": "Pastel friends",                 shown in the game's settings (<= 23 chars)
      "cell": [20, 20],                          cell width and height in pixels
      "key": "#ff00ff",                          optional transparent colour
      "sprites": [                               up to 160 (the cast and its animation frames)
        {"cell": 0, "slot": "garden", "name": "Pip"},     cell: index (row by row) or [col, row]
        {"cell": [1, 0], "slot": 1, "name": "Dot"},       slot: 0..8 or a role name
        {"cell": 9, "slot": "garden", "anim": "happy", "frame": 0}   anim: idle (default), happy,
      ],                                                  think, dance; frame: 0, 1, ...
      "worlds": {                                optional per-world colours ("skins")
        "garden": {"background": "#e8faee", "accent": "#5cbf8a", "ground": "#a8e6cf"}
      },
      "characters": {                            optional: who the cast are in the stories
        "garden": {"food": "strawberry", "colour": "green", "hobby": "planting seeds",
                   "thing": "watering can", "pronoun": "she",      (singular nouns; <= 15 chars)
                   "voice": {"babble": {"f0": 640, "tint": 1.2, "step": 100, "warble": 0.6, "sig": "breathy"},
                             "tts": {"pitch": 1.45, "rate": 0.9, "voice": "Samantha"}}}
      },                                         voice: optional, for the Mac page's talking characters
                                                 (web/page/sparkle_voice.js: babble f0 Hz, tint (formants
                                                 x), step ms a syllable, warble semitones, sig squeak |
                                                 deep | breathy | chirp | bell | bounce; tts pitch 1-1.6,
                                                 rate 0.7-1.1, an installed macOS voice's name). Not in
                                                 the pack: --voices FILE writes them as JSON for the page
                                                 (SPARKLE_CONFIG.voices)
      "variants": [                              optional: sprites drawn for another display scale
        {"sprite": 0, "scale": 4, "sheet": "sheet4.png", "rect": [0, 0, 80, 80]}
      ],
      "items": [                                 optional: pictures for home items (up to 48)
        {"item": "bed_heart", "rect": [0, 40, 32, 24]},   item: key (tools/gen_mq_items.py) or id
        {"item": 21, "cell": [3, 1]}                      rect: x, y, w, h (<= 64 x 64) or a cell
      ]
    }

Home items (rooms, shop) can be redrawn too: an item picture replaces the built-in one
everywhere (room, shop, book) and is drawn at 2x like the built-in art, bottom-centred
on the item's footprint (gen_mq_items.py --list shows the keys and sizes in cells of
8 x 8 art pixels; gen_mq_items.py --sheet DIR writes every item as a template). A pack
with items is version 2; the "sprites" list may then be empty. A pack without items
stays version 1, readable by every Math Quest version.

Slots (roles): 0 garden, 1 post, 2 bakery, 3 clock, 4 shop, 5 art, 6 fair, 7 racer
(the arcade friend), 8 sky (the challenge track). A slot the pack does not fill
keeps the built-in character.

Animations: each slot may have frames for idle (2-4, a slow loop: breathing, a blink),
happy (2-4, a jump), think (2, a "hmm") and dance (4). The game shows them at the moments
they are for; an animation without frames is made from the slot's idle frame 0 (the same
moves as the built-in cast: bob and blink, jump and squash, tilt, lean and stretch). Frames
are stored in the sprite entries' spare bytes, so an animated pack is still version 1: an
older Math Quest shows the first sprite of each slot, still. Names are at most 11 characters. Pout (2, a little sulk) and fly (2-4) are
the two newer animations; a pack without them gets them made from its idle frame. Newer still:
talk (1-2, the mouth open while a friend speaks), wow (1, amazed), look (2: looking left, then
right, an idle quirk) and blink (1, the eyes shut now and then; with it the idle frames are not
cycled as a blink). Cells up to 48 x 48 (an older Math Quest reads cells up to 32 x 32 only).

Characters (version 3): the stories are errands for the cast ("Pip is baking for a party:
4 trays with 6 strawberries..."); a pack names its cast's favourite food, colour, hobby and
thing and how to talk about them (she / he / they). Variants (version 3): the same sprite
drawn at 2x or 4x for a high-resolution display (stored as RGB565; a 1x display reads past
them). A pack with characters or variants is version 3 (an older Math Quest refuses it and
keeps its own cast); a pack without them is written as before. Colours are
reduced to at most 255 (plus transparent) and stored as RGB565.

File format (little-endian; the loader is modules/mathquest/mq_theme.c, the
documentation docs/MATHQUEST.md "Theme packs"):

    header, 64 bytes: "MQTH", u16 version (1; 2 with items), u16 header size 64,
      char title[24], u8 cell w, u8 cell h, u8 sprites, u8 flags (1: world skins),
      u16 colours, u16 items (version 2, else 0), u32 payload bytes, u32 CRC-32 of the
      payload, 16 zero bytes
    payload: colours x u16 RGB565 (index 0 transparent);
      sprites x {char name[12]; u8 slot; u8 anim (0 idle, 1 happy, 2 think, 3 dance); u8 frame; u8 0};
      if flags & 1: 8 worlds x {u16 background, accent, ground} (0 = built-in);
      sprites x w x h u8 palette indices;
      version 2: items x {u16 item id; u8 w, h}, then items x w x h u8 palette indices
"""
import argparse
import json
import os
import struct
import sys
import tempfile
import zlib

ROLES = ["garden", "post", "bakery", "clock", "shop", "art", "fair", "racer", "sky"]
WORLDS = ["garden", "post", "bakery", "clock", "shop", "art", "fair", "sky"]
MAXCELL, MAXSPR, MAXPX, MAXRLE = 48, 160, 160 * 48 * 48, 73728
ANIMS = ["idle", "happy", "think", "dance", "pout", "fly", "talk", "wow", "look", "blink"]
TRAITS = ["food", "colour", "hobby", "thing"]
PRONOUNS = {"she": 1, "he": 2, "they": 3}
MAXITEM, MAXITEMSIDE, MAXITEMPX = 48, 64, 24576


def item_ids():
    """key -> id of the home items (the catalogue of tools/gen_mq_items.py, next to this file)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import gen_mq_items
    return {it["key"]: k + 1 for k, it in enumerate(gen_mq_items.ITEMS)}


def item_id(v, ids):
    if isinstance(v, int) and 1 <= v <= 65535:
        return v
    if isinstance(v, str) and v in ids:
        return ids[v]
    raise SystemExit(f"unknown item {v!r} (gen_mq_items.py --list shows the keys)")


# ------------------------------------------------------------------ PNG
def read_png(path):
    """(width, height, [(r, g, b, a)] row by row) for 8-bit RGB, RGBA, grey(+alpha) and palette PNGs."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG file")
    pos, idat, plte, trns = 8, b"", None, None
    w = h = depth = ctype = None
    while pos < len(data):
        n, t = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if t == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace:
                return read_png_pil(path)
        elif t == b"PLTE":
            plte = body
        elif t == b"tRNS":
            trns = body
        elif t == b"IDAT":
            idat += body
        elif t == b"IEND":
            break
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    out, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        for x in range(w):
            px = line[x * bpp:(x + 1) * bpp]
            if ctype == 6:
                out.append(tuple(px))
            elif ctype == 2:
                out.append((px[0], px[1], px[2], 255))
            elif ctype == 0:
                out.append((px[0], px[0], px[0], 255))
            elif ctype == 4:
                out.append((px[0], px[0], px[0], px[1]))
            else:
                k = px[0]
                al = trns[k] if trns and k < len(trns) else 255
                out.append((plte[3 * k], plte[3 * k + 1], plte[3 * k + 2], al))
    return w, h, out


def read_png_pil(path):
    try:
        from PIL import Image
    except ImportError:
        raise SystemExit(f"{path}: only 8-bit non-interlaced PNGs are read without Pillow")
    im = Image.open(path).convert("RGBA")
    return im.width, im.height, list(im.getdata())


def write_png(path, w, h, rgba):
    raw = b"".join(b"\x00" + bytes(v for px in rgba[y * w:(y + 1) * w] for v in px) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


# ------------------------------------------------------------------ building
def colour(s):
    s = s.strip().lstrip("#")
    if len(s) != 6:
        raise SystemExit(f"bad colour {s!r}: use #rrggbb")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def rgb565(c):
    r, g, b = c[:3]
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def load_manifest(path):
    text = open(path).read()
    if path.endswith((".yaml", ".yml")):
        try:
            import yaml
        except ImportError:
            raise SystemExit("YAML manifests need PyYAML (pip install pyyaml); or use JSON")
        return yaml.safe_load(text)
    return json.loads(text)


def slot_of(v):
    if isinstance(v, int) and 0 <= v < len(ROLES):
        return v
    if isinstance(v, str) and v in ROLES:
        return ROLES.index(v)
    raise SystemExit(f"bad slot {v!r}: 0..8 or one of {', '.join(ROLES)}")


def rle_size(v):
    """bytes of a sprite as the module keeps it: x, y, w, h, then (run 1..255, value) pairs"""
    n, i = 4, 0
    while i < len(v):
        j = i
        while j < len(v) and v[j] == v[i] and j - i < 255:
            j += 1
        n, i = n + 2, j
    return n


def build(sheet, man):
    w, h, px = sheet
    cw, ch = man["cell"]
    if not (1 <= cw <= MAXCELL and 1 <= ch <= MAXCELL):
        raise SystemExit(f"cell {cw}x{ch}: at most {MAXCELL}x{MAXCELL}")
    sprites = man.get("sprites") or []
    items = man.get("items") or []
    if not (1 if not items else 0) <= len(sprites) <= MAXSPR:
        raise SystemExit(f"{len(sprites)} sprites: 1..{MAXSPR} (0 with items)")
    if len(sprites) * cw * ch > MAXPX:
        raise SystemExit(f"{len(sprites)} sprites of {cw}x{ch}: more than {MAXPX} pixels")
    if len(items) > MAXITEM:
        raise SystemExit(f"{len(items)} items: at most {MAXITEM}")
    key = colour(man["key"]) if man.get("key") else None
    cols = w // cw
    cells = []

    def grab(x0, y0, iw, ih):
        if x0 < 0 or y0 < 0 or x0 + iw > w or y0 + ih > h:
            raise SystemExit(f"{iw}x{ih} at {x0},{y0} is outside the {w}x{h} sheet")
        out = []
        for y in range(ih):
            for x in range(iw):
                r, g, b, a = px[(y0 + y) * w + x0 + x]
                out.append(None if a < 128 or (key and (r, g, b) == key) else (r, g, b))
        return out
    ids = item_ids() if items else {}
    pics = []
    for it in items:
        if "rect" in it:
            x0, y0, iw, ih = it["rect"]
        else:
            c = it["cell"]
            cx, cy = (c % cols, c // cols) if isinstance(c, int) else c
            x0, y0, iw, ih = cx * cw, cy * ch, cw, ch
        if not (1 <= iw <= MAXITEMSIDE and 1 <= ih <= MAXITEMSIDE):
            raise SystemExit(f"item {it['item']}: {iw}x{ih}, at most {MAXITEMSIDE}x{MAXITEMSIDE}")
        pics.append((item_id(it["item"], ids), iw, ih, grab(x0, y0, iw, ih)))
    if sum(iw * ih for _, iw, ih, _ in pics) > MAXITEMPX:
        raise SystemExit(f"item pictures: more than {MAXITEMPX} pixels in all")
    for s in sprites:
        c = s["cell"]
        cx, cy = (c % cols, c // cols) if isinstance(c, int) else c
        if (cx + 1) * cw > w or (cy + 1) * ch > h:
            raise SystemExit(f"cell {c} is outside the {w}x{h} sheet")
        cell = []
        for y in range(ch):
            for x in range(cw):
                r, g, b, a = px[(cy * ch + y) * w + cx * cw + x]
                cell.append(None if a < 128 or (key and (r, g, b) == key) else (r, g, b))
        cells.append(cell)
    # palette: exact colours, reduced by dropping low bits until at most 255 remain
    shift = 0
    while True:
        q = lambda c: tuple((v >> shift) << shift for v in c)
        uniq = sorted({q(c) for cell in cells + [p[3] for p in pics] for c in cell if c})
        if len(uniq) <= 255:
            break
        shift += 1
    index = {c: i + 1 for i, c in enumerate(uniq)}
    pal = [0] + [rgb565(c) for c in uniq]
    payload = b"".join(struct.pack("<H", v) for v in pal)
    for s in sprites:
        name = s.get("name", "").encode("ascii", "replace")[:11]
        anim = s.get("anim", "idle")
        anim = ANIMS.index(anim) if anim in ANIMS else int(anim)
        if not 0 <= anim < len(ANIMS) or not 0 <= int(s.get("frame", 0)) <= 15:
            raise SystemExit(f"sprite {s.get('name', '')}: anim {s.get('anim')} / frame {s.get('frame')}")
        payload += name.ljust(12, b"\0") + bytes([slot_of(s["slot"]), anim, int(s.get("frame", 0)), 0])
    worlds = man.get("worlds") or {}
    flags = 0
    if worlds:
        flags |= 1
        for wn in WORLDS:
            sk = worlds.get(wn, {})
            for k in ("background", "accent", "ground"):
                payload += struct.pack("<H", rgb565(colour(sk[k])) if k in sk else 0)
    rle = 0
    for cell in cells:
        v = bytes(index[q(c)] if c else 0 for c in cell)
        payload += v
        rle += rle_size(v)
    if rle > MAXRLE:  # the module keeps the sprites run-length coded (mq_theme.c)
        raise SystemExit(f"{len(sprites)} sprites of {cw}x{ch}: {rle} bytes coded, more than {MAXRLE}: fewer frames")
    for iid, iw, ih, _ in pics:
        payload += struct.pack("<HBB", iid, iw, ih)
    for _, _, _, pic in pics:
        payload += bytes(index[q(c)] if c else 0 for c in pic)
    chars = man.get("characters") or {}
    if chars:
        flags |= 2
        payload += bytes([len(chars)])
        for role, c in chars.items():
            pr = PRONOUNS.get(str(c.get("pronoun", "")).lower(), 0)
            payload += bytes([slot_of(int(role) if str(role).isdigit() else role), pr])
            for k in TRAITS:
                v = str(c.get(k, "")).encode("ascii", "replace")
                if len(v) > 15:
                    raise SystemExit(f"{role} {k} {v!r}: at most 15 characters")
                payload += v.ljust(16, b"\0")
    variants = man.get("variants") or []
    if variants:
        flags |= 4
        payload += struct.pack("<H", len(variants))
        for v in variants:
            vw, vh, vpx = read_png(v["sheet"]) if isinstance(v.get("sheet"), str) else sheet
            x0, y0, iw, ih = v["rect"]
            if x0 < 0 or y0 < 0 or x0 + iw > vw or y0 + ih > vh or not (2 <= int(v["scale"]) <= 8):
                raise SystemExit(f"variant {v}: outside its sheet or a bad scale")
            payload += struct.pack("<BBHH", int(v["sprite"]), int(v["scale"]), iw, ih)
            for y in range(ih):
                for x in range(iw):
                    r, g, b, a = vpx[(y0 + y) * vw + x0 + x]
                    c = rgb565((r, g, b)) if a >= 128 else 1
                    payload += struct.pack("<H", 0x0021 if c == 1 and a >= 128 else c)
    title = man.get("title", "Theme").encode("ascii", "replace")[:23]
    ver = 3 if (chars or variants) else 2 if pics else 1
    hdr = b"MQTH" + struct.pack("<HH", ver, 64) + title.ljust(24, b"\0")
    hdr += struct.pack("<BBBBHHII", cw, ch, len(sprites), flags, len(pal), len(pics), len(payload),
                       zlib.crc32(payload) & 0xFFFFFFFF)
    hdr += b"\0" * 16
    assert len(hdr) == 64
    return hdr + payload


def parse(b):
    """The pack as a dict (the same checks as the loader)."""
    if b[:4] != b"MQTH":
        raise ValueError("not a theme pack")
    ver, hs = struct.unpack_from("<HH", b, 4)
    if ver not in (1, 2, 3) or hs != 64:
        raise ValueError(f"version {ver} / header {hs}: not supported")
    title = b[8:32].split(b"\0")[0].decode()
    cw, ch, n, flags, ncol, nitem, plen, crc = struct.unpack_from("<BBBBHHII", b, 32)
    if ver == 1:
        nitem = 0
    if ver < 3:
        flags &= 1
    p = b[64:]
    if len(p) != plen:
        raise ValueError("truncated")
    if zlib.crc32(p) & 0xFFFFFFFF != crc:
        raise ValueError("damaged (CRC)")
    o = 0
    pal = list(struct.unpack_from(f"<{ncol}H", p, o))
    o += 2 * ncol
    spr = []
    for i in range(n):
        nm = p[o:o + 12].split(b"\0")[0].decode()
        spr.append({"name": nm, "slot": p[o + 12], "anim": p[o + 13], "frame": p[o + 14]})
        o += 16
    skins = None
    if flags & 1:
        skins = [struct.unpack_from("<3H", p, o + 6 * k) for k in range(len(WORLDS))]
        o += 6 * len(WORLDS)
    for s in spr:
        s["pixels"] = p[o:o + cw * ch]
        o += cw * ch
        if max(s["pixels"]) >= ncol:
            raise ValueError("pixel index out of the palette")
    items = []
    for i in range(nitem):
        iid, iw, ih = struct.unpack_from("<HBB", p, o)
        items.append({"id": iid, "size": (iw, ih)})
        o += 4
    for it in items:
        iw, ih = it["size"]
        it["pixels"] = p[o:o + iw * ih]
        o += iw * ih
        if len(it["pixels"]) != iw * ih or max(it["pixels"]) >= ncol:
            raise ValueError("item pixels out of the palette or truncated")
    chars = {}
    if flags & 2:
        nc = p[o]
        o += 1
        for i in range(nc):
            slot, pr = p[o], p[o + 1]
            tr = [p[o + 2 + 16 * k:o + 18 + 16 * k].split(b"\0")[0].decode() for k in range(len(TRAITS))]
            chars[slot] = dict(zip(TRAITS, tr), pronoun=pr)
            o += 2 + 16 * len(TRAITS)
    variants = []
    if flags & 4:
        nv, = struct.unpack_from("<H", p, o)
        o += 2
        for i in range(nv):
            sp, sc, iw, ih = struct.unpack_from("<BBHH", p, o)
            o += 6 + 2 * iw * ih
            variants.append({"sprite": sp, "scale": sc, "size": (iw, ih)})
    if o != len(p):
        raise ValueError("payload size does not add up")
    return {"title": title, "version": ver, "cell": (cw, ch), "palette": pal, "sprites": spr, "skins": skins,
            "items": items, "characters": chars, "variants": variants}


def selftest():
    with tempfile.TemporaryDirectory() as d:
        w, h = 16, 8
        rgba = []
        for y in range(h):
            for x in range(w):
                if x < 8:
                    rgba.append((255, 120, 160, 255) if (x - 4) ** 2 + (y - 4) ** 2 < 10 else (0, 0, 0, 0))
                else:
                    rgba.append((90, 170, 230, 255) if y > 2 else (0, 0, 0, 0))
        png = os.path.join(d, "s.png")
        write_png(png, w, h, rgba)
        man = {"title": "Self test", "cell": [8, 8],
               "sprites": [{"cell": 0, "slot": "bakery", "name": "Berry"}, {"cell": [1, 0], "slot": 7, "name": "Comet"}],
               "worlds": {"garden": {"background": "#ffffff"}}}
        b = build(read_png(png), man)
        t = parse(b)
        assert t["title"] == "Self test" and t["cell"] == (8, 8)
        assert [s["slot"] for s in t["sprites"]] == [2, 7] and t["sprites"][1]["name"] == "Comet"
        assert len(t["palette"]) == 3 and t["skins"][0][0] == 0xFFFF and t["skins"][1] == (0, 0, 0)
        assert t["sprites"][0]["pixels"][0] == 0 and t["sprites"][1]["pixels"][63] != 0
        assert t["version"] == 1 and not t["items"]
        # animation frames: still version 1, the anim and frame in the sprite entries
        man3 = {"title": "Anim", "cell": [8, 8],
                "sprites": [{"cell": 0, "slot": 2, "name": "Berry"},
                            {"cell": 1, "slot": 2, "anim": "happy", "frame": 1}]}
        t3 = parse(build(read_png(png), man3))
        assert t3["version"] == 1 and t3["sprites"][1]["anim"] == 1 and t3["sprites"][1]["frame"] == 1
        assert t3["sprites"][0]["anim"] == 0
        bad = bytearray(b)
        bad[70] ^= 1
        try:
            parse(bytes(bad))
            raise AssertionError("a damaged pack was accepted")
        except ValueError:
            pass
        # version 2: a pack of home item pictures only (by key and by id)
        man2 = {"title": "Items", "cell": [8, 8],
                "items": [{"item": "bed_heart", "rect": [0, 0, 8, 8]}, {"item": 3, "cell": [1, 0]}]}
        t2 = parse(build(read_png(png), man2))
        assert t2["version"] == 2 and not t2["sprites"] and [i["id"] for i in t2["items"]] == [1, 3]
        assert t2["items"][0]["size"] == (8, 8) and t2["items"][1]["pixels"][63] != 0
        # version 3: characters for the stories and a 2x variant of a sprite
        man4 = {"title": "Chars", "cell": [8, 8], "sprites": [{"cell": 0, "slot": 0, "name": "Berry"},
                                                              {"cell": 1, "slot": 0, "anim": "pout", "frame": 0}],
                "characters": {"garden": {"food": "plum", "colour": "red", "hobby": "singing", "thing": "drum",
                                          "pronoun": "they"}},
                "variants": [{"sprite": 0, "scale": 2, "rect": [0, 0, 16, 8]}]}
        t4 = parse(build(read_png(png), man4))
        assert t4["version"] == 3 and t4["characters"][0]["food"] == "plum" and t4["characters"][0]["pronoun"] == 3
        assert t4["sprites"][1]["anim"] == 4 and t4["variants"] == [{"sprite": 0, "scale": 2, "size": (16, 8)}]
        assert parse(build(read_png(png), man))["version"] == 1
    print("make_theme.py: selftest passed")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sheet")
    ap.add_argument("--manifest")
    ap.add_argument("--out")
    ap.add_argument("--info")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--voices", help="write the characters' voices (JSON {slot: voice}) for the Mac page")
    a = ap.parse_args()
    if a.voices and a.manifest:
        man = load_manifest(a.manifest)
        voices = {}
        for role, c in (man.get("characters") or {}).items():
            if isinstance(c, dict) and isinstance(c.get("voice"), dict):
                voices[str(int(role) if str(role).isdigit() else slot_of(role))] = c["voice"]
        with open(a.voices, "w") as f:
            json.dump(voices, f, indent=1)
        print(f"{a.voices}: voices for {len(voices)} characters")
        if not a.sheet:
            return
    if a.selftest:
        selftest()
        return
    if a.info:
        t = parse(open(a.info, "rb").read())
        print(f"{a.info}: \"{t['title']}\" (version {t['version']}), {len(t['sprites'])} sprites of "
              f"{t['cell'][0]}x{t['cell'][1]}, {len(t['palette'])} colours{', world skins' if t['skins'] else ''}, "
              f"{len(t['items'])} home items, {len(t['characters'])} characters, {len(t['variants'])} variants")
        for slot, c in t["characters"].items():
            print(f"  character {ROLES[slot] if slot < len(ROLES) else slot}: {c}")
        for s in t["sprites"]:
            an = ANIMS[s["anim"]] if s["anim"] < len(ANIMS) else "?"
            tag = "" if (an, s["frame"]) == ("idle", 0) else " [%s %d]" % (an, s["frame"])
            role = ROLES[s["slot"]] if s["slot"] < len(ROLES) else "?"
            print(f"  slot {s['slot']} ({role}): {s['name']}{tag}")
        if t["items"]:
            keys = {v: k for k, v in item_ids().items()}
            for it in t["items"]:
                print(f"  item {it['id']} ({keys.get(it['id'], '?')}): {it['size'][0]}x{it['size'][1]}")
        return
    if not (a.sheet and a.manifest and a.out):
        ap.error("--sheet, --manifest and --out are needed (or --info / --selftest)")
    b = build(read_png(a.sheet), load_manifest(a.manifest))
    name = os.path.basename(a.out)
    stem, ext = os.path.splitext(name)
    if len(stem) > 8 or ext.upper() != ".THM":
        print(f"note: {name}: the calculator lists 8.3 names ending in .THM", file=sys.stderr)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "wb") as f:
        f.write(b)
    t = parse(b)
    print(f"{a.out}: {len(b)} bytes, {len(t['sprites'])} sprites, {len(t['items'])} items, "
          f"{len(t['palette'])} colours")


if __name__ == "__main__":
    main()
