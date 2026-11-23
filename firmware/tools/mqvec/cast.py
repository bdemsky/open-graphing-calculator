"""The cast and the stickers as vector art.

The nine friends are drawn here in the 40 x 40 design space of tools/gen_mq_art.py (the body round
(20, 24), the eyes on row 22), so a vector face sits exactly where the pixel face does and the
module's animation transforms (squash, shear, hops, mirroring) apply to it unchanged. Each friend
is built from layers -- feet, body (gradient, a soft shade on the side away from the light, a
gloss), their own parts (ears, leaves, gills, cap, shell...), arms, the face -- and the nine faces
(idle, blink, looking left / right, talk, happy, sad, wow, think) change only the face and the
arms, so the poses flow into each other. The group ids in the SVG name the layers.
"""
import math

import gen_mq_art as GA
from . import svg as V

P = GA.PAL2
INK, WHITE, PINK, HOT, MINT, GREEN, LAV, PURPLE, SKY, BLUE, PEACH, ORANGE, YELLOW, BROWN, BLUSH = (P[i] for i in range(1, 16))
(MINT_D, SKY_D, PINK_D, LAV_D, ORANGE_D, YELLOW_D, BROWN_D, PEACH_D, WHITE_D, MOUTH, TONGUE, CREAM, GOLD, RED, GREY,
 DARK) = (P[i] for i in range(16, 32))
SHADE = {MINT: MINT_D, SKY: SKY_D, PINK: PINK_D, LAV: LAV_D, ORANGE: ORANGE_D, YELLOW: YELLOW_D, BROWN: BROWN_D,
         PEACH: PEACH_D, WHITE: WHITE_D}
EXPR = GA.EXPR
PAD = 2.0  # design units around the 40 x 40 cell (outlines and arms reach past it)
OW = 0.62  # outline, outside the shape


def new():
    s = V.Svg(40, 40, ow=OW, ink=INK, vb=(-PAD, -PAD, 40 + 2 * PAD, 40 + 2 * PAD))
    s.light, s.dark = 0.38, 0.1
    return s


def rot_ellipse(s, cx, cy, rx, ry, deg, rgb, **kw):
    return s.ellipse(cx, cy, rx, ry, rgb, rot=deg, **kw)


def bar(s, x0, y0, x1, y1, w, rgb):
    return s.line(x0, y0, x1, y1, rgb, w=w)


def shade_body(s, body, cx, cy, rx, ry, base):
    """the side away from the light a shade deeper (clipped to the body), a gloss top left"""
    sh = SHADE.get(base, V.deeper(base, 0.25))
    st = s.group_start()
    g = s.rgrad(sh, sh, cx=0.72, cy=0.78, r=0.62, o0=0.0, o1=0.85)
    s.raw("<ellipse cx=\"%s\" cy=\"%s\" rx=\"%s\" ry=\"%s\" fill=\"%s\"/>" % (
        V.f(cx), V.f(cy), V.f(rx + 0.6), V.f(ry + 0.6), s.rgrad(sh, sh, cx=0.38, cy=0.32, r=0.75, o0=0.0, o1=0.75)))
    s.group_end(st, kind="clip", clip=body if isinstance(body, list) else [body])
    del g
    s.gloss(cx - rx * 0.42, cy - ry * 0.56, rx * 0.26, ry * 0.15, rot=-28, op=0.75)
    s.shine(cx - rx * 0.12, cy - ry * 0.74, max(0.5, rx * 0.055), 0.9)


def feet(s, col, cx=20, cy=24, r=14.0):
    sh = SHADE.get(col, col)
    s.ellipse(cx - 6.2, cy + r - 1.2, 3.8, 2.4, sh)
    s.ellipse(cx + 6.2, cy + r - 1.2, 3.8, 2.4, sh)


