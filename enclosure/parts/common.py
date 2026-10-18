"""Shared helpers. All solids use the assembled-device frame described in params.py."""

from build123d import (
    Box, Circle, Plane, Polygon, Pos, Rectangle, RectangleRounded, Rot, extrude, mirror,
)

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
    return Polygon(*points, align=None)


__all__ = [
    "EPS", "rrect", "prism", "cyl", "box", "y_prism", "x_prism", "for_print_face_down",
    "polygon", "Box", "Pos", "Rot",
]
