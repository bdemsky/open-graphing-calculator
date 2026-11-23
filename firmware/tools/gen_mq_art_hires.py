#!/usr/bin/env python3
"""Sparkle Town high-resolution art: the built-in cast, stickers and home items, rasterised again
from the same drawing code at a high scale with anti-aliasing, for the standalone page
(web/page/sparkle_*, `make sparkle-web`).

    gen_mq_art_hires.py --out web/assets/sparkle_art      sparkle_art.webp (atlas) + sparkle_art.json
    gen_mq_art_hires.py --check web/assets/sparkle_art    fail if the art changed since (no numpy needed)
    gen_mq_art_hires.py --preview out.png                 every picture, for review

Needs numpy and Pillow (`make sparkle-art` runs it with ../.venv/bin/python).

How. tools/gen_mq_art.py and tools/gen_mq_items.py draw every picture with a few shape calls on
a pixel canvas (discs, ellipses, polygons, rounded rectangles, lines, an outline pass, faces).
This tool imports them read-only and swaps their Canvas for a subclass that keeps a twin of the
picture at K x 3 times the resolution: every shape call also fills the twin with the exact shape
(the same inside test at the twin's sample centres), the outline pass dilates the twin by a disc
of one art pixel, faces are drawn as round eyes with a sparkle, blush and a little smile. The
twin is then reduced 3 x 3 -> 1 (anti-aliasing) to K pixels per art pixel. Anything drawn by
other means (pixels set directly, e.g. the views inside window items) is stamped into the twin
as clean K x K squares: it stays pixel art, integer scaled (listed in the JSON as "pixelart").

The page finds a picture by its content: the FNV-1a hash of its 1x colours (RGB565 per pixel, 1
for transparent, width and height first) -- what platform/web/web_sparkle.c computes from the
pixels the module draws. A changed or new picture simply has no high-resolution art yet (it is
drawn as pixel art) until this tool runs again.
"""
import argparse
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_mq_art  # noqa: E402  (read-only: the drawing code of the cast and stickers)
import gen_mq_items  # noqa: E402  (read-only: the catalogue and its drawing code)

A = 3  # supersampling of the twin (per axis)
K_CAST, K_STICKER, K_ITEM = 24, 16, 10  # output pixels per art pixel (the cast: 320-pixel pictures)


def rgb565(c):
    r, g, b = c[:3]
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def art_hash(cols, w, h):
    """FNV-1a of a picture: w, h (16 bits each, little-endian), then each pixel's RGB565 (1 =
    transparent), row by row (platform/web/web_sparkle.c img_hash)."""
    hv = 2166136261
    for b in (w & 255, w >> 8, h & 255, h >> 8):
        hv = ((hv ^ b) * 16777619) & 0xFFFFFFFF
    for c in cols:
        hv = ((hv ^ (c & 255)) * 16777619) & 0xFFFFFFFF
        hv = ((hv ^ (c >> 8)) * 16777619) & 0xFFFFFFFF
    return hv


