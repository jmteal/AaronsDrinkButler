# Enclosure

A parametric build123d case for the Hosyond ES3C35P board and the 66 × 35.5 × 19.7 mm battery + speaker pack, printed in PETG on a Bambu Lab P2S.

**Outside size:** 64 W × 112 H × 33.7 D mm (the stand adds a 68.6 × 60.1 mm footprint).

| ![front](out/previews/iso_front.png) | ![back](out/previews/iso_back.png) |
|---|---|
| ![section](out/previews/section_center.png) | ![exploded](out/previews/exploded.png) |
| ![screw section](out/previews/section_screw.png) | ![stand](out/previews/on_stand.png) |

## Files

| Path | What it is |
|---|---|
| `params.py` | Every dimension. Each one is tagged `[drawing]`, `[plan]`, `[assumed]` (measure it) or `[design]` |
| `parts/front_shell.py` | Flush glass window, board pocket, locating bands with 4 insert holes, walls, USB-C and microSD openings |
| `parts/rear_cover.py` | Back plate, locating lip, 4 clamp standoffs, pack ribs, speaker grille, switch slot and cradle, LED light-pipe tube, RESET/BOOT poke tubes |
| `parts/stand.py` | Detachable 60° desk stand |
| `parts/fit_coupon.py` | The first 6.8 mm of the front shell, for a fit test |
| `parts/refs.py` | Simple boxes for the board, pad, pack, USB plug and cable channels (validation and previews only) |
| `build.py` | Writes `out/<part>.stl`, `.3mf` and `.step` in print orientation, plus `out/assembly.step` |
| `validate.py` | 50 checks (see below), then renders `out/previews/*.png` |

## Setup

build123d needs the OCP (OpenCascade) wheels, which don't support the system Python 3.14 yet. Use a Python 3.13 (or 3.12) venv:

```bash
python3.13 -m venv enclosure/.venv
```

```bash
enclosure/.venv/bin/pip install -r enclosure/requirements.txt
```

On macOS, Homebrew's `python@3.13` provides `python3.13`. `uv venv -p 3.13 enclosure/.venv` works too. The venv is git-ignored.

## Build and check

```bash
enclosure/.venv/bin/python enclosure/build.py
```

```bash
enclosure/.venv/bin/python enclosure/validate.py
```

The build takes about 6 s. Validation takes about 25 s, including the previews; add `--no-previews` to skip them. Validation exits non-zero if any check fails. It checks:

- **Solids.** Every part is one valid solid.
- **Build volume.** Every part fits the 256 mm build volume, lying flat on the plate in its print orientation.
- **Interference.** Neither printed part overlaps the glass, PCB, rear-parts envelope, pad or pack, and the two parts don't overlap each other. The pack is checked in its installed position, pushed 0.3 mm forward by the ribs.
- **Clearances and contacts.**
  - Glass-to-window gap: 0.2 mm.
  - PCB-to-pocket gap: 0.2 mm.
  - The PCB touches the bands, the standoffs touch the PCB pads, and the ribs touch the pack.
  - The board can slide in from the back.
- **Fasteners.** Front skin in front of the inserts, screw tip depth, thread engagement and head recess.
- **Ports and cable space.** The USB-C overmold and the microSD path are clear, and the left and right cable channels are free.
- **Stand.** The device rests on the stand without overlap, its bottom is 8–12 mm up, and the USB-C plug clears both the stand and the counter.

## Design notes

- **Datum and flushness.**
  - The glass is exactly as wide as the PCB, so the PCB front face bears on two solid bands over the 9.25 mm strips above and below the glass, `GLASS_TO_PCB` (4.2 mm) behind the case face.
  - The window is the glass outline plus `GLASS_GAP` (0.2 mm) per side, with a 0.4 mm chamfer at the face.
  - `GLASS_FLUSH` lets the glass stand slightly proud if a print comes out recessed.
- **One set of screws clamps everything.** Four **M3 × 30 button-head** screws go through the rear cover's standoffs and the PCB holes into **M3 × 3 mm heat-set inserts** in the front bands.
  - The standoffs (Ø5.4) bear only on the Ø5.6 pad rings and press the PCB onto the bands.
  - The cover stops 0.2 mm short of the shell rim (`SEAM_GAP`), so the screws always clamp the board rather than the seam.
  - The ribs also push the pack and pad against the board, so the board stays seated even with the cover off.
