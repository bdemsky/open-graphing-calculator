"""Minimal KiCad schematic generator.

Parts are placed on a grid; every connected pin gets a short wire stub and a
net label (global if the net spans sheets), unused pins get no-connect flags.
Connectivity is the source of truth; the drawing is for review.

design.py describes the circuit with this module's Design/Sheet API
(sheet.add(lib_id, ref, value, {pin: net}, footprint, **fields)); write()
produces the root schematic plus one sub-sheet per Sheet, and project-local
sym-lib-table / fp-lib-table files. Symbols come from KiCad's installed
libraries plus hardware/lib/calc.kicad_sym (parts imported by
lib/import_parts.py) and are embedded in each sheet, as KiCad requires.

Also provides the tiny s-expression toolkit (parse/dump/find, Q for quoted
atoms, fmt for numbers, uid for deterministic UUIDs) shared by kicad_pcb.py,
check_netlist.py and the other generators.

Determinism: every UUID is a uuid5 of a stable key and auto-assigned
references are remembered in refs.json, so regenerating an unchanged design
reproduces the files byte for byte and keeps the schematic-to-PCB links.
Coordinates are KiCad schematic millimetres (Y down); library symbols use Y up.
"""
import json
import math
import os
import re
import uuid as _uuid

KICAD_SYMBOLS = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols"
KICAD_FOOTPRINTS = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
SCH_VERSION = "20250114"
NC = object()  # pin explicitly not connected


# ---------------------------------------------------------------- s-expr ---
class Q(str):
    """A quoted string atom."""


def parse(text):
    """Parse KiCad s-expression text into nested lists.

    Each "(head a b ...)" becomes [head, a, b, ...]; quoted strings become Q
    (with \\" and \\\\ unescaped), every other atom stays a plain str (numbers
    are not converted). Returns the list of top-level nodes, so a single-file
    document is parse(text)[0].
    """
    tokens = re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()"]+', text)
    stack = [[]]
    for t in tokens:
        if t == "(":
            stack.append([])
        elif t == ")":
            node = stack.pop()
            stack[-1].append(node)
        elif t.startswith('"'):
            stack[-1].append(Q(t[1:-1].replace('\\"', '"').replace("\\\\", "\\")))
        else:
            stack[-1].append(t)
    return stack[0]


def dump(node, indent=0):
    """Serialise a parse()-style tree back to text in KiCad's layout.

    Lists of atoms only stay on one line; a list with sub-lists puts each
    sub-list on its own tab-indented line (the layout KiCad itself writes,
    which cleanup.py relies on). Q atoms are quoted and escaped, everything
    else is written with str().
    """
    if isinstance(node, Q):
        return '"' + node.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if not isinstance(node, list):
        return str(node)
    simple = all(not isinstance(c, list) for c in node)
    if simple:
        return "(" + " ".join(dump(c) for c in node) + ")"
    pad = "\t" * (indent + 1)
    head = [dump(c) for c in node[:1]]
    rest = []
    for c in node[1:]:
        rest.append(("\n" + pad + dump(c, indent + 1)) if isinstance(c, list) else (" " + dump(c)))
    return "(" + "".join(head) + "".join(rest) + "\n" + "\t" * indent + ")"


def find(node, key):
    """Direct children of node that are lists headed by key (not recursive)."""
    return [c for c in node if isinstance(c, list) and c and c[0] == key]


_NS = _uuid.UUID("6f1d3c5e-0b4a-4c1e-9d2f-ca1c0000beef")   # arbitrary, fixed uuid5 namespace


def uid(*key):
    """Deterministic UUID from a stable key, so regenerating keeps PCB links intact."""
    return Q(str(_uuid.uuid5(_NS, "/".join(str(k) for k in key))))


def fmt(v):
    """Number atom: rounded to 0.1 um, integers without a trailing ".0"."""
    v = round(v, 4)
    return str(int(v)) if v == int(v) else repr(v)


