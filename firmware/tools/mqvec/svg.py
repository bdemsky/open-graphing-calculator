"""The kawaii vector style of Sparkle Town's Mac art (tools/gen_mq_vector.py).

An Svg is a little scene graph that writes one minified SVG: shapes with soft vertical gradients
(lighter on top, a shade deeper below), clean plum outlines around the union of whatever was
drawn before outline() (a stroke behind the fills, so overlapping parts read as one body), cut-outs
(masks) whose cut edges are outlined too, glossy shines, blush and sparkly eyes. Coordinates are in
the picture's own units (its viewBox): art pixels for items and stickers, the 40 x 40 design space
of the cast, logical screen pixels for scenes.
"""
import math

INK = (90, 61, 92)
WHITE = (255, 255, 255)
BLUSH = (255, 150, 170)


def hexc(c):
    return "#%02x%02x%02x" % tuple(max(0, min(255, int(round(v)))) for v in c[:3])


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def lighter(c, t=0.3):
    return mix(c, WHITE, t)


def deeper(c, t=0.16):
    """a shade: darker and a touch toward plum (pastel shadows, never grey-brown)"""
    return mix(mix(c, (0, 0, 0), t * 0.8), (120, 70, 150), t * 0.35)


def f(v):
    """a number, short"""
    s = ("%.2f" % v).rstrip("0").rstrip(".")
    if s.startswith("0."):
        s = s[1:]
    elif s.startswith("-0."):
        s = "-" + s[2:]
    return s if s not in ("", "-") else "0"


def pts_str(pts):
    return " ".join(f(x) + "," + f(y) for x, y in pts)


# ------------------------------------------------------------------ nodes
class Prim:
    """one shape: tag + geometry attributes; rgb its colour; line: a stroke (width w) not a fill"""

    def __init__(self, tag, geo, rgb, line=0.0, flat=False, opacity=1.0, cap="round", grad=None, extra=""):
        self.tag, self.geo, self.rgb, self.line = tag, geo, rgb, line
        self.flat, self.opacity, self.cap, self.grad, self.extra = flat, opacity, cap, grad, extra

    def geo_str(self):
        return "".join(" %s=\"%s\"" % (k, v) for k, v in self.geo)

    def fill_svg(self, svg):
        op = "" if self.opacity >= 1 else " opacity=\"%s\"" % f(self.opacity)
        if self.line:
            return "<%s%s fill=\"none\" stroke=\"%s\" stroke-width=\"%s\" stroke-linecap=\"%s\" stroke-linejoin=\"round\"%s%s/>" % (
                self.tag, self.geo_str(), hexc(self.rgb), f(self.line), self.cap, op, self.extra)
        paint = self.grad or (hexc(self.rgb) if self.flat or self.rgb == INK else svg.grad(self.rgb))
        return "<%s%s fill=\"%s\"%s%s/>" % (self.tag, self.geo_str(), paint, op, self.extra)

    def stroke_svg(self, svg, ow):
        """the shape fattened by ow on every side, in ink (the outline behind)"""
        if self.opacity < 0.6:
            return ""
        if self.line:
            return "<%s%s fill=\"none\" stroke=\"%s\" stroke-width=\"%s\" stroke-linecap=\"%s\" stroke-linejoin=\"round\"/>" % (
                self.tag, self.geo_str(), hexc(svg.ink), f(self.line + 2 * ow), self.cap)
        return "<%s%s fill=\"%s\" stroke=\"%s\" stroke-width=\"%s\" stroke-linejoin=\"round\"/>" % (
            self.tag, self.geo_str(), hexc(svg.ink), hexc(svg.ink), f(2 * ow))

    def clip_svg(self):
        if self.line:
            return ""
        return "<%s%s/>" % (self.tag, self.geo_str())

    def edge_svg(self, svg, ow):
        return "<%s%s fill=\"none\" stroke=\"%s\" stroke-width=\"%s\"/>" % (self.tag, self.geo_str(), hexc(svg.ink), f(2 * ow))


class Raw:
    """SVG text as is (details: no outline, never part of a union)"""

    def __init__(self, s):
        self.s = s

    def fill_svg(self, svg):
        return self.s

    def stroke_svg(self, svg, ow):
        return ""

    def clip_svg(self):
        return ""


