"""Generate short, imperative steps from a structured spec, in our own words.

This stands in for the "LLM drafts steps from the structured spec" stage in the plan: it
only ever sees our own ingredient list, glass, method and ice, never source prose. A drink
can replace the result with its own `steps:` in drinks_master.yaml.
"""

from __future__ import annotations

from .master import Ing

GLASS_NAMES = {
    "rocks": "rocks glass", "double_rocks": "double rocks glass", "coupe": "coupe",
    "martini": "martini glass", "highball": "highball glass", "collins": "Collins glass",
    "flute": "flute", "wine": "wine glass", "copper_mug": "copper mug",
    "hurricane": "hurricane glass", "tiki": "tiki mug", "irish_coffee_mug": "heatproof mug",
    "nick_and_nora": "Nick & Nora glass", "shot": "shot glass",
}
ICE_PHRASE = {
    "cubes": "over fresh ice", "large_cube": "over a large ice cube",
    "crushed": "filled with crushed ice", "blended": "", "none": "",
}
FILL_PHRASE = {"cubes": "ice", "large_cube": "a large ice cube", "crushed": "crushed ice"}
EGGS = {"egg_white", "egg_yolk", "whole_egg"}
HERB_LIKE = {"mint", "basil", "fresh_ginger", "chili_pepper", "strawberry", "watermelon",
             "cucumber", "pineapple", "lime", "lemon", "orange", "cloves"}


def join(names: list[str]) -> str:
    names = list(dict.fromkeys(names))
    if not names:
        return ""
    if len(names) == 1:
        return names[0]
    return ", ".join(names[:-1]) + " and " + names[-1]


def cap(s: str) -> str:
    return s[:1].upper() + s[1:]


NO_ARTICLE = {"a", "an", "the", "half", "three", "two", "3", "fresh", "grated", "candied", "some"}
PROPER = {"Champagne"}


def garnish_phrase(g: str, has_rim: bool = False) -> str | None:
    g = g.strip().rstrip(".")
    optional = "(optional)" in g.lower()
    g = g.replace(" (optional)", "").replace("(optional)", "").strip()
    # Split into "x, y and z" segments so we can drop a rim the steps already made.
    segs = [s.strip() for s in g.replace(" and ", ", ").split(",") if s.strip()]
    if has_rim:
        segs = [s for s in segs if "rim" not in s.lower()]
    if not segs:
        return None
    segs = [s if s.split()[0] in PROPER else s[0].lower() + s[1:] for s in segs]
    first = segs[0]
    words = first.split()
    plural = words[-1].endswith("s") and not words[-1].endswith("ss")
    if words[0] not in NO_ARTICLE and not plural:
        segs[0] = ("an " if first[0] in "aeiou" else "a ") + first
    text = join(segs)
    return f"Garnish with {text}{' if you like' if optional else ''}."


