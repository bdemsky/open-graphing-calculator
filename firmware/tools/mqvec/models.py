"""Sparkle Town's ways to work it as vector art: the pieces of the hands-on models (MODX_HR_SPRITE ids model/...).

The module draws every piece of a model (modules/mathquest/mq_strat.c) with the mq_vis primitives inside an hr
bracket with its name; the Mac page draws these pictures in the bracket's box instead. A picture fills its box,
which the module may stretch (a ribbon as long as its part, a floor piece as wide as its tens), so the shapes are
simple and soft: rounded panels, base-ten blocks, little flowers for the garden rows, treats, bags, plates, a pie,
cake pieces, number cards, coins of place value (discs), the hop's arc, an X mark, the slider's knob. Numbers are
the module's own text, drawn after the bracket. One kawaii style with the rest of the set.
"""
import math

from . import svg as V

INK = (90, 61, 92)
WHITE = (255, 255, 255)
CREAM = (255, 248, 236)
PINK = (255, 179, 199)
HOT = (255, 122, 162)
MINT = (168, 230, 207)
GREEN = (92, 191, 138)
LAV = (201, 182, 255)
PURPLE = (155, 127, 230)
SKY = (159, 216, 255)
BLUE = (90, 169, 230)
PEACH = (255, 211, 165)
YELLOW = (255, 230, 128)
BROWN = (176, 133, 96)
GREY = (217, 217, 224)


def new(w, h, pad=0.0, ow=0.5):
    s = V.Svg(w, h, ow=ow, ink=INK, vb=(-pad, -pad, w + 2 * pad, h + 2 * pad))
    s.light, s.dark = 0.3, 0.12
    return s


def body(s, fn):
    st = s.group_start()
    fn()
    s.outline_since(st)


def cube(rgb):
    s = new(6, 6, ow=0.35)
    body(s, lambda: s.rect(0.35, 0.35, 5.3, 5.3, rgb, r=0.9))
    s.gloss(2, 1.8, 1.2, 0.5, rot=-20, op=0.6)
    return s


def rod():
    s = new(4, 22, ow=0.3)
    body(s, lambda: s.rect(0.3, 0.3, 3.4, 21.4, MINT, r=0.8))
    for i in range(1, 10):
        s.line(0.6, 0.3 + 2.14 * i, 3.4, 0.3 + 2.14 * i, V.deeper(MINT, 0.25), w=0.25, flat=True)
    return s


def flat():
    s = new(14, 14, ow=0.4)
    body(s, lambda: s.rect(0.4, 0.4, 13.2, 13.2, SKY, r=1.2))
    for i in range(1, 5):
        v = 0.4 + 13.2 * i / 5
        s.line(v, 0.8, v, 13.2, V.deeper(SKY, 0.25), w=0.3, flat=True)
        s.line(0.8, v, 13.2, v, V.deeper(SKY, 0.25), w=0.3, flat=True)
    s.gloss(4, 3, 2.6, 0.9, rot=-20, op=0.5)
    return s


def flower(rgb, centre=YELLOW):
    s = new(10, 10, ow=0.4)

    def petals():
        for i in range(5):
            a = 2 * math.pi * i / 5 - math.pi / 2
            s.circle(5 + math.cos(a) * 2.4, 5 + math.sin(a) * 2.4, 2.2, rgb)
    body(s, petals)
    s.circle(5, 5, 1.5, centre, flat=True)
    return s


def treat():
    s = new(8, 8, ow=0.3)
    body(s, lambda: s.circle(4, 4, 3.75, HOT))
    s.circle(2.9, 3.6, 0.45, (255, 236, 242), flat=True)
    s.circle(5, 4.6, 0.45, (255, 236, 242), flat=True)
    s.gloss(3, 2.4, 1.2, 0.5, rot=-30, op=0.7)
    return s


def card(rgb):
    s = new(40, 24, ow=0.6)
    body(s, lambda: s.rect(0.6, 0.6, 38.8, 22.8, rgb, r=4))
    s.gloss(10, 4.5, 7, 1.2, rot=-4, op=0.45)
    return s


def xmark():
    s = new(10, 10, ow=0.0)
    s.line(1.4, 1.4, 8.6, 8.6, HOT, w=2.0)
    s.line(1.4, 8.6, 8.6, 1.4, HOT, w=2.0)
    return s


def hop():
    """the hop's arc from its bottom-left corner to its bottom-right one (stretched to the hop)"""
    s = new(40, 20, ow=0.0)
    s.raw("<path d=\"M1 19.5C4 1 36 1 39 19.5\" fill=\"none\" stroke=\"%s\" stroke-width=\"1.6\" stroke-linecap=\"round\"/>" % V.hexc(HOT))
    s.raw("<path d=\"M36.2 15.4L39 19.5L39.6 14.6\" fill=\"none\" stroke=\"%s\" stroke-width=\"1.3\" stroke-linecap=\"round\" "
          "stroke-linejoin=\"round\"/>" % V.hexc(HOT))
    return s


def handle():
    s = new(16, 16, ow=0.7)
    body(s, lambda: s.circle(8, 8, 7.2, PEACH))
    s.circle(8, 8, 3, YELLOW, flat=True)
    s.gloss(5.6, 5, 2.2, 1, rot=-30, op=0.7)
    return s


def house():
    s = new(96, 100, ow=0.0)
    s.rect(0.5, 0.5, 95, 99, (250, 247, 255), r=8, flat=True)
    s.raw("<rect x=\".5\" y=\".5\" width=\"95\" height=\"99\" rx=\"8\" fill=\"none\" stroke=\"%s\" stroke-width=\"1\"/>" % V.hexc(LAV))
    s.rect(4, 3, 88, 12, (238, 230, 255), r=5, flat=True)
    return s