- **Why this differs from PLAN.md §9.**
  - The plan had separate board screws (M3 × 8) and rear-cover screws. The bands are only 4.2 mm thick, so an insert can be at most 3 mm long to keep 1.2 mm of front skin. An M3 × 8 would come out through the front face.
  - Separate rear-cover bosses would have to sit outside the board outline, and at 64 mm wide the corners only leave about 1.3 mm of wall around a Ø4 insert.
  - Through-bolting avoids both problems and cuts the fastener list to 4 inserts and 4 screws.
- **Inside.**
  - The pack is centred on the board (`PACK_CY = 0`), leaving 17.75 mm free above and below it.
  - Each side has a cable channel for the battery lead (left socket) and the speaker lead (right socket).
  - The switch sits top-left (seen from the front), away from the stand.
- **Speaker grille.** 55 hexagonal holes (1.6 mm across flats, 1.0 mm webs) in a Ø22 circle. It's centred on the pack until you measure the speaker; then set `GRILLE_CX`, `GRILLE_CY` and `GRILLE_D`. The ribs keep a ring clear around the grille for a foam gasket.
- **Stand.**
  - Two wedge cheeks with side fences, joined by a cross bar.
  - The middle is open: the USB-C plug hangs from the bottom face, and the cable runs back along the counter through the pointed notch in the bar.
  - `STAND_ANGLE` and `STAND_LIFT` are parameters.

## Printing (PETG, Bambu Lab P2S, 0.4 mm nozzle)

Start from Bambu Studio's **Generic PETG** (or your brand's) profile on the **textured PEI** plate, then change:

| Setting | Value |
|---|---|
| Layer height | 0.20 mm (0.16 mm for the front shell if you want finer side walls) |
| Wall loops | 5 (makes the 2.0 mm walls solid) |
| Top / bottom shell layers | 5 / 5 |
| Infill | 20% gyroid. The stand can use 15% |
| Nozzle / bed | 240–250 °C / 70–80 °C |
| Part cooling fan | 30–50%, and up to 80% for the cover's standoffs |
| Supports | **Off** for every part |
| Elephant foot compensation | 0.15 mm |
| X-Y hole compensation | 0, then tune from the coupon |
| Brim | None (the stand can take a 3 mm brim if it lifts) |

| Part | Orientation (as exported) | Notes |
|---|---|---|
| `fit_coupon` | Face down | About 14 cm³; roughly 25–30 min. Print it first |
| `front_shell` | **Face down** on the plate | Flat, textured front. The USB-C top is a 12.5 mm bridge; everything else is vertical or 45° |
| `rear_cover` | Outside face down | Standoffs, tubes and ribs point up. Counterbores are on the plate side |
| `stand` | Upright, base down | The backrest leans at 30° from vertical. The cable notch has a 45° pointed top |

All parts fit on one plate together.

**Heat-set inserts.** Install them with a soldering iron at about **225 °C** (PETG; follow your insert maker's chart). Rest the shell face down on a flat, hard surface so the 1.2 mm front skin is supported. Press straight down slowly and stop when the insert is flush with the band face. Don't push past flush: the hole is blind, with only 1.2 mm of skin left. Let it cool before fitting the screws.

## Assembly order

1. **Fit coupon.** Print it, then check four things:
   - Glass and window: the board's glass slides into the window with an even gap.
   - Pocket: the PCB drops into the pocket without force.
   - Flushness: lay a straightedge across the glass and frame. The glass should be flush to 0.1 mm proud.
   - Inserts: one insert installs cleanly.

   You can clamp the board to the coupon with M3 × 4 screws to check flushness under load. Adjust `GLASS_GAP`, `BOARD_GAP` or `GLASS_TO_PCB` / `GLASS_FLUSH` in `params.py`, then rebuild.
2. **Print** the front shell, rear cover and stand.
3. **Inserts.** Install the 4 inserts in the front bands.
4. **Wiring.**
   - Solder the slide switch in series with the battery **+** lead.
   - Fit the 1.25 mm plugs (adapter or re-crimp).
   - **Check polarity with a meter** against the board's BAT socket.
   - Glue the switch into the cradle on the cover, with its actuator through the slot.
5. **Board.** Drop the board into the shell from the back, glass first, onto the bands.
6. **Leads.** Plug in the battery lead (left socket) and the speaker lead (right socket). Lay the wires along the side channels.
7. **Pad and gasket.** Stick the 1 mm insulating pad to the pack's front face (optionally add Kapton over the board's rear parts). Stick a thin foam ring around the speaker on the pack's back face.
8. **Pack.** Lay the pack centred on the pad, speaker facing back.
9. **Light pipe.** Push a Ø3 mm acrylic rod, about **25.8 mm** long, into the cover's LED tube. That's the cover's back face to 0.5 mm above the LED; trim it to your measured LED height.
10. **Cover.** Lower the cover so its lip enters the shell and the standoffs land on the PCB pads. Fit 4 × M3 × 30 button-head screws (ISO 7380) and tighten until snug. The inserts are short, so don't overtighten.
11. **Stand.** Set the device on the stand. Optionally add TPU or silicone feet under the stand.

