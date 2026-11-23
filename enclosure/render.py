#!/usr/bin/env python3
"""Render the board and the assembled calculator (pyvista, off-screen).

Inputs: hardware/build/3d/board.glb (kicad-cli export, origin at the board
centre) and enclosure/out/*.stl. Outputs PNGs in enclosure/renders/.

Run enclosure/case.py first (it writes the render_*.stl parts in assembled position).
The board meshes are shifted down by the PCB thickness so that, like case.py, Z = 0 is
the PCB top surface; colours come from firmware/keys/layout.py via case.keys_layout.
Images: board_iso.png, board_top.png, exploded.png, assembled_front.png, keypad_top.png,
assembled_back.png (the transferred face artwork, overlay.py, is on the top shell:
render_overlay.stl and its labels render_top_{shift,alpha}.stl), and overlay.png (a 2D picture
of the artwork on the shell with the keycaps).
"""
import os
import sys

import numpy as np
import pyvista as pv
import trimesh

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
REN = os.path.join(HERE, "renders")
pv.OFF_SCREEN = True


def board_meshes(path):
    """GLB scene -> [(vertices, faces, rgb)] merged per colour; mm, Z up, PCB bottom at Z=0."""
    scene = trimesh.load(path, force="scene")
    groups = {}
    for node in scene.graph.nodes_geometry:
        tf, gname = scene.graph[node]
        g = scene.geometry[gname]
        color = (0.6, 0.6, 0.6)
        mat = getattr(g.visual, "material", None)
        c = getattr(mat, "baseColorFactor", None) if mat is not None else None
        if c is not None:
            c = np.asarray(c[:3], dtype=float)
            color = tuple(np.round(c / (255.0 if c.max() > 1 else 1.0), 2))
        v = trimesh.transformations.transform_points(g.vertices, tf) * 1000.0
        v = np.column_stack([v[:, 0], -v[:, 2], v[:, 1]])     # glTF Y-up -> Z-up, board y flipped
        groups.setdefault(color, []).append((v, g.faces))
    out = []
    for color, items in groups.items():
        offs, vs, fs = 0, [], []
        for v, f in items:
            vs.append(v)
            fs.append(f + offs)
            offs += len(v)
        out.append((np.vstack(vs), np.vstack(fs), color))
    return out


def to_pv(v, f):
    """Triangle mesh (vertices N x 3, faces M x 3) -> pyvista PolyData (VTK's [3, a, b, c] face list)."""
    faces = np.hstack([np.full((len(f), 1), 3), f]).ravel()
    return pv.PolyData(v, faces)