class Group:
    """kind: 'outline' (children outlined as one), 'mask' (children minus erasers), 'clip' (children
    inside the union of clip shapes), 'g' (attrs only)"""

    def __init__(self, kind, children, erasers=None, clip=None, attrs="", ow=None):
        self.kind, self.children, self.erasers, self.clip, self.attrs, self.ow = kind, children, erasers or [], clip, attrs, ow
        self.id = None

    def _mask_id(self, svg):
        if self.id is None:
            self.id = svg.uid("m")
            body = "".join(e.clip_svg().replace("/>", " fill=\"#000\"/>") if not e.line else
                           e.fill_svg(svg).replace(hexc(e.rgb), "#000") for e in self.erasers)
            svg.defs.append("<mask id=\"%s\" maskUnits=\"userSpaceOnUse\" x=\"%s\" y=\"%s\" width=\"%s\" height=\"%s\">"
                            "<rect x=\"%s\" y=\"%s\" width=\"%s\" height=\"%s\" fill=\"#fff\"/>%s</mask>" % (
                                self.id, f(-svg.w), f(-svg.h), f(3 * svg.w), f(3 * svg.h),
                                f(-svg.w), f(-svg.h), f(3 * svg.w), f(3 * svg.h), body))
        return self.id

    def _clip_id(self, svg):
        if self.id is None:
            self.id = svg.uid("c")
            svg.defs.append("<clipPath id=\"%s\">%s</clipPath>" % (self.id, "".join(p.clip_svg() for p in self.clip)))
        return self.id

    def fill_svg(self, svg):
        inner = "".join(c.fill_svg(svg) for c in self.children)
        if self.kind == "outline":
            ow = svg.ow if self.ow is None else self.ow
            strokes = "".join(c.stroke_svg(svg, ow) for c in self.children)
            edges = "".join(edges_of(c, svg, ow) for c in self.children)
            return "<g>%s%s%s</g>" % (strokes, inner, edges)
        if self.kind == "mask":
            return "<g mask=\"url(#%s)\">%s</g>" % (self._mask_id(svg), inner)
        if self.kind == "clip":
            return "<g clip-path=\"url(#%s)\">%s</g>" % (self._clip_id(svg), inner)
        return "<g%s>%s</g>" % (self.attrs, inner)

    def stroke_svg(self, svg, ow):
        if self.kind == "clip":
            return ""
        inner = "".join(c.stroke_svg(svg, ow) for c in self.children)
        if self.kind == "mask":
            return "<g mask=\"url(#%s)\">%s</g>" % (self._mask_id(svg), inner)
        if self.kind == "g" and self.attrs:
            return "<g%s>%s</g>" % (self.attrs, inner)
        return inner

    def clip_svg(self):
        if self.kind == "clip":
            return ""
        return "".join(c.clip_svg() for c in self.children)


def edges_of(node, svg, ow):
    """the cut edges of masks inside node (drawn over the fills, inside what is left)"""
    if not isinstance(node, Group):
        return ""
    out = "".join(edges_of(c, svg, ow) for c in node.children)
    if node.kind == "mask" and node.erasers:
        cid = svg.uid("c")
        svg.defs.append("<clipPath id=\"%s\">%s</clipPath>" % (cid, "".join(c.clip_svg() for c in node.children)))
        es = "".join(e.edge_svg(svg, ow) for e in node.erasers if not e.line)
        out += "<g mask=\"url(#%s)\"><g clip-path=\"url(#%s)\">%s</g></g>" % (node._mask_id(svg), cid, es)
    if node.kind == "g" and node.attrs and out:
        out = "<g%s>%s</g>" % (node.attrs, out)
    return out


