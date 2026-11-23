"""Finishing router: connects the few connections the autorouter leaves open.

For each net, copper (pads, tracks, vias) is grouped into connected islands;
islands are joined by a grid A* search over the three signal layers with vias
between them, keeping the net-class clearance to every other net. The result
is plain tracks/vias added to the board; KiCad's DRC is the final judge.

Where it sits in the routing pipeline (driven by pcb_build.py):

  1. fanout.py pre-places GND/3V3/3V3_BUS stubs and vias (protected wiring).
  2. Freerouting autoroutes the DSN from route.py; route.import_ses() reads the
     session back. drop_vias_in_pads() removes vias it put on SMD pads.
  3. finish() joins whatever is still split. If nets remain, Freerouting is run
     again as a "continuation" on the current copper (up to three times).
  4. drop_dangling_vias() removes rip-up leftovers.
  5. Fill-aware passes: KiCad fills the zones, read_fills() reads the poured
     polygons back, and finish() runs again with them as obstacles/copper,
     because pours fragment around vias in ways this module can't predict.
  6. DRC repair: repair_from_drc() rips up tracks KiCad reports in clearance
     or short violations and reconnects (finish()) plus point-to-point links
     for each "unconnected items" pair KiCad reports.

Geometry is shapely in board millimetres (KiCad coordinates, Y down). The A*
search runs on a regular grid (GRID mm) of numpy boolean masks, one per layer:
a cell is free when a track of the requested width centred on it keeps CLR
from every other net's copper. In1.Cu is not routed: it is the solid GND plane.
"""
import heapq
import math

import numpy as np
import shapely
from shapely.geometry import LineString, Point, Polygon, box
from shapely.ops import unary_union

from route import BATT_NETS, CLOCK_NETS, POWER_NETS, RAIL_NETS, pad_layers, pad_shape, short

LAYERS = ["F.Cu", "In2.Cu", "B.Cu"]   # routable layers (In1.Cu = GND plane)
SKIP = {"GND"}          # carried by the ground plane and pours
VIA_D = 0.6             # via pad diameter, mm (drill 0.3)
CLR = 0.125             # 0.1 mm rule + margin for grid rounding on diagonal moves
GRID = 0.05             # A* grid pitch, mm


def width_for(net):
    """Net-class track width in mm (same classes as project.py / route.py)."""
    n = short(net)
    return 0.8 if n in BATT_NETS else 0.3 if n in POWER_NETS else 0.2 if n in RAIL_NETS else 0.1


def copper(board):
    """[(net, layer, geom)] for every pad, track and via.

    Only the routable LAYERS are included. A via or through-hole pad appears
    once per layer with identical geometry (islands() relies on that to join
    layers). Zone fills are added when board.fills has been set from
    read_fills(); the GND fills are filtered out by the caller.
    """
    items = []
    for ref, pl in board.placed.items():
        for pad in pl.fp.pads:
            net = pl.comp["pads"].get(pad["num"], "")
            kind, data = pad_shape(pl, pad)
            g = Point(data[0], data[1]).buffer(data[2] / 2) if kind == "circle" else Polygon(data)
            for l in pad_layers(pl, pad):
                if l in LAYERS:
                    items.append((net, l, g))
    for net, layer, a, b, w in board.tracks:
        if layer in LAYERS:
            items.append((net, layer, LineString([a, b]).buffer(w / 2)))
    for net, at, size, _ in board.vias:
        g = Point(at).buffer(size / 2)
        for l in LAYERS:
            items.append((net, l, g))
    for net, layer, poly in getattr(board, "fills", []):   # zone fills as KiCad poured them
        items.append((net, layer, poly))
    return items


