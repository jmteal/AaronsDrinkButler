"""Button plunger: a printed pin that carries a finger press from the door well to a button.

Drop one into each floor hole from outside (shaft first). The cap is wider than the hole,
so a plunger can't fall into the case; the closed door keeps it from falling out. At rest
the tip sits on the button with the cap PLUNGER_CAP_GAP above the floor; pressing the cap
onto the floor clicks the button and stops before over-travel. Print two, cap down.
"""

from build123d import Axis, chamfer

import params as P
from parts.common import cyl

CAP_BOTTOM = P.FLOOR_TOP + P.PLUNGER_CAP_GAP
CAP_TOP = CAP_BOTTOM + P.PLUNGER_CAP_T


def build(x=0.0, y=0.0, pressed=0.0):
    """One plunger at rest over the button at (x, y); pressed moves it in by that much."""
    d0 = P.BTN_TOP - pressed
    tip = cyl(P.PLUNGER_TIP_D, d0, P.PLUNGER_SHAFT_END - pressed + 0.01, x, y)
    shaft = cyl(P.PLUNGER_D, P.PLUNGER_SHAFT_END - pressed, CAP_BOTTOM - pressed + 0.01, x, y)
    cap = cyl(P.PLUNGER_CAP_D, CAP_BOTTOM - pressed, CAP_TOP - pressed, x, y)
    cap = chamfer(cap.edges().group_by(Axis.Z)[0], 0.4)     # finger side
    return tip + shaft + cap


def installed(pressed=0.0):
    solid = None
    for x, y, _ in P.PLUNGERS:
        p = build(x, y, pressed)
        solid = p if solid is None else solid + p
    return solid
