"""Wafer: a thin printed plate between the board's rear parts and the battery pack.

It replaces the stick-on insulating pad. The standoffs pass through holes in its four
ears, so the cover locates it; L-shaped fences on its back face hold the pack's corners.
It stops short of the plungers at the bottom end and leaves the side cable runs clear.
Print it flat, board side down, fences up.
"""

import params as P
from parts.common import box, cyl, prism, rrect

D0 = P.PAD_FRONT                 # board side
D1 = P.PAD_FRONT + P.WAFER_T     # pack side (= PACK_FRONT)


def outline():
    fx = P.PACK_W / 2 + P.FENCE_GAP + P.FENCE_T
    body = prism(rrect(2 * fx, 2 * P.WAFER_BODY_HL, P.WAFER_R), D0, D1)
    ya, yb = P.WAFER_EAR_Y
    # Top: one bar across the full width (nothing in the way at that end).
    body += prism(rrect(2 * P.WAFER_EAR_X, yb - ya, P.WAFER_R), D0, D1, 0, (ya + yb) / 2)
    # Bottom: two ears that clear the plungers in the middle.
    for s in (-1, 1):
        x0, x1 = P.WAFER_EAR_X0, P.WAFER_EAR_X
        body += prism(rrect(x1 - x0, yb - ya, P.WAFER_R), D0, D1, s * (x0 + x1) / 2, -(ya + yb) / 2)
    for x, y in P.HOLES:
        body -= cyl(P.WAFER_HOLE_D, D0 - 0.1, D1 + 0.1, x, y)
    return body


def fences():
    """L-shaped corner fences round the pack footprint."""
    g, t, L, h = P.FENCE_GAP, P.FENCE_T, P.FENCE_L, P.FENCE_H
    hw, hl, cy = P.PACK_W / 2, P.PACK_L / 2, P.PACK_CY
    solid = None
    for sx in (-1, 1):
        for sy in (-1, 1):
            x_in, x_out = sx * (hw + g), sx * (hw + g + t)
            y_in, y_out = cy + sy * (hl + g), cy + sy * (hl + g + t)
            along_y = box(*sorted((x_in, x_out)), *sorted((y_out, y_out - sy * (L + t))), D1 - 0.01, D1 + h)
            along_x = box(*sorted((x_out, x_out - sx * (L + t))), *sorted((y_in, y_out)), D1 - 0.01, D1 + h)
            part = along_y + along_x
            solid = part if solid is None else solid + part
    return solid


def build():
    return outline() + fences()
