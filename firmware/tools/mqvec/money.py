"""Sparkle Town's money as vector art: the coins, the bills and her purse (MODX_HR_SPRITE ids money/...).

The module draws them with the mq_vis primitives inside an hr bracket (modules/mathquest/mq_purse.c:
mq_money_draw, mq_purse_icon); the Mac page draws these pictures in the bracket's box instead. Each
picture fills its box (a coin's box is its diameter, a bill's 36 x 20, the purse's a square), and the
value on a coin or bill is the module's own text, drawn after the bracket (so it stays crisp and
readable on top). One kawaii style with the rest of the set: soft gradients, round plum outlines, a
glossy shine.
"""
from . import svg as V

INK = (90, 61, 92)
WHITE = (255, 255, 255)
PINK = (255, 179, 199)
HOT = (236, 120, 160)
GOLD = (240, 196, 80)
COPPER = (222, 150, 100)
COPPER_RIM = (176, 104, 64)
SILVER = (214, 218, 230)
SILVER_RIM = (150, 150, 172)
GREEN = (120, 186, 130)
LGREEN = (196, 236, 200)


def new(w, h, pad=1.0, ow=0.6):
    s = V.Svg(w, h, ow=ow, ink=INK, vb=(-pad, -pad, w + 2 * pad, h + 2 * pad))
    s.light, s.dark = 0.32, 0.14
    return s


def body(s, fn):
    st = s.group_start()
    fn()
    s.outline_since(st)


def coin(d, face, rim, reeded=False):
    """a coin d units across: a rim, a softly domed face, a ring of dots or reeds, a shine"""
    s = new(d, d)
    c, r = d / 2.0, d / 2.0 - 0.3
    body(s, lambda: s.circle(c, c, r, rim))
    s.circle(c, c, r - 1.4, face)
    s.raw("<circle cx=\"%s\" cy=\"%s\" r=\"%s\" fill=\"%s\"/>" % (V.f(c), V.f(c), V.f(r - 1.4), s.rgrad(V.lighter(face, 0.35), face, cx=0.38, cy=0.32, r=0.7, o0=0.9, o1=0.0)))
    n = 28 if reeded else 14
    for i in range(n):
        import math
        a = 2 * math.pi * i / n
        rr = r - 0.7
        if reeded:
            s.line(c + math.cos(a) * (rr - 0.5), c + math.sin(a) * (rr - 0.5), c + math.cos(a) * (rr + 0.3), c + math.sin(a) * (rr + 0.3),
                   V.deeper(rim, 0.1), w=0.35, flat=True)
        else:
            s.circle(c + math.cos(a) * (r - 2.2), c + math.sin(a) * (r - 2.2), 0.28, V.deeper(face, 0.12), flat=True)
    s.gloss(c - r * 0.42, c - r * 0.45, r * 0.32, r * 0.16, rot=-35, op=0.8)
    return s


def bill(five):
    """a dollar bill (36 x 20): a green note, a lighter panel, an oval with a little star, corner rosettes"""
    s = new(36, 20, pad=1.0, ow=0.55)
    body(s, lambda: s.rect(0.3, 0.3, 35.4, 19.4, GREEN, r=2.6))
    s.rect(2.2, 2.2, 31.6, 15.6, LGREEN, r=1.8, flat=True)
    s.raw("<rect x=\"2.2\" y=\"2.2\" width=\"31.6\" height=\"15.6\" rx=\"1.8\" fill=\"none\" stroke=\"%s\" stroke-width=\"0.5\" "
          "stroke-dasharray=\"1 0.8\"/>" % V.hexc(GREEN))
    s.ellipse(18, 10, 6.4, 5.6, (170, 220, 176), flat=True)
    s.path(V.rounded_poly(V.star_pts(18, 10.3, 2.4 if five else 1.8, 0.45), 0.2), (150, 206, 160), flat=True)
    for (x, y) in ((4.6, 4.6), (31.4, 4.6), (4.6, 15.4), (31.4, 15.4)):
        s.circle(x, y, 1.5, (150, 206, 160), flat=True)
    if five:  # (the five: a pink ribbon so it tells apart at a glance)
        s.rect(26.5, 2.2, 2.4, 15.6, PINK, flat=True, opacity=0.8)
    s.gloss(9, 4.5, 5.5, 1.2, rot=-8, op=0.5)
    return s


def purse():
    """her purse: a pink pouch with a gold handle and clasp, a heart on the flap (20 x 20)"""
    s = new(20, 20, pad=1.0, ow=0.7)
    s.raw("<path d=\"M6.2 6.8C6.2 2.6 13.8 2.6 13.8 6.8\" fill=\"none\" stroke=\"%s\" stroke-width=\"1.6\" stroke-linecap=\"round\"/>"
          % V.hexc(GOLD))
    body(s, lambda: s.path("M2 9C2 6.8 3.6 6 6 6L14 6C16.4 6 18 6.8 18 9L18.6 15.6C18.8 18 17.2 19.4 14.8 19.4L5.2 19.4C2.8 19.4 1.2 18 1.4 15.6Z", PINK))
    s.path("M2.2 9.2C2.2 7.2 3.8 6.4 6 6.4L14 6.4C16.2 6.4 17.8 7.2 17.8 9.2L17.9 11.2C14 12.8 6 12.8 2.1 11.2Z", HOT, flat=True, opacity=0.55)
    body(s, lambda: s.circle(10, 11.6, 1.6, GOLD))
    s.path(V.heart_path(10, 16, 1.6), (255, 236, 242), flat=True)
    s.gloss(5.2, 9, 2.2, 0.9, rot=-20, op=0.7)
    return s


def trophy():
    """a new best (never a number): a little gold cup with handles, a star on it, on a plum foot (20 x 20)"""
    s = new(20, 20, pad=1.0, ow=0.7)
    for x in (3.2, 16.8):
        s.raw("<circle cx=\"%s\" cy=\"7.2\" r=\"2.6\" fill=\"none\" stroke=\"%s\" stroke-width=\"1.4\"/>" % (V.f(x), V.hexc(GOLD)))
    body(s, lambda: s.path("M4 2.6L16 2.6L15.4 9C15 12.2 12.8 13.6 10 13.6C7.2 13.6 5 12.2 4.6 9Z", GOLD))
    s.path(V.rounded_poly(V.star_pts(10, 7.4, 2.6, 0.45), 0.2), (255, 236, 160), flat=True)
    body(s, lambda: s.rect(8.6, 13.4, 2.8, 2.8, (214, 160, 60), r=0.6))
    body(s, lambda: s.rect(5.4, 16.0, 9.2, 3.2, (150, 100, 150), r=1.2))
    s.gloss(6.6, 5.0, 1.0, 2.4, rot=-10, op=0.7)
    return s


def money():
    return [("money/trophy", trophy()), ("money/penny", coin(20, COPPER, COPPER_RIM)), ("money/nickel", coin(22, SILVER, SILVER_RIM)),
            ("money/dime", coin(18, SILVER, SILVER_RIM, reeded=True)), ("money/quarter", coin(24, SILVER, SILVER_RIM, reeded=True)),
            ("money/bill1", bill(False)), ("money/bill5", bill(True)), ("money/purse", purse())]
