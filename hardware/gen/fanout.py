"""Ground vias placed by geometry, outside the autorouter.

prefanout(): before routing, every SMD GND pad gets a short stub and a via to
the In1 ground plane (ICs fan inward, under the package). The vias go to
Freerouting as protected wiring, so it routes around them.

stitch(): after routing, ground vias on a grid wherever there is room, so the
GND pours on F/In2/B tie into the plane instead of floating as islands.

bridge_adjacent(): before routing, short same-net neighbouring pads of a part
with a direct track.

pcb_build.py runs bridge_adjacent and prefanout before Freerouting:
prefanout(GND) for every part, then prefanout("3V3") for the MCU and the
bottom-side decoupling caps, and prefanout("3V3_BUS") for the SDRAM and the
same caps. Those 3V3/3V3_BUS vias land in the In2 power islands defined by
layout.py, so each bottom cap is a via pair away from its pin. stitch() runs
after routing. All geometry is shapely in board mm; vias are 0.6 mm pads with
0.3 mm drills, kept 0.9 mm centre to centre from other vias.
"""
import math

from shapely.geometry import LineString, Point, Polygon, box
from shapely.strtree import STRtree

from route import pad_layers, pad_shape, short

VIA_D, VIA_DRILL = 0.6, 0.3
CLR = 0.22          # copper clearance for these checks (covers the 0.2 mm power class)
STUB_W = 0.25       # pad-to-via stub width, mm


class Copper:
    """Copper of other nets, per layer, for clearance queries.

    Holds every pad, track and via (with its short net name) on the three
    routable layers, an STRtree per layer for fast candidate lookup, the
    mounting holes/slots as obstacles, and the via centres for drill spacing.
    In1.Cu (the GND plane) is not modelled.
    """

    def __init__(self, board):
        self.items = {l: [] for l in ("F.Cu", "In2.Cu", "B.Cu")}   # (geom, net)
        for ref, pl in board.placed.items():
            for pad in pl.fp.pads:
                net = short(pl.comp["pads"].get(pad["num"], "")) or None
                kind, data = pad_shape(pl, pad)
                g = Point(data[0], data[1]).buffer(data[2] / 2) if kind == "circle" else Polygon(data)
                for l in pad_layers(pl, pad):
                    if l in self.items:
                        self.items[l].append((g, net))
        for net, layer, a, b, w in board.tracks:
            if layer in self.items:
                self.items[layer].append((LineString([a, b]).buffer(w / 2), short(net)))
        for net, at, size, drill in board.vias:
            g = Point(at).buffer(size / 2)
            for l in self.items:
                self.items[l].append((g, short(net)))
        self.holes = [Point(x, y).buffer(2.6) for x, y in getattr(board, "mount_holes", [])]
        self.holes += [box(*r).buffer(0.5) for r in getattr(board, "slots", [])]
        self.vias = [(Point(at), short(net)) for net, at, _, _ in board.vias]   # any net
        self._index()

    def _index(self):
        """(Re)build the per-layer STRtrees (shapely trees are immutable)."""
        self.trees = {l: (STRtree([g for g, _ in it]), it) for l, it in self.items.items()}

    def clear(self, geom, layers, net, clr=CLR, any_net=False):
        """True if geom grown by clr touches no other-net copper on layers (with
        any_net, no copper at all, same net included) and no hole/slot."""
        g = geom.buffer(clr)
        for l in layers:
            tree, it = self.trees[l]
            for i in tree.query(g):
                other_geom, other_net = it[i]
                if (any_net or other_net != net) and g.intersects(other_geom):
                    return False
        return not any(g.intersects(h) for h in self.holes)

    def via_ok(self, pt, min_d=VIA_D + 0.3):
        """Drill spacing to every existing via, whatever its net."""
        return all(pt.distance(v) >= min_d for v, _ in self.vias)

    def near_via(self, pt, net, reach):
        """An existing same-net via within reach of pt, if any."""
        best = None
        for v, n in self.vias:
            d = pt.distance(v)
            if n == net and d <= reach and (best is None or d < best[0]):
                best = (d, v)
        return best[1] if best else None

    def add(self, geom, layers, net):
        """Record newly placed copper (re-indexes; callers add vias to self.vias themselves)."""
        for l in layers:
            self.items[l].append((geom, net))
        self._index()


def _inside(board, pt, margin):
    """pt lies inside the outline's bounding rectangle by more than margin (corners ignored)."""
    x0, y0, x1, y1, r = board.outline
    return x0 + margin < pt[0] < x1 - margin and y0 + margin < pt[1] < y1 - margin