# ------------------------------------------------------------------ the high-resolution twin
def hires_class(Base, K, ink, transparent, face=None, excl_col=False, round_put=False):
    """A subclass of the canvas class Base (attribute px: rows of values, `transparent` empty)
    that keeps a twin at K * A samples per art pixel. ink: the outline colour value; excl_col:
    the outline pass skips pixels of its own colour (else of ink); face(canvas, *args): the face
    drawn on the twin (None: stamped); round_put: single pixels as soft dots (else squares)."""
    import numpy as np
    R = K * A

    class Hi(Base):
        def __init__(self, *args, **kw):
            self._busy = 0
            Base.__init__(self, *args, **kw)
            self._H, self._W = len(self.px), len(self.px[0])
            self.hi = np.zeros((self._H * R, self._W * R), np.int32)
            self._ids, self._vals = {}, [transparent]
            self._snap = [r[:] for r in self.px]
            self.stamped = 0  # art pixels that stayed pixel art

        # ---- colours as ids (0 = transparent)
        def _id(self, v):
            if v == transparent or v is None:
                return 0
            key = tuple(v) if isinstance(v, list) else v
            if key not in self._ids:
                self._ids[key] = len(self._vals)
                self._vals.append(v)
            return self._ids[key]

        # ---- what was set by other means: clean squares
        def _sync(self):
            for y in range(self._H):
                ra, rb = self.px[y], self._snap[y]
                if ra == rb:
                    continue
                for x in range(self._W):
                    if ra[x] != rb[x]:
                        self.hi[y * R:(y + 1) * R, x * R:(x + 1) * R] = self._id(ra[x])
                        self.stamped += 1
            self._snap = [r[:] for r in self.px]

        def _grid(self, x0, y0, x1, y1):
            """sample centres (art units) of the twin's window [x0, x1) x [y0, y1) (art units,
            clamped), and its slices"""
            i0, i1 = max(0, int(math.floor(y0 * R))), min(self._H * R, int(math.ceil(y1 * R)))
            j0, j1 = max(0, int(math.floor(x0 * R))), min(self._W * R, int(math.ceil(x1 * R)))
            if i0 >= i1 or j0 >= j1:
                return None
            ys = (np.arange(i0, i1) + 0.5) / R
            xs = (np.arange(j0, j1) + 0.5) / R
            return np.meshgrid(xs, ys), (slice(i0, i1), slice(j0, j1))

        def _fill(self, g, mask, c):
            if g is None:
                return
            (_, _), sl = g
            sub = self.hi[sl]
            sub[mask] = self._id(c)

        # ---- shapes (the twin gets the same inside test, finer)
        def _on(self, g, on):
            """the twin's samples of colour `on` in window g (None: all)"""
            if on is None:
                return True
            (_, _), sl = g
            return self.hi[sl] == self._ids.get(tuple(on) if isinstance(on, list) else on, -1)

        def _ellipse(self, cx, cy, rx, ry, c, on=None):
            if rx <= 0 or ry <= 0:
                return
            g = self._grid(cx - rx - 1, cy - ry - 1, cx + rx + 1, cy + ry + 1)
            if g:
                (X, Y), _ = g
                self._fill(g, (((X - cx) / rx) ** 2 + ((Y - cy) / ry) ** 2 <= 1.0) & self._on(g, on), c)

        def _poly(self, pts, c, on=None):
            xs, ys = [p[0] for p in pts], [p[1] for p in pts]
            g = self._grid(min(xs) - 1, min(ys) - 1, max(xs) + 1, max(ys) + 1)
            if not g:
                return
            (X, Y), _ = g
            inside = np.zeros(X.shape, bool)
            j = len(pts) - 1
            for i in range(len(pts)):
                xi, yi = pts[i]
                xj, yj = pts[j]
                if yi != yj:
                    cond = (yi > Y) != (yj > Y)
                    xint = (xj - xi) * (Y - yi) / (yj - yi) + xi
                    inside ^= cond & (X < xint)
                j = i
            self._fill(g, inside & self._on(g, on), c)

        def _rrect(self, x, y, w, h, r, c):
            g = self._grid(x, y, x + w, y + h)
            if not g:
                return
            (X, Y), _ = g
            dx = np.maximum(np.maximum(x + r - X, X - (x + w - r)), 0)
            dy = np.maximum(np.maximum(y + r - Y, Y - (y + h - r)), 0)
            m = (dx * dx + dy * dy <= r * r) & (X >= x) & (X < x + w) & (Y >= y) & (Y < y + h)
            self._fill(g, m, c)

        def _capsule(self, x0, y0, x1, y1, rad, c):
            g = self._grid(min(x0, x1) - rad - 1, min(y0, y1) - rad - 1, max(x0, x1) + rad + 1, max(y0, y1) + rad + 1)
            if not g:
                return
            (X, Y), _ = g
            vx, vy = x1 - x0, y1 - y0
            ll = vx * vx + vy * vy
            t = np.clip(((X - x0) * vx + (Y - y0) * vy) / ll, 0, 1) if ll else 0
            d2 = (X - x0 - t * vx) ** 2 + (Y - y0 - t * vy) ** 2
            self._fill(g, d2 <= rad * rad, c)

        def _block(self, x, y, c):
            x, y = int(x), int(y)
            if round_put:
                return self._ellipse(x + 0.5, y + 0.5, 0.68, 0.68, c)
            if 0 <= x < self._W and 0 <= y < self._H:
                self.hi[y * R:(y + 1) * R, x * R:(x + 1) * R] = self._id(c)

        def _outline(self, col):
            ink_id = self._id(col if excl_col else ink)
            src = (self.hi != 0) & (self.hi != ink_id)
            dil = dilate(src, R * 0.95)
            self.hi[dil & (self.hi == 0)] = self._id(col)

    # wrap: the logical canvas as before, the twin drawn by the outermost call only
    def wrap(name, hi_fn):
        base = getattr(Base, name, None)
        if base is None:
            return

        def f(self, *args, **kw):
            if self._busy:
                return base(self, *args, **kw)
            self._sync()
            self._busy += 1
            try:
                r = base(self, *args, **kw)
                hi_fn(self, *args, **kw)
            finally:
                self._busy -= 1
            self._snap = [row[:] for row in self.px]
            return r
        setattr(Hi, name, f)

    wrap("ellipse", lambda s, cx, cy, rx, ry, c: s._ellipse(cx, cy, rx, ry, c))
    wrap("ell", lambda s, cx, cy, rx, ry, c: s._ellipse(cx, cy, rx, ry, c))
    wrap("poly", lambda s, pts, c: s._poly(pts, c))
    wrap("ellipse_on", lambda s, cx, cy, rx, ry, c, on: s._ellipse(cx, cy, rx, ry, c, on))
    wrap("poly_on", lambda s, pts, c, on: s._poly(pts, c, on))
    wrap("rrect", lambda s, x, y, w, h, r, c: s._rrect(x, y, w, h, r, c))
    wrap("rect", lambda s, x, y, w, h, c: s._rrect(int(x), int(y), int(x + w) - int(x), int(y + h) - int(y), 0, c))
    wrap("line", lambda s, x0, y0, x1, y1, c: s._capsule(x0 + 0.5, y0 + 0.5, x1 + 0.5, y1 + 0.5, 0.5, c))
    wrap("put", lambda s, x, y, c: s._block(x, y, c))
    wrap("shine", lambda s, x, y: s._ellipse(int(x) + 0.5, int(y) + 0.5, 0.6, 0.6, gen_mq_items.WHITE))

    def outline_hi(s, *args, **kw):
        col = args[0] if args else kw.get("col", ink)
        s._outline(col)
    wrap("outline", outline_hi)
    if face:
        wrap("face", face)

    def flat_synced(self):
        self._sync()
        return Base.flat(self) if hasattr(Base, "flat") else [v for r in self.px for v in r]
    Hi.flat = flat_synced

    def finish(self):
        self._sync()
        return self
    Hi.finish = finish
    return Hi