# ------------------------------------------------------------------ the picture
class Svg:
    def __init__(self, w, h, ow=0.8, ink=INK, vb=None):
        self.w, self.h, self.ow, self.ink = w, h, ow, ink
        self.vb = vb or (0, 0, w, h)
        self.nodes = []
        self.defs = []
        self._grads = {}
        self._n = 0
        self.light = 0.32   # gradient: how much lighter on top
        self.dark = 0.14    # and deeper at the bottom

    def uid(self, p):
        self._n += 1
        return p + str(self._n)

    def grad(self, rgb, light=None, dark=None, angle=0.25):
        light = self.light if light is None else light
        dark = self.dark if dark is None else dark
        key = (tuple(int(v) for v in rgb[:3]), light, dark, angle)
        if key not in self._grads:
            gid = "g%d" % len(self._grads)
            self._grads[key] = gid
            self.defs.append("<linearGradient id=\"%s\" x1=\"0\" y1=\"0\" x2=\"%s\" y2=\"1\"><stop offset=\"0\" stop-color=\"%s\"/>"
                             "<stop offset=\".55\" stop-color=\"%s\"/><stop offset=\"1\" stop-color=\"%s\"/></linearGradient>" % (
                                 gid, f(angle), hexc(lighter(rgb, light)), hexc(rgb), hexc(deeper(rgb, dark))))
        return "url(#%s)" % self._grads[key]

    def rgrad(self, inner, outer, cx=0.5, cy=0.5, r=0.5, o0=1.0, o1=0.0):
        """a radial gradient (blush, glows): inner colour at opacity o0 to outer at o1"""
        key = ("r", tuple(int(v) for v in inner), tuple(int(v) for v in outer), cx, cy, r, o0, o1)
        if key not in self._grads:
            gid = "g%d" % len(self._grads)
            self._grads[key] = gid
            self.defs.append("<radialGradient id=\"%s\" cx=\"%s\" cy=\"%s\" r=\"%s\"><stop offset=\"0\" stop-color=\"%s\" stop-opacity=\"%s\"/>"
                             "<stop offset=\"1\" stop-color=\"%s\" stop-opacity=\"%s\"/></radialGradient>" % (
                                 gid, f(cx), f(cy), f(r), hexc(inner), f(o0), hexc(outer), f(o1)))
        return "url(#%s)" % self._grads[key]

    def lgrad(self, stops, x1=0, y1=0, x2=0, y2=1, user=False):
        """a linear gradient of [(offset, rgb[, opacity])]"""
        key = ("l", tuple((o, tuple(int(v) for v in c[:3]), (c[3] if len(c) > 3 else 1)) for o, c in stops), x1, y1, x2, y2, user)
        if key not in self._grads:
            gid = "g%d" % len(self._grads)
            self._grads[key] = gid
            st = "".join("<stop offset=\"%s\" stop-color=\"%s\"%s/>" % (
                f(o), hexc(c), "" if len(c) < 4 or c[3] >= 1 else " stop-opacity=\"%s\"" % f(c[3])) for o, c in stops)
            un = " gradientUnits=\"userSpaceOnUse\"" if user else ""
            self.defs.append("<linearGradient id=\"%s\" x1=\"%s\" y1=\"%s\" x2=\"%s\" y2=\"%s\"%s>%s</linearGradient>" % (
                gid, f(x1), f(y1), f(x2), f(y2), un, st))
        return "url(#%s)" % self._grads[key]

    # ---- adding
    def add(self, node):
        self.nodes.append(node)
        return node

    def outline(self, ow=None):
        """everything so far becomes one outlined body"""
        if self.nodes:
            self.nodes = [Group("outline", self.nodes, ow=ow)]

    def erase(self, prim):
        """cut prim out of everything so far"""
        if self.nodes:
            last = self.nodes[-1] if len(self.nodes) == 1 else None
            if isinstance(last, Group) and last.kind == "mask" and False:
                last.erasers.append(prim)
            else:
                self.nodes = [Group("mask", self.nodes, erasers=[prim])]

    def group_start(self):
        return len(self.nodes)

    def group_end(self, start, kind="g", attrs="", clip=None):
        kids = self.nodes[start:]
        del self.nodes[start:]
        g = Group(kind, kids, attrs=attrs, clip=clip)
        self.nodes.append(g)
        return g

    def outline_since(self, start, ow=None):
        """only what was drawn since start becomes an outlined body (on top of what is there)"""
        kids = self.nodes[start:]
        del self.nodes[start:]
        if kids:
            self.nodes.append(Group("outline", kids, ow=ow))

    # ---- shapes (return the Prim)
    def circle(self, cx, cy, r, rgb, **kw):
        return self.add(Prim("circle", [("cx", f(cx)), ("cy", f(cy)), ("r", f(max(0.01, r)))], rgb, **kw))

    def ellipse(self, cx, cy, rx, ry, rgb, rot=0, **kw):
        geo = [("cx", f(cx)), ("cy", f(cy)), ("rx", f(max(0.01, rx))), ("ry", f(max(0.01, ry)))]
        if rot:
            geo.append(("transform", "rotate(%s %s %s)" % (f(rot), f(cx), f(cy))))
        return self.add(Prim("ellipse", geo, rgb, **kw))

    def rect(self, x, y, w, h, rgb, r=0.0, **kw):
        geo = [("x", f(x)), ("y", f(y)), ("width", f(max(0.01, w))), ("height", f(max(0.01, h)))]
        r = min(r, w / 2, h / 2)
        if r > 0.01:
            geo.append(("rx", f(r)))
        return self.add(Prim("rect", geo, rgb, **kw))

    def poly(self, pts, rgb, round_r=0.0, **kw):
        if round_r > 0:
            return self.path(rounded_poly(pts, round_r), rgb, **kw)
        return self.add(Prim("polygon", [("points", pts_str(pts))], rgb, **kw))

    def path(self, d, rgb, **kw):
        return self.add(Prim("path", [("d", d)], rgb, **kw))

    def line(self, x0, y0, x1, y1, rgb, w=1.0, **kw):
        return self.add(Prim("line", [("x1", f(x0)), ("y1", f(y0)), ("x2", f(x1)), ("y2", f(y1))], rgb, line=w, **kw))

    def polyline(self, pts, rgb, w=1.0, **kw):
        return self.add(Prim("polyline", [("points", pts_str(pts))], rgb, line=w, **kw))

    def stroke_path(self, d, rgb, w=1.0, **kw):
        return self.add(Prim("path", [("d", d)], rgb, line=w, **kw))

    def raw(self, s):
        return self.add(Raw(s))

    # ---- kawaii details
    def shine(self, x, y, r=0.55, op=0.95):
        """a glossy white dot"""
        self.raw("<circle cx=\"%s\" cy=\"%s\" r=\"%s\" fill=\"#fff\" opacity=\"%s\"/>" % (f(x), f(y), f(r), f(op)))

    def gloss(self, cx, cy, rx, ry, rot=-30, op=0.55):
        """a soft highlight on a round thing (an ellipse, a little turned)"""
        self.raw("<ellipse cx=\"%s\" cy=\"%s\" rx=\"%s\" ry=\"%s\" fill=\"#fff\" opacity=\"%s\" transform=\"rotate(%s %s %s)\"/>" % (
            f(cx), f(cy), f(rx), f(ry), f(op), f(rot), f(cx), f(cy)))

    def blush(self, cx, cy, rx, ry, rgb=BLUSH, op=0.85):
        g = self.rgrad(rgb, rgb, o0=op, o1=0.0)
        self.raw("<ellipse cx=\"%s\" cy=\"%s\" rx=\"%s\" ry=\"%s\" fill=\"%s\"/>" % (f(cx), f(cy), f(rx * 1.35), f(ry * 1.35), g))

    def eye(self, cx, cy, rx, ry, look=(0, 0), sparkle=1.0, iris=None):
        """a sparkly eye: a dark oval (a hint of colour low down), a big shine and a small one"""
        ink = self.ink
        low = iris or mix(ink, (120, 90, 200), 0.45)
        g = self.lgrad([(0, mix(ink, (0, 0, 0), 0.25)), (0.6, ink), (1, low)])
        x, y = cx + look[0], cy + look[1]
        self.raw("<ellipse cx=\"%s\" cy=\"%s\" rx=\"%s\" ry=\"%s\" fill=\"%s\"/>" % (f(x), f(y), f(rx), f(ry), g))
        if sparkle:
            self.raw("<ellipse cx=\"%s\" cy=\"%s\" rx=\"%s\" ry=\"%s\" fill=\"#fff\"/>" % (
                f(x - rx * 0.32), f(y - ry * 0.36), f(rx * 0.42 * sparkle), f(ry * 0.36 * sparkle)))
            self.raw("<circle cx=\"%s\" cy=\"%s\" r=\"%s\" fill=\"#fff\" opacity=\".9\"/>" % (
                f(x + rx * 0.38), f(y + ry * 0.4), f(min(rx, ry) * 0.2 * sparkle)))

    def arc_line(self, cx, cy, rx, ry, a0, a1, rgb, w):
        """an arc (degrees, y down: 0 right, 90 below) as a round-capped stroke"""
        n = max(4, int(abs(a1 - a0) / 12))
        pts = [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
               for i in range(n + 1)]
        return self.stroke_path(smooth_open(pts), rgb, w)

    # ---- out
    def svg(self):
        body = "".join(n.fill_svg(self) for n in self.nodes)
        vb = " ".join(f(v) for v in self.vb)
        return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"%s\" width=\"%s\" height=\"%s\">%s%s</svg>" % (
            vb, f(self.vb[2]), f(self.vb[3]), "<defs>%s</defs>" % "".join(self.defs) if self.defs else "", body)


