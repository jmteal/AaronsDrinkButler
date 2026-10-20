#!/usr/bin/env python3
"""Build the Aaron's Drink Butler dataset into data/.

    python build.py                 # full build (fetches anything not yet cached)
    python build.py --offline       # full build from tools/data/.cache only
    python build.py import-review   # copy reviewed rows of data/review/tags.csv into tags_overrides.yaml
    python build.py pack [-o FILE]  # device recipe pack from data/ (default firmware/build/drinks.bin),
                                    #   with spoken recipes (sets up Parler-TTS on first run)
    python build.py pack --no-audio # the same without speech (standard library only)
    python build.py pack --verify   # also check every sentence with Whisper, re-record bad ones
    python build.py say DRINK_ID    # play a drink's spoken recipe on the Mac
    python build.py say DRINK_ID --path  # print its cached .opus file instead

See README.md for details.
"""

from __future__ import annotations

import argparse
import csv
import os
import shutil
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

if __name__ == "__main__" and sys.argv[1:2] in (["pack"], ["say"]):
    # The pack only reads data/, so it runs on a plain python3 without the pipeline's packages.
    # Speech runs torch in its own venv (adb/audio.py), so this process stays standard library.
    from adb import pack  # noqa: E402

    sys.exit((pack.main if sys.argv[1] == "pack" else pack.say)(sys.argv[2:]))

from adb import crosscheck, derive, emit, http, icons, master, photos, steps, tags, wiki  # noqa: E402
from adb.paths import DATA_DIR, REPORTS_DIR  # noqa: E402

WIKI_URL = "https://en.wikipedia.org/wiki/"


def resolve_entities(drinks: list[dict]) -> dict[str, dict]:
    """drink id -> Wikidata entity, flagged _exclusive when the item is this drink's alone."""
    by_title = wiki.entities_by_enwiki([d["wiki"] for d in drinks if d.get("wiki")])
    qids = [d["qid"] for d in drinks if d.get("qid")]
    by_qid = wiki.entities_by_qid(qids) if qids else {}
    index = None
    out: dict[str, dict] = {}
    for d in drinks:
        e = None
        if d.get("qid"):
            e = by_qid.get(d["qid"])
        elif d.get("wiki"):
            e = by_title.get(d["wiki"])
        else:
            if index is None:
                index = wiki.cocktail_index()
            names = {wiki.norm(n) for n in [d["name"], *d["aka"]]}
            hits = [x["qid"] for x in index if names & {wiki.norm(n) for n in x["names"]}]
            if hits:
                e = wiki.entities_by_qid(hits[:1]).get(hits[0])
        if e:
            out[d["id"]] = dict(e)
    groups: dict[str, list[str]] = {}
    for did, e in out.items():
        groups.setdefault(e["id"], []).append(did)
    by_id = {d["id"]: d for d in drinks}
    for qid, dids in groups.items():
        for did in dids:
            e = out[did]
            d = by_id[did]
            names = {wiki.norm(n) for n in [d["name"], *d["aka"]]}
            lab = wiki.norm(wiki.label(e) or "")
            title = wiki.norm(e.get("sitelinks", {}).get("enwiki", {}).get("title", ""))
            e["_exclusive"] = len(dids) == 1 or bool(names & {lab, title})
            p31 = set(wiki.claim_values(e, "P31")) | set(wiki.claim_values(e, "P279"))
            e["_is_drink"] = not (p31 & wiki.NOT_DRINK_CLASSES)
    return out