## Calipers checklist

Measure these, update `params.py`, re-run `build.py` and `validate.py`, and reprint the coupon if anything in the first 6.8 mm changed. The values shown are the current defaults.

| # | Parameter | Default (mm) | How to measure |
|---|---|---|---|
| 1 | `GLASS_TO_PCB` (**critical**) | 4.20 | Depth gauge from the glass front face to the PCB front face, in the top strip |
| 2 | `REAR_PARTS_H` | 4.70 | PCB back to the tallest rear part; note which part it is. The spec says 10.0 total thickness and the drawing says 10.5 |
| 3 | `PCB_W` × `PCB_H`, `PCB_T` | 54.5 × 101.5, 1.6 | Outer PCB edges at several points; PCB edge thickness |
| 4 | `GLASS_W` × `GLASS_H`, `GLASS_TOP_BY`, `GLASS_CORNER_R` | 54.5 × 83.0, 9.25, 0.3 | Glass edges and corners. Check whether the glass overhangs the PCB anywhere |
| 5 | `HOLE_DX` × `HOLE_DY`, `HOLE_D` | 47.9 × 94.5, 3.2 | Edge-to-edge across the holes, plus the diameter |
| 6 | `USB_Z_OFF` | 1.65 | USB-C centre behind the PCB back. Also check whether the receptacle overhangs the PCB edge |
| 7 | `USB_OPEN_W` × `USB_OPEN_H` (and `USB_PLUG_*`) | 12.5 × 7.0 | The widest USB-C cable overmold you'll use |
| 8 | `SD_BY`, `SD_Z_OFF` | 31.22, 1.0 | Slot centre from the top edge; card centre behind the PCB back. Check push-push and card protrusion |
| 9 | `RESET_BX`, `BOOT_BX`, `BTN_BY` | 40.98, 13.55, 98.2 | Button centres in front-view coordinates (x from the left edge with the glass facing you) |
| 10 | `BAT_SOCK_BY`, `SPK_SOCK_BY`, plug directions | 59.89 (left), 74.39 (right) | Socket centres, which way the plugs go in, and plug length including wires |
| 11 | `LED_BX`, `LED_BY`, `LED_H` | 5.0, 47.4, 1.6 | Power on, find the WS2812 on the rear, and measure its centre and height |
| 12 | `FRONT_RELIEF_*` | ±12 around the USB, from by 93, 0.6 deep | Any parts on the PCB **front** in the bottom strip below the glass |
| 13 | `PACK_L` × `PACK_W` × `PACK_T` | 66 × 35.5 × 19.7 | Re-check including tape and the protection-board bulge. Note where the leads exit |
| 14 | **`GRILLE_CX`, `GRILLE_CY`** | 0, 0 | **Speaker centre on the pack's back face**, measured from the pack's centre: +X to the right (seen from the front), +Y toward the device's top. The long side runs along the device's height |
| 15 | **`GRILLE_D`** | 22 | Speaker cone or port diameter |
| 16 | `SW_BODY_L` × `SW_BODY_W`, `SW_SLOT_L` × `SW_SLOT_W` | 8.6 × 3.6, 4.5 × 2.2 | Your slide switch body, actuator size and travel |
| 17 | `INSERT_HOLE_D`, `INSERT_L` | 4.0, 3.0 | Your insert maker's hole diameter and length. Keep `INSERT_L` ≤ 3.0 |
| 18 | Battery connector and polarity | — | Confirm the plug against the board socket and **check +/− with a meter** |
