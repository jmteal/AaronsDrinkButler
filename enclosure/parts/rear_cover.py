"""Rear cover: back plate, locating lip, clamp standoffs, pack ribs, grille, switch, LED pipe,
and the button door's well.

The cover snaps on. Its four standoffs bear only on the PCB's Ø5.6 pad rings and hold
the board on the front bands; a pocket in each standoff's foot takes the tip of the
shell's locating pin. Ribs press the pack against the
1 mm printed wafer; the speaker fires through the grille.

The door (parts/door.py) prints in place in an opening over BOOT and RESET. Behind it a
solid block inside the cover holds a shallow well, the hinge's cone pins, the latch
grooves and two plunger guide tubes that reach down to the buttons.
"""

import math

from build123d import Axis, RegularPolygon, chamfer

import params as P
from parts import door, snaps
from parts.common import EPS, box, cyl, polygon, prism, rrect, x_cone, x_cyl, x_prism

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


def pin_pockets():
    solid = None
    depth = P.LOCATE_PIN_PROUD + P.PIN_POCKET_CLEAR
    for x, y in P.HOLES:
        s = cyl(P.PIN_POCKET_D, P.PCB_BACK - EPS, P.PCB_BACK + depth, x, y)
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


# --- Button door well -------------------------------------------------------------------

def bay():
    """Solid block inside the cover round the door well (it merges with the lip)."""
    y0 = -(P.INNER_H / 2 - P.COVER_LIP_GAP)
    return box(-P.BAY_HW, P.BAY_HW, y0, P.BAY_Y1, P.FLOOR_BOTTOM, PLATE_FRONT + EPS)


def plunger_tubes():
    solid = None
    for x, y, _ in P.PLUNGERS:
        t = cyl(P.PLUNGER_TUBE_OD, P.PLUNGER_TUBE_END, P.FLOOR_BOTTOM + EPS, x, y)
        solid = t if solid is None else solid + t
    return solid


def well_cut():
    """Door opening plus the well behind it, the hinge clearance and its edge chamfer."""
    hw, y0, ya = P.DOOR_OPEN_HW, P.DOOR_OPEN_Y0, P.HINGE_Y
    y1 = ya + 1.0                       # past the axis; the clearance cylinder takes over
    cut = prism(rrect(2 * hw, y1 - y0, P.DOOR_CORNER_R + P.DOOR_GAP),
                P.FLOOR_TOP, PLATE_BACK + EPS, 0, (y0 + y1) / 2)
    cut += x_cyl(P.HINGE_CLEAR_R, ya, P.HINGE_DEPTH, -hw, hw)
    # 45-degree chamfer on the cover edge past the hinge lets the door swing further.
    yc, c = ya + P.HINGE_EDGE_V, P.HINGE_CHAMFER
    sk = polygon([(yc - 0.5, PLATE_BACK + 1), (yc + c + 1, PLATE_BACK + 1),
                  (yc - 0.5, PLATE_BACK - c - 0.5)])
    cut += x_prism(sk, -hw, hw)
    return cut


def latch_grooves():
    a, b = P.LATCH_TAB_X
    g = P.LATCH_GAP
    sk = polygon(door.ridge_profile(grow=g))
    return x_prism(sk, -b - g, -a + g) + x_prism(sk, a - g, b + g)


def nail_notch():
    """45-degree scoop in the cover at the middle of the door's free edge."""
    y0, d = P.DOOR_OPEN_Y0, P.NAIL_D
    sk = polygon([(y0 + 0.5, PLATE_BACK + 1), (y0 - d - 1, PLATE_BACK + 1),
                  (y0 + 0.5, PLATE_BACK - d - 0.5)])
    return x_prism(sk, -P.NAIL_HW, P.NAIL_HW)


def plunger_bores():
    solid = None
    for x, y, _ in P.PLUNGERS:
        h = cyl(P.PLUNGER_BORE_D, P.PLUNGER_TUBE_END - EPS, P.FLOOR_TOP + EPS, x, y)
        solid = h if solid is None else solid + h
    return solid


def hinge_pins():
    """45-degree cone pins on the well's side walls, pointing in at the door's sockets."""
    wall = P.DOOR_OPEN_HW
    r0, h = P.HINGE_CONE_R, P.HINGE_CONE_H
    solid = None
    for s in (-1, 1):
        base_x = wall + 0.3                        # rooted 0.3 inside the wall
        c = x_cone(s * base_x, r0 + 0.3, s * (wall - h), r0 - h, P.HINGE_Y, P.HINGE_DEPTH)
        solid = c if solid is None else solid + c
    return solid


def build():
    cover = plate() + (lip() - snaps.lip_slots()) + standoffs() + ribs() + switch_cradle() + led_tube()
    cover += bay() + plunger_tubes() + snaps.arms()
    cover -= pin_pockets()
    cover -= grille_cut()
    cover -= switch_slot()
    cover -= led_hole()
    cover -= well_cut()
    cover -= latch_grooves()
    cover -= nail_notch()
    cover -= plunger_bores()
    cover += hinge_pins()
    return cover
