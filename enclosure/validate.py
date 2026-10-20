#!/usr/bin/env python3
"""Check the enclosure: valid solids, fit of board/pad/pack, interference, build volume.

Usage:
    enclosure/.venv/bin/python enclosure/validate.py            # checks + PNG previews
    enclosure/.venv/bin/python enclosure/validate.py --no-previews

Exits non-zero if any check fails. The board and pack are simple boxes (parts/refs.py)
placed where params.py says they go. The pack sits on the rigid printed wafer, so the
ribs' preload is taken by the cover plate flexing back; pack checks use the cover moved
back by RIB_PRELOAD. The door is checked shut, and swung open in steps to DOOR_OPEN_MIN.
"""


import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from build123d import Pos  # noqa: E402

import params as P  # noqa: E402
from build import build_all, print_pose  # noqa: E402
from parts import door, plunger, rear_cover, refs, snaps, stand  # noqa: E402

TOL_VOL = 1e-3     # mm^3 of overlap treated as zero (numerical noise)
TOL_DIST = 0.01    # mm

results = []
parts_g = {}


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


TEXT_BODIES = ("front_shell_text", "door_text", "fit_coupon_text", "door_test_text")


def solids(parts):
    print("Solids")
    for name, part in parts.items():
        n = len(part.solids())
        if name in TEXT_BODIES:      # one solid per letter
            check(f"{name} is valid", part.is_valid and n >= 1 and part.volume > 0,
                  f"{n} solid(s), {part.volume:.0f} mm3")
            continue
        check(f"{name} is one valid solid", part.is_valid and n == 1 and part.volume > 0,
              f"{n} solid(s), valid={part.is_valid}, {part.volume / 1000:.1f} cm3")


def build_volume(parts):
    print("Build volume (P2S 256 mm cube), print orientation")
    for name, part in parts.items():
        bb = print_pose(name, part).bounding_box()
        size = (bb.size.X, bb.size.Y, bb.size.Z)
        fits = all(s <= lim for s, lim in zip(size, P.BUILD_VOLUME))
        on_plate = abs(bb.min.Z) < 0.05 or name in TEXT_BODIES   # text rides on its part
        check(f"{name} fits", fits and on_plate,
              "{:.1f} x {:.1f} x {:.1f} mm, bottom at Z={:.2f}".format(*size, bb.min.Z))


def plate_check(parts):
    from build import PLATE, PLATE_GAP, PLATE_MARGIN, PRIME_TOWER_KEEP, plate_bodies
    print("One-plate layout (out/plate.stl)")
    black, white, feet = plate_bodies(parts)
    m = PLATE_MARGIN - 0.01
    inside = all(bb.min.X >= m and bb.min.Y >= m and bb.max.X <= PLATE[0] - m
                 and bb.max.Y <= PLATE[1] - m and abs(bb.min.Z) < 0.05 for _, bb in feet)
    check(f"all {len(feet)} parts on the plate, {PLATE_MARGIN:.0f} mm from the edges, flat",
          inside, ", ".join(f"{n} {bb.size.X:.0f}x{bb.size.Y:.0f}" for n, bb in feet))
    worst = 1e9
    for i in range(len(feet)):
        for j in range(i + 1, len(feet)):
            a, b = feet[i][1], feet[j][1]
            dx = max(b.min.X - a.max.X, a.min.X - b.max.X)
            dy = max(b.min.Y - a.max.Y, a.min.Y - b.max.Y)
            worst = min(worst, max(dx, dy))
    check(f"parts at least {PLATE_GAP - 3:.0f} mm apart", worst >= PLATE_GAP - 3 - 1e-6,
          f"closest pair {worst:.1f} mm")
    tx, ty = PLATE[0] - PRIME_TOWER_KEEP[0], PLATE[1] - PRIME_TOWER_KEEP[1]
    free = all(bb.max.X <= tx or bb.max.Y <= ty for _, bb in feet)
    check(f"top-right {PRIME_TOWER_KEEP[0]:.0f} x {PRIME_TOWER_KEEP[1]:.0f} mm free for the prime tower", free)
    for s in white:
        if not any(s.distance_to(bl) < 1e-3 for bl in black):
            check("every white label sits on its black part", False)
            break
    else:
        check("every white label sits on its black part", True)


