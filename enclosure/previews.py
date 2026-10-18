"""Render PNG previews of the enclosure into out/previews/ (off-screen, with PyVista)."""

from pathlib import Path

import numpy as np
import pyvista as pv
from build123d import Plane, Pos, Keep, split

import params as P
from parts import refs, stand

OUT = Path(__file__).resolve().parent / "out" / "previews"
SIZE = (1400, 1050)

COLORS = {
    "shell": "#5d6878",
    "cover": "#97a2b2",
    "stand": "#8a6d4b",
    "glass": "#0b1622",
    "pcb": "#2f7d4a",
    "rear": "#5aa36f",
    "pad": "#e8c547",
    "pack": "#4a78c2",
    "screws": "#c0c0c0",
}


def mesh(shape, tol=0.05):
    verts, tris = shape.tessellate(tol, 0.2)
    pts = np.array([(v.X, v.Y, v.Z) for v in verts])
    faces = np.hstack([[3, *t] for t in tris]).astype(np.int64)
    return pv.PolyData(pts, faces)


def plotter():
    p = pv.Plotter(off_screen=True, window_size=SIZE)
    p.set_background("white")
    p.enable_anti_aliasing("ssaa")
    return p


def add(p, shape, color, opacity=1.0, edges=False):
    p.add_mesh(mesh(shape), color=color, opacity=opacity, smooth_shading=False,
               show_edges=edges, edge_color="#222222", specular=0.2)


def title(p, text):
    p.add_text(text, position="upper_left", font_size=12, color="#222222")


def shoot(p, name, position, focal=(0, 0, -P.DEPTH / 2), up=(0, 1, 0), zoom=1.0):
    p.camera_position = [position, focal, up]
    p.camera.zoom(zoom)
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / f"{name}.png"
    p.screenshot(str(path))
    p.close()
    return path


def board_bodies():
    return [
        (refs.glass(), COLORS["glass"]),
        (refs.pcb(), COLORS["pcb"]),
        (refs.rear_parts(), COLORS["rear"]),
    ]


def iso_front(parts):
    p = plotter()
    add(p, parts["front_shell"], COLORS["shell"])
    add(p, parts["rear_cover"], COLORS["cover"])
    add(p, refs.glass(), COLORS["glass"])
    title(p, f"Assembled, front  {P.OUTER_W:.0f} x {P.OUTER_H:.0f} x {P.DEPTH:.1f} mm")
    return shoot(p, "iso_front", (150, 110, 230), zoom=1.15)


def iso_back(parts):
    p = plotter()
    add(p, parts["front_shell"], COLORS["shell"])
    add(p, parts["rear_cover"], COLORS["cover"])
    title(p, "Assembled, back: grille, switch slot, LED pipe, RESET/BOOT holes")
    return shoot(p, "iso_back", (-150, 110, -260), zoom=1.15)


def exploded(parts):
    p = plotter()
    step = 28.0
    add(p, parts["front_shell"], COLORS["shell"])
    for body, color in board_bodies():
        add(p, Pos(0, 0, -step) * body, color)
    add(p, Pos(0, 0, -2 * step) * refs.pad(compressed=False), COLORS["pad"])
    add(p, Pos(0, 0, -3 * step) * refs.pack(installed=False), COLORS["pack"], opacity=0.85)
    add(p, Pos(0, 0, -4.4 * step) * parts["rear_cover"], COLORS["cover"])
    add(p, Pos(0, 0, -5.4 * step) * refs.screws(), COLORS["screws"])
    title(p, "Exploded: shell, board, pad, pack, cover, 4x M3x30")
    return shoot(p, "exploded", (-330, 170, -40), focal=(0, 0, -2.6 * step), zoom=1.0)


def section(parts, x, name, label):
    p = plotter()
    plane = Plane.YZ.offset(x)
    bodies = [
        (parts["front_shell"], COLORS["shell"]),
        (parts["rear_cover"], COLORS["cover"]),
        *board_bodies(),
        (refs.pad(), COLORS["pad"]),
        (refs.pack(), COLORS["pack"]),
    ]
    if abs(x) > 1:
        bodies.append((refs.screws(), COLORS["screws"]))
    for body, color in bodies:
        try:
            half = split(body, bisect_by=plane, keep=Keep.BOTTOM)
        except Exception:
            continue
        if half is not None and half.volume > 1e-6:
            add(p, half, color)
    title(p, label)
    return shoot(p, name, (x + 260, 0, -P.DEPTH / 2), focal=(x, 0, -P.DEPTH / 2), zoom=1.15)


def on_stand(parts):
    p = plotter()
    loc = stand.device_location()
    add(p, parts["stand"], COLORS["stand"])
    add(p, loc * parts["front_shell"], COLORS["shell"])
    add(p, loc * parts["rear_cover"], COLORS["cover"])
    add(p, loc * refs.glass(), COLORS["glass"])
    add(p, loc * refs.usb_plug(), "#222222")
    title(p, f"On the stand: {P.STAND_ANGLE:.0f} deg, bottom lifted {P.STAND_LIFT:.0f} mm")
    return shoot(p, "on_stand", (-260, -200, 120), focal=(0, 10, 40), up=(0, 0, 1))


def coupon(parts):
    p = plotter()
    add(p, parts["fit_coupon"], COLORS["shell"])
    title(p, "Fit coupon (from the back): pocket, bands, insert holes")
    return shoot(p, "fit_coupon", (-120, 90, -200), focal=(0, 0, -3), zoom=1.2)


def render_all(parts):
    return [
        iso_front(parts),
        iso_back(parts),
        exploded(parts),
        section(parts, 0.0, "section_center", "Section X=0: glass flush, bands, PCB, pad, pack, ribs, grille, USB-C"),
        section(parts, P.HOLES[-1][0], "section_screw",
                "Section through the right-hand screws: standoff clamps the PCB onto the insert"),
        on_stand(parts),
        coupon(parts),
    ]
