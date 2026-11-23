#!/usr/bin/env python3
"""Printability checks for the exported print files (enclosure/out/**/*.stl, in print orientation).

1. Supports. For every part (the base file; legend files sit flush in the top layers of their
   base) it finds the downward-facing surfaces that are not on the bed and sorts them:
     * overhang steeper than 45 degrees from vertical with nothing below it but air, and
     * bridges: flat ceilings over air. A bridge is fine when its shorter span is short.
   Each region of connected downward faces is reported with its area and its span (the
   shorter side of its bounding box, or 4 x area / perimeter for thin rings and lips). Regions
   under 0.5 mm^2 (fillets, text edges) are ignored. A region whose span is at most MAX_BRIDGE
   (10 mm) is a printable bridge or a short overhang; anything longer would need support.
2. Thin walls. Every SLICE mm the part is cut; whatever of a cut is narrower than MIN_WALL
   (two 0.4 mm perimeters) is found by a morphological opening
   with a disc that wide. A thin patch larger than MIN_THIN_AREA that persists THIN_LAYERS
   layers at the same place fails (a 45-degree chamfer is thin for a layer or two: fine).
   Corners lose a few hundredths of a mm^2 to the opening and are ignored. The top INLAY of
   parts with inlaid legends is skipped (the legend gaps are thin on purpose, legendcheck.py
   measures them). Patches at least MIN_SKIN wide (Arachne prints them as two narrow lines:
   the skins left outside the corner catches' slot and undercut) are listed as skins, not failed.
3. Slender columns. Free-standing compact islands of the cuts (posts, pins, stems; not walls
   or fingers, whose long side is over 3x their width) are followed up through the layers. A
   column thinner than MIN_POST (its narrowest cut's least width) and taller than twice that
   is listed as slender; one taller than MAX_ASPECT times its width fails (it would snap or
   wobble while printing).

Usage: printcheck.py [files...]
"""
import glob
import os
import sys

import numpy as np
import trimesh

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
COS45 = np.cos(np.radians(45.0)) + 0.01      # a 45-degree chamfer itself is fine
MAX_BRIDGE = 10.0
MIN_AREA = 0.5
SLICE = 0.2           # slice spacing (one layer)
MIN_WALL = 0.8        # two perimeters of a 0.4 mm nozzle
MIN_SKIN = 0.6        # deliberate skins (case.py CATCH_SKIN): listed, not failed
THIN_LAYERS = 3       # a thin patch must persist this many layers to fail
MIN_THIN_AREA = 1.0   # thin patches up to this size (mm^2 per slice) are corners and tips
MIN_POST = 2.5        # columns thinner than this are listed
MAX_ASPECT = 8.0      # height / width beyond which a column fails
INLAY = 0.4


def regions(mesh):
    """[(area, span, z)] of the connected regions of downward faces above the bed."""
    n = mesh.face_normals
    zmin = mesh.bounds[0][2]
    zc = mesh.triangles_center[:, 2]
    down = np.where((n[:, 2] < -COS45) & (zc > zmin + 0.05))[0]
    if len(down) == 0:
        return []
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
    adj = mesh.face_adjacency
    pairs = adj[np.isin(adj[:, 0], down) & np.isin(adj[:, 1], down)]
    pos = {f: k for k, f in enumerate(down)}
    r = [pos[a] for a in pairs[:, 0]]
    c = [pos[b] for b in pairs[:, 1]]
    g = coo_matrix((np.ones(len(r)), (r, c)), shape=(len(down), len(down)))
    ncomp, lab = connected_components(g, directed=False)
    out = []
    for k in range(ncomp):
        idx = down[lab == k]
        area = mesh.area_faces[idx].sum()
        if area < MIN_AREA:
            continue
        pts = mesh.triangles[idx].reshape(-1, 3)
        ext = pts.max(0) - pts.min(0)
        # span: the shorter side of the bounding box, or for thin shapes (rings, lips) about
        # twice their width (4 * area / perimeter), whichever is smaller
        e = np.sort(mesh.faces[idx][:, [0, 1, 1, 2, 2, 0]].reshape(-1, 2), axis=1)
        u, cnt = np.unique(e, axis=0, return_counts=True)
        b = u[cnt == 1]
        perim = np.linalg.norm(mesh.vertices[b[:, 0]][:, :2] - mesh.vertices[b[:, 1]][:, :2], axis=1).sum()
        out.append((area, min(ext[0], ext[1], 4 * area / max(perim, 1e-9)), pts[:, 2].mean()))
    return out


def section(mesh, z):
    """Shapely cross-section of `mesh` at height z (even-odd: holes stay holes)."""
    from shapely.geometry import MultiLineString, Polygon
    from shapely.ops import polygonize
    segs = trimesh.intersections.mesh_plane(mesh, (0, 0, 1), (0, 0, z))
    if len(segs) == 0:
        return None
    segs = np.round(segs[:, :, :2], 5)
    sec = None
    for face in polygonize(MultiLineString([tuple(map(tuple, sg)) for sg in segs if not (sg[0] == sg[1]).all()])):
        ring = Polygon(face.exterior)
        sec = ring if sec is None else sec.symmetric_difference(ring)
    return sec


