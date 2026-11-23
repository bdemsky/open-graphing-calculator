"""Scenes and things the module draws from shapes, as vector art (MODX_HR_SPRITE ids).

Each picture is drawn in the module's logical pixels relative to the box the module names (see
the mq_hr_sprite calls in modules/mathquest/*.c), so it lands where the shapes would. Moving
parts (clock hands, the ferris wheel, smoke, bubbles' wobble, the backgrounds' clouds...) are not
in these pictures: the page's rigs (web/page/sparkle_rigs.js) draw them, by the module's clock.
"""
import math

from . import svg as V

INK = (90, 61, 92)
WHITE = (255, 255, 255)
PINK = (255, 179, 199)
HOT = (255, 122, 162)
MINT = (168, 230, 207)
GREEN = (92, 191, 138)
LAV = (201, 182, 255)
PURPLE = (155, 127, 230)
SKY = (159, 216, 255)
BLUE = (90, 169, 230)
PEACH = (255, 211, 165)
ORANGE = (255, 164, 92)
YELLOW = (255, 230, 128)
BROWN = (176, 133, 96)
CREAM = (255, 248, 236)
GOLD = (240, 196, 80)
RED = (236, 80, 90)
DWOOD = (140, 100, 72)
LWOOD = (222, 184, 140)
GREY = (200, 200, 214)
DIM = (150, 140, 170)
NAVY = (64, 72, 140)


def new(w, h, pad=2, ow=0.85):
    s = V.Svg(w, h, ow=ow, ink=INK, vb=(-pad, -pad, w + 2 * pad, h + 2 * pad))
    s.light, s.dark = 0.3, 0.12
    return s


def body(s, fn):
    """fn draws shapes; they become one outlined body"""
    st = s.group_start()
    fn()
    s.outline_since(st)


# ------------------------------------------------------------------ town parts
def window(s, x, y, box=False):
    """the module's 10 x 10 window at (x, y): a frame, a pane, curtains, a sill"""
    body(s, lambda: s.rect(x - 1.5, y - 1.5, 13, 13, WHITE, r=2.2))
    s.rect(x, y, 10, 10, SKY, r=1.2)
    s.raw("<path d=\"M%s %sL%s %sL%s %sZ\" fill=\"#fff\" opacity=\".45\"/>" % (V.f(x + 1), V.f(y + 9), V.f(x + 9), V.f(y + 1), V.f(x + 9),
                                                                          V.f(y + 4)))
    s.rect(x + 4.3, y, 1.4, 10, WHITE, flat=True)
    s.rect(x, y + 4.3, 10, 1.4, WHITE, flat=True)
    s.path("M%s %sQ%s %s %s %sL%s %sZ" % (V.f(x), V.f(y), V.f(x + 1), V.f(y + 5), V.f(x + 3.4), V.f(y + 7), V.f(x), V.f(y + 7)), PINK)
    s.path("M%s %sQ%s %s %s %sL%s %sZ" % (V.f(x + 10), V.f(y), V.f(x + 9), V.f(y + 5), V.f(x + 6.6), V.f(y + 7), V.f(x + 10),
                                         V.f(y + 7)), PINK)
    body(s, lambda: s.rect(x - 2.5, y + 10, 15, 2.4, LWOOD, r=1))
    if box:
        flower_box(s, x, y)


def flower_box(s, x, y):
    body(s, lambda: s.rect(x - 1, y + 11, 12, 3.4, BROWN, r=1))
    for k in range(3):
        s.circle(x + 1.2 + k * 3.8, y + 10.2, 1.5, PINK if k % 2 else YELLOW)
        s.circle(x + 1.2 + k * 3.8, y + 10.2, 0.55, WHITE, flat=True)


def door(s, cx, b, w, h, col):
    body(s, lambda: s.rect(cx - w / 2 - 2.5, b - 2.2, w + 5, 2.4, LWOOD, r=1))
    body(s, lambda: s.path("M%s %sL%s %sA%s %s 0 0 1 %s %sL%s %sZ" % (
        V.f(cx - w / 2), V.f(b - 2), V.f(cx - w / 2), V.f(b - h + w / 2 - 2), V.f(w / 2), V.f(w / 2), V.f(cx + w / 2),
        V.f(b - h + w / 2 - 2), V.f(cx + w / 2), V.f(b - 2)), col))
    s.rect(cx - w / 2 + 2, b - h + w / 2, w - 4, h * 0.3, V.deeper(col, 0.12), r=1.5, opacity=0.55, flat=True)
    s.circle(cx + w / 4, b - h / 2 - 2, 1.1, GOLD)
    s.shine(cx + w / 4 - 0.3, b - h / 2 - 2.3, 0.35)


def roof(s, x0, x1, y, h, col, shingle=True):
    cx = (x0 + x1) / 2
    body(s, lambda: s.poly([(x0, y), (x1, y), (cx, y - h)], col, round_r=2.2))
    if shingle:
        for r in range(1, 4):
            yy = y - h + h * r / 4
            half = (x1 - x0) * r / 8
            s.stroke_path("M%s %sQ%s %s %s %s" % (V.f(cx - half), V.f(yy), V.f(cx), V.f(yy + 1.4), V.f(cx + half), V.f(yy)),
                          V.deeper(col, 0.2), w=0.8)
    s.gloss(cx - (x1 - x0) * 0.16, y - h * 0.45, (x1 - x0) * 0.08, h * 0.08, rot=-28, op=0.45)


def wall(s, x, y, w, h, col, r=1.5):
    body(s, lambda: s.rect(x, y, w, h, col, r=r))
    s.raw("<rect x=\"%s\" y=\"%s\" width=\"%s\" height=\"%s\" fill=\"%s\"/>" % (
        V.f(x), V.f(y + h - 6), V.f(w), "6", s.lgrad([(0, V.deeper(col, 0.0) + (0,)), (1, V.deeper(col, 0.2) + (0.5,))])))


def chimney(s, x, y):
    body(s, lambda: [s.rect(x, y, 8, 14, (220, 140, 128), r=0.8), s.rect(x - 1, y, 10, 3, (190, 112, 104), r=0.8)])


def bid(cx, b):
    """building helpers work in the module's coordinates with sx -> cx, b -> b"""
    return cx, b


def b_garden():
    s = new(100, 120)
    sx, b = 50, 120
    st = s.group_start()
    s.rect(sx - 40, b - 50, 80, 50, (206, 244, 220), r=4)
    s.ellipse(sx, b - 50, 40, 16, (220, 248, 232))
    s.outline_since(st)
    for k in range(-30, 31, 15):  # the glass ribs
        s.line(sx + k, b - 63 + abs(k) * 0.12, sx + k, b - 1, (140, 206, 166), w=1.2)
    s.stroke_path("M%s %sQ%s %s %s %s" % (V.f(sx - 40), V.f(b - 50), V.f(sx), V.f(b - 41), V.f(sx + 40), V.f(b - 50)), (140, 206, 166), w=1)
    for k in range(4):  # plants inside
        x = sx - 26 + k * 17 + (6 if k > 1 else 0)
        y = b - 30 + (k % 2) * 6
        s.circle(x, y, 5.5, (120, 200, 146), opacity=0.9)
        s.circle(x - 1.6, y - 1.8, 1.6, (176, 234, 196), flat=True)
    s.gloss(sx - 26, b - 54, 9, 3, rot=-10, op=0.6)
    s.gloss(sx + 22, b - 40, 2, 10, rot=8, op=0.35)
    door(s, sx, b, 16, 22, GREEN)
    for k in range(6):
        x = sx - 36 + k * 14 + (4 if k > 2 else 0)
        flower(s, x, b - 4, PINK if k % 2 else YELLOW)
    return s


