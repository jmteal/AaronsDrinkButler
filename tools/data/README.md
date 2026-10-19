# Recipe data pipeline

Builds `data/` (recipes, ingredients, photos, icons, attribution, review sheet) from our own
curated files. Runs on the Mac; the device only ever sees the output.

## Run it

```bash
cd tools/data
python3 -m venv .venv            # Python 3.11+ (tested with 3.13)
.venv/bin/pip install -r requirements.txt
.venv/bin/python build.py --offline   # rebuild from the cache (no network)
.venv/bin/python build.py             # fetch anything new, then build
```

Every network response (IBA pages, Wikidata, Wikipedia, Commons, TheCocktailDB, photo files)
is cached under `tools/data/.cache/` (git-ignored), so a rerun is offline and gives the same
output. `--offline` fails loudly on a cache miss instead of fetching. Requests send a
descriptive User-Agent and are throttled per host (1–1.5 s between calls).

A fresh clone has no cache, so the first `build.py` run fetches about 1,500 requests and takes
roughly 30–40 minutes because of the throttling.

## What you edit

| File | What it holds |
|---|---|
| `drinks_master.yaml` | **Source of truth.** Every drink: name, IBA category, glass, method, ice, garnish, ingredient specs, optional hand-written steps, Wikipedia title, spec doubts. Format notes are at the top of the file |
| `ingredients.yaml` | Normalized ingredients: name, category, ABV, base spirit, substitutes, pantry flags, aliases used to match reference data |
| `tags_proposed.yaml` | Flavor / season / occasion / colour **proposed by an LLM** (Claude), one line per drink |
| `tags_overrides.yaml` | Reviewed values; always win. Created by `build.py import-review` (or by hand) |
| `photo_overrides.yaml` | Per drink: a hand-picked Commons file, `none` to force the icon, `reject:` a bad pick, or `focus:` (0–1) to move the crop up or down |

## Stages (`build.py`)

1. **Load and validate** the master list and ingredient table (unknown ids, glasses, units fail the build).
2. **Wikidata** (CC0): resolve each drink's item from its Wikipedia title (or a cocktail-name index from SPARQL).
3. **Cross-check** (`adb/crosscheck.py`): IBA list membership and categories against iba-world.com and Wikipedia's list; our amounts against each IBA spec; ingredient sets against TheCocktailDB (test key `1`, dev only, nothing bundled) and Wikidata P186. Results go to `reports/REPORT.md`.
4. **Derive** (`adb/derive.py`): ABV estimate (dilution: shaken +25%, stirred +20%, built +10%, muddled +15%, blended +30%), strength, base spirit, alcoholic / low-ABV / mocktail, difficulty, and the objective flavors (bubbly, coffee, creamy, smoky). Then merge the proposed tags and any overrides.
5. **Steps** (`adb/steps.py`): short imperative steps generated from the structured spec, in our own words. A drink can supply its own `steps:` instead. Nothing is copied from IBA, Wikipedia or TheCocktailDB prose.
6. **Photos** (`adb/photos.py`): candidates from the override file, Wikidata P18, the Wikipedia lead image, the item's Commons category, then a Commons search whose file title must contain the drink name. Only **CC0, public domain, CC BY and CC BY-SA** pass (multi-licensed files may qualify through their license category); everything else is rejected and logged in `reports/photo_log.csv`. Accepted photos are centre-cropped and resized to a 320×240 baseline JPEG (q78) and a 96×96 JPEG (q80), with EXIF stripped.
7. **Icons** (`adb/icons.py`): a glass + liquid colour + ice + garnish drawing for every drink, 96×96 and 320×240 palette PNGs.
8. **Write** `data/recipes.json`, `data/ingredients.json`, `data/ATTRIBUTION.md`, `data/review/tags.csv` + `tags.html`, validate against `recipe.schema.json`, and write `reports/REPORT.md`.

## Reviewing tags

Open `data/review/tags.html` in a browser to scan the proposed tags with each drink's picture.
To correct them, edit `data/review/tags.csv`, put `yes` in the `reviewed` column of each row you
checked, then:

```bash
.venv/bin/python build.py import-review
.venv/bin/python build.py --offline
```

## Fixing a photo

If a photo shows the wrong drink or crops badly, add to `photo_overrides.yaml`:

```yaml
dirty_martini: none                              # use the icon
gimlet: {reject: ["File:Bad gimlet.jpg"]}        # skip this file, try the next candidate
negroni: {file: "File:Negroni served in Vintage Cocktail Glass.jpg", focus: 0.35}
```

A new or changed pick needs one online run (`build.py` without `--offline`) to fetch it.

## Licensing rules (the repo is public)

- Our steps, notes and tags: CC BY 4.0. Specs (ingredients and amounts) are facts.
- Photos: Commons only, allowlisted licenses only, each credited in `data/ATTRIBUTION.md` (title, author, license, source URL, "cropped and resized").
- TheCocktailDB and IBA are references only: no text and no images from them are bundled. The raw cache stays out of git.