def generate(d: dict, ingredients: dict) -> list[str]:
    ings: list[Ing] = d["ings"]
    glass = GLASS_NAMES[d["glass"]]
    ice = d["ice"]
    method = d["method"]

    def name(i: Ing) -> str:
        return ingredients[i.ingredient_id]["short"]

    rim = [i for i in ings if "rim" in i.flags]
    rinse = [i for i in ings if "rinse" in i.flags]
    muddle = [i for i in ings if "muddle" in i.flags]
    top = [i for i in ings if i.unit == "top" and not ({"float", "sink", "side"} & i.flags)]
    float_ = [i for i in ings if "float" in i.flags]
    sink = [i for i in ings if "sink" in i.flags]
    foam = [i for i in ings if "foam" in i.flags]
    side = [i for i in ings if "side" in i.flags]
    special = {id(i) for i in rim + rinse + muddle + top + float_ + sink + foam + side}
    main = [i for i in ings if id(i) not in special]
    main_names = join([name(i) for i in main])
    all_mixed = join([name(i) for i in muddle + main])

    steps: list[str] = []
    if d.get("prep"):
        steps.append(d["prep"])
    for i in rim:
        opt = "If you like, " if i.optional else ""
        steps.append(cap(f"{opt}moisten the rim of the {glass} with a citrus wedge and dip it in {name(i)}."))
    chill = ice == "none" and method in ("shaken", "stirred")
    for i in rinse:
        steps.append(f"Rinse the{' chilled' if chill else ''} {glass} with {name(i)} and pour out the excess.")

    has_egg = any(i.ingredient_id in EGGS for i in main)
    egg_optional = has_egg and all(i.optional for i in main if i.ingredient_id in EGGS)
    fine = bool(muddle) or any(i.ingredient_id in HERB_LIKE for i in main)

    if method == "shaken":
        if muddle:
            steps.append(f"Gently muddle the {join([name(i) for i in muddle])} in a shaker.")
        if has_egg and not egg_optional:
            steps.append(f"Shake {main_names} without ice for 10 seconds to whip the egg.")
            steps.append("Add ice and shake again, hard, until very cold.")
        elif has_egg:
            steps.append(f"Shake {main_names} hard with ice for 10–15 seconds. "
                         "(If using egg white, shake once without ice first for a thicker foam.)")
        else:
            steps.append(f"{'Add' if muddle else 'Shake'} {main_names}"
                         f"{', then shake' if muddle else ''} hard with ice for 10–15 seconds.")
        strain = "Double-strain" if fine or has_egg else "Strain"
        if ice == "none":
            steps.append(f"{strain} into a chilled {glass}.")
        else:
            steps.append(f"{strain} into a {glass} {ICE_PHRASE[ice]}.")
    elif method == "stirred":
        if muddle:
            steps.append(f"Muddle the {join([name(i) for i in muddle])} in a mixing glass.")
        steps.append(f"{'Add' if muddle else 'Stir'} {main_names}"
                     f"{' and stir' if muddle else ''} with ice in a mixing glass for about 20 seconds.")
        if ice == "none":
            steps.append(f"Strain into a chilled {glass}.")
        else:
            steps.append(f"Strain into a {glass} {ICE_PHRASE[ice]}.")
    elif method == "built":
        if muddle:
            steps.append(f"Muddle the {join([name(i) for i in muddle])} in the {glass}.")
        if ice in FILL_PHRASE:
            steps.append(f"Fill the glass with {FILL_PHRASE[ice]}." if muddle or rim or rinse
                         else f"Fill a {glass} with {FILL_PHRASE[ice]}.")
            if main:
                steps.append(f"Add {main_names}.")
        elif main:
            steps.append(f"Pour {main_names} into a{' chilled' if d['glass'] in ('flute', 'coupe', 'martini') else ''} {glass}.")
    elif method == "muddled_built":
        steps.append(f"Muddle the {join([name(i) for i in muddle])} in a {glass}." if muddle
                     else f"Muddle the fruit in a {glass}.")
        fill = FILL_PHRASE.get(ice, "ice")
        steps.append(f"Fill the glass with {fill}" + (f" and add {main_names}." if main else "."))
    elif method == "blended":
        steps.append(f"Blend {all_mixed} with a cup of ice until smooth.")
        steps.append(f"Pour into a {glass}.")
    elif method == "layered":
        steps.append(f"Pour each ingredient slowly over the back of a bar spoon into a {glass} so it "
                     f"floats in its own layer, in this order: {main_names}.")
    elif method == "hot":
        steps.append(f"Warm a {glass} with hot water, then empty it.")
        steps.append(f"Add {main_names} and stir until any sugar dissolves.")

    if top:
        steps.append(f"Top with {join([name(i) for i in top])}.")
    for i in sink:
        steps.append(f"Slowly drizzle the {name(i)} over the top so it sinks down through the drink.")
    for i in float_:
        steps.append(f"Float the {name(i)} on top by pouring it slowly over the back of a spoon.")
    for i in foam:
        steps.append(f"Dot a few drops of {name(i)} on the foam.")
    if (top or (method == "built" and len(main) > 1)) and not float_ and not sink and method in ("built", "muddled_built"):
        steps.append("Stir gently to mix." if method == "built" else "Stir from the bottom to mix.")
    if method == "built" and top and not float_ and not sink:
        # "Top" step already followed by a stir; nothing else to add.
        pass
    for i in side:
        steps.append(f"Serve a small glass of {name(i)} on the side.")
    if d.get("garnish") and d.get("garnish_step", True):
        g = garnish_phrase(d["garnish"], has_rim=bool(rim))
        if g:
            steps.append(g)
    if d.get("finish"):
        steps.append(d["finish"])
    return steps
