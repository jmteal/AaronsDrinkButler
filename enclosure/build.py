#!/usr/bin/env python3
"""Regenerate every enclosure part from params.py into out/ (STL, 3MF and STEP).

Usage (from the repo root or enclosure/):
    enclosure/.venv/bin/python enclosure/build.py

Parts are written in their print orientation. Each <part>.3mf holds the print as one
object, centred on the plate, with the white labels (if any) as a second part named
"... labels (white)": open it and set that part to the white filament. Two-colour parts
also come as a pair of STLs in the same frame: <part>.stl (black) and <part>_text.stl
(white). Load one part and its own _text file together and answer "Yes" to loading them
as one object with multiple parts. Never combine two different parts that way: they
share an origin and would print merged. out/assembly.step holds the printed parts in
their assembled positions.

The two test pieces are separate on purpose: test_front checks the front shell (window,
pocket, bands, inserts, microSD), test_back checks the rear cover (door, hinge, latch,
plungers). Their STLs are written already spread out on the test plate, so even loaded
together they sit side by side. out/test_plate.3mf holds both plus two plungers.
"""

import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from build123d import Compound, Pos, Rot, export_step, export_stl  # noqa: E402

import params as P  # noqa: E402
from parts import door, door_test, fit_coupon, front_shell, plunger, rear_cover, stand, wafer  # noqa: E402
from parts import threemf  # noqa: E402
from parts.common import for_print_face_down  # noqa: E402

OUT = HERE / "out"

# Printed files: name -> (black bodies, white bodies, pose). Bodies are keys of build_all().
PRINTS = {
    "front_shell": (["front_shell"], ["front_shell_text"], "face_down"),
    "rear_cover": (["rear_cover", "door"], ["door_text"], "cover"),
    "stand": (["stand"], [], "as_built"),
    "wafer": (["wafer"], [], "wafer"),
    "plunger": (["plunger"], [], "plunger"),
    "test_front": (["fit_coupon"], ["fit_coupon_text"], "face_down"),
    "test_back": (["door_test", "door_test_door"], ["door_test_text"], "cover"),
}

# Test plate: the two test pieces and two plungers, each its own object. The test STLs are
# exported at these positions too (see export()), so they can't land on top of each other.
TEST_PRINTS = ("test_front", "test_back")
TEST_LAYOUT = [
    ("test_front", 0, 10.0, 10.0),
    ("test_back", 0, 90.0, 10.0),
    ("plunger", 0, 90.0, 45.0),
    ("plunger", 1, 110.0, 45.0),
]


def build_all():
    """Build every body in the assembled-device frame (the stand in its own frame)."""
    shell_text = front_shell.build_text()
    shell = front_shell.build(shell_text)
    cover = rear_cover.build()
    the_door = door.build()
    door_text = door.build_text()
    test_cover, test_door = door_test.build(cover, the_door)
    return {
        "front_shell": shell,
        "front_shell_text": shell_text,
        "rear_cover": cover,
        "door": the_door,
        "door_text": door_text,
        "stand": stand.build(),
        "wafer": wafer.build(),
        "plunger": plunger.build(),
        "fit_coupon": fit_coupon.build(shell),
        "fit_coupon_text": shell_text & fit_coupon.slab(),
        "door_test": test_cover,
        "door_test_door": test_door,
        "door_test_text": door_text & door_test.region(),
    }


def pose(kind, part):
    if kind == "face_down":
        return for_print_face_down(part)                  # front face on the plate
    if kind == "cover":
        return Pos(0, 0, P.DEPTH) * part                  # outside face already points down
    if kind == "wafer":
        return Pos(0, 0, -P.PAD_FRONT) * Rot(180, 0, 0) * part   # board side down
    if kind == "plunger":
        return Pos(0, 0, plunger.CAP_TOP) * part          # cap down
    return part                                           # stand is built upright on Z = 0


def print_pose(name, part):
    """Print pose of a single body (used by validation)."""
    for black, white, kind in PRINTS.values():
        if name in black or name in white:
            return pose(kind, part)
    raise KeyError(name)


# One-plate layout for a full build (everything except the test pieces). Each entry is
# (print, copy, lower-left corner X, Y on the plate). The top-right corner stays empty
# for Bambu Studio's prime tower (multi-colour needs one).
PLATE = (256.0, 256.0)
PLATE_MARGIN = 10.0
PLATE_GAP = 10.0
PRIME_TOWER_KEEP = (60.0, 60.0)     # left free in the top-right corner
PLATE_LAYOUT = [
    ("front_shell", 0, 10.0, 10.0),
    ("rear_cover", 0, 84.0, 10.0),
    ("wafer", 0, 158.0, 10.0),
    ("stand", 0, 10.0, 132.0),
    ("plunger", 0, 92.0, 132.0),
    ("plunger", 1, 109.0, 132.0),
]


