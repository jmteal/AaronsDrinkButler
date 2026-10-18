"""Detachable desk stand: two wedge cheeks joined by a cross bar, open in the middle.

Stand frame: X across (same as the device), Y toward the back of the counter, Z up from
the counter. The device's rear-bottom edge sits at P0, its back on the backrest at
STAND_ANGLE from horizontal and its bottom face on the ledge. The middle stays open so
the USB-C plug hangs down from the bottom face and the cable runs back along the counter
through the notch in the cross bar. Print it upright, base on the plate.
"""

import math

from build123d import Location, Pos, Rot

import params as P
from parts.common import EPS, polygon, x_prism, y_prism

A = math.radians(P.STAND_ANGLE)
UP = (math.cos(A), math.sin(A))          # along the device's back, toward its top
FWD = (-math.sin(A), math.cos(A))         # perpendicular, toward the device's front face
P0 = (0.0, P.STAND_LIFT)                  # device rear-bottom edge (Y, Z)


def pt(s=0.0, t=0.0):
    """Point s along the backrest (from P0) and t in front of it (negative = behind)."""
    return (P0[0] + s * UP[0] + t * FWD[0], P0[1] + s * UP[1] + t * FWD[1])


def cheek_profile():
    q1 = pt(0, P.STAND_LEDGE_L)                  # front end of the ledge
    q0 = (q1[0], 0.0)
    p2 = pt(P.STAND_BACK_L, 0)                   # top of the backrest
    p3 = pt(P.STAND_BACK_L, -P.STAND_BACK_T)
    p4 = (P.STAND_BASE_BACK, 0.0)
    return [q0, q1, pt(0, 0), p2, p3, p4]


def fence_profile():
    h = P.STAND_FENCE_H
    q1 = pt(0, P.STAND_LEDGE_L)
    return [
        (q1[0], 0.0), q1, pt(h, P.STAND_LEDGE_L), pt(h, h),
        pt(P.STAND_BACK_L, h), pt(P.STAND_BACK_L, -P.STAND_BACK_T), (P.STAND_BASE_BACK, 0.0),
    ]


def bar_profile():
    """Cross bar between the cheeks: from the counter up to STAND_BAR_S along the backrest."""
    top = pt(P.STAND_BAR_S, 0)
    rear = pt(P.STAND_BAR_S, -P.STAND_BACK_T)
    base_front = (P0[0] - P0[1] / math.tan(A), 0.0)   # backrest plane meets the counter
    return [base_front, top, rear, (rear[0], 0.0)]


def notch_profile():
    w, h = P.STAND_NOTCH_W, P.STAND_NOTCH_H
    # (X, Z) in the bar's cross-section: rectangle with a 45-degree pointed top.
    return [(-w / 2, -1), (w / 2, -1), (w / 2, h - w / 2), (0, h), (-w / 2, h - w / 2)]


def _yz_prism(points, x0, x1):
    # x_prism sketches are (Y, depth); the stand works in (Y, Z) with Z up, so negate.
    return x_prism(polygon([(y, -z) for y, z in points]), x0, x1)


def build():
    half = P.OUTER_W / 2 + P.STAND_FIT_GAP
    inner = half - P.STAND_CHEEK_W
    stand = None
    for sign in (-1, 1):
        lo, hi = sorted((sign * inner, sign * half))
        cheek = _yz_prism(cheek_profile(), lo, hi)
        flo, fhi = sorted((sign * (half - EPS), sign * (half + P.STAND_FENCE_T)))
        fence = _yz_prism(fence_profile(), flo, fhi)
        part = cheek + fence
        stand = part if stand is None else stand + part
    stand += _yz_prism(bar_profile(), -inner - EPS, inner + EPS)
    # Cable notch, cut along Y through the bar.
    notch = y_prism(polygon([(x, -z) for x, z in notch_profile()]), -60, P.STAND_BASE_BACK + 10)
    stand -= notch
    return stand


def device_location():
    """Location that moves the assembled device (params.py frame) onto the stand.

    Device +Y (toward its top) maps to UP, device +Z (toward its front) maps to FWD, and
    the device's rear-bottom edge (y = -OUTER_H/2, depth = DEPTH) lands on P0.
    """
    # Rotate about X so device +Y points along UP and +Z along FWD: angle = STAND_ANGLE.
    rot = Rot(P.STAND_ANGLE, 0, 0)
    # After rotation, the rear-bottom edge point (0, -H/2, -D) moves; shift it onto P0.
    y, z = -P.OUTER_H / 2, -P.DEPTH
    c, s = math.cos(A), math.sin(A)
    ry, rz = y * c - z * s, y * s + z * c
    return Pos(0, P0[0] - ry, P0[1] - rz) * rot


__all__ = ["build", "device_location", "Location"]
