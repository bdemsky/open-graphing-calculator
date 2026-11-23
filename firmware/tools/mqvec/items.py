"""The home items and stickers as vector art.

The items' designs live in tools/gen_mq_items.py as shape calls on a pixel canvas (rect, ellipse,
disc, rrect, poly, line, star, heart, outline, face, shine, put). VecCanvas runs the same calls
(read-only use of that file): it keeps the pixels (a few items read them back) and records every
shape as vector in art-pixel units, so the vector picture has the exact footprint of the pixel
one. The kawaii renderer of svg.py then gives the shapes soft gradients, round plum outlines,
sparkly faces and glossy shines. Items that the shapes alone do not carry well at a large size
(the window views, which the pixel art sets pixel by pixel, the bird cage) are redrawn by hand
below (OVERRIDES).
"""
import math

import gen_mq_items as GI
from . import svg as V

MASTER = GI.MASTER


def col(v):
    return MASTER[v]


class VecCanvas(GI.Canvas):
    """gen_mq_items.Canvas that also draws an Svg"""

    def __init__(self, w, h):
        GI.Canvas.__init__(self, w, h)
        self.s = V.Svg(w, h, ow=0.62, vb=(-1, -1, w + 2, h + 2))
        self.s.ink = col(GI.INK)
        self._q = 0

    # recording helpers
    def _rec(self):
        return self._q == 0

    def _shape(self, prim_fn, c):
        if c == GI.T:
            p = prim_fn(V.WHITE)
            self.s.nodes.pop()
            self.s.erase(p)
        else:
            prim_fn(col(c))

    def put(self, x, y, c):
        GI.Canvas.put(self, x, y, c)
        if not self._rec():
            return
        x, y = int(x), int(y)
        if not (0 <= x < self.w and 0 <= y < self.h):
            return
        if c == GI.T:
            p = self.s.rect(x, y, 1, 1, V.WHITE)
            self.s.nodes.pop()
            self.s.erase(p)
        elif c == GI.WHITE:
            self.s.shine(x + 0.5, y + 0.5, 0.5)
        elif c == GI.BLUSH:
            self.s.blush(x + 0.5, y + 0.55, 0.6, 0.42)
        else:
            self.s.circle(x + 0.5, y + 0.5, 0.56, col(c), flat=True)

    def rect(self, x, y, w, h, c):
        self._q += 1
        GI.Canvas.rect(self, x, y, w, h, c)
        self._q -= 1
        if not self._rec() or w <= 0 or h <= 0:
            return
        x0, y0, x1, y1 = int(x), int(y), int(x + w), int(y + h)
        if x1 <= x0 or y1 <= y0:
            return
        small = min(x1 - x0, y1 - y0)
        r = 0.0 if c == GI.T else (0.28 if small <= 1 else 0.45)
        self._shape(lambda rgb: self.s.rect(x0, y0, x1 - x0, y1 - y0, rgb, r=r, flat=small <= 1), c)

    def ellipse(self, cx, cy, rx, ry, c):
        self._q += 1
        GI.Canvas.ellipse(self, cx, cy, rx, ry, c)
        self._q -= 1
        if self._rec():
            self._shape(lambda rgb: self.s.ellipse(cx, cy, rx, ry, rgb), c)

    def disc(self, cx, cy, r, c):
        self._q += 1
        GI.Canvas.disc(self, cx, cy, r, c)
        self._q -= 1
        if self._rec():
            self._shape(lambda rgb: self.s.circle(cx, cy, r, rgb), c)

    def rrect(self, x, y, w, h, r, c):
        self._q += 1
        GI.Canvas.rrect(self, x, y, w, h, r, c)
        self._q -= 1
        if self._rec():
            self._shape(lambda rgb: self.s.rect(x, y, w, h, rgb, r=r), c)

    def poly(self, pts, c):
        self._q += 1
        GI.Canvas.poly(self, pts, c)
        self._q -= 1
        if self._rec():
            self._shape(lambda rgb: self.s.poly(pts, rgb, round_r=0.35), c)

    def line(self, x0, y0, x1, y1, c):
        self._q += 1
        GI.Canvas.line(self, x0, y0, x1, y1, c)
        self._q -= 1
        if self._rec():
            self.s.line(round(x0) + 0.5, round(y0) + 0.5, round(x1) + 0.5, round(y1) + 0.5, col(c), w=1.0)

    def star(self, cx, cy, r, c, inner=0.45):
        self._q += 1
        GI.Canvas.star(self, cx, cy, r, c, inner)
        self._q -= 1
        if self._rec():
            self._shape(lambda rgb: self.s.poly(V.star_pts(cx, cy, r * 1.04, inner * 1.05), rgb, round_r=r * 0.12), c)

    def heart(self, cx, cy, r, c):
        self._q += 1
        GI.Canvas.heart(self, cx, cy, r, c)
        self._q -= 1
        if self._rec():
            self._shape(lambda rgb: self.s.path(V.heart_path(cx, cy - r * 0.05, r * 1.02), rgb), c)

    def outline(self, col_=GI.INK):
        GI.Canvas.outline(self, col_)
        if self._rec():
            self.s.outline()

    def face(self, cx, ey, gap=2, mouth=True, cheeks=True):
        self._q += 1
        GI.Canvas.face(self, cx, ey, gap, mouth, cheeks)
        self._q -= 1
        if not self._rec():
            return
        s = self.s
        for sx in (-1, 1):
            ex = round(cx + sx * gap - (1 if sx < 0 else 0)) + 0.5
            if cheeks:
                s.blush(ex + sx * 1.15, ey + 1.6, 0.75, 0.45)
            s.eye(ex, ey + 0.45, 0.55, 0.68, sparkle=0.9)
        if mouth:
            if gap >= 2:
                m = round(cx)
                s.stroke_path("M%s %sQ%s %s %s %s" % (V.f(m - 0.65), V.f(ey + 1.45), V.f(m), V.f(ey + 2.15), V.f(m + 0.65),
                                                       V.f(ey + 1.45)), s.ink, w=0.34)
            else:
                s.stroke_path("M%s %sQ%s %s %s %s" % (V.f(round(cx) + 0.05), V.f(ey + 1.4), V.f(round(cx) + 0.5), V.f(ey + 1.9),
                                                       V.f(round(cx) + 0.95), V.f(ey + 1.4)), s.ink, w=0.3)

    def shine(self, x, y):
        GI.Canvas.put(self, x, y, GI.WHITE)
        if self._rec():
            self.s.shine(int(x) + 0.5, int(y) + 0.5, 0.55)
            self.s.shine(int(x) + 1.25, int(y) + 1.1, 0.22, 0.8)