def read_fills(path):
    """Filled zone polygons [(net, layer, Polygon)] from a KiCad board after a zone refill.

    The file must have been saved by KiCad after filling (kicad-cli pcb drc
    --refill-zones --save-board); otherwise it has no filled_polygon blocks and
    the result is empty. The zone's net is taken from net_name, a quoted net, or
    a numeric net id resolved through the board's net table, so the parser
    copes with both older and newer KiCad file formats. buffer(0) repairs any
    self-touching outlines KiCad writes for fragmented pours.
    """
    import re
    t = open(path).read()
    ids = {m.group(1): m.group(2) for m in re.finditer(r'\(net (\d+) "([^"]*)"\)', t)}
    out = []
    for z in re.finditer(r'\(zone\s(.*?)\n\t\)', t, re.S):
        body = z.group(1)
        n = re.search(r'\(net_name "([^"]*)"\)', body) or re.search(r'\(net "([^"]*)"\)', body)
        if not n:
            n2 = re.search(r'\(net (\d+)\)', body)
            net = ids.get(n2.group(1), "") if n2 else ""
        else:
            net = n.group(1)
        if not net:
            continue
        for fp in re.finditer(r'\(filled_polygon\s*\(layer "([^"]+)"\)\s*\(pts(.*?)\)\s*\)', body, re.S):
            pts = [(float(x), float(y)) for x, y in re.findall(r'\(xy ([-\d.]+) ([-\d.]+)\)', fp.group(2))]
            if len(pts) >= 3:
                poly = Polygon(pts).buffer(0)
                if not poly.is_empty:
                    out.append((net, fp.group(1), poly))
    return out


def islands(items, net):
    """Connected groups of this net's copper: list of {layer: geometry}.

    Union-find over all of the net's items; O(n^2) pairwise tests, which is
    fine for the item counts of a single net.
    """
    mine = [(l, g) for n, l, g in items if n == net]
    parent = list(range(len(mine)))

    def find(i):   # union-find root with path halving
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    # vias/through pads appear once per layer with the same geometry: join those too
    for i in range(len(mine)):
        for j in range(i + 1, len(mine)):
            li, gi = mine[i]
            lj, gj = mine[j]
            if (li == lj and gi.intersects(gj)) or (li != lj and gi.equals(gj)):
                parent[find(i)] = find(j)
    groups = {}
    for i, (l, g) in enumerate(mine):
        groups.setdefault(find(i), {}).setdefault(l, []).append(g)
    return [{l: unary_union(gs) for l, gs in grp.items()} for grp in groups.values()]