# --------------------------------------------------------------- symbols ---
class Pin:
    """One symbol pin: number/name/electrical type, library position (mm, Y up),
    angle (direction from the pin's tip towards the body, degrees), length and
    unit."""

    def __init__(self, number, name, etype, x, y, angle, length, unit=1):
        self.number, self.name, self.etype, self.unit = number, name, etype, unit
        self.x, self.y, self.angle, self.length = x, y, angle, length


class Symbol:
    """A library symbol flattened (extends resolved) and ready to embed."""

    def __init__(self, lib_id, node):
        self.lib_id = lib_id
        self.node = node
        self.props = {p[1]: p[2] for p in find(node, "property")}
        self.pins = []
        # sub-symbols are named "<name>_<unit>_<body style>"; unit 0 holds items
        # common to all units, style 2 is the De Morgan alternate (ignored)
        for sub in find(node, "symbol"):
            m = re.search(r"_(\d+)_(\d+)$", sub[1])
            unit, style = int(m.group(1)), int(m.group(2))
            if style not in (0, 1):
                continue
            for p in find(sub, "pin"):
                at = find(p, "at")[0]
                length = float(find(p, "length")[0][1]) if find(p, "length") else 2.54
                name = find(p, "name")[0][1]
                number = find(p, "number")[0][1]
                self.pins.append(Pin(str(number), str(name), p[1], float(at[1]), float(at[2]),
                                     int(float(at[3])) if len(at) > 3 else 0, length, max(unit, 1)))
        if not self.pins:
            raise ValueError(f"{lib_id}: no pins found")

    def embedded(self):
        """The symbol node for a sheet's lib_symbols, renamed to its full lib_id."""
        node = [c for c in self.node]
        node[1] = Q(self.lib_id)
        return node

    def units(self):
        return sorted({p.unit for p in self.pins})

    def unit_pins(self, unit):
        return [p for p in self.pins if p.unit == unit]

    def body_bbox(self, unit=1):
        """(xmin, xmax, ymin, ymax) of the unit's pin tips, library coords (Y up).

        Used as the body extent for placement; the drawn graphics are ignored.
        """
        xs, ys = [], []
        for p in self.unit_pins(unit):
            xs.append(p.x)
            ys.append(p.y)
        return min(xs), max(xs), min(ys), max(ys)


class Library:
    """Symbol lookup by "nick:name" over KiCad's installed .kicad_sym files plus
    extra {nick: path} libraries; files are parsed lazily and cached."""

    def __init__(self, extra=None):
        self.paths = {}  # nickname -> file
        for f in os.listdir(KICAD_SYMBOLS):
            if f.endswith(".kicad_sym"):
                self.paths[f[:-10]] = os.path.join(KICAD_SYMBOLS, f)
        for nick, path in (extra or {}).items():
            self.paths[nick] = path
        self._files = {}
        self._syms = {}

    def _file(self, nick):
        if nick not in self._files:
            tree = parse(open(self.paths[nick], encoding="utf-8").read())[0]
            self._files[nick] = {s[1]: s for s in find(tree, "symbol")}
        return self._files[nick]

    def _flatten(self, nick, name):
        """Resolve "(extends parent)" into a standalone symbol node.

        A derived symbol (e.g. a specific regulator sharing a generic drawing)
        holds only its own properties; a schematic's embedded lib_symbols must
        be self-contained, so the parent's units are copied (renamed to the
        child) and the child's properties override the parent's.
        """
        raw = self._file(nick)[name]
        ext = find(raw, "extends")
        if not ext:
            node = [c for c in raw]
        else:
            parent = self._flatten(nick, ext[0][1])
            pname = parent[1]
            node = [c for c in parent if not (isinstance(c, list) and c[0] == "property")]
            node[1] = Q(name)
            child_props = {p[1]: p for p in find(raw, "property")}
            parent_props = [p for p in find(parent, "property")]
            props = [child_props.pop(p[1], p) for p in parent_props] + list(child_props.values())
            # keep head flags (pin_numbers, in_bom...) then props then units
            head = [c for c in node[:2]]
            flags = [c for c in node[2:] if isinstance(c, list) and c[0] not in ("symbol",)]
            subs = []
            for s in find(node, "symbol"):
                s = [c for c in s]
                s[1] = Q(s[1].replace(pname, name, 1))
                subs.append(s)
            node = head + flags + props + subs
        # strip legacy (id N) from properties so newer parsers accept it
        clean = []
        for c in node:
            if isinstance(c, list) and c and c[0] == "property":
                c = [x for x in c if not (isinstance(x, list) and x and x[0] == "id")]
            clean.append(c)
        return clean

    def get(self, lib_id):
        """Symbol for "nick:name" (flattened, cached)."""
        if lib_id not in self._syms:
            nick, name = lib_id.split(":", 1)
            self._syms[lib_id] = Symbol(lib_id, self._flatten(nick, name))
        return self._syms[lib_id]