def dilate(mask, rad):
    """mask grown by a disc of radius rad (samples): a convolution by FFT"""
    import numpy as np
    r = int(math.ceil(rad))
    if r < 1 or not mask.any():
        return mask.copy()
    H, W = mask.shape
    ky, kx = np.mgrid[-r:r + 1, -r:r + 1]
    ker = ((kx * kx + ky * ky) <= rad * rad).astype(np.float32)
    sh = (H + 2 * r + 1, W + 2 * r + 1)
    fs = [1 << int(math.ceil(math.log2(n))) for n in sh]
    fa = np.fft.rfft2(mask.astype(np.float32), fs)
    fk = np.fft.rfft2(ker, fs)
    conv = np.fft.irfft2(fa * fk, fs)[r:r + H, r:r + W]
    return conv > 0.5


def cast_face(s, cx=10, ey=11, gap=3, mouth=True, cheeks=True):
    """gen_mq_art.Canvas.face on the twin: round eyes with a sparkle, blush, a little smile"""
    INK, WHITE, BLUSH = gen_mq_art.INK, gen_mq_art.WHITE, gen_mq_art.BLUSH
    for sx in (-1, 1):
        ex = cx + sx * gap - (1 if sx < 0 else 0)
        if cheeks:
            bx = ex + (-1.5 if sx < 0 else 3.0) + 0.0
            s._ellipse(bx, ey + 2.5, 1.05, 0.6, BLUSH)
        s._ellipse(ex + 1.0, ey + 1.0, 1.08, 1.18, INK)
        s._ellipse(ex + 0.62, ey + 0.6, 0.42, 0.42, WHITE)
    if mouth:
        pts = [(cx - 1.05, ey + 3.25), (cx - 0.5, ey + 3.75), (cx, ey + 3.85), (cx + 0.5, ey + 3.75), (cx + 1.05, ey + 3.25)]
        for a, b in zip(pts, pts[1:]):
            s._capsule(a[0], a[1], b[0], b[1], 0.3, INK)


