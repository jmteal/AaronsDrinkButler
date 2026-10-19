"""Spoken script for "speak the recipe": the sentences the TTS reads for one drink.

Standard library only (build.py pack imports it). Amounts are spoken in ounces, rounded
the way the device shows them (firmware/components/data/src/adb_db.c adb_format_amount):
quarter ounces, and teaspoons below 7.5 ml.

Each item of the returned list is one sentence; the TTS generates them one at a time and
joins them with short silences, because Parler-TTS drifts on long inputs. A sentence
ending in ":" (the ingredient list intro) gets the longer "section" pause after it.
"""

from __future__ import annotations

import re

ML_PER_OZ = 29.5735
ML_PER_TSP = 4.93

GLASS_NAMES = {
    "rocks": "a rocks glass", "double_rocks": "a double rocks glass", "coupe": "a coupe",
    "martini": "a martini glass", "highball": "a highball glass", "collins": "a Collins glass",
    "flute": "a flute", "wine": "a wine glass", "copper_mug": "a copper mug",
    "hurricane": "a hurricane glass", "tiki": "a tiki mug", "irish_coffee_mug": "a heatproof mug",
    "nick_and_nora": "a Nick and Nora glass", "shot": "a shot glass",
}

ONES = ["zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
        "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen",
        "nineteen"]
