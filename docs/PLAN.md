# Aaron's Drink Butler — Project Plan (rev 2)

Status: **approved 2026-10-18**. Decisions on the open questions are in section 10.
Rev 2 switches to the **Hosyond ES3C35P** board (3.5", 320×480 portrait) and adds a **3D-printed battery-powered enclosure** for the Bambu Lab P2S.

Research: [board_es3c35p.md](research/board_es3c35p.md) · [enclosure.md](research/enclosure.md) · [drinks.md](research/drinks.md) · [sample_recipes.json](research/sample_recipes.json).
[board.md](research/board.md) covers the Waveshare 4.3" board and is **superseded**.

## 1. What we're building

A handheld or countertop cocktail browser in a printed case. It ships 150–250 recipes, each with a photo or a generated glass icon. You can filter by ingredient, spirit, flavor, season and more; see what you can make from your own bar; and save favorites. It runs on a 3.7 V 3000 mAh battery, charges over USB-C, and has a speaker for sound feedback.

## 2. Hardware (verified from LCDWiki schematic and drawings)

| Item | Detail |
|---|---|
| MCU | ESP32-S3R8 (chip on board): **8 MB octal PSRAM, 16 MB quad flash** (`qio_opi`) |
| Display | 3.5" IPS **320×480 portrait**, **ST77922** over QSPI (CS 10, SCLK 12, D0–D3 11/13/14/9), backlight GPIO41 (PWM-dimmable), TE GPIO42. LCD reset is tied to chip reset |
| Touch | Sitronix touch built into the panel at I2C **0x55** (SDA 38, SCL 39, RST 48, INT 47). Driver `esp_lcd_touch_st7123` (*not* FT6336G as in your PDF) |
| Color | Needs **byte swap** (`swap_bytes = true`) plus inversion. The PDF's ">>5 rotate" fix is wrong |
| Audio | **Onboard ES8311 codec + SC8002B amp**; amp enable GPIO1 (active low); I2S MCLK 17/BCLK 18/WS 21/DOUT 15/DIN 16; 1.25 mm 2-pin speaker socket; mic |
| Power | TP4054 charger (~290 mA, so 3000 mAh takes about 10–12 h to charge), battery voltage on GPIO8 (2:1 divider), 1.25 mm 2-pin battery socket. **No protection circuit, no power switch** |
| Storage | microSD (4-bit SDIO), optional. Everything fits in flash |
| Other | WS2812 LED GPIO40, BOOT GPIO0, native USB-C (no USB-serial chip) |
| Mechanical | PCB 54.5 × 101.5 × 1.6 mm, glass 54.5 × 83.0 mm (same width as PCB), ~10.5 mm total thickness, 4× M3 holes on a 47.9 × 94.5 mm pattern. USB-C on the bottom edge, microSD on the right edge, all sockets on the rear |

## 3. Key decisions

| Area | Decision |
|---|---|
| Firmware | **ESP-IDF 5.4/5.5 + LVGL 9.4** via `esp_lvgl_port`, `esp_lcd_st77922`, `esp_lcd_touch_st7123`, `esp_codec_dev`. Start from the working board code at github.com/andrewscherpbier/esp32-s3-display |
| Rendering | QSPI 40 MHz, two partial draw buffers in internal RAM, a 4-pixel area rounder; TE sync later as polish. Expect ~25–40 FPS scrolling |
| Data storage | All in 16 MB flash: ~3 MB app + ~12 MB read-only data partition. **No SD card needed** (the slot stays reachable for future expansion) |
| Images | Detail photo **320×240** JPEG q75–80 (~20–25 KB); thumbnail **96×96**, decoded into PSRAM at boot for smooth scrolling. 250 drinks ≈ 7.3 MB |
| Favorites / pantry | NVS bitsets keyed by stable drink IDs |
| Recipe data | Our own curated list: IBA's 102 official cocktails + classics + mocktails. Ingredient specs recorded as facts, instructions in our own words |
| Photos | Wikimedia Commons (CC0/PD/CC BY/CC BY-SA) with attribution. **Generated glass icon** for drinks with no photo (expect ~35–50% of drinks) |
| Power | Logo screensaver after 4 min; screen off after 10 min (light sleep, wake on touch to a logo); deep sleep after 30 more min (wake with BOOT or power switch). Inline power switch. Battery % in the status bar. About 20+ h of screen-on use at 40% brightness (estimate) |
| Sound | Short tap and "clink" effects through the onboard amp, with mute and volume settings |
| Enclosure CAD | **build123d** (Python, parametric); every measurement in one parameter file; outputs STL (as you asked) plus 3MF/STEP |
| Print material | **PETG or ASA**, not PLA: the board can reach 62 °C while charging |

## 4. App design (portrait 320×480)

Layout: a 24 px status bar at the top (time, battery %, mute icon), content in the middle, and a 56 px **bottom tab bar** with 4 tabs: **Home · Browse · My Bar · Favorites**. Touch targets are at least 44 px. Dark "bar" theme with warm accents and flat styles (no transparency in lists).

1. **Home**: a "Drink of the moment" card that changes with the season, plus big buttons: *What Can I Make?*, *Surprise Me*, *Seasonal Picks*, *Mocktails*, and a search field with an on-screen keyboard.
2. **Browse**: a one-column scrolling list (96 px rows: thumbnail, name, spirit, 2 flavor chips). A **Filter** button opens a full-screen filter sheet. Active filters show as removable chips above the list, with a live result count.
3. **Filter sheet**: collapsible sections for spirit, ingredients (include/exclude), flavor (sweet, sour, bitter, spirit-forward, fruity, herbal, creamy, spicy, smoky, bubbly, coffee), strength, season, occasion, glass, method, difficulty, alcoholic / low-ABV / mocktail.
4. **Drink detail**: a 320×240 photo at the top with a favorite heart; below it a scrolling section with the ingredient list (amounts, oz/ml toggle), steps, glass and garnish, and the photo credit.
5. **My Bar**: an ingredient checklist grouped by category. *What Can I Make?* then shows **Can make now**, **Missing 1** and **Missing 2**.
6. **Favorites**: the same list as Browse, filtered to hearts.
7. **Settings**: brightness, sleep timeout, sound/volume, units, battery details, attribution list, version.

## 5. Enclosure design

**Parts** (all parametric, regenerated from one `params.py`):
1. **Front shell**: the glass sits **flush** with the front face. Since the glass is no wider than the PCB, the shell locates the board by the 9.25 mm PCB strips above and below the glass, using solid bands 4.2 mm behind the face with **M3 heat-set inserts**. The screen opening is cut to the glass outline with a 0.15–0.2 mm gap.
2. **Rear cover**: screwed on, with a **speaker grille**, a **USB-C cutout** (bottom), a **microSD slot cutout** (right side), a **power switch cutout**, a slot for the WS2812 light, and a **hinged door over BOOT and RESET** with white labels inside (see `enclosure/README.md`).
3. **Battery + speaker bay**: your battery has the speaker mounted on it, and the pack measures **66 L × 35.5 W × 19.7 T mm** with the speaker included. Its 66 × 35.5 face sits **flat against the back of the board**, long side running along the board's 101.5 mm length and centred side to side, so it stays inside the board's footprint. The board's rear components stick out up to 4.7 mm, so a **1 mm printed wafer** sits between them and the battery (`enclosure/parts/wafer.py`; it replaced the stick-on pad first planned here). The wafer also shields the cell from board heat while charging (up to 62 °C). Ribs on the rear cover press the battery against the wafer, so no glue is needed.
   - The battery leaves about 9.5 mm free on each side, so the edge sockets stay reachable (SD on the right, battery and speaker sockets on the sides).
   - It also leaves about 35 mm of free length inside the case, used for cable routing and the power switch.
4. **Speaker grille**: the speaker sits on the battery pack and faces away from the board, so the grille goes in the **rear cover**, directly over the speaker. The pack's back face sits 0.5 mm from the rear wall; a thin foam gasket around the speaker seals it to the grille for louder, cleaner sound. The grille's position is a parameter, set once the speaker's position on the pack is measured.
5. **Desk stand**: a separate wedge that tilts the screen about 60° and raises the bottom 8–12 mm so the USB-C cable runs out the back. The device lifts off for handheld use.

**Depth stack (front to back):** glass/front face → PCB front 4.2 mm → PCB 1.6 mm → rear components 4.7 mm → wafer 1.0 mm → battery 19.7 mm → clearance 0.5 mm → rear wall 2.0 mm ≈ **33.7 mm**.

**Proposed size:** about **64 W × 112 H × 34 D mm**. The battery fixes the depth; the board fixes the width and height.

**Print:** P2S (256 mm build volume, 0.4 mm nozzle). Print the front shell **face down** for a clean surface. Optional two-colour accents using the AMS.

**Fit check first:** print a thin "fit coupon" (just the front frame and board pocket, about 20 min) before the full case.

**Calipers needed:** [enclosure.md](research/enclosure.md) lists 22 board measurements; the key ones are total thickness (10.0 vs 10.5), USB-C and SD socket heights, and the glass-to-PCB offset. Also check **where the tallest rear components are**, so the wafer and battery clear them.

## 6. Data pipeline (Mac only)

`tools/data/` (Python): curated drink list → cross-check against Wikidata/TheCocktailDB → normalize ingredients and substitutes → auto-derive spirit/ABV/glass/method → AI-proposed flavor/season/occasion tags → **you review** in a sheet → fetch Commons photos (licenses checked) → resize to 320×240 and 96×96, generate icons → pack the data partition image + `ATTRIBUTION.md`. All fetches are cached.

## 7. Repository layout

```
firmware/              ESP-IDF project
  components/bsp/        ST77922, touch, backlight PWM, audio, battery ADC, sleep
  components/data/       recipe index, filters, pantry, favorites (NVS)
  components/ui/         LVGL screens (shared with simulator)
simulator/             LVGL SDL desktop build (320×480)
tools/data/            data pipeline
data/                  built dataset, images, ATTRIBUTION.md
enclosure/             build123d source (params.py, parts), out/*.stl, *.3mf
docs/                  plan, research, assembly + wiring guide
```

## 8. Build phases and agents

Coding starts only after you approve this plan.

| Phase | Work | Agent | Runs |
|---|---|---|---|
| 0 | Repo skeleton, README, .gitignore, licenses | me | first |
| 1a | **Data pipeline + full dataset** (tags, photos, icons, attribution) | Data agent | in parallel |
| 1b | **Board bring-up**: display, touch, colors, backlight PWM, audio test tone, battery reading, sleep/wake test, partition layout | Firmware agent | in parallel |
| 1c | **UI in the simulator** with the 10 sample drinks: all screens at 320×480 | UI agent | in parallel |
| 1d | **Enclosure CAD**: parameters, fit coupon STL, full case STLs, stand | CAD agent | in parallel |
| — | **Checkpoint**: you review the tag sheet and simulator screenshots, take caliper measurements, print the fit coupon | you | |
| 2 | Integration on device: full dataset, JPEG decode + thumbnail cache, favorites/pantry, sounds, power management | Firmware + UI agents | after 1 |
| 2e | Enclosure rev 2 from your measurements and coupon fit → final STLs | CAD agent | in parallel with 2 |
| 3 | On-device tuning (scroll FPS, battery life), polish, assembly + wiring guide | Firmware agent | after 2 |
| 4 | Code review, tests, docs, flashing instructions | Review agent + me | last |

Phases 1a–1d each work in their own folder on their own branch, so they can run at the same time.

## 9. Parts list (beyond the board)

- 3.7 V 3000 mAh battery, **66 × 35.5 × 19.7 mm** (yours). It needs a **protection circuit** and a **1.25 mm 2-pin plug** (adapter or re-crimp; check polarity)
- Nothing to buy for the board-to-pack spacer: it's the printed 1 mm wafer. Optional Kapton tape on the PCB side
- Speaker: mounted on the battery pack (yours). It needs a 1.25 mm 2-pin plug for the board's speaker socket
- Mini slide switch (e.g. SS12D00) on the battery lead
- 4× M3 heat-set inserts (short) + 4× M3×8 screws; 4× M2 or M3 screws for the rear cover
- PETG or ASA filament; foam tape for the battery

## 10. Decisions (approved defaults)

1. **Battery + speaker pack**: 66 × 35.5 × 19.7 mm with the speaker included. The speaker faces the rear cover, so the grille goes there. **Still to measure**: where the speaker sits on the pack's back face, the plug types on its leads, and whether the battery has a protection circuit.
2. **Use**: both. The device works handheld, and a detachable desk stand holds it on the counter.
3. **Print**: PETG, one colour. Two-colour accents with the AMS are an optional extra.
4. **Number of drinks**: about 200 (102 IBA drinks + ~70 classics + ~25 mocktails).
5. **Tag review**: you review the AI-proposed tags at the checkpoint.
6. **Wi-Fi/Bluetooth**: off for v1, fully offline.
7. **Repo**: public on GitHub. **MIT** for code and enclosure, **CC BY 4.0** for our recipe text, and each photo under its own license as listed in `ATTRIBUTION.md`.