def test_piece_checks(parts):
    """The two test pieces stay separate: front-only and back-only, never on top of each other."""
    from build import PLATE, PLATE_MARGIN, PRINTS, TEST_LAYOUT, TEST_PRINTS, _one, plate_bodies, pose, test_move
    print("Test pieces (test_front, test_back, out/test_plate.3mf)")
    front, back, back_door = parts["fit_coupon"], parts["door_test"], parts["door_test_door"]
    no_overlap("test_front has no rear-cover or door geometry", front, parts["rear_cover"] + parts["door"])
    no_overlap("test_back has no front-shell geometry", back + back_door, parts["front_shell"])
    for label, body in (("glass", refs.glass()), ("PCB", refs.pcb()), ("rear parts", refs.rear_parts())):
        no_overlap(f"test_front vs {label} (board drops in)", front, body)
    gap("test_front glass to window wall", refs.glass(), front, P.GLASS_GAP, P.GLASS_GAP + 0.05)
    no_overlap("test_back door vs its cover piece (shut)", back_door, back)
    swing = door.build(latch=False)            # latch ridges spring past; skip them
    stop = None
    for a in range(5, int(P.DOOR_OPEN_MIN) + 1, 5):
        if overlap(door.open_location(a) * swing, back) > TOL_VOL:
            stop = a
            break
    check(f"test_back door swings to {P.DOOR_OPEN_MIN:.0f} deg on its own",
          stop is None, "" if stop is None else f"first contact near {stop} deg")
    # Exported STLs: each test print sits at its own spot, so loading them together can't merge them.
    boxes = {}
    for name in TEST_PRINTS:
        b_names, _, kind = PRINTS[name]
        boxes[name] = (test_move(parts, name) * _one([pose(kind, parts[k]) for k in b_names])).bounding_box()
    a, b = boxes["test_front"], boxes["test_back"]
    apart = max(b.min.X - a.max.X, a.min.X - b.max.X, b.min.Y - a.max.Y, a.min.Y - b.max.Y)
    check("test_front.stl and test_back.stl don't overlap even if loaded as one object",
          apart >= 5.0, f"{apart:.1f} mm apart")
    _, _, feet = plate_bodies(parts, TEST_LAYOUT)
    m = PLATE_MARGIN - 0.01
    inside = all(bb.min.X >= m and bb.min.Y >= m and bb.max.X <= PLATE[0] - m
                 and bb.max.Y <= PLATE[1] - m and abs(bb.min.Z) < 0.05 for _, bb in feet)
    worst = min(max(q.min.X - p.max.X, p.min.X - q.max.X, q.min.Y - p.max.Y, p.min.Y - q.max.Y)
                for i, (_, p) in enumerate(feet) for _, q in feet[i + 1:])
    check(f"test plate: {len(feet)} objects on the plate, at least 5 mm apart", inside and worst >= 5.0,
          f"closest pair {worst:.1f} mm")


BAMBU_CLI = Path("/Applications/BambuStudio.app/Contents/MacOS/BambuStudio")