def arms(s, ex, col, cx=20, cy=24, r=14.0):
    """the arms of face ex, outlined on their own (on top of the body)"""
    sh = SHADE.get(col, col)
    st = s.group_start()
    if ex in ("happy", "wow"):
        rot_ellipse(s, cx - r + 0.6, cy - 4.5, 2.4, 4.4, 35, sh)
        rot_ellipse(s, cx + r - 0.6, cy - 4.5, 2.4, 4.4, -35, sh)
    elif ex == "think":
        rot_ellipse(s, cx - r + 1.2, cy + 3.5, 2.4, 4.0, 25, sh)
        rot_ellipse(s, cx + 4.5, cy + 6.5, 2.6, 3.6, -50, sh)
    elif ex == "sad":
        rot_ellipse(s, cx - r + 2.2, cy + 5.0, 2.2, 3.8, 10, sh)
        rot_ellipse(s, cx + r - 2.2, cy + 5.0, 2.2, 3.8, -10, sh)
    elif ex == "talk":
        rot_ellipse(s, cx - r + 1.0, cy + 3.5, 2.4, 4.0, 30, sh)
        rot_ellipse(s, cx + r - 0.2, cy - 0.5, 2.4, 4.2, -70, sh)
    else:
        rot_ellipse(s, cx - r + 1.0, cy + 3.5, 2.4, 4.0, 30, sh)
        rot_ellipse(s, cx + r - 1.0, cy + 3.5, 2.4, 4.0, -30, sh)
    s.outline_since(st, ow=OW * 0.8)


def eyes(s, ex, cx=20, ey=22, gap=6.0, size=1.0):
    rx, ry = 2.3 * size, 2.9 * size
    st = s.group_start()
    for sx in (-1, 1):
        x = cx + sx * gap
        if ex == "blink":
            s.arc_line(x, ey + 0.2, 2.4 * size, 1.4 * size, 20, 160, INK, 1.05)
            for k in (-1, 1):  # lashes
                a = math.radians(90 + k * 70 + (sx * 8))
                s.line(x + math.cos(a) * 2.4 * size * 1.02, ey + 0.2 + math.sin(a) * 1.4 * size,
                       x + math.cos(a) * 2.4 * size * 1.02 + sx * 0.0 + (0.7 if (k * 1) > 0 else -0.7), ey + 0.2 + math.sin(a) * 1.4 * size - 0.2,
                       INK, w=0.6) if False else None
            continue
        if ex == "happy":
            s.arc_line(x, ey + 1.6, 2.5 * size, 2.3 * size, 200, 340, INK, 1.25)
            continue
        dx = dy = 0.0
        r1, r2 = rx, ry
        if ex == "lookl":
            dx = -1.3
        elif ex == "lookr":
            dx = 1.3
        elif ex == "think":
            dx, dy = 1.0, -1.0
            r1, r2 = rx * 0.9, ry * 0.9
        elif ex == "sad":
            dy = 0.8
            r1, r2 = rx * 0.9, ry * 0.85
        elif ex == "wow":
            r1, r2 = rx * 1.15, ry * 1.2
        s.eye(x + dx, ey + dy, r1, r2, sparkle=1.15 if ex == "wow" else 1.0)
        if ex == "wow":  # an extra twinkle
            s.path(V.rounded_poly(V.star_pts(x + dx + r1 * 0.05, ey + dy + r2 * 0.15, r1 * 0.38, 0.38, 4), 0.05), WHITE, flat=True,
                   opacity=0.85)
        if ex == "sad":  # worried brows, the inner ends up
            bar(s, x - sx * 2.2, ey - 5.7, x + sx * 1.9, ey - 4.5, 0.85, INK)
        if ex == "think":  # one brow up
            if sx > 0:
                s.arc_line(x + 0.6, ey - 3.8, 2.0, 0.9, 200, 330, INK, 0.75)
    s.group_end(st, attrs=" id=\"eyes\"")


def cheeks(s, cx=20, ey=22, gap=6.0, wide=1.0):
    for sx in (-1, 1):
        s.blush(cx + sx * (gap + 1.8 * wide), ey + 3.8, 2.1, 1.25, op=0.75)