# ------------------------------------------------------------- schematic ---
class Part:
    """A symbol instance with its connections.

    conns maps a pin key to a net name (or NC). The key is a pin name (every
    pin of that name, e.g. all "GND" or "VDD" pins, gets the net), a pin
    number, or "#<number>", which always means a number: it picks one pin out
    of several sharing a name (the MCU's two VCAP pins) and wins over a pin
    whose name happens to look like a number. Every pin must be assigned, so a forgotten pin is an error here
    rather than an ERC warning.
    """

    def __init__(self, sheet, sym, ref, value, footprint, fields, conns):
        self.sheet, self.sym, self.ref, self.value = sheet, sym, ref, value
        self.footprint, self.fields = footprint, fields
        self.conns = {}  # pin number -> net name or NC
        pins_by_name = {}
        for p in sym.pins:
            pins_by_name.setdefault(p.name, []).append(p)
        numbers = {p.number for p in sym.pins}
        for key, net in conns.items():
            if key.startswith("#"):
                if key[1:] not in numbers:
                    raise KeyError(f"{ref}: no pin number {key[1:]}")
                self.conns[key[1:]] = net
            elif key in pins_by_name:
                for p in pins_by_name[key]:
                    self.conns[p.number] = net
            elif key in numbers:
                self.conns[key] = net
            else:
                raise KeyError(f"{ref} ({sym.lib_id}): no pin named {key!r}; "
                               f"pins: {sorted(pins_by_name)}")
        missing = [p for p in sym.pins if p.number not in self.conns]
        if missing:
            raise KeyError(f"{ref}: unassigned pins " +
                           ", ".join(f"{p.number}:{p.name}" for p in missing))
        self.pos = {}  # unit -> (x, y)


class Sheet:
    """One hierarchical sub-sheet (<name>.kicad_sch) and the parts on it."""

    def __init__(self, design, name, title):
        self.design, self.name, self.title = design, name, title
        self.parts = []
        self.uuid = uid("sheet", name)
        self.file = f"{name}.kicad_sch"

    def add(self, lib_id, ref, value, conns, footprint="", **fields):
        """ref is either explicit ('U1') or a prefix ('R') numbered at write time."""
        sym = self.design.lib.get(lib_id)
        part = Part(self, sym, ref, value, footprint, fields, conns)
        part.auto_ref = not ref[-1].isdigit()
        self.parts.append(part)
        return part

    def nets(self):
        """Names of all nets used on this sheet."""
        return {n for p in self.parts for n in p.conns.values() if n is not NC}