def main():
    """Load the board and case meshes and write the five renders to REN."""
    os.makedirs(REN, exist_ok=True)
    glb = os.path.join(HERE, "../hardware/build/3d/board.glb")
    parts = board_meshes(glb)
    allv = np.vstack([v for v, _, _ in parts])
    print("board bounds", np.round(allv.min(0), 2), np.round(allv.max(0), 2))
    # shift so the PCB top is Z = 0 (case.py's frame): kicad-cli puts the PCB bottom at Z = 0
    # and the board is 1.6 mm thick
    z_top = 1.6
    board = [(to_pv(v - [0, 0, z_top], f), c) for v, f, c in parts]
    sys.path.insert(0, HERE)
    import case as cs
    kl = cs.keys_layout   # firmware/keys/layout.py: colour groups and filament colours
    files = {"top": "top_shell.stl", "back": "back_shell_base.stl", "door": "render_door.stl",
             "top_shift": "render_top_shift.stl", "top_alpha": "render_top_alpha.stl",
             "overlay": "render_overlay.stl"}
    for g in cs.GROUPS:
        files["k_" + g] = f"render_keys_{g}.stl"
        files["k_" + g + "_legend"] = f"render_keys_{g}_legend.stl"
    case = {k: pv.read(os.path.join(OUT, f)) for k, f in files.items()}
    # display panel: a box filling the module outline (case.PANEL) from PANEL_BACK to just under
    # CEIL, with a thin dark-glass box over the LCD active area (case.AA) on top
    px, py, pw, ph = cs.board_rect(cs.PANEL)
    ax, ay, aw, ah = cs.board_rect(cs.AA)
    case["panel"] = pv.Box(bounds=(px - pw / 2, px + pw / 2, py - ph / 2, py + ph / 2, cs.PANEL_BACK, cs.CEIL - 0.05))
    case["glass"] = pv.Box(bounds=(ax - aw / 2, ax + aw / 2, ay - ah / 2, ay + ah / 2, cs.CEIL - 0.05, cs.CEIL))
    import overlay as ov
    colors = {"top_shift": ov.ink(kl.SHIFT_INK), "top_alpha": ov.ink(kl.ALPHA_INK), "overlay": kl.BODY,
              "panel": "#9aa0a6", "glass": "#0b1a2a", "top": kl.BODY, "back": kl.BODY, "door": kl.BODY}
    for g in cs.GROUPS:
        colors["k_" + g] = kl.PALETTE[g]["cap"]
        colors["k_" + g + "_legend"] = kl.PALETTE[g]["legend"]

    def shot(name, items, view, size=(1400, 1800), zoom=1.0, parallel=False):
        """Render [(mesh, colour, add_mesh options)] with camera setter `view` to REN/name."""
        p =pv.Plotter(off_screen=True, window_size=size, lighting="three lights")
        p.set_background("#f2f2f2", top="#c9d3df")
        for mesh, color, opts in items:
            p.add_mesh(mesh, color=color, smooth_shading=False, **opts)
        view(p)
        if parallel:
            p.enable_parallel_projection()
        p.camera.zoom(zoom)
        p.screenshot(os.path.join(REN, name))
        p.close()
        print("wrote", name)

    board_items = [(m, c, {}) for m, c in board]

    def iso(p):
        p.camera_position = [(140, -260, 230), (0, -10, 0), (0, 0, 1)]

    def top_view(p):
        p.camera_position = [(0, 0, 500), (0, 0, 0), (0, 1, 0)]

    shot("board_iso.png", board_items, iso, size=(1600, 1400), zoom=1.15)
    shot("board_top.png", board_items, top_view, size=(900, 1800), zoom=1.9, parallel=True)
    # exploded (Z offsets in mm): back shell and door -25, board 0, panel +8, keycaps +18,
    # top shell and its legends +42 (clears the 9.3 mm tall keys)
    ex = []
    for k in ("back", "door"):
        ex.append((case[k].translate((0, 0, -25)), colors[k], {}))
    ex += board_items
    for k in ("panel", "glass"):
        ex.append((case[k].translate((0, 0, 8)), colors[k], {}))
    for g in cs.GROUPS:
        for kk in ("k_" + g, "k_" + g + "_legend"):
            ex.append((case[kk].translate((0, 0, 18)), colors[kk], {}))
    for kk in ("top", "overlay", "top_shift", "top_alpha"):
        ex.append((case[kk].translate((0, 0, 42)), colors[kk], {}))

    def ex_view(p):
        p.camera_position = [(260, -330, 170), (0, 0, 0), (0, 0, 1)]
    shot("exploded.png", ex, ex_view, size=(1600, 1600), zoom=1.05)
    asm = [(case[k], colors[k], {}) for k in colors] + board_items

    def front(p):
        p.camera_position = [(90, -170, 330), (0, -5, 0), (0, 0.8, 0.2)]
    shot("assembled_front.png", asm, front, size=(1200, 1700), zoom=1.15)

    # keypad straight on at about 20 px/mm (legend legibility; the overlay labels are 1.2-2.8 mm)
    def keypad(p):
        p.camera_position = [(0, -39, 400), (0, -39, 0), (0, 1, 0)]
        p.enable_parallel_projection()
        p.camera.parallel_scale = 56
    shot("keypad_top.png", asm, keypad, size=(1800, 2240))

    def back(p):
        p.camera_position = [(-90, -170, -330), (0, -5, 0), (0, 0.8, -0.2)]
    shot("assembled_back.png", asm, back, size=(1200, 1700), zoom=1.15)

    ov.preview(os.path.join(REN, "overlay.png"), px_per_mm=10)
    print("wrote overlay.png")

if __name__ == "__main__":
    main()
