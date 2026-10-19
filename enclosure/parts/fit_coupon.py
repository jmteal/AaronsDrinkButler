"""Fit coupon: the front frame of the shell, just deep enough to hold the board.

It keeps the flush window, the board pocket, the locating bands with all four insert
holes, and the first millimetre of side wall, so you can check the glass gap, flushness,
pocket fit and one insert install before printing the full shell. Print it face down.
"""

import params as P
from parts import front_shell
from parts.common import prism, rrect

DEPTH = P.PCB_BACK + P.COUPON_EXTRA


def slab():
    return prism(rrect(P.OUTER_W + 2, P.OUTER_H + 2, P.OUTER_R), -1, DEPTH)


def build(shell=None):
    shell = shell if shell is not None else front_shell.build()
    return shell & slab()
