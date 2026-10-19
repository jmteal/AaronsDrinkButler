"""Cross-check our specs against IBA, TheCocktailDB and Wikidata, and write a report.

Only facts are compared (which ingredients, how much). The report lists names and amounts;
it never quotes method text from any source.
"""

from __future__ import annotations

import re

from . import cocktaildb, iba, wiki
from .derive import volume
from .wiki import norm

IGNORE = {"water", "hot_water", "salt", "lime", "lemon", "orange", "cherry", "olive", "celery",
          "mint", "nutmeg", "cocktail_onion", "pineapple", "strawberry", "cucumber", "cinnamon"}
IGNORE_NAMES = {"ice", "ice cubes", "crushed ice", "garnish", "maraschino cherry", "cherry", "lime peel",
                "lemon peel", "orange peel", "orange spiral", "lime wedge", "lemon wedge", "olive", "salt",
                "nutmeg", "mint", "water", "orange slice", "lemon slice", "lime slice"}
UNIT_ML = {"oz": 30, "cl": 10, "ml": 1, "tsp": 5, "tblsp": 15, "tbsp": 15, "shot": 45, "jigger": 45,
           "dash": 0.8, "dashes": 0.8, "cup": 240, "splash": 15}


def alias_map(ingredients: dict) -> dict[str, str]:
    m = {}
    for iid, ing in ingredients.items():
        for a in [iid.replace("_", " "), ing["name"], ing["short"], *ing["aliases"]]:
            m.setdefault(norm(a), iid)
    return m


def map_name(name: str, amap: dict) -> str | None:
    n = norm(re.sub(r"\b(fresh(ly)?|squeezed|chilled|homemade|premium|good|cold|hot)\b", " ", name, flags=re.I))
    if n in amap:
        return amap[n]
    n2 = norm(name)
    return amap.get(n2)


def parse_measure(m: str) -> float | None:
    m = m.lower().strip()
    if not m:
        return None
    num = 0.0
    found = re.findall(r"(\d+)\s+(\d+)/(\d+)|(\d+)/(\d+)|(\d+(?:\.\d+)?)", m)
    if not found:
        return None
    a = found[0]
    if a[0]:
        num = int(a[0]) + int(a[1]) / int(a[2])
    elif a[3]:
        num = int(a[3]) / int(a[4])
    else:
        num = float(a[5])
    for u, f in UNIT_ML.items():
        if re.search(rf"\b{u}\b", m):
            return num * f
    return None


def run(drinks: list[dict], ingredients: dict, entities: dict) -> dict:
    amap = alias_map(ingredients)
    out = {"iba_list": {}, "iba": {}, "cocktaildb": {}, "wikidata": {}, "cdb_ids": {}}

    # --- IBA list membership
    site = iba.fetch_list()
    ours = {d.get("iba_slug") or d["id"].replace("_", "-"): d for d in drinks if d.get("iba")}
    out["iba_list"]["site_count"] = len(site)
    out["iba_list"]["missing_from_ours"] = sorted(s for s in site if s not in ours)
    out["iba_list"]["not_on_site"] = sorted(s for s in ours if s not in site)
    out["iba_list"]["category_mismatch"] = sorted(
        (s, ours[s]["iba"], site[s]["category"]) for s in ours if s in site and ours[s]["iba"] != site[s]["category"])
    wp_names = iba.fetch_wikipedia_names()
    our_names = {norm(n) for d in drinks if d.get("iba") for n in [d["name"], *d["aka"]]}
    out["iba_list"]["wikipedia_count"] = len(wp_names)
    out["iba_list"]["wikipedia_not_ours"] = sorted(n for n in wp_names if norm(n) not in our_names)

    # --- IBA specs
    for slug, d in ours.items():
        spec = iba.fetch_spec(slug)
        if not spec:
            continue
        issues = []
        theirs = {}
        for i in spec["ingredients"]:
            iid = map_name(i["name"], amap)
            if iid is None:
                # try the trailing words ("Bitter Campari" -> "campari")
                words = norm(i["name"]).split()
                for k in range(1, len(words)):
                    iid = amap.get(" ".join(words[k:]))
                    if iid:
                        break
            if iid is None:
                issues.append(f"unmapped IBA ingredient '{i['raw']}'")
                continue
            ml = i["amount"] if i["unit"] == "ml" and i["amount"] else None
            if iid in theirs and theirs[iid] and ml:
                ml += theirs[iid]  # e.g. two sherries in the Sherry Cobbler
            theirs[iid] = ml
        mine = {}
        for i in d["ings"]:
            v = volume(i) if i.unit not in ("top",) else i.amount
            mine[i.ingredient_id] = (mine.get(i.ingredient_id) or 0) + (v or 0)

        def related(a, b):
            return a == b or b in ingredients[a]["sub"] or a in ingredients[b]["sub"]

        for iid, ml in theirs.items():
            hit = iid if iid in mine else next((x for x in mine if related(iid, x) and x not in theirs), None)
            if hit is None:
                if iid not in IGNORE:
                    issues.append(f"IBA has {iid}, we don't")
                continue
            if ml and mine[hit] and abs(ml - mine[hit]) > max(2.5, 0.12 * ml):
                issues.append(f"{hit}: IBA {ml:g} ml vs ours {mine[hit]:g}")
        for iid in mine:
            if not any(related(iid, x) for x in theirs) and iid not in IGNORE and not any(
                    i.optional for i in d["ings"] if i.ingredient_id == iid):
                issues.append(f"we have {iid}, IBA doesn't")
        out["iba"][d["id"]] = issues

    # --- TheCocktailDB
    for d in drinks:
        cd = cocktaildb.find([d["name"], *d["aka"]])
        if not cd:
            out["cocktaildb"][d["id"]] = None
            continue
        out["cdb_ids"][d["id"]] = cocktaildb.page_url(cd)
        theirs, unmapped = set(), []
        for name, _measure in cocktaildb.ingredients(cd):
            if norm(name) in IGNORE_NAMES:
                continue
            iid = map_name(name, amap)
            if iid:
                theirs.add(iid)
            else:
                unmapped.append(name)
        mine = {i.ingredient_id for i in d["ings"] if not i.optional}
        mine_ext = mine | {s for x in mine for s in ingredients[x]["sub"]}
        theirs_ext = theirs | {s for x in theirs for s in ingredients[x]["sub"]}
        out["cocktaildb"][d["id"]] = {
            "missing": sorted(x for x in theirs if x not in mine_ext and x not in IGNORE),
            "extra": sorted(x for x in mine if x not in theirs_ext and x not in IGNORE),
            "unmapped": unmapped,
            "alcoholic": cd.get("strAlcoholic"),
            "glass": cd.get("strGlass"),
        }

    # --- Wikidata P186 "made from material"
    mats = {}
    for d in drinks:
        e = entities.get(d["id"])
        if e and e.get("_exclusive"):
            mats[d["id"]] = wiki.claim_values(e, "P186")
    labels = wiki.labels_for(sorted({q for v in mats.values() for q in v}))
    for did, qs in mats.items():
        d = next(x for x in drinks if x["id"] == did)
        mine = {i.ingredient_id for i in d["ings"]}
        mine_ext = mine | {s for x in mine for s in ingredients[x]["sub"]}
        missing, unmapped = [], []
        for q in qs:
            lab = labels.get(q, q)
            iid = map_name(lab, amap)
            if iid is None:
                unmapped.append(lab)
            elif iid not in mine_ext and iid not in IGNORE:
                missing.append(iid)
        out["wikidata"][did] = {"missing": missing, "unmapped": unmapped}
    return out