def flower(s, x, y, col, r=3.0):
    st = s.group_start()
    for i in range(5):
        a = i * 2 * math.pi / 5 - math.pi / 2
        s.circle(x + r * 0.55 * math.cos(a), y + r * 0.55 * math.sin(a), r * 0.55, col)
    s.outline_since(st, ow=0.4)
    s.circle(x, y, r * 0.38, YELLOW if col != YELLOW else ORANGE, flat=True)


def b_post():
    s = new(100, 120)
    sx, b = 50, 120
    wall(s, sx - 40, b - 64, 80, 64, (186, 214, 248))
    roof(s, sx - 46, sx + 46, b - 64, 18, BLUE)
    window(s, sx - 30, b - 54)
    window(s, sx + 20, b - 54)
    door(s, sx, b, 16, 26, WHITE)
    s.rect(sx - 3, b - 20, 6, 3.5, SKY, r=1)
    body(s, lambda: s.rect(sx + 22, b - 22, 12, 22, RED, r=4.5))
    s.rect(sx + 24, b - 16, 8, 2, INK, r=0.8, flat=True)
    s.gloss(sx + 25, b - 18, 1, 2.5, rot=0, op=0.5)
    body(s, lambda: [s.rect(sx - 14, b - 78, 28, 10, WHITE, r=3)])  # an envelope sign
    s.polyline([(sx - 13, b - 77), (sx, b - 72), (sx + 13, b - 77)], INK, w=0.8)
    s.path(V.heart_path(sx, b - 71.6, 2), HOT)
    return s


def b_bakery():
    s = new(100, 120)
    sx, b = 50, 120
    chimney(s, sx + 26, b - 76)
    wall(s, sx - 40, b - 58, 80, 58, (255, 220, 232))
    st = s.group_start()  # the striped awning, scalloped
    d = "M%s %s" % (V.f(sx - 44), V.f(b - 62))
    d += "L%s %sL%s %s" % (V.f(sx + 44), V.f(b - 62), V.f(sx + 44), V.f(b - 54))
    for k in range(8):
        x1 = sx + 44 - (k + 1) * 11
        d += "A5.5 4 0 0 1 %s %s" % (V.f(x1), V.f(b - 54))
    d += "Z"
    s.path(d, HOT)
    s.outline_since(st)
    for k in range(8):
        if k % 2:
            x0 = sx - 44 + k * 11
            s.path("M%s %sL%s %sL%s %sA5.5 4 0 0 1 %s %sZ" % (V.f(x0), V.f(b - 62), V.f(x0 + 11), V.f(b - 62), V.f(x0 + 11), V.f(b - 54),
                                                            V.f(x0), V.f(b - 54)), WHITE, flat=True)
    window(s, sx - 30, b - 40)
    window(s, sx + 20, b - 40)
    door(s, sx, b, 14, 26, BROWN)
    # the cupcake sign
    body(s, lambda: [s.poly([(sx - 6.5, b - 70), (sx + 6.5, b - 70), (sx + 5, b - 63), (sx - 5, b - 63)], PEACH, round_r=1),
                     s.circle(sx - 3.6, b - 71, 3.6, PINK), s.circle(sx + 3.6, b - 71, 3.6, PINK), s.circle(sx, b - 74, 4, PINK),
                     s.circle(sx, b - 78.5, 1.8, RED)])
    s.shine(sx - 1.5, b - 75.5, 0.7)
    return s


def b_clock():
    s = new(100, 120)
    sx, b = 50, 120
    wall(s, sx - 22, b - 84, 44, 84, (220, 208, 252))
    roof(s, sx - 28, sx + 28, b - 84, 18, PURPLE)
    body(s, lambda: s.circle(sx, b - 64, 14, WHITE))
    s.raw("<circle cx=\"%s\" cy=\"%s\" r=\"15\" fill=\"none\" stroke=\"%s\" stroke-width=\"2.2\"/>" % (V.f(sx), V.f(b - 64), V.hexc(GOLD)))
    for k in range(12):
        a = math.radians(k * 30)
        r0 = 11 if k % 3 else 9.5
        s.line(sx + math.sin(a) * r0, b - 64 - math.cos(a) * r0, sx + math.sin(a) * 12.5, b - 64 - math.cos(a) * 12.5, INK, w=0.9)
    window(s, sx - 5, b - 42)
    door(s, sx, b, 14, 24, BROWN)
    return s


def b_shop():
    s = new(100, 120)
    sx, b = 50, 120
    wall(s, sx - 38, b - 54, 76, 54, (255, 226, 196))
    st = s.group_start()
    for k in range(7):
        s.circle(sx - 36 + k * 12, b - 54, 6.4, ORANGE if k % 2 == 0 else WHITE)
    s.outline_since(st)
    for k in range(7):
        if k % 2:
            s.circle(sx - 36 + k * 12, b - 54, 6.4, WHITE)
    body(s, lambda: s.rect(sx - 31, b - 41, 28, 19, (255, 248, 210), r=2))  # the shop window
    s.rect(sx - 32, b - 23.5, 30, 2.4, LWOOD, r=1)
    body(s, lambda: [s.circle(sx - 22, b - 30, 4, GOLD), s.rect(sx - 14, b - 36, 7, 9, PINK, r=2),
                     s.rect(sx - 26, b - 39, 8, 4, MINT, r=1.5)])
    s.shine(sx - 23.3, b - 31.3, 0.7)
    door(s, sx + 16, b, 16, 28, BROWN)
    body(s, lambda: s.rect(sx + 10, b - 22, 12, 6, WHITE, r=2))
    s.rect(sx + 12, b - 20, 8, 2, HOT, r=1, flat=True)
    return s


def b_art():
    s = new(100, 120)
    sx, b = 50, 120
    wall(s, sx - 38, b - 60, 76, 60, (255, 246, 200))
    roof(s, sx - 44, sx + 44, b - 60, 14, ORANGE)
    body(s, lambda: s.path("M%s %sC%s %s %s %s %s %sC%s %s %s %s %s %sC%s %s %s %s %s %sZ" % (
        V.f(sx - 12), V.f(b - 73), V.f(sx - 12), V.f(b - 82), V.f(sx + 12), V.f(b - 82), V.f(sx + 12), V.f(b - 74),
        V.f(sx + 12), V.f(b - 67), V.f(sx + 2), V.f(b - 68), V.f(sx + 2), V.f(b - 70), V.f(sx - 1), V.f(b - 66), V.f(sx - 12),
        V.f(b - 66), V.f(sx - 12), V.f(b - 73)), WHITE))
    for k, c in enumerate((PINK, SKY, MINT, HOT)):
        s.circle(sx - 7 + k * 5, b - 76 + (k % 2) * 3, 2, c)
    window(s, sx - 28, b - 46)
    window(s, sx + 18, b - 46)
    door(s, sx, b, 14, 24, SKY)
    for (x, y, h, c) in ((sx - 14, b - 30, 6, PINK), (sx + 12, b - 28, 5, MINT)):  # paint drips
        s.rect(x, y, 2.2, h, c, r=1.1)
        s.circle(x + 1.1, y + h, 1.4, c)
    return s