def route_pair(board, items, net, src, dst, margin=14.0, w=None, grid=None, clr=None):
    """Grid A* from island src to island dst of net; add the path to the board.

    src/dst are {layer: geometry} islands (or small target discs from a DRC
    report). The search window is their joint bounding box grown by margin mm
    and clipped 0.5 mm inside the board outline. w is the track width (default:
    the net-class width), grid/clr override GRID/CLR for tight corridors.

    Moves are the 8 grid neighbours (diagonal cost sqrt 2) plus a layer change
    at cost 25 grid steps (1.25 mm at the default grid), which keeps vias rare.
    Vias are only allowed where a VIA_D pad keeps clearance from other nets,
    stays off every SMD pad (any net) and keeps 0.9 mm centre spacing from the
    existing vias. Clock nets (route.CLOCK_NETS) get 0.25 mm clearance on both
    sides: a clock track keeps it from everything, other tracks keep it from
    clock copper. The expansion is capped at 12 million nodes.

    On success the path is emitted as straight track runs (a new segment at
    every direction change) and vias at layer changes, and True is returned;
    on failure nothing is added and False is returned.
    """
    w = w or width_for(net)
    GRID = grid or globals()["GRID"]
    CLR = clr or globals()["CLR"]
    geoms = [g for grp in (src, dst) for g in grp.values()]
    x0, y0, x1, y1 = unary_union(geoms).bounds
    x0, y0, x1, y1 = x0 - margin, y0 - margin, x1 + margin, y1 + margin
    bx0, by0, bx1, by1, _ = board.outline
    x0, y0, x1, y1 = max(x0, bx0 + 0.5), max(y0, by0 + 0.5), min(x1, bx1 - 0.5), min(y1, by1 - 0.5)
    xs = np.arange(x0, x1, GRID)
    ys = np.arange(y0, y1, GRID)
    X, Y = np.meshgrid(xs, ys)   # grid cell centres; masks below are indexed [row=y, col=x]
    # obstacles for every layer: mounting holes, board slots, footprint NPTHs
    holes = [Point(h).buffer(2.4) for h in getattr(board, "mount_holes", [])]
    holes += [box(*s).buffer(0.4) for s in getattr(board, "slots", [])]
    for pl in board.placed.values():   # footprint NPTH holes need the 0.25 mm hole clearance
        for pad in pl.fp.pads:
            if pad["kind"] == "np_thru_hole" or not pad["num"]:
                kind, data = pad_shape(pl, pad)
                if kind == "circle":
                    holes.append(Point(data[0], data[1]).buffer(data[2] / 2 + 0.3 - CLR))
    # per layer: free[l] = cell can carry the track centre; start/goal[l] = cell
    # lies on the source/destination island; via_free = a via may sit here
    free, goal, start = {}, {}, {}
    via_free = np.ones(X.shape, bool)
    # no via in or touching an SMD pad, same net included (solder wicks down open vias)
    smd = [Polygon(d) if k != "circle" else Point(d[0], d[1]).buffer(d[2] / 2)
           for pl in board.placed.values() for pad in pl.fp.pads if pad["kind"] == "smd"
           for k, d in [pad_shape(pl, pad)]]
    if smd:
        via_free &= ~shapely.contains_xy(unary_union(smd).buffer(VIA_D / 2 + 0.05), X, Y)
    keep = {l: [box(*r) for ll, r in getattr(board, "route_keepouts", []) if ll == l] for l in LAYERS}
    for l in LAYERS:
        others = [g for n, ll, g in items if ll == l and n != net] + holes + keep[l]
        if short(net) in CLOCK_NETS:   # clock clearance against everything
            extra = [g.buffer(0.25 - CLR) for g in others]
        else:                          # and everything keeps it from clock copper
            extra = [g.buffer(0.25 - CLR) for n, ll, g in items if ll == l and n != net and short(n) in CLOCK_NETS]
        # Minkowski grow by half the track width + clearance: a centre outside
        # "blocked" means the whole track edge keeps CLR from the obstacle
        blocked = unary_union(others + extra).buffer(w / 2 + CLR) if others else None
        f = ~shapely.contains_xy(blocked, X, Y) if blocked is not None else np.ones(X.shape, bool)
        free[l] = f
        vb = unary_union(others).buffer(VIA_D / 2 + CLR)
        via_free &= ~shapely.contains_xy(vb, X, Y)
        goal[l] = shapely.contains_xy(dst[l], X, Y) if l in dst else np.zeros(X.shape, bool)
        start[l] = shapely.contains_xy(src[l], X, Y) if l in src else np.zeros(X.shape, bool)
    # also forbid vias near any via (drill spacing): centres > 0.9 mm apart
    # leaves 0.3 mm copper-to-copper between 0.6 mm via pads
    for n, at, size, _ in board.vias:
        d = np.hypot(X - at[0], Y - at[1])
        via_free &= d > (VIA_D + 0.3)
    li = {l: k for k, l in enumerate(LAYERS)}
    H, W = X.shape
    # The heuristic treats the goal region as a disc around its centroid (gr, gc)
    # of radius greach (all in grid cells) and measures distance to that disc.
    gpts = [(k, r, c) for l, k in li.items() for r, c in zip(*np.nonzero(goal[l] & free[l]))]
    if not gpts:
        return False
    gr = float(np.mean([r for _, r, _ in gpts]))
    gc = float(np.mean([c for _, _, c in gpts]))
    greach = max(max(abs(r - gr), abs(c - gc)) for _, r, c in gpts)

    def h(r, c):   # distance to the goal region (cheap, admissible enough to steer)
        return max(0.0, math.hypot(r - gr, c - gc) - greach)

    # Multi-source A*: every free cell of the source island starts at cost 0.
    # Nodes are (layer index, row, col); stale heap entries are skipped lazily.
    openq, came, cost = [], {}, {}
    for l, k in li.items():
        for r, c in zip(*np.nonzero(start[l] & free[l])):
            node = (k, r, c)
            cost[node] = 0
            heapq.heappush(openq, (h(r, c), 0, node))
    steps = [(-1, 0, 1), (1, 0, 1), (0, -1, 1), (0, 1, 1), (-1, -1, 1.414), (-1, 1, 1.414), (1, -1, 1.414), (1, 1, 1.414)]
    end = None
    n_exp = 0
    while openq and n_exp < 12_000_000:
        _, g0, node = heapq.heappop(openq)
        if g0 > cost.get(node, 1e18):
            continue
        n_exp += 1
        k, r, c = node
        if goal[LAYERS[k]][r, c]:
            end = node
            break
        nbrs = []
        for dr, dc, sc in steps:
            rr, cc = r + dr, c + dc
            if 0 <= rr < H and 0 <= cc < W and free[LAYERS[k]][rr, cc]:
                nbrs.append(((k, rr, cc), sc))
        if via_free[r, c]:
            for kk in range(len(LAYERS)):
                if kk != k and free[LAYERS[kk]][r, c]:
                    nbrs.append(((kk, r, c), 25.0))
        for nb, sc in nbrs:
            ng = g0 + sc
            if ng < cost.get(nb, 1e18):
                cost[nb] = ng
                came[nb] = node
                heapq.heappush(openq, (ng + h(nb[1], nb[2]), ng, nb))
    if end is None:
        return False
    path = [end]
    while path[-1] in came:
        path.append(came[path[-1]])
    path.reverse()
    # emit straight runs and vias: walk the cell path, flushing a track segment
    # whenever the step direction changes, and a via (plus the pending segment)
    # whenever the layer changes
    pts = [(LAYERS[k], (round(float(X[r, c]), 3), round(float(Y[r, c]), 3))) for k, r, c in path]
    seg_start = pts[0]
    prev = pts[0]
    prev_dir = None
    for cur in pts[1:]:
        if cur[0] != prev[0]:                       # layer change: via
            if seg_start[1] != prev[1]:
                board.track(net, prev[0], seg_start[1], prev[1], w)
            board.via(net, prev[1], VIA_D, 0.3)
            seg_start, prev, prev_dir = cur, cur, None
            continue
        d = (round(cur[1][0] - prev[1][0], 3), round(cur[1][1] - prev[1][1], 3))
        if prev_dir is not None and d != prev_dir:
            board.track(net, prev[0], seg_start[1], prev[1], w)
            seg_start = prev
        prev_dir, prev = d, cur
    if seg_start[1] != prev[1]:
        board.track(net, prev[0], seg_start[1], prev[1], w)
    return True


