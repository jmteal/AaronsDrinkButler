# Aaron's Drink Butler

An offline, battery-powered touchscreen cocktail browser. It has about 200 recipes with photos, filters by ingredient, spirit, flavor and season, a "what can I make?" pantry, and favorites. It runs on a Hosyond ES3C35P (ESP32-S3, 3.5" 320×480 touch display) in a 3D-printed case for the Bambu Lab P2S.

See [docs/PLAN.md](docs/PLAN.md) for the full plan.

## Layout

| Folder | Contents |
|---|---|
| `firmware/` | ESP-IDF + LVGL 9 firmware |
| `simulator/` | LVGL SDL desktop build of the UI (320×480) |
| `tools/data/` | Python pipeline that builds the recipe dataset |
| `data/` | Built dataset, images, `ATTRIBUTION.md` |
| `enclosure/` | build123d case source and STL/3MF output |
| `docs/` | Plan and research |

## Licenses

- Code and enclosure: [MIT](LICENSE)
- Our recipe text: CC BY 4.0
- Photos: each under its own license, listed in `data/ATTRIBUTION.md`