def threemf_checks(bambu=False):
    """Read every 3MF back: one object per print, labels as a part, flat, full size, apart.

    These are what Bambu Studio checks on import. Before this, each letter was its own tiny
    object, which set off its "too small" and "multiple heights" prompts.
    """
    from build import OUT, PLATE, PLATE_LAYOUT, PRINTS, TEST_LAYOUT
    from parts import threemf
    print("3MF files (what Bambu Studio sees)")
    expect = {f"{n}.3mf": [n] for n in PRINTS}
    expect["plate.3mf"] = [n for n, *_ in PLATE_LAYOUT]
    expect["test_plate.3mf"] = [n for n, *_ in TEST_LAYOUT]
    for fname, prints in expect.items():
        objs = threemf.summary(OUT / fname)
        problems = []
        if len(objs) != len(prints):
            problems.append(f"{len(objs)} objects, want {len(prints)}")
        for (oname, pnames, bb), pname in zip(objs, prints):
            want_parts = 2 if PRINTS[pname][1] else 1
            if len(pnames) != want_parts:
                problems.append(f"{oname}: {len(pnames)} parts")
            if abs(bb[2]) > 0.05:
                problems.append(f"{oname} floats at z={bb[2]:.2f}")
            if min(bb[3] - bb[0], bb[4] - bb[1]) < 5.0:
                problems.append(f"{oname} under 5 mm")
            if bb[0] < 0 or bb[1] < 0 or bb[3] > PLATE[0] or bb[4] > PLATE[1]:
                problems.append(f"{oname} off the plate")
        for i, (_, _, a) in enumerate(objs):
            for (_, _, b) in objs[i + 1:]:
                if max(b[0] - a[3], a[0] - b[3], b[1] - a[4], a[1] - b[4]) < 5.0:
                    problems.append("objects closer than 5 mm")
        check(f"{fname}: {len(prints)} object(s), labels as parts, flat on the plate",
              not problems, "; ".join(problems) or ", ".join(o for o, _, _ in objs))
    if not bambu or not BAMBU_CLI.exists():
        # Opt-in: it launches Bambu Studio's command line, which macOS may flag.
        print("  (add --bambu to also round-trip test_plate.3mf through Bambu Studio)")
        return
    import re
    import subprocess
    import tempfile
    import zipfile
    with tempfile.TemporaryDirectory() as tmp:
        r = subprocess.run([str(BAMBU_CLI), str(OUT / "test_plate.3mf"), "--arrange", "0",
                            "--outputdir", tmp, "--export-3mf", "rt.3mf"],
                           capture_output=True, text=True, timeout=300)
        ok = r.returncode == 0 and (Path(tmp) / "rt.3mf").exists()
        names = []
        if ok:
            cfg = zipfile.ZipFile(Path(tmp) / "rt.3mf").read("Metadata/model_settings.config").decode()
            for ob in re.findall(r'<object id="\d+">(.*?)</object>', cfg, re.S):
                parts = re.findall(r"<part ", ob)
                names.append(f"{re.search(r'key=\"name\" value=\"([^\"]*)', ob).group(1)}:{len(parts)}")
        want = [f"{n}{'' if sum(m == n for m, *_ in TEST_LAYOUT) == 1 else f' {c + 1}'}:"
                f"{2 if PRINTS[n][1] else 1}" for n, c, *_ in TEST_LAYOUT]
        check("Bambu Studio imports test_plate.3mf as the same objects and parts",
              ok and names == want, ", ".join(names) or f"exit {r.returncode}")


