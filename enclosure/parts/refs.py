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
    """Envelope of the rear components: the whole PCB outline, minus the pad keep-outs."""
    env = prism(rrect(P.PCB_W, P.PCB_H, P.PCB_R), P.PCB_BACK, P.REAR_ENVELOPE)
    for x, y in P.HOLES:
        env -= cyl(P.PAD_D + 0.2, P.PCB_BACK - 0.1, P.REAR_ENVELOPE + 0.1, x, y)
    return env


def board():
    return glass() + pcb() + rear_parts()


def pad(compressed=True):
    t = P.PAD_T - (P.RIB_PRELOAD if compressed else 0.0)
    return box(-P.PACK_W / 2, P.PACK_W / 2, P.PACK_CY - P.PACK_L / 2, P.PACK_CY + P.PACK_L / 2,
               P.PAD_FRONT, P.PAD_FRONT + t)


def pack(installed=True):
    """The battery + speaker pack. Installed = pushed forward by the rib preload."""
    shift = P.RIB_PRELOAD if installed else 0.0
    return box(-P.PACK_W / 2, P.PACK_W / 2, P.PACK_CY - P.PACK_L / 2, P.PACK_CY + P.PACK_L / 2,
               P.PACK_FRONT - shift, P.PACK_BACK - shift)


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
    y0, y1 = -40.0, 40.0
    d0, d1 = P.REAR_ENVELOPE + 0.5, P.COVER_INNER - 3.0
    left = box(-P.INNER_W / 2 + 0.4, -P.INNER_W / 2 + 4.4, y0, y1, d0, d1)
    right = box(P.PACK_W / 2 + 1.0, P.INNER_W / 2 - 0.4, y0, y1, d0, d1)
    return left, right


def screws():
    """M3 x 30 button heads seated in the counterbores."""
    solid = None
    seat = P.DEPTH - P.CBORE_DEPTH
    for x, y in P.HOLES:
        s = cyl(3.0, seat - P.SCREW_L, seat, x, y) + cyl(P.SCREW_HEAD_D, seat, seat + P.SCREW_HEAD_H, x, y)
        solid = s if solid is None else solid + s
    return solid


__all__ = ["board", "glass", "pcb", "rear_parts", "pad", "pack", "usb_plug", "sd_card_path",
           "cable_channels", "screws", "Box", "Pos"]
