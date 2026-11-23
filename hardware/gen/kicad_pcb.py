"""Minimal KiCad board writer: footprints placed from a netlist, plus outline,
zones, tracks and vias. Output is KiCad 9 format; pcb_build.py upgrades it
with "kicad-cli pcb upgrade".

Board is the in-memory model every board stage works on: layout.py places
footprints (Board.place), fanout.py / route.py / finish.py append tracks and
vias, pcb_build.py adds zones, and write() emits the .kicad_pcb. Footprints
are read from KiCad's installed libraries or hardware/lib/calc.pretty and
copied into the board with the netlist's nets assigned to their pads.

Tracks are tuples (net, layer, (x, y), (x, y), width) and vias
(net, (x, y), size, drill); nets are full netlist names ("/MCU/VCAP1").
Coordinates are KiCad board mm (Y down). Rotations follow KiCad: degrees,
counter-clockwise as seen from the top. Zone fills are never written (KiCad
computes them); all UUIDs are deterministic (kicad_sch.uid).
"""
import math
import os
import re

from kicad_sch import KICAD_FOOTPRINTS, Q, dump, find, fmt, parse, uid

PCB_VERSION = "20241229"
# (KiCad layer id, name, type[, user name]); copper order is F, In1, In2, B.
# In1.Cu is typed "power" (the GND plane), the other copper layers "signal".
LAYERS = [(0, "F.Cu", "signal"), (4, "In1.Cu", "power"), (6, "In2.Cu", "signal"), (2, "B.Cu", "signal"),
          (9, "F.Adhes", "user", "F.Adhesive"), (11, "B.Adhes", "user", "B.Adhesive"),
          (13, "F.Paste", "user"), (15, "B.Paste", "user"),
          (5, "F.SilkS", "user", "F.Silkscreen"), (7, "B.SilkS", "user", "B.Silkscreen"),
          (1, "F.Mask", "user"), (3, "B.Mask", "user"),
          (17, "Dwgs.User", "user", "User.Drawings"), (19, "Cmts.User", "user", "User.Comments"),
          (21, "Eco1.User", "user", "User.Eco1"), (23, "Eco2.User", "user", "User.Eco2"),
          (25, "Edge.Cuts", "user"), (27, "Margin", "user"),
          (31, "F.CrtYd", "user", "F.Courtyard"), (29, "B.CrtYd", "user", "B.Courtyard"),
          (35, "F.Fab", "user"), (33, "B.Fab", "user")]
FLIP = {"F": "B", "B": "F"}


def flip_layer(name):
    """Mirror a layer name for a bottom-side footprint (F.* <-> B.*); others unchanged."""
    if name.startswith(("F.", "B.")):
        return FLIP[name[0]] + name[1:]
    if name == "*.Cu" or name.startswith("*"):
        return name
    return name


# ------------------------------------------------------------- netlist ---
def load_netlist(path):
    """Read a KiCad .net file into (comps, net names).

    comps maps ref -> {ref, value, footprint, path (the symbol's instance path,
    which links the PCB footprint to the schematic symbol), sheetname,
    sheetfile, fields (e.g. LCSC), pads {pin number: full net name},
    nets {pin number: short net name}}.
    """
    root = parse(open(path).read())[0]
    sheet_files = {}
    for sh in find(find(root, "design")[0], "sheet"):
        sheet_files[find(sh, "name")[0][1]] = find(find(sh, "title_block")[0], "source")[0][1]
    comps = {}
    for c in find(find(root, "components")[0], "comp"):
        ref = find(c, "ref")[0][1]
        fields = {}
        for fl in find(c, "fields"):
            for f in find(fl, "field"):
                name = find(f, "name")[0][1]
                fields[name] = f[2] if len(f) > 2 and not isinstance(f[2], list) else ""
        sp = find(c, "sheetpath")[0]
        comps[ref] = {
            "ref": ref, "value": find(c, "value")[0][1],
            "footprint": (find(c, "footprint") or [[None, ""]])[0][1],
            "path": find(sp, "tstamps")[0][1] + find(c, "tstamps")[0][1],
            "sheetname": find(sp, "names")[0][1],
            "sheetfile": sheet_files[find(sp, "names")[0][1]],
            "fields": fields, "pads": {},
        }
    nets = []
    for n in find(find(root, "nets")[0], "net"):
        name = find(n, "name")[0][1]
        nets.append(name)
        for x in find(n, "node"):
            comps[find(x, "ref")[0][1]]["pads"][find(x, "pin")[0][1]] = name
    for c in comps.values():  # short names ("VCAP1" rather than "/MCU/VCAP1") for placement code
        c["nets"] = {k: v.rsplit("/", 1)[-1] for k, v in c["pads"].items()}
    return comps, nets