def finish(board, max_rounds=8):
    """Join every net's islands. Returns (joined, failed) lists of net names.

    Each round joins one pair of islands per split net (the closest pair), then
    recomputes the copper, so a net split into k islands needs k-1 rounds;
    max_rounds bounds the work. GND (SKIP) and KiCad's "unconnected-*" nets are
    ignored. joined accumulates over rounds (a net can appear more than once);
    failed holds the nets that could not be joined in the last round.
    """
    joined, failed = [], []
    for _ in range(max_rounds):
        items = copper(board)
        nets = sorted({n for n, _, _ in items if n and short(n) not in SKIP and not n.startswith("unconnected-")})
        work = []
        for net in nets:
            isl = islands(items, net)
            if len(isl) > 1:
                work.append((net, isl))
        if not work:
            break
        failed = []
        for net, isl in work:
            # join the closest pair of islands (short links are the easiest to find)
            us = [unary_union(list(g.values())) for g in isl]
            _, i, j = min((us[i].distance(us[j]), i, j) for i in range(len(us)) for j in range(i + 1, len(us)))
            a, b = isl[i], isl[j]
            # try the net-class width first, then neck down (fine for a few mm at our currents)
            ok = False
            for w in sorted({width_for(net), 0.15, 0.1}, reverse=True):
                if w <= width_for(net) and route_pair(board, copper(board), net, a, b, w=w):
                    ok = True
                    break
            if not ok:   # tight corridor: fine grid, clearance just over the 0.1 mm rule
                ok = route_pair(board, copper(board), net, a, b, margin=3.0, w=0.1, grid=0.025, clr=0.106)
            (joined if ok else failed).append(net)
    return joined, failed