def b_fair():
    s = new(100, 120)
    sx, b = 50, 120
    st = s.group_start()
    s.poly([(sx - 44, b), (sx + 44, b), (sx, b - 70)], (120, 214, 190), round_r=2)
    s.outline_since(st)
    for k in range(6):
        if k % 2:
            s.poly([(sx - 42 + k * 14, b), (sx - 28 + k * 14, b), (sx, b - 70)], WHITE, flat=True)
    s.stroke_path("M%s %sQ%s %s %s %s" % (V.f(sx - 44), V.f(b - 1), V.f(sx), V.f(b + 1), V.f(sx + 44), V.f(b - 1)), INK, w=0.8)
    body(s, lambda: s.path("M%s %sL%s %sA8 8 0 0 1 %s %sL%s %sZ" % (V.f(sx - 8), V.f(b), V.f(sx - 8), V.f(b - 10), V.f(sx + 8),
                                                                   V.f(b - 10), V.f(sx + 8), V.f(b)), (110, 80, 120)))
    s.line(sx, b - 84, sx, b - 69, DWOOD, w=1.6)
    body(s, lambda: s.poly([(sx + 1, b - 84), (sx + 11, b - 81), (sx + 1, b - 78)], HOT, round_r=0.6))
    s.circle(sx, b - 85, 1.4, GOLD)
    for k in range(5):  # a garland of lights round the tent
        x = sx - 30 + k * 15
        y = b - 26 + abs(k - 2) * 4
        s.circle(x, y, 1.4, YELLOW if k % 2 else PINK)
    return s


def b_sky():
    s = new(100, 120)
    sx, b = 50, 120
    st = s.group_start()
    pts = [(sx - 20 + math.sin(math.radians((b - y) * 6)) * 6, y) for y in range(b, b - 71, -6)]
    s.stroke_path(V.smooth_open(pts), GREEN, w=4.2)
    s.outline_since(st)
    for i, (x, y) in enumerate(pts[1::2]):
        s.ellipse(x + (4 if i % 2 else -4), y, 3.4, 1.8, (130, 214, 150), rot=(-30 if i % 2 else 30))
    body(s, lambda: [s.ellipse(sx + 4, b - 70, 34, 9, WHITE), s.circle(sx - 14, b - 74, 7, WHITE), s.circle(sx + 20, b - 75, 8, WHITE)])
    body(s, lambda: [s.rect(sx - 6, b - 96, 30, 24, (216, 210, 250), r=1.5), s.rect(sx - 10, b - 104, 9, 30, (226, 220, 252), r=1),
                     s.rect(sx + 19, b - 104, 9, 30, (226, 220, 252), r=1)])
    for (x0, x1) in ((sx - 12, sx + 1), (sx + 17, sx + 30)):
        body(s, lambda x0=x0, x1=x1: s.poly([(x0, b - 104), (x1, b - 104), ((x0 + x1) / 2, b - 116)], PINK, round_r=1))
    body(s, lambda: s.path("M%s %sL%s %sA4 4 0 0 1 %s %sL%s %sZ" % (V.f(sx + 5), V.f(b - 74), V.f(sx + 5), V.f(b - 82), V.f(sx + 13),
                                                                   V.f(b - 82), V.f(sx + 13), V.f(b - 74)), PURPLE))
    for x in (sx - 7.5, sx + 21.5):
        s.rect(x, b - 96, 4, 5, SKY, r=2, flat=True)
    s.path(V.rounded_poly(V.star_pts(sx + 9, b - 89, 3.2, 0.45), 0.3), YELLOW)
    return s


def b_star():
    s = new(100, 120)
    sx, b = 50, 120
    s.rect(sx - 44, b - 10, 88, 10, (150, 204, 244), r=2)
    for k in range(5):
        s.stroke_path("M%s %sq2 -1.6 4 0" % (V.f(sx - 40 + k * 18), V.f(b - 5)), WHITE, w=0.9)
    body(s, lambda: s.rect(sx - 40, b - 14, 60, 4, BROWN, r=1))
    for k in range(4):
        s.rect(sx - 36 + k * 16, b - 10, 2.2, 8, DWOOD, r=0.8)
    body(s, lambda: s.path("M%s %sL%s %sL%s %sL%s %sZ" % (V.f(sx + 21), V.f(b - 84), V.f(sx + 33), V.f(b - 84), V.f(sx + 35),
                                                          V.f(b - 10), V.f(sx + 19), V.f(b - 10)), WHITE))
    for y in (b - 64, b - 40):
        s.path("M%s %sL%s %sL%s %sL%s %sZ" % (V.f(sx + 20.4), V.f(y), V.f(sx + 33.6), V.f(y), V.f(sx + 34), V.f(y + 8), V.f(sx + 20),
                                             V.f(y + 8)), HOT, flat=True)
    body(s, lambda: [s.rect(sx + 18, b - 92, 18, 8, (110, 86, 130), r=1.5), s.path("M%s %sQ%s %s %s %sZ" % (
        V.f(sx + 17), V.f(b - 92), V.f(sx + 27), V.f(b - 102), V.f(sx + 37), V.f(b - 92)), HOT)])
    s.rect(sx + 22, b - 90.5, 10, 5, (255, 246, 200), r=1, flat=True)
    door(s, sx + 27, b - 9, 6, 10, NAVY)
    s.line(sx - 25, b - 27, sx - 25, b - 13, DWOOD, w=1.4)  # a boat
    body(s, lambda: s.poly([(sx - 24, b - 27), (sx - 24, b - 18), (sx - 15, b - 18)], WHITE, round_r=0.5))
    body(s, lambda: s.poly([(sx - 32, b - 14), (sx - 16, b - 14), (sx - 19, b - 10), (sx - 29, b - 10)], ORANGE, round_r=0.8))
    return s


def scaffold():
    s = new(92, 72)
    wood, dark = (214, 180, 140), (186, 150, 110)
    st = s.group_start()
    for k in range(4):
        s.rect(2 + k * 29, 2, 3.2, 70, wood, r=1)
    for y in (6, 28, 50):
        s.rect(2, y, 90, 3.2, wood, r=1)
    s.outline_since(st, ow=0.55)
    for k in range(4):
        s.line(3.6 + k * 29, 6, 3.6 + k * 29, 70, dark, w=0.5)
    s.raw("<circle cx=\"46\" cy=\"31\" r=\"5\" fill=\"none\" stroke=\"%s\" stroke-width=\"2\"/>" % V.hexc(DIM))
    body(s, lambda: s.rect(37, 32, 18, 14, (176, 164, 196), r=3))
    s.circle(46, 38, 1.6, INK, flat=True)
    s.rect(45.4, 38, 1.2, 4, INK, flat=True)
    s.shine(40, 34.5, 0.8)
    # a few yellow-and-white barrier stripes
    body(s, lambda: s.rect(4, 62, 84, 6, YELLOW, r=1.5))
    for k in range(7):
        s.poly([(8 + k * 12, 62), (13 + k * 12, 62), (9 + k * 12, 68), (4 + k * 12, 68)], WHITE, flat=True)
    return s


def house():
    s = new(80, 72)
    sx, b = 40, 72
    chimney(s, sx + 16, b - 66)
    wall(s, sx - 30, b - 48, 60, 48, PEACH)
    roof(s, sx - 36, sx + 36, b - 48, 22, HOT)
    window(s, sx - 22, b - 38, box=True)
    window(s, sx + 12, b - 38, box=True)
    door(s, sx, b, 14, 24, PURPLE)
    s.path(V.heart_path(sx, b - 60.5, 4), WHITE)
    s.path(V.heart_path(sx, b - 60.5, 2.3), HOT, flat=True)
    return s


