#!/usr/bin/env python3
"""Math Quest home items: the shop catalogue and its pixel art, drawn procedurally.

    gen_mq_items.py --c modules/mathquest/mq_items.c --h modules/mathquest/mq_items.h
    gen_mq_items.py --check-c FILE --check-h FILE   fail if the generated files are out of date
    gen_mq_items.py --preview out.png               every item on one sheet (art review)
    gen_mq_items.py --sheet DIR                     items as a theme sprite sheet + manifest
                                                    (a template for tools/make_theme.py)

Every item is an ORIGINAL design in a soft, round, pastel style: plum outlines, little
smiling faces on many things, white shine dots. Items are drawn at "art" resolution (one
art pixel = 2 x 2 screen pixels) on a canvas of w x h room cells of 8 x 8 art pixels;
each item uses at most 15 colours of the 32-colour master palette (index 0 = transparent)
and is stored at 4 bits per pixel with its own 16-entry map into the master palette.

The catalogue (ITEMS) is the single source of truth: names, categories, sets, sizes,
prices and art. Item ids are positions in ITEMS (1-based, 0 = none) and are saved in
players' rooms and named in theme packs by key, so items are only ever APPENDED: never
reorder or remove an entry (retire one by setting price 0 and flag F_RETIRED).
"""
import argparse
import json
import math
import os
import struct
import sys
import zlib

# ---------------------------------------------------------------- palette (32 colours)
MASTER = [
    (0, 0, 0), (90, 61, 92), (255, 255, 255), (255, 179, 199), (255, 122, 162), (168, 230, 207),
    (92, 191, 138), (201, 182, 255), (155, 127, 230), (159, 216, 255), (90, 169, 230), (255, 211, 165),
    (255, 164, 92), (255, 230, 128), (176, 133, 96), (255, 150, 170), (255, 248, 236), (222, 184, 140),
    (130, 92, 70), (232, 232, 240), (190, 190, 206), (130, 128, 150), (240, 100, 115), (240, 196, 80),
    (64, 72, 140), (84, 196, 196), (60, 150, 100), (232, 214, 255), (255, 222, 230), (255, 246, 196),
    (150, 205, 120), (120, 90, 160)]
(T, INK, WHITE, PINK, HOT, MINT, GREEN, LAV, PURPLE, SKY, BLUE, PEACH, ORANGE, YELLOW, BROWN, BLUSH, CREAM, LWOOD,
 DWOOD, LGREY, GREY, DGREY, RED, GOLD, NAVY, TEAL, LEAF, LILAC, ROSE, LEMON, MOSS, PLUM) = range(32)

# ---------------------------------------------------------------- catalogue constants (shared with mq.h)
CATS = ["FURN", "LIGHT", "RUG", "PLANT", "WALLDECO", "TOY", "PET", "WALL", "FLOOR", "WINDOW", "OUTFIT", "STICKER"]
(C_FURN, C_LIGHT, C_RUG, C_PLANT, C_WALLDECO, C_TOY, C_PET, C_WALL, C_FLOOR, C_WINDOW, C_OUTFIT,
 C_STICKER) = range(len(CATS))
SETS = ["GARDEN", "POST", "BAKERY", "CLOCK", "CAFE", "ART", "FAIR", "SKY", "HOME", "GIFT", "SPECIAL", "STICKER"]
(S_GARDEN, S_POST, S_BAKERY, S_CLOCK, S_CAFE, S_ART, S_FAIR, S_SKY, S_HOME, S_GIFT, S_SPECIAL,
 S_STICKER) = range(len(SETS))
FLAGS = {"F_WALLHINT": 1, "F_GIFT": 2, "F_SPECIAL": 4, "F_BEHIND": 8, "F_RETIRED": 16, "F_EVENT": 32}
F_WALLHINT, F_GIFT, F_SPECIAL, F_BEHIND, F_RETIRED, F_EVENT = 1, 2, 4, 8, 16, 32
REACTS = ["HEART", "ZZZ", "NOTE", "STAR", "YUM", "WOW"]
R_HEART, R_ZZZ, R_NOTE, R_STAR, R_YUM, R_WOW = range(6)
SLOTS = ["HEAD", "FACE", "NECK", "BODY", "BACK"]
O_HEAD, O_FACE, O_NECK, O_BODY, O_BACK = range(5)
# wall / floor patterns (drawn by mq_home.c)
PATS = ["STRIPES", "DOTS", "CHECKS", "HEARTS", "CLOUDS", "PANELS", "STARS", "SCALLOPS", "BRICKS", "SKYFENCE",
        "LEAVES", "PLANKS", "TILES", "CARPET", "GRASS", "CLOUDFLOOR", "STONE", "BARK"]
P = {n: i for i, n in enumerate(PATS)}


# ---------------------------------------------------------------- canvas
class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[T] * w for _ in range(h)]

    def put(self, x, y, c):
        x, y = int(x), int(y)
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def get(self, x, y):
        return self.px[y][x] if 0 <= x < self.w and 0 <= y < self.h else T

    def rect(self, x, y, w, h, c):
        for yy in range(int(y), int(y + h)):
            for xx in range(int(x), int(x + w)):
                self.put(xx, yy, c)

    def ellipse(self, cx, cy, rx, ry, c):
        for y in range(self.h):
            for x in range(self.w):
                if ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2 <= 1.0:
                    self.px[y][x] = c

    def disc(self, cx, cy, r, c):
        self.ellipse(cx, cy, r, r, c)

    def rrect(self, x, y, w, h, r, c):
        for yy in range(int(y), int(y + h)):
            for xx in range(int(x), int(x + w)):
                dx = max(x + r - (xx + 0.5), (xx + 0.5) - (x + w - r), 0)
                dy = max(y + r - (yy + 0.5), (yy + 0.5) - (y + h - r), 0)
                if dx * dx + dy * dy <= r * r:
                    self.put(xx, yy, c)

    def poly(self, pts, c):
        for y in range(self.h):
            for x in range(self.w):
                px, py, inside = x + 0.5, y + 0.5, False
                j = len(pts) - 1
                for i in range(len(pts)):
                    xi, yi = pts[i]
                    xj, yj = pts[j]
                    if (yi > py) != (yj > py) and px < (xj - xi) * (py - yi) / (yj - yi) + xi:
                        inside = not inside
                    j = i
                if inside:
                    self.px[y][x] = c

    def line(self, x0, y0, x1, y1, c):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n + 1):
            t = i / max(n, 1)
            self.put(round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t), c)

    def star(self, cx, cy, r, c, inner=0.45):
        self.poly([(cx + r * math.cos(-math.pi / 2 + i * math.pi / 5) * (1 if i % 2 == 0 else inner),
                    cy + r * math.sin(-math.pi / 2 + i * math.pi / 5) * (1 if i % 2 == 0 else inner))
                   for i in range(10)], c)

    def heart(self, cx, cy, r, c):
        self.disc(cx - r * 0.5, cy - r * 0.2, r * 0.55, c)
        self.disc(cx + r * 0.5, cy - r * 0.2, r * 0.55, c)
        self.poly([(cx - r * 1.05, cy), (cx + r * 1.05, cy), (cx, cy + r * 1.0)], c)

    def outline(self, col=INK):
        out = [row[:] for row in self.px]
        for y in range(self.h):
            for x in range(self.w):
                if self.px[y][x] != T:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    v = self.get(x + dx, y + dy)
                    if v not in (T, INK):
                        out[y][x] = col
                        break
        self.px = out

    def face(self, cx, ey, gap=2, mouth=True, cheeks=True):
        """Two dot eyes centred on cx (gap = half the distance), blush, a tiny smile."""
        for sx in (-1, 1):
            ex = round(cx + sx * gap - (1 if sx < 0 else 0))
            self.put(ex, ey, INK)
            if cheeks:
                self.put(ex + (-1 if sx < 0 else 1), ey + 1, BLUSH)
        if mouth:
            self.put(round(cx) - 1 if gap >= 2 else round(cx), ey + 1, INK)
            if gap >= 2:
                self.put(round(cx), ey + 1, INK)

    def shine(self, x, y):
        self.put(x, y, WHITE)

    def flat(self):
        return [c for row in self.px for c in row]


# ---------------------------------------------------------------- items
ITEMS = []  # dicts: key name cat set w h price draw frames flags react extra pal aw ah


def item(key, name, cat, st, w, h, price, draw=None, frames=1, flags=0, react=R_HEART, extra=0, aw=None, ah=None):
    assert len(name) <= 15, name
    assert all(i["key"] != key for i in ITEMS), key
    ITEMS.append(dict(key=key, name=name, cat=cat, set=st, w=w, h=h, price=price, draw=draw, frames=frames,
                      flags=flags, react=react, extra=extra, aw=aw if aw is not None else w * 8,
                      ah=ah if ah is not None else h * 8))


def style(key, name, cat, st, price, pat, c0, c1, c2=None):
    """A wall or floor style: no art, a pattern id and three colours (extra = pattern)."""
    item(key, name, cat, st, 0, 0, price, None, extra=P[pat], aw=0, ah=0)
    ITEMS[-1]["cols"] = (c0, c1, c2 if c2 is not None else c1)


# ---- drawing helpers for furniture
def legs(c, x0, x1, y, h, col=DWOOD):
    c.rect(x0, y, 2, h, col)
    c.rect(x1 - 2, y, 2, h, col)