def prefanout(board, net="GND", inward=("U1", "U2"), only=None):
    """Give every SMD pad of net a stub track and a via (see module doc).

    inward: parts whose vias go toward the part centre (under the package)
    rather than away from it. only: restrict to these references.
    For each pad, first try to reuse a same-net via within reach (a stub to it
    if the stub is clear); otherwise try 8 directions, starting along the
    pad's long axis (outward, or inward for ICs) and alternating +-45 deg steps
    from there, at distances growing by 0.1 mm from just beyond the pad edge
    (14 steps). The first via that clears all copper by CLR, keeps drill
    spacing, stays 0.8 mm inside the board and has a clear stub wins.
    Circular pads are skipped. Returns (vias or stubs added, ["REF.pad" failed]).
    """
    cu = Copper(board)
    done = 0
    failed = []
    for ref, pl in sorted(board.placed.items()):
        if only and ref not in only:
            continue
        for pad in pl.fp.pads:
            if short(pl.comp["pads"].get(pad["num"], "")) != net or pad["kind"] != "smd":
                continue
            kind, data = pad_shape(pl, pad)
            if kind == "circle":
                continue
            layer = pad_layers(pl, pad)[0]
            # pad centre and half its long side
            cx, cy = sum(p[0] for p in data) / 4, sum(p[1] for p in data) / 4
            ext = max(math.dist(data[0], data[1]), math.dist(data[1], data[2])) / 2
            # fan along the pad's long axis, away from (or, for ICs, toward) the part centre
            e1 = (data[1][0] - data[0][0], data[1][1] - data[0][1])
            e2 = (data[2][0] - data[1][0], data[2][1] - data[1][1])
            ax = e1 if math.hypot(*e1) >= math.hypot(*e2) else e2
            axis = math.atan2(ax[1], ax[0])
            out = (cx - pl.x) * math.cos(axis) + (cy - pl.y) * math.sin(axis)
            away = axis if out >= 0 else axis + math.pi
            first = away + math.pi if ref in inward else away
            dirs = [first + k * math.pi / 4 for k in (0, 1, -1, 2, -2, 3, -3, 4)]
            placed = False
            # reuse a same-net via close by (e.g. the neighbouring capacitor's) if the stub is clear
            v = cu.near_via(Point(cx, cy), net, ext + 1.2)
            if v is not None:
                stub = LineString([(cx, cy), (v.x, v.y)]).buffer(STUB_W / 2)
                if cu.clear(stub, (layer,), net):
                    board.track(net, layer, (round(cx, 3), round(cy, 3)), (round(v.x, 3), round(v.y, 3)), STUB_W)
                    cu.add(stub, (layer,), net)
                    done += 1
                    continue
            for a in dirs:
                for d in [ext + VIA_D / 2 + 0.2 + 0.1 * i for i in range(14)]:
                    vx, vy = cx + d * math.cos(a), cy + d * math.sin(a)
                    via = Point(vx, vy).buffer(VIA_D / 2)
                    stub = LineString([(cx, cy), (vx, vy)]).buffer(STUB_W / 2)
                    if not _inside(board, (vx, vy), 0.8):
                        continue
                    # the via must clear every pad, same net included (no via-in-pad: solder wicking)
                    if (cu.via_ok(Point(vx, vy)) and cu.clear(via, ("F.Cu", "In2.Cu", "B.Cu"), net, any_net=True)
                            and cu.clear(stub, (layer,), net)):
                        board.via(net, (round(vx, 3), round(vy, 3)), VIA_D, VIA_DRILL)
                        board.track(net, layer, (round(cx, 3), round(cy, 3)), (round(vx, 3), round(vy, 3)), STUB_W)
                        cu.add(via, ("F.Cu", "In2.Cu", "B.Cu"), net)
                        cu.add(stub, (layer,), net)
                        cu.vias.append((Point(vx, vy), net))
                        placed = True
                        break
                if placed:
                    break
            if placed:
                done += 1
            else:
                failed.append(f"{ref}.{pad['num']}")
    board.fanout_count = done
    return done, failed


def stitch(board, net="GND", pitch=3.0):
    """Grid of ground vias wherever every layer has room.

    pitch mm grid starting 2 mm inside the outline's bounding box; a via needs
    0.3 mm clearance to all copper on F/In2/B and 1.0 mm centre spacing to
    every other via. Returns the number of vias added.
    """
    cu = Copper(board)
    x0, y0, x1, y1, r = board.outline
    n = 0
    y = y0 + 2.0
    while y < y1 - 2.0:
        x = x0 + 2.0
        while x < x1 - 2.0:
            via = Point(x, y).buffer(VIA_D / 2)
            # any_net: never land on (or next to) an existing via or same-net copper
            if cu.via_ok(Point(x, y), 1.0) and cu.clear(via, ("F.Cu", "In2.Cu", "B.Cu"), net, clr=0.3, any_net=True):
                board.via(net, (round(x, 3), round(y, 3)), VIA_D, VIA_DRILL)
                cu.add(via, ("F.Cu", "In2.Cu", "B.Cu"), net)
                cu.vias.append((Point(x, y), net))
                n += 1
            x += pitch
        y += pitch
    return n


def bridge_adjacent(board, max_gap=0.6, width=0.2):
    """Join neighbouring pads of the same net on one part (e.g. the buck's VIN and EN),
    which the router finds hard to reach separately at 0.5 mm pitch.

    Pads qualify when their centres are at most max_gap mm apart (so only
    immediate neighbours at fine pitch); GND is left to the fanout. The bridge
    is a width-mm track centre to centre on the first pad's layer. Returns the
    number of bridges added."""
    n = 0
    for ref, pl in board.placed.items():
        pads = [(p, pl.comp["pads"].get(p["num"])) for p in pl.fp.pads if p["kind"] == "smd"]
        for i, (a, na) in enumerate(pads):
            for b, nb in pads[i + 1:]:
                if not na or na != nb or short(na) == "GND":
                    continue
                pa, pb = pl.to_board(a["x"], a["y"]), pl.to_board(b["x"], b["y"])
                if math.dist(pa, pb) <= max_gap + 1e-6:
                    layer = pad_layers(pl, a)[0]
                    board.track(na, layer, (round(pa[0], 3), round(pa[1], 3)), (round(pb[0], 3), round(pb[1], 3)), width)
                    n += 1
    return n