def item_face(s, cx, ey, gap=2, mouth=True, cheeks=True):
    """gen_mq_items.Canvas.face on the twin"""
    INK, BLUSH = gen_mq_items.INK, gen_mq_items.BLUSH
    for sx in (-1, 1):
        ex = round(cx + sx * gap - (1 if sx < 0 else 0))
        if cheeks:
            s._ellipse(ex + (-1 if sx < 0 else 1) + 0.5, ey + 1.5, 0.62, 0.45, BLUSH)
        s._ellipse(ex + 0.5, ey + 0.5, 0.62, 0.66, INK)
    if mouth:
        if gap >= 2:
            x0 = round(cx) - 1
            s._capsule(x0 + 0.45, ey + 1.55, x0 + 1.55, ey + 1.55, 0.36, INK)
        else:
            s._ellipse(round(cx) + 0.5, ey + 1.5, 0.45, 0.4, INK)


def rgba_of(canvas, colour_of):
    """the twin reduced to K pixels per art pixel: an RGBA uint8 array (anti-aliased)"""
    import numpy as np
    lut = np.zeros((len(canvas._vals), 4), np.float32)
    for i, v in enumerate(canvas._vals):
        if i:
            r, g, b = colour_of(v)
            lut[i] = (r, g, b, 255)
    px = lut[canvas.hi]
    H, W = canvas.hi.shape
    px = px.reshape(H // A, A, W // A, A, 4)
    a = px[..., 3].mean(axis=(1, 3))
    rgb = (px[..., :3] * px[..., 3:4]).mean(axis=(1, 3))
    out = np.zeros((H // A, W // A, 4), np.float32)
    nz = a > 0
    out[..., :3][nz] = rgb[nz] / a[nz][:, None]
    out[..., 3] = a
    return np.clip(out + 0.5, 0, 255).astype(np.uint8)


# ------------------------------------------------------------------ the built-in art
def pictures(hires=True):
    """-> [(name, hash, w, h, K, canvas or None)] for the cast, the stickers and every item frame"""
    out = []
    if hires:
        CastBig = hires_class(gen_mq_art.CastCanvas, K_CAST // 3, gen_mq_art.INK, gen_mq_art.T, round_put=True)
        CastSmall = hires_class(gen_mq_art.CastCanvas, K_CAST * 2 // 3, gen_mq_art.INK, gen_mq_art.T, round_put=True)
        Stk = hires_class(gen_mq_art.Canvas, K_STICKER, gen_mq_art.INK, gen_mq_art.T, face=cast_face, round_put=True)
        Item = hires_class(gen_mq_items.Canvas, K_ITEM, gen_mq_items.INK, gen_mq_items.T, face=item_face)
    saved_a, saved_i, saved_c = gen_mq_art.Canvas, gen_mq_items.Canvas, gen_mq_art.CastCanvas

    def pal_art(v):
        return 1 if v == gen_mq_art.T else rgb565(gen_mq_art.PAL2[v])

    def pal_item(v):
        return 1 if v == gen_mq_items.T else rgb565(gen_mq_items.MASTER[v])
    try:
        for name, fn in gen_mq_art.CAST:
            for n in (gen_mq_art.BIG, gen_mq_art.SMALL):
                for ex, en in enumerate(gen_mq_art.EXPR):
                    if hires:
                        gen_mq_art.CastCanvas = CastBig if n == gen_mq_art.BIG else CastSmall
                    c = fn(ex, n)
                    flat = c.flat()
                    out.append(("cast:%s/%s/%d" % (name, en, n), art_hash([pal_art(v) for v in flat], c.n, c.n), c.n, c.n,
                                K_CAST * 2 * gen_mq_art.SMALL // (3 * n), c if hires else None))
        gen_mq_art.CastCanvas = saved_c
        for k in gen_mq_art.STICKERS:
            if hires:
                gen_mq_art.Canvas = Stk
            c = gen_mq_art.sticker(k)
            flat = c.flat()
            out.append(("sticker:" + k, art_hash([pal_art(v) for v in flat], c.n, c.n), c.n, c.n, K_STICKER,
                        c if hires else None))
        gen_mq_art.Canvas = saved_a
        if hires:
            gen_mq_items.Canvas = Item
        for it in gen_mq_items.art_items():
            for fr in range(it["frames"]):
                c = gen_mq_items.render(it, fr)
                flat = c.flat()
                out.append(("item:%s/%d" % (it["key"], fr), art_hash([pal_item(v) for v in flat], c.w, c.h), c.w, c.h,
                            K_ITEM, c if hires else None))
    finally:
        gen_mq_art.Canvas, gen_mq_items.Canvas, gen_mq_art.CastCanvas = saved_a, saved_i, saved_c
    return out


# ------------------------------------------------------------------ the atlas
def pack(entries, width=4096, pad=2):
    """entries [(key, rgba array)] -> (Pillow RGBA image, {key: [x, y, w, h]}): rows of shelves"""
    from PIL import Image
    order = sorted(range(len(entries)), key=lambda i: -entries[i][1].shape[0])
    x = y = shelf = 0
    place = {}
    for i in order:
        h, w = entries[i][1].shape[:2]
        if x + w + pad > width:
            x, y, shelf = 0, y + shelf + pad, 0
        place[i] = (x + pad, y + pad)
        x += w + pad
        shelf = max(shelf, h + pad)
    H = y + shelf + pad
    img = Image.new("RGBA", (width, H), (0, 0, 0, 0))
    index = {}
    for i, (key, arr) in enumerate(entries):
        px, py = place[i]
        img.paste(Image.fromarray(arr, "RGBA"), (px, py))
        index[key] = [px, py, arr.shape[1], arr.shape[0]]
    return img, index


def write_atlas(prefix, items, meta):
    """items [(hash, art w, art h, rgba, name, stamped)] -> PREFIX.webp + PREFIX.json"""
    img, idx = pack([("%08x" % hv, arr) for hv, _, _, arr, _, _ in items])
    img.save(prefix + ".webp", "WEBP", lossless=True, quality=100, method=6)
    sprites = {}
    for hv, w, h, arr, name, stamped in items:
        k = "%08x" % hv
        sprites[k] = idx[k] + [w, h]
    doc = dict(meta)
    doc.update({"version": 1, "atlas": os.path.basename(prefix) + ".webp", "width": img.width, "height": img.height,
                "sprites": sprites, "names": {"%08x" % hv: name for hv, _, _, _, name, _ in items},
                "pixelart": sorted({name: n for _, _, _, _, name, n in items if n}.items())})
    with open(prefix + ".json", "w") as f:
        json.dump(doc, f, indent=0, sort_keys=True)
        f.write("\n")
    return img


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", help="PREFIX: writes PREFIX.webp and PREFIX.json")
    ap.add_argument("--check", help="PREFIX: fail if PREFIX.json lacks a picture of the current art")
    ap.add_argument("--preview", help="a PNG of every picture")
    a = ap.parse_args()
    if a.check:
        want = {"%08x" % hv: name for name, hv, _, _, _, _ in pictures(hires=False)}
        have = json.load(open(a.check + ".json"))["sprites"]
        missing = sorted(n for k, n in want.items() if k not in have)
        if missing:
            sys.exit("%s.json is out of date (%d pictures changed, e.g. %s): run make sparkle-art"
                     % (a.check, len(missing), ", ".join(missing[:5])))
        print("%s.json: all %d pictures present" % (a.check, len(want)))
        return
    pics = pictures()
    items, seen = [], set()
    for name, hv, w, h, K, c in pics:
        if hv in seen:
            continue
        seen.add(hv)
        colour = (lambda v: gen_mq_art.PAL2[v]) if not name.startswith("item:") else (lambda v: gen_mq_items.MASTER[v])
        c.finish()
        items.append((hv, w, h, rgba_of(c, colour), name, c.stamped))
    if a.out:
        img = write_atlas(a.out, items, {"k": {"cast": K_CAST, "sticker": K_STICKER, "item": K_ITEM}})
        n_px = sum(1 for it in items if it[5])
        print("wrote %s.webp (%dx%d, %d bytes) and %s.json: %d pictures, %d with pixel-art parts"
              % (a.out, img.width, img.height, os.path.getsize(a.out + ".webp"), a.out, len(items), n_px))
    if a.preview:
        img, _ = pack([("%08x" % it[0], it[3]) for it in items], width=2048)
        img.save(a.preview)


if __name__ == "__main__":
    main()