def pethouse():
    s = new(22, 20, pad=1.5, ow=0.6)
    body(s, lambda: s.rect(4, 10, 14, 10, SKY, r=1))
    body(s, lambda: s.poly([(2, 10.5), (20, 10.5), (11, 3)], HOT, round_r=1))
    s.path("M8 20L8 17A3 3 0 0 1 14 17L14 20Z", INK)
    s.path(V.heart_path(11, 7.6, 1.4), WHITE)
    return s


def cottage(col):
    s = new(24, 26, pad=1.5, ow=0.6)
    body(s, lambda: s.rect(3, 12, 18, 14, col, r=1))
    body(s, lambda: s.poly([(1, 12.5), (23, 12.5), (12, 2.5)], HOT, round_r=1.2))
    s.path("M9.5 26L9.5 21.5A2.5 2.5 0 0 1 14.5 21.5L14.5 26Z", BROWN)
    s.rect(5, 15, 4, 4, SKY, r=0.8)
    s.rect(15, 15, 4, 4, SKY, r=0.8)
    s.shine(6, 16, 0.5)
    return s


def lamp():
    s = new(20, 42, pad=1.5, ow=0.6)
    body(s, lambda: [s.rect(9, 12, 2.2, 28, (110, 86, 130), r=1), s.rect(6, 39, 8, 3, (110, 86, 130), r=1.2),
                     s.path("M5 12L15 12L13 8L7 8Z", (110, 86, 130))])
    s.circle(10, 10, 4, (255, 246, 200))
    s.raw("<circle cx=\"10\" cy=\"10\" r=\"4\" fill=\"none\" stroke=\"%s\" stroke-width=\".7\"/>" % V.hexc(INK))
    s.path("M6.5 6L13.5 6L12 4L8 4Z", (110, 86, 130))
    s.shine(8.6, 8.8, 0.8)
    return s


def lamp_lit():
    s = lamp()
    s.circle(10, 10, 3.2, YELLOW, flat=True)
    s.shine(8.8, 8.8, 0.9)
    return s


def fountain():
    s = new(20, 20, pad=1.5, ow=0.6)
    body(s, lambda: s.ellipse(10, 15.5, 9.5, 4.5, (200, 214, 236)))
    s.ellipse(10, 14.5, 7, 2.6, BLUE)
    s.gloss(7, 14, 2.4, 0.8, rot=0, op=0.6)
    body(s, lambda: s.rect(8.8, 6, 2.4, 9, (200, 214, 236), r=1))
    s.ellipse(10, 6, 3, 1.2, SKY)
    return s


def tree():
    s = new(24, 36, pad=1.5, ow=0.6)
    body(s, lambda: [s.rect(8, 26, 4, 10, BROWN, r=1), s.circle(10, 20, 9, GREEN), s.circle(5, 23, 5, GREEN), s.circle(15, 23, 5, GREEN)])
    s.gloss(6.5, 15.5, 3, 1.6, op=0.4)
    for (x, y) in ((6, 20), (13, 17), (11, 24)):
        s.circle(x, y, 1, (255, 196, 210), flat=True)
    return s


def balloon(col=HOT):
    s = new(8, 16, pad=1, ow=0.5)
    s.stroke_path("M4 9Q2.5 12 4 16", DIM, w=0.6)
    body(s, lambda: [s.ellipse(4, 4.5, 4, 4.6, col), s.poly([(3.2, 8.8), (4.8, 8.8), (4, 10)], col)])
    s.gloss(2.7, 3, 1, 1.5, op=0.6)
    return s


def funfair_base():
    """the carousel's base and roof (the wheel and the horses move: the rig)"""
    s = new(104, 90)
    sx, b = 46, 90
    cx, cy = sx + 26, b - 56
    s.line(cx, cy, cx - 12, b, DIM, w=2)
    s.line(cx, cy, cx + 12, b, DIM, w=2)
    body(s, lambda: s.rect(sx - 40, b - 12, 44, 12, PINK, r=2))
    for k in range(4):
        s.circle(sx - 34 + k * 12, b - 6, 1.5, YELLOW, flat=True)
    body(s, lambda: s.poly([(sx - 46, b - 40), (sx + 10, b - 40), (sx - 18, b - 58)], HOT, round_r=2))
    st = s.group_start()
    for k in range(5):
        s.circle(sx - 40 + k * 11, b - 39, 5.5, YELLOW if k % 2 == 0 else WHITE)
    s.outline_since(st, ow=0.6)
    s.circle(sx - 18, b - 59, 2, GOLD)
    return s


def kitchen():
    s = new(80, 56, pad=6)
    sx, b = 40, 56
    body(s, lambda: s.rect(sx - 34, b - 40, 68, 34, (255, 228, 238), r=6))
    s.rect(sx - 34, b - 14, 68, 4, HOT, flat=True)
    st = s.group_start()
    for k in range(6):
        s.path("M%s %sL%s %sL%s %sA7 5 0 0 1 %s %sZ" % (V.f(sx - 40 + k * 14), V.f(b - 54), V.f(sx - 26 + k * 14), V.f(b - 54),
                                                     V.f(sx - 26 + k * 14), V.f(b - 44), V.f(sx - 40 + k * 14), V.f(b - 44)),
               MINT if k % 2 == 0 else WHITE)
    s.outline_since(st)
    body(s, lambda: s.rect(sx - 30, b - 34, 60, 14, (255, 250, 240), r=2))
    return s


def kitchen_wheels():
    s = new(60, 11, pad=4, ow=0.6)
    for x in (8, 52):
        body(s, lambda x=x: s.circle(x, 7, 6, (110, 86, 130)))
        s.circle(x, 7, 3, GREY)
        s.circle(x, 7, 1, INK, flat=True)
    return s


def cloud(op=1.0):
    s = new(33, 21, pad=1.5, ow=0.0)
    s.light, s.dark = 0.0, 0.0
    g = s.lgrad([(0, WHITE), (0.7, WHITE), (1, (226, 232, 250))])
    for (x, y, r) in ((7, 13, 6), (16, 9, 8.2), (26, 13, 6), (12, 14.5, 5), (21, 14.5, 5)):
        s.raw("<circle cx=\"%s\" cy=\"%s\" r=\"%s\" fill=\"%s\"/>" % (V.f(x), V.f(y), V.f(r), g))
    s.raw("<rect x=\"7\" y=\"13\" width=\"19\" height=\"6\" rx=\"3\" fill=\"%s\"/>" % g)
    s.gloss(13, 5.5, 3, 1.4, rot=-10, op=0.8)
    return s


# ------------------------------------------------------------------ game things
def coin():
    s = new(16, 16, pad=1, ow=0.4)  # (as bright as the 1x coin: a thin outline, a light star)
    body(s, lambda: s.circle(8, 8, 7.2, GOLD))
    s.circle(8, 8, 5.2, YELLOW)
    s.path(V.rounded_poly(V.star_pts(8, 8.2, 3.2, 0.45), 0.25), (250, 214, 110), flat=True)
    s.gloss(5.4, 5, 1.6, 0.8, op=0.75)
    return s


def rock():
    s = new(22, 18, pad=1, ow=0.6)
    body(s, lambda: s.path("M2 13C1 8 4 3 9 2.5C14 1.5 20 4 20.5 9C21 14 17 16.5 11 16.5C6 16.5 2.6 15.6 2 13Z", (176, 164, 196)))
    s.circle(7, 9, 1.6, (150, 138, 172), flat=True)
    s.circle(14, 11, 1.1, (150, 138, 172), flat=True)
    s.gloss(8, 5.5, 2.5, 1, op=0.5)
    return s


