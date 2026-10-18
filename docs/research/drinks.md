# Aaron's Drink Butler: Recipe and Image Sourcing Research

Researched 2026-10-18. Anything marked **(unverified)** I could not confirm from a primary source.

---

## 1. Recommendation

1. **Build the recipe list ourselves, using public specs as references.** Start with the
   IBA official list (102 drinks), then add 50-150 well-known classics and modern drinks
   (Hot Toddy, Paloma, Aperol-style spritzes, mocktails and so on).
   - Ingredient names and amounts are facts. We can record them, citing IBA, Wikipedia
     and TheCocktailDB as references.
   - **Write every set of instructions in our own words.** Do not copy instruction text
     from any source.
2. **Use TheCocktailDB on the dev machine as a cross-check and gap-filler**, not as the
   bundled data source.
   - Its free test key is meant for development and education.
   - Its terms let you copy and modify what the API returns.
   - It doesn't grant a clear license for its images. Only 62 of the 441 drinks I sampled
     are marked `strCreativeCommonsConfirmed: Yes`, and even those don't name a specific
     CC license.
   - **Do not bundle its photos** unless they're individually confirmed. This is a personal
     build, but the firmware and repo are public.
3. **Get photos from Wikimedia Commons, through Wikidata.**
   - Each file has a machine-readable license (CC0, CC BY, CC BY-SA or public domain).
   - Wikidata itself is CC0. In my check, 149 of 397 cocktail items have an image (P18).
     Commons also has 70 per-drink subcategories under *IBA official cocktails*.
   - Expect real photos for **about 50-65% of a 200-drink list**.
4. **Give every drink a generated "glass + liquid colour" icon**, drawn from the glass
   type, colour and garnish. Use it when no photo is available, and as the
   small-thumbnail style in lists everywhere.
   - Icons are uniform, have no licensing risk and are tiny in flash.
   - Photos then appear only on the detail screen.
5. **Tag each recipe in three passes:**
   - Derive what can be derived directly: base spirit, method, glass, mocktail flag,
     estimated ABV.
   - Have an LLM propose subjective tags: flavour, season, occasion, difficulty.
   - Review all of them by hand in one CSV or YAML file, with the reviewed values stored
     as overrides.
6. **Generate an `ATTRIBUTION.md` and a per-image license file automatically** from the
   recipe records. Do not install any image whose license isn't on an allowlist.

---

## 2. Source survey

