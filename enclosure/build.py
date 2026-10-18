#!/usr/bin/env python3
"""Regenerate every enclosure part from params.py into out/ (STL, 3MF and STEP).

Usage (from the repo root or enclosure/):
    enclosure/.venv/bin/python enclosure/build.py

Parts are written in their print orientation. out/assembly.step holds the printed parts
in their assembled positions for checking in a CAD viewer or Bambu Studio.
"""

import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from build123d import Compound, Mesher, Pos, export_step, export_stl  # noqa: E402

import params as P  # noqa: E402
from parts import fit_coupon, front_shell, rear_cover, stand  # noqa: E402
from parts.common import for_print_face_down  # noqa: E402

OUT = HERE / "out"


def build_all():
    """Build every part in the assembled-device frame (the stand in its own frame)."""
    shell = front_shell.build()
    return {
        "front_shell": shell,
        "rear_cover": rear_cover.build(),
        "stand": stand.build(),
        "fit_coupon": fit_coupon.build(shell),
    }


def print_pose(name, part):
    if name in ("front_shell", "fit_coupon"):
        return for_print_face_down(part)          # front face on the plate
    if name == "rear_cover":
        return Pos(0, 0, P.DEPTH) * part          # outside face already points down
    return part                                   # stand is built upright on Z = 0


def export(parts):
    OUT.mkdir(exist_ok=True)
    for name, part in parts.items():
        posed = print_pose(name, part)
        export_stl(posed, str(OUT / f"{name}.stl"), tolerance=0.01, angular_tolerance=0.1)
        export_step(posed, str(OUT / f"{name}.step"))
        mesher = Mesher()
        mesher.add_shape(posed, linear_deflection=0.01, angular_deflection=0.1, part_number=name)
        mesher.write(str(OUT / f"{name}.3mf"))
    assembly = Compound(label="drink_butler_case", children=[
        parts["front_shell"], parts["rear_cover"],
    ])
    export_step(assembly, str(OUT / "assembly.step"))


def main():
    t0 = time.time()
    parts = build_all()
    export(parts)
    for name, part in parts.items():
        bb = print_pose(name, part).bounding_box()
        print(f"{name:12s} {bb.size.X:6.1f} x {bb.size.Y:6.1f} x {bb.size.Z:6.1f} mm"
              f"  {part.volume / 1000:6.1f} cm3")
    print(f"wrote {OUT} in {time.time() - t0:.1f} s")
    return parts


if __name__ == "__main__":
    main()
