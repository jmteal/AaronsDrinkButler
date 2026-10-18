"""Front shell: flush glass window, board pocket with locating bands, side walls, ports.

The glass is exactly as wide as the PCB, so nothing behind the glass edge can hold the
board. Instead the PCB front face bears on two solid bands over the 9.25 mm strips above
and below the glass, PCB_FRONT behind the case face. The four M3 inserts sit in those
bands; the rear cover's standoffs clamp the PCB onto them.
"""

from build123d import Axis, Circle, Pos, chamfer, loft, Plane

import params as P
from parts.common import EPS, box, cyl, polygon, prism, rrect, x_prism, y_prism


def outer_body(depth):
    body = prism(rrect(P.OUTER_W, P.OUTER_H, P.OUTER_R), 0, depth)
    front_edges = body.edges().group_by(Axis.Z)[-1]
    return chamfer(front_edges, P.EDGE_CHAMFER)


def window_cut():
    w = P.GLASS_W + 2 * P.GLASS_GAP
    h = P.GLASS_H + 2 * P.GLASS_GAP
    r = P.GLASS_CORNER_R + P.GLASS_GAP
    straight = prism(rrect(w, h, r), 0, P.PCB_FRONT + EPS, 0, P.GLASS_Y)
    # 45-degree chamfer on the window edge at the front face (hides the gap, helps the
    # elephant-foot edge when printed face down).
    c = P.WINDOW_CHAMFER
    flare = loft([
        Plane.XY.offset(-c) * Pos(0, P.GLASS_Y) * rrect(w, h, r),
        Plane.XY.offset(EPS) * Pos(0, P.GLASS_Y) * rrect(w + 2 * (c + EPS), h + 2 * (c + EPS), r + c + EPS),
    ])
    return straight + flare


def pocket_cut(depth):
    pocket = prism(
        rrect(P.PCB_W + 2 * P.BOARD_GAP, P.PCB_H + 2 * P.BOARD_GAP, P.PCB_R + P.BOARD_GAP),
        P.PCB_FRONT, P.POCKET_BACK + EPS,
    )
    interior = prism(rrect(P.INNER_W, P.INNER_H, P.INNER_R), P.POCKET_BACK, depth + EPS)
    return pocket + interior


def insert_holes():
    holes = None
    for x, y in P.HOLES:
        h = cyl(P.INSERT_HOLE_D, P.PCB_FRONT - P.INSERT_L, P.PCB_FRONT + EPS, x, y)
        holes = h if holes is None else holes + h
    return holes


def front_relief():
    """Shallow relief in the bottom band for the small front-side parts beside the USB."""
    _, y_top = P.board_xy(0, P.FRONT_RELIEF_BY)
    y_glass = P.GLASS_Y - P.GLASS_H / 2 - P.GLASS_GAP
    y_top = min(y_top, y_glass - 0.5)
    return box(
        -P.FRONT_RELIEF_HALF_W, P.FRONT_RELIEF_HALF_W,
        -P.PCB_H / 2 - P.BOARD_GAP - EPS, y_top,
        P.PCB_FRONT - P.FRONT_RELIEF_DEPTH, P.PCB_FRONT + EPS,
    )


def usb_cut():
    """USB-C opening through the bottom wall, sized for the cable overmold."""
    sk = Pos(P.USB_X, P.USB_DEPTH) * rrect(P.USB_OPEN_W, P.USB_OPEN_H, P.USB_OPEN_R)
    return y_prism(sk, -P.OUTER_H / 2 - 1, -P.PCB_H / 2 + 0.25)


def sd_cut():
    """microSD slot through the right wall plus a 45-degree scallop for a fingernail."""
    slot_sk = Pos(P.SD_Y, P.SD_DEPTH) * rrect(P.SD_OPEN_L, P.SD_OPEN_H, 0.5)
    slot = x_prism(slot_sk, P.PCB_W / 2 - 0.5, P.OUTER_W / 2 + 1)

    # Trapezoid in (X, depth): narrow (slot height) at the groove bottom, widening 45 degrees
    # to the outer face, so both flanks print without supports.
    d = P.SD_SCALLOP_DEPTH
    x_in = P.OUTER_W / 2 - d
    x_out = P.OUTER_W / 2 + 1
    hh = P.SD_OPEN_H / 2
    grow = x_out - x_in
    sk = polygon([
        (x_in, P.SD_DEPTH - hh), (x_out, P.SD_DEPTH - hh - grow),
        (x_out, P.SD_DEPTH + hh + grow), (x_in, P.SD_DEPTH + hh),
    ])
    groove = y_prism(sk, P.SD_Y - P.SD_SCALLOP_L / 2, P.SD_Y + P.SD_SCALLOP_L / 2)
    return slot + groove


def build():
    shell = outer_body(P.SHELL_DEPTH)
    shell -= window_cut()
    shell -= pocket_cut(P.SHELL_DEPTH)
    shell -= insert_holes()
    shell -= front_relief()
    shell -= usb_cut()
    shell -= sd_cut()
    if P.MIC_HOLE:
        shell -= cyl(P.MIC_HOLE_D, -EPS, P.PCB_FRONT + EPS, P.MIC_X, P.MIC_Y)
    return shell