TENS = ["", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"]

# Count units: (singular, plural). Mass-like units read "a pinch of salt".
UNITS = {
    "dash": ("dash", "dashes"), "barspoon": ("barspoon", "barspoons"), "tsp": ("teaspoon", "teaspoons"),
    "cube": ("cube", "cubes"), "leaf": ("leaf", "leaves"), "pinch": ("pinch", "pinches"),
}
# Words a TTS mispronounces or spells out letter by letter.
SAY = [
    (r"\b7UP\b", "Seven Up"), (r"\bB-52\b", "B fifty-two"), (r"&", " and "), (r"\s*–\s*", " to "),
    (r"\bml\b", "milliliters"), (r"\boz\b", "ounces"), (r"\bIBA\b", "I B A"), (r"°C\b", " degrees"),
    (r"\bSt\.\s", "Saint "), (r"\bNo\.\s?(\d)", r"number \1"),
]


def words(n: int) -> str:
    if n < 20:
        return ONES[n]
    if n < 100:
        return TENS[n // 10] + ("-" + ONES[n % 10] if n % 10 else "")
    if n < 1000:
        rest = n % 100
        return ONES[n // 100] + " hundred" + (" and " + words(rest) if rest else "")
    return str(n)


def quarters(v: float) -> tuple[int, int]:
    q = round(v * 4)
    return q // 4, q % 4


def measure(v: float, unit: str, plural: str) -> str:
    """'half an ounce', 'one and a quarter ounces', 'three quarters of a teaspoon'."""
    whole, frac = quarters(v)
    art = "an" if unit[0] in "aeiou" else "a"
    if whole == 0:
        return {0: f"a little {unit}", 1: f"a quarter {unit}", 2: f"half {art} {unit}",
                3: f"three quarters of {art} {unit}"}[frac]
    head = words(whole)
    if frac:
        return f"{head} and {['', 'a quarter', 'a half', 'three quarters'][frac]} {plural}"
    return f"{head} {unit if whole == 1 else plural}"


def clean_name(display: str) -> str:
    """Ingredient display name as speakable text: no parentheticals, no stray brackets."""
    s = re.sub(r"\([^)]*\)", "", display)
    s = re.sub(r"\s*\(.*$", "", s)          # unclosed bracket ("Lemon-lime soda (7UP")
    return re.sub(r"\s+", " ", s).strip(" ,")


def lower_first(s: str, context: str = "") -> str:
    """Lower-case a leading common word ('Gin' -> 'gin'), keep brand and proper names
    (Campari, Angostura, Grand Marnier). A word the steps write in lower case is common."""
    w = s.split(" ", 1)[0]
    if w.isupper() or not w[:1].isupper():
        return s
    if w.lower() in COMMON or re.search(r"\b" + re.escape(w.lower()) + r"\b", context):
        return s[:1].lower() + s[1:]
    return s


COMMON = {"gin", "vodka", "sweet", "dry", "white", "dark", "fresh", "lime", "lemon", "orange", "simple",
          "sugar", "soda", "cola", "egg", "cream", "honey", "mint", "salt", "ginger", "aged", "light",
          "chilled", "splash", "rye", "bourbon", "blanco", "reposado", "coffee", "hot", "brown", "soft",
          "grapefruit", "pineapple", "cranberry", "tomato", "apple", "raspberry", "passion", "peach",
          "coconut", "cherry", "blue", "triple", "absinthe", "espresso", "agave", "maple", "cold", "tonic",
          "ginger", "prosecco", "water", "milk", "heavy", "whole", "elderflower", "watermelon",
          "strawberry", "cucumber", "basil", "grated", "freshly", "spiced", "overproof", "apricot",
          "melon", "banana", "vanilla", "chocolate", "green", "celery", "blended", "peated", "yellow", "red", "black"}


def ingredient_sentence(i: dict, context: str = "") -> str:
    name = clean_name(i["display"])
    amt, unit = i.get("amount"), i["unit"]
    low = lower_first(name, context)
    if re.match(r"^(a )?(splash|dash|drop|pinch) of ", low) and unit != "top":
        s = cap(low if low.startswith("a ") else "a " + low)   # "Splash of water" with a nominal amount
    elif unit in UNITS and UNITS[unit][0] in low.split():
        s = f"{cap(words(round(amt or 1)))} {low}"            # "Sugar cube", 1 cube -> "One sugar cube"
    elif unit == "top":
        low = re.sub(r"^(a )?splash of ", "", low)
        s = f"{cap(low)}, to top up"
    elif unit == "piece":
        if re.match(r"^\d", name):          # "1 lime, cut into small wedges"
            s = re.sub(r"^(\d+)(?:\s*-\s*\d+)?", lambda m: words(int(m.group(1))), name)
            s = cap(s)
        elif amt in (None, 1):
            s = cap(low) if low.split()[0] in ("a", "an", "some") else cap(("an " if low[0] in "aeiou" else "a ") + low)
        else:
            s = f"{cap(words(round(amt)))} {low}"
    elif unit in UNITS:
        one, many = UNITS[unit]
        if amt is None:
            s = f"{'An' if one[0] in 'aeiou' else 'A'} {one} of {low}"
        else:
            s = f"{cap(measure(amt, one, many))} of {low}"
    elif unit == "ml" and amt is not None:
        if amt < 7.5:
            tsp = max(0.5, round(amt / ML_PER_TSP * 2) / 2)
            s = f"{cap(measure(tsp, 'teaspoon', 'teaspoons'))} of {low}"
        else:
            s = f"{cap(measure(max(0.25, amt / ML_PER_OZ), 'ounce', 'ounces'))} of {low}"
    elif amt is not None:
        s = f"{cap(words(round(amt)))} {low}"
    else:
        s = cap(low)
    if i.get("optional"):
        s += ", if you like"
    return s + "."


def cap(s: str) -> str:
    return s[:1].upper() + s[1:]


def speakable(text: str) -> str:
    """Normalise one sentence for the TTS: numbers to words, symbols to words."""
    for pat, rep in SAY:
        text = re.sub(pat, rep, text)
    text = re.sub(r"(\d+)\s*-\s*(\d+)", r"\1 to \2", text)
    text = re.sub(r"\b(\d+)\b", lambda m: words(int(m.group(1))), text)
    return re.sub(r"\s+", " ", text).strip()


def split_step(step: str) -> list[str]:
    """A step as sentences: a trailing '(If ...)' aside becomes its own sentence; inline
    asides become commas; 'a; b' becomes two sentences."""
    out = []
    m = re.match(r"^(.*?[.!?])\s*\((.+?)\)\s*$", step)
    if m:
        step, aside = m.group(1), m.group(2)
    else:
        aside = None
    step = re.sub(r"\s*\(([^)]*)\)", r", \1,", step).replace(",,", ",").replace(", .", ".")
    step = re.sub(r",\s*([.!?])", r"\1", step)
    for part in re.split(r";\s*", step):
        part = part.strip()
        if part:
            out.append(cap(part) if part[-1] in ".!?" else cap(part) + ".")
    if aside:
        out.append(cap(aside.rstrip(".")) + ".")
    return out


def parts(r: dict) -> list[tuple[str, bool]]:
    """(sentence, section_break_after) for one recipe: name, glass, ingredients, steps,
    garnish. A section break gets the longer pause."""
    out = [(f"{r['name']}.", True)]
    glass = GLASS_NAMES.get(r["glass"], "a " + r["glass"].replace("_", " "))
    out.append((f"Serve it in {glass}.", False))
    out.append(("You will need:", True))
    context = " ".join(r["steps"])
    out += [(ingredient_sentence(i, context), False) for i in r["ingredients"]]
    if out[-1][0] != "You will need:":
        out[-1] = (out[-1][0], True)
    for st in r["steps"]:
        out += [(x, False) for x in split_step(st)]
    if r.get("garnish") and not any(x.lower().startswith("garnish") for x in r["steps"]):
        out.append((f"Garnish with {lower_first(clean_name(r['garnish']), 'a')}.", False))
    out.append(("Enjoy!", False))
    return [(speakable(t), brk) for t, brk in out]


def script(r: dict) -> list[str]:
    return [t for t, _ in parts(r)]


if __name__ == "__main__":
    import json
    import sys

    from .paths import DATA_DIR

    recipes = json.loads((DATA_DIR / "recipes.json").read_text())["recipes"]
    want = set(sys.argv[1:])
    for r in recipes:
        if not want or r["id"] in want:
            print(f"# {r['id']}")
            print("\n".join(script(r)))