| Source | What it gives | License / terms | Can we bundle? |
|---|---|---|---|
| **TheCocktailDB** ([API](https://www.thecocktaildb.com/api.php), [Terms](https://www.thecocktaildb.com/terms_of_use.php)) | About 440+ drinks. Fields include `idDrink`, `strDrink`, `strCategory`, `strIBA`, `strAlcoholic`, `strGlass`, `strInstructions` (plus ES/DE/FR/IT/ZH), `strIngredient1-15`/`strMeasure1-15`, `strTags`, `strDrinkThumb`, `strImageSource`, `strImageAttribution`, `strCreativeCommonsConfirmed` and `dateModified`. Images come at `.../drink/<file>.jpg`, with `/small` (200px), `/medium` (350px) and `/large` (500px) sizes. | Test key `1` is for "development … or educational use". **Premium** costs a one-off $10 and is required to publish to an app store. It adds multi-ingredient filters, popular/latest/random lookups, and full lists (free lists stop at 100). Terms say you "can scrape, copy and modify any content returned from the API" through official endpoints, but must not scrape the website. They also say most artwork is user-created and you should link back. Rate limits aren't published **(unverified)**. | **Data: yes**, as a reference. Credit TheCocktailDB and link back. We should still rewrite the instructions, since many look like they came from elsewhere. **Images: only when `strCreativeCommonsConfirmed=Yes` and the `strImageSource` page confirms the license** (62 of 441 in my sample). Treat the rest as not licensed for redistribution. |
| **IBA official list** ([iba-world.com](https://iba-world.com/cocktails/all-cocktails/), [Wikipedia list](https://en.wikipedia.org/wiki/List_of_IBA_official_cocktails)) | 102 drinks in three groups of 34: *The Unforgettables*, *Contemporary Classics* and *New Era*. Gives ml specs, method and garnish. | The site footer says "© IBA … All rights reserved". There are no terms of use, and photos and text are copyrighted. | **Specs only**, as facts: names, amounts and method type. Do not copy the instruction text or photos. Use the three groups as a "classic" tag. |
| **GitHub IBA datasets**: [teijo/iba-cocktails](https://github.com/teijo/iba-cocktails), [ahcombs/iba-cocktails](https://github.com/ahcombs/iba-cocktails) | IBA specs as JSON or CSV, scraped from IBA and Wikipedia. | teijo has **no license**. ahcombs is **NOASSERTION**, meaning no recognised license (both checked through the GitHub API). | Use them only to cross-check our own entries. Don't vendor them. |
| **Wikipedia** articles | History, variations, and specs in the infobox (from `Template:Infobox cocktail`). | Text is **CC BY-SA 4.0**. Anything copied makes our derived text share-alike and requires attribution. | Use it as a fact reference. Don't copy prose. If we ever copy a description, attribute it and license that field CC BY-SA. |
| **Wikidata** ([SPARQL](https://query.wikidata.org/)) | Cocktail items (`Q134768` and subclasses; 397 found). Includes `P186` "made from material" (203 items), `P18` image (149 items), and links to Wikipedia and Commons. | **CC0**. | **Yes.** It's a good way to map names to Commons images and a basic ingredient list. |
| **Wikimedia Commons** ([IBA category](https://commons.wikimedia.org/wiki/Category:IBA_official_cocktails), [licensing](https://commons.wikimedia.org/wiki/Commons:Licensing)) | Photos of most famous cocktails. The IBA category has 70 per-drink subcategories. | Each file has its own license: CC0, PD, CC BY or CC BY-SA (2.0-4.0). The [Extmetadata API](https://www.mediawiki.org/wiki/API:Imageinfo) returns `LicenseShortName`, `Artist`, `AttributionRequired` and `LicenseUrl`. | **Yes, with attribution.** For CC BY and CC BY-SA, give title, author, source URL and license (TASL), and say the image was resized or cropped. A resized BY-SA image stays BY-SA, which doesn't affect the firmware's own license. |
| **Hugging Face** [erwanlc/cocktails_recipe](https://huggingface.co/datasets/erwanlc/cocktails_recipe) (and Russian MIT-labelled derivatives) | About 6,900 recipes scraped from Difford's Guide. | License listed as "other". The source is a copyrighted commercial site, so the MIT label on the derivatives doesn't cure that. | **No.** |
| **Kaggle**: Hotaling & Co. cocktails, "AI-first cocktail ingredients" (about 546 rows), and others | Scraped or company menus. | Licenses vary or aren't stated. One description says MIT **(unverified)**. | Not worth the risk. Skip them. |
| **Hugging Face** [Egrigor/Cocktails](https://huggingface.co/datasets/Egrigor/Cocktails) | 78 entries. | No license stated **(unverified)**. | No. |

**Bottom line on text.** Ingredient lists and amounts are generally not copyrightable, but the
written instructions, descriptions and photos are. For a clean public repo:

- Record specs as facts and keep source URLs as references.
- Write all instruction steps and descriptions ourselves. Short and imperative works best on
  an 800x480 screen anyway.
- Credit TheCocktailDB and IBA in `ATTRIBUTION.md`, as a courtesy and to follow
  TheCocktailDB's terms.

---

## 3. Image strategy

**Expected coverage for 200 drinks (estimates):**

| Tier | Source | Likely coverage |
|---|---|---|
| A | Commons photo found through Wikidata P18 or the Commons category, with a license on the allowlist | About 55-65% of IBA drinks. About 40-50% of the extended list. |
| B | TheCocktailDB photo with `strCreativeCommonsConfirmed=Yes` and the license confirmed on the source page | +5-10% |
| C | Our own photos. Aaron or James shoot the drinks they make, and we own them. | Whatever gets made. This is the best long-term option. |
| D | Generated icon (always produced) | 100% |

Overall, expect **real photos for about 50-65%** and an icon for the rest **(estimate)**.

**License allowlist:** CC0, PD-*, CC BY 2.0/3.0/4.0 and CC BY-SA 2.0/3.0/4.0.

- Reject NC, ND, "fair use", GFDL-only and anything without a license.
- Keep a per-image record of author, title, source URL, license, license URL and
  "modified: resized/cropped".

**Generated icon fallback:**

- Draw each icon from `glass` (about 12 silhouettes), `color` (liquid fill, plus an
  optional layer or ombre), `garnish_icon` (lime wheel, cherry, olive, mint, twist,
  umbrella) and an ice flag.
- Render it at build time to an indexed PNG or JPEG, or draw it at runtime with LVGL from
  vector primitives. Runtime drawing costs no flash per drink.
- Keep the style uniform so tier A and D drinks sit well together in lists.

**AI-generated photos are possible but not recommended.**

- Rendered drinks often look wrong (wrong glass or colour), and the provenance needs its
  own disclosure.
- If we use them, label them "illustration" and record the tool and its terms in
  `ATTRIBUTION.md`.

**Device sizing:**

- Store photos as JPEG at 320x320 (detail screen) and 120x120 (list), quality about 75.
  Expect about 15-25 KB plus 4 KB per drink, or **about 5 MB for 200 drinks**.
- That fits on a 16 MB flash (LittleFS) next to the firmware, or on a microSD card.
- Avoid storing raw RGB565: 320x320x2 is 200 KB per image.
- Decode with LVGL's TJpgDec/JPEG decoder or esp_jpeg.

---

## 4. Filter taxonomy

`D` means derived automatically from source data, `L` means an LLM proposes it and a
human reviews it, and `M` means it's set manually.

| Attribute | Values | How it's set |
|---|---|---|
| `base_spirit` (1-2 values) | gin, vodka, rum_white, rum_dark, tequila, mezcal, whiskey_bourbon, whiskey_rye, whiskey_scotch, whiskey_irish, brandy_cognac, pisco, cachaca, amaro_aperitif, wine_sparkling, liqueur, none | **D**: the largest-volume spirit after normalisation. Spot-check by hand. |
| `ingredients[].ingredient_id` | A normalised slug from `ingredients.json` (`lime_juice`, `simple_syrup`, `angostura_bitters`, `campari` …). Each ingredient has a `category` (spirit, liqueur, wine, mixer, juice, syrup, bitters, dairy, egg, garnish, other), `substitutes[]` and a `pantry_essential` flag. | **D**: an alias table maps "fresh lime juice" and "juice of 1 lime" to `lime_juice`. |
| **Pantry filter** ("what I have") | The user ticks ingredient IDs (or broad parents such as "any gin"). The filter shows *Can make now* (everything present), *Missing 1* and *Missing 2*. Ignore ice, water and garnish that's marked `optional`. Substitutes count as present. | Runs at runtime, using favourites-style storage in NVS. |
| `flavor[]` (multi) | sweet, sour, bitter, spirit_forward, fruity, herbal, creamy, spicy, smoky, bubbly, coffee | **L**, with rules such as citrus plus syrup giving "sour", or any amaro giving "bitter". Human review. |
| `strength` | light (<10% ABV in glass), medium (10-20%), strong (>20%), plus `abv_est` | **D**: sum of spirit volume × ABV, divided by total volume after dilution. Dilution is roughly +25% shaken, +20% stirred, +10% built over ice and +30% blended. |
| `season[]` | spring, summer, fall, winter, holiday, all_year | **L**/**M** |
| `occasion[]` | brunch, party, aperitif, after_dinner, nightcap, date_night, classic | **L**/**M**. Set "classic" with **D** from IBA membership. |
| `glass` | rocks, double_rocks, coupe, martini, highball, collins, flute, wine, copper_mug, hurricane, tiki, irish_coffee_mug, nick_and_nora, shot | **D**: map TheCocktailDB `strGlass` values to this list. |
| `method` | shaken, stirred, built, blended, layered, muddled_built, hot | **D**: keywords in the source instructions or IBA method. Review. |
| `difficulty` | 1 (pour and stir), 2 (shake or measure carefully), 3 (egg white, layering, homemade syrup, special kit) | **D** from method, ingredient count and special ingredients, then **M** override. |
| `color` | Hex code plus a name (amber, red, orange, pink, clear, cream, brown, green, blue, yellow, layered) | **L**/**M**. Also drives the icon. |
| `alcoholic` | alcoholic, mocktail, low_abv | **D**: TheCocktailDB `strAlcoholic`, or `abv_est`. |
| `iba_category` | unforgettable, contemporary_classic, new_era, null | **D** |
| `favorite` | Per-device runtime state, not part of the dataset. | Runtime |

UI tip: show four filter chips on screen (Spirit, Flavour, Season, "Can make now") and put
everything else under a "More" sheet. 800x480 has room for about 6 chips per row.

---

## 5. Recipe record schema

There's one JSON file per build (`recipes.json`), plus `ingredients.json` and
`ATTRIBUTION.md`. Amounts are stored in **ml** and converted to oz for display.

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "adb/recipe.schema.json",
  "type": "object",
  "required": ["id", "name", "ingredients", "steps", "glass", "method", "tags", "image", "sources"],
  "properties": {
    "id":   { "type": "string", "pattern": "^[a-z0-9_]+$" },
    "name": { "type": "string" },
    "aka":  { "type": "array", "items": { "type": "string" } },
    "ingredients": { "type": "array", "items": {
      "type": "object", "required": ["ingredient_id", "display"],
      "properties": {
        "ingredient_id": { "type": "string" },
        "display":  { "type": "string" },
        "amount":   { "type": ["number", "null"] },
        "unit":     { "enum": ["ml", "dash", "barspoon", "tsp", "cube", "piece", "leaf", "top", "pinch", null] },
        "optional": { "type": "boolean", "default": false }
      } } },
    "steps":   { "type": "array", "items": { "type": "string" }, "description": "Original wording, written by us" },
    "glass":   { "type": "string" },
    "method":  { "type": "string" },
    "ice":     { "enum": ["none", "cubes", "large_cube", "crushed", "blended"] },
    "garnish": { "type": ["string", "null"] },
    "garnish_icon": { "type": ["string", "null"] },
    "color":   { "type": "object", "properties": { "name": {"type": "string"}, "hex": {"type": "string"} } },
    "abv_est": { "type": "number", "description": "Estimated % ABV in glass after dilution" },
    "tags": { "type": "object", "properties": {
      "base_spirit": { "type": "array", "items": {"type": "string"} },
      "flavor":      { "type": "array", "items": {"type": "string"} },
      "strength":    { "enum": ["light", "medium", "strong"] },
      "season":      { "type": "array", "items": {"type": "string"} },
      "occasion":    { "type": "array", "items": {"type": "string"} },
      "difficulty":  { "enum": [1, 2, 3] },
      "alcoholic":   { "enum": ["alcoholic", "mocktail", "low_abv"] },
      "iba_category": { "enum": ["unforgettable", "contemporary_classic", "new_era", null] }
    } },
    "image": { "type": "object", "properties": {
      "file":   { "type": ["string", "null"] },
      "thumb":  { "type": ["string", "null"] },
      "kind":   { "enum": ["photo", "icon"] },
      "author": { "type": ["string", "null"] },
      "source_url":  { "type": ["string", "null"] },
      "license":     { "type": ["string", "null"], "description": "SPDX-like, e.g. CC-BY-SA-4.0, CC0-1.0" },
      "license_url": { "type": ["string", "null"] },
      "modified":    { "type": ["string", "null"] }
    } },
    "sources": { "type": "array", "items": { "type": "object",
      "properties": { "name": {"type": "string"}, "url": {"type": "string"}, "used_for": {"type": "string"} } } },
    "text_license": { "type": "string", "description": "License of our own steps/notes text" },
    "review": { "type": "object", "properties": { "tags_reviewed": {"type": "boolean"}, "notes": {"type": "string"} } }
  }
}
```

For the device, the build script can also write a compact form: short keys, tag bitmasks and
integer ingredient IDs. That makes RAM filtering fast. Keep the readable JSON as the source of
truth in the repo.

---

## 6. Sourcing pipeline (dev machine, Python)

```
tools/build_dataset/
  drinks.yaml          # our curated list: id, name, IBA flag, manual overrides (the source of truth)
  ingredients.yaml     # normalised ingredient table + alias map + substitutes
  build.py             # stages below; each stage caches to .cache/ so reruns are offline
```

1. **Seed.** Read `drinks.yaml` (our 150-250 picks).
2. **Fetch references** (cached, about 1 request per second, with a User-Agent):
   - TheCocktailDB `search.php?s=` for each drink, to get ingredients, measures, glass,
     the alcoholic flag and image metadata.
   - Wikidata SPARQL, to get the QID, P186 materials, P18 image and the enwiki title.
   - The Commons `imageinfo&iiprop=url|extmetadata` API, to get license, artist and URL.
3. **Normalise:**
   - Parse measures ("1 1/2 oz", "2 cl", "Juice of 1/2") into ml.
   - Map ingredient strings to `ingredient_id` using the alias table. Fail the build when an
     ingredient can't be mapped.
   - Map glass names. Check amounts against the IBA spec and flag differences.
4. **Write steps.** Keep a `steps:` field in `drinks.yaml`, written by us. An LLM can draft
   steps from the *structured* spec (not from the source prose), and a human edits them.
5. **Tag.** Apply the derivation rules from section 4.
   - Then make one batched LLM call per drink for flavour, season, occasion and colour hex.
     It returns JSON and is cached.
   - Merge in the manual overrides from `drinks.yaml`. Overrides always win.
6. **Images.** Pick, in priority order: our own photo, a Commons image on the allowlist, then
   a TheCocktailDB image with confirmed CC.
   - Download to `.cache/img/`, then centre-crop to a square with Pillow, resize to
     320 and 120 px, strip EXIF and save as baseline JPEG.
   - Always render the icon. Drop any image without a license on the allowlist.
7. **Validate.** Run the JSON Schema check, confirm every image has a license, and confirm
   there are no duplicate IDs. Report coverage (photo % and tag-review %).
8. **Emit:**
   - `data/recipes.json`, `data/ingredients.json` and `data/images/*.jpg`.
   - The compact device bundle (for example `drinks.bin` or minified JSON, for LittleFS).
   - **`ATTRIBUTION.md`**, which lists:
     - Recipe references (IBA, TheCocktailDB with a link back, Wikipedia/Wikidata).
     - Every image in TASL form ("*Negroni.jpg* by X, CC BY-SA 4.0, <url>, resized").
   - `LICENSES/` with the CC license texts or links. Show a short "Credits" screen on the
     device that points to the repo's `ATTRIBUTION.md`.

**Repo licensing.** Put the code under MIT or similar. Put our recipe text and tags under
**CC BY 4.0** or CC0. Images keep their own licenses, as listed in `ATTRIBUTION.md`.
Keep `.cache/` git-ignored so raw TheCocktailDB and Commons dumps aren't redistributed.

---

## 7. Samples

See `sample_recipes.json`, which has 10 records:

- Old Fashioned, Negroni, Daiquiri, Margarita, Whiskey Sour, Espresso Martini, Piña Colada,
  Irish Coffee, Mimosa and Shirley Temple (mocktail).
- Specs follow the IBA or common published ratios. The steps are our own wording.
- Image fields are placeholders, set to `kind: icon` until the pipeline picks a photo.
  `abv_est` values are rough estimates.

## Open questions / unverified

- The exact license behind TheCocktailDB's `strCreativeCommonsConfirmed=Yes` (which CC
  variant) needs checking per image on its `strImageSource` page.
- TheCocktailDB's free-tier rate limits aren't published.
- My 441-drink count came from first-letter search with the test key, so it may be incomplete.
- The `iba_category` values in the samples came from memory and weren't checked against the
  current IBA list. Espresso Martini, Irish Coffee and Mimosa are the least certain. The
  pipeline should take this field from IBA data.
- The photo coverage percentages are estimates. They'll be measured in pipeline step 7.