# ------------------------------------------------------------------ hand-drawn parts
def window_vec(c, view, fr, frame_col=GI.WHITE, shape="square"):
    """gen_mq_items.window, the view clipped to the pane as vector (the pixel art copies it pixel by pixel)"""
    W, H = c.w, c.h
    s = c.s
    st = s.group_start()
    if shape == "round":
        s.circle(W / 2, H / 2, W / 2 - 1, col(frame_col))
        pane = V.Prim("circle", [("cx", V.f(W / 2)), ("cy", V.f(H / 2)), ("r", V.f(W / 2 - 3))], V.WHITE)
    elif shape == "arch":
        s.rect(1, 1, W - 2, H - 2, col(frame_col), r=(W - 2) / 2)
        pane = V.Prim("rect", [("x", "3"), ("y", "3"), ("width", V.f(W - 6)), ("height", V.f(H - 6)), ("rx", V.f((W - 6) / 2))], V.WHITE)
    else:
        s.rect(1, 1, W - 2, H - 2, col(frame_col), r=2)
        pane = V.Prim("rect", [("x", "3"), ("y", "3"), ("width", V.f(W - 6)), ("height", V.f(H - 6)), ("rx", ".6")], V.WHITE)
    # the view, inside the pane
    v = VecCanvas(W, H)
    v.s = s
    vs = s.group_start()
    s.rect(0, 0, W, H, col(GI.SKY), flat=True)
    view(v, fr)
    # the glass: a soft sheen across the pane
    s.raw("<rect x=\"0\" y=\"0\" width=\"%s\" height=\"%s\" fill=\"%s\"/>" % (V.f(W), V.f(H), s.lgrad(
        [(0, (255, 255, 255, 0.0)), (0.42, (255, 255, 255, 0.0)), (0.5, (255, 255, 255, 0.28)), (0.62, (255, 255, 255, 0.0)),
         (1, (255, 255, 255, 0.0))], 0, 0, 1, 1)))
    s.group_end(vs, kind="clip", clip=[pane])
    if shape == "square":
        s.rect(W // 2 - 1, 2.5, 1.2, H - 5, col(frame_col), r=0.4)
        s.rect(2.5, H // 2 - 1, W - 5, 1.2, col(frame_col), r=0.4)
    s.outline_since(st)
    if shape == "square":
        st2 = s.group_start()
        s.rect(0.6, H - 3.2, W - 1.2, 2.4, col(frame_col), r=0.8)  # the sill
        s.outline_since(st2)
    s.shine(2.4, 2.4, 0.6)


def birdcage_vec(c, fr):
    W, H = c.w, c.h
    s = c.s
    st = s.group_start()
    s.rect(2, 3, W - 4, H - 5, col(GI.LGREY), r=5, opacity=0.55, flat=True)
    s.rect(3, H - 3, W - 6, 2, col(GI.GOLD), r=0.6)
    s.circle(W / 2, 2, 1.5, col(GI.GOLD))
    s.outline_since(st)
    by = H / 2 + 1 - fr
    st = s.group_start()
    s.circle(W / 2, by, 3, col(GI.YELLOW))
    s.ellipse(W / 2 + 2.2, by + 0.6, 1.6, 1.0, col(GI.LEMON), rot=-20)
    s.outline_since(st)
    s.poly([(W / 2 + 2.6, by + 0.6), (W / 2 + 3.9, by + 1.1), (W / 2 + 2.6, by + 1.6)], col(GI.ORANGE))
    s.eye(W / 2 - 0.8, by - 0.2, 0.45, 0.55)
    s.blush(W / 2 - 1.8, by + 0.9, 0.6, 0.35)
    for x in range(4, W - 3, 3):  # the bars, over the bird
        s.line(x + 0.5, 4.2, x + 0.5, H - 3, col(GI.GOLD), w=0.7)
    s.raw("<rect x=\"2\" y=\"3\" width=\"%s\" height=\"%s\" rx=\"5\" fill=\"none\" stroke=\"%s\" stroke-width=\".9\"/>" % (
        V.f(W - 4), V.f(H - 5), V.hexc(col(GI.GOLD))))
    s.shine(4, 5)


def moon_path(cx, cy, r, bx, by, br):
    """a crescent: the disc (cx, cy, r) minus the disc (bx, by, br), as one path"""
    d = math.hypot(bx - cx, by - cy)
    if d < 1e-6 or d > r + br:
        return "M%s %sA%s %s 0 1 0 %s %sA%s %s 0 1 0 %s %sZ" % (
            V.f(cx - r), V.f(cy), V.f(r), V.f(r), V.f(cx + r), V.f(cy), V.f(r), V.f(r), V.f(cx - r), V.f(cy))
    a = (r * r - br * br + d * d) / (2 * d)
    hh = math.sqrt(max(0.0, r * r - a * a))
    ux, uy = (bx - cx) / d, (by - cy) / d
    px, py = cx + a * ux, cy + a * uy
    p1 = (px + hh * -uy, py + hh * ux)
    p2 = (px - hh * -uy, py - hh * ux)
    return "M%s %sA%s %s 0 1 1 %s %sA%s %s 0 0 0 %s %sZ" % (
        V.f(p1[0]), V.f(p1[1]), V.f(r), V.f(r), V.f(p2[0]), V.f(p2[1]), V.f(br), V.f(br), V.f(p1[0]), V.f(p1[1]))


OVERRIDES = {"canary": birdcage_vec}


def item_svg(it, fr):
    """the vector picture of an item frame (an Svg)"""
    saved = GI.window
    GI.window = window_vec
    try:
        c = VecCanvas(it["aw"], it["ah"])
        fn = OVERRIDES.get(it["key"])
        if fn:
            fn(c, fr)
        else:
            it["draw"](c, fr)
    finally:
        GI.window = saved
    return c.s


def items():
    """[(id, Svg)] of every item frame; ids item/<key>_<frame>"""
    out = []
    for it in GI.art_items():
        for fr in range(it["frames"]):
            out.append(("item/%s_%d" % (it["key"], fr), item_svg(it, fr)))
    return out