# ----------------------------------------------------------- footprints ---
class Footprint:
    """A parsed .kicad_mod: the raw node plus a pad list and body bounding box.

    Each pad is a dict: num, kind (smd / thru_hole / np_thru_hole / connect),
    x, y, rot (footprint-local), w, h (copper extent), ox, oy (offset of the
    copper centre from the pad anchor, non-zero only for custom pads) and the
    raw node.
    """

    def __init__(self, lib_id, local_lib):
        nick, name = lib_id.split(":", 1)
        d = local_lib if nick == "calc" else os.path.join(KICAD_FOOTPRINTS, f"{nick}.pretty")
        self.lib_id = lib_id
        self.node = parse(open(os.path.join(d, f"{name}.kicad_mod"), encoding="utf-8").read())[0]
        self.pads = []
        for p in find(self.node, "pad"):
            at = find(p, "at")[0]
            size = find(p, "size")[0]
            pad = {"num": str(p[1]), "kind": p[2], "x": float(at[1]), "y": float(at[2]),
                   "rot": float(at[3]) if len(at) > 3 else 0.0,
                   "w": float(size[1]), "h": float(size[2]), "ox": 0.0, "oy": 0.0, "node": p}
            if p[3] == "custom":
                self._custom_extent(pad, p)
            self.pads.append(pad)
        self.attr = (find(self.node, "attr") or [["attr"]])[0][1:]
        self.bbox = self._bbox()

    @staticmethod
    def _custom_extent(pad, node):
        """Custom pads carry a dummy size; their copper is the primitives (pad-local)."""
        xs, ys = [-pad["w"] / 2, pad["w"] / 2], [-pad["h"] / 2, pad["h"] / 2]
        for prims in find(node, "primitives"):
            for g in prims[1:]:
                w = float(find(g, "width")[0][1]) / 2 if find(g, "width") else 0.0
                pts = [(float(e[1]), float(e[2])) for k in ("start", "end", "mid", "center")
                       for e in find(g, k)]
                for pl in find(g, "pts"):
                    pts += [(float(xy[1]), float(xy[2])) for xy in find(pl, "xy")]
                if g[0] == "gr_circle":
                    c, e = find(g, "center")[0], find(g, "end")[0]
                    r = math.hypot(float(e[1]) - float(c[1]), float(e[2]) - float(c[2]))
                    pts += [(float(c[1]) - r, float(c[2]) - r), (float(c[1]) + r, float(c[2]) + r)]
                for x, y in pts:
                    xs += [x - w, x + w]
                    ys += [y - w, y + w]
        pad["w"], pad["h"] = max(xs) - min(xs), max(ys) - min(ys)
        pad["ox"], pad["oy"] = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2

    def _bbox(self):
        """Body extent: pads plus F.Fab/F.CrtYd graphics (local coords).

        Returns (xmin, ymin, xmax, ymax). Silkscreen is not included.
        """
        xs, ys = [], []
        for p in self.pads:
            r = math.radians(p["rot"])
            hw = abs(p["w"] / 2 * math.cos(r)) + abs(p["h"] / 2 * math.sin(r))
            hh = abs(p["w"] / 2 * math.sin(r)) + abs(p["h"] / 2 * math.cos(r))
            cx = p["x"] + p["ox"] * math.cos(r) + p["oy"] * math.sin(r)
            cy = p["y"] - p["ox"] * math.sin(r) + p["oy"] * math.cos(r)
            xs += [cx - hw, cx + hw]
            ys += [cy - hh, cy + hh]
        for g in self.node:
            if isinstance(g, list) and g and g[0] in ("fp_line", "fp_rect", "fp_poly", "fp_circle", "fp_arc"):
                ly = find(g, "layer")
                if not ly or ly[0][1] not in ("F.CrtYd", "F.Fab"):
                    continue
                if g[0] == "fp_circle":
                    c, e = find(g, "center")[0], find(g, "end")[0]
                    rad = math.hypot(float(e[1]) - float(c[1]), float(e[2]) - float(c[2]))
                    xs += [float(c[1]) - rad, float(c[1]) + rad]
                    ys += [float(c[2]) - rad, float(c[2]) + rad]
                    continue
                for k in ("start", "end", "mid"):
                    for e in find(g, k):
                        xs.append(float(e[1]))
                        ys.append(float(e[2]))
                for pts in find(g, "pts"):
                    for xy in find(pts, "xy"):
                        xs.append(float(xy[1]))
                        ys.append(float(xy[2]))
        return min(xs), min(ys), max(xs), max(ys)