def _dims(poly):
    """(least, greatest) side of a polygon's minimum rotated rectangle."""
    r = poly.minimum_rotated_rectangle
    c = list(r.exterior.coords)
    a, b = np.hypot(c[1][0] - c[0][0], c[1][1] - c[0][1]), np.hypot(c[2][0] - c[1][0], c[2][1] - c[1][1])
    return min(a, b), max(a, b)


def features(mesh, skip_top=0.0, min_wall=MIN_WALL):
    """(thin, columns): thin = [(height above the bed, area, layers)] of thin-wall patches over
    MIN_THIN_AREA that persist THIN_LAYERS layers; columns = [(height, least width, x, y)] of
    the free-standing compact columns."""
    from shapely.geometry import Point
    z0, z1 = mesh.bounds[0][2], mesh.bounds[1][2]
    r = min_wall / 2 - 0.01
    thin, tracks, done, patches = [], [], [], []
    for z in np.arange(z0 + SLICE / 2, z1 - skip_top, SLICE):
        sec = section(mesh, z)
        if sec is None or sec.is_empty:
            continue
        lost = sec.difference(sec.buffer(-r, join_style=1).buffer(r, join_style=1))
        live = []
        for p in getattr(lost, "geoms", [lost]):
            if p.area > MIN_THIN_AREA:
                c = p.centroid
                prev = next((t for t in patches if Point(t["xy"]).distance(c) < 0.5), None)
                t = dict(xy=(c.x, c.y), z=z - z0, area=p.area, n=prev["n"] + 1 if prev else 1)
                live.append(t)
                if t["n"] == THIN_LAYERS:
                    thin.append((t["z"], p.area, t["n"]))
        patches = live
        isl = [p for p in getattr(sec, "geoms", [sec]) if p.area < 40.0]
        cur = []
        for p in isl:
            c = p.centroid
            w, long_ = _dims(p)
            if long_ > 3 * w:
                continue            # a wall, rib or finger, not a column
            prev = next((t for t in tracks if Point(t["xy"]).distance(c) < 0.6), None)
            if prev:
                prev.update(xy=(c.x, c.y), top=z, w=min(prev["w"], w), seen=True)
                cur.append(prev)
            else:
                cur.append({"xy": (c.x, c.y), "bot": z, "top": z, "w": w, "seen": True})
        done += [t for t in tracks if t not in cur]
        tracks = cur
    done += tracks
    cols = [(t["top"] - t["bot"] + SLICE, t["w"], *t["xy"]) for t in done]
    return thin, [c for c in cols if c[0] > 1.0]


def check(path, verbose=True):
    mesh = trimesh.load(path, force="mesh")
    rs = regions(mesh)
    long_ = [r for r in rs if r[1] > MAX_BRIDGE]
    name = os.path.relpath(path, OUT)
    worst = max([r[1] for r in rs] or [0.0])
    legends = glob.glob(path.replace("_base.stl", "_legend*.stl")) if path.endswith("_base.stl") else []
    wall = MIN_WALL
    skins, cols = features(mesh, skip_top=INLAY + 0.05 if legends else 0.0, min_wall=wall)
    thin = features(mesh, skip_top=INLAY + 0.05 if legends else 0.0, min_wall=MIN_SKIN)[0] if skins else []
    slender = [c for c in cols if c[1] < MIN_POST and c[0] > 2 * c[1]]
    fragile = [c for c in cols if c[0] > MAX_ASPECT * c[1]]
    ok = not long_ and not thin and not fragile
    status = "OK" if ok else "needs a look"
    print(f"  {name:34s} {len(rs):4d} downward regions, longest span {worst:5.1f} mm; "
          f"thin < {MIN_SKIN} mm: {len(thin)} ({len(skins)} under {wall}); columns {len(cols)}, slender {len(slender)}, "
          f"fragile {len(fragile)}: {status}")
    for a, span, z in sorted(long_, key=lambda r: -r[1])[:5]:
        print(f"      span {span:.1f} mm, area {a:.1f} mm2, at {z:.1f} mm above the bed")
    for z, a, n in sorted(thin, key=lambda t: -t[1])[:5]:
        print(f"      thin patch {a:.2f} mm2 from {z - (n - 1) * SLICE:.1f} mm above the bed")
    if not thin:
        for z, a, n in sorted(skins, key=lambda t: -t[1])[:5]:
            print(f"      skin {MIN_SKIN}-{wall} mm, {a:.2f} mm2 from {z - (n - 1) * SLICE:.1f} mm above the bed")
    if verbose and slender:
        kinds = {}
        for h, w, x, y in slender:
            kinds.setdefault((round(w, 1), round(h, 1)), []).append((x, y))
        for (w, h), xy in sorted(kinds.items())[:6]:
            print(f"      slender: {len(xy):3d} x {w:.1f} mm wide, {h:.1f} mm tall (h/w {h / w:.1f})")
    for h, w, x, y in fragile[:5]:
        print(f"      FRAGILE column at ({x:.1f}, {y:.1f}): {w:.2f} mm wide, {h:.1f} mm tall")
    return ok


def main(argv):
    files = argv or sorted(f for f in glob.glob(os.path.join(OUT, "**", "*.stl"), recursive=True)
                           if not os.path.basename(f).startswith(("render_", "top_shell.stl"))
                           and "legend" not in os.path.basename(f) and "_ears" not in f
                           and "_shift" not in f and "_alpha" not in f)
    ok = all([check(f) for f in files])
    print("printcheck:", "all parts OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
