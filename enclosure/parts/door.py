"""Button door: a flush door in the rear cover over BOOT and RESET, printed in place.

The door hinges on two 45-degree cone pins that stick out of the well's side walls into
sockets in barrels at each end of the door's top edge. Both cones and sockets print
without supports, and the door can't come off once printed. The door swings outward
past vertical (DOOR_OPEN_MIN) so it stays open when the device lies face down.

The bottom (free) edge is a strip that flexes in its own plane: a through-slot behind it
lets it bow. Two tabs on the strip carry ridges that click into grooves in the well's
bottom wall. A fingernail notch in the cover and a V-groove in the door edge pull it open.

BOOT and RESET labels stand proud of the door's inner face in the second (white)
filament. Each has a triangle at its button's exact X that points at the plunger.
"""

import math

from build123d import Location, Pos, Rot, extrude

import params as P
from parts.common import (
    EPS, box, on_plane, polygon, prism, rrect, text_sketch, x_cone, x_cyl, x_prism,
)

HW = P.DOOR_HW
Y0 = P.DOOR_Y0
YA, DA = P.HINGE_Y, P.HINGE_DEPTH
IN = P.DOOR_INNER           # door's inner face (DOOR_T thick)
OUT = P.DEPTH               # door's outer (back) face


def plate():
    y1 = YA + P.HINGE_RHO + 0.5
    body = prism(rrect(2 * HW, y1 - Y0, P.DOOR_CORNER_R), IN, OUT, 0, (Y0 + y1) / 2)
    # Hinge end: everything past the axis line stays within HINGE_RHO of the axis.
    keep = box(-HW - 1, HW + 1, Y0 - 1, YA, IN - 1, OUT + 1)
    keep += x_cyl(P.HINGE_RHO, YA, DA, -HW - 1, HW + 1)
    return body & keep


def barrels():
    solid = None
    for s in (-1, 1):
        x0, x1 = sorted((s * (HW - P.HINGE_BARREL_L), s * HW))
        b = x_cyl(P.HINGE_BARREL_R, YA, DA, x0, x1)
        solid = b if solid is None else solid + b
    return solid


def sockets():
    """Cone sockets: the cover's cone pins offset by HINGE_GAP (normal to the 45° face)."""
    g = P.HINGE_GAP
    solid = None
    for s in (-1, 1):
        wall = P.DOOR_OPEN_HW
        tip_x = wall - P.HINGE_CONE_H - g                 # socket bottom
        tip_r = P.HINGE_CONE_R - P.HINGE_CONE_H - g + g * math.sqrt(2)
        out_x = HW + 0.5
        out_r = tip_r + (out_x - tip_x)                   # 45 degrees
        c = x_cone(s * out_x, out_r, s * tip_x, tip_r, YA, DA)
        solid = c if solid is None else solid + c
    return solid


def latch_slot():
    y = Y0 + P.LATCH_STRIP_W + P.LATCH_SLOT_W / 2
    return prism(rrect(2 * P.LATCH_SLOT_HL, P.LATCH_SLOT_W, P.LATCH_SLOT_W / 2 - 1e-3),
                 IN - EPS, OUT + EPS, 0, y)


def _tab_ranges():
    a, b = P.LATCH_TAB_X
    return [(-b, -a), (a, b)]


def latch_tabs():
    solid = None
    for x0, x1 in _tab_ranges():
        t = box(x0, x1, Y0, Y0 + P.LATCH_TAB_T, IN - P.LATCH_TAB_DROP, IN + EPS)
        solid = t if solid is None else solid + t
    return solid


def ridge_profile(grow=0.0):
    """Triangle (Y, depth) of a latch ridge on the free edge; grow offsets it outward."""
    p = P.LATCH_RIDGE + grow * math.sqrt(2)
    dc = P.LATCH_RIDGE_D
    return [(Y0 + 0.6, dc - p - 0.6), (Y0 - p, dc), (Y0 + 0.6, dc + p + 0.6)]


def latch_ridges():
    solid = None
    for x0, x1 in _tab_ranges():
        r = x_prism(polygon(ridge_profile()), x0, x1)
        solid = r if solid is None else solid + r
    # Keep the ridge to the strip and tab (trim the bits of triangle behind the face).
    return solid & box(-HW, HW, Y0 - P.LATCH_RIDGE - 0.1, Y0 + EPS,
                       IN - P.LATCH_TAB_DROP, OUT)


def nail_groove():
    d0 = OUT - 0.4
    g = P.NAIL_GROOVE
    sk = polygon([(Y0 - 0.2, d0 + 0.2), (Y0 + g, d0 - g), (Y0 - 0.2, d0 - 2 * g - 0.2)])
    return x_prism(sk, -P.NAIL_HW, P.NAIL_HW)


def build(latch=True):
    """The door in its closed position, in the assembled-device frame."""
    door = plate() + barrels() + latch_tabs()
    if latch:
        door += latch_ridges()
    door -= sockets()
    door -= latch_slot()
    door -= nail_groove()
    return door


# --- White labels ---------------------------------------------------------------------

def label_x(name, bx):
    """Centre each label on its button, nudged inward just enough to fit on the door."""
    w = text_sketch(name, P.LABEL_SIZE).bounding_box().size.X
    lim = HW - P.LABEL_EDGE - w / 2
    return max(-lim, min(lim, bx)), w


def build_text():
    """BOOT / RESET and their pointers, standing LABEL_RELIEF proud of the inner face.

    The text reads correctly from inside the case, with its top toward the free edge.
    Opened, the door stands on its hinge with the labels upright, facing the plungers.
    """
    solid = None
    for bx, by, name in P.PLUNGERS:
        cx, _ = label_x(name, bx)
        sk = on_plane(text_sketch(name, P.LABEL_SIZE), (cx, P.LABEL_Y, -IN), (-1, 0, 0), (0, 0, 1))
        t = extrude(sk, amount=P.LABEL_RELIEF)
        w, h = P.POINTER_W / 2, P.POINTER_H
        tri = polygon([(bx - w, P.POINTER_Y), (bx + w, P.POINTER_Y), (bx, P.POINTER_Y + h)])
        t += prism(tri, IN - P.LABEL_RELIEF, IN)
        solid = t if solid is None else solid + t
    return solid


# --- Hinge motion ---------------------------------------------------------------------

def open_location(angle):
    """Rotate the door outward by angle (degrees) about the hinge axis."""
    return Pos(0, YA, -DA) * Rot(angle, 0, 0) * Pos(0, -YA, DA)


__all__ = ["build", "build_text", "open_location", "label_x", "ridge_profile", "Location"]