class Placed:
    """A footprint instance: netlist component, Footprint, position (mm),
    rotation (deg) and side ("F" or "B")."""

    def __init__(self, comp, fp, x, y, rot, side):
        self.comp, self.fp, self.x, self.y, self.rot, self.side = comp, fp, x, y, rot, side

    @property
    def ref(self):
        return self.comp["ref"]

    def to_board(self, lx, ly):
        """Footprint-local point -> board coordinates (mirror for B, then rotate CCW, then translate)."""
        if self.side == "B":  # KiCad stores back-side footprints mirrored top-to-bottom
            ly = -ly
        r = math.radians(self.rot)
        return (self.x + lx * math.cos(r) + ly * math.sin(r),
                self.y - lx * math.sin(r) + ly * math.cos(r))

    def rect(self, margin=0.0):
        """Axis-aligned board-coordinate bounding box of the body, grown by margin."""
        x0, y0, x1, y1 = self.fp.bbox
        pts = [self.to_board(x, y) for x, y in ((x0, y0), (x1, y0), (x0, y1), (x1, y1))]
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        return min(xs) - margin, min(ys) - margin, max(xs) + margin, max(ys) + margin

    def pad_pos(self, num):
        """Board positions of every pad numbered num (anchor points)."""
        return [self.to_board(p["x"], p["y"]) for p in self.fp.pads if p["num"] == num]