def bed(c, frame_col, quilt, pillow, deco=None, headboard="round"):
    W, H = c.w, c.h
    if headboard == "round":
        c.rrect(1, 3, 10, H - 5, 4, frame_col)
    elif headboard == "cloud":
        for i in range(3):
            c.disc(3 + i * 3, 6 + (i % 2), 3.2, frame_col)
        c.rect(1, 7, 10, H - 9, frame_col)
    else:
        c.rect(1, 4, 10, H - 6, frame_col)
    c.rrect(W - 5, H - 13, 4, 11, 2, frame_col)
    c.rrect(2, H - 12, W - 4, 7, 2, CREAM)
    c.rrect(3, H - 15, 9, 5, 2, pillow)
    c.rrect(11, H - 13, W - 14, 9, 2, quilt)
    c.rect(1, H - 5, W - 2, 3, frame_col)
    legs(c, 2, W - 2, H - 3, 2, DWOOD)
    c.outline()
    if deco == "heart":
        c.heart(W // 2 + 4, H - 9, 2.2, HOT)
    elif deco == "star":
        c.star(W // 2 + 4, H - 9, 3, YELLOW)
    elif deco == "dots":
        for i in range(4):
            c.put(13 + i * 4, H - 10 + (i % 2) * 2, WHITE)
    c.shine(4, H - 14)
    if headboard == "round":
        c.face(6, 8, 2)


def sofa(c, col, cushion, face=True):
    W, H = c.w, c.h
    c.rrect(2, 2, W - 4, H - 6, 3, col)
    c.rrect(0, 6, 5, H - 8, 2, col)
    c.rrect(W - 5, 6, 5, H - 8, 2, col)
    c.rrect(4, H - 9, W - 8, 5, 2, cushion)
    legs(c, 2, W - 2, H - 3, 2)
    c.outline()
    for x in range(W // 2, W // 2 + 1):
        c.line(x, H - 9, x, H - 5, INK)
    if face:
        c.face(W // 2, 6, 3)
    c.shine(4, 4)


def table(c, top, leg=DWOOD, cloth=None, round_top=True):
    W, H = c.w, c.h
    if round_top:
        c.ellipse(W / 2, 5, W / 2 - 1, 3, top)
    else:
        c.rect(1, 3, W - 2, 4, top)
    if cloth is not None:
        c.poly([(2, 5), (W - 2, 5), (W - 4, 10), (4, 10)], cloth)
    c.rect(W // 2 - 1, 7, 2, H - 9, leg)
    c.rect(W // 2 - 4, H - 3, 8, 2, leg)
    c.outline()
    c.shine(4, 4)


def chair(c, col, seat, back="round"):
    W, H = c.w, c.h
    if back == "round":
        c.rrect(3, 1, W - 6, 8, 3, col)
    elif back == "heart":
        c.heart(W / 2, 4, 4.4, col)
    else:
        c.rect(3, 1, W - 6, 8, col)
    c.rect(3, 8, 2, 4, col)
    c.rect(W - 5, 8, 2, 4, col)
    c.rrect(2, 9, W - 4, 3, 1, seat)
    legs(c, 3, W - 3, 12, H - 13)
    c.outline()
    c.shine(5, 3)


def lamp(c, shade, base=DWOOD, light=False, star=False):
    W, H = c.w, c.h
    cx = W / 2
    c.rect(cx - 1, 9, 2, H - 12, base)
    c.rrect(cx - 4, H - 3, 8, 2, 1, base)
    c.poly([(cx - 5, 9), (cx + 5, 9), (cx + 3, 2), (cx - 3, 2)], shade)
    if star:
        c.star(cx, 0.5 + 2.5, 2.6, YELLOW)
    c.outline()
    c.face(cx, 5, 2)
    if light:
        c.put(cx - 4, 10, LEMON), c.put(cx + 3, 10, LEMON)


def plant(c, pot, leaf, flower=None, kind="bush"):
    W, H = c.w, c.h
    cx = W / 2
    c.poly([(cx - 5, H - 8), (cx + 5, H - 8), (cx + 4, H - 1), (cx - 4, H - 1)], pot)
    if kind == "bush":
        c.disc(cx, H - 13, 5, leaf)
        c.disc(cx - 4, H - 10, 3.4, leaf)
        c.disc(cx + 4, H - 10, 3.4, leaf)
    elif kind == "tall":
        for i, dx in enumerate((-3, 0, 3)):
            c.ellipse(cx + dx, H - 13 - (i == 1) * 4, 2.2, 6, leaf)
    elif kind == "cactus":
        c.rrect(cx - 2.5, H - 18, 5, 11, 2.5, leaf)
        c.rrect(cx - 6, H - 15, 3, 5, 1.5, leaf)
        c.rrect(cx + 3, H - 16, 3, 5, 1.5, leaf)
    elif kind == "flowers":
        for dx in (-3, 0, 3):
            c.line(cx + dx, H - 8, cx + dx * 1.3, H - 15, LEAF)
    c.rect(cx - 6, H - 9, 12, 2, pot)
    if kind == "flowers":
        for i, dx in enumerate((-4, 0, 4)):
            c.disc(cx + dx, H - 16 - (i == 1) * 2, 2.2, flower or PINK)
    c.outline()
    if kind == "flowers":
        for i, dx in enumerate((-4, 0, 4)):
            c.put(cx + dx, H - 16 - (i == 1) * 2, YELLOW)
    elif flower is not None:
        for (x, y) in ((cx - 3, H - 14), (cx + 2, H - 12), (cx, H - 16)):
            c.put(x, y, flower)
    c.face(cx, H - 6, 2)


def rug(c, col, inner, kind="oval"):
    W, H = c.w, c.h
    if kind == "oval":
        c.ellipse(W / 2, H / 2 + 0.5, W / 2 - 1, H / 2 - 1.5, col)
        c.ellipse(W / 2, H / 2 + 0.5, W / 2 - 5, H / 2 - 3, inner)
    elif kind == "rect":
        c.rect(1, 2, W - 2, H - 4, col)
        c.rect(4, 3, W - 8, H - 6, inner)
    elif kind == "heart":
        c.heart(W / 2, H / 2 - 1, H / 2 - 1, col)
    c.outline()


def shelf(c, col, items=(PINK, SKY, MINT, YELLOW, LAV)):
    W, H = c.w, c.h
    c.rect(1, 1, W - 2, H - 2, col)
    rows = (H - 4) // 8
    for r in range(rows):
        y = 3 + r * 8
        c.rect(3, y, W - 6, 6, DWOOD)
        for k in range((W - 6) // 3):
            if (k + r) % 5 != 4:
                c.rect(3 + k * 3, y + 1 + (k % 2), 2, 5 - (k % 2), items[(k + r) % len(items)])
    c.outline()


def window(c, view, fr, frame_col=WHITE, shape="square"):
    W, H = c.w, c.h
    if shape == "round":
        c.disc(W / 2, H / 2, W / 2 - 1, frame_col)
        inner = Canvas(W, H)
        inner.disc(W / 2, H / 2, W / 2 - 3, WHITE)
        mask = [[inner.px[y][x] != T for x in range(W)] for y in range(H)]
    elif shape == "arch":
        c.rrect(1, 1, W - 2, H - 2, (W - 2) / 2, frame_col)
        inner = Canvas(W, H)
        inner.rrect(3, 3, W - 6, H - 6, (W - 6) / 2, WHITE)
        mask = [[inner.px[y][x] != T for x in range(W)] for y in range(H)]
    else:
        c.rrect(1, 1, W - 2, H - 2, 2, frame_col)
        mask = [[3 <= x < W - 3 and 3 <= y < H - 3 for x in range(W)] for y in range(H)]
    v = Canvas(W, H)
    view(v, fr)
    for y in range(H):
        for x in range(W):
            if mask[y][x]:
                c.px[y][x] = v.px[y][x] if v.px[y][x] != T else SKY
    if shape == "square":
        c.rect(W // 2 - 1, 3, 1, H - 6, frame_col)
        c.rect(3, H // 2 - 1, W - 6, 1, frame_col)
    c.outline()
    if shape == "square":
        c.rect(1, H - 3, W - 2, 2, frame_col)


# window views (drawn on a full canvas; the window masks them)
def v_meadow(v, fr):
    v.rect(0, 0, v.w, v.h, SKY)
    v.disc(v.w - 7, 6, 3, YELLOW)
    v.ellipse(6 + fr, 7, 4, 2, WHITE)
    v.ellipse(v.w / 2, v.h + 2, v.w, 8, MOSS)
    for i in range(4):
        v.put(4 + i * 5, v.h - 6, PINK)


def v_night(v, fr):
    v.rect(0, 0, v.w, v.h, NAVY)
    v.disc(v.w - 8, 7, 3.4, LEMON), v.disc(v.w - 6.6, 6, 3, NAVY)
    for i, (x, y) in enumerate(((4, 5), (10, 9), (7, 14), (15, 4), (18, 13), (5, 19), (13, 18))):
        if (i + fr) % 3:
            v.put(x, y, YELLOW if i % 2 else WHITE)


def v_sea(v, fr):
    v.rect(0, 0, v.w, v.h, SKY)
    v.rect(0, v.h // 2, v.w, v.h, BLUE)
    for x in range(v.w):
        if (x + fr * 2) % 6 < 2:
            v.put(x, v.h // 2, WHITE)
        if (x + 3 - fr * 2) % 7 < 2:
            v.put(x, v.h // 2 + 4, SKY)
    v.disc(6, 6, 2.4, YELLOW)


def v_snow(v, fr):
    v.rect(0, 0, v.w, v.h, LGREY)
    v.ellipse(v.w / 2, v.h + 3, v.w, 9, WHITE)
    for i in range(9):
        x, y = (i * 7 + 3) % v.w, (i * 5 + fr * 3) % v.h
        v.put(x, y, WHITE)


def v_rainbow(v, fr):
    v.rect(0, 0, v.w, v.h, SKY)
    for r, col in ((10, RED), (9, ORANGE), (8, YELLOW), (7, MINT), (6, BLUE), (5, PURPLE), (4, SKY)):
        v.ellipse(v.w / 2, v.h - 3, r, r, col)
    v.ellipse(4, v.h - 3, 4, 2, WHITE), v.ellipse(v.w - 4, v.h - 3, 4, 2, WHITE)


def v_city(v, fr):
    v.rect(0, 0, v.w, v.h, PLUM)
    for i, (x, w, h) in enumerate(((2, 5, 12), (8, 4, 16), (13, 6, 10), (19, 4, 14))):
        v.rect(x, v.h - h, w, h, NAVY)
        for k in range(3):
            if (i + k + fr) % 2:
                v.put(x + 1 + k % 2 * 2, v.h - h + 2 + k * 3, YELLOW)


def v_flowers(v, fr):
    v.rect(0, 0, v.w, v.h, SKY)
    v.rect(0, v.h - 9, v.w, 9, MOSS)
    for i in range(5):
        x = 3 + i * 4
        v.line(x, v.h - 2, x, v.h - 7, LEAF)
        v.disc(x, v.h - 8 - (i + fr) % 2, 1.5, (PINK, YELLOW, LAV, HOT, WHITE)[i])


def v_clouds(v, fr):
    v.rect(0, 0, v.w, v.h, LILAC)
    for (x, y, r) in ((5 + fr, 8, 3), (14 - fr, 14, 4), (8, 20, 3)):
        v.disc(x, y, r, WHITE), v.disc(x + r, y + 1, r - 1, WHITE)
    v.star(v.w - 5, 5, 2.5, YELLOW)


def v_moon(v, fr):
    v.rect(0, 0, v.w, v.h, PLUM)
    v.disc(v.w / 2, v.h / 2, 5, LEMON)
    v.disc(v.w / 2 + 2, v.h / 2 - 1, 4.2, PLUM)
    if fr:
        v.put(5, 6, WHITE), v.put(v.w - 6, v.h - 7, WHITE)
    else:
        v.put(6, v.h - 6, WHITE), v.put(v.w - 5, 5, WHITE)


def v_forest(v, fr):
    v.rect(0, 0, v.w, v.h, SKY)
    for i in range(4):
        x = 2 + i * 6
        v.poly([(x - 3, v.h), (x + 3, v.h), (x, v.h - 10 - (i % 2) * 3)], LEAF if i % 2 else GREEN)
    v.disc(v.w - 5, 5, 2, WHITE)


# ---- pets (2 frames each)
def kitten(c, fr):  # a sleeping kitten curled on a cushion, breathing
    W, H = c.w, c.h
    c.ellipse(W / 2, H - 3, W / 2 - 1, 2.5, LAV)
    c.ellipse(W / 2, H - 7 - fr * 0.5, 6.5, 4 + fr * 0.5, PEACH)
    c.disc(5, H - 8, 3.6, PEACH)
    c.poly([(2, H - 10), (3, H - 14), (5, H - 11)], PEACH)
    c.poly([(5.5, H - 11), (7.5, H - 14), (8, H - 10)], PEACH)
    c.ellipse(W - 4, H - 6, 3, 1.4, ORANGE)
    c.outline()
    c.put(4, H - 8, INK), c.put(6, H - 8, INK)  # closed eyes
    c.put(3, H - 7, BLUSH), c.put(7, H - 7, BLUSH)
    c.put(3, H - 12, PINK), c.put(7, H - 12, PINK)
    if fr:
        c.put(W - 3, 1, INK), c.put(W - 2, 1, INK), c.put(W - 2, 2, INK), c.put(W - 3, 3, INK), c.put(W - 2, 3, INK)


def fishbowl(c, fr):
    W, H = c.w, c.h
    c.disc(W / 2, H / 2 + 1, 6.5, SKY)
    c.rect(2, 1, W - 4, 3, T)
    c.rect(W / 2 - 4, 3, 8, 1, SKY)
    c.rect(W / 2 - 4, H - 2, 8, 2, LWOOD)
    fx = 4 + fr * 3
    c.ellipse(fx + 1.5, 9, 2, 1.4, ORANGE)
    c.poly([(fx - 1 + (fr * 5 if fr else 0) * 0, 8), (fx - 1, 10.5), (fx + 0.2, 9)], ORANGE) if not fr else \
        c.poly([(fx + 4, 8), (fx + 4, 10.5), (fx + 2.8, 9)], ORANGE)
    c.outline()
    c.put(fx + (2 if not fr else 0), 9, INK)
    c.put(W / 2 + 3, 5 + fr, WHITE), c.put(W / 2 - 3, 13 - fr, WHITE)
    c.put(3, 12, GREEN), c.put(3, 11, GREEN), c.put(W - 4, 12, GREEN)


def puppy(c, fr):
    W, H = c.w, c.h
    c.ellipse(W / 2 + 1, H - 5, 5, 3.5, CREAM)
    c.disc(6, H - 9, 4.2, CREAM)
    c.ellipse(3, H - 9, 1.5, 3, BROWN), c.ellipse(9, H - 9, 1.5, 3, BROWN)
    c.rect(W / 2 - 2, H - 3, 2, 2, CREAM), c.rect(W / 2 + 3, H - 3, 2, 2, CREAM)
    tx = W - 3
    c.line(tx - 1, H - 6, tx + (1 if fr else -1), H - 9, CREAM)
    c.outline()
    c.face(6, H - 10, 1.5)
    c.put(6, H - 8, INK)


def bunny(c, fr):
    W, H = c.w, c.h
    dy = -2 if fr else 0
    c.ellipse(W / 2, H - 5 + dy, 5, 4, WHITE)
    c.disc(W / 2, H - 10 + dy, 3.6, WHITE)
    c.ellipse(W / 2 - 2, H - 15 + dy, 1.2, 3.5, WHITE)
    c.ellipse(W / 2 + 2, H - 15 + dy, 1.2, 3.5, WHITE)
    c.disc(W / 2 + 5, H - 4 + dy, 1.5, WHITE)
    c.outline()
    c.put(W / 2 - 2, H - 15 + dy, PINK), c.put(W / 2 + 2, H - 15 + dy, PINK)
    c.face(W / 2, H - 10 + dy, 1.5)


def hamster(c, fr):
    W, H = c.w, c.h
    c.disc(W / 2, H / 2, W / 2 - 1, GREY)
    c.disc(W / 2, H / 2, W / 2 - 3, LGREY)
    c.rect(W / 2 - 1, H / 2, 2, H / 2, DGREY)
    c.rect(W / 2 - 4, H - 2, 8, 2, DGREY)
    c.ellipse(W / 2, H / 2 + 3, 3.4, 2.6, PEACH)
    c.outline()
    for k in range(6):
        a = (k * 60 + fr * 30) * math.pi / 180
        c.put(W / 2 + (W / 2 - 2) * math.cos(a), H / 2 + (W / 2 - 2) * math.sin(a), DGREY)
    c.face(W / 2, H / 2 + 3, 1.5, mouth=False)


def birdcage(c, fr):
    W, H = c.w, c.h
    c.rrect(2, 3, W - 4, H - 5, 5, LGREY)
    c.rect(3, H - 3, W - 6, 2, GOLD)
    c.disc(W / 2, 2, 1.5, GOLD)
    c.disc(W / 2, H / 2 + 1 - fr, 3, YELLOW)
    c.outline()
    for x in range(4, W - 3, 3):
        for y in range(5, H - 3):
            if c.get(x, y) == LGREY:
                c.put(x, y, GOLD)
    c.face(W / 2, H / 2 + 1 - fr, 1, mouth=False)
    c.put(W / 2 + 2, H / 2 + 2 - fr, ORANGE)


def turtle(c, fr):
    W, H = c.w, c.h
    c.ellipse(W / 2, H - 6, 6, 4, GREEN)
    c.rect(0, H - 6, W, 6, T)
    c.ellipse(W / 2, H - 6, 6, 3.6, GREEN)
    c.rect(1, H - 5, W - 2, 6, T)
    c.disc(W - 3 + fr * 0.5, H - 4, 2.3, MOSS)
    c.rect(W / 2 - 4, H - 4, 2, 3, MOSS), c.rect(W / 2 + 2, H - 4, 2, 3, MOSS)
    c.outline()
    c.put(W - 3 + fr * 0.5, H - 5, INK)
    c.put(W / 2 - 1, H - 8, LEAF), c.put(W / 2 + 2, H - 7, LEAF)


def dragon(c, fr):  # a baby cloud dragon, flapping
    W, H = c.w, c.h
    wy = -3 if fr else 0
    c.poly([(W / 2 - 2, H - 9), (2, H - 13 + wy), (4, H - 7)], LILAC)
    c.poly([(W / 2 + 2, H - 9), (W - 2, H - 13 + wy), (W - 4, H - 7)], LILAC)
    c.ellipse(W / 2, H - 5, 4.5, 4, MINT)
    c.disc(W / 2, H - 11, 4, MINT)
    c.poly([(W / 2 - 3, H - 14), (W / 2 - 2, H - 17), (W / 2 - 1, H - 14)], YELLOW)
    c.poly([(W / 2 + 1, H - 14), (W / 2 + 2, H - 17), (W / 2 + 3, H - 14)], YELLOW)
    c.outline()
    c.face(W / 2, H - 11, 1.5)


def hedgehog(c, fr):
    W, H = c.w, c.h
    for i in range(6):
        c.poly([(3 + i * 2, H - 7), (4 + i * 2, H - 13 + (i % 2) - fr * (i % 2)), (6 + i * 2, H - 7)], BROWN)
    c.ellipse(W / 2, H - 5, 6, 4, BROWN)
    c.ellipse(W / 2 + 3, H - 4, 3.5, 3, PEACH)
    c.outline()
    c.put(W / 2 + 3, H - 5, INK), c.put(W / 2 + 6, H - 4, INK), c.put(W / 2 + 2, H - 3, BLUSH)


def snailpet(c, fr):
    W, H = c.w, c.h
    c.rect(2 + fr, H - 4, W - 5, 3, LEMON)
    c.disc(W / 2 + fr, H - 8, 4.5, PINK)
    c.disc(W - 4 + fr, H - 6, 2.2, LEMON)
    c.outline()
    for a in range(0, 360, 60):
        r = 2.5 - a / 360
        c.put(W / 2 + fr + r * math.cos(a * math.pi / 180), H - 8 + r * math.sin(a * math.pi / 180), HOT)
    c.put(W - 4 + fr, H - 7, INK)
    c.line(W - 4 + fr, H - 8, W - 3 + fr, H - 11, INK)


def penguin_plush(c, fr=0):
    W, H = c.w, c.h
    c.ellipse(W / 2, H / 2 + 1, 5, 6, BLUE)
    c.ellipse(W / 2, H / 2 + 2, 3.2, 4.4, WHITE)
    c.poly([(W / 2 - 1, H / 2), (W / 2 + 1, H / 2), (W / 2, H / 2 + 1.5)], ORANGE)
    c.outline()
    c.face(W / 2, H / 2 - 1, 1.5, mouth=False)


# ---- small generic shapes used by several sets
def box(c, col, lid, deco=None):
    W, H = c.w, c.h
    c.rrect(1, 4, W - 2, H - 5, 2, col)
    c.rect(1, 3, W - 2, 3, lid)
    c.outline()
    if deco:
        deco(c)
    c.face(W / 2, H / 2 + 2, 2)


def frame_pic(c, frame_col, inner):
    W, H = c.w, c.h
    c.rect(1, 1, W - 2, H - 2, frame_col)
    c.rect(3, 3, W - 6, H - 6, WHITE)
    inner(c)
    c.outline()


def balloon(c, x, y, col):
    c.ellipse(x, y, 2.6, 3.2, col)


# ----------------------------------------------------------------------------- the catalogue
# Home basics (open from the start)
item("bed_heart", "Heart bed", C_FURN, S_HOME, 4, 3, 40, lambda c, f: bed(c, PINK, ROSE, WHITE, "heart"), react=R_ZZZ)
item("sofa_peach", "Peach sofa", C_FURN, S_HOME, 4, 2, 30, lambda c, f: sofa(c, PEACH, CREAM))
item("table_round", "Round table", C_FURN, S_HOME, 3, 2, 18, lambda c, f: table(c, LWOOD), react=R_STAR)
item("chair_heart", "Heart chair", C_FURN, S_HOME, 2, 2, 12, lambda c, f: chair(c, HOT, PINK, "heart"))
item("lamp_star", "Star lamp", C_LIGHT, S_HOME, 2, 4, 20, lambda c, f: lamp(c, YELLOW, star=True, light=True),
     react=R_STAR)
item("shelf_books", "Bookshelf", C_FURN, S_HOME, 3, 4, 25, lambda c, f: shelf(c, BROWN), react=R_STAR)
item("rug_lav", "Lavender rug", C_RUG, S_HOME, 5, 1, 12, lambda c, f: rug(c, LAV, PINK))
item("plant_leafy", "Leafy plant", C_PLANT, S_HOME, 2, 3, 10, lambda c, f: plant(c, ORANGE, GREEN))
item("lights_fairy", "Fairy lights", C_WALLDECO, S_HOME, 5, 1, 15,
     lambda c, f: [c.line(0, 1, c.w - 1, 2, DGREY)] + [c.disc(2 + i * 4, 4 + (i % 2), 1.6,
                                                                (YELLOW, PINK, SKY, MINT)[(i + f) % 4]) for i in range(10)],
     frames=2, flags=F_WALLHINT, react=R_WOW)
item("teapot", "Teapot", C_TOY, S_HOME, 2, 2, 8,
     lambda c, f: [c.disc(8, 10, 5.5, MINT), c.rect(13, 8, 3, 2, MINT), c.disc(8, 4, 1.5, PINK), c.outline(),
                   c.face(8, 10, 2)], react=R_YUM)
item("cakestand", "Cake stand", C_TOY, S_HOME, 2, 2, 10,
     lambda c, f: [c.rect(2, 12, 12, 1, GREY), c.rect(7, 12, 2, 3, GREY), c.rrect(3, 5, 10, 7, 2, PINK),
                   c.rect(3, 7, 10, 1, WHITE), c.disc(8, 4, 1.6, HOT), c.outline(), c.face(8, 9, 2)], react=R_YUM)
item("win_sunny", "Sunny window", C_WINDOW, S_HOME, 3, 3, 20, lambda c, f: window(c, v_meadow, f), frames=2,
     flags=F_WALLHINT, react=R_WOW)
item("teddy", "Teddy bear", C_TOY, S_HOME, 2, 2, 14,
     lambda c, f: [c.disc(8, 11, 4.5, BROWN), c.disc(8, 6, 3.8, BROWN), c.disc(4.5, 3, 1.6, BROWN),
                   c.disc(11.5, 3, 1.6, BROWN), c.ellipse(8, 11.5, 2.5, 2.2, PEACH), c.outline(),
                   c.face(8, 6, 1.5), c.put(8, 12, HOT)], react=R_HEART)
item("ball", "Bouncy ball", C_TOY, S_HOME, 1, 1, 4,
     lambda c, f: [c.disc(4, 4 - f, 3, HOT), c.outline(), c.rect(1, 4 - f, 6, 1, WHITE), c.shine(3, 2 - f)],
     frames=2, react=R_NOTE)
item("desk", "Little desk", C_FURN, S_HOME, 3, 2, 16,
     lambda c, f: [c.rect(1, 3, 22, 3, LWOOD), c.rect(2, 6, 7, 8, LWOOD), c.rect(20, 6, 2, 9, DWOOD),
                   c.rect(3, 8, 5, 2, PEACH), c.rect(3, 11, 5, 2, PEACH), c.rect(15, 0, 5, 3, SKY), c.outline(),
                   c.put(5, 8, INK), c.put(5, 11, INK)], react=R_STAR)
item("dresser", "Dresser", C_FURN, S_HOME, 3, 3, 22,
     lambda c, f: [c.rrect(1, 2, 22, 20, 2, MINT), [c.rect(3, 4 + i * 6, 18, 5, WHITE) for i in range(3)],
                   c.rect(5, 22, 2, 2, DWOOD), c.rect(17, 22, 2, 2, DWOOD), c.outline(),
                   [c.put(12, 6 + i * 6, PINK) for i in range(3)], c.face(12, 2, 3, mouth=False)])
item("clock_wall", "Cat clock", C_WALLDECO, S_HOME, 2, 2, 12,
     lambda c, f: [c.disc(8, 9, 6, PINK), c.poly([(3, 5), (4, 1), (7, 4)], PINK), c.poly([(9, 4), (12, 1), (13, 5)], PINK),
                   c.disc(8, 9, 4.4, WHITE), c.outline(), c.line(8, 9, 8, 6, INK), c.line(8, 9, 10 + f, 9 + f, INK)],
     frames=2, flags=F_WALLHINT, react=R_STAR)
item("poster_rainbow", "Rainbow poster", C_WALLDECO, S_HOME, 2, 2, 8,
     lambda c, f: frame_pic(c, LAV, lambda c: [c.ellipse(8, 12, r, r, col) for r, col in
                                                ((5, HOT), (4, YELLOW), (3, MINT), (2, SKY), (1, WHITE))]),
     flags=F_WALLHINT, react=R_WOW)
item("beanbag", "Bean bag", C_FURN, S_HOME, 3, 2, 14,
     lambda c, f: [c.ellipse(12, 10, 10, 5.5, SKY), c.ellipse(9, 6, 6, 4.5, SKY), c.outline(), c.face(9, 7, 2),
                   c.shine(5, 4)])
item("kitten", "Sleepy kitten", C_PET, S_HOME, 2, 2, 45, kitten, frames=2, react=R_ZZZ)
item("fishbowl", "Fish bowl", C_PET, S_HOME, 2, 2, 35, fishbowl, frames=2, react=R_HEART)
style("wall_pink", "Pink stripes", C_WALL, S_HOME, 0, "STRIPES", ROSE, PINK)
style("wall_mint", "Mint dots", C_WALL, S_HOME, 15, "DOTS", MINT, WHITE, MINT)
style("wall_lemon", "Lemon checks", C_WALL, S_HOME, 15, "CHECKS", LEMON, CREAM)
style("wall_hearts", "Heart paper", C_WALL, S_HOME, 20, "HEARTS", LILAC, PINK)
style("wall_sky", "Cloudy sky", C_WALL, S_HOME, 20, "CLOUDS", SKY, WHITE)
style("wall_panel", "Cozy panels", C_WALL, S_HOME, 15, "PANELS", CREAM, LWOOD)
style("floor_honey", "Honey boards", C_FLOOR, S_HOME, 0, "PLANKS", LWOOD, BROWN)
style("floor_mint", "Mint tiles", C_FLOOR, S_HOME, 15, "TILES", MINT, WHITE)
style("floor_carpet", "Pink carpet", C_FLOOR, S_HOME, 15, "CARPET", PINK, ROSE)
style("floor_checker", "Checker floor", C_FLOOR, S_HOME, 20, "CHECKS", WHITE, LAV)
item("win_night", "Starry window", C_WINDOW, S_HOME, 3, 3, 25, lambda c, f: window(c, v_night, f), frames=2,
     flags=F_WALLHINT, react=R_WOW)
item("blocks", "Toy blocks", C_TOY, S_HOME, 2, 2, 8,
     lambda c, f: [c.rect(1, 9, 6, 6, HOT), c.rect(8, 9, 6, 6, SKY), c.rect(4, 3, 6, 6, YELLOW), c.outline(),
                   c.put(4, 12, WHITE), c.put(11, 12, WHITE), c.put(7, 6, WHITE)], react=R_NOTE)

# Garden set (world 0)
item("bench_garden", "Garden bench", C_FURN, S_GARDEN, 4, 2, 25,
     lambda c, f: [c.rect(2, 1, 28, 3, WHITE), c.rect(2, 5, 28, 3, WHITE), c.rect(1, 9, 30, 3, WHITE),
                   legs(c, 3, 29, 12, 3, WHITE), c.outline(), c.face(16, 6, 3)])
item("flowerbed", "Flower bed", C_PLANT, S_GARDEN, 4, 2, 18,
     lambda c, f: [c.rect(1, 10, 30, 5, BROWN)] + [c.disc(3 + i * 4, 7 + (i % 2) * 2, 2.2, (PINK, YELLOW, LAV, HOT)[i % 4])
                                                  for i in range(8)] + [c.outline()] +
                  [c.put(3 + i * 4, 7 + (i % 2) * 2, WHITE if i % 2 else YELLOW) for i in range(8)])
item("birdbath", "Bird bath", C_FURN, S_GARDEN, 2, 3, 20,
     lambda c, f: [c.ellipse(8, 7, 7, 2.5, LGREY), c.ellipse(8, 6.5, 5.5, 1.4, SKY), c.rect(6, 9, 4, 11, LGREY),
                   c.rect(3, 20, 10, 3, LGREY), c.disc(11, 3 - f, 2, YELLOW), c.outline(),
                   c.put(12, 3 - f, INK), c.put(13, 3 - f, ORANGE)], frames=2, react=R_NOTE)
item("wateringcan", "Watering can", C_TOY, S_GARDEN, 2, 2, 8,
     lambda c, f: [c.rrect(3, 6, 9, 8, 2, SKY), c.poly([(11, 9), (15, 3), (15, 5), (12, 11)], SKY),
                   c.line(4, 6, 6, 3, BLUE), c.line(6, 3, 10, 6, BLUE), c.outline(), c.face(7, 9, 1.5)])
item("toadstool", "Toadstool seat", C_FURN, S_GARDEN, 2, 2, 12,
     lambda c, f: [c.ellipse(8, 6, 7, 4.5, RED), c.rect(0, 7, 16, 9, T), c.rect(5, 7, 6, 8, CREAM), c.outline(),
                   c.put(5, 3, WHITE), c.put(10, 4, WHITE), c.put(8, 2, WHITE), c.face(8, 10, 1.5)])
item("sunflower", "Sunflower", C_PLANT, S_GARDEN, 2, 4, 14,
     lambda c, f: [c.rect(7, 10, 2, 20, LEAF), c.ellipse(4, 18, 3, 1.5, LEAF), c.ellipse(12, 22, 3, 1.5, LEAF)] +
                  [c.disc(8 + 4.5 * math.cos(a * math.pi / 4), 7 + 4.5 * math.sin(a * math.pi / 4), 2.2, YELLOW)
                   for a in range(8)] + [c.disc(8, 7, 3.2, BROWN), c.outline(), c.face(8, 7, 1.5)])
item("swing", "Tree swing", C_TOY, S_GARDEN, 3, 3, 22,
     lambda c, f: [c.rect(0, 0, 24, 2, DWOOD), c.line(5 + f, 2, 4 + 2 * f, 17, BROWN), c.line(18 + f, 2, 19 + 2 * f, 17, BROWN),
                   c.rect(2 + 2 * f, 17, 20, 3, PINK), c.outline()], frames=2, react=R_WOW)
item("bunny", "Hoppy bunny", C_PET, S_GARDEN, 2, 2, 40, bunny, frames=2)
item("fence_white", "Picket fence", C_FURN, S_GARDEN, 4, 2, 10,
     lambda c, f: [c.rect(0, 8, 32, 2, WHITE)] + [c.poly([(1 + i * 6, 15), (1 + i * 6, 4), (3 + i * 6, 2), (5 + i * 6, 4),
                                                          (5 + i * 6, 15)], WHITE) for i in range(5)] + [c.outline()])
item("tulippot", "Tulip pot", C_PLANT, S_GARDEN, 2, 2, 8, lambda c, f: plant(c, SKY, LEAF, kind="flowers"))
style("floor_grass", "Soft grass", C_FLOOR, S_GARDEN, 15, "GRASS", MOSS, GREEN)
style("wall_leafy", "Leafy wall", C_WALL, S_GARDEN, 15, "LEAVES", MINT, GREEN, LEAF)
item("win_flowers", "Flower window", C_WINDOW, S_GARDEN, 3, 3, 22, lambda c, f: window(c, v_flowers, f, frame_col=MINT),
     frames=2, flags=F_WALLHINT, react=R_WOW)
item("snail", "Snail pal", C_PET, S_GARDEN, 2, 2, 30, snailpet, frames=2)

# Post Office set (world 1)
item("mailbox", "Mailbox", C_FURN, S_POST, 2, 3, 18,
     lambda c, f: [c.rect(7, 12, 2, 12, DWOOD), c.rrect(1, 3, 14, 10, 4, BLUE), c.rect(13, 1 + f * 3, 2, 6, RED),
                   c.outline(), c.face(8, 8, 2)], frames=2, react=R_NOTE)
item("parcels", "Parcel stack", C_TOY, S_POST, 2, 2, 10,
     lambda c, f: [c.rect(1, 8, 14, 7, LWOOD), c.rect(3, 2, 10, 6, PEACH), c.rect(7, 2, 2, 13, RED), c.outline(),
                   c.face(8, 11, 2)])
item("poster_stamp", "Stamp poster", C_WALLDECO, S_POST, 2, 2, 8,
     lambda c, f: [c.rect(1, 1, 14, 14, WHITE)] + [c.put(x, y, T) for x in range(1, 15, 2) for y in (1, 14)] +
                  [c.rect(3, 3, 10, 10, SKY), c.heart(8, 7, 3, HOT), c.outline()], flags=F_WALLHINT)
item("globe", "Spinny globe", C_TOY, S_POST, 2, 2, 14,
     lambda c, f: [c.disc(8, 7, 5.5, BLUE), c.ellipse(6 + f * 2, 6, 2.5, 3, GREEN), c.ellipse(11 - f, 9, 1.5, 1.5, GREEN),
                   c.rect(7, 13, 2, 2, GOLD), c.rect(4, 15, 8, 1, GOLD), c.outline()], frames=2, react=R_WOW)
item("armchair_blue", "Comfy armchair", C_FURN, S_POST, 3, 3, 26, lambda c, f: sofa(c, SKY, WHITE))
item("plush_penguin", "Penguin plush", C_TOY, S_POST, 2, 2, 12, penguin_plush)
item("letterdesk", "Letter desk", C_FURN, S_POST, 3, 3, 20,
     lambda c, f: [c.rect(1, 9, 22, 3, BLUE), c.rect(2, 12, 3, 10, BLUE), c.rect(19, 12, 3, 10, BLUE),
                   c.rect(4, 3, 7, 6, WHITE), c.rect(12, 5, 6, 4, ROSE), c.outline(), c.line(4, 3, 7, 6, GREY),
                   c.line(10, 3, 7, 6, GREY), c.heart(15, 7, 1.4, HOT)])
item("planes", "Paper planes", C_WALLDECO, S_POST, 3, 2, 10,
     lambda c, f: [c.line(0, 0, 23, 0, DGREY)] + [c.poly([(3 + i * 8, 6 + (i + f) % 2 * 2), (9 + i * 8, 8 + (i + f) % 2 * 2),
                                                         (3 + i * 8, 11 + (i + f) % 2 * 2)], (WHITE, SKY, PINK)[i])
                                                for i in range(3)] + [c.outline()], frames=2, flags=F_WALLHINT, react=R_WOW)
item("hamster", "Wheel hamster", C_PET, S_POST, 2, 2, 35, hamster, frames=2)
style("wall_sky2", "Airmail stripes", C_WALL, S_POST, 15, "STRIPES", WHITE, SKY)
item("win_city", "City window", C_WINDOW, S_POST, 3, 3, 22, lambda c, f: window(c, v_city, f, frame_col=SKY), frames=2,
     flags=F_WALLHINT, react=R_WOW)

# Bakery set (world 2)
item("oven", "Pink oven", C_FURN, S_BAKERY, 3, 3, 30,
     lambda c, f: [c.rrect(1, 2, 22, 21, 3, PINK), c.rect(4, 9, 16, 10, CREAM), c.rect(6, 11, 12, 6, ORANGE if f else PEACH),
                   c.outline(), [c.put(5 + i * 5, 5, HOT) for i in range(4)], c.rect(8, 8, 8, 1, INK)],
     frames=2, react=R_YUM)
item("mixer", "Stand mixer", C_TOY, S_BAKERY, 2, 2, 16,
     lambda c, f: [c.rect(2, 13, 12, 2, MINT), c.rect(3, 3, 3, 10, MINT), c.rrect(3, 1, 11, 5, 2, MINT),
                   c.ellipse(10, 11, 4, 3, LGREY), c.outline(), c.face(8, 3, 1.5, mouth=False)], react=R_YUM)
item("lamp_cupcake", "Cupcake lamp", C_LIGHT, S_BAKERY, 2, 3, 22,
     lambda c, f: [c.poly([(3, 15), (13, 15), (11, 22), (5, 22)], SKY), c.disc(5, 13, 3, PINK), c.disc(11, 13, 3, PINK),
                   c.disc(8, 10, 4, PINK), c.disc(8, 5, 1.8, RED), c.outline(), c.face(8, 12, 2),
                   c.put(4, 10, LEMON if f else PINK)], frames=2, react=R_STAR)
item("cakecase", "Cake display", C_FURN, S_BAKERY, 3, 3, 28,
     lambda c, f: [c.rect(1, 3, 22, 18, SKY), c.rect(2, 4, 20, 16, LGREY), c.rect(2, 11, 20, 1, WHITE),
                   c.rrect(4, 6, 6, 5, 1, PINK), c.rrect(13, 7, 6, 4, 1, YELLOW), c.disc(7, 15, 2.5, BROWN),
                   c.disc(15, 15, 2.5, HOT), c.rect(1, 21, 22, 3, LWOOD), c.outline()], react=R_YUM)
item("counter", "Kitchen counter", C_FURN, S_BAKERY, 4, 3, 26,
     lambda c, f: [c.rect(1, 8, 30, 3, WHITE), c.rect(2, 11, 28, 12, MINT), c.rect(4, 13, 11, 8, WHITE),
                   c.rect(17, 13, 11, 8, WHITE), c.disc(6, 6, 2.2, RED), c.rrect(20, 3, 8, 5, 2, YELLOW), c.outline(),
                   c.put(14, 17, INK), c.put(18, 17, INK)])
item("fridge", "Mint fridge", C_FURN, S_BAKERY, 2, 4, 30,
     lambda c, f: [c.rrect(1, 1, 14, 30, 3, MINT), c.rect(1, 11, 14, 1, INK), c.outline(),
                   c.rect(12, 4, 1, 5, WHITE), c.rect(12, 14, 1, 6, WHITE), c.face(7, 20, 2), c.heart(5, 6, 1.5, HOT)])
item("cookiejar", "Cookie jar", C_TOY, S_BAKERY, 1, 2, 8,
     lambda c, f: [c.rrect(0.5, 4, 7, 11, 2, LILAC), c.rect(1.5, 2, 5, 2, PINK), c.disc(4, 1.5, 1, PINK), c.outline(),
                   c.disc(4, 10, 1.6, BROWN), c.face(4, 7, 1.5)], react=R_YUM)
item("rug_donut", "Donut rug", C_RUG, S_BAKERY, 4, 1, 14,
     lambda c, f: [c.ellipse(16, 4, 15, 3.5, PEACH), c.ellipse(16, 3.6, 13, 2.6, PINK), c.ellipse(16, 4, 4, 1.2, T),
                   c.outline()] + [c.put(6 + i * 3, 3 + i % 2, (WHITE, YELLOW, SKY)[i % 3]) for i in range(7)])
item("pouf_macaron", "Macaron pouf", C_FURN, S_BAKERY, 2, 2, 12,
     lambda c, f: [c.ellipse(8, 5, 7, 3.5, LAV), c.rect(1, 7, 14, 2, CREAM), c.ellipse(8, 11, 7, 3.5, LAV),
                   c.outline(), c.face(8, 4, 2)])
item("pans", "Hanging pans", C_WALLDECO, S_BAKERY, 3, 2, 10,
     lambda c, f: [c.rect(0, 1, 24, 2, DWOOD), c.line(5, 3, 5, 6, DGREY), c.disc(5, 10, 3.5, PINK),
                   c.line(12, 3, 12, 6, DGREY), c.disc(12, 10, 3.5, SKY), c.line(19, 3, 19, 5, DGREY), c.disc(19, 8, 2.5, GOLD),
                   c.outline()], flags=F_WALLHINT, react=R_YUM)
style("wall_bakery", "Sprinkle wall", C_WALL, S_BAKERY, 15, "DOTS", ROSE, HOT, SKY)
style("floor_bakery", "Bakery tiles", C_FLOOR, S_BAKERY, 15, "CHECKS", ROSE, WHITE)

# Clock Tower attic set (world 3)
item("grandclock", "Grand clock", C_FURN, S_CLOCK, 2, 4, 32,
     lambda c, f: [c.rrect(2, 1, 12, 30, 2, DWOOD), c.disc(8, 7, 4.5, CREAM), c.rect(4, 14, 8, 14, BROWN),
                   c.line(8, 15, 8 + (2 if f else -2), 23, GOLD), c.disc(8 + (2 if f else -2), 24, 1.6, GOLD), c.outline(),
                   c.line(8, 7, 8, 4, INK), c.line(8, 7, 10, 8, INK)], frames=2, react=R_STAR)
item("telescope", "Telescope", C_TOY, S_CLOCK, 3, 3, 24,
     lambda c, f: [c.poly([(4, 10), (20, 2), (22, 6), (6, 14)], BLUE), c.line(12, 10, 6, 23, DWOOD),
                   c.line(12, 10, 18, 23, DWOOD), c.line(12, 10, 12, 23, DWOOD), c.outline(),
                   c.put(21, 4, GOLD), c.star(4, 3, 2, YELLOW)], react=R_WOW)
item("trunk", "Treasure trunk", C_FURN, S_CLOCK, 3, 2, 18,
     lambda c, f: [c.rrect(1, 2, 22, 7, 3, BROWN), c.rect(1, 7, 22, 8, BROWN), c.rect(5, 2, 2, 13, GOLD),
                   c.rect(17, 2, 2, 13, GOLD), c.rect(10, 7, 4, 4, GOLD), c.outline(), c.face(12, 12, 3, mouth=False)])
item("gears", "Gear clock", C_WALLDECO, S_CLOCK, 2, 2, 10,
     lambda c, f: [[c.disc(8 + 6.5 * math.cos((a + f * 22) * math.pi / 180), 8 + 6.5 * math.sin((a + f * 22) * math.pi / 180),
                           1.3, GOLD) for a in range(0, 360, 45)], c.disc(8, 8, 5.5, GOLD), c.disc(8, 8, 2, CREAM),
                   c.outline()], frames=2, flags=F_WALLHINT, react=R_WOW)
item("armchair_velvet", "Velvet chair", C_FURN, S_CLOCK, 3, 3, 26, lambda c, f: sofa(c, PURPLE, LAV))
item("candle", "Candle lamp", C_LIGHT, S_CLOCK, 1, 2, 10,
     lambda c, f: [c.rect(2, 7, 4, 7, CREAM), c.rect(0.5, 14, 7, 2, GOLD),
                   c.ellipse(4, 4 + f * 0.5, 1.6, 2.6 - f * 0.5, ORANGE), c.outline(), c.put(4, 5, YELLOW)],
     frames=2, react=R_STAR)
item("win_moon", "Moon window", C_WINDOW, S_CLOCK, 3, 3, 26, lambda c, f: window(c, v_moon, f, frame_col=LWOOD,
                                                                               shape="round"),
     frames=2, flags=F_WALLHINT, react=R_WOW)
item("plush_owl", "Owl plush", C_TOY, S_CLOCK, 2, 2, 12,
     lambda c, f: [c.ellipse(8, 9, 6, 6, LAV), c.poly([(3, 4), (4, 1), (6, 4)], LAV), c.poly([(10, 4), (12, 1), (13, 4)], LAV),
                   c.ellipse(8, 11, 3.5, 3.5, LILAC), c.outline(), c.disc(5.5, 7, 2, WHITE), c.disc(10.5, 7, 2, WHITE),
                   c.put(5, 7, INK), c.put(10, 7, INK), c.put(8, 9, ORANGE)])
item("hourglass", "Hourglass", C_TOY, S_CLOCK, 1, 2, 8,
     lambda c, f: [c.rect(0.5, 1, 7, 2, DWOOD), c.rect(0.5, 13, 7, 2, DWOOD), c.poly([(1, 3), (7, 3), (4, 8), (1, 13), (7, 13)], SKY),
                   c.poly([(2, 4 + f * 2), (6, 4 + f * 2), (4, 7.5)], YELLOW) if f < 1 else None,
                   c.poly([(2, 12), (6, 12), (4, 10 - f * 2)], YELLOW), c.outline()], frames=2, react=R_WOW)
item("bookstack", "Book stack", C_TOY, S_CLOCK, 2, 2, 6,
     lambda c, f: [c.rect(1, 11, 14, 4, RED), c.rect(2, 7, 12, 4, BLUE), c.rect(3, 3, 11, 4, MINT), c.outline(),
                   c.rect(2, 13, 12, 1, WHITE), c.rect(3, 9, 10, 1, WHITE), c.face(8, 4, 2, mouth=False)])
item("canary", "Canary cage", C_PET, S_CLOCK, 2, 3, 35, birdcage, frames=2, react=R_NOTE)
style("wall_attic", "Attic beams", C_WALL, S_CLOCK, 15, "PANELS", PEACH, BROWN)
style("floor_attic", "Old boards", C_FLOOR, S_CLOCK, 15, "PLANKS", BROWN, DWOOD)
style("wall_stars", "Starry paper", C_WALL, S_CLOCK, 20, "STARS", NAVY, YELLOW)

# Cafe set (world 4: the Shop)
item("cafe_table", "Cafe table", C_FURN, S_CAFE, 2, 2, 14, lambda c, f: table(c, WHITE, leg=DGREY, cloth=PINK))
item("cafe_chair", "Cafe chair", C_FURN, S_CAFE, 2, 2, 10, lambda c, f: chair(c, MINT, WHITE))
item("coffee", "Coffee machine", C_TOY, S_CAFE, 2, 2, 18,
     lambda c, f: [c.rrect(2, 2, 12, 13, 2, RED), c.rect(4, 7, 8, 6, DGREY), c.rect(6, 10, 4, 3, WHITE),
                   c.outline(), c.face(8, 4, 2, mouth=False),
                   c.put(7 + f, 8, LGREY), c.put(8 - f, 9, LGREY)],
     frames=2, react=R_YUM)
item("menu_board", "Menu board", C_WALLDECO, S_CAFE, 2, 2, 8,
     lambda c, f: [c.rect(1, 1, 14, 14, DWOOD), c.rect(2, 2, 12, 12, DGREY), c.outline(),
                   [c.rect(4, 4 + i * 3, 7 - i % 2 * 2, 1, (WHITE, PINK, YELLOW)[i]) for i in range(3)],
                   c.heart(11, 12, 1.4, PINK)], flags=F_WALLHINT)
item("awning", "Stripy awning", C_WALLDECO, S_CAFE, 5, 1, 16,
     lambda c, f: [c.rect(0, 0, 40, 4, WHITE)] + [c.rect(i * 8, 0, 4, 4, HOT) for i in range(5)] +
                  [c.disc(2 + i * 4, 5, 2, HOT if i % 2 == 0 else WHITE) for i in range(10)] + [c.outline()],
     flags=F_WALLHINT)
item("register", "Cash till", C_TOY, S_CAFE, 2, 2, 14,
     lambda c, f: [c.rrect(1, 6, 14, 9, 2, LAV), c.rect(3, 2, 8, 4, SKY), c.rect(3, 13 - f * 2, 10, 2, GOLD),
                   c.outline(), [c.put(4 + i * 2, 9, WHITE) for i in range(5)], c.face(7, 3, 1.5)], frames=2, react=R_NOTE)
item("teaset", "Tea set", C_TOY, S_CAFE, 2, 1, 10,
     lambda c, f: [c.disc(5, 4, 3, WHITE), c.rect(8, 3, 2, 1, WHITE), c.rect(10, 4, 4, 3, PINK), c.rect(11, 3, 2, 1, PINK),
                   c.outline(), c.put(5, 2 - f, LGREY) if f else None, c.heart(5, 4, 1, HOT)], frames=2, react=R_YUM)
item("vase", "Flower vase", C_PLANT, S_CAFE, 1, 2, 6,
     lambda c, f: [c.rrect(1.5, 9, 5, 6, 2, SKY), c.line(4, 9, 3, 4, LEAF), c.line(4, 9, 5, 3, LEAF), c.disc(3, 3, 1.6, HOT),
                   c.disc(5.5, 2.5, 1.6, YELLOW), c.outline()])
item("turtle", "Turtle buddy", C_PET, S_CAFE, 2, 2, 30, turtle, frames=2)
item("sign_cafe", "Cafe sign", C_WALLDECO, S_CAFE, 3, 1, 10,
     lambda c, f: [c.rrect(1, 1, 22, 6, 2, CREAM), c.outline(), [c.rect(4 + i * 4, 3, 2, 2, (HOT, ORANGE, GREEN, BLUE)[i])
                                                                  for i in range(4)]], flags=F_WALLHINT)
style("floor_cafe", "Cafe checks", C_FLOOR, S_CAFE, 15, "CHECKS", WHITE, MINT)
style("wall_cafe", "Cafe bricks", C_WALL, S_CAFE, 15, "BRICKS", PEACH, ROSE)

# Art Studio set (world 5)
item("easel", "Easel", C_FURN, S_ART, 2, 4, 20,
     lambda c, f: [c.line(3, 30, 8, 1, LWOOD), c.line(13, 30, 8, 1, LWOOD), c.line(8, 10, 8, 30, LWOOD),
                   c.rect(1, 6, 14, 12, WHITE), c.disc(5, 11, 2.5, SKY), c.heart(10, 11, 2, HOT), c.rect(1, 18, 14, 2, LWOOD),
                   c.outline()], react=R_STAR)
item("rug_palette", "Palette rug", C_RUG, S_ART, 4, 1, 14,
     lambda c, f: [c.ellipse(16, 4, 15, 3.5, LWOOD), c.ellipse(26, 4, 2, 1.4, T), c.outline()] +
                  [c.disc(6 + i * 4, 3 + i % 2, 1.4, (RED, YELLOW, GREEN, BLUE, PURPLE)[i]) for i in range(5)])
item("crayons", "Crayon box", C_TOY, S_ART, 2, 2, 8,
     lambda c, f: [[c.poly([(2 + i * 3, 7), (4 + i * 3, 7), (3 + i * 3, 2)], col) for i, col in
                    enumerate((RED, YELLOW, GREEN, BLUE))], c.rect(1, 6, 14, 9, ORANGE), c.outline(), c.face(8, 10, 2)])
item("painting", "Sunny painting", C_WALLDECO, S_ART, 3, 2, 12,
     lambda c, f: frame_pic(c, GOLD, lambda c: [c.rect(3, 3, 18, 10, SKY), c.ellipse(12, 14, 10, 4, MOSS),
                                                c.disc(17, 6, 2, YELLOW)]), flags=F_WALLHINT, react=R_WOW)
item("sculpture", "Blob sculpture", C_TOY, S_ART, 2, 3, 16,
     lambda c, f: [c.rect(3, 17, 10, 6, WHITE), c.disc(8, 11, 4.5, LAV), c.disc(6, 6, 2.5, PINK), c.disc(10, 6, 2, MINT),
                   c.outline(), c.face(8, 11, 1.5)], react=R_WOW)
item("beanbag_splat", "Splat beanbag", C_FURN, S_ART, 3, 2, 16,
     lambda c, f: [c.ellipse(12, 10, 10, 5.5, WHITE), c.ellipse(9, 6, 6, 4.5, WHITE), c.disc(6, 7, 1.5, HOT),
                   c.disc(14, 10, 2, SKY), c.disc(18, 8, 1.2, YELLOW), c.outline(), c.face(10, 7, 2)])
item("pencil_cup", "Pencil cup", C_TOY, S_ART, 1, 2, 5,
     lambda c, f: [c.line(2, 2, 3, 8, RED), c.line(4, 1, 4, 8, BLUE), c.line(6, 3, 5, 8, GREEN), c.rect(1, 8, 6, 7, PINK),
                   c.outline(), c.face(4, 11, 1.5)])
item("lamp_rainbow", "Rainbow lamp", C_LIGHT, S_ART, 2, 3, 20,
     lambda c, f: [lamp(c, (HOT, ORANGE, YELLOW, MINT, SKY, LAV)[f * 3 % 6])], frames=2, react=R_STAR)
item("shelf_art", "Paint shelf", C_FURN, S_ART, 3, 3, 18, lambda c, f: shelf(c, WHITE, (RED, YELLOW, BLUE, GREEN, PURPLE)))
style("wall_splash", "Paint splats", C_WALL, S_ART, 15, "DOTS", WHITE, HOT, BLUE)
style("floor_art", "Studio floor", C_FLOOR, S_ART, 15, "PLANKS", LGREY, GREY)
item("win_rainbow", "Rainbow window", C_WINDOW, S_ART, 3, 3, 26, lambda c, f: window(c, v_rainbow, f, shape="arch"),
     flags=F_WALLHINT, react=R_WOW)

# Fair set (world 6)
item("carousel_horse", "Carousel pony", C_TOY, S_FAIR, 2, 4, 26,
     lambda c, f: [c.rect(7, 0, 2, 32, GOLD), c.ellipse(8, 15 - f, 6, 4, WHITE), c.disc(12, 10 - f, 3, WHITE),
                   c.rect(4, 18 - f, 2, 5, WHITE), c.rect(10, 18 - f, 2, 5, WHITE), c.rect(10, 7 - f, 2, 5, PINK),
                   c.rect(3, 30, 10, 2, GOLD), c.outline(), c.put(13, 10 - f, INK)], frames=2, react=R_WOW)
item("balloons", "Balloon bunch", C_TOY, S_FAIR, 2, 3, 10,
     lambda c, f: [c.line(8, 23, 4, 9, DGREY), c.line(8, 23, 8, 7, DGREY), c.line(8, 23, 12, 9, DGREY),
                   balloon(c, 4, 6 - f, PINK), balloon(c, 8, 4 + f, SKY), balloon(c, 12, 6 - f, YELLOW), c.outline(),
                   c.shine(3, 4 - f), c.shine(7, 2 + f), c.shine(11, 4 - f)], frames=2, react=R_WOW)
item("cottoncandy", "Candy floss", C_FURN, S_FAIR, 3, 3, 24,
     lambda c, f: [c.rect(2, 12, 20, 8, SKY), c.disc(6, 21, 2.5, DGREY), c.disc(18, 21, 2.5, DGREY),
                   c.disc(9, 7, 4, PINK), c.disc(15, 6, 4, LAV), c.rect(11, 9, 2, 3, WHITE), c.outline(), c.face(12, 15, 3)],
     react=R_YUM)
item("popcorn", "Popcorn maker", C_TOY, S_FAIR, 2, 3, 18,
     lambda c, f: [c.rect(2, 2, 12, 2, RED), c.rect(2, 4, 12, 12, LGREY), c.rect(3, 16, 10, 7, RED),
                   [c.disc(4 + (i * 3) % 9, 12 - (i * 2 + f) % 6, 1.3, CREAM) for i in range(7)], c.outline(),
                   c.face(8, 19, 2)], frames=2, react=R_YUM)
item("ferris", "Ferris wheel", C_TOY, S_FAIR, 3, 3, 28,
     lambda c, f: [c.disc(12, 10, 9, WHITE), c.disc(12, 10, 7.5, T),
                   [c.line(12, 10, 12 + 8 * math.cos((a + f * 30) * math.pi / 180), 10 + 8 * math.sin((a + f * 30) * math.pi / 180), LGREY)
                    for a in range(0, 360, 60)],
                   [c.rect(10 + 9 * math.cos((a + f * 30) * math.pi / 180), 9 + 9 * math.sin((a + f * 30) * math.pi / 180), 3, 3,
                           (PINK, YELLOW, MINT, SKY, LAV, HOT)[k]) for k, a in enumerate(range(0, 360, 60))],
                   c.line(12, 10, 6, 23, PINK), c.line(12, 10, 18, 23, PINK), c.outline()], frames=2, react=R_WOW)
item("prize_bear", "Prize bear", C_TOY, S_FAIR, 2, 2, 14,
     lambda c, f: [c.disc(8, 11, 4.5, PINK), c.disc(8, 6, 3.8, PINK), c.disc(4.5, 3, 1.6, PINK), c.disc(11.5, 3, 1.6, PINK),
                   c.heart(8, 11.5, 2, HOT), c.outline(), c.face(8, 6, 1.5)])
item("flags", "Party flags", C_WALLDECO, S_FAIR, 5, 1, 8,
     lambda c, f: [c.line(0, 1, 39, 1, DGREY)] + [c.poly([(1 + i * 5, 2), (5 + i * 5, 2), (3 + i * 5, 6 + (i + f) % 2)],
                                                         (HOT, YELLOW, SKY, MINT, LAV)[i % 5]) for i in range(8)] + [c.outline()],
     frames=2, flags=F_WALLHINT, react=R_WOW)
item("ringtoss", "Ring toss", C_TOY, S_FAIR, 2, 2, 8,
     lambda c, f: [c.rect(2, 13, 12, 2, LWOOD), c.rect(7, 3, 2, 10, RED), c.ellipse(8, 9, 4, 1.4, YELLOW),
                   c.ellipse(8, 6, 4, 1.4, MINT), c.outline()], react=R_NOTE)
item("hedgehog", "Hedgehog pal", C_PET, S_FAIR, 2, 2, 35, hedgehog, frames=2)
style("wall_fair", "Circus stripes", C_WALL, S_FAIR, 15, "STRIPES", WHITE, RED)
style("floor_fair", "Fair boards", C_FLOOR, S_FAIR, 15, "PLANKS", PEACH, ORANGE)

# Sky Castle set (world 7)
item("bed_cloud", "Cloud bed", C_FURN, S_SKY, 4, 3, 45,
     lambda c, f: bed(c, WHITE, LILAC, LAV, "star", headboard="cloud"), react=R_ZZZ)
item("sofa_cloud", "Cloud sofa", C_FURN, S_SKY, 4, 2, 35,
     lambda c, f: [[c.disc(5 + i * 7, 6, 4.5, WHITE) for i in range(4)], c.rrect(1, 6, 30, 8, 4, WHITE),
                   c.rrect(3, 9, 26, 4, 2, LILAC), c.outline(), c.face(16, 5, 3)])
item("lamp_moon", "Moon lamp", C_LIGHT, S_SKY, 2, 3, 24,
     lambda c, f: [c.rect(7, 12, 2, 10, GOLD), c.rect(4, 22, 8, 2, GOLD), c.disc(8, 7, 5.5, LEMON), c.disc(10.5, 5.5, 4.5, T),
                   c.outline(), c.put(5, 7, INK), c.put(6, 9, BLUSH),
                   c.put(12, 2 + f, YELLOW), c.put(13, 9 - f, WHITE)], frames=2, react=R_STAR)
item("rug_rainbow", "Rainbow rug", C_RUG, S_SKY, 5, 1, 18,
     lambda c, f: [c.ellipse(20, 4, 19, 3.5, RED), c.ellipse(20, 4, 16, 3, YELLOW), c.ellipse(20, 4, 13, 2.6, MINT),
                   c.ellipse(20, 4, 10, 2.2, SKY), c.ellipse(20, 4, 7, 1.8, LAV), c.outline()])
item("chair_moon", "Moon chair", C_FURN, S_SKY, 3, 3, 26,
     lambda c, f: [c.disc(12, 11, 10, YELLOW), c.disc(16, 7, 8.5, T), c.rect(9, 20, 2, 4, GOLD), c.outline(),
                   c.face(6, 12, 1.5)])
item("tower_model", "Castle model", C_TOY, S_SKY, 2, 3, 22,
     lambda c, f: [c.rect(3, 9, 10, 14, LILAC), c.poly([(2, 10), (14, 10), (8, 1)], PINK), c.rect(6, 16, 4, 7, PURPLE),
                   c.line(8, 1, 8, 0, DGREY), c.outline(), c.put(9, 0, HOT if f else YELLOW), c.face(8, 13, 2, mouth=False)],
     frames=2, react=R_WOW)
item("crystal", "Crystal ball", C_TOY, S_SKY, 2, 2, 18,
     lambda c, f: [c.disc(8, 7, 5.5, LAV), c.rect(4, 12, 8, 3, GOLD), c.outline(), c.shine(6, 5), c.shine(7, 4),
                   c.star(9 + f, 8, 1.8, WHITE)], frames=2, react=R_WOW)
item("starmobile", "Star mobile", C_WALLDECO, S_SKY, 3, 2, 14,
     lambda c, f: [c.line(2, 1, 21, 1, GOLD), c.line(5, 1, 5, 6 + f, GOLD), c.line(12, 1, 12, 9 - f, GOLD),
                   c.line(19, 1, 19, 6 + f, GOLD), c.star(5, 9 + f, 3, YELLOW), c.star(12, 12 - f, 3, PINK),
                   c.star(19, 9 + f, 3, SKY), c.outline()], frames=2, flags=F_WALLHINT, react=R_WOW)
item("dragon", "Cloud dragon", C_PET, S_SKY, 2, 3, 50, dragon, frames=2, react=R_WOW)
item("win_clouds", "Cloud window", C_WINDOW, S_SKY, 3, 3, 28, lambda c, f: window(c, v_clouds, f, frame_col=LILAC,
                                                                                  shape="arch"),
     frames=2, flags=F_WALLHINT, react=R_WOW)
style("wall_castle", "Castle stone", C_WALL, S_SKY, 15, "BRICKS", LILAC, LAV)
style("floor_cloud", "Cloud floor", C_FLOOR, S_SKY, 20, "CLOUDFLOOR", SKY, WHITE)

# more windows and room styles for the special rooms
item("win_sea", "Seaside window", C_WINDOW, S_HOME, 3, 3, 24, lambda c, f: window(c, v_sea, f), frames=2,
     flags=F_WALLHINT, react=R_WOW)
item("win_snow", "Snowy window", C_WINDOW, S_HOME, 3, 3, 24, lambda c, f: window(c, v_snow, f, frame_col=LAV),
     frames=2, flags=F_WALLHINT, react=R_WOW)
item("win_forest", "Forest window", C_WINDOW, S_GARDEN, 3, 3, 22, lambda c, f: window(c, v_forest, f, frame_col=LWOOD),
     frames=2, flags=F_WALLHINT, react=R_WOW)
style("wall_skyfence", "Garden sky", C_WALL, S_GARDEN, 0, "SKYFENCE", SKY, WHITE, LWOOD)
style("wall_bark", "Treehouse bark", C_WALL, S_HOME, 0, "BARK", LWOOD, BROWN)
style("floor_stone", "Tower stone", C_FLOOR, S_SKY, 0, "STONE", LGREY, GREY)
style("wall_scallop", "Scallop paper", C_WALL, S_HOME, 20, "SCALLOPS", PEACH, ROSE)

# Gift-only surprises (the daily gift box)
item("snowglobe", "Snow globe", C_TOY, S_GIFT, 2, 2, 0,
     lambda c, f: [c.disc(8, 7, 5.5, SKY), c.rect(3, 12, 10, 3, PINK), c.disc(8, 9, 2.5, WHITE), c.outline(),
                   c.put(5, 4 + f, WHITE), c.put(10, 6 - f, WHITE), c.put(8, 3 + f, WHITE), c.face(8, 9, 1, mouth=False)],
     frames=2, flags=F_GIFT, react=R_WOW)
item("musicbox", "Music box", C_TOY, S_GIFT, 2, 2, 0,
     lambda c, f: [c.rect(1, 9, 14, 6, LAV), c.rect(1, 7, 14, 2, PURPLE), c.disc(8, 5 - f, 2, PINK), c.rect(7, 6, 2, 2, PINK),
                   c.outline(), c.face(8, 12, 2)], frames=2, flags=F_GIFT, react=R_NOTE)
item("clover", "Lucky clover", C_PLANT, S_GIFT, 1, 2, 0,
     lambda c, f: [c.rect(1, 10, 6, 5, PINK), c.line(4, 10, 4, 6, LEAF)] +
                  [c.heart(4 + dx, 4 + dy, 1.6, GREEN) for dx, dy in ((-1.5, 0), (1.5, 0), (0, -2))] + [c.outline()],
     flags=F_GIFT, react=R_STAR)
item("heart_crystal", "Heart crystal", C_TOY, S_GIFT, 1, 2, 0,
     lambda c, f: [c.rect(1, 12, 6, 3, LWOOD), c.heart(4, 6, 3, PINK), c.outline(), c.shine(3, 5 + f)],
     frames=2, flags=F_GIFT, react=R_HEART)
item("poster_comet", "Comet poster", C_WALLDECO, S_GIFT, 2, 2, 0,
     lambda c, f: frame_pic(c, PURPLE, lambda c: [c.rect(3, 3, 10, 10, NAVY), c.line(4, 12, 10, 6, LAV),
                                                   c.star(10, 6, 2.5, YELLOW)]), flags=F_GIFT | F_WALLHINT, react=R_WOW)
item("kite", "Rainbow kite", C_WALLDECO, S_GIFT, 2, 3, 0,
     lambda c, f: [c.poly([(8, 1), (14, 8), (8, 15), (2, 8)], HOT), c.poly([(8, 1), (14, 8), (8, 8)], YELLOW),
                   c.poly([(8, 15), (2, 8), (8, 8)], SKY), c.outline(), c.line(8, 15, 8 + f * 2, 23, DGREY),
                   c.put(8 + f, 19, PINK), c.put(9 + f, 22, MINT)], frames=2, flags=F_GIFT | F_WALLHINT, react=R_WOW)
item("shell", "Pearl shell", C_TOY, S_GIFT, 2, 2, 0,
     lambda c, f: [c.poly([(1, 10), (8, 2), (15, 10), (11, 15), (5, 15)], PEACH), c.disc(8, 11, 2, WHITE), c.outline(),
                   [c.line(8, 4, 3 + i * 2.5, 14, ROSE) for i in range(5)], c.disc(8, 11, 1.6, WHITE), c.shine(8, 10)],
     flags=F_GIFT, react=R_WOW)
item("puppy", "Waggy puppy", C_PET, S_GIFT, 2, 2, 0, puppy, frames=2, flags=F_GIFT)

# Set bonuses (one special item per completed world set; never sold)
item("bonus_garden", "Fairy fountain", C_FURN, S_SPECIAL, 3, 3, 0,
     lambda c, f: [c.ellipse(12, 19, 11, 4, LGREY), c.ellipse(12, 18, 9, 2.5, SKY), c.rect(10, 8, 4, 10, LGREY),
                   c.ellipse(12, 8, 6, 2, LGREY), c.disc(12, 4 - f, 1.6, SKY), c.disc(7, 5 + f, 1, SKY),
                   c.disc(17, 5 + f, 1, SKY), c.outline(), c.star(4, 3, 2, YELLOW), c.face(12, 13, 1.5)],
     frames=2, flags=F_SPECIAL, extra=S_GARDEN, react=R_WOW)
item("bonus_post", "Golden mailbox", C_FURN, S_SPECIAL, 2, 3, 0,
     lambda c, f: [c.rect(7, 12, 2, 12, GOLD), c.rrect(1, 3, 14, 10, 4, GOLD), c.rect(13, 1 + f * 3, 2, 6, HOT),
                   c.outline(), c.face(8, 8, 2), c.shine(4, 5)], frames=2, flags=F_SPECIAL, extra=S_POST, react=R_WOW)
item("bonus_bakery", "Cupcake throne", C_FURN, S_SPECIAL, 3, 4, 0,
     lambda c, f: [c.poly([(3, 18), (21, 18), (19, 31), (5, 31)], SKY), c.disc(6, 15, 5, PINK), c.disc(18, 15, 5, PINK),
                   c.disc(12, 10, 7, PINK), c.disc(12, 3, 2.5, RED), c.rect(6, 16, 12, 3, WHITE), c.outline(),
                   c.face(12, 11, 3), [c.put(7 + i * 3, 23 + i % 2 * 3, WHITE) for i in range(4)]],
     flags=F_SPECIAL, extra=S_BAKERY, react=R_WOW)
item("bonus_clock", "Star clock", C_WALLDECO, S_SPECIAL, 2, 2, 0,
     lambda c, f: [c.star(8, 8.5, 7.5, YELLOW, 0.55), c.disc(8, 9, 3.2, WHITE), c.outline(), c.line(8, 9, 8, 7, INK),
                   c.line(8, 9, 9 + f, 10, INK)], frames=2, flags=F_SPECIAL | F_WALLHINT, extra=S_CLOCK, react=R_WOW)
item("bonus_cafe", "Golden teapot", C_TOY, S_SPECIAL, 2, 2, 0,
     lambda c, f: [c.disc(8, 10, 5.5, GOLD), c.rect(13, 8, 3, 2, GOLD), c.disc(8, 4, 1.5, HOT), c.outline(),
                   c.face(8, 10, 2), c.shine(5, 8), c.put(14, 5 - f, WHITE)], frames=2, flags=F_SPECIAL, extra=S_CAFE,
     react=R_WOW)
item("bonus_art", "Rainbow easel", C_FURN, S_SPECIAL, 2, 4, 0,
     lambda c, f: [c.line(3, 30, 8, 1, GOLD), c.line(13, 30, 8, 1, GOLD), c.rect(1, 6, 14, 12, WHITE)] +
                  [c.ellipse(8, 17, r, r, col) for r, col in ((6, RED), (5, YELLOW), (4, MINT), (3, SKY), (2, WHITE))] +
                  [c.rect(1, 18, 14, 2, GOLD), c.outline()], flags=F_SPECIAL, extra=S_ART, react=R_WOW)
item("bonus_fair", "Mini carousel", C_TOY, S_SPECIAL, 3, 3, 0,
     lambda c, f: [c.poly([(1, 8), (23, 8), (12, 1)], HOT), c.rect(2, 8, 20, 2, YELLOW), c.rect(11, 10, 2, 10, GOLD),
                   c.rect(2, 20, 20, 3, PINK), c.ellipse(6 + f * 12, 15, 3, 2, WHITE), c.ellipse(18 - f * 12, 15, 3, 2, SKY),
                   c.outline(), c.face(12, 21, 3, mouth=False)], frames=2, flags=F_SPECIAL, extra=S_FAIR, react=R_WOW)
item("bonus_sky", "Rainbow bed", C_FURN, S_SPECIAL, 4, 3, 0,
     lambda c, f: bed(c, LILAC, RED if f else ORANGE, WHITE, "star"), frames=2, flags=F_SPECIAL, extra=S_SKY, react=R_ZZZ)

# Outfits and accessories for the buddy (20 x 20 overlays on the cast cell; extra = slot)
def outfit(key, name, st, price, slot, draw, flags=0):
    item(key, name, C_OUTFIT, st, 0, 0, price, lambda c, f: draw(c), flags=flags, extra=slot, aw=20, ah=20)


outfit("bow_pink", "Pink bow", S_HOME, 10, O_HEAD,
       lambda c: [c.poly([(10, 4), (5, 1), (5, 7)], HOT), c.poly([(10, 4), (15, 1), (15, 7)], HOT), c.disc(10, 4, 1.5, PINK),
                  c.outline()])
outfit("glasses_round", "Round glasses", S_HOME, 10, O_FACE,
       lambda c: [c.disc(7, 11.5, 2.4, INK), c.disc(13, 11.5, 2.4, INK), c.disc(7, 11.5, 1.6, T), c.disc(13, 11.5, 1.6, T),
                  c.rect(9, 11, 2, 1, INK)])
outfit("scarf_red", "Cozy scarf", S_HOME, 12, O_NECK,
       lambda c: [c.rrect(4, 15, 12, 3, 1.5, RED), c.rect(12, 16, 3, 4, RED), c.outline(),
                  [c.put(5 + i * 2, 16, WHITE) for i in range(5)]])
outfit("sunhat", "Sun hat", S_GARDEN, 14, O_HEAD,
       lambda c: [c.ellipse(10, 5, 9, 2, LEMON), c.ellipse(10, 3, 5, 3, LEMON), c.rect(5, 4, 10, 1, HOT), c.outline(),
                  c.disc(14, 3, 1.2, PINK)])
outfit("flowerclip", "Flower clip", S_GARDEN, 8, O_HEAD,
       lambda c: [[c.disc(15 + 1.8 * math.cos(a * math.pi / 2.5), 5 + 1.8 * math.sin(a * math.pi / 2.5), 1.3, PINK)
                   for a in range(5)], c.outline(), c.put(15, 5, YELLOW)])
outfit("mailcap", "Mail cap", S_POST, 12, O_HEAD,
       lambda c: [c.rrect(5, 1, 10, 5, 2, BLUE), c.rect(10, 5, 7, 1.5, NAVY), c.outline(), c.put(10, 3, GOLD)])
outfit("satchel", "Mail satchel", S_POST, 12, O_BODY,
       lambda c: [c.line(5, 13, 14, 18, BROWN), c.rrect(12, 15, 6, 4, 1, BROWN), c.outline(), c.put(15, 16, GOLD)])
outfit("chefhat", "Chef hat", S_BAKERY, 14, O_HEAD,
       lambda c: [c.rect(6, 3, 8, 3, WHITE), c.disc(7, 2, 2.4, WHITE), c.disc(10, 1.5, 2.4, WHITE), c.disc(13, 2, 2.4, WHITE),
                  c.outline()])
outfit("apron", "Cherry apron", S_BAKERY, 12, O_BODY,
       lambda c: [c.rrect(6, 14, 8, 6, 1, WHITE), c.outline(), c.disc(10, 17, 1, RED), c.put(10, 15, LEAF)])
outfit("starglasses", "Star glasses", S_CLOCK, 14, O_FACE,
       lambda c: [c.star(6.5, 11.5, 3.2, YELLOW), c.star(13.5, 11.5, 3.2, YELLOW), c.rect(9, 11, 2, 1, GOLD), c.outline(),
                  c.put(6, 12, INK), c.put(13, 12, INK)])
outfit("wizardhat", "Wizard hat", S_CLOCK, 16, O_HEAD,
       lambda c: [c.poly([(4, 5), (16, 5), (11, -3)], NAVY), c.rect(3, 4, 14, 2, NAVY), c.outline(), c.star(10, 2, 1.5, YELLOW)])
outfit("bowtie", "Bow tie", S_CAFE, 8, O_NECK,
       lambda c: [c.poly([(10, 16), (6, 14), (6, 18)], HOT), c.poly([(10, 16), (14, 14), (14, 18)], HOT), c.outline(),
                  c.put(10, 16, PINK)])
outfit("beret", "Beret", S_ART, 12, O_HEAD,
       lambda c: [c.ellipse(9, 4, 6, 2.6, RED), c.rect(9, 0.5, 1, 2, RED), c.outline()])
outfit("smock", "Paint smock", S_ART, 12, O_BODY,
       lambda c: [c.rrect(5, 14, 10, 6, 2, SKY), c.outline(), c.put(7, 16, HOT), c.put(12, 17, YELLOW), c.put(9, 18, GREEN)])
outfit("heartshades", "Heart shades", S_FAIR, 14, O_FACE,
       lambda c: [c.heart(6.5, 11, 2.6, HOT), c.heart(13.5, 11, 2.6, HOT), c.rect(9, 10, 2, 1, HOT), c.outline(),
                  c.put(6, 10, WHITE), c.put(13, 10, WHITE)])
outfit("balloon_held", "Balloon", S_FAIR, 8, O_BACK,
       lambda c: [c.line(17, 18, 17, 6, DGREY), c.ellipse(17, 3, 2.6, 3, PINK), c.outline(), c.put(16, 2, WHITE)],
       flags=F_BEHIND)
outfit("wings", "Fairy wings", S_SKY, 20, O_BACK,
       lambda c: [c.ellipse(2.5, 10, 2.5, 4, LILAC), c.ellipse(17.5, 10, 2.5, 4, LILAC), c.ellipse(3, 15, 2, 2.5, PINK),
                  c.ellipse(17, 15, 2, 2.5, PINK), c.outline()], flags=F_BEHIND)
outfit("starwand", "Star wand", S_SKY, 14, O_BACK,
       lambda c: [c.line(1, 18, 3, 10, GOLD), c.star(3.5, 7.5, 3, YELLOW), c.outline()])
outfit("tiara", "Moon tiara", S_SKY, 16, O_HEAD,
       lambda c: [c.rect(5, 4, 10, 1.5, GOLD), c.disc(10, 2.5, 2, LEMON), c.disc(11, 2, 1.6, T), c.outline(),
                  c.put(6, 3, PINK), c.put(14, 3, PINK)])
outfit("catears", "Cat ears", S_HOME, 12, O_HEAD,
       lambda c: [c.poly([(4, 6), (5, 0), (9, 4)], PEACH), c.poly([(11, 4), (15, 0), (16, 6)], PEACH), c.outline(),
                  c.put(6, 3, PINK), c.put(14, 3, PINK)])

STICKER_ART = 12  # the 16 stickers of mq_art.c become wall stickers (art from mq_art_sticker)
for i, nm in enumerate(["heart", "star", "flower", "rainbow", "moon", "cupcake", "strawberry", "cloud", "icecream",
                        "balloon", "gem", "mushroom", "leaf", "shell", "sun", "cherry"]):
    item("sticker_" + nm, nm.capitalize(), C_STICKER, S_STICKER, 2, 2, 0, None, flags=F_WALLHINT,
         extra=i, aw=0, ah=0, react=R_STAR)

# ---- appended items (ids are positions: only ever add at the end) ----
# the squishy shelf: comes with the first squishy; the room draws the collection on it
item("squishy_shelf", "Squishy shelf", C_FURN, S_HOME, 3, 3, 0,
     lambda c, f: [c.rrect(1, 1, 22, 22, 3, ROSE), c.rect(2, 8, 20, 2, LWOOD), c.rect(2, 15, 20, 2, LWOOD),
                   c.rect(2, 21, 20, 2, LWOOD), c.rect(3, 23, 2, 1, DWOOD), c.rect(19, 23, 2, 1, DWOOD), c.outline(),
                   c.heart(12, 4, 1.6, HOT)], react=R_HEART)

# weekly events (by play days): each sold during its week, kept forever (extra = the event)
item("blossom_tree", "Blossom tree", C_PLANT, S_HOME, 2, 4, 20,
     lambda c, f: [c.rect(7, 18, 3, 13, BROWN), c.disc(8, 10, 7, PINK), c.disc(4, 13, 4, ROSE), c.disc(12, 13, 4, ROSE),
                   c.outline(), [c.put(4 + i * 3, 6 + (i + f) % 3 * 3, WHITE) for i in range(4)], c.face(8, 11, 2)],
     frames=2, flags=F_EVENT, extra=0, react=R_WOW)
item("beach_umbrella", "Beach umbrella", C_FURN, S_HOME, 3, 4, 20,
     lambda c, f: [c.rect(11, 8, 2, 23, LWOOD), c.poly([(1, 10), (23, 10), (12, 1)], HOT), c.poly([(8, 10), (16, 10), (12, 1)], WHITE),
                   c.ellipse(12, 30, 11, 2, LEMON), c.outline(), c.disc(19, 27, 2, SKY)], flags=F_EVENT, extra=1, react=R_STAR)
item("pumpkin_lamp", "Pumpkin lamp", C_LIGHT, S_HOME, 2, 2, 20,
     lambda c, f: [c.ellipse(8, 10, 7, 5.5, ORANGE), c.rect(7, 2, 2, 3, LEAF), c.outline(), c.face(8, 9, 2),
                   c.put(5, 12, YELLOW if f else LEMON), c.put(11, 12, YELLOW if f else LEMON)], frames=2, flags=F_EVENT,
     extra=2, react=R_WOW)
item("snowman", "Snow friend", C_TOY, S_HOME, 2, 3, 20,
     lambda c, f: [c.disc(8, 17, 6, WHITE), c.disc(8, 8, 4.5, WHITE), c.rect(5, 2, 6, 3, NAVY), c.rect(4, 5, 8, 1, NAVY),
                   c.outline(), c.face(8, 8, 1.5), c.put(9, 9, ORANGE), c.rect(5, 12, 6, 1, RED)], flags=F_EVENT, extra=3,
     react=R_HEART)

# Dear things ($1 to $10): big furniture, room themes, pet houses, rare squishies -- something to save up
# for, so paying with dollar bills and getting change from $5 has real use. On sale from the start (Home);
# the shop marks them with a gold ribbon. A first level pays about 58c, a play day's quests 50c: a $1
# theme is a day or two, a $4.50 bed a week of play, the $10 castle a few weeks.
item("canopy_bed", "Canopy bed", C_FURN, S_HOME, 5, 4, 450,
     lambda c, f: [c.rect(2, 4, 2, 27, GOLD), c.rect(36, 4, 2, 27, GOLD), c.rrect(0, 0, 40, 6, 2, LILAC),
                   [c.disc(3 + i * 5, 6, 2.2, LILAC) for i in range(8)],
                   c.poly([(4, 7), (10, 7), (5, 24)], PINK), c.poly([(36, 7), (30, 7), (35, 24)], PINK),
                   c.rrect(6, 11, 28, 10, 4, WHITE), c.rrect(4, 19, 32, 9, 2, ROSE), c.rrect(8, 15, 9, 5, 2, CREAM),
                   c.rrect(23, 15, 9, 5, 2, CREAM), c.rect(3, 27, 34, 3, LWOOD), c.rect(4, 30, 2, 2, DWOOD),
                   c.rect(34, 30, 2, 2, DWOOD), c.outline(), c.heart(20, 23, 2.2, HOT), c.star(20, 2, 1.6, YELLOW),
                   c.face(20, 14, 3)], react=R_ZZZ)
item("piano_pink", "Pink piano", C_FURN, S_HOME, 4, 3, 375,
     lambda c, f: [c.rect(10, 1, 12, 4, LWOOD), c.rrect(1, 4, 30, 14, 3, HOT), c.rect(3, 11, 26, 4, WHITE),
                   c.rect(3, 18, 2, 6, DWOOD), c.rect(27, 18, 2, 6, DWOOD), c.outline(),
                   [c.rect(5 + i * 4, 11, 2, 2, INK) for i in range(6)], c.face(16, 6, 3),
                   c.disc(27, 3 + f, 1.3, PURPLE), c.line(28, f, 28, 3 + f, PURPLE)], frames=2, react=R_NOTE)
item("dollhouse", "Dollhouse", C_FURN, S_HOME, 4, 4, 300,
     lambda c, f: [c.poly([(0, 12), (31, 12), (16, 0)], HOT), c.rect(3, 12, 26, 19, PEACH), c.rect(3, 21, 26, 1, LWOOD),
                   c.rect(6, 14, 7, 5, SKY), c.rect(19, 14, 7, 5, SKY), c.rect(6, 24, 5, 4, SKY),
                   c.rrect(14, 23, 6, 8, 2, BROWN), c.outline(), c.heart(16, 7, 2, WHITE), c.put(18, 27, YELLOW),
                   c.put(8, 15, WHITE), c.put(21, 15, WHITE)], react=R_WOW)
item("cloud_sofa", "Cloud sofa", C_FURN, S_HOME, 5, 2, 225,
     lambda c, f: [[c.disc(6 + i * 7, 6, 5, WHITE) for i in range(5)], c.rrect(1, 6, 38, 8, 4, WHITE),
                   c.rrect(5, 8, 30, 4, 2, LILAC), c.rect(5, 14, 2, 2, GOLD), c.rect(33, 14, 2, 2, GOLD), c.outline(),
                   c.face(20, 9, 3), c.shine(6, 3), c.shine(27, 3)])
item("carousel", "Carousel", C_TOY, S_HOME, 4, 4, 800,
     lambda c, f: [c.poly([(1, 10), (30, 10), (16, 0)], PINK), c.poly([(11, 10), (16, 0), (15, 10)], WHITE),
                   c.poly([(19, 10), (16, 0), (23, 10)], WHITE), c.rect(1, 10, 30, 3, HOT), c.rect(15, 13, 2, 15, GOLD),
                   c.rect(7, 13, 1, 15, GOLD), c.rect(24, 13, 1, 15, GOLD),
                   c.ellipse(8, 20 - 2 * f, 4, 2.5, WHITE), c.disc(4, 17 - 2 * f, 2, WHITE),
                   c.ellipse(24, 18 + 2 * f, 4, 2.5, MINT), c.disc(28, 15 + 2 * f, 2, MINT),
                   c.rrect(1, 27, 30, 5, 2, LAV), c.outline(), c.star(16, 2, 1.5, YELLOW), c.face(16, 6, 2),
                   c.put(3, 17 - 2 * f, INK), c.put(29, 15 + 2 * f, INK)], frames=2, react=R_WOW)
item("castle_toy", "Rainbow castle", C_TOY, S_HOME, 4, 4, 1000,
     lambda c, f: [c.rect(1, 7, 8, 24, LILAC), c.rect(23, 7, 8, 24, LILAC), c.poly([(0, 8), (10, 8), (5, 0)], HOT),
                   c.poly([(22, 8), (32, 8), (27, 0)], HOT), c.rect(8, 13, 16, 18, LAV),
                   [c.rect(8 + i * 4, 10, 2, 3, LAV) for i in range(4)], c.rrect(12, 22, 8, 9, 3, PURPLE),
                   c.rect(0, 30, 32, 2, MINT), c.outline(), c.face(16, 17, 3), c.star(16, 5, 2.5, YELLOW),
                   c.disc(5, 14, 1.5, YELLOW if f else LEMON), c.disc(27, 14, 1.5, LEMON if f else YELLOW),
                   c.put(12, 27, HOT), c.put(19, 27, SKY)], frames=2, react=R_WOW)
item("bunny_house", "Bunny cottage", C_PET, S_HOME, 3, 3, 325,
     lambda c, f: [c.poly([(0, 10), (23, 10), (12, 1)], HOT), c.rect(2, 10, 20, 13, CREAM),
                   c.rrect(7, 13, 10, 10, 4, BROWN)] +
                  ([c.disc(12, 19, 3.5, WHITE), c.rect(9, 12, 2, 5, WHITE), c.rect(13, 12, 2, 5, WHITE)] if f else []) +
                  [c.rect(1, 22, 22, 2, MOSS), c.outline(), c.heart(12, 6, 1.6, WHITE)] +
                  ([c.face(12, 19, 1.5)] if f else []), frames=2, react=R_HEART)
item("kitty_tower", "Kitty tower", C_PET, S_HOME, 2, 4, 275,
     lambda c, f: [c.rect(6, 8, 4, 22, LWOOD), c.ellipse(8, 6, 5, 3, PEACH), c.poly([(4, 5), (5, 1), (7, 4)], PEACH),
                   c.poly([(9, 4), (11, 1), (12, 5)], PEACH), c.rrect(1, 8, 14, 4, 2, PINK), c.rrect(1, 18, 14, 4, 2, PINK),
                   c.rrect(0, 28, 16, 4, 2, PINK), c.line(14, 22, 14, 25 + f, DGREY), c.outline(),
                   c.face(8, 6, 1.5, mouth=False), c.disc(14, 26 + f, 1.2, YELLOW)], frames=2, react=R_ZZZ)
style("wall_candy", "Candy stripes", C_WALL, S_HOME, 100, "STRIPES", HOT, WHITE)
style("floor_puff", "Puffy carpet", C_FLOOR, S_HOME, 125, "CARPET", LILAC, WHITE)
style("wall_galaxy", "Galaxy wall", C_WALL, S_HOME, 150, "STARS", NAVY, LAV, YELLOW)
style("floor_rose", "Rose tiles", C_FLOOR, S_HOME, 175, "TILES", ROSE, WHITE)
style("wall_goldbrick", "Gold castle", C_WALL, S_HOME, 200, "BRICKS", GOLD, LEMON)
item("squishy_gold", "Gold squishy", C_TOY, S_HOME, 2, 2, 150,
     lambda c, f: [c.ellipse(8, 9 + f, 6 + f, 6 - f, GOLD), c.outline(), c.face(8, 9 + f, 2), c.shine(4, 6 + f),
                   c.star(13, 3, 1.5, LEMON)], frames=2, react=R_HEART)
item("squishy_rainbow", "Rainbow squishy", C_TOY, S_HOME, 2, 2, 200,
     lambda c, f: [c.ellipse(8, 9 + f, 7, 6 - f, HOT), c.ellipse(8, 10 + f, 5.5, 4.5 - f, YELLOW),
                   c.ellipse(8, 11 + f, 4, 3 - f, MINT), c.ellipse(8, 12, 2.5, 1.5, SKY), c.outline(),
                   c.face(8, 8 + f, 2), c.shine(3, 6 + f)], frames=2, react=R_HEART)
item("squishy_star", "Star squishy", C_TOY, S_HOME, 2, 2, 250,
     lambda c, f: [c.ellipse(8, 9 + f, 6.5 + f, 6 - f, PURPLE), c.outline(), c.face(8, 9 + f, 2),
                   c.put(4, 6, WHITE), c.put(12, 7, YELLOW), c.put(6, 13, YELLOW), c.put(11, 12, WHITE),
                   c.star(13, 3, 1.5, YELLOW if f else LEMON)], frames=2, react=R_WOW)
item("squishy_giant", "Giant squishy", C_TOY, S_HOME, 3, 3, 400,
     lambda c, f: [c.ellipse(12, 14 + f, 11, 9 - f, PINK), c.ellipse(5, 6 + f, 2.5, 3, PINK), c.ellipse(19, 6 + f, 2.5, 3, PINK),
                   c.ellipse(12, 18 + f, 6, 3, ROSE), c.outline(), c.face(12, 13 + f, 3), c.shine(5, 10 + f),
                   c.heart(12, 18 + f, 1.6, HOT)], frames=2, react=R_HEART)

# Room styles referenced by the C code (default wall/floor of each room)

ROOM_DEFAULTS = [("bedroom", "wall_pink", "floor_honey"), ("garden", "wall_skyfence", "floor_grass"),
                 ("cafe", "wall_cafe", "floor_cafe"), ("treehouse", "wall_bark", "floor_attic"),
                 ("tower", "wall_castle", "floor_stone")]


# ---------------------------------------------------------------- render + pack
def render(it, fr):
    c = Canvas(it["aw"], it["ah"])
    it["draw"](c, fr)
    return c


def pack_items():
    """-> (records, pixel bytes). Each art item: up to 15 colours mapped to the master palette, 4 bits per pixel,
    frames one after the other; rows padded to whole bytes."""
    data = bytearray()
    recs = []
    for it in ITEMS:
        pal = [0] * 16
        off = len(data)
        if it["draw"] is not None:
            frames = [render(it, f) for f in range(it["frames"])]
            used = sorted({v for fr in frames for v in fr.flat()} - {T})
            assert len(used) <= 15, (it["key"], len(used))
            remap = {T: 0}
            for k, v in enumerate(used):
                remap[v] = k + 1
                pal[k + 1] = v
            for fr in frames:
                for row in fr.px:
                    vals = [remap[v] for v in row] + ([0] if len(row) % 2 else [])
                    for k in range(0, len(vals), 2):
                        data.append(vals[k] | vals[k + 1] << 4)
        elif "cols" in it:
            pal[0:3] = list(it["cols"])
        recs.append((it, off, pal))
    return recs, bytes(data)


def rgb565(c):
    r, g, b = c
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def c_ident(s):
    return "IT_" + s.upper()


def h_source():
    out = ["/* Generated by tools/gen_mq_items.py: ids and constants of the Math Quest home items.",
           " * Do not edit: change the catalogue in the tool and run `make mq-art`. */",
           "#ifndef MATHQUEST_MQ_ITEMS_H", "#define MATHQUEST_MQ_ITEMS_H", ""]
    out.append("#define MQ_NITEM %d /* items (ids 1..MQ_NITEM; 0 = none) */" % len(ITEMS))
    out.append("enum { %s, HM_NCAT };" % ", ".join("HC_" + c for c in CATS))
    out.append("enum { %s, HM_NSET };" % ", ".join("HS_" + s for s in SETS))
    out.append("enum { %s };" % ", ".join("%s = %d" % (k, v) for k, v in FLAGS.items()))
    out.append("enum { %s, HM_NREACT };" % ", ".join("RE_" + r for r in REACTS))
    out.append("enum { %s, HM_NSLOT };" % ", ".join("OS_" + s for s in SLOTS))
    out.append("enum { %s, HM_NPAT };" % ", ".join("PT_" + p for p in PATS))
    out.append("enum {")
    for k, it in enumerate(ITEMS):
        out.append("    %s = %d," % (c_ident(it["key"]), k + 1))
    out.append("};")
    out.append("#define HM_NROOM %d" % len(ROOM_DEFAULTS))
    out.append("")
    out.append("#endif")
    return "\n".join(out) + "\n"


def c_source():
    recs, data = pack_items()
    out = ["/* Generated by tools/gen_mq_items.py: the Math Quest home catalogue and its pixel art",
           " * (original designs; 4 bits per pixel, each item maps its 16 indices into mq_item_master).",
           " * Do not edit: change the catalogue in the tool and run `make mq-art`. */",
           '#include "mq.h"', "",
           "const uint16_t mq_item_master[32] = {" + ", ".join("0x%04X" % rgb565(c) for c in MASTER) + "};", "",
           "const mq_item_t mq_item[MQ_NITEM + 1] = {",
           '    {"", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0}},']
    for it, off, pal in recs:
        out.append('    {"%s", HC_%s, HS_%s, %d, %d, %d, %d, %d, %d, %d, %d, %d, %du, {%s}}, /* %s */' % (
            it["name"], CATS[it["cat"]], SETS[it["set"]], it["w"], it["h"], it["aw"], it["ah"], it["frames"],
            it["flags"], it["react"], it["extra"], it["price"], off, ",".join(str(v) for v in pal), it["key"]))
    out.append("};")
    out.append("")
    out.append("const uint8_t mq_room_default[HM_NROOM][2] = {%s};" % ", ".join(
        "{%s, %s}" % (c_ident(w), c_ident(f)) for _, w, f in ROOM_DEFAULTS))
    out.append("")
    out.append("const uint8_t mq_item_px[%d] = {" % max(len(data), 1))
    for k in range(0, len(data), 32):
        out.append("    " + ",".join("%d" % b for b in data[k:k + 32]) + ",")
    out.append("};")
    return "\n".join(out) + "\n"


def write_png(path, w, h, rgba):
    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def art_items():
    return [it for it in ITEMS if it["draw"] is not None]


def preview(path, scale=3):
    """Every art item (every frame) on a grid of 48 x 40 art-pixel cells, scaled up."""
    its = art_items()
    cw, ch, cols = 44, 36, 12
    cells = [(it, f) for it in its for f in range(it["frames"])]
    rows = (len(cells) + cols - 1) // cols
    W, H = cols * cw * scale, rows * ch * scale
    rgba = bytearray(W * H * 4)
    for i in range(W * H):
        rgba[i * 4:i * 4 + 4] = bytes((250, 246, 240, 255))
    for k, (it, f) in enumerate(cells):
        c = render(it, f)
        ox, oy = (k % cols) * cw + 2, (k // cols) * ch + 2
        for y in range(c.h):
            for x in range(c.w):
                v = c.px[y][x]
                if v == T:
                    continue
                for sy in range(scale):
                    for sx in range(scale):
                        i = (((oy + y) * scale + sy) * W + (ox + x) * scale + sx) * 4
                        rgba[i:i + 4] = bytes(list(MASTER[v]) + [255])
    write_png(path, W, H, rgba)


def sheet(d, limit=48):
    """A theme template: every art item's first frame in a 40 x 32 cell, plus a manifest naming them by key."""
    os.makedirs(d, exist_ok=True)
    its = art_items()
    cw, ch, cols = 40, 32, 10
    rows = (len(its) + cols - 1) // cols
    W, H = cols * cw, rows * ch
    rgba = [0] * (W * H * 4)
    man = {"title": "Home items", "cell": [20, 20], "sprites": [], "items": []}
    for k, it in enumerate(its):
        c = render(it, 0)
        ox, oy = (k % cols) * cw, (k // cols) * ch
        for y in range(c.h):
            for x in range(c.w):
                v = c.px[y][x]
                if v != T:
                    i = ((oy + y) * W + ox + x) * 4
                    rgba[i:i + 4] = list(MASTER[v]) + [255]
        if k < limit:  # a pack holds at most 48 item pictures: keep the ones you redraw
            man["items"].append({"item": it["key"], "rect": [ox, oy, c.w, c.h]})
    write_png(os.path.join(d, "items.png"), W, H, rgba)
    with open(os.path.join(d, "items.json"), "w") as f:
        json.dump(man, f, indent=1)
        f.write("\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--c")
    ap.add_argument("--h")
    ap.add_argument("--check-c")
    ap.add_argument("--check-h")
    ap.add_argument("--preview")
    ap.add_argument("--sheet")
    ap.add_argument("--max", type=int, default=48, help="items listed in the --sheet manifest")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    if a.c or a.check_c:
        src = c_source()
        if a.c:
            open(a.c, "w").write(src)
        if a.check_c and open(a.check_c).read() != src:
            sys.exit(f"{a.check_c} is out of date: run make mq-art")
    hs = h_source()
    if a.h:
        open(a.h, "w").write(hs)
    if a.check_h and open(a.check_h).read() != hs:
        sys.exit(f"{a.check_h} is out of date: run make mq-art")
    if a.preview:
        preview(a.preview)
    if a.sheet:
        sheet(a.sheet, a.max)
    if a.list:
        for k, it in enumerate(ITEMS):
            print(k + 1, it["key"], it["name"], CATS[it["cat"]], SETS[it["set"]], it["price"])


if __name__ == "__main__":
    main()