def power(wand):
    s = new(24, 24, pad=1, ow=0.6)
    body(s, lambda: s.circle(12, 12, 11, WHITE))
    if wand:
        body(s, lambda: s.path(V.rounded_poly(V.star_pts(12, 12.5, 7.4, 0.46), 0.6), LAV))
    else:
        body(s, lambda: s.circle(12, 12, 5.2, MINT))
        s.stroke_path("M12 8.5L12 12L14.5 13.5", INK, w=0.9)
    s.gloss(7.5, 7, 2.6, 1.2, op=0.8)
    return s


def gate():
    s = new(50, 46, pad=1.5, ow=0.65)
    body(s, lambda: [s.rect(7, 5, 4, 40, BROWN, r=1.2), s.rect(39, 5, 4, 40, BROWN, r=1.2)])
    body(s, lambda: s.rect(1, 1, 48, 10, PEACH, r=4))
    for x in (9, 41):
        s.circle(x, 1.8, 1.8, PINK)
    s.gloss(8, 3.5, 6, 1, rot=0, op=0.6)
    return s


def pad(gone):
    s = new(54, 30, pad=1.5, ow=0.65)
    col = GREY if gone else YELLOW
    body(s, lambda: s.rect(1, 1, 52, 28, col, r=6))
    s.rect(4, 4, 46, 6, WHITE, r=3, opacity=0.45, flat=True)
    return s


def gate_big_post():
    s = new(62, 50, pad=1.5, ow=0.7)
    body(s, lambda: [s.rect(5, 5, 5, 44, BROWN, r=1.5), s.rect(52, 5, 5, 44, BROWN, r=1.5)])
    body(s, lambda: s.rect(1, 1, 60, 8, PEACH, r=4))
    s.gloss(10, 3, 8, 1, rot=0, op=0.6)
    return s


def gate_door():
    s = new(20, 34, pad=1, ow=0.55)
    body(s, lambda: s.rect(0, 0, 20, 34, (214, 170, 120), r=1.5))
    for x in (5, 10, 15):
        s.line(x, 2, x, 32, (190, 146, 100), w=0.6)
    s.rect(2, 8, 16, 2.4, (190, 146, 100), r=1, flat=True)
    s.rect(2, 24, 16, 2.4, (190, 146, 100), r=1, flat=True)
    return s


def boost(state):
    s = new(58, 24, pad=1.5, ow=0.65)
    col = YELLOW if state == 2 else PEACH if state == 1 else GREY
    body(s, lambda: s.rect(1, 1, 56, 22, col, r=6))
    s.rect(4, 3.5, 50, 5, WHITE, r=2.5, opacity=0.45, flat=True)
    return s


def mound():
    s = new(62, 24, pad=1.5, ow=0.7)
    body(s, lambda: s.path("M1 23C1 10 10 2 31 2C52 2 61 10 61 23Z", BROWN))
    for (x, y) in ((14, 12), (40, 9), (50, 16), (24, 18)):
        s.circle(x, y, 1.3, (150, 108, 76), flat=True)
    s.path("M8 6C12 2 20 1 26 1.5C18 3 12 5 8 9Z", (120, 200, 140))
    s.gloss(20, 6, 7, 1.6, rot=-6, op=0.3)
    return s


def hole():
    """a soft brown mound of earth, as the 1x game draws it (a rounded bar, lit on top): not a dark pit"""
    s = new(54, 16, pad=1.5, ow=0.6)
    body(s, lambda: s.rect(1, 2, 52, 14, (176, 133, 96), r=7))
    s.ellipse(27, 6.5, 20, 2.6, (198, 160, 124), flat=True)
    for (x, y) in ((12, 10), (30, 12), (44, 9)):
        s.circle(x, y, 1.1, (150, 108, 76), flat=True)
    return s


def catch_cloud():
    s = new(48, 27, pad=1.5, ow=0.65)
    body(s, lambda: [s.circle(10, 17, 9, WHITE), s.circle(24, 13, 12, WHITE), s.circle(38, 17, 9, WHITE), s.rect(10, 16, 28, 10, WHITE, r=4)])
    s.gloss(18, 6.5, 4, 1.6, rot=-12, op=0.9)
    for sx in (-1, 1):
        s.blush(24 + sx * 9, 19, 2, 1.2)
    return s


def till(pets):
    s = new(110, 152, pad=1.5, ow=0.8)
    col = MINT if pets else PEACH
    body(s, lambda: s.rect(1, 1, 108, 150, col, r=8))
    s.rect(4, 4, 102, 8, WHITE, r=4, opacity=0.4, flat=True)
    if pets:
        for (x, y) in ((22, 138), (88, 136)):
            for (dx, dy, r) in ((0, 0, 3), (-3.4, -3.6, 1.4), (0, -5, 1.4), (3.4, -3.6, 1.4)):
                s.circle(x + dx, y + dy, r, (130, 206, 176), flat=True)
    else:
        s.rect(30, 140, 50, 6, V.deeper(col, 0.15), r=3, flat=True)
    return s


def oven(cheer):
    s = new(78, 72, pad=1.5, ow=0.75)
    body(s, lambda: s.rect(1, 1, 76, 70, (255, 214, 190), r=10))
    s.rect(4, 4, 70, 6, WHITE, r=3, opacity=0.45, flat=True)
    win = (255, 200, 120) if cheer else (120, 90, 90)
    body(s, lambda: s.rect(9, 21, 60, 42, win, r=8))
    s.raw("<rect x=\"12\" y=\"24\" width=\"54\" height=\"36\" rx=\"6\" fill=\"%s\"/>" % s.rgrad(
        (255, 236, 170) if cheer else (160, 120, 120), win, cx=0.5, cy=0.6, r=0.6, o0=0.9, o1=0.0))
    s.path("M14 26L24 26L16 40Z", WHITE, opacity=0.3, flat=True)
    for (x, c) in ((19, HOT), (31, YELLOW)):
        body(s, lambda x=x, c=c: s.circle(x, 9, 3.2, c))
    s.rect(50, 7, 18, 4, (240, 180, 150), r=2, flat=True)
    return s


def bowl():
    s = new(108, 36, pad=1.5, ow=0.8)
    body(s, lambda: s.path("M4 16C4 32 26 35 54 35C82 35 104 32 104 16Z", (206, 236, 250)))
    s.ellipse(54, 10, 46, 8, (255, 246, 236))
    s.raw("<ellipse cx=\"54\" cy=\"10\" rx=\"46\" ry=\"8\" fill=\"none\" stroke=\"%s\" stroke-width=\"1.2\"/>" % V.hexc(INK))
    s.ellipse(54, 11, 40, 5.4, (255, 236, 214), flat=True)
    s.gloss(24, 22, 9, 2.4, rot=8, op=0.6)
    for k in range(3):
        s.circle(40 + k * 14, 26, 1.6, HOT if k % 2 else MINT, flat=True)
    return s


def cake_layer(col):
    s = new(48, 9, pad=1, ow=0.55)
    body(s, lambda: s.rect(0, 0, 48, 9, col, r=3))
    s.path("M2 1.5Q8 5 12 1.5Q17 5 22 1.5Q27 5 32 1.5Q37 5 42 1.5Q45 4 46 1.5", WHITE, opacity=0.0, flat=True)
    s.stroke_path("M3 2.2Q8 5.4 13 2.2Q18 5.4 23 2.2Q28 5.4 33 2.2Q38 5.4 43 2.2", WHITE, w=1.2)
    return s