class Board:
    """The board under construction (see module doc).

    net_ids numbers the nets in netlist order (0 = no net), as the KiCad file
    format requires. Stages also attach ad-hoc attributes (outline, mount_holes,
    power_islands, gnd_patches, fills, ...) that the later stages read with
    getattr defaults.
    """

    def __init__(self, netlist, local_lib, project="calc"):
        self.comps, self.net_names = load_netlist(netlist)
        self.local_lib = local_lib
        self.project = project
        self.placed = {}
        self.fps = {}
        self.graphics = []
        self.zones = []
        self.tracks = []
        self.vias = []
        self.net_ids = {"": 0}
        for n in self.net_names:
            self.net_ids.setdefault(n, len(self.net_ids))

    def footprint(self, lib_id):
        """Footprint for "nick:name", loaded once and cached."""
        if lib_id not in self.fps:
            self.fps[lib_id] = Footprint(lib_id, self.local_lib)
        return self.fps[lib_id]

    def place(self, ref, x, y, rot=0.0, side="F"):
        """Place (or re-place) ref's footprint at board (x, y) mm; returns the Placed."""
        c = self.comps[ref]
        p = Placed(c, self.footprint(c["footprint"]), x, y, rot, side)
        self.placed[ref] = p
        return p

    # ----------------------------------------------------------- items ---
    def outline_rounded_rect(self, x0, y0, x1, y1, r):
        """Edge.Cuts outline: four lines plus four 90-degree corner arcs of radius r.

        KiCad arcs are given by start/mid/end; the mid point of each corner is
        r * (1 - cos 45 deg) in from both edges. Also sets self.outline.
        """
        segs = [((x0 + r, y0), (x1 - r, y0)), ((x1, y0 + r), (x1, y1 - r)),
                ((x1 - r, y1), (x0 + r, y1)), ((x0, y1 - r), (x0, y0 + r))]
        for (a, b) in segs:
            self.graphics.append(["gr_line", ["start", fmt(a[0]), fmt(a[1])], ["end", fmt(b[0]), fmt(b[1])],
                                  ["stroke", ["width", "0.1"], ["type", "solid"]], ["layer", Q("Edge.Cuts")],
                                  ["uuid", uid("edge", a, b)]])
        k = r * (1 - math.cos(math.radians(45)))
        corners = [((x0, y0 + r), (x0 + k, y0 + k), (x0 + r, y0)), ((x1 - r, y0), (x1 - k, y0 + k), (x1, y0 + r)),
                   ((x1, y1 - r), (x1 - k, y1 - k), (x1 - r, y1)), ((x0 + r, y1), (x0 + k, y1 - k), (x0, y1 - r))]
        for s, m, e in corners:
            self.graphics.append(["gr_arc", ["start", fmt(s[0]), fmt(s[1])], ["mid", fmt(m[0]), fmt(m[1])],
                                  ["end", fmt(e[0]), fmt(e[1])], ["stroke", ["width", "0.1"], ["type", "solid"]],
                                  ["layer", Q("Edge.Cuts")], ["uuid", uid("edgearc", s, e)]])
        self.outline = (x0, y0, x1, y1, r)

    def text(self, s, x, y, layer="F.SilkS", size=1.0, rot=0):
        """Board text (mirrored automatically on B.* layers so it reads from below)."""
        self.graphics.append(["gr_text", Q(s), ["at", fmt(x), fmt(y), str(rot)], ["layer", Q(layer)],
                              ["uuid", uid("text", s, x, y, layer)],
                              ["effects", ["font", ["size", fmt(size), fmt(size)], ["thickness", fmt(size * 0.15)]]]
                              + ([["justify", "mirror"]] if layer.startswith("B.") else [])])

    def rect_graphic(self, x0, y0, x1, y1, layer, width=0.1):
        self.graphics.append(["gr_rect", ["start", fmt(x0), fmt(y0)], ["end", fmt(x1), fmt(y1)],
                              ["stroke", ["width", fmt(width)], ["type", "solid"]], ["fill", "no"],
                              ["layer", Q(layer)], ["uuid", uid("rect", x0, y0, layer)]])

    def zone(self, net, layer, pts, priority=0, clearance=0.25, keepout=None, name=None, solid=True):
        """Add a copper zone (or, with keepout={...}, a rule area) over polygon pts.

        Higher priority zones fill first and win where zones overlap. solid=True
        connects same-net pads solidly ("connect_pads yes"); solid=False uses
        thermal reliefs (0.3 mm gap, 0.35 mm spokes). Minimum fill width 0.2 mm.
        """
        z =["zone", ["net", str(self.net_ids[net]) if net else "0"], ["net_name", Q(net or "")],
             ["layer", Q(layer)], ["uuid", uid("zone", net, layer, pts[0])]]
        if name:
            z.append(["name", Q(name)])
        z += [["hatch", "edge", "0.5"], ["priority", str(priority)],
              ["connect_pads", *(["yes"] if solid else []), ["clearance", fmt(clearance)]],
              ["min_thickness", "0.2"],
              ["filled_areas_thickness", "no"]]
        if keepout:
            z.append(["keepout", ["tracks", keepout.get("tracks", "not_allowed")],
                      ["vias", keepout.get("vias", "not_allowed")], ["pads", "allowed"],
                      ["copperpour", keepout.get("copperpour", "not_allowed")], ["footprints", "allowed"]])
        else:
            z.append(["fill", "yes", ["thermal_gap", "0.3"], ["thermal_bridge_width", "0.35"]])
        z.append(["polygon", ["pts", *[["xy", fmt(x), fmt(y)] for x, y in pts]]])
        self.zones.append(z)

    def track(self, net, layer, a, b, width):
        self.tracks.append((net, layer, a, b, width))

    def via(self, net, at, size=0.6, drill=0.3):
        self.vias.append((net, at, size, drill))

    # ---------------------------------------------------------- output ---
    def _fp_node(self, pl):
        """The board's copy of a placed footprint.

        Library items are transformed for rotation/side, Reference and Value
        are set from the netlist, each pad gets its net, the schematic fields
        (e.g. LCSC) are added as hidden properties, and path/sheetname/sheetfile
        link the footprint to its schematic symbol.
        """
        c, fp = pl.comp, pl.fp
        node = ["footprint", Q(fp.lib_id), ["layer", Q("F.Cu" if pl.side == "F" else "B.Cu")],
                ["uuid", uid("fp", c["ref"])], ["at", fmt(pl.x), fmt(pl.y), fmt(pl.rot)]]
        skip = {"version", "generator", "generator_version", "layer", "uuid", "at"}
        for item in fp.node[2:]:
            if not isinstance(item, list) or item[0] in skip:
                continue
            item = self._transform(item, pl)
            if item[0] == "property":
                if item[1] == "Reference":
                    item[2] = Q(c["ref"])
                elif item[1] == "Value":
                    item[2] = Q(c["value"])
            if item[0] == "pad":
                net = c["pads"].get(str(item[1]), "")
                item = [x for x in item if not (isinstance(x, list) and x[0] in ("net", "uuid"))]
                if net and item[2] != "np_thru_hole":
                    item.append(["net", str(self.net_ids[net]), Q(net)])
                item.append(["uuid", uid("pad", c["ref"], item[1], len(node))])
            node.append(item)
        for k, v in c["fields"].items():
            node.append(["property", Q(k), Q(v), ["at", "0", "0", "0"], ["layer", Q("F.Fab")], ["hide", "yes"],
                         ["uuid", uid("prop", c["ref"], k)], ["effects", ["font", ["size", "1", "1"]]]])
        node.append(["path", Q(c["path"])])
        node.append(["sheetname", Q(c["sheetname"])])
        node.append(["sheetfile", Q(c["sheetfile"])])
        return node

    def _hole_node(self, ref, x, y, lib_id="MountingHole:MountingHole_2.2mm_M2"):
        """Board-only mounting hole (not in the schematic)."""
        fp = self.footprint(lib_id)
        node = ["footprint", Q(lib_id), ["layer", Q("F.Cu")], ["uuid", uid("hole", ref)],
                ["at", fmt(x), fmt(y)]]
        for item in fp.node[2:]:
            if not isinstance(item, list) or item[0] in ("version", "generator", "generator_version",
                                                         "layer", "uuid", "at", "attr"):
                continue
            if item[0] == "property" and item[1] == "Reference":
                item = [x for x in item]
                item[2] = Q(ref)
            node.append(item)
        node.append(["attr", "board_only", "exclude_from_pos_files", "exclude_from_bom"])
        return node

    def _transform(self, item, pl):
        """Deep-copy an item; add footprint rotation to pad/text angles; mirror for bottom.

        In a .kicad_pcb, footprint children keep local coordinates, but pad and
        text "at" angles are absolute (local angle + footprint rotation). For a
        bottom-side footprint KiCad stores the library geometry mirrored in Y,
        with F/B layers swapped, pad angles negated and text mirrored.
        """
        def walk(n):
            if not isinstance(n, list):
                return n
            n = [walk(x) for x in n]
            if not n:
                return n
            head = n[0]
            if pl.side == "B":
                if head == "layer":
                    n[1] = Q(flip_layer(str(n[1])))
                elif head == "layers":
                    n[1:] = [Q(flip_layer(str(x))) for x in n[1:]]
                elif head in ("start", "end", "mid", "center", "xy") and len(n) >= 3:
                    n[2] = fmt(-float(n[2]))
            return n
        item = walk(item)
        is_text = item[0] in ("property", "fp_text")
        if item[0] == "pad" or is_text:
            for i, x in enumerate(item):
                if isinstance(x, list) and x and x[0] == "at":
                    ang = float(x[3]) if len(x) > 3 else 0.0
                    ly = float(x[2])
                    if pl.side == "B":
                        ly = -ly
                        ang = -ang if item[0] == "pad" else ang
                    item[i] = ["at", x[1], fmt(ly), fmt((ang + pl.rot) % 360)]
            if pl.side == "B" and is_text:
                for x in item:
                    if isinstance(x, list) and x and x[0] == "effects":
                        if not find(x, "justify"):
                            x.append(["justify", "mirror"])
        return item

    def write(self, path, title_block=None):
        """Write the complete .kicad_pcb (nets, footprints, holes, graphics,
        tracks, vias, unfilled zones) to path. title_block is design.TITLE_BLOCK
        (title, rev, date, comments)."""
        tb = title_block or {"title": "Graphing Calculator"}
        tb_node = ["title_block", ["title", Q(tb["title"])]]
        tb_node += [[k, Q(tb[k])] for k in ("date", "rev", "company") if tb.get(k)]
        tb_node += [["comment", str(i), Q(c)] for i, c in enumerate(tb.get("comments", []), 1)]
        node =["kicad_pcb", ["version", PCB_VERSION], ["generator", Q("calcgen")], ["generator_version", Q("1.0")],
                ["general", ["thickness", "1.6"], ["legacy_teardrops", "no"]], ["paper", Q("A3")],
                tb_node,
                ["layers", *[[str(l[0]), Q(l[1]), l[2], *([Q(l[3])] if len(l) > 3 else [])] for l in LAYERS]],
                ["setup", ["pad_to_mask_clearance", "0"], ["allow_soldermask_bridges_in_footprints", "no"],
                 ["tenting", "front", "back"]]]
        for name, i in sorted(self.net_ids.items(), key=lambda kv: kv[1]):
            node.append(["net", str(i), Q(name)])
        for ref in sorted(self.placed):
            node.append(self._fp_node(self.placed[ref]))
        for i, (x, y) in enumerate(getattr(self, "mount_holes", []), 1):
            node.append(self._hole_node(f"H{i}", x, y))
        node += self.graphics
        for net, layer, a, b, w in self.tracks:
            node.append(["segment", ["start", fmt(a[0]), fmt(a[1])], ["end", fmt(b[0]), fmt(b[1])],
                         ["width", fmt(w)], ["layer", Q(layer)], ["net", str(self.net_ids[net])],
                         ["uuid", uid("seg", net, layer, a, b)]])
        for net, at, size, drill in self.vias:
            node.append(["via", ["at", fmt(at[0]), fmt(at[1])], ["size", fmt(size)], ["drill", fmt(drill)],
                         ["layers", Q("F.Cu"), Q("B.Cu")],
                         # no inner-layer pads where nothing connects: smaller antipads keep
                         # webs of In1 GND between closely spaced vias
                         ["remove_unused_layers", "yes"], ["keep_end_layers", "yes"],
                         ["net", str(self.net_ids[net])],
                         ["uuid", uid("via", net, at)]])
        node += self.zones
        with open(path, "w") as f:
            f.write(dump(node) + "\n")
