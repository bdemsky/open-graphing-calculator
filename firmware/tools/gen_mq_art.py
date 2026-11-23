#!/usr/bin/env python3
"""Math Quest built-in art: the original cast and the stickers, drawn procedurally.

    gen_mq_art.py --c modules/mathquest/mq_art.c     the C tables of the module
    gen_mq_art.py --sheet DIR                        the cast as a theme sprite sheet
                                                     (sheet.png + theme.json, a template
                                                     for tools/make_theme.py)
    gen_mq_art.py --check modules/mathquest/mq_art.c fail if the C file is out of date

Every character is an ORIGINAL design (a sprout, a mail penguin, an axolotl baker,
an owl with glasses, a fox kit, a paint-splashed chick, a hedgehog, a snail racer
and a star sprite): round, soft pastel bodies with a dark plum outline, sparkly
eyes and blush. Sprites are 20 x 20 pixels indexed into a 16-colour palette
(index 0 = transparent); stickers are 12 x 12. The drawing is shape based
(discs, ellipses, polygons), followed by an outline pass around the body and
then the face, so new characters are a few lines each.
"""
import argparse
import json
import math
import os
import struct
import sys
import zlib

PAL = [(0, 0, 0), (90, 61, 92), (255, 255, 255), (255, 179, 199), (255, 122, 162), (168, 230, 207),
       (92, 191, 138), (201, 182, 255), (155, 127, 230), (159, 216, 255), (90, 169, 230), (255, 211, 165),
       (255, 164, 92), (255, 230, 128), (176, 133, 96), (255, 150, 170)]
T, INK, WHITE, PINK, HOT, MINT, GREEN, LAV, PURPLE, SKY, BLUE, PEACH, ORANGE, YELLOW, BROWN, BLUSH = range(16)
N = 20


class Canvas:
    def __init__(self, n=N):
        self.n = n
        self.px = [[T] * n for _ in range(n)]

    def put(self, x, y, c):
        if 0 <= x < self.n and 0 <= y < self.n:
            self.px[y][x] = c

    def ellipse(self, cx, cy, rx, ry, c):
        for y in range(self.n):
            for x in range(self.n):
                if ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2 <= 1.0:
                    self.px[y][x] = c

    def disc(self, cx, cy, r, c):
        self.ellipse(cx, cy, r, r, c)

    def poly(self, pts, c):
        for y in range(self.n):
            for x in range(self.n):
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

    def outline(self):
        out = [row[:] for row in self.px]
        for y in range(self.n):
            for x in range(self.n):
                if self.px[y][x] != T:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < self.n and 0 <= yy < self.n and self.px[yy][xx] not in (T, INK):
                        out[y][x] = INK
                        break
        self.px = out

    def face(self, cx=10, ey=11, gap=3, mouth=True, cheeks=True):
        for sx in (-1, 1):
            ex = cx + sx * gap - (1 if sx < 0 else 0)
            for dx in (0, 1):
                for dy in (0, 1):
                    self.put(ex + dx, ey + dy, INK)
            self.put(ex, ey, WHITE)  # sparkle
            if cheeks:
                self.put(ex + (-1 if sx < 0 else 2), ey + 2, BLUSH)
                self.put(ex + (-2 if sx < 0 else 3), ey + 2, BLUSH)
        if mouth:
            self.put(cx - 1, ey + 3, INK)
            self.put(cx, ey + 3, INK)

    def flat(self):
        return [c for row in self.px for c in row]


# ---------------------------------------------------------------- the cast (40 x 40, nine faces each)
# The cast is drawn in a 40 x 40 design space (body round the middle at (20, 24), the eyes on row
# 22: the proportions of the old 20 x 20 cast doubled, so outfits and hats still sit right) on a
# canvas of BIG (40) or SMALL (20) pixels: the same shapes, rasterised at two sizes (the small one
# for little places such as a row of friends). Every character has nine faces (EXPR): idle, blink,
# looking left and right, talking, happy (arms up), sad, wow and thinking (a hand at the chin).
BIG, SMALL = 40, 20
EXPR = ["idle", "blink", "lookl", "lookr", "talk", "happy", "sad", "wow", "think"]
(E_IDLE, E_BLINK, E_LOOKL, E_LOOKR, E_TALK, E_HAPPY, E_SAD, E_WOW, E_THINK) = range(len(EXPR))
# shades of the body colours (darker: the side away from the light) and the face's colours
(MINT_D, SKY_D, PINK_D, LAV_D, ORANGE_D, YELLOW_D, BROWN_D, PEACH_D, WHITE_D, MOUTH, TONGUE, CREAM, GOLD, RED,
 GREY, DARK) = range(16, 32)