def mouth(s, ex, cx=20, my=26.5, w=1.0):
    st = s.group_start()
    if ex in ("idle", "blink", "lookl", "lookr"):
        s.stroke_path("M%s %sQ%s %s %s %s" % (V.f(cx - 1.8 * w), V.f(my - 1.0), V.f(cx), V.f(my + 0.9), V.f(cx + 1.8 * w), V.f(my - 1.0)),
                      INK, w=0.85)
    elif ex == "talk":
        s.ellipse(cx, my, 2.1 * w, 1.85, MOUTH, flat=True)
        s.raw("<ellipse cx=\"%s\" cy=\"%s\" rx=\"%s\" ry=\"%s\" fill=\"none\" stroke=\"%s\" stroke-width=\".7\"/>" % (
            V.f(cx), V.f(my), V.f(2.1 * w), "1.85", V.hexc(INK)))
        s.ellipse(cx, my + 0.85, 1.15 * w, 0.62, TONGUE, flat=True)
    elif ex == "happy":
        d = "M%s %sQ%s %s %s %sZ" % (V.f(cx - 3.1 * w), V.f(my - 1.1), V.f(cx), V.f(my + 4.6), V.f(cx + 3.1 * w), V.f(my - 1.1))
        s.path(d, MOUTH, flat=True)
        s.raw("<path d=\"%s\" fill=\"none\" stroke=\"%s\" stroke-width=\".75\" stroke-linejoin=\"round\"/>" % (d, V.hexc(INK)))
        s.ellipse(cx, my + 1.0, 1.3 * w, 0.75, TONGUE, flat=True)
    elif ex == "sad":
        s.stroke_path("M%s %sQ%s %s %s %s" % (V.f(cx - 1.7 * w), V.f(my + 1.0), V.f(cx), V.f(my - 0.6), V.f(cx + 1.7 * w), V.f(my + 1.0)),
                      INK, w=0.85)
    elif ex == "wow":
        s.ellipse(cx, my + 0.4, 1.45 * w, 1.85, MOUTH, flat=True)
        s.raw("<ellipse cx=\"%s\" cy=\"%s\" rx=\"%s\" ry=\"1.85\" fill=\"none\" stroke=\"%s\" stroke-width=\".75\"/>" % (
            V.f(cx), V.f(my + 0.4), V.f(1.45 * w), V.hexc(INK)))
    elif ex == "think":
        s.stroke_path("M%s %sQ%s %s %s %s" % (V.f(cx - 0.6), V.f(my + 0.1), V.f(cx + 1.2 * w), V.f(my - 0.2), V.f(cx + 2.6 * w), V.f(my - 0.9)),
                      INK, w=0.85)
    s.group_end(st, attrs=" id=\"mouth\"")


def beak(s, ex, cx=20, by=26.0, col=ORANGE, w=1.0):
    st = s.group_start()
    if ex in ("talk", "happy", "wow"):
        s.poly([(cx - 2.4 * w, by - 1.4), (cx + 2.4 * w, by - 1.4), (cx, by + 0.6)], col, round_r=0.5)
        s.poly([(cx - 1.9 * w, by + 0.6), (cx + 1.9 * w, by + 0.6), (cx, by + 3.0)], col, round_r=0.5)
        s.outline_since(st, ow=0.45)
        s.ellipse(cx, by + 0.6, 1.3 * w, 0.62, MOUTH, flat=True)
    else:
        s.poly([(cx - 2.4 * w, by - 1.2), (cx + 2.4 * w, by - 1.2), (cx, by + 1.9)], col, round_r=0.6)
        s.outline_since(st, ow=0.45)
    s.shine(cx - 0.8 * w, by - 0.55, 0.45, 0.9)


def face(s, ex, cx=20, ey=22, gap=6.0, my=26.5, blush=True, has_mouth=True, size=1.0, w=1.0):
    if blush:
        cheeks(s, cx, ey, gap)
    eyes(s, ex, cx, ey, gap, size)
    if has_mouth:
        mouth(s, ex, cx, my, w)


def round_body(s, ex, col, cx=20, cy=24, r=14.4, extra_back=None, extra_front=None, has_feet=True):
    """feet, the body and whatever stands behind it (ears, a tail...), outlined as one"""
    st = s.group_start()
    if has_feet:
        feet(s, col, cx, cy, r)
    if extra_back:
        extra_back()
    body = s.circle(cx, cy, r, col)
    if extra_front:
        extra_front()
    s.outline_since(st)
    return body


# ------------------------------------------------------------------ the friends
def pip(ex):
    s = new()
    st = s.group_start()
    feet(s, MINT)
    bar(s, 20, 10.5, 20.6, 4.5, 1.6, GREEN)
    leaf1 = rot_ellipse(s, 14.4, 5.4, 5.6, 2.8, -18, GREEN)
    leaf2 = rot_ellipse(s, 26.2, 4.6, 6.0, 3.0, 14, GREEN)
    body = s.circle(20, 24, 14.4, MINT)
    s.outline_since(st)
    for (x, y, rx, a) in ((14.0, 5.2, 3.6, -18), (26.4, 4.4, 3.8, 14)):  # leaf veins
        s.raw("<path d=\"M%s %sL%s %s\" stroke=\"%s\" stroke-width=\".55\" stroke-linecap=\"round\" transform=\"rotate(%s %s %s)\"/>" % (
            V.f(x - rx), V.f(y), V.f(x + rx), V.f(y), V.hexc(MINT_D), V.f(a), V.f(x), V.f(y)))
    del leaf1, leaf2
    st = s.group_start()
    s.ellipse(20, 31.5, 6.5, 4.6, CREAM)  # a pale tummy
    s.group_end(st, kind="clip", clip=[body])
    shade_body(s, body, 20, 24, 14.4, 14.4, MINT)
    arms(s, ex, MINT)
    face(s, ex)
    return s


