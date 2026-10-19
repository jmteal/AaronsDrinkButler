"""Load and validate drinks_master.yaml and ingredients.yaml."""

from __future__ import annotations

import re
from dataclasses import dataclass, field

import yaml

from .paths import INGREDIENTS_YAML, MASTER_YAML

GLASSES = [
    "rocks", "double_rocks", "coupe", "martini", "highball", "collins", "flute", "wine",
    "copper_mug", "hurricane", "tiki", "irish_coffee_mug", "nick_and_nora", "shot",
]
METHODS = ["shaken", "stirred", "built", "blended", "layered", "muddled_built", "hot"]
ICE = ["none", "cubes", "large_cube", "crushed", "blended"]
UNITS = ["ml", "dash", "barspoon", "tsp", "cube", "piece", "leaf", "top", "pinch"]
FLAGS = ["opt", "muddle", "rim", "rinse", "float", "sink", "foam", "side"]
IBA_CATEGORIES = ["unforgettable", "contemporary_classic", "new_era"]
GARNISH_ICONS = [
    "none", "lime_wheel", "lime_wedge", "lemon_wheel", "lemon_twist", "orange_slice",
    "orange_twist", "cherry", "olive", "onion", "mint", "basil", "pineapple", "coffee_beans",
    "cream_float", "nutmeg", "cinnamon", "celery", "cucumber", "berries", "chili", "salt_rim",
    "sugar_rim", "passion_fruit", "ginger", "apple", "watermelon",
]
INGREDIENT_CATEGORIES = [
    "spirit", "liqueur", "wine", "mixer", "juice", "syrup", "bitters", "dairy", "egg", "garnish", "other",
]


@dataclass
class Ing:
    ingredient_id: str
    amount: float | None
    unit: str | None
    flags: set[str] = field(default_factory=set)
    display: str | None = None

    @property
    def optional(self) -> bool:
        return "opt" in self.flags


_NUM = re.compile(r"^-?\d+(\.\d+)?$")


def parse_ing(spec: str, ingredients: dict) -> Ing:
    """Parse "<amount> [unit] <id> [flags] [| display]"."""
    display = None
    if "|" in spec:
        spec, display = spec.split("|", 1)
        display = display.strip()
    toks = spec.split()
    if not toks:
        raise ValueError("empty ingredient spec")
    amt_tok = toks.pop(0)
    if amt_tok == "-":
        amount = None
    elif _NUM.match(amt_tok):
        amount = float(amt_tok)
        if amount.is_integer():
            amount = int(amount)
    else:
        raise ValueError(f"bad amount {amt_tok!r} in {spec!r}")
    unit = "ml"
    if toks and toks[0] in UNITS:
        unit = toks.pop(0)
    if not toks:
        raise ValueError(f"missing ingredient id in {spec!r}")
    iid = toks.pop(0)
    if iid not in ingredients:
        raise ValueError(f"unknown ingredient id {iid!r} in {spec!r}")
    flags = set(toks)
    bad = flags - set(FLAGS)
    if bad:
        raise ValueError(f"unknown flags {bad} in {spec!r}")
    return Ing(iid, amount, unit, flags, display)


def load_ingredients() -> dict[str, dict]:
    data = yaml.safe_load(INGREDIENTS_YAML.read_text())
    for iid, ing in data.items():
        if not re.fullmatch(r"[a-z0-9_]+", iid):
            raise ValueError(f"bad ingredient id {iid}")
        if ing.get("cat") not in INGREDIENT_CATEGORIES:
            raise ValueError(f"{iid}: bad category {ing.get('cat')}")
        ing.setdefault("abv", 0)
        ing.setdefault("sub", [])
        ing.setdefault("aliases", [])
        ing.setdefault("short", ing["name"][0].lower() + ing["name"][1:])
        for s in ing["sub"]:
            if s not in data:
                raise ValueError(f"{iid}: unknown substitute {s}")
    return data


def load_master(ingredients: dict) -> list[dict]:
    drinks = yaml.safe_load(MASTER_YAML.read_text())
    seen = set()
    errors = []
    for d in drinks:
        did = d.get("id", "?")
        try:
            if not re.fullmatch(r"[a-z0-9_]+", did):
                raise ValueError("bad id")
            if did in seen:
                raise ValueError("duplicate id")
            seen.add(did)
            for key, allowed in (("glass", GLASSES), ("method", METHODS), ("ice", ICE),
                                 ("garnish_icon", GARNISH_ICONS)):
                if d.get(key) not in allowed:
                    raise ValueError(f"{key}={d.get(key)!r} not in {allowed}")
            if d.get("iba") is not None and d["iba"] not in IBA_CATEGORIES:
                raise ValueError(f"bad iba category {d['iba']}")
            d["aka"] = d.get("aka") or []
            d["ings"] = [parse_ing(s, ingredients) for s in d["ing"]]
        except ValueError as e:
            errors.append(f"{did}: {e}")
    if errors:
        raise SystemExit("drinks_master.yaml errors:\n  " + "\n  ".join(errors))
    return drinks
