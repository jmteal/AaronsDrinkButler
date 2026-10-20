"""Snap tabs: flexing arms on the rear cover that click into pockets in the shell walls.

Each arm sits where the cover's lip would be, its outer face COVER_LIP_GAP off the shell
wall, and reaches SNAP_L forward from the cover's inner face. Its hook has a shallow
lead-in ramp at the tip and a 45-degree back face. Seated, that back face lies on the
pocket's matching 45-degree face, so the cover can't move back unless pulled hard.

Profiles are drawn in (w, depth): w is outward from the arm's outer face, toward the wall.
"""

import params as P
from parts.common import EPS, polygon, x_prism, y_prism

T = P.SNAP_TIP
H, R, F = P.SNAP_HOOK, P.SNAP_RAMP, P.SNAP_FLAT


def arm_profile():
    root = P.COVER_INNER + EPS
    g = P.SNAP_ROOT
    return [(-P.SNAP_T - g, root), (-P.SNAP_T, root - g), (-P.SNAP_T, T), (0, T), (H, T + R),
            (H, T + R + F), (0, T + R + F + H), (0, root)]


def pocket_profile():
    """Pocket in the wall. Its back face is the hook's 45° back face, extended."""
    back = T + R + F + H                       # where the hook's back face meets the arm
    k = P.SNAP_POCKET
    return [(-0.2, back + 0.2), (k, back - k), (k, T + 0.4 - (k - H)), (-0.2, T - 0.8 - (k - H))]


def _place(profile, wall, side, pos, width):
    """Sweep a (w, depth) profile along the wall, width wide, centred at pos."""
    if wall == "x":
        x0 = P.INNER_W / 2 - P.COVER_LIP_GAP
        sk = polygon([(side * (x0 + w), d) for w, d in profile])
        return y_prism(sk, pos - width / 2, pos + width / 2)
    y0 = P.INNER_H / 2 - P.COVER_LIP_GAP
    sk = polygon([(side * (y0 + w), d) for w, d in profile])
    return x_prism(sk, pos - width / 2, pos + width / 2)


def _all(make):
    solid = None
    for spec in P.SNAPS:
        s = make(*spec)
        solid = s if solid is None else solid + s
    return solid


def arms():
    return _all(lambda wall, side, pos: _place(arm_profile(), wall, side, pos, P.SNAP_W))


def lip_slots():
    """Cut the lip back round each arm so the arm can flex on its own."""
    t = max(P.COVER_LIP_T, P.SNAP_T + P.SNAP_ROOT)
    prof = [(-t - 0.2, P.COVER_INNER - P.COVER_LIP_H - 0.1), (0.3, P.COVER_INNER - P.COVER_LIP_H - 0.1),
            (0.3, P.COVER_INNER - 0.001), (-t - 0.2, P.COVER_INNER - 0.001)]
    w = P.SNAP_W + 2 * P.SNAP_SLOT
    return _all(lambda wall, side, pos: _place(prof, wall, side, pos, w))


def pockets():
    w = P.SNAP_W + 2 * P.SNAP_SIDE_GAP
    return _all(lambda wall, side, pos: _place(pocket_profile(), wall, side, pos, w))


def pry_notches():
    """45-degree notches in the shell's rim edge at mid-height of each side wall."""
    d, sd, x = P.PRY_D, P.SHELL_DEPTH, P.OUTER_W / 2
    solid = None
    for s in (-1, 1):
        sk = polygon([(s * (x + 1), sd + 0.5), (s * (x - d - 0.5), sd + 0.5), (s * (x + 1), sd - d - 1)])
        n = y_prism(sk, P.PRY_Y - P.PRY_W / 2, P.PRY_Y + P.PRY_W / 2)
        solid = n if solid is None else solid + n
    return solid
