"""Door test piece: the bottom of the rear cover with the door, hinge, latch and labels.

About 48 x 26 x 6 mm. Print it (cover face down, labels in white) before the full cover
to check that the print-in-place gaps free up, the door swings past vertical and the
latch clicks. If the door is stuck, raise DOOR_GAP / HINGE_GAP / LATCH_GAP in params.py.
"""

import params as P
from parts.common import box

X = P.BAY_HW + 3.0
Y0, Y1 = -P.OUTER_H / 2 - 1, P.BAY_Y1 + 1.5
D0 = P.FLOOR_BOTTOM - 0.01


def region():
    return box(-X, X, Y0, Y1, D0, P.DEPTH + 1)


def build(cover, door):
    """Cover and door cut to the test region (two separate solids)."""
    return cover & region(), door & region()
