"""Verify a sliced key plate (base + two-colour legend) against its model, layer by layer.

usage: verify_keys.py PLATE.gcode BASE.stl LEGEND.stl [PNG_DIR]

Placement: the base's bounding-box centre at (112.5, 112.5), its bottom on the bed, then moved onto the
print by matching bounding boxes (so any PLATE_XY in slice.sh is followed).
Checks (FAIL lines make the exit status 1):
  * no brim, skirt or support lines; exactly two PAUSEs, the first at the legend's first layer;
    black after the first, base colour after the second; the nozzle temperature set after each;
  * layers every 0.2 mm, no move below the layer being printed, the end routine lifts above the print;
  * no prime (G1 E) between a PAUSE and the next move: the printer's RESUME purges, an extra prime blobs;
  * every layer: printed islands vs the model's section: a printed island that joins two model islands
    (fused keys), material more than 0.25 mm outside the model, model area left unprinted;
  * legend layers: black inside the legend, base colour outside it, per key;
  * PNGs of the first layer and the legend layers (black/beige as printed) in PNG_DIR.
"""
import re
import sys

import numpy as np
import trimesh
from shapely.geometry import LineString, Point, Polygon, MultiPolygon
from shapely.ops import unary_union

gc, base_p, leg_p = sys.argv[1:4]
png = sys.argv[4] if len(sys.argv) > 4 else None
fails = []


def fail(msg):
    fails.append(msg)
    print("FAIL", msg)


base, leg = trimesh.load(base_p), trimesh.load(leg_p)
c = (base.bounds[0] + base.bounds[1]) / 2
for m in (base, leg):
    m.apply_translation([112.5 - c[0], 112.5 - c[1], -base.bounds[0][2]])
merged = trimesh.util.concatenate([base, leg])
z_leg0 = leg.bounds[0][2]


def section(mesh, z):
    s = mesh.section(plane_origin=[0, 0, z], plane_normal=[0, 0, 1])
    if s is None:
        return Polygon()
    p2, _ = s.to_2D(to_2D=np.eye(4))          # keep bed XY (a Z plane needs no rotation)
    return unary_union(p2.polygons_full)


def parts(g):
    return [p for p in (g.geoms if isinstance(g, MultiPolygon) else [g]) if not p.is_empty]


# ---- parse
layers = {}            # z -> {tool: [buffered segments]}
types, pauses, temps_after = set(), [], []
z = None; x = y = None; w = 0.45; typ = ''; tool = 0; zl = []
minz_move = []         # (layer z, lowest Z of a move while printing it)
cur_z = None; end_started = False; end_z = []
lines = open(gc).read().split('\n')
for i, ln in enumerate(lines):
    if ln.startswith(';PRINT_END'):
        end_started = True
    if ln.startswith(';Z:'):
        z = round(float(ln[3:]), 3); zl.append(z); cur_z = z
    elif ln.startswith(';TYPE:'):
        typ = ln[6:].strip(); types.add(typ)
    elif ln.startswith(';WIDTH:'):
        w = float(ln[7:])
    elif ln == 'PAUSE':
        tool = 1 - tool
        pauses.append((z, i))
        t = next((l for l in lines[i:i + 30] if l.startswith('M104')), None)
        temps_after.append(t)
    m = re.match(r'G[0-3] ', ln)
    if m:
        code = ln.split(';')[0]
        d = dict((t[0], float(t[1:])) for t in code.split()[1:] if t[0] in 'XYZE' and len(t) > 1)
        if 'Z' in d:
            if end_started:
                end_z.append(d['Z'])
            elif cur_z is not None:
                minz_move.append((cur_z, d['Z']))
        nx, ny = d.get('X', x), d.get('Y', y)
        if not end_started and z is not None and 'E' in d and d['E'] > 0 and x is not None and (nx, ny) != (x, y) \
                and ln.startswith('G1'):
            layers.setdefault(z, {}).setdefault(tool, []).append(LineString([(x, y), (nx, ny)]).buffer(w / 2, quad_segs=4))
        x, y = nx, ny

