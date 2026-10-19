#!/usr/bin/env python3
"""Regenerate every enclosure part from params.py into out/ (STL, 3MF and STEP).

Usage (from the repo root or enclosure/):
    enclosure/.venv/bin/python enclosure/build.py

Parts are written in their print orientation. Two-colour parts are written as a pair of
files in the same frame: <part>.stl (black) and <part>_text.stl (white). Load both into
Bambu Studio at once and answer "Yes" to loading them as one object with multiple parts,
then set the _text part to the white filament. The 3MF of a two-colour part holds both
bodies. out/assembly.step holds the printed parts in their assembled positions.
"""

import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from build123d import Compound, Mesher, Pos, Rot, export_step, export_stl  # noqa: E402

import params as P  # noqa: E402
from parts import door, door_test, fit_coupon, front_shell, plunger, rear_cover, stand, wafer  # noqa: E402
from parts.common import for_print_face_down  # noqa: E402

OUT = HERE / "out"

# Printed files: name -> (black bodies, white bodies, pose). Bodies are keys of build_all().
PRINTS = {
    "front_shell": (["front_shell"], ["front_shell_text"], "face_down"),
    "rear_cover": (["rear_cover", "door"], ["door_text"], "cover"),
    "stand": (["stand"], [], "as_built"),
    "wafer": (["wafer"], [], "wafer"),
    "plunger": (["plunger"], [], "plunger"),
    "fit_coupon": (["fit_coupon"], ["fit_coupon_text"], "face_down"),
    "door_test": (["door_test", "door_test_door"], ["door_test_text"], "cover"),
}


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


def _one(bodies):
    return bodies[0] if len(bodies) == 1 else Compound(children=list(bodies))


def write_stl(shape, path):
    export_stl(shape, str(path), tolerance=0.01, angular_tolerance=0.1)


def export(parts):
    OUT.mkdir(exist_ok=True)
    for name, (black, white, kind) in PRINTS.items():
        b = _one([pose(kind, parts[k]) for k in black])
        write_stl(b, OUT / f"{name}.stl")
        export_step(b, str(OUT / f"{name}.step"))
        mesher = Mesher()
        mesher.add_shape(b, linear_deflection=0.01, angular_deflection=0.1, part_number=f"{name} (black)")
        if white:
            w = _one([pose(kind, parts[k]) for k in white])
            write_stl(w, OUT / f"{name}_text.stl")
            mesher.add_shape(w, linear_deflection=0.01, angular_deflection=0.1, part_number=f"{name} text (white)")
        mesher.write(str(OUT / f"{name}.3mf"))
    assembly = Compound(label="drink_butler_case", children=[
        parts["front_shell"], parts["front_shell_text"], parts["rear_cover"], parts["door"],
        parts["door_text"], parts["wafer"], plunger.installed(),
    ])
    export_step(assembly, str(OUT / "assembly.step"))


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
    print(f"wrote {OUT} in {time.time() - t0:.1f} s")
    return parts


if __name__ == "__main__":
    main()
