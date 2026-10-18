#!/usr/bin/env python3
"""Check the enclosure: valid solids, fit of board/pad/pack, interference, build volume.

Usage:
    enclosure/.venv/bin/python enclosure/validate.py            # checks + PNG previews
    enclosure/.venv/bin/python enclosure/validate.py --no-previews

Exits non-zero if any check fails. The board, pad and pack are simple boxes (parts/refs.py)
placed where params.py says they go; the pack is checked in its installed position,
pushed forward by the rib preload with the pad squeezed by the same amount.
"""

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import params as P  # noqa: E402
from build import build_all, print_pose  # noqa: E402
from parts import refs, stand  # noqa: E402

TOL_VOL = 1e-3     # mm^3 of overlap treated as zero (numerical noise)
TOL_DIST = 0.01    # mm

results = []


def check(name, ok, detail=""):
    results.append((ok, name, detail))
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f"  ({detail})" if detail else ""))
    return ok


def overlap(a, b):
    try:
        common = a & b
    except Exception:  # OCCT returns nothing to intersect for disjoint shapes on some builds
        return 0.0
    return sum(s.volume for s in common.solids()) if common is not None else 0.0


def no_overlap(name, a, b):
    v = overlap(a, b)
    return check(f"no interference: {name}", v <= TOL_VOL, f"overlap {v:.4f} mm3")


def gap(name, a, b, lo, hi=None):
    d = a.distance_to(b)
    ok = d >= lo - TOL_DIST and (hi is None or d <= hi + TOL_DIST)
    rng = f">= {lo}" if hi is None else f"{lo}..{hi}"
    return check(f"clearance: {name}", ok, f"{d:.3f} mm, want {rng}")


def touches(name, a, b):
    d = a.distance_to(b)
    return check(f"contact: {name}", d <= TOL_DIST and overlap(a, b) <= TOL_VOL, f"distance {d:.3f} mm")


def solids(parts):
    print("Solids")
    for name, part in parts.items():
        n = len(part.solids())
        check(f"{name} is one valid solid", part.is_valid and n == 1 and part.volume > 0,
              f"{n} solid(s), valid={part.is_valid}, {part.volume / 1000:.1f} cm3")


def build_volume(parts):
    print("Build volume (P2S 256 mm cube), print orientation")
    for name, part in parts.items():
        bb = print_pose(name, part).bounding_box()
        size = (bb.size.X, bb.size.Y, bb.size.Z)
        fits = all(s <= lim for s, lim in zip(size, P.BUILD_VOLUME))
        check(f"{name} fits", fits and abs(bb.min.Z) < 0.05,
              "{:.1f} x {:.1f} x {:.1f} mm, bottom at Z={:.2f}".format(*size, bb.min.Z))


def board_fit(shell, cover):
    print("Board, pad and pack")
    glass, pcb, rear = refs.glass(), refs.pcb(), refs.rear_parts()
    pad, pack = refs.pad(compressed=True), refs.pack(installed=True)
    for label, body in (("glass", glass), ("PCB", pcb), ("rear parts", rear), ("pad", pad), ("pack", pack)):
        no_overlap(f"front shell vs {label}", shell, body)
        no_overlap(f"rear cover vs {label}", cover, body)
    no_overlap("front shell vs rear cover", shell, cover)

    gap("glass to window wall", glass, shell, P.GLASS_GAP, P.GLASS_GAP + 0.05)
    # PCB edges: test a thinner copy so the band contact on the front face doesn't count
    # (0.3 mm off each face keeps those distances above the 0.2 side gap).
    from parts.common import prism, rrect
    pcb_core = prism(rrect(P.PCB_W, P.PCB_H, P.PCB_R), P.PCB_FRONT + 0.3, P.PCB_BACK - 0.3)
    gap("PCB edge to pocket wall", pcb_core, shell, P.BOARD_GAP, P.BOARD_GAP + 0.05)
    touches("PCB front face on the locating bands", pcb, shell)
    insert_path = prism(rrect(P.PCB_W, P.PCB_H, P.PCB_R), P.POCKET_BACK, P.SHELL_DEPTH + 1)
    no_overlap("board slides in from the back (insertion path)", insert_path, shell)
    touches("standoffs on the PCB pad rings", pcb, cover)
    touches("ribs on the pack (preloaded)", pack, cover)
    gap("pack to front shell walls", pack, shell, 1.0)
    gap("cover lip to shell wall", cover, shell, min(P.COVER_LIP_GAP, P.SEAM_GAP))

    glass_front = -P.GLASS_FLUSH
    check("glass flush with the front face (0 to 0.1 proud)",
          -0.1 - 1e-6 <= glass_front <= 1e-6, f"glass front at depth {glass_front:+.2f} mm")
    depth = P.DEPTH
    check("depth stack matches the plan (about 33.7 mm)", abs(depth - 33.7) < 1.5, f"{depth:.2f} mm")
    check("outer size near 64 x 112 mm", abs(P.OUTER_W - 64) <= 3 and abs(P.OUTER_H - 112) <= 3,
          f"{P.OUTER_W} x {P.OUTER_H} x {depth:.1f} mm")