def dot(ex):
    s = new()
    st = s.group_start()
    feet(s, SKY)
    body = s.circle(20, 24, 14.4, SKY)
    s.outline_since(st)
    shade_body(s, body, 20, 24, 14.4, 14.4, SKY)
    st = s.group_start()
    s.ellipse(20, 29.5, 9.4, 8.6, WHITE)  # the white front
    s.group_end(st, kind="clip", clip=[body])
    st = s.group_start()
    s.poly([(9.0, 15.0), (31.0, 15.0), (28.6, 8.8), (11.4, 8.8)], PURPLE, round_r=1.2)  # the cap
    s.poly([(19.0, 15.0), (35.4, 15.0), (35.0, 17.2), (19.0, 17.2)], LAV_D, round_r=0.8)  # its peak
    s.circle(20, 9.4, 2.0, YELLOW)
    s.outline_since(st)
    s.gloss(14.5, 10.8, 2.4, 0.8, rot=-8, op=0.5)
    arms(s, ex, SKY)
    st = s.group_start()
    s.poly([(26.0, 30.0), (34.6, 30.0), (34.0, 37.0), (26.6, 37.0)], PURPLE, round_r=0.9)  # the satchel
    s.outline_since(st, ow=0.5)
    s.poly([(26.0, 30.0), (34.6, 30.0), (33.4, 33.0), (27.2, 33.0)], LAV_D, round_r=0.6)
    s.circle(30.3, 33.0, 0.9, YELLOW)
    s.line(25.0, 23.0, 27.4, 30.2, PURPLE, w=0.9)  # the strap
    face(s, ex, ey=21, has_mouth=False)
    beak(s, ex, by=26.4)
    return s


def momo(ex):
    s = new()
    st = s.group_start()
    gills = []
    for sx in (-1, 1):
        for (dx, dy, a) in ((14.0, -6.4, 35), (15.2, -0.6, 8), (14.2, 5.2, -22)):
            gills.append((20 + sx * dx, 22 + dy, a * sx))
            rot_ellipse(s, 20 + sx * dx, 22 + dy, 3.6, 1.9, a * sx, HOT)
    feet(s, PINK, r=13.6)
    body = s.circle(20, 24, 13.6, PINK)
    s.outline_since(st)
    for (x, y, a) in gills:
        rot_ellipse(s, x + (0.9 if x > 20 else -0.9), y, 1.7, 0.75, a, PINK)
    shade_body(s, body, 20, 24, 13.6, 13.6, PINK)
    st = s.group_start()
    s.ellipse(20, 33.0, 7.6, 5.2, WHITE)  # the apron
    s.group_end(st, kind="clip", clip=[body])
    bar(s, 12.6, 28.6, 27.4, 28.6, 1.2, HOT)
    s.path(V.heart_path(20, 33.2, 2.2), HOT)
    s.shine(19.0, 32.4, 0.4)
    arms(s, ex, PINK, r=13.6)
    face(s, ex, gap=5.8)
    return s


def olli(ex):
    s = new()
    st = s.group_start()
    feet(s, LAV)
    s.poly([(7.0, 17.0), (8.6, 4.4), (16.0, 11.0)], LAV, round_r=1.0)
    s.poly([(33.0, 17.0), (31.4, 4.4), (24.0, 11.0)], LAV, round_r=1.0)
    body = s.circle(20, 24, 14.4, LAV)
    s.outline_since(st)
    s.poly([(9.6, 13.0), (10.2, 7.6), (13.6, 11.0)], LAV_D, round_r=0.6)
    s.poly([(30.4, 13.0), (29.8, 7.6), (26.4, 11.0)], LAV_D, round_r=0.6)
    shade_body(s, body, 20, 24, 14.4, 14.4, LAV)
    st = s.group_start()
    s.ellipse(20, 31.0, 7.4, 5.6, PEACH)
    s.group_end(st, kind="clip", clip=[body])
    for (x, y) in ((17.0, 29.6), (23.0, 29.6), (20.0, 32.4)):  # feathers
        s.arc_line(x, y, 1.6, 1.0, 20, 160, PEACH_D, 0.7)
    arms(s, ex, LAV)
    st = s.group_start()  # the glasses
    for sx in (-1, 1):
        s.raw("<ellipse cx=\"%s\" cy=\"20.6\" rx=\"4.6\" ry=\"4.4\" fill=\"%s\" stroke=\"%s\" stroke-width=\"1.25\"/>" % (
            V.f(20 + sx * 6.4), V.hexc(CREAM), V.hexc(PURPLE)))
    bar(s, 18.2, 19.6, 21.8, 19.6, 1.1, PURPLE)
    s.group_end(st, attrs=" id=\"glasses\"")
    face(s, ex, ey=20.6, gap=6.4, has_mouth=False, blush=False, size=0.85)
    for sx in (-1, 1):  # the lenses' shine
        s.gloss(20 + sx * 6.4 - 2.2, 18.4, 1.0, 0.45, rot=-35, op=0.6)
        s.blush(20 + sx * 10.2, 26.4, 1.8, 1.1, op=0.75)
    beak(s, ex, by=25.4, w=0.85)
    return s


