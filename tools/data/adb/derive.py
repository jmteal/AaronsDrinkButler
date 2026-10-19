"""Derived (D) attributes: ABV estimate, strength, base spirit, alcoholic class, difficulty,
and the objective flavor tags (bubbly, coffee, creamy, smoky)."""

from __future__ import annotations

from .master import Ing

# Approximate ml per unit, for the ABV estimate only.
UNIT_ML = {"ml": 1, "dash": 0.8, "barspoon": 5, "tsp": 5, "cube": 0, "piece": 0, "leaf": 0,
           "top": 1, "pinch": 0}
PIECE_ML = {"whole_egg": 45}
# Water added by ice, as a fraction of the mixed volume.
DILUTION = {"shaken": 0.25, "stirred": 0.20, "built": 0.10, "muddled_built": 0.15,
            "blended": 0.30, "layered": 0.0, "hot": 0.0}
LOW_ABV_MAX = 10.0
SPIRIT_BASES = {"gin", "vodka", "rum_white", "rum_dark", "tequila", "mezcal", "whiskey_bourbon",
                "whiskey_rye", "whiskey_scotch", "whiskey_irish", "brandy_cognac", "pisco", "cachaca"}
SPECIAL_INGREDIENTS = {"raspberry_syrup", "ginger_syrup", "cinnamon_syrup", "chamomile_syrup",
                       "strawberry_puree", "peach_puree"}
EGGS = {"egg_white", "egg_yolk", "whole_egg"}
CARBONATED = {"soda_water", "tonic_water", "ginger_beer", "ginger_ale", "cola", "lemon_lime_soda",
              "grapefruit_soda", "sparkling_wine"}
COFFEE = {"espresso", "coffee_hot", "coffee_liqueur"}
CREAMY = {"heavy_cream", "milk", "coconut_cream", "irish_cream", "butter", "whole_egg", "egg_yolk"}
SMOKY = {"mezcal", "islay_scotch"}


def volume(i: Ing) -> float:
    if i.amount is None:
        return 0.0
    if i.unit == "piece":
        return PIECE_ML.get(i.ingredient_id, 0) * i.amount
    return UNIT_ML[i.unit] * i.amount


def counted(d: dict) -> list[Ing]:
    """Ingredients that end up in the glass (not optional, not on the side, not a rim)."""
    return [i for i in d["ings"] if not i.optional and not ({"side", "rim"} & i.flags)]


def abv_estimate(d: dict, ingredients: dict) -> float:
    total = alcohol = 0.0
    for i in counted(d):
        v = volume(i)
        if "rinse" in i.flags:
            v = min(v, 1.0)
        total += v
        alcohol += v * ingredients[i.ingredient_id]["abv"] / 100
    if total == 0:
        return 0.0
    dil = DILUTION[d["method"]]
    if d["method"] == "built" and d["ice"] == "none":
        dil = 0.0
    return round(100 * alcohol / (total * (1 + dil)), 1)


def strength(abv: float) -> str:
    if abv < 10:
        return "light"
    if abv <= 20:
        return "medium"
    return "strong"


def alcoholic(abv: float, bases: list[str]) -> str:
    """low_abv = under 10% and led by wine, an aperitif or a liqueur (not a spirit)."""
    if abv == 0:
        return "mocktail"
    if abv < LOW_ABV_MAX and bases[0] not in SPIRIT_BASES:
        return "low_abv"
    return "alcoholic"


def base_spirits(d: dict, ingredients: dict) -> list[str]:
    """Rank by alcohol contributed; keep a second base if it brings at least 60% as much."""
    score: dict[str, float] = {}
    for i in counted(d):
        ing = ingredients[i.ingredient_id]
        if not ing.get("base") or ing["abv"] == 0:
            continue
        score[ing["base"]] = score.get(ing["base"], 0) + volume(i) * ing["abv"]
    if not score:
        return ["none"]
    # A spirit leads if it brings at least 40% of the top score (a Negroni is a gin drink, not
    # a Campari drink), but a splash of brandy doesn't turn Sangria into a brandy drink.
    top = max(score.values())
    ranked = sorted(score.items(), key=lambda kv: (not (kv[0] in SPIRIT_BASES and kv[1] >= 0.4 * top), -kv[1]))
    out = [ranked[0][0]]
    if len(ranked) > 1 and ranked[1][1] >= 0.6 * ranked[0][1]:
        out.append(ranked[1][0])
    return out


def difficulty(d: dict) -> int:
    ings = counted(d)
    base = {"built": 1, "stirred": 1, "hot": 2, "muddled_built": 2, "blended": 2,
            "shaken": 2, "layered": 3}[d["method"]]
    if d["method"] == "layered" or any("rinse" in i.flags for i in d["ings"]):
        return 3
    if any(i.ingredient_id in EGGS for i in ings):
        return 3
    score = base
    if any(i.ingredient_id in SPECIAL_INGREDIENTS for i in ings):
        score += 1
    if len(ings) >= 7:
        score += 1
    if base == 1 and any("float" in i.flags for i in ings):
        score += 1
    return min(score, 3)


def objective_flavors(d: dict) -> list[str]:
    ids = {i.ingredient_id for i in counted(d)}
    tops = {i.ingredient_id for i in counted(d) if volume(i) >= 30}
    out = []
    if tops & CARBONATED:
        out.append("bubbly")
    if ids & COFFEE:
        out.append("coffee")
    if ids & CREAMY:
        out.append("creamy")
    if ids & SMOKY:
        out.append("smoky")
    return out