def board_fit(shell, cover, wafer):
    print("Board, wafer and pack")
    glass, pcb, rear = refs.glass(), refs.pcb(), refs.rear_parts()
    pack = refs.pack()
    flexed = Pos(0, 0, -P.RIB_PRELOAD) * cover          # cover plate pushed back by the ribs
    for label, body in (("glass", glass), ("PCB", pcb), ("rear parts", rear)):
        no_overlap(f"front shell vs {label}", shell, body)
        no_overlap(f"rear cover vs {label}", cover, body)
        no_overlap(f"wafer vs {label}", wafer, body)
    no_overlap("front shell vs pack", shell, pack)
    no_overlap("rear cover (flexed by the preload) vs pack", flexed, pack)
    no_overlap("front shell vs rear cover", shell, cover)
    no_overlap("wafer vs front shell", wafer, shell)
    no_overlap("wafer vs rear cover", wafer, cover)
    no_overlap("wafer vs pack", wafer, pack)

    gap("glass to window wall", glass, shell, P.GLASS_GAP, P.GLASS_GAP + 0.05)
    # PCB edges: test a thinner copy so the band contact on the front face doesn't count
    # (shaving each face by more than BOARD_GAP keeps those distances above the side gap).
    from parts.common import prism, rrect
    shave = min(P.BOARD_GAP + 0.03, P.PCB_T / 2 - 0.05)
    pcb_core = prism(rrect(P.PCB_W, P.PCB_H, P.PCB_R), P.PCB_FRONT + shave, P.PCB_BACK - shave)
    gap("PCB edge to pocket wall", pcb_core, shell, P.BOARD_GAP, P.BOARD_GAP + 0.05)
    touches("PCB front face on the locating bands", pcb, shell)
    insert_path = prism(rrect(P.PCB_W, P.PCB_H, P.PCB_R), P.POCKET_BACK, P.SHELL_DEPTH + 1)
    no_overlap("board slides in from the back (insertion path)", insert_path, shell)
    touches("standoffs on the PCB pad rings", pcb, cover)
    touches("wafer rests on the rear parts", wafer, rear)
    touches("pack sits on the wafer", pack, wafer)
    touches("ribs on the pack, with the cover flexed back by the preload", pack, flexed)
    check(f"rib preload {P.RIB_PRELOAD} mm is small enough for the plate to flex (<= 0.3)",
          0 < P.RIB_PRELOAD <= 0.3)
    gap("pack to front shell walls", pack, shell, 1.0)
    from parts.common import box
    block = cover & box(-P.OUTER_W, P.OUTER_W, -P.OUTER_H, P.BAY_Y1 + 0.5, 0, P.DEPTH + 1)
    gap("pack to the door block", pack, block, 0.5)
    gap("cover lip to shell wall", rear_cover.lip(), shell, min(P.COVER_LIP_GAP, P.SEAM_GAP))

    glass_front = -P.GLASS_FLUSH
    check("glass flush with the front face (0 to 0.1 proud)",
          -0.1 - 1e-6 <= glass_front <= 1e-6, f"glass front at depth {glass_front:+.2f} mm")
    depth = P.DEPTH
    check("depth stack matches the plan (about 33.7 mm)", abs(depth - 33.7) < 1.5, f"{depth:.2f} mm")
    check("outer size near 64 x 112 mm", abs(P.OUTER_W - 64) <= 3 and abs(P.OUTER_H - 112) <= 3,
          f"{P.OUTER_W} x {P.OUTER_H} x {depth:.1f} mm")
    side = (P.OUTER_W - P.GLASS_W) / 2 - P.GLASS_GAP
    check("front bevel leaves a flat bezel beside the glass", P.FRONT_BEVEL <= side - 2.0,
          f"{P.FRONT_BEVEL} mm bevel, {side - P.FRONT_BEVEL:.2f} mm flat each side")


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
    no_overlap("screws vs door", screws, parts_g["door"])


def text_checks(parts):
    print("White text")
    shell, text = parts["front_shell"], parts["front_shell_text"]
    no_overlap("USB-C inlay vs front shell", text, shell)
    fronts = [s.bounding_box().max.Z for s in text.solids()]
    backs = [s.bounding_box().min.Z for s in text.solids()]
    flush = max(abs(z) for z in fronts) < 1e-3 and max(abs(z + P.INLAY_D) for z in backs) < 1e-3
    check("USB-C inlay: every letter flush with the front, INLAY_D deep", flush,
          f"{len(fronts)} pieces, fronts {min(fronts):+.3f}..{max(fronts):+.3f}, depth {P.INLAY_D} mm")
    touches("USB-C inlay fills its pocket", text, shell)
    bb = text.bounding_box()
    flat_bottom = -P.OUTER_H / 2 + P.FRONT_BEVEL
    window = P.GLASS_Y - P.GLASS_H / 2 - P.GLASS_GAP - P.WINDOW_CHAMFER
    check("USB-C label sits on the flat band below the screen", bb.min.Y >= flat_bottom + 0.5
          and bb.max.Y <= window - 0.5, f"Y {bb.min.Y:.1f}..{bb.max.Y:.1f}")
    check("USB-C label caps at least 4 mm tall", P.USB_LABEL_SIZE * 0.75 >= 4.0,
          f"{P.USB_LABEL_SIZE * 0.75:.2f} mm")
    for name in ("front_shell_text", "door_text"):
        bb = print_pose(name, parts[name]).bounding_box()
        layers = (bb.max.Z - bb.min.Z) / 0.2
        check(f"{name}: white is at least 3 layers (0.2 mm) thick", layers >= 2.99, f"{layers:.1f} layers")


def openings(shell, cover):
    print("Ports and cable space")
    no_overlap("USB-C cable overmold vs front shell", refs.usb_plug(), shell)
    no_overlap("microSD card path vs front shell", refs.sd_card_path(), shell)
    left, right = refs.cable_channels()
    for label, ch in (("left (battery lead)", left), ("right (speaker lead)", right)):
        no_overlap(f"cable channel {label} vs rear cover", ch, cover)
        no_overlap(f"cable channel {label} vs front shell", ch, shell)
        no_overlap(f"cable channel {label} vs pack", ch, refs.pack())