def tango(ex):
    s = new()
    st = s.group_start()
    rot_ellipse(s, 33.4, 29.0, 4.8, 7.6, -25, ORANGE)  # the tail
    feet(s, ORANGE, r=13.8)
    s.poly([(7.6, 19.0), (8.6, 4.0), (18.0, 12.6)], ORANGE, round_r=1.0)
    s.poly([(32.4, 19.0), (31.4, 4.0), (22.0, 12.6)], ORANGE, round_r=1.0)
    body = s.circle(20, 24, 13.8, ORANGE)
    s.outline_since(st)
    st = s.group_start()
    rot_ellipse(s, 35.0, 34.6, 2.6, 2.6, -25, WHITE)  # its white tip
    rot_ellipse(s, 33.4, 29.0, 4.8, 7.6, -25, ORANGE, opacity=0.0)
    s.group_end(st, kind="clip", clip=[V.Prim("ellipse", [("cx", "33.4"), ("cy", "29"), ("rx", "4.8"), ("ry", "7.6"),
                                                         ("transform", "rotate(-25 33.4 29)")], ORANGE)])
    s.poly([(10.2, 15.0), (10.8, 8.0), (15.6, 12.4)], PEACH, round_r=0.7)
    s.poly([(29.8, 15.0), (29.2, 8.0), (24.4, 12.4)], PEACH, round_r=0.7)
    shade_body(s, body, 20, 24, 13.8, 13.8, ORANGE)
    s.ellipse(20, 29.6, 7.4, 5.2, WHITE)  # the muzzle
    s.ellipse(13.0, 25.6, 3.4, 2.4, WHITE)
    s.ellipse(27.0, 25.6, 3.4, 2.4, WHITE)
    arms(s, ex, ORANGE, r=13.8)
    face(s, ex, my=28.0)
    s.ellipse(20, 25.4, 1.4, 1.0, INK, flat=True)  # the nose
    s.shine(19.6, 25.0, 0.35)
    return s


def dabble(ex):
    s = new()
    st = s.group_start()
    feet(s, YELLOW)
    body = s.circle(20, 24, 14.4, YELLOW)
    s.outline_since(st)
    shade_body(s, body, 20, 24, 14.4, 14.4, YELLOW)
    st = s.group_start()
    for (x, y, r, c) in ((10.6, 30.0, 1.6, SKY), (29.2, 31.6, 1.5, MINT), (27.6, 13.6, 1.3, HOT)):  # paint splashes
        s.circle(x, y, r, c)
        s.circle(x + r * 1.1, y - r * 0.9, r * 0.38, c)
    s.group_end(st, kind="clip", clip=[body])
    arms(s, ex, YELLOW)
    st = s.group_start()
    s.ellipse(20, 9.6, 9.0, 3.8, HOT)  # the beret
    bar(s, 20.4, 6.2, 21.2, 3.6, 1.4, HOT)
    s.outline_since(st)
    s.ellipse(20, 8.4, 7.6, 2.4, PINK)
    s.gloss(16.6, 7.8, 2.4, 0.7, rot=-6, op=0.55)
    face(s, ex, has_mouth=False)
    beak(s, ex, by=26.6)
    return s