# ------------------------------------------------------------------ path helpers
def smooth_open(pts):
    """a smooth curve through the points (Catmull-Rom as cubic Beziers)"""
    if len(pts) < 3:
        return "M" + " L".join(f(x) + " " + f(y) for x, y in pts)
    d = "M%s %s" % (f(pts[0][0]), f(pts[0][1]))
    for i in range(len(pts) - 1):
        p0 = pts[i - 1] if i > 0 else pts[i]
        p1, p2 = pts[i], pts[i + 1]
        p3 = pts[i + 2] if i + 2 < len(pts) else p2
        c1 = (p1[0] + (p2[0] - p0[0]) / 6, p1[1] + (p2[1] - p0[1]) / 6)
        c2 = (p2[0] - (p3[0] - p1[0]) / 6, p2[1] - (p3[1] - p1[1]) / 6)
        d += "C%s %s %s %s %s %s" % (f(c1[0]), f(c1[1]), f(c2[0]), f(c2[1]), f(p2[0]), f(p2[1]))
    return d


def smooth_closed(pts, tension=1.0):
    n = len(pts)
    d = "M%s %s" % (f(pts[0][0]), f(pts[0][1]))
    for i in range(n):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[(i + 1) % n], pts[(i + 2) % n]
        c1 = (p1[0] + (p2[0] - p0[0]) / 6 * tension, p1[1] + (p2[1] - p0[1]) / 6 * tension)
        c2 = (p2[0] - (p3[0] - p1[0]) / 6 * tension, p2[1] - (p3[1] - p1[1]) / 6 * tension)
        d += "C%s %s %s %s %s %s" % (f(c1[0]), f(c1[1]), f(c2[0]), f(c2[1]), f(p2[0]), f(p2[1]))
    return d + "Z"