def snap_checks(parts):
    print("Snap tabs (toolless cover)")
    shell, cover = parts["front_shell"], parts["rear_cover"]
    arms = snaps.arms()
    no_overlap("snap arms vs front shell", arms, shell)
    touches("snap hooks seated on the pocket faces (cover held on)", arms, shell)
    for label, body in (("pack", refs.pack()), ("wafer", parts["wafer"]), ("rear parts", refs.rear_parts()),
                        ("door", parts["door"])):
        no_overlap(f"snap arms vs {label}", arms, body)
    for label, ch in zip(("left", "right"), refs.cable_channels()):
        no_overlap(f"snap arms vs {label} cable channel", arms, ch)
    # Each arm flexes inward on its own: nothing of the cover within the flex space.
    room = None
    for wall, side, pos in P.SNAPS:
        prof = [(-P.SNAP_T - P.SNAP_ENGAGE - 0.1, P.SNAP_TIP), (-P.SNAP_T - 0.01, P.SNAP_TIP),
                (-P.SNAP_T - 0.01, P.COVER_INNER - P.COVER_LIP_H + 0.5),
                (-P.SNAP_T - P.SNAP_ENGAGE - 0.1, P.COVER_INNER - P.COVER_LIP_H + 0.5)]
        r = snaps._place(prof, wall, side, pos, P.SNAP_W)
        room = r if room is None else room + r
    no_overlap("room for each arm to flex inward past the wall", room, cover)
    check("hook reaches >= 0.5 mm into the wall", P.SNAP_ENGAGE >= 0.5, f"{P.SNAP_ENGAGE:.2f} mm")
    strain = 1.5 * P.SNAP_T * P.SNAP_ENGAGE / P.SNAP_L ** 2
    check("snap arm bending strain <= 2% (PETG)", strain <= 0.02, f"{strain * 100:.2f}%")
    skin = P.WALL - (P.SNAP_POCKET - P.COVER_LIP_GAP)
    check("shell wall behind each pocket >= 1.0 mm", skin >= 1.0 - 1e-6, f"{skin:.2f} mm")
    check(f"{len(P.SNAPS)} snap tabs, on at least 3 walls", len(P.SNAPS) >= 4
          and len({(w, s) for w, s, _ in P.SNAPS}) >= 3)


def door_checks(parts):
    print("Button door (printed in place)")
    cover, dr, text = parts["rear_cover"], parts["door"], parts["door_text"]
    shell = parts["front_shell"]
    no_overlap("door vs rear cover (shut)", dr, cover)
    gap("print-in-place gap, door to cover", dr, cover,
        min(P.HINGE_GAP, P.LATCH_GAP, P.DOOR_GAP) - 0.01)
    no_overlap("door vs front shell", dr, shell)
    bb = dr.bounding_box()
    check("door's outer face flush with the back", abs(bb.min.Z + P.DEPTH) < 0.01,
          f"outer face at depth {-bb.min.Z:.3f}, back at {P.DEPTH:.3f}")
    # Hinge: cone pins reach into the door's sockets, so the door can't come off.
    engage = P.DOOR_HW - (P.DOOR_OPEN_HW - P.HINGE_CONE_H)
    check("cone pins reach >= 0.5 mm into the door barrels", engage >= 0.5, f"{engage:.2f} mm")
    # Swing: the door (latch ridges aside, they spring past the groove) clears the cover.
    swing = door.build(latch=False)
    worst, stop = 0.0, None
    a = 2.0
    while a <= P.DOOR_OPEN_MIN + 1e-6:
        v = overlap(door.open_location(a) * swing, cover)
        worst = max(worst, v)
        if v > TOL_VOL and stop is None:
            stop = a
        a += 7.0 if a + 7.0 <= P.DOOR_OPEN_MIN else max(P.DOOR_OPEN_MIN - a, 1.0)
    full = P.DOOR_OPEN_MIN
    if stop is None:                     # how far it actually goes, to the nearest 2 degrees
        while full < 180 and overlap(door.open_location(full + 2) * swing, cover) <= TOL_VOL:
            full += 2
    check(f"door swings to {P.DOOR_OPEN_MIN:.0f} deg without touching the cover", stop is None,
          f"worst overlap {worst:.4f} mm3" + (f", first contact near {stop:.0f} deg" if stop
                                               else f", opens to about {full:.0f} deg"))
    # Latch: ridges reach past the wall face; the strip bends that much to let go.
    check("latch ridge engages the groove >= 0.3 mm", P.LATCH_ENGAGE >= 0.3, f"{P.LATCH_ENGAGE:.2f} mm")
    L = 2 * P.LATCH_SLOT_HL
    strain = 6 * P.LATCH_ENGAGE * P.LATCH_STRIP_W / L ** 2
    check("latch strip bending strain <= 2% (PETG)", strain <= 0.02, f"{strain * 100:.2f}%")
    check("slot is wider than the strip has to bend", P.LATCH_SLOT_W >= P.LATCH_ENGAGE + 0.2)
    # Labels.
    touches("labels sit on the door's inner face", text, dr)
    no_overlap("labels vs rear cover (shut)", text, cover)
    for bx, by, name in P.PLUNGERS:
        cx, w = door.label_x(name, bx)
        check(f"{name} label within 3 mm of its button's X, on the door",
              abs(cx - bx) <= 3.0 and abs(cx) + w / 2 <= P.DOOR_HW - P.LABEL_EDGE + 1e-6,
              f"label centre {cx:+.2f}, button {bx:+.2f}, width {w:.1f}; pointer at {bx:+.2f}")
    cap = P.LABEL_SIZE * 0.75
    check("label caps at least 3.5 mm tall", cap >= 3.5, f"{cap:.2f} mm")