def bramble(ex):
    s = new()
    spikes = [(4.0, 27.0)]
    for i in range(13):
        a = math.pi * (1.0 - i / 12.0)
        r = 18.6 if i % 2 == 0 else 13.2
        spikes.append((20 + r * math.cos(a), 25 - r * math.sin(a) * 0.95))
    spikes.append((36.0, 27.0))
    st = s.group_start()
    feet(s, PEACH, cy=25, r=14.0)
    s.poly(spikes, BROWN, round_r=1.1)
    body = s.circle(20, 25, 14.0, BROWN)
    s.outline_since(st)
    shade_body(s, body, 20, 25, 14.0, 14.0, BROWN)
    for i in range(5):  # a few quill lines
        a = math.pi * (0.85 - i * 0.175)
        s.line(20 + 15.4 * math.cos(a), 25 - 15.4 * math.sin(a) * 0.95, 20 + 12.6 * math.cos(a), 25 - 12.6 * math.sin(a) * 0.95,
               BROWN_D, w=0.6)
    st = s.group_start()
    s.ellipse(20, 27.2, 10.6, 9.4, PEACH)  # the face
    s.outline_since(st, ow=0.35)
    s.gloss(14.8, 21.8, 2.6, 1.2, rot=-25, op=0.45)
    arms(s, ex, PEACH, cy=25, r=14.0)
    face(s, ex, ey=24.4, gap=5.4, my=30.0)
    s.ellipse(20, 27.4, 1.5, 1.2, RED)
    s.shine(19.5, 27.0, 0.4)
    return s


def shelly(ex):
    s = new()
    st = s.group_start()
    s.ellipse(19, 33.4, 17.6, 4.6, MINT)  # the foot
    s.circle(9.6, 24.0, 7.6, MINT)  # the head
    bar(s, 6.4, 18.0, 4.8, 8.4, 1.8, MINT)  # stalks
    bar(s, 12.0, 18.0, 13.0, 8.4, 1.8, MINT)
    s.circle(4.8, 8.4, 2.2, MINT)
    s.circle(13.0, 8.4, 2.2, MINT)
    shell = s.circle(27.4, 21.0, 11.0, PINK)  # the shell
    s.outline_since(st)
    shade_body(s, shell, 27.4, 21.0, 11.0, 11.0, PINK)
    s.ellipse(19, 34.6, 16.0, 2.0, MINT_D, opacity=0.6, flat=True)
    s.arc_line(27.4, 21.0, 7.0, 7.0, -90, 180, HOT, 1.4)  # the swirl
    s.arc_line(26.4, 22.0, 4.0, 4.0, 180, 450, HOT, 1.4)
    s.arc_line(27.4, 21.0, 1.6, 1.6, 90, 300, HOT, 1.4)
    st = s.group_start()  # goggles on the head
    bar(s, 2.8, 15.6, 16.4, 15.6, 1.6, PURPLE)
    s.circle(6.6, 15.6, 2.2, SKY)
    s.circle(12.6, 15.6, 2.2, SKY)
    s.outline_since(st, ow=0.4)
    s.shine(6.0, 15.0, 0.6)
    s.shine(12.0, 15.0, 0.6)
    face(s, ex, cx=9.8, ey=22.6, gap=3.4, my=27.4, size=0.8, w=0.8, blush=False)
    s.blush(4.8, 26.0, 1.4, 0.9)
    s.blush(14.8, 26.0, 1.4, 0.9)
    return s


def nova(ex):
    s = new()
    pts = []
    for i in range(10):
        r = 18.8 if i % 2 == 0 else 9.6
        a = -math.pi / 2 + i * math.pi / 5
        pts.append((20 + r * math.cos(a), 22.4 + r * math.sin(a)))
    st = s.group_start()
    body = s.poly(pts, YELLOW, round_r=2.2)
    s.outline_since(st)
    st = s.group_start()
    s.raw("<circle cx=\"25\" cy=\"29\" r=\"14\" fill=\"%s\"/>" % s.rgrad(YELLOW_D, YELLOW_D, cx=0.6, cy=0.62, r=0.5, o0=0.0, o1=0.8))
    s.group_end(st, kind="clip", clip=[body])
    s.gloss(14.6, 15.6, 2.6, 1.2, rot=-35, op=0.7)
    s.shine(17.6, 12.8, 0.6)
    if ex in ("happy", "wow", "think"):
        arms(s, ex, YELLOW, cy=23, r=13.6)
    face(s, ex, ey=21.6, gap=4.8, my=26.2, size=0.9, w=0.9)
    sp = [(33.6, 6.0), (34.6, 8.4), (37.0, 9.4), (34.6, 10.4), (33.6, 12.8), (32.6, 10.4), (30.2, 9.4), (32.6, 8.4)]
    st = s.group_start()
    s.poly(sp, WHITE, round_r=0.3)
    s.outline_since(st, ow=0.35)
    return s


CAST = [("Pip", pip), ("Dot", dot), ("Momo", momo), ("Olli", olli), ("Tango", tango), ("Dabble", dabble),
        ("Bramble", bramble), ("Shelly", shelly), ("Nova", nova)]


