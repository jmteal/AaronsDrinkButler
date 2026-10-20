"""Device recipe pack: one image for the read-only "drinks" flash partition.

    python build.py pack [-o drinks.bin]

Reads data/ (no network, standard library only) and writes a flat archive the firmware
maps with esp_partition_mmap() and reads in place. Format (all integers little-endian),
mirrored in firmware/components/data/include/adb_pack.h:

    header (16 bytes)  magic "ADBP", u32 version, u32 entry count, u32 crc32
    entries            count x 64 bytes: u32 offset, u32 size, char name[56] (NUL-padded),
                       sorted by name (byte order) so the device can binary-search them
    data               each file 4-byte aligned, followed by at least one NUL byte (so
                       the JSON can be parsed in place); offsets are from the image start

crc32 (zlib) covers everything after the header. The archive holds:

    recipes.json        data/recipes.json minus fields the device never shows
                        (sources, review notes, licence text id, icon paths)
    ingredients.json    data/ingredients.json
    images/detail/*.jpg, images/thumb/*.jpg   the photos (drinks without one get the
                        UI's generated glass icon, so the PNG icons are left out)
    speech/<id>.opk     the spoken recipe, Opus (format: adb/audio.py). Left out with
                        --no-audio; the UI hides its speaker button for a drink without one
"""

from __future__ import annotations

import argparse
import json
import struct
import zlib
from pathlib import Path

from .paths import DATA_DIR, REPO_DIR

MAGIC = b"ADBP"
VERSION = 1
HEADER = struct.Struct("<4sIII")
ENTRY = struct.Struct("<II56s")
PARTITION_SIZE = 0xCF0000  # firmware/partitions.csv
DROP_RECIPE_KEYS = ("sources", "review", "text_license")
DROP_IMAGE_KEYS = ("title", "icon_file", "icon_thumb")


def _compact(obj) -> bytes:
    return json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode()


def collect(data_dir: Path, audio: bool = False, verify: bool = False, device: str = "auto") -> dict[str, bytes]:
    recipes = json.loads((data_dir / "recipes.json").read_text())
    files: dict[str, bytes] = {}
    if audio:
        # Imported here so --no-audio never touches the TTS setup.
        from . import audio as speech_audio

        files.update(speech_audio.build(recipes["recipes"], verify=verify, device=device))
    for r in recipes["recipes"]:
        for k in DROP_RECIPE_KEYS:
            r.pop(k, None)
        img = r.get("image") or {}
        for k in DROP_IMAGE_KEYS:
            img.pop(k, None)
        if img.get("kind") == "photo":
            for k in ("file", "thumb"):
                files[img[k]] = (data_dir / img[k]).read_bytes()
        else:
            # The device draws its own icon; don't point at PNGs that aren't packed.
            img["file"] = img["thumb"] = None
    files["recipes.json"] = _compact(recipes)
    files["ingredients.json"] = _compact(json.loads((data_dir / "ingredients.json").read_text()))
    return files


def build(files: dict[str, bytes]) -> bytes:
    names = sorted(files, key=lambda n: n.encode())
    for n in names:
        if len(n.encode()) >= 56:
            raise SystemExit(f"pack: name too long for the 56-byte field: {n}")
    offset = HEADER.size + ENTRY.size * len(names)
    toc, blobs = [], []
    for n in names:
        data = files[n]
        offset = (offset + 3) & ~3
        toc.append(ENTRY.pack(offset, len(data), n.encode()))
        padded = data + b"\0" * (4 - len(data) % 4)  # >= 1 NUL, keeps 4-byte alignment
        blobs.append(padded)
        offset += len(padded)
    body = b"".join(toc) + b"".join(blobs)
    return HEADER.pack(MAGIC, VERSION, len(names), zlib.crc32(body)) + body


def say(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="build.py say", description="Play a drink's spoken recipe on the Mac")
    ap.add_argument("drink_id")
    ap.add_argument("--data", type=Path, default=DATA_DIR)
    ap.add_argument("--path", action="store_true", help="print the cached .opus file instead of playing it")
    args = ap.parse_args(argv)
    from . import audio as speech_audio

    return speech_audio.say(args.drink_id, args.data, path_only=args.path)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="build.py pack", description="Write the device recipe pack")
    ap.add_argument("-o", "--output", type=Path, default=REPO_DIR / "firmware" / "build" / "drinks.bin")
    ap.add_argument("--data", type=Path, default=DATA_DIR)
    ap.add_argument("--no-audio", action="store_true",
                    help="leave out the spoken recipes (no TTS venv, no opusenc; standard library only)")
    ap.add_argument("--verify", action="store_true",
                    help="transcribe every sentence with Whisper and re-record bad ones (see voice.yaml)")
    ap.add_argument("--device", choices=["auto", "mps", "cpu"], default="auto", help="torch device for the TTS")
    args = ap.parse_args(argv)
    if not args.no_audio:
        from . import audio as speech_audio

        speech_audio.need_opusenc()   # fail before any slow work
    files = collect(args.data, audio=not args.no_audio, verify=args.verify, device=args.device)
    image = build(files)
    if len(image) > PARTITION_SIZE:
        raise SystemExit(f"pack: {len(image)} bytes does not fit the {PARTITION_SIZE}-byte drinks partition")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    n_photos = sum(1 for n in files if n.startswith("images/detail/"))
    n_speech = sum(1 for n in files if n.startswith("speech/"))
    print(f"pack: {args.output} ({len(image) / 1024 / 1024:.2f} MB, {len(files)} files, "
          f"{n_photos} photos, {n_speech} spoken recipes, {100 * len(image) / PARTITION_SIZE:.0f}% of the "
          f"{PARTITION_SIZE / 1024 / 1024:.1f} MB partition)")
    return 0
