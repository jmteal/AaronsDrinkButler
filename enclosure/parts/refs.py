"""Simple stand-in bodies for the parts that go inside, used by validation and previews."""

from build123d import Box, Pos

import params as P
from parts.common import box, cyl, prism, rrect, y_prism


def glass():
    """Cover glass + LCD + glue: one block from the front face to the PCB front."""
    return prism(rrect(P.GLASS_W, P.GLASS_H, P.GLASS_CORNER_R), -P.GLASS_FLUSH, P.PCB_FRONT, 0, P.GLASS_Y)


def pcb():
    return prism(rrect(P.PCB_W, P.PCB_H, P.PCB_R), P.PCB_FRONT, P.PCB_BACK)


def rear_parts():
    """Envelope of the rear components: the whole PCB outline, minus the pad keep-outs.

    Over each button the envelope stops at the button top (BTN_TOP), leaving a
    BTN_KEEP_D clear column for the plunger tip.
    """
    env = prism(rrect(P.PCB_W, P.PCB_H, P.PCB_R), P.PCB_BACK, P.REAR_ENVELOPE)
    for x, y in P.HOLES:
        env -= cyl(P.PAD_D + 0.2, P.PCB_BACK - 0.1, P.REAR_ENVELOPE + 0.1, x, y)
    for x, y, _ in P.PLUNGERS:
        env -= cyl(P.BTN_KEEP_D, P.BTN_TOP, P.REAR_ENVELOPE + 0.1, x, y)
    return env


def button_tops():
    """Thin discs on each button's actuator, for the plunger contact check."""
    solid = None
    for x, y, _ in P.PLUNGERS:
        b = cyl(P.PLUNGER_TIP_D + 0.4, P.BTN_TOP - 0.2, P.BTN_TOP, x, y)
        solid = b if solid is None else solid + b
    return solid


def board():
    return glass() + pcb() + rear_parts()


def pack():
    """The battery + speaker pack, sitting on the wafer.

    The wafer is rigid, so the ribs' RIB_PRELOAD is taken by the cover plate flexing
    outward; validation checks the pack against the cover moved back by that much.
    """
    return box(-P.PACK_W / 2, P.PACK_W / 2, P.PACK_CY - P.PACK_L / 2, P.PACK_CY + P.PACK_L / 2,
               P.PACK_FRONT, P.PACK_BACK)


def usb_plug():
    """Cable overmold pushed fully home (its face at the PCB edge), sticking out below."""
    sk = Pos(P.USB_X, P.USB_DEPTH) * rrect(P.USB_PLUG_W, P.USB_PLUG_H, P.USB_OPEN_R)
    return y_prism(sk, -P.PCB_H / 2 - P.USB_PLUG_L, -P.PCB_H / 2)


def sd_card_path():
    """Card-sized path from the socket out through the right wall (11 x 1 mm section)."""
    return box(P.PCB_W / 2, P.OUTER_W / 2 + 2, P.SD_Y - 5.5, P.SD_Y + 5.5,
               P.SD_DEPTH - 0.5, P.SD_DEPTH + 0.5)


def cable_channels():
    """Free space each side of the pack for the battery and speaker leads to the sockets."""
    # Below -36 the door block takes the space and above 36 the upper snap arms do;
    # the sockets are at Y -9 and -24.
    y0, y1 = -36.0, 36.0
    d0, d1 = P.REAR_ENVELOPE + 0.5, P.COVER_INNER - 3.0
    left = box(-P.INNER_W / 2 + 0.4, -P.INNER_W / 2 + 4.4, y0, y1, d0, d1)
    x_fence = P.PACK_W / 2 + P.FENCE_GAP + P.FENCE_T     # outside the wafer's pack fences
    right = box(x_fence + 0.4, P.INNER_W / 2 - 0.4, y0, y1, d0, d1)
    return left, right


def screws():
    """M3 x 30 button heads seated in the counterbores."""
    solid = None
    seat = P.DEPTH - P.CBORE_DEPTH
    for x, y in P.HOLES:
        s = cyl(3.0, seat - P.SCREW_L, seat, x, y) + cyl(P.SCREW_HEAD_D, seat, seat + P.SCREW_HEAD_H, x, y)
        solid = s if solid is None else solid + s
    return solid


__all__ = ["board", "glass", "pcb", "rear_parts", "button_tops", "pack", "usb_plug",
           "sd_card_path", "cable_channels", "screws", "Box", "Pos"]
