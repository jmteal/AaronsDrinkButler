# Enclosure research: Hosyond ES3C35P (3.5" ESP32-S3) on a Bambu Lab P2S

Researched 2026-10-18. Items marked **(unverified)** come from inference, from pixel-measuring the drawing, or from secondary sources. Check them with calipers before you print the final version (see the [calipers table](#dimensions-to-confirm-with-calipers)).

> **Board mismatch.** `docs/PLAN.md` and `docs/research/board.md` currently target the **Waveshare ESP32-S3-Touch-LCD-4.3** (800x480 RGB, GT911). This enclosure is for the **Hosyond / LCDWiki ES3C35P**, a different board: 3.5", 320x480, ST77922 QSPI panel, 54.5 x 101.5 mm. Firmware assumptions such as resolution, panel driver and touch IC need re-checking if the project has switched boards.

## Recommendation

- **Board outline is confirmed** by the official LCDWiki/QDtech outline drawing, V1.0, dated 2025-07-24:
  - **PCB:** 54.50 x 101.50 mm, 1.60 mm thick, corner radius R3.5.
  - **Glass:** the cover glass (CTP) is **54.50 x 83.00 mm, the same width as the PCB, not larger**. It is centred vertically with a 9.25 mm PCB strip above and below.
  - **Stack:** total thickness is 10.50 mm. That is CTP 1.00 + LCD 2.20 + glue 1.00 + PCB 1.60 + rear SMD parts 4.70. The glass front face sits **4.20 mm in front of the PCB front face**.
  - **Mounting holes:** 4x **Ø3.2 mm (M3)** on a **47.90 x 94.50 mm** pattern, 3.30 mm from the long edges and 3.50 mm from the short edges. The pads are Ø5.6. All four holes sit in the PCB strips **outside** the glass.
- **Flush mounting.** The glass has no overhang to sit on, so use the two exposed PCB strips (top and bottom) as the datum:
  - Give the front shell a full-depth pocket that matches the module outline (54.5 x 101.5, R3.5) plus about 0.2 mm clearance per side.
  - Inside the pocket, add two solid "bands" over the top and bottom PCB strips. They act as the ledge: the PCB front face bears on them, 4.20 mm below the case front face (make this a parameter).
  - Clamp the board from behind with four M3 screws. Each passes through a rear standoff and the PCB hole into an M3 heat-set insert in the front band, keeping at least 1.2 mm of front skin. Alternatively, use rear clamp posts on the back cover.
  - Leave the glass edges bare in a 0.2 mm gap. Don't add a lip, or it won't be flush.
- **Layout.** Stack the parts front to back: display module, then a 1.2 mm divider plate, then a rear bay.
  - **Battery:** put a 3000 mAh flat LiPo (**755060 or 105060, 50 x 60 mm**) in the lower part of the bay. Its 50 x 60 footprint fits inside the 54.5 x 101.5 board outline.
  - **Speaker:** put a **2030 cavity speaker (20 x 30 x 6.8 mm)** or a **28 mm round speaker** in the upper ~38 mm of the bay, firing through a rear or top grille.
  - **Board connectors:** the board has only **1.25 mm-pitch (MX1.25 / PicoBlade-style) 2-pin** sockets for battery and speaker. Most 3000 mAh packs ship with **JST-PH 2.0**, so plan on an adapter lead or re-crimping. Check polarity.
- **Proposed overall case size:**
  - **Body:** about **64 W x 112 H x 26–29 D mm** with a 105060 pack, or about 24–26 mm deep with a 755060 pack.
  - **Stand:** add a detachable or integrated stand that tilts the screen back **about 60° from horizontal** (parametric 55–70°) for a bar counter.
  - **Wider battery:** a 906090 (60 x 90 mm) pack forces the body to about 70 W x 112 H mm.
- **Material: PETG or ASA, not PLA.**
  - The spec lists a module temperature of **up to 62 °C while charging**, which is above PLA's ~55–60 °C softening point.
  - The case will sit in a bar near spills and sanitiser.
  - The P2S is fully enclosed, so ASA prints well. PETG is easier and tougher for snaps.
- **Print orientation:**
  - **Front shell: face down** on the plate. This gives the best flush surface: textured PEI for a matte finish, smooth for gloss.
  - **Back cover:** outside face down.
  - No supports are needed if the side cut-outs are kept to bridgeable widths or given teardrop or flat tops.
- **CAD toolchain: build123d (Python, OCCT B-rep).**
  - Put all dimensions in a single parameter file and export STL, 3MF and STEP from a script.
  - It gives real fillets and chamfers, `shell`/offset operations, and clean multi-body 3MF for AMS colours.
  - **Fallback:** OpenSCAD + BOSL2 if you want zero Python dependencies.

## 1. Board mechanical data (ES3C35P)

Primary source: the LCDWiki outline drawing, *"LCM OUTLINE", QDtech, V1.0, 2025-07-24*, read at high resolution. Secondary sources: the LCDWiki specification V1.0 and the product wiki page.

Coordinate convention used below: the **front view** has the glass facing you and the USB-C at the **bottom**. X runs from the **left** PCB edge and Y from the **top** PCB edge. The drawing's back view is mirrored left-to-right, and the conversions are already applied.

### 1.1 Outline and stack

| Item | Value | Status |
|---|---|---|
| PCB outline | 54.50 x 101.50 mm, corner R3.50 | drawing |
| PCB thickness | 1.60 | drawing |
| Backlight (BL) outline | 54.50 x 83.00 | drawing |
| Cover glass / touch (CTP) outline | **54.50 x 83.00**, the same width as the PCB | drawing; spec says ±0.2 |
| Glass vertical position | top edge at Y = 9.25, bottom edge at Y = 92.25 (9.25 strip at each end) | drawing |
| Visible area (LCD_VA) | 49.96 x 74.44; X inset 2.27 each side; top at Y = 10.90 (bottom at Y = 85.34) | drawing; spec says ±0.15 |
| Active area (LCD_AA) | 48.96 x 73.44; X inset 2.77; top at Y = 11.40 (bottom at Y = 84.84) | drawing |
| Black border under glass | About 1.65 mm at top and sides, about **6.9 mm at the bottom** (between Y 85.34 and 92.25). The visible window is **not vertically centred** on the glass. | derived |
| Stack | CTP 1.00 / LCD 2.20 / glue 1.00 / PCB 1.60 / rear SMD 4.70 = **10.50 total** | drawing. The spec table says the module is "10.00(D)" and the LCD is "3.2(D)", so the docs disagree by 0.5 mm. **(verify)** |
| Glass front above PCB front face | **4.20** (1.00 + 2.20 + 1.00) | derived. This is the critical number for flushness. **(verify)** |
| Tallest rear part above PCB back | 4.70 | drawing. From the side view, this looks like the battery socket or the USB-C. **(unverified which part)** |

### 1.2 Mounting holes

| Item | Value | Status |
|---|---|---|
| Hole diameter | Ø3.20 (R1.60 x4), suits M3 | drawing |
| Pad / keep-out diameter | Ø5.60 (R2.80 x4) | drawing |
| Pattern | 47.90 (X) x 94.50 (Y) | drawing |
| Hole centres | X = 3.30 and 51.20; Y = 3.50 and 98.00 | derived |
| Holes relative to glass | All four holes are in the PCB strips outside the glass. Their Ø5.6 pads end at Y = 6.3 (top) and Y = 95.2 (bottom), so a front-shell boss can bear on them. | derived |

### 1.3 Connectors, buttons and other features

All connectors except the mic are on the **rear** side of the PCB.

| Feature | Position (front-view coords) | Notes / status |
|---|---|---|
| **USB-C** | Bottom edge, **centred at X = 27.25** (13.52 + 13.73 from the drawing's back-view left edge). Flush with the PCB bottom edge. | Rear-mounted. The receptacle is about 3.2–3.5 mm tall from the PCB back, so its centre is about **7.4 mm behind the case front plane**: 4.2 + 1.6 + ~1.65. **(verify)** Plan a case opening of about **12.5 x 7.0 mm**, sized for the cable overmold rather than the receptacle. |
| **RESET** button | X ≈ **40.98** (13.52 from the right edge), Y ≈ **98.2** (3.26 from the bottom edge) | Top-actuated SMD tact switch on the rear **(verify height and that it's top-actuated)**. Needs a rear-cover poke hole or a printed plunger. |
| **BOOT** button | X ≈ **13.55** (40.95 from the right edge), Y ≈ 98.2 | Same as RESET. BOOT (IO0) is also a user button in firmware. |
| **microSD slot** | **Right long edge** (front view), slot centreline at **Y = 31.22**, card inserted from the right edge | Rear-mounted socket. Push-push vs push-pull is **unverified**. Socket height is about 1.4–1.9 mm off the PCB back **(verify)**. Plan a side opening of about **14 x 3 mm** plus a finger scallop. |
| **Speaker** socket (2-pin, 1.25 mm) | Right long edge, Y ≈ **74.39** (31.22 + 43.17) | Rated **1.5 W @ 8 Ω or 2 W @ 4 Ω** max. Entry direction is **unverified**. |
| **BAT** socket (2-pin, 1.25 mm) | Left long edge, Y ≈ **59.89** (17.76 + 19.70 + 22.43) | 3.7 V LiPo. The spec warns to check polarity. Entry direction is **unverified**: it looks like it opens toward the board interior. |
| UART socket (4-pin, 1.25 mm: RXD/TXD/GND/5V) | Left edge, Y ≈ 83.77 | Side-entry toward the edge **(verify)**. Leave cable room if used. |
| I2C socket (4-pin, 1.25 mm: IO38/IO39/GND/3V3) | Left edge, Y ≈ 37.46 | Shared with the touch and audio codec. |
| Expansion socket (4-pin, 1.25 mm: IO45/IO46/GND/3V3) | Left edge, Y ≈ 17.76 | Could drive an external button or LED. |
| **MEMS microphone** | Front-side port at X ≈ 44.68 (9.82 from the right edge), Y ≈ 3.94 | Bottom-port mic: the sound hole goes through the PCB to the front. If voice is ever used, put a **Ø1.0–1.5 mm hole** in the front band here. |
| RGB LED (IO40, addressable) | Rear side, about X 5.0, Y 47.4 **(unverified, pixel-measured; identifying the part is a guess)** | Candidate for a light pipe. The board has **no documented charge-status LED (unverified)**. |
| Wi-Fi antenna (PCB trace) | Rear, top edge, roughly X 20–35, Y 0–6 | Keep metal (inserts, screws, foil tape) and the battery away from this area if you can. |
| Front-side parts in the bottom strip | Small pads either side of the USB, at about Y 94–99 | Leave about 0.5 mm relief in the front band there **(verify on the real board)**. |
| Power switch | **None on board** | Add an inline switch (§4.6). |

The spec also gives:

- Charging: up to 500 mA max, about 290 mA actual; charge voltage 4.24 V.
- Backlight: 120 mA.
- Battery ADC on IO8, amp enable on IO1, SD over 4-bit SDIO.

Accessories in the box: a 4-pin 1.25-to-2.54 mm lead and a USB-C cable. **No battery and no speaker are listed in the box (unverified for the Hosyond retail bundle).** LCDWiki lists two SKUs: **ES3C35P (with speaker support)** and **ES3C35P-NS (no speaker)**.

**No STEP, DXF or STL is published.** The LCDWiki quick-start zip (42 MB) contains only PDFs, firmware binaries and the Espressif flash tool. The only mechanical source is the outline PDF.

### 1.4 Existing cases

- MakerWorld **"ESP32-32E 3.5in Vertical Standing Case"** (model 2856553). It is for a Hosyond 3.5" ESP32 display, but that is the older ESP32-32E / ILI9488 board, **not** the ES3C35P. It uses M3 x 8 screws for the face frame, has no port openings, and the page returned HTTP 403, so it wasn't inspected. Use it for ideas only.
- No Thingiverse or Printables case for the ES3C35P was found.

## 2. Battery and speaker options

### 2.1 3.7 V ~3000 mAh LiPo packs

Pack names follow the convention thickness x width x length, in tenths of a mm for thickness and mm for the rest. For example, 105060 is about 10.5 x 50 x 60.

| Pack | Typical size (mm) | Fit notes |
|---|---|---|
| **755060** (3000 mAh) | 7.5 x 50 x 60 | **Best fit**: thinnest option, and the footprint is inside the board outline. PiShop sells it with a JST-PH 2.0 lead and PCM. |
| **105060** (3000 mAh) | about 10–10.5 x 50 x 60 | Fits behind the board and adds about 3 mm of depth over the 755060. |
| 906090 / 906070 | 9 x 60 x 90 (about 4000 mAh) / 9 x 60 x 70 | 60 mm is wider than the 54.5 board, so the case grows to about 70 mm wide. |
| Long-flat (Electrokit) | 5 x 45 x 135 or 4 x 45 x 130 | Too long (> 101.5) for this case. |
| 18650 cell (3000 mAh class) | Ø18.5 x 65 (about 69 with protection PCB) | Needs about 20 mm of depth just for the cell. Bulky, and it needs a holder and protection. Not recommended. |

**Connector:** most packs ship with **JST-PH 2.0** (2-pin, 2.0 mm). The ES3C35P socket is **1.25 mm 2-pin** (MX1.25 / PicoBlade family). Buy a PH2.0-to-1.25 adapter, or a pack with an MX1.25 plug, and **check polarity**: there is no industry-standard pin order. Size the bay at **pack + 1 mm** each way to leave room for swelling. Don't press the pack against hard edges or component leads; the divider plate handles that.

### 2.2 Speakers commonly paired with these boards

| Speaker | Size (mm) | Rating | Connector |
|---|---|---|---|
| **2030 cavity (box) speaker** (Waveshare "8Ω 2W Speaker (B)") | 20 x 30 x 6.8 | 8 Ω 2 W | 2-pin **PH1.25** (MX1.25-style), about 120 mm lead |
| 2831 / 2840 cavity speakers | about 28 x 31 / 28 x 40 x 4–5 | 8 Ω 1–2 W | Usually MX1.25 or PH2.0 **(unverified, sizes vary by vendor)** |
| 28 mm round | Ø28 x about 4–5 | 8 Ω 1–2 W | Wire leads or MX1.25 |
| 40 mm round | Ø40 x about 5 | 8 Ω 2–3 W | Wire leads or PH2.0 |

The board amplifier maxes out at **1.5 W into 8 Ω** (2 W into 4 Ω), so a 1–2 W 8 Ω speaker is right. A **cavity (boxed) 2030** is the easiest because it doesn't need a sealed back volume from the case. An open-frame round speaker needs a sealed rear chamber, about 3–5 cm³, printed into the case, plus a gasket or foam ring. "Battery with an attached speaker" bundles vary, so measure the actual parts you have.

## 3. Bambu Lab P2S (design-relevant specs)

| Item | Value |
|---|---|
| Build volume | **256 x 256 x 256 mm**. The whole case fits on one plate. |
| Nozzle | **0.4 mm hardened steel** quick-swap (0.2, 0.6 and 0.8 also available); 300 °C max |
| Bed | 110 °C max |
| Enclosure | Fully enclosed with filtration. Suits ASA, ABS, PC and PETG as well as PLA. |
| Multi-colour | **AMS 2 Pro** in the P2S Combo, up to 16 colours with multiple units. AMS HT and AMS lite compatibility is **unverified**. |
| File formats | Bambu Studio imports **STL, 3MF, STEP, OBJ**. **3MF** keeps multiple bodies or colours, plate layout and print settings, so it's the best format to commit for multi-colour. **STL** is fine for single-colour parts. Bambu Handy prints sliced jobs and MakerWorld models; local STLs are sliced in Bambu Studio first **(Handy workflow details unverified)**. |

**Tolerances.** These are general Bambu and FDM practice with a 0.4 nozzle; tune them with a quick test coupon.

| Fit | PLA | PETG | ASA |
|---|---|---|---|
| Module pocket (sliding, no force) | +0.20/side | +0.25/side | +0.25/side |
| Lid / press fit | 0.10–0.15 | 0.15–0.20 | 0.15–0.20 (allow for about 0.5% shrink) |
| Clearance fit (moving parts, snaps) | 0.25–0.30 | 0.30 | 0.30 |
| Clearance hole for M3 | Ø3.4 | Ø3.4 | Ø3.5 |
| M3 heat-set insert (e.g. Ruthex M3x5.7) | Ø4.0 hole (follow the insert maker's spec) | same | same |
| M3 self-tapping into plastic | Ø2.5–2.6 pilot | Ø2.6 | Ø2.6 |

Bambu Studio's **X-Y hole compensation** and **elephant-foot compensation** help with face-down parts. The pocket edge on the plate is the elephant-foot edge, so add a 0.3–0.4 mm chamfer there.

## 4. Design approach

### 4.1 Flush front shell

- **Datum.** The glass and LCD are the same width as the PCB, so there's nothing behind the glass edge to rest on. Use the PCB front face in the two 9.25 mm strips as the datum.
- **Pocket and bands.**
  - Front skin: about 1.6–2.0 mm.
  - Pocket: 54.9 x 101.9 mm (module + 0.2/side) with R3.7 corners, depth = 4.20 + 1.60 + rear clearance.
  - Inside the pocket, solid **bands** fill Y 0–9.0 and Y 92.5–101.5. They stop about 0.25 mm short of the glass edge.
  - The bands' rear faces sit exactly `glass_to_pcb` = 4.20 mm behind the front plane. The PCB bears on them.
  - Relieve the bands where front-side parts sit near the USB, and at the mic port.
- **Window.** The glass sits in a 54.9 x 83.4 mm opening through the front skin. That's the glass plus 0.2 mm per side. The black border under the glass already hides the bezel area, so no printed bezel overlap is needed.
- **Flush adjustment.**
  - Make `glass_to_pcb` and `flush_offset` parameters.
  - Aim for the glass **0.0 to +0.1 mm proud**, which feels better than recessed and protects the case edge.
  - Add 0.1 mm Kapton or foam shims on the bands if a printed batch comes out recessed.
- **Edge protection.** Add a 0.4–0.6 mm chamfer on the case edge around the glass. It hides the gap and stops chipped corners.

### 4.2 Rear clamp

- **Clamp path.** Each of the four M3 screws runs from the back, through a rear standoff tube on the divider plate, then through the PCB hole, into a **heat-set insert in the front band**.
- **Insert depth.** The band is only about 4.2 mm thick in front of the PCB, so use a **short insert**: M3 x 3 mm, or M2.5 x 3 with M2.5 screws. Keep at least 1.2 mm of skin in front of it.
  - Alternative: drop the inserts and use **M3 self-tappers** (or M2.5 x 6 pan-head self-tappers) into Ø2.5 pilot holes.
- **Standoff contact.** Standoffs should touch only the Ø5.6 pad ring. Use Ø5.4 OD with a Ø3.4 bore so they stay clear of nearby parts.
- **Rear part clearance.** Leave **≥ 5.5 mm** behind the PCB back face for the 4.7 mm parts. Leave 8–10 mm at the side sockets for cable bend radius: MX1.25 plugs stick out about 6 mm.

### 4.3 Back cover: screwed vs snap-fit

- **Recommended: screwed.** Use 4x M3 (or M2.5) into heat-set inserts in corner bosses of the front shell. It survives repeated battery and SD service and bar handling, and it gives a predictable clamp on the battery bay.
- **Snap-fit.** Fine in PETG with cantilever hooks: about 1.0–1.2 mm thick, 6–8 mm long, 0.6–0.8 mm undercut. PLA hooks crack after a few cycles, and ASA is acceptable. If you snap the back on, keep the display clamp screws independent so opening the back never disturbs flushness.
- **Seal.** Add a 0.8 mm tongue-and-groove on the cover seam for splash resistance. That's useful in a bar, but it doesn't make the case waterproof.

### 4.4 Internal layout (front to back)

1. **Front skin and display module.** About 10.5 mm deep including rear parts, plus 1 mm of air.
2. **Divider plate,** 1.2 mm thick. It carries the four clamp standoffs, protects the battery from component pins, and has wire pass-through slots at the BAT socket (left edge, Y ≈ 60) and the SPK socket (right edge, Y ≈ 74).
3. **Rear bay:**
   - **Battery:** a 50 x 60 pack, long side vertical, in the **lower** part of the bay (about Y 38–100). This keeps the centre of mass low for stability on a stand, and keeps the battery away from the top-edge Wi-Fi antenna. Hold it in a printed cradle (0.5–1 mm clearance) with a foam pad or VHB tape, not glue.
   - **Speaker:** a 2030 cavity speaker in the **upper** part (Y about 4–34), lying flat. Its sound port faces a **rear grille**, or the top-edge grille if the stand covers the back.
4. **Back cover:** 2.0 mm.

Depth budget with a 105060 pack: 1.8 skin + 4.2 + 1.6 + 5.5 + 1.2 divider + 11.0 battery bay + 2.0 back ≈ **27.3 mm**. With a 755060 pack it's about **24.8 mm**.

### 4.5 Openings

| Opening | Where | Size (start) |
|---|---|---|
| USB-C | Bottom wall, centred on X = 27.25, centre about 7.4 mm behind the front plane | 12.5 x 7.0, R1.5 slot (fits the cable overmold) |
| microSD | Right wall, centred on Y = 31.22, at the socket height | 14 x 3.0 slot plus a Ø10 finger scallop on the outside |
| RESET / BOOT | Back cover over X 40.98 / 13.55, Y 98.2, or printed plungers | Ø3 poke hole for RESET (recessed, so it's paperclip-only); a printed plunger for BOOT if used as a user button |
| Speaker grille | Back cover over the speaker, or the top wall | Hex or slot pattern: 1.2–1.5 mm holes, 0.8–1.2 mm webs, about 25–35% open area. Keep holes ≥ 0.8 mm so they print cleanly. |
| Mic | Front band at X 44.68, Y 3.94 | Ø1.2 (optional) |
| Power switch | Left or bottom wall | Depends on the switch (§4.6) |
| Light pipe | Rear or side, aligned to the RGB LED (about X 5, Y 47; verify) | Ø3 clear rod, or a printed clear PETG insert |

### 4.6 Power switch, LED and stand

- **Power switch.** The board has no switch, and a LiPo plugged into the socket keeps the ESP32 powered. Put a switch **in series with the battery + lead**. Options:
  - Mini slide switch SS12D00 (body about 8.5 x 3.6 mm, cut-out about 4.5 x 2.2 mm for the actuator).
  - Panel slide MSK-12C02.
  - KCD11 mini rocker (cut-out 15 x 10 mm).

  **Trade-off:** with the switch off, USB can't charge the battery. If you want "charge while off", the switch would have to cut the board's 3V3 regulator enable, which is a board modification. Not recommended.
- **Charging LED.** No dedicated charge LED is documented. Options:
  - **(a)** Light-pipe the rear RGB LED (IO40) and have firmware show charge state, inferred from the battery ADC (IO8) rising while USB is present.
  - **(b)** If the real board has a charge-IC LED, light-pipe that instead. Look for a small LED near the USB/BAT area that lights while charging.

  Use a 3 mm acrylic light pipe or print a short pin in transparent PETG with the AMS.
- **Stand for counter use.**
  - Tilt the screen back to **about 55–65° from horizontal** for someone standing at a 0.9–1.1 m bar counter. Make it a parameter.
  - Either option works:
    - An integrated **wedge base**, where the back cover tapers to a foot. Simplest and most stable.
    - A **fold-out kickstand** on a print-in-place or pin hinge.
  - Raise the bottom edge about 8–12 mm off the counter so spills don't reach the USB-C, which is on the **bottom edge**.
  - Add a cable channel in the stand so the USB-C cable exits rearward.
  - Add TPU or silicone feet; the AMS can print TPU only through an external spool.
  - **Alternative:** if the UI is landscape (480x320), the USB ends up on the left or right side. That is cleaner for a counter device. Decide orientation before modelling the stand.

### 4.7 Wall thickness and printing

- **Thickness:** walls 2.0–2.4 mm (5–6 perimeters of 0.42 mm). Front skin 1.6–2.0 mm. Back cover 2.0 mm. Insert bosses with ≥ 1.6 mm wall around the insert.
- **Internal corners:** fillet them at R1 or more for strength. Keep outer edges at R2–R3 for hand feel.
- **Front shell:** print **face down**. The visible face is the plate side, so it comes out flat and textured, and flushness depends on it being flat.
  - The window and pocket are vertical extrusions, so no supports are needed.
  - Side openings like the USB and SD slots are vertical walls in this orientation, so they need no support. Give them flat or teardrop tops if they're taller than about 8 mm.
- **Back cover:** print outside face down.
- **Stand:** print on its side.
- **Infill:** 3–4 walls with 15–20% gyroid is plenty. Use 100% around the inserts by adding a modifier, or simply enough walls.
- **Colour:** with the AMS, give the front shell a dark first 2–3 layers (same colour as the glass border) and use an accent colour elsewhere. Export as a multi-body 3MF.

## 5. CAD toolchain (code → STL)

| | **OpenSCAD (+ BOSL2)** | **CadQuery** | **build123d** |
|---|---|---|---|
| Kernel | CSG on meshes (CGAL / Manifold) | OpenCascade B-rep | OpenCascade B-rep (same OCP bindings as CadQuery) |
| Language | Own DSL | Python, fluent/chained API | Python, builder or algebra API (more Pythonic, readable) |
| Fillets / chamfers on arbitrary edges | No. Only through hull/minkowski tricks or BOSL2 rounded primitives. | Yes | Yes, with edge selectors such as `.edges().filter_by(Axis.Z)` |
| Shell / offset (hollow box) | Manual | Yes | Yes (`offset`, `shell`) |
| Export | STL, 3MF, AMF, DXF, SVG (no STEP) | STL, STEP, 3MF (via OCP), DXF, SVG | STL, STEP, 3MF (multi-part, coloured) via `Mesher`, DXF, SVG |
| Parametric updates | `-D name=value` on the CLI; Customizer UI | Python variables or a config file | Python dataclass / TOML config; easy to unit-test (assert clearances) |
| Install | One app / Homebrew; zero Python | `pip install cadquery`: large OCP wheel, Python version limits | `pip install build123d`: large OCP wheel, Python 3.10+ |
| Viewer | Built-in, fast preview | CQ-editor or ocp_vscode | ocp_vscode (VS Code), Jupyter |
| Ecosystem | Largest hobby community; BOSL2 has snap-fits, threads, rounded boxes, hinges | Mature, many examples, cq_warehouse | Newer but very active; bd_warehouse fasteners and inserts |
| Downsides | No true fillets; slow CGAL on complex parts (Manifold backend fixes speed); mesh-only output | Fluent API gets awkward for complex parts | Big dependency; API still evolving between minor versions |

**Recommendation: build123d.**

- An enclosure is mostly rounded boxes, shelled walls, filleted edges, pockets and cut-outs, which a B-rep kernel handles cleanly.
- It exports **STEP** for checking in Bambu Studio or a mechanical review, and **multi-body 3MF** for AMS colours, as well as STL.
- Keep every measured value in one file, e.g. `enclosure/params.py` or `params.toml` with the calipers table's names. Then `python enclosure/build.py` regenerates `out/*.stl`, `out/case.3mf` and `out/case.step`.
- Python asserts can check clearances: battery vs divider, glass flush offset, minimum wall.
- Pin the version in `requirements.txt` or a `uv` lockfile, since the API is still moving.

**Fallback: OpenSCAD + BOSL2.**

- Choose it if you'd rather avoid a ~200 MB Python/OCP install or want the GUI Customizer for non-programmers.
- Accept approximate rounding and STL/3MF-only output.
- CLI: `openscad -o front.stl -D 'glass_to_pcb=4.25' case.scad`.

## Dimensions to confirm with calipers

Measure the real board, battery and speaker, then update the parameter file. The "drawing" values are the starting defaults.

| # | Parameter (suggested name) | Drawing / assumed value (mm) | How to measure |
|---|---|---|---|
| 1 | `pcb_w` x `pcb_h` | 54.50 x 101.50 | Outer PCB edges, at several points |
| 2 | `glass_w` x `glass_h` | 54.50 x 83.00 (±0.2) | Glass edges. Check whether the glass overhangs the PCB anywhere. |
| 3 | `glass_top_y` | 9.25 | PCB top edge to the glass top edge |
| 4 | `glass_to_pcb` (**critical**) | 4.20 | Depth gauge: glass front face to the PCB front face in the top strip |
| 5 | `pcb_t` | 1.60 | PCB edge |
| 6 | `rear_parts_h` | 4.70 | PCB back to the tallest rear part. Note which part it is. |
| 7 | `total_t` | 10.50 (spec also says 10.00) | Glass front to the tallest rear part |
| 8 | `hole_d`, `hole_dx`, `hole_dy`, `hole_edge_x/y` | 3.20, 47.90, 94.50, 3.30 / 3.50 | Pin gauges or a caliper inside jaw; measure edge-to-edge and add the diameter |
| 9 | `va_w` x `va_h`, `va_top_y` | 49.96 x 74.44, 10.90 | Lit visible area with the display on (white screen) |
| 10 | `usb_x`, `usb_w`, `usb_h`, `usb_z` | 27.25, ~8.9, ~3.2, ~1.65 above the PCB back | Receptacle centre from the left edge, width, height, and stand-off from the PCB back. Also check overhang past the PCB edge. |
| 11 | Your USB-C cable overmold | ~12 x 6.5 | Widest plug you'll use |
| 12 | `sd_y`, `sd_w`, `sd_h`, `sd_z` | 31.22, ~11.5, ~1.5, ? | Slot centre from the top edge; slot width and height; offset from the PCB back. Check push-push and whether the card protrudes. |
| 13 | `btn_reset_x`, `btn_boot_x`, `btn_y`, `btn_h` | 40.98, 13.55, 98.2, ? | Button centres (front-view coords) and plunger height above the PCB back |
| 14 | `bat_sock_y`, `spk_sock_y`, entry directions | 59.89 (left), 74.39 (right) | Socket centres; which way the plug inserts; plug length including wires |
| 15 | `mic_x`, `mic_y` | 44.68, 3.94 | Front-side hole position |
| 16 | `led_x`, `led_y` (RGB LED; any charge LED) | ~5.0, ~47.4 (unverified) | Power on, find the LED(s), and measure the centre |
| 17 | Front-side parts in the bottom strip | ? | Height and location of any parts on the PCB front below the glass |
| 18 | `bat_t` x `bat_w` x `bat_l` | 10.5 x 50 x 60 (or 7.5 x 50 x 60) | Actual pack, including the PCM bulge and tape. Also lead length and exit corner. |
| 19 | Battery connector type and polarity | JST-PH 2.0 vs 1.25 | Confirm against the board socket. **Check +/- with a meter.** |
| 20 | `spk_w` x `spk_l` x `spk_t`, port location | 20 x 30 x 6.8 (2030) | Actual speaker; where the sound port is; lead length |
| 21 | Power switch body and actuator | e.g. SS12D00 | Body, actuator travel, and mounting style |
| 22 | Printer calibration coupon | — | Print a 55 x 20 mm pocket test at the chosen clearance, plus an M3 insert boss, before the full case |

## Sources

- LCDWiki product page, 3.5inch ESP32-S3 Display (ES3C35P / ES3C35P-NS), with interface descriptions and resource links: https://www.lcdwiki.com/3.5inch_ESP32-S3_Display
- **Outline / size drawing (primary mechanical source):** https://www.lcdwiki.com/res/ES3C35P/3.5inch_ESP32-S3_touch_Size.pdf
- Specification V1.0 (sizes, charging, speaker rating, pin map, accessory list): https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_Specification_V1.0.pdf
- Schematic: https://www.lcdwiki.com/res/ES3C35P/ESP32-S3%E5%8E%9F%E7%90%86%E5%9B%BE.pdf
- Quick-start package (checked: no CAD files): https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_ES3C35P_Quick_Start.zip
- User manual: https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_User_Manual.pdf
- MakerWorld, Hosyond 3.5" (ESP32-32E) standing case, a different board: https://makerworld.com/models/2856553
- Elecrow 3.5" ESP32-S3 (similar class of board; ships with a speaker): https://www.elecrow.com/3-5-esp32-s3-display-320x480-capacitive-ips-touchscreen-with-speaker-mic-bat-interface-supports-ai-voice-chat.html
- The Pi Hut, Waveshare 3.5" ESP32-S3 (note on MX1.25 vs JST-PH battery plugs): https://thepihut.com/products/esp32-s3-3-5-capacitive-touch-display-dev-board-320x480
- PiShop, 755060 3000 mAh LiPo, 7.5 x 50 x 60, JST-PH 2.0: https://www.pishop.us/product/lithium-ion-polymer-battery-3-7v-3000mah/
- Electrokit, 3000 mAh LiPo variants (135 x 45 x 5, 130 x 45 x 4, JST-PH 2.0): https://www.electrokit.com/en/batteri-lipo-3.7v-3000mah-1
- Waveshare 2030 cavity speaker 8 Ω 2 W, 20 x 30 x 6.8, PH1.25: https://www.waveshare.com/8ohm-2w-speaker-b.htm
- Grobotronics 2030 speaker 2 W 8 Ω PH1.25: https://grobotronics.com/speaker-enclosed-2w-8ohm-20x30mm-ph1-25.html?sl=en
- Bambu Lab P2S FAQ / specs (wiki): https://wiki.bambulab.com/en/p2s/manual/p2s-faq
- P2S retailer spec sheets (build volume, nozzle, AMS 2 Pro combo): https://shop3duniverse.com/collections/bambu-lab-3d-printers/products/bambu-lab-p2s , https://www.3djake.com/bambu-lab/p2s-combo?sai=38590 , https://core-electronics.com.au/bambu-lab-p2s-3d-printer-machine-only.html
- build123d docs: https://build123d.readthedocs.io/ · CadQuery: https://cadquery.readthedocs.io/ · OpenSCAD: https://openscad.org/ · BOSL2: https://github.com/BelfrySCAD/BOSL2

The tolerance numbers, insert hole sizes, grille sizing, stand angle and the CAD comparison come from general FDM and CAD practice, not from a single cited source. Treat them as starting points and validate them with a test coupon.