def build_recipe(d, ingredients, proposed, overrides, entity, cdb_url):
    ings_out = []
    for i in d["ings"]:
        rec = {"ingredient_id": i.ingredient_id,
               "display": i.display or ingredients[i.ingredient_id]["name"],
               "amount": i.amount, "unit": i.unit}
        if i.optional:
            rec["optional"] = True
        ings_out.append(rec)
    abv = derive.abv_estimate(d, ingredients)
    p = proposed.get(d["id"])
    if p is None:
        raise SystemExit(f"tags_proposed.yaml has no entry for {d['id']}")
    ov = overrides.get(d["id"], {})
    flavor = list(dict.fromkeys(ov.get("flavor") or (p["flavor"] + [f for f in derive.objective_flavors(d)
                                                                     if f not in p["flavor"]])))
    occasion = list(ov.get("occasion") or p["occasion"])
    if d.get("iba") and "classic" not in occasion:
        occasion.append("classic")
    bases = derive.base_spirits(d, ingredients)
    alcoholic = d.get("alcoholic") or derive.alcoholic(abv, bases)
    sources = []
    if d.get("iba"):
        slug = d.get("iba_slug") or d["id"].replace("_", "-")
        sources.append({"name": "IBA official cocktails", "url": f"https://iba-world.com/iba-cocktail/{slug}/",
                        "used_for": "spec reference (facts)"})
    if d.get("wiki"):
        sources.append({"name": "Wikipedia", "url": WIKI_URL + d["wiki"].replace(" ", "_"),
                        "used_for": "spec cross-check (facts)"})
    if entity:
        sources.append({"name": "Wikidata", "url": f"https://www.wikidata.org/wiki/{entity['id']}",
                        "used_for": "identifiers and ingredient cross-check (CC0)"})
    if cdb_url:
        sources.append({"name": "TheCocktailDB", "url": cdb_url, "used_for": "spec cross-check (facts)"})
    notes = ["Flavor/season/occasion/colour tags proposed by an LLM; not yet reviewed." if not ov.get("reviewed")
             else "Tags reviewed."]
    if d.get("doubt"):
        notes.append("Spec note: " + d["doubt"])
    if ov.get("notes"):
        notes.append(ov["notes"])
    return {
        "id": d["id"],
        "name": d["name"],
        **({"aka": d["aka"]} if d["aka"] else {}),
        "ingredients": ings_out,
        "steps": d.get("steps") or steps.generate(d, ingredients),
        "glass": d["glass"],
        "method": d["method"],
        "ice": d["ice"],
        "garnish": d.get("garnish"),
        "garnish_icon": d["garnish_icon"],
        "color": ov.get("color") or p["color"],
        "abv_est": abv,
        "tags": {
            "base_spirit": ov.get("base_spirit") or bases,
            "flavor": flavor,
            "strength": derive.strength(abv),
            "season": list(ov.get("season") or p["season"]),
            "occasion": occasion,
            "difficulty": ov.get("difficulty") or derive.difficulty(d),
            "alcoholic": alcoholic,
            "iba_category": d.get("iba"),
        },
        "image": {},
        "sources": sources,
        "text_license": emit.TEXT_LICENSE,
        "review": {"tags_reviewed": bool(ov.get("reviewed")), "notes": " ".join(notes)},
    }


def clean_dir(p: Path) -> None:
    if p.exists():
        shutil.rmtree(p)
    p.mkdir(parents=True)


def human(n: int) -> str:
    return f"{n / 1024 / 1024:.2f} MB" if n > 1024 * 1024 else f"{n / 1024:.0f} KB"