def clock_face():
    """a clock face in a 100 x 100 box (the rig scales it to the module's radius)"""
    s = new(100, 100, pad=1, ow=1.0)
    body(s, lambda: s.circle(50, 50, 49, PURPLE))
    s.circle(50, 50, 43, WHITE)
    s.raw("<circle cx=\"50\" cy=\"50\" r=\"43\" fill=\"%s\"/>" % s.rgrad(WHITE, (236, 230, 250), cx=0.45, cy=0.4, r=0.6, o0=1.0, o1=1.0))
    for k in range(60):
        a = math.radians(k * 6)
        r0 = 38.5 if k % 5 else 36.5
        s.line(50 + math.sin(a) * r0, 50 - math.cos(a) * r0, 50 + math.sin(a) * 41, 50 - math.cos(a) * 41, DIM if k % 5 else INK,
               w=0.7 if k % 5 else 1.4)
    s.gloss(32, 22, 9, 3, rot=-35, op=0.7)
    return s


def empty():
    return V.Svg(1, 1, vb=(0, 0, 1, 1))


# ------------------------------------------------------------------ the story's things (16 x 17 boxes, centred (8, 9))
BEADS = [HOT, BLUE, GREEN, PURPLE, ORANGE]


def stage_item(kind, k=0):
    s = new(16, 17, pad=1, ow=0.5)
    cx, cy = 8, 9
    if kind == "bead":
        body(s, lambda: s.circle(cx, cy, 4.2, BEADS[k]))
        s.shine(cx - 1.5, cy - 1.5, 0.9)
    elif kind == "cookie":
        body(s, lambda: s.circle(cx, cy, 5.2, (214, 160, 100)))
        for (x, y) in ((-2, -2), (2, 0), (-1, 2.4), (1.6, -2.8)):
            s.circle(cx + x, cy + y, 0.9, (110, 70, 50), flat=True)
    elif kind == "star":
        body(s, lambda: s.path(V.rounded_poly(V.star_pts(cx, cy + 0.4, 6.4, 0.46), 0.5), YELLOW))
        s.shine(cx - 1.4, cy - 1, 0.7)
    elif kind == "flower":
        flower(s, cx, cy, LAV if k % 2 else PINK, r=5.4)
    elif kind == "friend":
        body(s, lambda: [s.circle(cx - 4, cy - 4, 2, PEACH), s.circle(cx + 4, cy - 4, 2, PEACH), s.circle(cx, cy, 5.2, PEACH)])
        s.eye(cx - 2, cy - 0.6, 0.6, 0.8)
        s.eye(cx + 2, cy - 0.6, 0.6, 0.8)
        s.blush(cx - 3.4, cy + 1.6, 0.8, 0.5)
        s.blush(cx + 3.4, cy + 1.6, 0.8, 0.5)
        s.stroke_path("M%s %sQ%s %s %s %s" % (V.f(cx - 1), V.f(cy + 1.8), V.f(cx), V.f(cy + 2.8), V.f(cx + 1), V.f(cy + 1.8)), INK, w=0.4)
    elif kind == "seed":
        body(s, lambda: s.ellipse(cx, cy, 2.4, 4.2, BROWN))
        s.gloss(cx - 0.8, cy - 1.6, 0.6, 1.2, rot=0)
    elif kind == "candle":
        body(s, lambda: s.rect(cx - 2, cy - 2, 4, 8, SKY, r=1))
        s.path("M%s %sQ%s %s %s %sQ%s %s %s %sZ" % (V.f(cx), V.f(cy - 8.5), V.f(cx + 2.4), V.f(cy - 5), V.f(cx), V.f(cy - 3.4),
                                                   V.f(cx - 2.4), V.f(cy - 5), V.f(cx), V.f(cy - 8.5)), ORANGE)
        s.ellipse(cx, cy - 4.8, 0.8, 1.2, YELLOW, flat=True)
    elif kind == "balloon":
        s.stroke_path("M%s %sQ%s %s %s %s" % (V.f(cx), V.f(cy + 3), V.f(cx - 1.4), V.f(cy + 5), V.f(cx), V.f(cy + 7.5)), DIM, w=0.5)
        body(s, lambda: s.ellipse(cx, cy - 2, 4.2, 5, BEADS[k]))
        s.gloss(cx - 1.6, cy - 4, 0.9, 1.5)
    elif kind == "shell":
        body(s, lambda: s.path("M%s %sC%s %s %s %s %s %sC%s %s %s %s %s %sZ" % (
            V.f(cx - 6), V.f(cy + 2), V.f(cx - 6), V.f(cy - 5), V.f(cx + 6), V.f(cy - 5), V.f(cx + 6), V.f(cy + 2),
            V.f(cx + 4), V.f(cy + 4.5), V.f(cx - 4), V.f(cy + 4.5), V.f(cx - 6), V.f(cy + 2)), PEACH))
        for i in (-1, 0, 1):
            s.line(cx, cy + 3.8, cx + i * 3.6, cy - 2.6, ORANGE, w=0.5)
    elif kind == "letter":
        body(s, lambda: s.rect(cx - 6, cy - 4, 12, 8, WHITE, r=1))
        s.polyline([(cx - 5.5, cy - 3.5), (cx, cy + 0.4), (cx + 5.5, cy - 3.5)], DIM, w=0.6)
        s.path(V.heart_path(cx, cy + 0.2, 1.3), HOT)
    elif kind == "marble":
        body(s, lambda: s.circle(cx, cy, 4.2, SKY if k % 2 else LAV))
        s.stroke_path("M%s %sQ%s %s %s %s" % (V.f(cx - 3), V.f(cy + 1), V.f(cx), V.f(cy - 2), V.f(cx + 3), V.f(cy + 1)), SKY, w=0.9)
        s.shine(cx - 1.5, cy - 1.7, 0.9)
    elif kind == "apple":
        body(s, lambda: s.path("M%s %sC%s %s %s %s %s %sC%s %s %s %s %s %sC%s %s %s %s %s %sC%s %s %s %s %s %sZ" % (
            V.f(cx), V.f(cy - 3.5), V.f(cx - 3), V.f(cy - 6), V.f(cx - 6.5), V.f(cy - 3), V.f(cx - 5.5), V.f(cy + 1.5),
            V.f(cx - 4.5), V.f(cy + 5.5), V.f(cx - 1.5), V.f(cy + 6), V.f(cx), V.f(cy + 5), V.f(cx + 1.5), V.f(cy + 6),
            V.f(cx + 4.5), V.f(cy + 5.5), V.f(cx + 5.5), V.f(cy + 1.5), V.f(cx + 6.5), V.f(cy - 3), V.f(cx + 3), V.f(cy - 6),
            V.f(cx), V.f(cy - 3.5)), (240, 90, 100)))
        s.line(cx, cy - 3.8, cx + 0.6, cy - 6.8, BROWN, w=0.8)
        s.ellipse(cx + 2.4, cy - 6.2, 1.8, 0.9, GREEN, rot=-20)
        s.gloss(cx - 2.8, cy - 1.8, 0.9, 1.4, rot=15)
    elif kind == "leaf":
        body(s, lambda: s.path("M%s %sC%s %s %s %s %s %sC%s %s %s %s %s %sZ" % (
            V.f(cx - 6.5), V.f(cy), V.f(cx - 3), V.f(cy - 4.5), V.f(cx + 4), V.f(cy - 4.5), V.f(cx + 6.5), V.f(cy),
            V.f(cx + 4), V.f(cy + 4), V.f(cx - 3), V.f(cy + 4), V.f(cx - 6.5), V.f(cy)), GREEN))
        s.line(cx - 5, cy, cx + 5, cy, (60, 150, 100), w=0.5)
    elif kind == "card":
        body(s, lambda: s.rect(cx - 4.5, cy - 6, 9, 12, BEADS[k], r=2))
        s.path(V.heart_path(cx, cy, 2), WHITE)
    elif kind == "light":
        body(s, lambda: [s.rect(cx - 2, cy + 2.6, 4, 3.4, DIM, r=0.8), s.circle(cx, cy - 1, 4.2, YELLOW)])
        s.shine(cx - 1.4, cy - 2.4, 0.8)
    elif kind == "bug":
        body(s, lambda: [s.circle(cx, cy - 5, 2.2, INK), s.circle(cx, cy, 5, (236, 70, 80))])
        s.line(cx, cy - 4.5, cx, cy + 5, INK, w=0.6)
        for (x, y) in ((-2.6, -1.4), (2.6, -0.6), (-2.2, 2.2), (2.4, 2.6)):
            s.circle(cx + x, cy + y, 0.9, INK, flat=True)
        s.shine(cx - 1.4, cy - 2, 0.55)
    elif kind == "slice":
        body(s, lambda: [s.poly([(cx - 6, cy - 5), (cx + 6, cy - 5), (cx, cy + 6)], YELLOW, round_r=0.8),
                         s.rect(cx - 6.5, cy - 6.5, 13, 2.6, (214, 150, 90), r=1.2)])
        for (x, y) in ((-1.5, -2), (1.8, -1), (0, 1.6)):
            s.circle(cx + x, cy + y, 0.9, HOT, flat=True)
    elif kind == "egg":
        body(s, lambda: s.path("M%s %sC%s %s %s %s %s %sC%s %s %s %s %s %sZ" % (
            V.f(cx), V.f(cy - 5.5), V.f(cx + 4.5), V.f(cy - 5.5), V.f(cx + 4.5), V.f(cy + 5), V.f(cx), V.f(cy + 5),
            V.f(cx - 4.5), V.f(cy + 5), V.f(cx - 4.5), V.f(cy - 5.5), V.f(cx), V.f(cy - 5.5)), CREAM))
        s.gloss(cx - 1.6, cy - 2.2, 0.8, 1.4, rot=10)
    elif kind == "drop":
        body(s, lambda: s.path("M%s %sC%s %s %s %s %s %sC%s %s %s %s %s %sZ" % (
            V.f(cx), V.f(cy - 6), V.f(cx + 2), V.f(cy - 2), V.f(cx + 4.5), V.f(cy), V.f(cx + 4.5), V.f(cy + 2),
            V.f(cx + 4.5), V.f(cy + 7), V.f(cx - 4.5), V.f(cy + 7), V.f(cx - 4.5), V.f(cy + 2)) + "C%s %s %s %s %s %sZ" % (
            V.f(cx - 4.5), V.f(cy), V.f(cx - 2), V.f(cy - 2), V.f(cx), V.f(cy - 6)), SKY))
        s.gloss(cx - 1.8, cy + 1, 0.8, 1.4, rot=10)
    elif kind == "coin":
        body(s, lambda: s.circle(cx, cy, 5.2, GOLD))
        s.circle(cx, cy, 3.4, YELLOW)
        s.shine(cx - 1.6, cy - 1.6, 0.7)
    elif kind == "chair":
        body(s, lambda: [s.rect(cx - 4, cy - 6, 2.2, 12, BROWN, r=0.8), s.rect(cx - 4, cy, 9, 2.2, BROWN, r=0.8),
                         s.rect(cx + 3, cy, 2, 6, BROWN, r=0.8)])
    elif kind == "carrot":
        body(s, lambda: s.poly([(cx - 3.2, cy - 4), (cx + 3.2, cy - 4), (cx, cy + 7)], ORANGE, round_r=1))
        s.ellipse(cx - 1.4, cy - 6, 0.8, 2, GREEN, rot=-15)
        s.ellipse(cx + 1.4, cy - 6, 0.8, 2, GREEN, rot=15)
        for y in (-1, 2):
            s.line(cx - 1.6, cy + y, cx, cy + y, (220, 120, 60), w=0.4)
    return s