def jar():
    s = new(96, 70, ow=0.0)
    s.rect(0.5, 0.5, 95, 69, (236, 248, 255), r=12, flat=True, opacity=0.95)
    s.raw("<rect x=\".5\" y=\".5\" width=\"95\" height=\"69\" rx=\"12\" fill=\"none\" stroke=\"%s\" stroke-width=\"1.2\"/>" % V.hexc(SKY))
    s.rect(8, 6, 80, 4, WHITE, r=2, flat=True, opacity=0.8)
    return s


def pie():
    s = new(94, 94, ow=0.8)
    body(s, lambda: s.circle(47, 47, 46, BROWN))
    s.circle(47, 47, 43, CREAM, flat=True)
    for i in range(16):
        a = 2 * math.pi * i / 16
        s.circle(47 + math.cos(a) * 44.5, 47 + math.sin(a) * 44.5, 1.4, V.lighter(BROWN, 0.25), flat=True)
    return s


def piece(rgb):
    s = new(20, 40, ow=0.0)
    s.rect(0, 0, 20, 40, rgb, flat=False)
    s.raw("<rect x=\".4\" y=\".4\" width=\"19.2\" height=\"39.2\" fill=\"none\" stroke=\"%s\" stroke-width=\".8\"/>" % V.hexc(BROWN))
    if rgb == PINK:
        s.rect(2, 3, 16, 3, WHITE, r=1.5, flat=True, opacity=0.5)
    return s


def pile():
    s = new(64, 80, ow=0.0)
    s.rect(0.5, 0.5, 63, 79, (255, 240, 230), r=10, flat=True)
    s.raw("<rect x=\".5\" y=\".5\" width=\"63\" height=\"79\" rx=\"10\" fill=\"none\" stroke=\"%s\" stroke-width=\"1\"/>" % V.hexc(PEACH))
    return s


def plate():
    s = new(50, 52, ow=0.6)
    body(s, lambda: s.ellipse(25, 26, 24, 25, WHITE))
    s.ellipse(25, 26, 17, 18, (248, 248, 252), flat=True)
    return s


def bag():
    s = new(50, 56, ow=0.7)
    body(s, lambda: s.path("M8 10L42 10C47 10 49 14 49 20L49 47C49 52 46 55 40 55L10 55C4 55 1 52 1 47L1 20C1 14 3 10 8 10Z", PEACH))
    body(s, lambda: s.rect(20, 1, 10, 9, BROWN, r=2))
    s.gloss(12, 18, 5, 2, rot=-20, op=0.5)
    return s


def floor(rgb):
    s = new(40, 40, ow=0.0)
    s.rect(0, 0, 40, 40, rgb)
    for i in range(1, 4):
        s.line(i * 10, 0, i * 10, 40, V.deeper(rgb, 0.12), w=0.4, flat=True)
        s.line(0, i * 10, 40, i * 10, V.deeper(rgb, 0.12), w=0.4, flat=True)
    s.raw("<rect x=\".4\" y=\".4\" width=\"39.2\" height=\"39.2\" fill=\"none\" stroke=\"%s\" stroke-width=\".8\"/>" % V.hexc(BROWN))
    return s


def ribbon(rgb):
    s = new(40, 14, ow=0.0)
    s.rect(0, 0, 40, 14, rgb, r=2)
    s.rect(1, 2, 38, 2.4, WHITE, r=1.2, flat=True, opacity=0.45)
    s.raw("<rect x=\".4\" y=\".4\" width=\"39.2\" height=\"13.2\" rx=\"2\" fill=\"none\" stroke=\"%s\" stroke-width=\".7\"/>" % V.hexc((150, 130, 155)))
    return s


def disc(rgb):
    s = new(16, 16, ow=0.6)
    body(s, lambda: s.circle(8, 8, 7.3, rgb))
    s.raw("<circle cx=\"8\" cy=\"8\" r=\"5.6\" fill=\"none\" stroke=\"%s\" stroke-width=\".5\" stroke-dasharray=\".8 .8\"/>" %
          V.hexc(V.deeper(rgb, 0.2)))
    s.gloss(5.6, 5, 2, 0.8, rot=-30, op=0.7)
    return s


def models():
    return [("model/cube", cube(PINK)), ("model/cube_yellow", cube(YELLOW)), ("model/rod", rod()), ("model/flat", flat()),
            ("model/flower", flower(PINK)), ("model/flower_lav", flower(LAV)), ("model/flower_yellow", flower(YELLOW, PEACH)),
            ("model/bud", flower(GREY, WHITE)), ("model/treat", treat()),
            ("model/card", card(CREAM)), ("model/card_yellow", card(YELLOW)), ("model/card_white", card(WHITE)),
            ("model/card_pink", card(PINK)), ("model/xmark", xmark()), ("model/hop", hop()), ("model/handle", handle()),
            ("model/house", house()), ("model/jar", jar()), ("model/pie", pie()), ("model/piece", piece(CREAM)),
            ("model/piece_pink", piece(PINK)), ("model/pile", pile()), ("model/plate", plate()), ("model/bag", bag()),
            ("model/floor", floor(PEACH)), ("model/floor_mint", floor(MINT)),
            ("model/ribbon_pink", ribbon(PINK)), ("model/ribbon_sky", ribbon(SKY)), ("model/ribbon_lav", ribbon(LAV)),
            ("model/ribbon_yellow", ribbon(YELLOW)), ("model/ribbon_mint", ribbon(MINT)), ("model/ribbon_peach", ribbon(PEACH)),
            ("model/disc_pink", disc(PINK)), ("model/disc_mint", disc(MINT)), ("model/disc_sky", disc(SKY))]
