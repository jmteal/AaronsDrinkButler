"""Rear cover: back plate, locating lip, clamp standoffs, pack ribs, grille, switch, LED pipe.

Four M3 x 30 button-head screws pass through the cover's standoffs and the PCB holes into
the inserts in the front bands, so one set of screws clamps the board and holds the
cover. The standoffs bear only on the Ø5.6 pad rings. Ribs press the pack against the
1 mm pad; the speaker fires through the grille.
"""

import math

from build123d import Axis, RegularPolygon, chamfer

import params as P
from parts.common import EPS, box, cyl, prism, rrect

PLATE_FRONT = P.COVER_INNER        # inner face of the plate
PLATE_BACK = P.DEPTH               # outer face


def plate():
    body = prism(rrect(P.OUTER_W, P.OUTER_H, P.OUTER_R), PLATE_FRONT, PLATE_BACK)
    back_edges = body.edges().group_by(Axis.Z)[0]
    return chamfer(back_edges, P.EDGE_CHAMFER)


def lip():
    outer_w = P.INNER_W - 2 * P.COVER_LIP_GAP
    outer_h = P.INNER_H - 2 * P.COVER_LIP_GAP
    r = max(P.INNER_R - P.COVER_LIP_GAP, 0.5)
    d0, d1 = PLATE_FRONT - P.COVER_LIP_H, PLATE_FRONT + EPS
    ring = prism(rrect(outer_w, outer_h, r), d0, d1)
    t = P.COVER_LIP_T
    ring -= prism(rrect(outer_w - 2 * t, outer_h - 2 * t, max(r - t, 0.3)), d0 - EPS, d1 + EPS)
    return ring


def standoffs():
    solid = None
    for x, y in P.HOLES:
        s = cyl(P.STANDOFF_OD, P.PCB_BACK, PLATE_FRONT + EPS, x, y)
        s += cyl(P.STANDOFF_BASE_OD, PLATE_FRONT - P.STANDOFF_BASE_H, PLATE_FRONT + EPS, x, y)
        solid = s if solid is None else solid + s
    return solid


def screw_bores():
    solid = None
    for x, y in P.HOLES:
        s = cyl(P.M3_CLEAR_D, P.PCB_BACK - EPS, PLATE_BACK + EPS, x, y)
        s += cyl(P.CBORE_D, PLATE_BACK - P.CBORE_DEPTH, PLATE_BACK + EPS, x, y)
        solid = s if solid is None else solid + s
    return solid


def ribs():
    """Bars on the pack's back face, kept outside the grille."""
    d0, d1 = PLATE_FRONT - P.RIB_H, PLATE_FRONT + EPS
    hw, hl = P.PACK_W / 2, P.PACK_L / 2
    cy = P.PACK_CY
    t = 1.6
    bars = [
        box(-hw + 2.5, hw - 2.5, cy + hl - 6 - t / 2, cy + hl - 6 + t / 2, d0, d1),
        box(-hw + 2.5, hw - 2.5, cy - hl + 6 - t / 2, cy - hl + 6 + t / 2, d0, d1),
        box(-hw + 3 - t / 2, -hw + 3 + t / 2, cy - hl + 12, cy + hl - 12, d0, d1),
        box(hw - 3 - t / 2, hw - 3 + t / 2, cy - hl + 12, cy + hl - 12, d0, d1),
    ]
    solid = bars[0]
    for b in bars[1:]:
        solid += b
    # Keep a ring of clear face around the grille for a foam gasket.
    solid -= cyl(P.GRILLE_D + 4, d0 - EPS, d1 + EPS, P.GRILLE_X, P.GRILLE_Y)
    return solid


def grille_centres():
    pts = []
    r_max = P.GRILLE_D / 2 - P.GRILLE_HOLE_AF / 2
    p = P.GRILLE_PITCH
    rows = int(P.GRILLE_D / (p * math.sqrt(3) / 2)) + 2
    for j in range(-rows, rows + 1):
        y = j * p * math.sqrt(3) / 2
        offset = (p / 2) if j % 2 else 0.0
        for i in range(-rows, rows + 1):
            x = i * p + offset
            if math.hypot(x, y) <= r_max:
                pts.append((P.GRILLE_X + x, P.GRILLE_Y + y))
    return pts


def grille_cut():
    r_corner = P.GRILLE_HOLE_AF / math.sqrt(3)  # hexagon circumradius from across-flats
    solid = None
    for x, y in grille_centres():
        h = prism(RegularPolygon(r_corner, 6), PLATE_FRONT - EPS, PLATE_BACK + EPS, x, y)
        solid = h if solid is None else solid + h
    return solid


def switch_cradle():
    gap, wall = P.SW_CRADLE_GAP, P.SW_CRADLE_WALL
    iw, ih = P.SW_BODY_L + 2 * gap, P.SW_BODY_W + 2 * gap
    d0, d1 = PLATE_FRONT - P.SW_CRADLE_H, PLATE_FRONT + EPS
    rim = prism(rrect(iw + 2 * wall, ih + 2 * wall, 0.6), d0, d1, P.SW_X, P.SW_Y)
    rim -= prism(rrect(iw, ih, 0.2), d0 - EPS, d1 + EPS, P.SW_X, P.SW_Y)
    return rim


def switch_slot():
    return prism(rrect(P.SW_SLOT_L, P.SW_SLOT_W, 0.5), PLATE_FRONT - EPS, PLATE_BACK + EPS, P.SW_X, P.SW_Y)


def led_tube():
    return cyl(P.LED_TUBE_OD, PLATE_FRONT - P.LED_TUBE_L, PLATE_FRONT + EPS, P.LED_X, P.LED_Y)


def led_hole():
    return cyl(P.LED_PIPE_HOLE_D, PLATE_FRONT - P.LED_TUBE_L - EPS, PLATE_BACK + EPS, P.LED_X, P.LED_Y)


def button_tubes():
    end = P.REAR_ENVELOPE + P.BTN_TUBE_CLEAR
    solid = None
    for x in (P.RESET_X, P.BOOT_X):
        t = cyl(P.BTN_TUBE_OD, end, PLATE_FRONT + EPS, x, P.BTN_Y)
        solid = t if solid is None else solid + t
    return solid


def button_holes():
    end = P.REAR_ENVELOPE + P.BTN_TUBE_CLEAR
    solid = None
    for x in (P.RESET_X, P.BOOT_X):
        h = cyl(P.BTN_HOLE_D, end - EPS, PLATE_BACK + EPS, x, P.BTN_Y)
        solid = h if solid is None else solid + h
    return solid


def build():
    cover = plate() + lip() + standoffs() + ribs() + switch_cradle() + led_tube()
    if P.BTN_POKE:
        cover += button_tubes()
    cover -= screw_bores()
    cover -= grille_cut()
    cover -= switch_slot()
    cover -= led_hole()
    if P.BTN_POKE:
        cover -= button_holes()
    return cover