STAGE_ITEMS = ["bead", "cookie", "star", "flower", "friend", "seed", "candle", "balloon", "shell", "letter", "marble", "apple", "leaf",
               "card", "light", "bug", "slice", "egg", "drop", "coin", "chair", "carrot"]


def cute(s, cx, cy):
    s.eye(cx - 3, cy + 0.6, 0.55, 0.75)
    s.eye(cx + 3, cy + 0.6, 0.55, 0.75)
    s.blush(cx - 4.6, cy + 2.2, 0.9, 0.5)
    s.blush(cx + 4.6, cy + 2.2, 0.9, 0.5)
    s.stroke_path("M%s %sQ%s %s %s %s" % (V.f(cx - 0.9), V.f(cy + 2.1), V.f(cx), V.f(cy + 3), V.f(cx + 0.9), V.f(cy + 2.1)), INK, w=0.4)


def stage_prop(kind):
    """26 x 21 boxes: cx -> 13, by -> 20 (y = by - 18 -> 2)"""
    s = new(26, 21, pad=1, ow=0.55)
    cx, by = 13, 20
    y = by - 18
    if kind == "bag":
        s.raw("<circle cx=\"%s\" cy=\"%s\" r=\"4\" fill=\"none\" stroke=\"%s\" stroke-width=\"1.4\"/>" % (V.f(cx), V.f(y + 4), V.hexc(BROWN)))
        body(s, lambda: s.rect(cx - 9, y + 5, 18, 13, (236, 196, 150), r=5))
        s.rect(cx - 7, y + 7, 14, 1.2, (250, 226, 196), r=0.6, flat=True)
        cute(s, cx, y + 10)
    elif kind == "tray":
        body(s, lambda: s.ellipse(cx, by - 3, 13, 5, (190, 180, 205)))
        s.ellipse(cx, by - 4, 12, 4, WHITE)
        s.ellipse(cx, by - 4, 9, 2, (226, 226, 238), flat=True)
    elif kind == "box":
        body(s, lambda: [s.rect(cx - 9, y + 6, 18, 12, PINK, r=1), s.rect(cx - 10, y + 3, 20, 4, HOT, r=1)])
        s.rect(cx - 1, y + 3, 3, 15, YELLOW, flat=True)
        s.path("M%s %sQ%s %s %s %sQ%s %s %s %sZ" % (V.f(cx + 0.5), V.f(y + 3), V.f(cx - 4), V.f(y - 2), V.f(cx - 3), V.f(y + 3),
                                                   V.f(cx + 4), V.f(y - 2), V.f(cx + 0.5), V.f(y + 3)), YELLOW)
        cute(s, cx - 4.5, y + 10)
    elif kind == "jar":
        body(s, lambda: [s.rect(cx - 8, y + 4, 16, 14, (206, 236, 250), r=4), s.rect(cx - 7, y + 1, 14, 4, BROWN, r=1)])
        s.rect(cx - 5, y + 6, 2, 8, WHITE, r=1, opacity=0.8, flat=True)
        cute(s, cx + 1, y + 11)
    elif kind == "bus":
        body(s, lambda: [s.rect(cx - 12, y + 3, 24, 12, YELLOW, r=3), s.circle(cx - 7, by - 2, 2.4, INK), s.circle(cx + 7, by - 2, 2.4, INK)])
        for i in range(3):
            s.rect(cx - 9 + i * 7, y + 5, 5, 4, SKY, r=1)
        s.circle(cx - 7, by - 2, 0.9, GREY, flat=True)
        s.circle(cx + 7, by - 2, 0.9, GREY, flat=True)
    elif kind == "tent":
        body(s, lambda: s.poly([(cx - 11, by), (cx + 11, by), (cx, y + 1)], ORANGE, round_r=0.8))
        s.poly([(cx - 3, by), (cx + 3, by), (cx, y + 9)], (110, 80, 120), flat=True)
        s.line(cx, y + 1, cx, y - 1, DWOOD, w=0.8)
    elif kind == "ring":
        body(s, lambda: s.circle(cx, y + 10, 8, (236, 200, 120)))
        s.circle(cx, y + 10, 5, WHITE)
        s.shine(cx - 4.4, y + 6.4, 0.8)
    elif kind == "row":
        body(s, lambda: s.rect(cx - 12, by - 6, 24, 6, BROWN, r=3))
        for i in range(3):
            s.ellipse(cx - 8 + i * 8 - 0.8, by - 8, 0.9, 2, GREEN, rot=-25)
            s.ellipse(cx - 8 + i * 8 + 0.8, by - 8, 0.9, 2, GREEN, rot=25)
    elif kind == "basket":
        s.raw("<path d=\"M%s %sA7 7 0 0 1 %s %s\" fill=\"none\" stroke=\"%s\" stroke-width=\"1.4\"/>" % (
            V.f(cx - 7), V.f(y + 8), V.f(cx + 7), V.f(y + 8), V.hexc(BROWN)))
        body(s, lambda: s.rect(cx - 10, y + 8, 20, 10, (214, 170, 110), r=4))
        s.line(cx - 9, y + 11.5, cx + 9, y + 11.5, BROWN, w=0.6)
        cute(s, cx, y + 13)
    elif kind == "cake":
        body(s, lambda: [s.rect(cx - 10, y + 7, 20, 11, PEACH, r=3), s.rect(cx - 10, y + 5, 20, 5, PINK, r=2)])
        s.circle(cx, y + 4, 1.6, RED)
        cute(s, cx, y + 11)
    elif kind == "pot":
        body(s, lambda: [s.poly([(cx - 9, y + 6), (cx + 9, y + 6), (cx + 6, by), (cx - 6, by)], (222, 130, 90), round_r=1),
                         s.rect(cx - 10, y + 4, 20, 4, (236, 150, 110), r=1.2)])
        cute(s, cx, y + 10)
    elif kind == "blanket":
        body(s, lambda: s.rect(cx - 12, by - 8, 24, 8, PINK, r=2))
        for i in range(4):
            s.rect(cx - 10 + i * 6, by - 8, 3, 8, WHITE, flat=True, opacity=0.9)
    elif kind == "page":
        body(s, lambda: s.rect(cx - 8, y + 1, 16, 17, WHITE, r=1.4))
        for k in range(4):
            s.line(cx - 5.5, y + 5 + k * 3.2, cx + 5.5, y + 5 + k * 3.2, LAV, w=0.6)
    elif kind == "leaf":
        body(s, lambda: s.path("M%s %sC%s %s %s %s %s %sC%s %s %s %s %s %sZ" % (
            V.f(cx - 11), V.f(by - 6), V.f(cx - 6), V.f(by - 12), V.f(cx + 6), V.f(by - 12), V.f(cx + 11), V.f(by - 6),
            V.f(cx + 6), V.f(by), V.f(cx - 6), V.f(by), V.f(cx - 11), V.f(by - 6)), GREEN))
        s.line(cx - 9, by - 6, cx + 9, by - 6, (60, 150, 100), w=0.6)
    elif kind == "chair":
        body(s, lambda: [s.rect(cx - 6, y + 1, 3, 17, BROWN, r=1), s.rect(cx - 6, y + 9, 13, 3, BROWN, r=1), s.rect(cx + 5, y + 9, 2, 9, BROWN, r=0.8)])
    elif kind == "boat":
        body(s, lambda: s.poly([(cx - 12, by - 7), (cx + 12, by - 7), (cx + 8, by), (cx - 8, by)], BLUE, round_r=1))
        body(s, lambda: s.poly([(cx + 0.5, y + 1), (cx + 0.5, by - 8), (cx + 9, by - 8)], WHITE, round_r=0.5))
        s.line(cx, y, cx, by - 7, DWOOD, w=0.8)
    elif kind == "cup":
        s.raw("<circle cx=\"%s\" cy=\"%s\" r=\"3\" fill=\"none\" stroke=\"%s\" stroke-width=\"1.4\"/>" % (V.f(cx + 8), V.f(y + 10), V.hexc(INK)))
        body(s, lambda: s.rect(cx - 7, y + 5, 14, 13, WHITE, r=3))
        s.rect(cx - 6, y + 6, 12, 3, PEACH, r=1.4, flat=True)
        cute(s, cx, y + 11)
    return s