# ---- align the model on the print (the slicer centres the plate on its own bed centre): bounding boxes of a
# mid-height layer (layer 1 has the first-layer relief, not symmetric)
_z1 = min(layers, key=lambda q: abs(q - 3.0))
_P1 = unary_union([g for t in layers[_z1].values() for g in t])
_D1 = section(merged, _z1 - 0.1)
_dx = (_P1.bounds[0] + _P1.bounds[2] - _D1.bounds[0] - _D1.bounds[2]) / 2
_dy = (_P1.bounds[1] + _P1.bounds[3] - _D1.bounds[1] - _D1.bounds[3]) / 2
for m in (merged, base, leg):
    m.apply_translation([_dx, _dy, 0])
print(f"model moved by ({_dx:.2f}, {_dy:.2f}) onto the print")

# ---- structure
bad = types & {'Brim', 'Skirt', 'Support', 'Support interface', 'Support transition'}
if bad:
    fail(f"line types {sorted(bad)} present")
if len(pauses) != 2:
    fail(f"{len(pauses)} PAUSEs (want 2)")
else:
    first_leg_layer = min(zz for zz in zl if zz > z_leg0 + 1e-6)
    print(f"pauses at Z {pauses[0][0]} and {pauses[1][0]}; legend layers from Z {first_leg_layer}")
    if abs(pauses[0][0] - first_leg_layer) > 1e-6:
        fail(f"first PAUSE at Z {pauses[0][0]}, the legend starts at Z {first_leg_layer}")
    print("  after the pauses:", temps_after)
    if any(t is None for t in temps_after):
        fail("no nozzle temperature after a PAUSE")
    for _, i in pauses:   # RESUME purges 100 mm: an extra prime before the first move would blob a part
        for l in lines[i + 1:i + 40]:
            if re.match(r'G1 X', l):
                break
            if re.match(r'G1 E[\d.]+', l):
                fail(f"a prime ({l}) after the PAUSE at line {i}: it lands as a blob on the first part")
steps = np.diff(zl)
if not np.allclose(steps, 0.2, atol=1e-3):
    fail(f"layer steps not all 0.2: {sorted(set(np.round(steps, 3)))}")
print(f"layers: {len(zl)} from {zl[0]} to {zl[-1]}")
low = [(lz, mz) for lz, mz in minz_move if mz < lz - 1e-3]
if low:
    fail(f"{len(low)} moves below the layer being printed, e.g. {low[:3]}")
if not end_z or min(end_z) < zl[-1]:
    fail(f"end routine Z moves {end_z} go below the top of the print ({zl[-1]})")
else:
    print(f"end routine: Z {end_z} (print top {zl[-1]})")

# ---- per layer against the model
print("layer   model printed  joins  outside>0.25  holes>1.5mm2 (sum)")
first_islands = None
for lz in sorted(layers):
    D = unary_union([section(base, lz - 0.1), section(leg, lz - 0.1)]).buffer(0.01).buffer(-0.01)
    P = unary_union([g for t in layers[lz].values() for g in t]).buffer(0)
    dp, pp = parts(D), parts(P)
    joins = 0
    for q in pp:
        hit = [k for k, dd in enumerate(dp) if q.intersection(dd).area > 0.02]
        if len(hit) > 1:
            joins += 1
            if joins <= 3:
                print(f"    join at {np.round(q.representative_point().coords[0], 1)} of model islands "
                      f"{[round(dp[k].area, 2) for k in hit]}")
    outside = P.difference(D.buffer(0.25)).area
    # holes in the solid band along the outline (walls); deeper inside is the slicer's sparse infill
    holes_ = [q for q in parts(D.buffer(-0.25).difference(D.buffer(-1.2)).difference(P)) if q.area > 1.5]
    missing = sum(q.area for q in holes_)
    print(f"{lz:5.2f} {len(dp):6d} {len(pp):7d} {joins:6d} {outside:12.2f} {missing:14.2f}")
    if joins:
        fail(f"layer {lz}: {joins} printed islands join separate model islands")
    if outside > 2.0:
        fail(f"layer {lz}: {outside:.1f} mm2 printed outside the model")
    if holes_:
        print(f"    unprinted: {[(round(q.area, 2), tuple(np.round(q.centroid.coords[0], 1))) for q in holes_[:6]]}")
    if any(q.area > 20.0 for q in holes_):
        fail(f"layer {lz}: model areas over 20 mm2 left unprinted")
    elif any(q.area > 3.0 for q in holes_):
        print(f"    WARN layer {lz}: unprinted model areas over 3 mm2 (a top or a bridge on a layer boundary? look at it)")