def drop_dangling_vias(board, first=0):
    """Remove routed vias (index >= first, i.e. not the pre-placed fanout) that touch their
    own net on fewer than two layers: leftovers from rip-up passes. Vias of nets that have
    planes or pours (GND, 3V3) stay: zones connect them, which this check can't see.
    Returns the number of vias removed."""
    keep, dropped = list(board.vias[:first]), 0
    pads = {}
    for ref, pl in board.placed.items():
        for pad in pl.fp.pads:
            net = pl.comp["pads"].get(pad["num"], "")
            kind, data = pad_shape(pl, pad)
            g = Point(data[0], data[1]).buffer(data[2] / 2) if kind == "circle" else Polygon(data)
            for l in pad_layers(pl, pad):
                pads.setdefault((net, l), []).append(g)
    tracks = {}
    for net, layer, a, b, w in board.tracks:
        tracks.setdefault((net, layer), []).append(LineString([a, b]).buffer(w / 2))
    for v in board.vias[first:]:
        net, at, size, drill = v
        if short(net) in ("GND", "3V3"):
            keep.append(v)
            continue
        g = Point(at).buffer(size / 2)
        layers = sum(1 for l in LAYERS
                     if any(g.intersects(x) for x in tracks.get((net, l), []) + pads.get((net, l), [])))
        if layers >= 2:
            keep.append(v)
        else:
            dropped += 1
    board.vias[:] = keep
    return dropped


def drop_vias_in_pads(board):
    """Remove vias whose copper touches an SMD pad (any net): open vias in pads wick solder.
    The finisher, which never places such vias, then reconnects whatever they joined.
    Returns the number of vias removed."""
    smd = [Polygon(d) if k != "circle" else Point(d[0], d[1]).buffer(d[2] / 2)
           for pl in board.placed.values() for pad in pl.fp.pads if pad["kind"] == "smd"
           for k, d in [pad_shape(pl, pad)]]
    tree = shapely.STRtree(smd)
    keep = [v for v in board.vias if not len(tree.query(Point(v[1]).buffer(v[2] / 2), "intersects"))]
    dropped = len(board.vias) - len(keep)
    board.vias[:] = keep
    return dropped


def repair_from_drc(board, drc_json, first_track=0):
    """Fix what KiCad's DRC reports: rip up routed segments involved in clearance violations
    (the finisher then reconnects those nets), and route unconnected pairs between the exact
    positions KiCad reports. Returns a short summary.

    Only tracks with index >= first_track are ripped up, so the pre-placed
    fanout copper is never touched. A violating track is identified by the net
    and layer in KiCad's item description plus the reported position (within
    0.05 mm of the segment). Each unconnected pair is routed between 0.12 mm
    discs at the two reported positions, on the reported layer (F.Cu if none
    or if it is not a routable layer)."""
    import json
    import re
    d = json.load(open(drc_json))
    ripped = 0
    for v in d["violations"]:
        if v["type"] not in ("clearance", "shorting_items"):
            continue
        for it in v["items"]:
            m = re.match(r"Track \[([^\]]+)\] on ([^\s,]+)", it["description"])
            if not m:
                continue
            net, layer = m.group(1), m.group(2)
            p = Point(it["pos"]["x"], it["pos"]["y"])
            for i in range(len(board.tracks) - 1, first_track - 1, -1):
                tn, tl, a, b, w = board.tracks[i]
                if short(tn) == short(net) and tl == layer and LineString([a, b]).distance(p) < 0.05:
                    del board.tracks[i]
                    ripped += 1
    joined, failed = finish(board)
    # unconnected pairs KiCad still sees (e.g. through fragmented pours): route point to point
    direct = 0
    for u in d.get("unconnected_items", []):
        ends = []
        for it in u["items"]:
            m = re.search(r"\[([^\]]+)\]", it["description"])
            lm = re.search(r" on (\S+\.Cu)", it["description"])
            if m:
                layer = lm.group(1) if lm else "F.Cu"
                ends.append((m.group(1), {layer if layer in LAYERS else "F.Cu":
                                          Point(it["pos"]["x"], it["pos"]["y"]).buffer(0.12)}))
        if len(ends) == 2 and short(ends[0][0]) == short(ends[1][0]):
            net = next(n for n, _, _ in copper(board) if short(n) == short(ends[0][0]))
            for w in (width_for(net), 0.15, 0.1):
                if w <= width_for(net) and route_pair(board, copper(board), net, ends[0][1], ends[1][1], w=w):
                    direct += 1
                    break
    return f"ripped {ripped} segments; finisher joined {joined or 'nothing'} failed {failed or 'none'}; direct links {direct}"
