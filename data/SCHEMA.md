# Dataset schema

`data/` is built by `tools/data/build.py`; don't edit the JSON by hand (change
`tools/data/drinks_master.yaml`, `ingredients.yaml`, `tags_proposed.yaml` or
`tags_overrides.yaml` and rebuild). All paths inside the JSON are relative to this folder.

| File | What |
|---|---|
| `recipes.json` | Every drink (`{"schema_version": 1, "recipes": [...]}`) |
| `ingredients.json` | Normalized ingredients for My Bar (`{"schema_version": 1, "categories": [...], "ingredients": [...]}`) |
| `images/detail/<id>.jpg` | Photo, 320×240 baseline JPEG, quality 78 (only drinks with a photo) |
| `images/thumb/<id>.jpg` | Photo, 96×96 baseline JPEG, quality 80 |
| `icons/detail/<id>.png` | Generated glass icon, 320×240 palette PNG (every drink) |
| `icons/thumb/<id>.png` | Generated glass icon, 96×96 palette PNG (every drink) |
| `ATTRIBUTION.md` | Credits for every photo (title, author, license, source, "cropped and resized") |
| `review/tags.csv`, `review/tags.html` | Review sheet for the LLM-proposed tags |

## recipes.json

The shape follows `docs/research/sample_recipes.json`. Changes from the sample are marked **new** or **changed**.

```jsonc
{
  "id": "negroni",                 // stable slug, ^[a-z0-9_]+$ (favorites/pantry key)
  "name": "Negroni",
  "aka": ["..."],                  // optional, other names (search)
  "ingredients": [
    {"ingredient_id": "gin", "display": "Gin", "amount": 30, "unit": "ml"},
    {"ingredient_id": "soda_water", "display": "Soda water", "amount": 60, "unit": "top"},
    {"ingredient_id": "salt", "display": "Salt for the rim", "amount": null, "unit": "pinch", "optional": true}
  ],
  "steps": ["Fill a rocks glass with ice.", "..."],   // our own words, CC BY 4.0
  "glass": "rocks",
  "method": "stirred",
  "ice": "cubes",
  "garnish": "Orange slice",       // null when none
  "garnish_icon": "orange_slice",
  "color": {"name": "red", "hex": "#A4161A"},
  "abv_est": 22.1,                 // estimated % ABV in the glass after dilution
  "tags": {
    "base_spirit": ["gin"],        // 1-2 values
    "flavor": ["bitter", "spirit_forward", "herbal"],
    "strength": "strong",
    "season": ["all_year"],
    "occasion": ["aperitif", "classic"],
    "difficulty": 1,
    "alcoholic": "alcoholic",
    "iba_category": "unforgettable"
  },
  "image": {
    "kind": "photo",               // "photo" or "icon"
    "file": "images/detail/negroni.jpg",   // what the detail screen shows (photo, else icon)
    "thumb": "images/thumb/negroni.jpg",   // what lists show (photo, else icon)
    "icon_file": "icons/detail/negroni.png",   // new: always present
    "icon_thumb": "icons/thumb/negroni.png",   // new: always present
    "title": "File:Negroni.jpg",    // new: Commons file title (null for icons)
    "author": "…", "source_url": "https://commons.wikimedia.org/wiki/File:…",
    "license": "CC-BY-SA-4.0", "license_url": "https://creativecommons.org/licenses/by-sa/4.0/",
    "modified": "cropped and resized"
  },
  "sources": [{"name": "IBA official cocktails", "url": "…", "used_for": "spec reference (facts)"}],
  "text_license": "CC-BY-4.0",
  "review": {"tags_reviewed": false, "notes": "…"}
}
```

### Amounts and units

`amount` is a number or `null`; `unit` is one of `ml`, `dash`, `barspoon`, `tsp`, `cube`,
`piece`, `leaf`, `top`, `pinch`. Amounts are stored in **ml**; convert to oz (÷ 30) for display.

**changed:** `unit: "top"` means "top up with". Its `amount` is an approximate ml figure used
for the ABV estimate; show it as "top" (or "top, about 60 ml"). `optional` is only present
when true.

### Enumerations

| Field | Values |
|---|---|
| `glass` | rocks, double_rocks, coupe, martini, highball, collins, flute, wine, copper_mug, hurricane, tiki, irish_coffee_mug, nick_and_nora, shot |
| `method` | shaken, stirred, built, blended, layered, muddled_built, hot |
| `ice` | none, cubes, large_cube, crushed, blended |
| `garnish_icon` | none, lime_wheel, lime_wedge, lemon_wheel, lemon_twist, orange_slice, orange_twist, cherry, olive, onion, mint, basil, pineapple, coffee_beans, cream_float, nutmeg, cinnamon, celery, cucumber, berries, chili, salt_rim, sugar_rim, passion_fruit, ginger, apple, watermelon |
| `tags.base_spirit` | gin, vodka, rum_white, rum_dark, tequila, mezcal, whiskey_bourbon, whiskey_rye, whiskey_scotch, whiskey_irish, brandy_cognac, pisco, cachaca, amaro_aperitif, wine_sparkling, **wine** (new: still or fortified wine, port, sherry), liqueur, none |
| `tags.flavor` | sweet, sour, bitter, spirit_forward, fruity, herbal, creamy, spicy, smoky, bubbly, coffee |
| `tags.strength` | light (< 10% ABV), medium (10-20%), strong (> 20%) |
| `tags.season` | spring, summer, fall, winter, holiday, all_year |
| `tags.occasion` | brunch, party, aperitif, after_dinner, nightcap, date_night, classic |
| `tags.difficulty` | 1 (pour and stir), 2 (shake, blend, muddle, measure carefully), 3 (egg, layering, rinse, homemade syrup) |
| `tags.alcoholic` | alcoholic, low_abv (< 10% ABV and led by wine, an aperitif or a liqueur), mocktail |
| `tags.iba_category` | unforgettable, contemporary_classic, new_era, null |

### How each field is set

- **Derived** by code (`tools/data/adb/derive.py`): `abv_est`, `strength`, `base_spirit`
  (the spirit contributing the most alcohol, or the top wine/aperitif/liqueur when there is no spirit; a second base is kept if it brings ≥ 60% as
  much), `alcoholic`, `difficulty`, and the objective flavors `bubbly`, `coffee`, `creamy`,
  `smoky`; `classic` is added to `occasion` for IBA drinks.
- **Proposed by an LLM** (`tools/data/tags_proposed.yaml`): `flavor`, `season`, `occasion`,
  `color`. Until a person reviews them, `review.tags_reviewed` is `false`.
- **Overrides** (`tools/data/tags_overrides.yaml`, filled from the review CSV) always win.

## ingredients.json

```jsonc
{
  "schema_version": 1,
  "categories": [{"id": "spirit", "label": "Spirits"}, "…"],   // display order for My Bar
  "ingredients": [
    {"id": "lime_juice", "name": "Lime juice", "category": "juice",
     "substitutes": [], "pantry_essential": true, "pantry_ignore": false,
     "abv": 0, "used_by": 61}
  ]
}
```

- `category`: spirit, liqueur, wine, mixer, juice, syrup, bitters, dairy, egg, garnish, other.
  **Note:** `garnish` also holds fresh produce that is a real ingredient (mint, basil, lime and
  lemon pieces, strawberries, cucumber, watermelon, pineapple, ginger, chili), so pantry
  matching should not skip that category; use `pantry_ignore` instead.
- `pantry_ignore`: true for water and hot water (never ask the user about these).
- `substitutes`: ingredient ids that count as "present" for this one in the pantry check.
- `used_by`: number of recipes using it (only used ingredients are listed).