def plunger_checks(parts):
    print("Plungers")
    cover, dr, text, wafer = parts["rear_cover"], parts["door"], parts["door_text"], parts["wafer"]
    rest = plunger.installed()
    pressed = plunger.installed(pressed=P.PLUNGER_CAP_GAP)
    no_overlap("plungers vs rear cover (at rest)", rest, cover)
    no_overlap("plungers vs rear cover (pressed)", pressed, cover)
    gap("plunger shaft to bore", rest, cover, round((P.PLUNGER_BORE_D - P.PLUNGER_D) / 2 - 0.01, 3))
    touches("plunger tips rest on the buttons", rest, refs.button_tops())
    no_overlap("plungers vs rear parts (at rest)", rest, refs.rear_parts())
    no_overlap("plungers vs wafer (pressed)", pressed, wafer)
    gap("plunger caps to the shut door", rest, dr + text, 0.5)
    stroke = P.PLUNGER_CAP_GAP
    check("cap-to-floor stroke covers the button travel", stroke >= P.BTN_TRAVEL + 0.15,
          f"{stroke:.2f} mm stroke, {P.BTN_TRAVEL} mm travel")
    end_gap = P.PLUNGER_SHAFT_END - P.PLUNGER_CAP_GAP - P.REAR_ENVELOPE
    check("pressed shaft stays above the rear-parts envelope", end_gap >= 0.3, f"{end_gap:.2f} mm")
    lift = P.DOOR_INNER - P.LABEL_RELIEF - plunger.CAP_TOP
    in_tube = P.PLUNGER_SHAFT_END + lift <= P.FLOOR_TOP
    check("a plunger can't leave its tube with the door shut", in_tube,
          f"can lift {lift:.2f} mm before the cap meets the door")


def stand_fit(shell, cover, st):
    print("Stand")
    loc = stand.device_location()
    device = loc * (shell + cover + parts_g["door"])
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
    ap.add_argument("--bambu", action="store_true",
                    help="also import test_plate.3mf with Bambu Studio's CLI (if installed)")
    args = ap.parse_args()

    parts = build_all()
    parts_g.update(parts)
    solids(parts)
    build_volume(parts)
    plate_check(parts)
    test_piece_checks(parts)
    threemf_checks(args.bambu)
    board_fit(parts["front_shell"], parts["rear_cover"], parts["wafer"])
    fasteners(parts["front_shell"], parts["rear_cover"])
    openings(parts["front_shell"], parts["rear_cover"])
    snap_checks(parts)
    door_checks(parts)
    plunger_checks(parts)
    text_checks(parts)
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