def plate_bodies(parts, layout=PLATE_LAYOUT):
    """Every print in its print pose, moved to its place on the plate.

    Returns (black bodies, white bodies, footprints) with footprints as (name, bbox).
    """
    black, white, feet = [], [], []
    for name, copy, x, y in layout:
        b_names, w_names, kind = PRINTS[name]
        posed_b = [pose(kind, parts[k]) for k in b_names]
        posed_w = [pose(kind, parts[k]) for k in w_names]
        bb = _one(posed_b).bounding_box()
        move = Pos(x - bb.min.X, y - bb.min.Y, -bb.min.Z)
        black += [move * s for s in posed_b]
        white += [move * s for s in posed_w]
        feet.append((f"{name}#{copy + 1}", _one([move * s for s in posed_b]).bounding_box()))
    return black, white, feet


def layout_objects(parts, layout):
    """3MF objects for a plate layout: one object per print, its labels as a second part."""
    objects = []
    for name, copy, x, y in layout:
        b_names, w_names, kind = PRINTS[name]
        posed_b = _one([pose(kind, parts[k]) for k in b_names])
        bb = posed_b.bounding_box()
        move = Pos(x - bb.min.X, y - bb.min.Y, -bb.min.Z)
        label = name if sum(n == name for n, *_ in layout) == 1 else f"{name} {copy + 1}"
        obj = [(label, move * posed_b)]
        if w_names:
            obj.append((f"{label} labels (white)", move * _one([pose(kind, parts[k]) for k in w_names])))
        objects.append((label, obj))
    return objects


def export_plate(parts):
    black, white, _ = plate_bodies(parts)
    write_stl(_one(black), OUT / "plate.stl")
    write_stl(_one(white), OUT / "plate_text.stl")
    threemf.write(OUT / "plate.3mf", layout_objects(parts, PLATE_LAYOUT))


def test_move(parts, name):
    """Where a test print sits on the test plate (applied to its own STL/3MF as well)."""
    for n, copy, x, y in TEST_LAYOUT:
        if n == name and copy == 0:
            b_names, _, kind = PRINTS[name]
            bb = _one([pose(kind, parts[k]) for k in b_names]).bounding_box()
            return Pos(x - bb.min.X, y - bb.min.Y, -bb.min.Z)
    return Pos(0, 0, 0)


def export_test_plate(parts):
    """One 3MF with each test print as its own object (never merged into one)."""
    threemf.write(OUT / "test_plate.3mf", layout_objects(parts, TEST_LAYOUT))


def _one(bodies):
    return bodies[0] if len(bodies) == 1 else Compound(children=list(bodies))


def write_stl(shape, path):
    export_stl(shape, str(path), tolerance=0.01, angular_tolerance=0.1)


def export(parts):
    OUT.mkdir(exist_ok=True)
    for old in ("fit_coupon", "door_test"):          # renamed to test_front / test_back
        for ext in (".stl", "_text.stl", ".3mf", ".step"):
            (OUT / f"{old}{ext}").unlink(missing_ok=True)
    for name, (black, white, kind) in PRINTS.items():
        move = test_move(parts, name) if name in TEST_PRINTS else Pos(0, 0, 0)
        b = move * _one([pose(kind, parts[k]) for k in black])
        write_stl(b, OUT / f"{name}.stl")
        export_step(b, str(OUT / f"{name}.step"))
        if white:
            write_stl(move * _one([pose(kind, parts[k]) for k in white]), OUT / f"{name}_text.stl")
        # 3MF: the print alone, centred on the plate.
        bb = _one([pose(kind, parts[k]) for k in black]).bounding_box()
        cx, cy = PLATE[0] / 2 - bb.size.X / 2, PLATE[1] / 2 - bb.size.Y / 2
        threemf.write(OUT / f"{name}.3mf", layout_objects(parts, [(name, 0, cx, cy)]))
    assembly = Compound(label="drink_butler_case", children=[
        parts["front_shell"], parts["front_shell_text"], parts["rear_cover"], parts["door"],
        parts["door_text"], parts["wafer"], plunger.installed(),
    ])
    export_step(assembly, str(OUT / "assembly.step"))
    export_plate(parts)
    export_test_plate(parts)


def main():
    t0 = time.time()
    parts = build_all()
    export(parts)
    for name, (black, white, kind) in PRINTS.items():
        b = _one([pose(kind, parts[k]) for k in black + white])
        bb = b.bounding_box()
        vol = sum(parts[k].volume for k in black + white) / 1000
        print(f"{name:12s} {bb.size.X:6.1f} x {bb.size.Y:6.1f} x {bb.size.Z:6.1f} mm"
              f"  {vol:6.1f} cm3" + ("  (+ white text)" if white else ""))
    _, _, feet = plate_bodies(parts)
    x1 = max(bb.max.X for _, bb in feet)
    y1 = max(bb.max.Y for _, bb in feet)
    print(f"plate       {len(feet)} parts on one {PLATE[0]:.0f} x {PLATE[1]:.0f} plate, "
          f"footprint {x1:.0f} x {y1:.0f} mm (out/plate.stl + plate_text.stl)")
    print(f"test plate  {', '.join(n for n, _, _, _ in TEST_LAYOUT)} as separate objects "
          f"(out/test_plate.3mf)")
    print(f"wrote {OUT} in {time.time() - t0:.1f} s")
    return parts


if __name__ == "__main__":
    main()