def rounded_poly(pts, r):
    """a polygon with its corners rounded by r (quadratic curves)"""
    n = len(pts)
    d = ""
    for i in range(n):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % n]
        l0 = math.hypot(p1[0] - p0[0], p1[1] - p0[1]) or 1
        l2 = math.hypot(p2[0] - p1[0], p2[1] - p1[1]) or 1
        k0, k2 = min(r, l0 / 2) / l0, min(r, l2 / 2) / l2
        a = (p1[0] + (p0[0] - p1[0]) * k0, p1[1] + (p0[1] - p1[1]) * k0)
        b = (p1[0] + (p2[0] - p1[0]) * k2, p1[1] + (p2[1] - p1[1]) * k2)
        d += ("M" if i == 0 else "L") + f(a[0]) + " " + f(a[1]) + "Q" + f(p1[0]) + " " + f(p1[1]) + " " + f(b[0]) + " " + f(b[1])
    return d + "Z"


def star_pts(cx, cy, r, inner=0.45, n=5, rot=-90):
    return [(cx + r * (1 if i % 2 == 0 else inner) * math.cos(math.radians(rot + i * 180 / n)),
             cy + r * (1 if i % 2 == 0 else inner) * math.sin(math.radians(rot + i * 180 / n))) for i in range(2 * n)]


def heart_path(cx, cy, r):
    """a plump heart about 2r wide, its middle near (cx, cy)"""
    t = cy - r * 0.55
    return ("M%s %s" % (f(cx), f(cy + r * 1.0)) +
            "C%s %s %s %s %s %s" % (f(cx - r * 0.35), f(cy + r * 0.7), f(cx - r * 1.12), f(cy + r * 0.25), f(cx - r * 1.08), f(t + r * 0.25)) +
            "C%s %s %s %s %s %s" % (f(cx - r * 1.02), f(t - r * 0.45), f(cx - r * 0.2), f(t - r * 0.55), f(cx), f(t + r * 0.05)) +
            "C%s %s %s %s %s %s" % (f(cx + r * 0.2), f(t - r * 0.55), f(cx + r * 1.02), f(t - r * 0.45), f(cx + r * 1.08), f(t + r * 0.25)) +
            "C%s %s %s %s %s %s" % (f(cx + r * 1.12), f(cy + r * 0.25), f(cx + r * 0.35), f(cy + r * 0.7), f(cx), f(cy + r * 1.0)) + "Z")


def ellipse_pts(cx, cy, rx, ry, n=24, a0=0, a1=360):
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
            for i in range(n + (0 if a1 - a0 >= 360 else 1))]