PAL2 = PAL + [(128, 206, 176), (122, 188, 240), (240, 146, 176), (172, 150, 236), (238, 132, 70), (242, 202, 92),
              (140, 100, 72), (238, 182, 138), (228, 228, 242), (196, 66, 108), (255, 140, 160), (255, 248, 236),
              (240, 196, 80), (236, 82, 96), (206, 206, 220), (52, 40, 62)]
SHADE = {MINT: MINT_D, SKY: SKY_D, PINK: PINK_D, LAV: LAV_D, ORANGE: ORANGE_D, YELLOW: YELLOW_D, BROWN: BROWN_D,
         PEACH: PEACH_D, WHITE: WHITE_D}


class CastCanvas:
    """A canvas of n x n pixels drawn in the 40 x 40 design space (coordinates x n / 40). The
    primitives (ellipse, ellipse_on, poly, poly_on, put, outline) take canvas pixels; the design
    helpers (E, P, ...) take design units and call them, so a high-resolution twin
    (tools/gen_mq_art_hires.py) sees every shape exactly."""

    def __init__(self, n=BIG):
        self.n = n
        self.k = n / 40.0
        self.px = [[T] * n for _ in range(n)]

    # ---- primitives (canvas pixels)
    def put(self, x, y, c):
        x, y = int(x), int(y)
        if 0 <= x < self.n and 0 <= y < self.n:
            self.px[y][x] = c

    def _span(self, lo, hi):
        return range(max(0, int(math.floor(lo))), min(self.n, int(math.ceil(hi)) + 1))

    def ellipse(self, cx, cy, rx, ry, c):
        self.ellipse_on(cx, cy, rx, ry, c, None)

    def ellipse_on(self, cx, cy, rx, ry, c, on):
        """the ellipse, painted only over pixels of colour `on` (None: everywhere)"""
        if rx <= 0 or ry <= 0:
            return
        for y in self._span(cy - ry - 1, cy + ry + 1):
            for x in self._span(cx - rx - 1, cx + rx + 1):
                if ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2 <= 1.0 and (on is None or self.px[y][x] == on):
                    self.px[y][x] = c

    def poly(self, pts, c):
        self.poly_on(pts, c, None)

    def poly_on(self, pts, c, on):
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        for y in self._span(min(ys) - 1, max(ys) + 1):
            for x in self._span(min(xs) - 1, max(xs) + 1):
                px, py, inside = x + 0.5, y + 0.5, False
                j = len(pts) - 1
                for i in range(len(pts)):
                    xi, yi = pts[i]
                    xj, yj = pts[j]
                    if (yi > py) != (yj > py) and px < (xj - xi) * (py - yi) / (yj - yi) + xi:
                        inside = not inside
                    j = i
                if inside and (on is None or self.px[y][x] == on):
                    self.px[y][x] = c

    def outline(self, col=INK):
        out = [row[:] for row in self.px]
        for y in range(self.n):
            for x in range(self.n):
                if self.px[y][x] != T:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < self.n and 0 <= yy < self.n and self.px[yy][xx] not in (T, col):
                        out[y][x] = col
                        break
        self.px = out

    def flat(self):
        return [c for row in self.px for c in row]

    # ---- design units (40 x 40)
    def E(self, cx, cy, rx, ry, c, on=None):
        k = self.k
        if on is None:
            self.ellipse(cx * k, cy * k, rx * k, ry * k, c)
        else:
            self.ellipse_on(cx * k, cy * k, rx * k, ry * k, c, on)

    def D(self, cx, cy, r, c, on=None):
        self.E(cx, cy, r, r, c, on)

    def P(self, pts, c, on=None):
        k = self.k
        pts = [(x * k, y * k) for x, y in pts]
        if on is None:
            self.poly(pts, c)
        else:
            self.poly_on(pts, c, on)

    def R(self, cx, cy, rx, ry, deg, c, on=None):
        """an ellipse turned by deg"""
        a = math.radians(deg)
        pts = [(cx + rx * math.cos(t) * math.cos(a) - ry * math.sin(t) * math.sin(a),
                cy + rx * math.cos(t) * math.sin(a) + ry * math.sin(t) * math.cos(a))
               for t in [i * 2 * math.pi / 20 for i in range(20)]]
        self.P(pts, c, on)

    def arc(self, cx, cy, r, a0, a1, w, c, ry=None):
        """a band along an arc (degrees, y down: 0 right, 90 below), w thick"""
        ry = r if ry is None else ry
        n = 10
        outer = [(cx + (r + w / 2) * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
                  cy + (ry + w / 2) * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]
        inner = [(cx + (r - w / 2) * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
                  cy + (ry - w / 2) * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in reversed(range(n + 1))]
        self.P(outer + inner, c)

    def bar(self, x0, y0, x1, y1, w, c):
        """a straight band from (x0, y0) to (x1, y1), w thick"""
        dx, dy = x1 - x0, y1 - y0
        ln = math.hypot(dx, dy) or 1
        nx, ny = -dy / ln * w / 2, dx / ln * w / 2
        self.P([(x0 + nx, y0 + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny), (x0 - nx, y0 - ny)], c)

    def shade(self, cx, cy, rx, ry, base):
        """the body's shading: the lower right a shade darker, a shine at the upper left"""
        sh = SHADE.get(base)
        if sh is None:
            return
        self.E(cx, cy, rx + 0.5, ry + 0.5, sh, on=base)
        self.E(cx - 1.4, cy - 1.8, rx - 0.6, ry - 0.9, base, on=sh)
        self.E(cx - rx * 0.42, cy - ry * 0.55, rx * 0.2, ry * 0.13, WHITE, on=base)


def limbs(c, ex, col, cx=20, cy=24, r=14.0, feet=True, arms=True):
    """feet (under the body: draw first) or arms (on top) in the body's shade"""
    sh = SHADE.get(col, col)
    if feet:
        c.E(cx - 6.2, cy + r - 1.2, 3.8, 2.4, sh)
        c.E(cx + 6.2, cy + r - 1.2, 3.8, 2.4, sh)
    if arms:
        if ex in (E_HAPPY, E_WOW):  # both arms up
            c.R(cx - r + 0.6, cy - 4.5, 2.4, 4.4, 35, sh)
            c.R(cx + r - 0.6, cy - 4.5, 2.4, 4.4, -35, sh)
        elif ex == E_THINK:  # a hand at the chin
            c.R(cx - r + 1.2, cy + 3.5, 2.4, 4.0, 25, sh)
            c.R(cx + 4.5, cy + 6.5, 2.6, 3.6, -50, sh)
        elif ex == E_SAD:  # arms hanging
            c.R(cx - r + 2.2, cy + 5.0, 2.2, 3.8, 10, sh)
            c.R(cx + r - 2.2, cy + 5.0, 2.2, 3.8, -10, sh)
        elif ex == E_TALK:  # one arm out, telling
            c.R(cx - r + 1.0, cy + 3.5, 2.4, 4.0, 30, sh)
            c.R(cx + r - 0.2, cy - 0.5, 2.4, 4.2, -70, sh)
        else:
            c.R(cx - r + 1.0, cy + 3.5, 2.4, 4.0, 30, sh)
            c.R(cx + r - 1.0, cy + 3.5, 2.4, 4.0, -30, sh)


def eyes(c, ex, cx=20, ey=22, gap=6.0, size=1.0):
    """the eyes of face ex: dark ovals with a sparkle, closed arcs, looking about"""
    small = c.n < 30
    rx, ry = 2.3 * size, 2.9 * size
    for sx in (-1, 1):
        x = cx + sx * gap
        if ex == E_BLINK:
            c.arc(x, ey + 0.4, 2.5 * size, 15, 165, 1.3 if not small else 2.0, INK, ry=1.4 * size)
            continue
        if ex == E_HAPPY:
            c.arc(x, ey + 1.6, 2.6 * size, 200, 340, 1.5 if not small else 2.0, INK, ry=2.4 * size)
            continue
        dx = dy = 0.0
        r1, r2 = rx, ry
        if ex == E_LOOKL:
            dx = -1.3
        elif ex == E_LOOKR:
            dx = 1.3
        elif ex == E_THINK:
            dx, dy = 1.0, -1.0
            r1, r2 = rx * 0.9, ry * 0.9
        elif ex == E_SAD:
            dy = 0.8
            r1, r2 = rx * 0.9, ry * 0.85
        elif ex == E_WOW:
            r1, r2 = rx * 1.15, ry * 1.2
        c.E(x + dx, ey + dy, r1, r2, INK)
        if small:
            c.E(x + dx - 0.6, ey + dy - 1.0, 0.9, 0.9, WHITE)
        else:
            c.E(x + dx - 0.75, ey + dy - 1.05, 0.95, 1.0, WHITE)
            c.D(x + dx + 0.85, ey + dy + 1.1, 0.48, WHITE)
        if ex == E_SAD and not small:  # worried brows, the inner ends up
            c.bar(x - sx * 2.6, ey - 4.2, x + sx * 1.6, ey - 5.6, 0.9, INK)


def cheeks(c, cx=20, ey=22, gap=6.0, wide=1.0):
    for sx in (-1, 1):
        c.E(cx + sx * (gap + 1.8 * wide), ey + 3.8, 2.1, 1.25, BLUSH)


def mouth(c, ex, cx=20, my=26.5, w=1.0):
    """the mouth of face ex (characters with a beak have their own)"""
    small = c.n < 30
    if ex in (E_IDLE, E_BLINK, E_LOOKL, E_LOOKR):
        c.arc(cx, my - 1.4, 1.9 * w, 25, 155, 1.0 if not small else 1.6, INK, ry=1.5)
    elif ex == E_TALK:
        c.E(cx, my, 2.3 * w, 2.0, INK)
        c.E(cx, my + 0.1, 1.6 * w, 1.3, MOUTH)
        c.E(cx, my + 0.9, 1.1 * w, 0.6, TONGUE)
    elif ex == E_HAPPY:
        pts = [(cx - 3.3 * w, my - 1.2)] + [(cx + 3.3 * w * math.cos(math.radians(a)), my - 1.2 + 3.0 * math.sin(math.radians(a)))
                                            for a in range(10, 171, 20)] + [(cx + 3.3 * w, my - 1.2)]
        c.P(pts, INK)
        inner = [(cx - 2.4 * w, my - 0.6)] + [(cx + 2.4 * w * math.cos(math.radians(a)), my - 0.6 + 2.0 * math.sin(math.radians(a)))
                                              for a in range(10, 171, 20)] + [(cx + 2.4 * w, my - 0.6)]
        c.P(inner, MOUTH)
        c.E(cx, my + 0.9, 1.4 * w, 0.7, TONGUE)
    elif ex == E_SAD:
        c.arc(cx, my + 0.6, 1.8 * w, 205, 335, 1.0 if not small else 1.6, INK, ry=1.3)
    elif ex == E_WOW:
        c.E(cx, my + 0.4, 1.6 * w, 2.0, INK)
        c.E(cx, my + 0.5, 0.95 * w, 1.3, MOUTH)
    elif ex == E_THINK:
        c.bar(cx + 0.2, my, cx + 2.6 * w, my - 0.7, 1.0 if not small else 1.6, INK)


def beak(c, ex, cx=20, by=26.0, col=ORANGE, w=1.0):
    """a little beak: closed, or open when talking, happy or amazed"""
    if ex in (E_TALK, E_HAPPY, E_WOW):
        c.P([(cx - 2.4 * w, by - 1.4), (cx + 2.4 * w, by - 1.4), (cx, by + 0.6)], col)
        c.P([(cx - 1.9 * w, by + 0.6), (cx + 1.9 * w, by + 0.6), (cx, by + 3.0)], col)
        c.E(cx, by + 0.7, 1.4 * w, 0.8, MOUTH)
    else:
        c.P([(cx - 2.4 * w, by - 1.2), (cx + 2.4 * w, by - 1.2), (cx, by + 1.9)], col)
    c.E(cx - 0.8 * w, by - 0.6, 0.6, 0.35, WHITE)


def face(c, ex, cx=20, ey=22, gap=6.0, my=26.5, blush=True, has_mouth=True, size=1.0, w=1.0):
    if blush:
        cheeks(c, cx, ey, gap)
    eyes(c, ex, cx, ey, gap, size)
    if has_mouth:
        mouth(c, ex, cx, my, w)


def body(c, ex, col, cx=20, cy=24, r=14.4, feet=True):
    limbs(c, ex, col, cx, cy, r, feet=feet, arms=False)
    c.D(cx, cy, r, col)


def finish(c, ex, col, cx=20, cy=24, r=14.4, arms=True):
    """shading, arms, the outline"""
    c.shade(cx, cy, r, r, col)
    if arms:
        limbs(c, ex, col, cx, cy, r, feet=False)


def pip(ex, n=BIG):  # a mint sprout bean with two leaves on a stem
    c = CastCanvas(n)
    body(c, ex, MINT)
    c.bar(20, 10.5, 20.6, 4.5, 1.6, GREEN)
    c.R(14.4, 5.4, 5.6, 2.8, -18, GREEN)
    c.R(26.2, 4.6, 6.0, 3.0, 14, GREEN)
    c.R(14.0, 5.2, 3.6, 1.0, -18, MINT_D, on=GREEN)
    c.R(26.4, 4.4, 3.8, 1.0, 14, MINT_D, on=GREEN)
    finish(c, ex, MINT)
    c.E(20, 31.5, 6.5, 4.6, CREAM, on=MINT)  # a pale tummy
    c.outline()
    face(c, ex)
    return c


def dot(ex, n=BIG):  # a sky-blue mail penguin with a cap and a satchel
    c = CastCanvas(n)
    body(c, ex, SKY)
    finish(c, ex, SKY, arms=False)
    c.E(20, 29.5, 9.4, 8.6, WHITE)
    c.E(20, 31.5, 8.6, 6.4, WHITE_D, on=WHITE)
    c.E(20, 29.0, 8.6, 7.4, WHITE, on=WHITE_D)
    c.P([(9.0, 15.0), (31.0, 15.0), (28.6, 8.8), (11.4, 8.8)], PURPLE)  # the cap
    c.P([(19.0, 15.0), (35.4, 15.0), (35.0, 17.2), (19.0, 17.2)], LAV_D)  # its peak
    c.D(20, 9.4, 2.0, YELLOW)
    limbs(c, ex, SKY)
    c.P([(26.0, 30.0), (34.6, 30.0), (34.0, 37.0), (26.6, 37.0)], PURPLE)  # the satchel
    c.P([(26.0, 30.0), (34.6, 30.0), (33.4, 33.0), (27.2, 33.0)], LAV_D)
    c.D(30.3, 33.0, 0.9, YELLOW)
    c.outline()
    face(c, ex, ey=21, has_mouth=False)
    beak(c, ex, by=26.4)
    return c


def momo(ex, n=BIG):  # a pink axolotl baker with frilly gills and an apron
    c = CastCanvas(n)
    for sx in (-1, 1):
        for (dx, dy, a) in ((14.0, -6.4, 35), (15.2, -0.6, 8), (14.2, 5.2, -22)):
            c.R(20 + sx * dx, 22 + dy, 3.6, 1.9, a * sx, HOT)
            c.R(20 + sx * (dx + 0.9), 22 + dy, 1.7, 0.75, a * sx, PINK, on=HOT)
    body(c, ex, PINK, r=13.6)
    finish(c, ex, PINK, r=13.6)
    c.E(20, 33.0, 7.6, 5.2, WHITE, on=PINK)  # the apron
    c.E(20, 33.0, 7.6, 5.2, WHITE, on=PINK_D)
    c.bar(12.6, 28.6, 27.4, 28.6, 1.2, HOT)
    c.D(18.9, 32.6, 1.25, HOT, on=WHITE)  # a heart on the pocket
    c.D(21.1, 32.6, 1.25, HOT, on=WHITE)
    c.P([(17.6, 33.0), (22.4, 33.0), (20.0, 35.6)], HOT, on=WHITE)
    c.outline()
    face(c, ex, gap=5.8)
    return c


def olli(ex, n=BIG):  # a lavender owl with ear tufts and round glasses
    c = CastCanvas(n)
    limbs(c, ex, LAV, arms=False)
    c.P([(7.0, 17.0), (8.6, 4.4), (16.0, 11.0)], LAV)
    c.P([(33.0, 17.0), (31.4, 4.4), (24.0, 11.0)], LAV)
    c.D(20, 24, 14.4, LAV)
    finish(c, ex, LAV, arms=False)
    c.E(20, 31.0, 7.4, 5.6, PEACH)
    for (x, y) in ((17.0, 29.6), (23.0, 29.6), (20.0, 32.4)):  # feathers
        c.arc(x, y, 1.6, 20, 160, 0.8, PEACH_D, ry=1.0)
    limbs(c, ex, LAV, feet=False)
    c.outline()
    for sx in (-1, 1):  # the glasses
        c.E(20 + sx * 6.4, 20.6, 5.2, 5.0, PURPLE)
        c.E(20 + sx * 6.4, 20.6, 4.0, 3.8, CREAM)
    c.bar(18.2, 19.6, 21.8, 19.6, 1.1, PURPLE)
    face(c, ex, ey=20.6, gap=6.4, has_mouth=False, blush=False, size=0.85)
    for sx in (-1, 1):
        c.E(20 + sx * 10.2, 26.4, 1.8, 1.1, BLUSH)
    beak(c, ex, by=25.4, w=0.85)
    return c


def tango(ex, n=BIG):  # an orange fox kit with a fluffy tail
    c = CastCanvas(n)
    c.R(33.4, 29.0, 4.8, 7.6, -25, ORANGE)  # the tail
    c.R(35.0, 34.6, 2.6, 2.6, -25, WHITE)
    body(c, ex, ORANGE, r=13.8)
    c.P([(7.6, 19.0), (8.6, 4.0), (18.0, 12.6)], ORANGE)
    c.P([(32.4, 19.0), (31.4, 4.0), (22.0, 12.6)], ORANGE)
    finish(c, ex, ORANGE, r=13.8)
    c.outline()
    c.P([(10.2, 15.0), (10.8, 8.0), (15.6, 12.4)], PEACH)
    c.P([(29.8, 15.0), (29.2, 8.0), (24.4, 12.4)], PEACH)
    c.E(20, 29.6, 7.4, 5.2, WHITE)  # the muzzle
    c.E(13.0, 25.6, 3.4, 2.4, WHITE)
    c.E(27.0, 25.6, 3.4, 2.4, WHITE)
    face(c, ex, my=28.0)
    c.E(20, 25.4, 1.4, 1.0, INK)  # the nose
    return c


def dabble(ex, n=BIG):  # a yellow chick painter with a beret and paint splashes
    c = CastCanvas(n)
    body(c, ex, YELLOW)
    finish(c, ex, YELLOW)
    c.E(20, 9.6, 9.0, 3.8, HOT)  # the beret
    c.E(20, 8.4, 7.6, 2.4, PINK, on=HOT)
    c.bar(20.4, 6.2, 21.2, 3.6, 1.4, HOT)
    c.D(10.6, 30.0, 1.6, SKY, on=YELLOW)  # paint splashes
    c.D(29.2, 31.6, 1.5, MINT, on=YELLOW)
    c.D(27.6, 13.6, 1.3, HOT, on=YELLOW)
    c.outline()
    face(c, ex, has_mouth=False)
    beak(c, ex, by=26.6)
    return c


def bramble(ex, n=BIG):  # a hedgehog with a peach face and a red nose
    c = CastCanvas(n)
    spikes = [(4.0, 27.0)]
    for i in range(13):
        a = math.pi * (1.0 - i / 12.0)
        r = 18.6 if i % 2 == 0 else 13.2
        spikes.append((20 + r * math.cos(a), 25 - r * math.sin(a) * 0.95))
    spikes.append((36.0, 27.0))
    limbs(c, ex, BROWN, arms=False)
    c.P(spikes, BROWN)
    c.D(20, 25, 14.0, BROWN)
    c.shade(20, 25, 14.0, 14.0, BROWN)
    limbs(c, ex, PEACH, cy=25, r=14.0, feet=False)
    c.outline()
    c.E(20, 27.2, 10.6, 9.4, PEACH)
    face(c, ex, ey=24.4, gap=5.4, my=30.0)
    c.E(20, 27.4, 1.5, 1.2, RED)
    return c


def shelly(ex, n=BIG):  # a snail racer with a swirly shell and goggles
    c = CastCanvas(n)
    c.E(19, 33.4, 17.6, 4.6, MINT)  # the foot
    c.D(9.6, 24.0, 7.6, MINT)  # the head
    c.bar(6.4, 18.0, 4.8, 8.4, 1.8, MINT)  # stalks
    c.bar(12.0, 18.0, 13.0, 8.4, 1.8, MINT)
    c.D(4.8, 8.4, 2.2, MINT)
    c.D(13.0, 8.4, 2.2, MINT)
    c.D(27.4, 21.0, 11.0, PINK)  # the shell
    c.shade(27.4, 21.0, 11.0, 11.0, PINK)
    c.E(19, 33.8, 17.0, 3.6, MINT_D, on=MINT)
    c.E(19, 33.2, 16.4, 3.0, MINT, on=MINT_D)
    c.outline()
    c.arc(27.4, 21.0, 7.0, -90, 180, 1.4, HOT)  # the swirl
    c.arc(26.4, 22.0, 4.0, 180, 450, 1.4, HOT)
    c.arc(27.4, 21.0, 1.6, 90, 300, 1.4, HOT)
    c.bar(2.8, 15.6, 16.4, 15.6, 1.6, PURPLE)  # goggles on the head
    c.D(6.6, 15.6, 2.2, SKY)
    c.D(12.6, 15.6, 2.2, SKY)
    face(c, ex, cx=9.8, ey=22.6, gap=3.4, my=27.4, size=0.8, w=0.8, blush=False)
    c.E(4.8, 26.0, 1.4, 0.9, BLUSH)
    c.E(14.8, 26.0, 1.4, 0.9, BLUSH)
    return c


def nova(ex, n=BIG):  # a star sprite with a sparkle
    c = CastCanvas(n)
    pts = []
    for i in range(10):
        r = 18.8 if i % 2 == 0 else 9.6
        a = -math.pi / 2 + i * math.pi / 5
        pts.append((20 + r * math.cos(a), 22.4 + r * math.sin(a)))
    c.P(pts, YELLOW)
    c.D(20, 23.4, 9.6, YELLOW)
    c.shade(20, 23.4, 10.6, 10.6, YELLOW)
    limbs(c, ex, YELLOW, cy=23, r=13.6, feet=False, arms=ex in (E_HAPPY, E_WOW, E_THINK))
    c.outline()
    face(c, ex, ey=21.6, gap=4.8, my=26.2, size=0.9, w=0.9)
    c.P([(33.6, 6.0), (34.6, 8.4), (37.0, 9.4), (34.6, 10.4), (33.6, 12.8), (32.6, 10.4), (30.2, 9.4), (32.6, 8.4)],
        WHITE)
    return c


CAST = [("Pip", pip), ("Dot", dot), ("Momo", momo), ("Olli", olli), ("Tango", tango), ("Dabble", dabble),
        ("Bramble", bramble), ("Shelly", shelly), ("Nova", nova)]


def sticker(kind):
    import math
    c = Canvas(12)
    if kind == "heart":
        c.disc(4, 4.6, 2.8, HOT), c.disc(8, 4.6, 2.8, HOT), c.poly([(1.2, 5.4), (10.8, 5.4), (6, 11)], HOT)
    elif kind == "star":
        c.poly([(6 + 5.6 * math.cos(-math.pi / 2 + i * math.pi / 5) * (1 if i % 2 == 0 else 0.45),
                 6.4 + 5.6 * math.sin(-math.pi / 2 + i * math.pi / 5) * (1 if i % 2 == 0 else 0.45)) for i in range(10)],
               YELLOW)
    elif kind == "flower":
        for i in range(5):
            a = i * 2 * math.pi / 5
            c.disc(6 + 3 * math.cos(a), 6 + 3 * math.sin(a), 2.2, PINK)
        c.disc(6, 6, 1.6, YELLOW)
    elif kind == "rainbow":
        for r, col in ((5.6, HOT), (4.6, ORANGE), (3.6, YELLOW), (2.6, MINT), (1.6, SKY)):
            c.ellipse(6, 9, r, r, col)
        c.poly([(0, 9), (12, 9), (12, 12), (0, 12)], T)
    elif kind == "moon":
        c.disc(6, 6, 5, YELLOW), c.disc(8.4, 4.6, 4, T)
    elif kind == "cupcake":
        c.poly([(2.5, 7), (9.5, 7), (8.5, 11), (3.5, 11)], SKY)
        c.disc(4.2, 6, 2.2, PINK), c.disc(7.8, 6, 2.2, PINK), c.disc(6, 4.4, 2.4, PINK), c.disc(6, 1.8, 1.1, HOT)
    elif kind == "strawberry":
        c.ellipse(6, 7, 4.4, 4.2, HOT), c.poly([(2, 4), (10, 4), (6, 11.5)], HOT)
        c.poly([(3, 2.6), (9, 2.6), (6, 4.8)], GREEN)
    elif kind == "cloud":
        c.disc(4, 7, 2.8, WHITE), c.disc(7, 5.4, 3.2, WHITE), c.disc(9, 7.6, 2.4, WHITE), c.ellipse(6, 8.6, 5, 1.8, WHITE)
    elif kind == "icecream":
        c.poly([(3.4, 6), (8.6, 6), (6, 11.6)], PEACH), c.disc(6, 4.4, 3.2, LAV)
    elif kind == "balloon":
        c.ellipse(6, 4.6, 3.8, 4.4, SKY), c.poly([(5.4, 8.6), (6.6, 8.6), (6, 9.8)], SKY)
    elif kind == "gem":
        c.poly([(2, 4.4), (4, 1.8), (8, 1.8), (10, 4.4), (6, 10.6)], SKY)
    elif kind == "mushroom":
        c.ellipse(6, 5.2, 5.4, 4.0, HOT), c.poly([(0, 5.4), (12, 5.4), (12, 12), (0, 12)], T)
        c.poly([(4.4, 5.4), (7.6, 5.4), (7.8, 10.6), (4.2, 10.6)], PEACH)
    elif kind == "leaf":
        c.ellipse(6, 6, 5, 2.8, GREEN)
    elif kind == "shell":
        c.poly([(1.6, 4.6), (6, 1.2), (10.4, 4.6), (7.4, 10.6), (4.6, 10.6)], PEACH)
    elif kind == "sun":
        for i in range(8):
            a = i * math.pi / 4
            c.disc(6 + 4.6 * math.cos(a), 6 + 4.6 * math.sin(a), 1.0, ORANGE)
        c.disc(6, 6, 3.4, YELLOW)
    elif kind == "cherry":
        c.disc(3.6, 8.4, 2.4, HOT), c.disc(8.4, 8.6, 2.4, HOT)
    c.outline()
    if kind == "cherry":
        for i in range(5):
            c.put(4 + i // 2, 5 - i, GREEN), c.put(8 - i // 3, 5 - i, GREEN)
    if kind == "mushroom":
        c.put(4, 3, WHITE), c.put(8, 4, WHITE), c.put(6, 2, WHITE)
    if kind == "strawberry":
        for (x, y) in ((4, 6), (7, 6), (5, 8), (8, 8), (6, 10)):
            c.put(x, y, YELLOW)
    if kind == "leaf":
        for x in range(2, 10):
            c.put(x, 6, MINT)
    if kind == "balloon":
        for y in range(10, 12):
            c.put(6, y, INK)
    if kind in ("cloud", "moon", "star", "heart"):
        c.put(5, 6, INK), c.put(7, 6, INK), c.put(6, 7, INK)  # a tiny smile
    return c


STICKERS = ["heart", "star", "flower", "rainbow", "moon", "cupcake", "strawberry", "cloud", "icecream", "balloon",
            "gem", "mushroom", "leaf", "shell", "sun", "cherry"]


def rgb565(c):
    r, g, b = c
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def rle(px, w, x0, y0, pw, ph):
    """a rectangle of a picture: x, y, w, h, then (run, value) pairs (runs of 1..255), row by row"""
    out = [x0, y0, pw, ph]
    vals = [px[(y0 + y) * w + x0 + x] for y in range(ph) for x in range(pw)]
    i = 0
    while i < len(vals):
        j = i
        while j < len(vals) and vals[j] == vals[i] and j - i < 255:
            j += 1
        out += [j - i, vals[i]]
        i = j
    return out


def frames(n):
    """[slot][expr] -> the RLE bytes: the idle face whole, the others as the rectangle that differs"""
    allf = []
    for name, fn in CAST:
        base = fn(E_IDLE, n).flat()
        row = [rle(base, n, 0, 0, n, n)]
        for ex in range(1, len(EXPR)):
            px = fn(ex, n).flat()
            d = [i for i in range(n * n) if px[i] != base[i]]
            if not d:
                row.append([0, 0, 0, 0])
                continue
            xs, ys = [i % n for i in d], [i // n for i in d]
            row.append(rle(px, n, min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1))
        allf.append(row)
    return allf


# the theme-pack animation each face is in the built-in sheet (gen_mq_art.py --sheet; make_theme.py ANIMS)
SHEET_ANIM = [("idle", 0), ("blink", 0), ("look", 0), ("look", 1), ("talk", 0), ("happy", 0), ("pout", 0), ("wow", 0),
              ("think", 0)]


def c_source():
    out = ["/* Generated by tools/gen_mq_art.py: the Math Quest built-in cast and stickers",
           " * (original characters: 40 x 40 and 20 x 20 pixels, nine faces each, run-length coded;",
           " * stickers 12 x 12; indices into mq_art_pal, 0 = transparent).",
           " * Do not edit: change the drawing code and run `make mq-art`. */",
           '#include "mq.h"', "",
           "const uint16_t mq_art_pal[32] = {" + ", ".join("0x%04X" % rgb565(c) for c in PAL2) + "};", ""]
    out.append("const char mq_art_cast_name[MQ_NCAST][12] = {" + ", ".join('"%s"' % n for n, _ in CAST) + "};")
    out.append("")
    data, offs = [], []
    for n in (BIG, SMALL):
        fr = frames(n)
        o = []
        for row in fr:
            orow = []
            for f in row:
                orow.append(len(data))
                data += f
            o.append(orow)
        offs.append(o)
    out.append("/* a face: x, y, w, h of its rectangle (the idle face: the whole picture; the others: what")
    out.append(" * differs from it), then (run, value) pairs row by row */")
    out.append("const uint8_t mq_art_rle[%d] = {" % len(data))
    for i in range(0, len(data), 32):
        out.append("    " + ",".join("%d" % v for v in data[i:i + 32]) + ",")
    out.append("};")
    out.append("const uint32_t mq_art_face[2][MQ_NCAST][EX_COUNT] = {")
    for o in offs:
        out.append("    {")
        for name, orow in zip([n for n, _ in CAST], o):
            out.append("        /* %s */ {%s}," % (name, ", ".join("%d" % v for v in orow)))
        out.append("    },")
    out.append("};")
    out.append("")
    out.append("const char mq_art_sticker_name[MQ_NSTICKER][12] = {" + ", ".join('"%s"' % s for s in STICKERS) + "};")
    out.append("const uint8_t mq_art_sticker[MQ_NSTICKER][ART_STK * ART_STK] = {")
    for s in STICKERS:
        px = sticker(s).flat()
        out.append("    {" + ",".join("%d" % v for v in px) + "},")
    out.append("};")
    return "\n".join(out) + "\n"


def write_png(path, w, h, rgba):
    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def sheet(d):
    """the built-in cast as a theme sheet: one row per character, its nine faces as animations"""
    os.makedirs(d, exist_ok=True)
    N = BIG
    cols, rows = len(EXPR), len(CAST)
    w, h = cols * N, rows * N
    rgba = [0] * (w * h * 4)
    sprites = []
    for k, (name, fn) in enumerate(CAST):
        for ex in range(len(EXPR)):
            px = fn(ex, N).flat()
            ox, oy = ex * N, k * N
            for y in range(N):
                for x in range(N):
                    v = px[y * N + x]
                    i = ((oy + y) * w + ox + x) * 4
                    if v:
                        rgba[i:i + 4] = list(PAL2[v]) + [255]
            an, fr = SHEET_ANIM[ex]
            e = {"cell": [ex, k], "slot": k}
            if ex == 0:
                e["name"] = name
            else:
                e.update({"anim": an, "frame": fr})
            sprites.append(e)
    write_png(os.path.join(d, "sheet.png"), w, h, rgba)
    man = {"title": "Pastel friends", "cell": [N, N], "sprites": sprites, "worlds": {}}
    with open(os.path.join(d, "theme.json"), "w") as f:
        json.dump(man, f, indent=2)
        f.write("\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--c")
    ap.add_argument("--check")
    ap.add_argument("--sheet")
    a = ap.parse_args()
    src = c_source()
    if a.c:
        with open(a.c, "w") as f:
            f.write(src)
    if a.check:
        if open(a.check).read() != src:
            sys.exit(f"{a.check} is out of date: run make mq-art")
    if a.sheet:
        sheet(a.sheet)


if __name__ == "__main__":
    main()
