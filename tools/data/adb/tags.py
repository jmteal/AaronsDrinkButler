"""Proposed (LLM) tags, human overrides, and the review CSV round trip."""

from __future__ import annotations

import csv
import re

import yaml

from .paths import DATA_DIR, TAGS_OVERRIDES_YAML, TAGS_PROPOSED_YAML

FLAVORS = ["sweet", "sour", "bitter", "spirit_forward", "fruity", "herbal", "creamy", "spicy",
           "smoky", "bubbly", "coffee"]
SEASONS = ["spring", "summer", "fall", "winter", "holiday", "all_year"]
OCCASIONS = ["brunch", "party", "aperitif", "after_dinner", "nightcap", "date_night", "classic"]
REVIEW_CSV = DATA_DIR / "review" / "tags.csv"
# Columns a reviewer may edit; everything else in the CSV is for reference.
EDITABLE = ["flavor", "season", "occasion", "difficulty", "color_name", "color_hex", "base_spirit"]


def _split(s: str) -> list[str]:
    return [x.strip() for x in re.split(r"[,;/ ]+", s or "") if x.strip()]


def load_proposed() -> dict[str, dict]:
    raw = yaml.safe_load(TAGS_PROPOSED_YAML.read_text())["tags"]
    out = {}
    for did, line in raw.items():
        parts = [p.strip() for p in line.split("|")]
        if len(parts) != 4:
            raise SystemExit(f"tags_proposed.yaml: {did}: need 4 '|' fields")
        flav, seas, occ, color = parts
        cname, chex = color.rsplit(" ", 1)
        rec = {"flavor": _split(flav), "season": _split(seas), "occasion": _split(occ),
               "color": {"name": cname.strip(), "hex": chex.strip().upper()}}
        validate(did, rec)
        out[did] = rec
    return out


def load_overrides() -> dict[str, dict]:
    if not TAGS_OVERRIDES_YAML.exists():
        return {}
    data = yaml.safe_load(TAGS_OVERRIDES_YAML.read_text()) or {}
    for did, rec in data.items():
        validate(did, rec)
    return data


def validate(did: str, rec: dict) -> None:
    for key, allowed in (("flavor", FLAVORS), ("season", SEASONS), ("occasion", OCCASIONS)):
        bad = set(rec.get(key) or []) - set(allowed)
        if bad:
            raise SystemExit(f"{did}: unknown {key} values {sorted(bad)}")
    color = rec.get("color")
    if color and not re.fullmatch(r"#[0-9A-Fa-f]{6}", color.get("hex", "")):
        raise SystemExit(f"{did}: bad color hex {color}")
    if "difficulty" in rec and rec["difficulty"] not in (1, 2, 3):
        raise SystemExit(f"{did}: difficulty must be 1-3")


def import_review(recipes_by_id: dict[str, dict]) -> int:
    """Read data/review/tags.csv and write reviewed rows (reviewed=yes) to tags_overrides.yaml."""
    overrides = load_overrides()
    n = 0
    with REVIEW_CSV.open(newline="") as f:
        for row in csv.DictReader(f):
            if row.get("reviewed", "").strip().lower() not in ("y", "yes", "true", "1", "x"):
                continue
            did = row["id"]
            if did not in recipes_by_id:
                print(f"  skipping unknown id {did}")
                continue
            rec = {
                "flavor": _split(row["flavor"]),
                "season": _split(row["season"]),
                "occasion": [o for o in _split(row["occasion"]) if o != "classic"],
                "difficulty": int(row["difficulty"]),
                "color": {"name": row["color_name"].strip(), "hex": row["color_hex"].strip().upper()},
                "base_spirit": _split(row["base_spirit"]),
                "reviewed": True,
            }
            if row.get("notes", "").strip():
                rec["notes"] = row["notes"].strip()
            validate(did, rec)
            overrides[did] = rec
            n += 1
    header = ("# Human-reviewed tag overrides. These always win over tags_proposed.yaml and the\n"
              "# derived values. Written by `build.py import-review`; safe to edit by hand.\n")
    TAGS_OVERRIDES_YAML.write_text(header + yaml.safe_dump(overrides, sort_keys=True, allow_unicode=True))
    return n