def cast():
    return [("cast/%s_%s" % (name.lower(), ex), fn(ex)) for name, fn in CAST for ex in EXPR]


# ------------------------------------------------------------------ stickers (12 x 12)
def stk():
    s = V.Svg(12, 12, ow=0.5, ink=INK, vb=(-1, -1, 14, 14))
    s.light, s.dark = 0.4, 0.1
    return s


def tiny_face(s, cx, cy, gap=1.3):
    for sx in (-1, 1):
        s.eye(cx + sx * gap, cy, 0.42, 0.52, sparkle=0.9)
        s.blush(cx + sx * (gap + 0.9), cy + 1.0, 0.55, 0.32)
    s.stroke_path("M%s %sQ%s %s %s %s" % (V.f(cx - 0.5), V.f(cy + 0.9), V.f(cx), V.f(cy + 1.45), V.f(cx + 0.5), V.f(cy + 0.9)), INK, w=0.28)


def sticker(kind):
    s = stk()
    st = s.group_start()
    if kind == "heart":
        s.path(V.heart_path(6, 6.2, 4.7), HOT)
        s.outline_since(st)
        s.gloss(3.6, 4.4, 1.3, 0.7, rot=-35)
        tiny_face(s, 6, 6.4)
    elif kind == "star":
        s.poly(V.star_pts(6, 6.5, 5.9, 0.48), YELLOW, round_r=0.7)
        s.outline_since(st)
        s.gloss(4.3, 4.8, 0.9, 0.5, rot=-35)
        tiny_face(s, 6, 6.6, 1.1)
    elif kind == "flower":
        for i in range(5):
            a = i * 2 * math.pi / 5 - math.pi / 2
            s.circle(6 + 3 * math.cos(a), 6 + 3 * math.sin(a), 2.3, PINK)
        s.outline_since(st)
        for i in range(5):
            a = i * 2 * math.pi / 5 - math.pi / 2
            s.shine(6 + 3.4 * math.cos(a) - 0.5, 6 + 3.4 * math.sin(a) - 0.5, 0.45, 0.7)
        s.circle(6, 6, 1.7, YELLOW)
        s.shine(5.5, 5.5, 0.4)
    elif kind == "rainbow":
        for k, (r, c) in enumerate(((5.6, HOT), (4.6, ORANGE), (3.6, YELLOW), (2.6, MINT), (1.6, SKY))):
            s.path("M%s 9A%s %s 0 0 1 %s 9Z" % (V.f(6 - r), V.f(r), V.f(r), V.f(6 + r)), c, flat=k > 0)
        s.outline_since(st)
        s.circle(1.4, 9.2, 1.4, WHITE)
        s.circle(10.6, 9.2, 1.4, WHITE)
    elif kind == "moon":
        from .items import moon_path
        s.path(moon_path(6, 6, 5, 8.4, 4.6, 4), YELLOW)
        s.outline_since(st)
        s.eye(3.3, 7.0, 0.38, 0.48)
        s.blush(3.8, 8.4, 0.5, 0.3)
    elif kind == "cupcake":
        s.poly([(2.5, 7), (9.5, 7), (8.5, 11), (3.5, 11)], SKY, round_r=0.5)
        s.circle(4.2, 6, 2.2, PINK), s.circle(7.8, 6, 2.2, PINK), s.circle(6, 4.4, 2.4, PINK)
        s.circle(6, 1.8, 1.1, HOT)
        s.outline_since(st)
        for x in (4.0, 6.0, 8.0):
            s.line(x - 0.2, 7.6, x - 0.4, 10.2, SKY_D, w=0.4)
        s.shine(5.0, 3.6, 0.5)
        s.shine(5.7, 1.4, 0.3)
    elif kind == "strawberry":
        s.path("M6 11.6C3 10 1.6 7.4 2 5.2C2.4 3.4 4.4 3 6 3.8C7.6 3 9.6 3.4 10 5.2C10.4 7.4 9 10 6 11.6Z", HOT)
        s.poly([(3, 2.6), (6, 1.4), (9, 2.6), (7.2, 4.6), (6, 3.6), (4.8, 4.6)], GREEN, round_r=0.4)
        s.outline_since(st)
        for (x, y) in ((4.4, 6.2), (7.4, 6.2), (5.6, 8.0), (8.0, 8.2), (6.2, 9.9), (3.8, 8.2)):
            s.ellipse(x + 0.2, y + 0.2, 0.32, 0.45, YELLOW, flat=True)
        s.gloss(4.0, 5.0, 0.9, 0.5)
    elif kind == "cloud":
        s.circle(4, 7, 2.8, WHITE), s.circle(7, 5.4, 3.2, WHITE), s.circle(9, 7.6, 2.4, WHITE), s.ellipse(6, 8.6, 5, 1.8, WHITE)
        s.outline_since(st)
        tiny_face(s, 6.3, 7.2)
    elif kind == "icecream":
        s.poly([(3.4, 6), (8.6, 6), (6, 11.6)], PEACH, round_r=0.5)
        s.circle(6, 4.4, 3.2, LAV)
        s.outline_since(st)
        s.line(4.4, 7.2, 7.4, 9.6, PEACH_D, w=0.35), s.line(7.6, 7.2, 5.0, 9.6, PEACH_D, w=0.35)
        s.gloss(4.8, 3.2, 1.0, 0.6)
        s.circle(7.6, 2.4, 0.7, HOT)
    elif kind == "balloon":
        s.ellipse(6, 4.6, 3.8, 4.4, SKY)
        s.poly([(5.3, 8.7), (6.7, 8.7), (6, 9.8)], SKY, round_r=0.2)
        s.outline_since(st)
        s.stroke_path("M6 9.8Q5.2 10.8 6.2 11.8", INK, w=0.4)
        s.gloss(4.6, 2.9, 1.1, 0.7)
    elif kind == "gem":
        s.poly([(2, 4.4), (4, 1.8), (8, 1.8), (10, 4.4), (6, 10.6)], SKY, round_r=0.3)
        s.outline_since(st)
        s.poly([(2, 4.4), (10, 4.4), (6, 10.6)], SKY_D, opacity=0.45, flat=True)
        s.poly([(4, 1.8), (6, 4.4), (8, 1.8)], WHITE, opacity=0.55, flat=True)
        s.line(2.2, 4.4, 9.8, 4.4, INK, w=0.3)
        s.shine(4.2, 3.2, 0.45)
    elif kind == "mushroom":
        s.poly([(4.4, 5.4), (7.6, 5.4), (7.8, 10.6), (4.2, 10.6)], PEACH, round_r=0.8)
        s.path("M0.6 5.6C0.6 2.6 3 1.2 6 1.2C9 1.2 11.4 2.6 11.4 5.6Z", HOT)
        s.outline_since(st)
        for (x, y, r) in ((3.6, 3.4, 0.8), (8.2, 4.0, 0.65), (6.1, 2.3, 0.55)):
            s.circle(x, y, r, WHITE, flat=True)
        tiny_face(s, 6, 7.6, 0.9)
    elif kind == "leaf":
        s.path("M1 6.4C3 2.6 9 2.4 11 5.6C9 9.4 3 9.6 1 6.4Z", GREEN)
        s.outline_since(st)
        s.stroke_path("M2.2 6.3Q6 5.2 9.8 5.7", MINT, w=0.45)
        s.gloss(4.6, 4.4, 1.2, 0.45)
    elif kind == "shell":
        s.poly([(1.6, 4.6), (6, 1.2), (10.4, 4.6), (7.4, 10.6), (4.6, 10.6)], PEACH, round_r=1.0)
        s.outline_since(st)
        for x in (3.6, 6.0, 8.4):
            s.line(6, 10.2, x, 3.0, PEACH_D, w=0.4)
        s.shine(4.4, 3.8, 0.45)
    elif kind == "sun":
        for i in range(8):
            a = i * math.pi / 4
            s.circle(6 + 4.6 * math.cos(a), 6 + 4.6 * math.sin(a), 1.05, ORANGE)
        s.circle(6, 6, 3.5, YELLOW)
        s.outline_since(st)
        tiny_face(s, 6, 6, 1.1)
    elif kind == "cherry":
        s.stroke_path("M3.8 7Q4.6 3 7.4 1.2", GREEN, w=0.6)
        s.stroke_path("M8.4 7Q8.2 3.6 7.4 1.2", GREEN, w=0.6)
        s.circle(3.6, 8.4, 2.4, HOT), s.circle(8.4, 8.6, 2.4, HOT)
        s.outline_since(st)
        s.ellipse(8.4, 1.6, 1.4, 0.7, GREEN, rot=-20)
        s.gloss(2.8, 7.5, 0.7, 0.45), s.gloss(7.6, 7.7, 0.7, 0.45)
    return s


def stickers():
    return [("sticker/" + k, sticker(k)) for k in GA.STICKERS]