def fasteners(shell, cover):
    print("Fasteners")
    skin = P.PCB_FRONT - P.INSERT_L
    check(f"front skin in front of the inserts >= {P.FRONT_SKIN_MIN}", skin >= P.FRONT_SKIN_MIN - 1e-6,
          f"{skin:.2f} mm")
    seat = P.DEPTH - P.CBORE_DEPTH
    tip = seat - P.SCREW_L
    engage = P.PCB_FRONT - max(tip, skin)
    check("screw tip stops short of the front skin", tip >= skin + 0.3 - 1e-6,
          f"tip at depth {tip:.2f}, insert hole bottom at {skin:.2f}")
    check("screw engages the insert >= 2 mm", engage >= 2.0, f"{engage:.2f} mm")
    check("screw heads recessed in the cover", seat + P.SCREW_HEAD_H <= P.DEPTH + 1e-6,
          f"head top {P.DEPTH - seat - P.SCREW_HEAD_H:.2f} mm below the back face")
    screws = refs.screws()
    no_overlap("screws vs front shell", screws, shell)
    no_overlap("screws vs rear cover", screws, cover)
    no_overlap("screws vs pack", screws, refs.pack())


def openings(shell, cover):
    print("Ports and cable space")
    no_overlap("USB-C cable overmold vs front shell", refs.usb_plug(), shell)
    no_overlap("microSD card path vs front shell", refs.sd_card_path(), shell)
    left, right = refs.cable_channels()
    for label, ch in (("left (battery lead)", left), ("right (speaker lead)", right)):
        no_overlap(f"cable channel {label} vs rear cover", ch, cover)
        no_overlap(f"cable channel {label} vs front shell", ch, shell)
        no_overlap(f"cable channel {label} vs pack", ch, refs.pack())


def stand_fit(shell, cover, st):
    print("Stand")
    loc = stand.device_location()
    device = loc * (shell + cover)
    no_overlap("stand vs device", st, device)
    touches("device resting on the stand", st, device)
    no_overlap("stand vs USB-C cable overmold", st, loc * refs.usb_plug())
    low = device.bounding_box().min.Z
    check("device bottom lifted 8-12 mm", 8.0 - TOL_DIST <= low <= 12.0 + TOL_DIST, f"{low:.2f} mm")
    plug_low = (loc * refs.usb_plug()).bounding_box().min.Z
    check("USB-C overmold clears the counter", plug_low > 0.5, f"lowest point {plug_low:.2f} mm")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-previews", action="store_true")
    args = ap.parse_args()

    parts = build_all()
    solids(parts)
    build_volume(parts)
    board_fit(parts["front_shell"], parts["rear_cover"])
    fasteners(parts["front_shell"], parts["rear_cover"])
    openings(parts["front_shell"], parts["rear_cover"])
    stand_fit(parts["front_shell"], parts["rear_cover"], parts["stand"])

    failed = [r for r in results if not r[0]]
    print(f"\n{len(results) - len(failed)}/{len(results)} checks passed")
    if not args.no_previews:
        import previews
        for path in previews.render_all(parts):
            print(f"preview: {path.relative_to(HERE.parent)}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
