"""Shared helpers. All solids use the assembled-device frame described in params.py."""

from pathlib import Path

from build123d import (
    Align, Box, Circle, Cone, Plane, Polygon, Pos, Rectangle, RectangleRounded, Rot, Text,
    extrude, mirror,
)

ROOT = Path(__file__).resolve().parent.parent

EPS = 0.02  # overlap for cutting tools so no faces end up coplanar


def rrect(w, h, r):
    """Rounded rectangle sketch centred on the origin (plain rectangle when r is ~0)."""
    r = min(r, w / 2 - 1e-3, h / 2 - 1e-3)
    return RectangleRounded(w, h, r) if r > 1e-3 else Rectangle(w, h)


def prism(sketch, d_front, d_back, x=0.0, y=0.0):
    """Extrude an XY sketch so it spans depths d_front..d_back behind the front face."""
    return Pos(x, y, -d_back) * extrude(sketch, amount=d_back - d_front)


def cyl(d, d_front, d_back, x=0.0, y=0.0):
    return prism(Circle(d / 2), d_front, d_back, x, y)


def box(x0, x1, y0, y1, d_front, d_back):
    return prism(Rectangle(x1 - x0, y1 - y0), d_front, d_back, (x0 + x1) / 2, (y0 + y1) / 2)


def y_prism(sketch_xz, y0, y1):
    """Extrude a sketch drawn in (X, depth) coordinates along Y from y0 to y1.

    The sketch's local x is case X and its local y is *depth* (positive = behind the face).
    """
    solid = extrude(Plane.XZ * sketch_xz, amount=(y1 - y0) / 2, both=True)
    # Plane.XZ maps local y to +Z; flip so local y means depth (-Z).
    return Pos(0, (y0 + y1) / 2, 0) * mirror_z(solid)


def x_prism(sketch_yz, x0, x1):
    """Extrude a sketch drawn in (Y, depth) coordinates along X from x0 to x1."""
    solid = extrude(Plane.YZ * sketch_yz, amount=(x1 - x0) / 2, both=True)
    return Pos((x0 + x1) / 2, 0, 0) * mirror_z(solid)


def mirror_z(solid):
    return mirror(solid, about=Plane.XY)


def for_print_face_down(solid):
    """Front shell / coupon print orientation: front face on the plate (Z = 0), rim up."""
    return Rot(180, 0, 0) * solid


def polygon(points):
    """Polygon sketch, always wound counter-clockwise so prism() extrudes the right way."""
    pts = list(points)
    area = sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]))
    if area < 0:
        pts.reverse()
    return Polygon(*pts, align=None)


def x_cyl(r, y, depth, x0, x1):
    """Cylinder along X from x0 to x1, its axis at (y, depth)."""
    return x_prism(Pos(y, depth) * Circle(r), x0, x1)


def x_cone(x0, r0, x1, r1, y, depth):
    """Truncated cone along X: radius r0 at x0, r1 at x1, axis at (y, depth)."""
    cone = Cone(r0, r1, abs(x1 - x0), align=(Align.CENTER, Align.CENTER, Align.MIN))
    turn = Rot(0, 90, 0) if x1 > x0 else Rot(0, -90, 0)    # +Z onto +X or -X
    return Pos(x0, y, -depth) * turn * cone


def text_sketch(txt, size):
    """Text centred on the origin in the bundled font (fonts/Inter-Bold.otf)."""
    import params as P
    return Text(txt, font_size=size, font_path=str(ROOT / P.FONT),
                align=(Align.CENTER, Align.CENTER))


def on_plane(sketch, origin, x_dir, normal):
    return Plane(origin=origin, x_dir=x_dir, z_dir=normal) * sketch


__all__ = [
    "EPS", "rrect", "prism", "cyl", "box", "y_prism", "x_prism", "for_print_face_down",
    "polygon", "x_cyl", "x_cone", "text_sketch", "on_plane", "Box", "Pos", "Rot",
]