def dir_size(p: Path) -> int:
    return sum(f.stat().st_size for f in p.rglob("*") if f.is_file())


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("command", nargs="?", default="all", choices=["all", "import-review"])
    ap.add_argument("--offline", action="store_true", help="use only tools/data/.cache (fail on a miss)")
    ap.add_argument("--no-photos", action="store_true", help="skip photo lookup (icons only)")
    args = ap.parse_args()
    if args.offline:
        os.environ["ADB_OFFLINE"] = "1"

    ingredients = master.load_ingredients()
    drinks = master.load_master(ingredients)

    if args.command == "import-review":
        n = tags.import_review({d["id"]: d for d in drinks})
        print(f"imported {n} reviewed rows into tags_overrides.yaml; now run build.py again")
        return

    proposed = tags.load_proposed()
    overrides = tags.load_overrides()
    extra = sorted(set(proposed) - {d["id"] for d in drinks})
    if extra:
        raise SystemExit(f"tags_proposed.yaml has unknown ids: {extra}")

    print(f"[1/6] {len(drinks)} drinks, {len(ingredients)} ingredients loaded")
    entities = resolve_entities(drinks)
    print(f"[2/6] Wikidata items for {len(entities)} drinks")
    cc = crosscheck.run(drinks, ingredients, entities)
    print("[3/6] cross-check done (IBA, TheCocktailDB, Wikidata)")

    recipes = [build_recipe(d, ingredients, proposed, overrides, entities.get(d["id"]),
                            cc["cdb_ids"].get(d["id"])) for d in drinks]
    by_id = {r["id"]: r for r in recipes}

    # ---- photos
    img_detail, img_thumb = DATA_DIR / "images" / "detail", DATA_DIR / "images" / "thumb"
    ic_detail, ic_thumb = DATA_DIR / "icons" / "detail", DATA_DIR / "icons" / "thumb"
    for p in (img_detail, img_thumb, ic_detail, ic_thumb):
        clean_dir(p)
    photo_log: list[tuple] = []
    chosen: dict[str, dict] = {}
    if not args.no_photos:
        pov = photos.load_overrides()
        titles_needed = [d["wiki"] for d in drinks if d.get("wiki") and entities.get(d["id"], {}).get("_exclusive")]
        pageimages = wiki.page_images(titles_needed)
        cands = {}
        for d in drinks:
            e = entities.get(d["id"])
            ctx = {"entity": e, "exclusive": bool(e and e["_exclusive"]) if e else bool(d.get("wiki")),
                   "is_drink": bool(e and e["_is_drink"]),
                   "pageimage": pageimages.get(d.get("wiki", ""))}
            d["alcoholic_hint"] = by_id[d["id"]]["tags"]["alcoholic"]
            cands[d["id"]] = photos.candidates(d, ctx, pov)
        infos = wiki.file_info([t for c in cands.values() for t, _ in c])
        for d in drinks:
            pick = photos.choose(d, cands[d["id"]], infos, photo_log)
            if not pick:
                continue
            ov = pov.get(d["id"]) if isinstance(pov.get(d["id"]), dict) else {}
            im = photos.download(pick)
            photos.save_jpeg(photos.fit(im, photos.DETAIL, ov.get("focus", 0.5)), img_detail / f"{d['id']}.jpg", 78)
            photos.save_jpeg(photos.fit(im, photos.THUMB, ov.get("focus", 0.5)), img_thumb / f"{d['id']}.jpg", 80)
            chosen[d["id"]] = pick
    print(f"[4/6] photos: {len(chosen)} of {len(drinks)}")

    # ---- icons and image records
    for d in drinks:
        r = by_id[d["id"]]
        d["color"] = r["color"]
        flags = icons.icon_flags(d, derive.objective_flavors(d))
        icons.save_png(icons.render(d, flags, 96, 96), ic_thumb / f"{d['id']}.png")
        icons.save_png(icons.render(d, flags, 320, 240), ic_detail / f"{d['id']}.png", colors=96)
        icon_file, icon_thumb = f"icons/detail/{d['id']}.png", f"icons/thumb/{d['id']}.png"
        p = chosen.get(d["id"])
        if p:
            r["image"] = {
                "file": f"images/detail/{d['id']}.jpg", "thumb": f"images/thumb/{d['id']}.jpg",
                "kind": "photo", "title": p["title"], "author": p["author"] or "Unknown",
                "source_url": p["desc_url"] or wiki.commons_page_url(p["title"]),
                "license": p["spdx"], "license_url": wiki.license_url_for(p["spdx"], p.get("license_url")),
                "modified": "cropped and resized",
                "icon_file": icon_file, "icon_thumb": icon_thumb,
            }
        else:
            r["image"] = {
                "file": icon_file, "thumb": icon_thumb, "kind": "icon", "title": None, "author": None,
                "source_url": None, "license": None, "license_url": None, "modified": None,
                "icon_file": icon_file, "icon_thumb": icon_thumb,
            }
    print("[5/6] icons drawn")

    # ---- write outputs
    emit.write_json(DATA_DIR / "recipes.json", {"schema_version": 1, "recipes": recipes})
    ing_json = emit.ingredients_json(ingredients, recipes)
    validate(recipes, ing_json)
    emit.write_json(DATA_DIR / "ingredients.json", ing_json)
    (DATA_DIR / "ATTRIBUTION.md").write_text(emit.attribution_md(recipes))
    emit.review_csv(DATA_DIR / "review" / "tags.csv", recipes)
    emit.review_html(DATA_DIR / "review" / "tags.html", recipes)
    REPORTS_DIR.mkdir(exist_ok=True)
    with (REPORTS_DIR / "photo_log.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["drink", "file", "candidate_source", "result", "reason"])
        w.writerows(photo_log)
    write_reports(drinks, recipes, cc, entities, chosen, photo_log)
    print(f"[6/6] wrote data/ (http: {http.stats['fetches']} fetched, {http.stats['hits']} from cache)")


def validate(recipes: list[dict], ing_json: dict) -> None:
    """JSON Schema check plus the cross-file checks a schema can't express."""
    import json

    import jsonschema

    schema = json.loads((Path(__file__).parent / "recipe.schema.json").read_text())
    jsonschema.validate({"schema_version": 1, "recipes": recipes}, schema)
    known = {i["id"] for i in ing_json["ingredients"]}
    errors = []
    ids = [r["id"] for r in recipes]
    if len(ids) != len(set(ids)):
        errors.append("duplicate recipe ids")
    for r in recipes:
        for i in r["ingredients"]:
            if i["ingredient_id"] not in known:
                errors.append(f"{r['id']}: ingredient {i['ingredient_id']} missing from ingredients.json")
        for key in ("file", "thumb", "icon_file", "icon_thumb"):
            if not (DATA_DIR / r["image"][key]).exists():
                errors.append(f"{r['id']}: missing {r['image'][key]}")
        if r["image"]["kind"] == "photo" and not r["image"]["license_url"]:
            errors.append(f"{r['id']}: photo without license URL")
    if errors:
        raise SystemExit("validation failed:\n  " + "\n  ".join(errors))


def write_reports(drinks, recipes, cc, entities, chosen, photo_log) -> None:
    n = len(recipes)
    by_kind = Counter(r["tags"]["alcoholic"] for r in recipes)
    iba_n = sum(1 for d in drinks if d.get("iba"))
    img_bytes = dir_size(DATA_DIR / "images")
    icon_bytes = dir_size(DATA_DIR / "icons")
    json_bytes = sum((DATA_DIR / f).stat().st_size for f in ("recipes.json", "ingredients.json"))
    det = sorted((f.stat().st_size for f in (DATA_DIR / "images" / "detail").glob("*.jpg")))
    iba_photo = sum(1 for d in drinks if d.get("iba") and d["id"] in chosen)
    lic = Counter(c["spdx"] for c in chosen.values())
    src = Counter(c["picked_from"] for c in chosen.values())
    rejected = Counter(r[4].split(":")[0] for r in photo_log if r[3] == "rejected")

    L = ["# Data build report", "", "Generated by `tools/data/build.py`.", "",
         "## Summary", "",
         f"- Drinks: **{n}** ({iba_n} IBA, {n - iba_n - by_kind['mocktail']} other classics, "
         f"{by_kind['mocktail']} mocktails; {by_kind['low_abv']} low-ABV)",
         f"- Photo coverage: **{len(chosen)}/{n} = {100 * len(chosen) / n:.0f}%** "
         f"(IBA drinks: {iba_photo}/{iba_n} = {100 * iba_photo / max(1, iba_n):.0f}%); every drink also has an icon",
         f"- Photo licenses: " + ", ".join(f"{k} {v}" for k, v in lic.most_common()),
         f"- Photo picked from: " + ", ".join(f"{k} {v}" for k, v in src.most_common()),
         f"- Rejected candidates by reason: " + ", ".join(f"{k} {v}" for k, v in rejected.most_common(8)),
         f"- Sizes: photos {human(img_bytes)}, icons {human(icon_bytes)}, "
         f"**images total {human(img_bytes + icon_bytes)}**; JSON {human(json_bytes)}",
         f"- Detail JPEG size: median {human(det[len(det) // 2]) if det else '-'}, max {human(det[-1]) if det else '-'}",
         f"- Wikidata items matched: {len(entities)}/{n}; TheCocktailDB matches: "
         f"{sum(1 for v in cc['cocktaildb'].values() if v)}/{n}",
         "", "## IBA list check", "",
         f"- IBA site lists {cc['iba_list']['site_count']} drinks; ours has {iba_n} marked IBA.",
         f"- On the IBA site but not in ours: {cc['iba_list']['missing_from_ours'] or 'none'}",
         f"- Marked IBA in ours but not on the site: {cc['iba_list']['not_on_site'] or 'none'}",
         f"- Category mismatches: {cc['iba_list']['category_mismatch'] or 'none'}",
         f"- Wikipedia's list ({cc['iba_list']['wikipedia_count']} names, it lags the 2024 update) names not matched to ours: "
         f"{cc['iba_list']['wikipedia_not_ours'] or 'none'}",
         "", "## Specs flagged as doubtful", "",
         "Our own notes (`doubt:` in drinks_master.yaml):", ""]
    for d in drinks:
        if d.get("doubt"):
            L.append(f"- **{d['name']}**: {d['doubt']}")
    L += ["", "Differences from the IBA spec (after allowing listed substitutes):", ""]
    for did, issues in cc["iba"].items():
        if issues:
            L.append(f"- **{did}**: " + "; ".join(issues))
    L += ["", "TheCocktailDB disagreements (ingredient sets only; TheCocktailDB is often looser, so treat as hints):", ""]
    for did, v in cc["cocktaildb"].items():
        if v and (v["missing"] or v["extra"]):
            bits = []
            if v["missing"]:
                bits.append("they add " + ", ".join(v["missing"]))
            if v["extra"]:
                bits.append("we add " + ", ".join(v["extra"]))
            if v["unmapped"]:
                bits.append("their names we could not map: " + ", ".join(v["unmapped"]))
            L.append(f"- {did}: " + "; ".join(bits))
    L += ["", "Wikidata P186 (made from material) not in our recipe:", ""]
    for did, v in cc["wikidata"].items():
        if v["missing"]:
            L.append(f"- {did}: " + ", ".join(v["missing"]))
    L += ["", "## Drinks without a photo (icon only)", "",
          ", ".join(r["name"] for r in recipes if r["image"]["kind"] == "icon"), ""]
    (REPORTS_DIR / "REPORT.md").write_text("\n".join(L) + "\n")


if __name__ == "__main__":
    main()
