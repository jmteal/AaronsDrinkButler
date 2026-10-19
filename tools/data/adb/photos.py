"""Pick, license-check, download and resize Wikimedia Commons photos.

Only CC0, public domain, CC BY and CC BY-SA (any version 1.0-4.0, ports included) pass.
Everything else is rejected and logged. Candidates, in priority order:
  1. photo_overrides.yaml (a hand-picked File: title, or "none" to force the icon)
  2. Wikidata P18 image of the drink's item
  3. English Wikipedia lead image of the drink's article
  4. files in the item's Commons category (P373)
  5. a Commons search for the drink name (the file title must contain the name)
Candidates 2-4 are only used when the Wikipedia article / Wikidata item belongs to this
drink alone (e.g. the Negroni article's photo is not reused for the Negroni Sbagliato).
"""

from __future__ import annotations

import io

import yaml
from PIL import Image, ImageOps

from . import http, wiki
from .paths import PHOTO_OVERRIDES_YAML

DETAIL = (320, 240)
THUMB = (96, 96)
MIN_SIDE = 300
OK_MIME = {"image/jpeg", "image/png", "image/webp"}


def load_overrides() -> dict:
    if not PHOTO_OVERRIDES_YAML.exists():
        return {}
    return yaml.safe_load(PHOTO_OVERRIDES_YAML.read_text()) or {}


def candidates(d: dict, ctx: dict, overrides: dict) -> list[tuple[str, str]]:
    ov = overrides.get(d["id"])
    rejected = set()
    if isinstance(ov, dict):
        rejected = {r if r.startswith("File:") else "File:" + r for r in ov.get("reject", [])}
        ov = ov.get("file")
    if ov == "none":
        return []
    out: list[tuple[str, str]] = []
    if ov:
        out.append((ov if ov.startswith("File:") else "File:" + ov, "override"))
    ent = ctx.get("entity")
    if ent and ctx.get("exclusive") and ctx.get("is_drink"):
        for f in wiki.claim_values(ent, "P18"):
            out.append(("File:" + f, "wikidata-P18"))
    if ctx.get("pageimage") and ctx.get("exclusive"):
        out.append(("File:" + ctx["pageimage"], "wikipedia-lead"))
    if ent and ctx.get("exclusive") and ctx.get("is_drink"):
        for cat in wiki.claim_values(ent, "P373")[:1]:
            for f in wiki.category_files(cat)[:12]:
                out.append((f, "commons-category"))
    names = [d["name"]] + d.get("aka", [])
    for n in names[:2]:
        q = n if d.get("alcoholic_hint") == "mocktail" else f"{n} cocktail"
        want = wiki.norm(n).split()
        for f in wiki.search_files(q):
            title = wiki.norm(f.rsplit(".", 1)[0].replace("File:", ""))
            if want and all(w in title.split() for w in want):
                out.append((f, "commons-search"))
    seen, uniq = set(), []
    for f, src in out:
        f = f.replace("_", " ")
        if f not in seen and f not in rejected:
            seen.add(f)
            uniq.append((f, src))
    return uniq


def choose(d: dict, cands: list[tuple[str, str]], infos: dict, log: list) -> dict | None:
    for title, src in cands:
        info = infos.get(title)
        if not info:
            log.append((d["id"], title, src, "rejected", "missing on Commons"))
            continue
        if info["mime"] not in OK_MIME:
            log.append((d["id"], title, src, "rejected", f"not a photo ({info['mime']})"))
            continue
        if min(info["width"] or 0, info["height"] or 0) < MIN_SIDE:
            log.append((d["id"], title, src, "rejected", "too small"))
            continue
        ok, lic, why = wiki.license_ok(info)
        if not ok:
            log.append((d["id"], title, src, "rejected", why))
            continue
        log.append((d["id"], title, src, "accepted", f"{lic} ({why})"))
        return {**info, "spdx": lic, "license_basis": why, "picked_from": src}
    return None


def download(info: dict) -> Image.Image:
    data = http.get_bytes("img", info["thumb"], "img")
    if data is None:
        raise RuntimeError(f"download failed: {info['thumb']}")
    im = Image.open(io.BytesIO(data))
    im = ImageOps.exif_transpose(im)
    return im.convert("RGB")


def fit(im: Image.Image, size: tuple[int, int], focus_y: float = 0.5) -> Image.Image:
    return ImageOps.fit(im, size, method=Image.LANCZOS, centering=(0.5, focus_y))


def save_jpeg(im: Image.Image, path, quality: int) -> None:
    im.save(path, "JPEG", quality=quality, optimize=True, progressive=False, subsampling="4:2:0")