STAGE_PROPS = ["bag", "tray", "box", "jar", "bus", "tent", "ring", "row", "basket", "cake", "pot", "blanket", "page", "leaf", "chair",
               "boat", "cup"]


def scenes():
    out = [("town/garden", b_garden()), ("town/post", b_post()), ("town/bakery", b_bakery()), ("town/clock", b_clock()),
           ("town/shop", b_shop()), ("town/art", b_art()), ("town/fair", b_fair()), ("town/castle", b_sky()), ("town/star", b_star()),
           ("town/scaffold", scaffold()), ("town/house", house()), ("town/pethouse", pethouse()),
           ("town/cottage", cottage(PEACH)), ("town/cottage_sky", cottage(SKY)), ("town/lamp", lamp()), ("town/lamp_lit", lamp_lit()),
           ("town/fountain", fountain()), ("town/tree", tree()), ("town/balloon", balloon()), ("town/funfair", funfair_base()),
           ("town/kitchen", kitchen()), ("town/kitchen_wheels", kitchen_wheels()), ("town/cloud", cloud()),
           ("game/coin", coin()), ("game/rock", rock()), ("game/power_wand", power(True)), ("game/power_slow", power(False)),
           ("game/gate", gate()), ("game/pad", pad(False)), ("game/pad_gone", pad(True)), ("game/gate_big_posts", gate_big_post()),
           ("game/gate_door", gate_door()), ("game/boost_0", boost(0)), ("game/boost_1", boost(1)), ("game/boost_2", boost(2)),
           ("game/mound", mound()), ("game/hole", hole()), ("game/cloud", catch_cloud()), ("game/till", till(False)),
           ("game/till_pets", till(True)), ("game/oven", oven(False)), ("game/oven_hot", oven(True)), ("game/bowl", bowl()),
           ("game/cake_pink", cake_layer(PINK)), ("game/cake_cream", cake_layer(CREAM)), ("game/clock_face", clock_face()),
           ("game/clock_ticks", empty())]
    out += [("stage/" + k, stage_item(k)) for k in STAGE_ITEMS]
    out += [("stage/%s_%d" % (k, i), stage_item(k, i)) for k in ("bead", "balloon", "card") for i in range(1, 5)]
    out += [("stage/%s_1" % k, stage_item(k, 1)) for k in ("flower", "marble")]
    out += [("prop/" + k, stage_prop(k)) for k in STAGE_PROPS]
    return out