class Design:
    """The whole schematic: a root sheet holding one sheet symbol per Sheet.

    power_nets lists the nets that get a PWR_FLAG (nets driven by something
    ERC can't see as a power output, e.g. a regulator's output pin typed
    passive, or a battery terminal).
    """

    def __init__(self, project, lib, title, rev=None, date=None, comments=()):
        """title, rev, date and comments fill every sheet's title block (the
        sub-sheets use their own title and carry the project title as company)."""
        self.project, self.lib, self.title = project, lib, title
        self.rev, self.date, self.comments = rev, date, list(comments)
        self.sheets = []
        self.root_uuid = uid("root", project)
        self.power_nets = []

    def sheet(self, name, title):
        """Create and return a new sub-sheet."""
        s = Sheet(self, name, title)
        self.sheets.append(s)
        return s

    def _annotate(self, ref_file):
        """Number auto refs, reusing the numbers recorded in ref_file.

        A part's identity is its sheet, symbol, value and connections (plus an
        occurrence index for identical parts), so adding a part elsewhere does not
        renumber existing ones and break their link to the PCB.

        Order: explicit refs are reserved first, then remembered refs are
        reused, then the rest get the lowest free number for their prefix.
        ref_file is rewritten with the current mapping and self.new_refs lists
        the references assigned fresh in this run.
        """
        parts = [p for s in self.sheets for p in s.parts]
        known = json.load(open(ref_file)) if os.path.exists(ref_file) else {}
        seen = {}
        for p in parts:
            if p.auto_ref:
                sig = "|".join([p.sheet.name, p.sym.lib_id, p.value] +
                               [f"{k}={'NC' if v is NC else v}" for k, v in sorted(p.conns.items())])
                seen[sig] = seen.get(sig, 0) + 1
                p.identity = f"{sig}#{seen[sig]}"
        used = set()
        for p in parts:
            if not p.auto_ref:
                if p.ref in used:
                    raise ValueError(f"duplicate reference {p.ref}")
                used.add(p.ref)
        for p in parts:
            if p.auto_ref and known.get(p.identity) and known[p.identity] not in used:
                p.ref = known[p.identity]
                p.auto_ref = False
                used.add(p.ref)
        counters = {}
        fresh = {}
        for p in parts:
            if p.auto_ref:
                prefix = p.ref
                n = counters.get(prefix, 0)
                while True:
                    n += 1
                    if f"{prefix}{n}" not in used:
                        break
                counters[prefix] = n
                p.ref = f"{prefix}{n}"
                p.auto_ref = False
                used.add(p.ref)
                fresh[p.identity] = p.ref
        current = {p.identity: p.ref for p in parts if hasattr(p, "identity")}
        with open(ref_file, "w") as f:
            json.dump(current, f, indent=1, sort_keys=True)
        self.new_refs = sorted(fresh.values())

    def _check_footprints(self):
        """Fail early if any part names a footprint that doesn't exist on disk."""
        local = {"calc": os.path.join(os.path.dirname(self.lib.paths["calc"]), "calc.pretty")}
        for s in self.sheets:
            for p in s.parts:
                if not p.footprint:
                    continue
                nick, name = p.footprint.split(":", 1)
                d = local.get(nick, os.path.join(KICAD_FOOTPRINTS, f"{nick}.pretty"))
                if not os.path.exists(os.path.join(d, f"{name}.kicad_mod")):
                    raise FileNotFoundError(f"{p.ref}: footprint {p.footprint} not found")

    # ------------------------------------------------------------ layout ---
    @staticmethod
    def _label_len(net):
        """Approximate drawn length of a net label in mm (~1 mm per character)."""
        return 1.0 * len(net) + 3.0

    def _extent(self, part, unit):
        """Bounding box (relative to origin) including stubs and labels."""
        x0, x1, y0, y1 = part.sym.body_bbox(unit)
        # lib y is up; convert to schematic (y down)
        left, right, top, bottom = x0, x1, -y1, -y0
        for p in part.sym.unit_pins(unit):
            net = part.conns[p.number]
            extra = 2.54 + (0 if net is NC else self._label_len(net))
            ox, oy = -math.cos(math.radians(p.angle)), math.sin(math.radians(p.angle))
            px, py = p.x + ox * extra, -p.y + oy * extra
            left, right = min(left, px), max(right, px)
            top, bottom = min(top, py), max(bottom, py)
        if self._small(part):  # ref/value text sits to the right of the body
            right = max(right, x1 + 3.81 + 1.0 * max(len(part.ref), len(part.value)))
            return left - 3.81, right + 3.81, top - 2.54, bottom + 2.54
        return left - 3.81, right + 3.81, top - 5.08, bottom + 5.08

    @staticmethod
    def _small(part):
        """Small vertical parts (R, C, ...) get their text beside the body."""
        return len(part.sym.pins) <= 3 and not all(p.angle in (0, 180) for p in part.sym.pins)

    def _place(self, sheet):
        """Shelf-pack the sheet's part units left to right, row by row.

        Tries paper sizes A4 up to A0 and keeps the first on which everything
        fits (20 mm left/top margin, room for the title block at the bottom).
        Positions are snapped to the 2.54 mm (100 mil) schematic grid so pin
        ends land on grid and wires connect. Sets part.pos[unit] and
        sheet.paper.
        """
        papers = [("A4", 297, 210), ("A3", 420, 297), ("A2", 594, 420), ("A1", 841, 594), ("A0", 1189, 841)]
        items = [(p, u) for p in sheet.parts for u in p.sym.units()]
        exts = [self._extent(p, u) for p, u in items]
        for paper, W, H in papers:
            x, y, row_h, ok = 20.0, 20.0, 0.0, True
            placed = []
            for item, (l, r, t, b) in zip(items, exts):
                w, h = r - l, b - t
                if x + w > W - 15:
                    x, y, row_h = 20.0, y + row_h, 0.0
                if y + h > H - 40 or w > W - 35:
                    ok = False
                    break
                placed.append((item, x - l, y - t))
                x += w
                row_h = max(row_h, h)
            if ok:
                for (part, unit), px, py in placed:
                    part.pos[unit] = (round(px / 2.54) * 2.54, round(py / 2.54) * 2.54)
                sheet.paper = paper
                return
        raise RuntimeError(f"sheet {sheet.name} does not fit on A0")

    # ------------------------------------------------------------ output ---
    def _effects(self, hide=False, justify=None):
        """Text "effects" node: 1.27 mm font, optional justification and hide."""
        e = ["effects", ["font", ["size", "1.27", "1.27"]]]
        if justify:
            e.append(["justify", *justify])
        if hide:
            e.append(["hide", "yes"])
        return e

    def _prop(self, key, val, x, y, hide=False):
        return ["property", Q(key), Q(val), ["at", fmt(x), fmt(y), "0"], self._effects(hide)]

    def _sheet_items(self, sheet, global_nets):
        """All drawn items of one sheet."""
        items = []
        path = f"/{self.root_uuid}/{sheet.uuid}"
        for part in sheet.parts:
            items += self._part_items(part, path, global_nets)
        return items

    def _part_items(self, part, path, global_nets):
        """Symbol instance(s) of a part plus, per pin, a 2.54 mm wire stub ending
        in a net label (global_label for nets used on several sheets, a local
        label otherwise) or a no-connect flag. path is the sheet's instance
        path "/<root uuid>/<sheet uuid>" for the reference annotation."""
        items = []
        datasheet = part.fields.get("datasheet", part.sym.props.get("Datasheet", ""))
        fields = {k: v for k, v in part.fields.items() if k != "datasheet"}
        for unit in part.sym.units():
            X, Y = part.pos[unit]
            x0, x1, y0, y1 = part.sym.body_bbox(unit)
            if self._small(part):
                tx = X + x1 + 3.81
                ref_p = self._prop("Reference", part.ref, tx, Y - 1.27)
                val_p = self._prop("Value", part.value, tx, Y + 1.27)
                for pr in (ref_p, val_p):
                    pr[4].append(["justify", "left"])
            else:
                ref_p = self._prop("Reference", part.ref, X, Y - y1 - 3.81)
                val_p = self._prop("Value", part.value, X, Y - y0 + 3.81)
            props = [
                ref_p,
                val_p,
                self._prop("Footprint", part.footprint, X, Y, hide=True),
                self._prop("Datasheet", datasheet, X, Y, hide=True),
            ]
            for k, v in fields.items():
                props.append(self._prop(k, str(v), X, Y, hide=True))
            # test points and solder jumpers are board features, not BOM items (matches their footprints)
            in_bom = "no" if part.sym.lib_id.split(":")[0] in ("Jumper",) or "TestPoint" in part.sym.lib_id else "yes"
            sym = ["symbol", ["lib_id", Q(part.sym.lib_id)], ["at", fmt(X), fmt(Y), "0"], ["unit", str(unit)],
                   ["exclude_from_sim", "no"], ["in_bom", in_bom], ["on_board", "yes"], ["dnp", "no"],
                   ["uuid", uid("sym", part.ref, unit)], *props]
            for p in part.sym.unit_pins(unit):
                sym.append(["pin", Q(p.number), ["uuid", uid("pin", part.ref, p.number)]])
            sym.append(["instances", ["project", Q(self.project),
                                      ["path", Q(path), ["reference", Q(part.ref)], ["unit", str(unit)]]]])
            items.append(sym)
            for p in part.sym.unit_pins(unit):
                px, py = X + p.x, Y - p.y          # pin tip, schematic coords (library Y flipped)
                net = part.conns[p.number]
                if net is NC:
                    items.append(["no_connect", ["at", fmt(px), fmt(py)], ["uuid", uid("nc", part.ref, p.number)]])
                    continue
                # unit vector pointing away from the body (schematic Y down)
                ox, oy = -math.cos(math.radians(p.angle)), math.sin(math.radians(p.angle))
                ex, ey = px + ox * 2.54, py + oy * 2.54
                items.append(["wire", ["pts", ["xy", fmt(px), fmt(py)], ["xy", fmt(ex), fmt(ey)]],
                              ["stroke", ["width", "0"], ["type", "default"]], ["uuid", uid("wire", part.ref, p.number)]])
                ang = {(1, 0): 0, (-1, 0): 180, (0, -1): 90, (0, 1): 270}[(round(ox), round(oy))]
                just = "left" if ang in (0, 90) else "right"
                if net in global_nets:
                    items.append(["global_label", Q(net), ["shape", "bidirectional"],
                                  ["at", fmt(ex), fmt(ey), str(ang)], ["fields_autoplaced", "yes"],
                                  self._effects(justify=[just]), ["uuid", uid("label", part.ref, p.number)],
                                  ["property", Q("Intersheetrefs"), Q("${INTERSHEET_REFS}"),
                                   ["at", fmt(ex), fmt(ey), "0"], self._effects(hide=True, justify=[just])]])
                else:
                    items.append(["label", Q(net), ["at", fmt(ex), fmt(ey), str(ang)],
                                  ["fields_autoplaced", "yes"], self._effects(justify=[just, "bottom"]),
                                  ["uuid", uid("label", part.ref, p.number)]])
        return items

    def _title_block(self, title):
        """title_block node: title, date, rev, company (the project title), comments."""
        tb = ["title_block", ["title", Q(title)]]
        if self.date:
            tb.append(["date", Q(self.date)])
        if self.rev:
            tb.append(["rev", Q(self.rev)])
        tb.append(["company", Q(self.title)])
        tb += [["comment", str(i), Q(c)] for i, c in enumerate(self.comments, 1)]
        return tb

    def _file(self, paper, title, items, lib_ids, root=False):
        """A complete .kicad_sch tree. For sub-sheets the uuid entry (node[4])
        is left None and filled in by the caller."""
        lib_symbols = ["lib_symbols"] + [self.lib.get(i).embedded() for i in sorted(lib_ids)]
        node = ["kicad_sch", ["version", SCH_VERSION], ["generator", Q("calcgen")],
                ["generator_version", Q("1.0")],
                ["uuid", self.root_uuid if root else None], ["paper", Q(paper)],
                self._title_block(title),
                lib_symbols, *items]
        if root:
            node.append(["sheet_instances", ["path", Q("/"), ["page", Q("1")]]])
        node.append(["embedded_fonts", "no"])
        return node

    def write(self, outdir):
        """Write <project>.kicad_sch, every sub-sheet and the library tables.

        Steps: verify footprints, add PWR_FLAGs, annotate references (refs.json
        next to this file), decide which nets need global labels, then place
        and emit each sheet and finally the root sheet with a 4-wide grid of
        sheet symbols.
        """
        self._check_footprints()
        # power flags: one PWR_FLAG per declared power net, on the first sheet using it
        pf_done = set()
        for s in self.sheets:
            for net in self.power_nets:
                if net in s.nets() and net not in pf_done:
                    s.add("power:PWR_FLAG", "#FLG", "PWR_FLAG", {"1": net})
                    pf_done.add(net)
        self._annotate(os.path.join(os.path.dirname(os.path.abspath(__file__)), "refs.json"))
        usage = {}
        for s in self.sheets:
            for n in s.nets():
                usage.setdefault(n, set()).add(s.name)
        global_nets = {n for n, ss in usage.items() if len(ss) > 1}
        self.global_nets = global_nets
        os.makedirs(outdir, exist_ok=True)
        for s in self.sheets:
            self._place(s)
            items = self._sheet_items(s, global_nets)
            node = self._file(s.paper, s.title, items, {p.sym.lib_id for p in s.parts})
            node[4] = ["uuid", s.uuid]
            with open(os.path.join(outdir, s.file), "w") as f:
                f.write(dump(node) + "\n")
        # root: one sheet symbol per subsheet
        items = []
        for i, s in enumerate(self.sheets):
            x, y = 25.4 + (i % 4) * 63.5, 30.48 + (i // 4) * 50.8
            items.append(["sheet", ["at", fmt(x), fmt(y)], ["size", "50.8", "30.48"],
                          ["exclude_from_sim", "no"], ["in_bom", "yes"], ["on_board", "yes"], ["dnp", "no"],
                          ["fields_autoplaced", "yes"],
                          ["stroke", ["width", "0.1524"], ["type", "solid"]],
                          ["fill", ["color", "0", "0", "0", "0.0000"]], ["uuid", s.uuid],
                          ["property", Q("Sheetname"), Q(s.title), ["at", fmt(x), fmt(y - 0.7), "0"],
                           self._effects(justify=["left", "bottom"])],
                          ["property", Q("Sheetfile"), Q(s.file), ["at", fmt(x), fmt(y + 31.1), "0"],
                           self._effects(justify=["left", "top"])],
                          ["instances", ["project", Q(self.project),
                                         ["path", Q(f"/{self.root_uuid}"), ["page", Q(str(i + 2))]]]]])
        root = self._file("A4", self.title, items, set(), root=True)
        with open(os.path.join(outdir, f"{self.project}.kicad_sch"), "w") as f:
            f.write(dump(root) + "\n")
        self._write_lib_tables(outdir)

    def _write_lib_tables(self, outdir):
        """Project-local library tables, so the project works without global setup."""
        parts = [p for s in self.sheets for p in s.parts]
        sym_libs = sorted({p.sym.lib_id.split(":")[0] for p in parts})
        fp_libs = sorted({p.footprint.split(":")[0] for p in parts if p.footprint})
        local = {"calc"}
        with open(os.path.join(outdir, "sym-lib-table"), "w") as f:
            f.write("(sym_lib_table\n  (version 7)\n")
            for nick in sym_libs:
                uri = ("${KIPRJMOD}/../lib/calc.kicad_sym" if nick in local
                       else f"${{KICAD10_SYMBOL_DIR}}/{nick}.kicad_sym")
                f.write(f'  (lib (name "{nick}")(type "KiCad")(uri "{uri}")(options "")(descr ""))\n')
            f.write(")\n")
        with open(os.path.join(outdir, "fp-lib-table"), "w") as f:
            f.write("(fp_lib_table\n  (version 7)\n")
            for nick in fp_libs:
                uri = ("${KIPRJMOD}/../lib/calc.pretty" if nick in local
                       else f"${{KICAD10_FOOTPRINT_DIR}}/{nick}.pretty")
                f.write(f'  (lib (name "{nick}")(type "KiCad")(uri "{uri}")(options "")(descr ""))\n')
            f.write(")\n")