# ---- legend layers by colour
legz = [q for q in sorted(layers) if q > z_leg0 + 1e-6]
for lz in legz:
    L = section(leg, lz - 0.1).buffer(0)
    blk = unary_union(layers[lz].get(1, [])).buffer(0)
    bei = unary_union(layers[lz].get(0, [])).buffer(0)
    black_out = blk.difference(L.buffer(0.2)).area
    beige_in = bei.intersection(L.buffer(-0.2)).area
    cover = blk.intersection(L).area / L.area if L.area else 1.0
    print(f"legend layer {lz}: legend {L.area:.1f} mm2, black covers {cover * 100:.0f} %, black >0.2 outside "
          f"{black_out:.2f} mm2, beige >0.2 inside {beige_in:.2f} mm2")
    if cover < 0.90:
        fail(f"legend layer {lz}: black covers only {cover * 100:.0f} % of the legend")
    if black_out > 1.0 or beige_in > 1.0:
        fail(f"legend layer {lz}: colours cross the legend outline")

# ---- pictures
if png:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import PathPatch
    from matplotlib.path import Path as MPath

    def draw(ax, g, **kw):
        for p in parts(g):
            verts = list(p.exterior.coords); codes = [MPath.MOVETO] + [MPath.LINETO] * (len(verts) - 1)
            for r in p.interiors:
                v = list(r.coords); verts += v; codes += [MPath.MOVETO] + [MPath.LINETO] * (len(v) - 1)
            ax.add_patch(PathPatch(MPath(verts, codes), **kw))

    def pic(lz, name, box=None):
        fig, ax = plt.subplots(figsize=(12, 15) if box is None else (8, 5), dpi=110)
        draw(ax, section(merged, lz - 0.1), fc="none", ec="red", lw=0.5)
        for t, col in ((0, "#C8B48C"), (1, "#202020")):
            if t in layers[lz]:
                draw(ax, unary_union(layers[lz][t]), fc=col, ec="none", alpha=0.85)
        if lz > z_leg0:
            draw(ax, section(leg, lz - 0.1), fc="none", ec="cyan", lw=0.6)
        b = box or D.bounds
        ax.set_xlim(b[0] - 1, b[2] + 1); ax.set_ylim(b[1] - 1, b[3] + 1); ax.set_aspect("equal")
        ax.set_title(f"{name}: Z {lz} (red: model outline, cyan: legend outline)")
        fig.savefig(f"{png}/{name}.png"); plt.close(fig)

    pic(min(layers), "first_layer")
    for lz in legz:
        pic(lz, f"legend_{lz:.1f}")
        Lb = section(leg, lz - 0.1)
        for k, p in enumerate(sorted(parts(Lb.buffer(1.5)), key=lambda q: (-q.centroid.y, q.centroid.x))):
            if k in (0, 5, 8, 9, 12, 13, 14, 16, 18, len(parts(Lb.buffer(1.5))) - 1):
                b = p.buffer(1.0).bounds
                pic(lz, f"legend_{lz:.1f}_key{k:02d}", box=b)
print("verify_keys:", "OK" if not fails else f"{len(fails)} problem(s)")
sys.exit(1 if fails else 0)
