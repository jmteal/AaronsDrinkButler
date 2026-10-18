# Board research: Hosyond / LCDWiki ES3C35P (3.5" ESP32-S3, ST77922 QSPI, 320x480)

Researched 2026-10-18. The Hosyond product is a rebadge of the LCDWiki (Shenzhen QDtech) **ES3C35P**. `ES3C35P-NS` is the same board without a speaker.

Evidence tags used below:
- **[schematic]**: read directly off the vendor schematic PDF (rev dated 1/30/2026).
- **[vendor]**: LCDWiki wiki page, spec PDF or user manual. These contain known copy-paste errors.
- **[community-probed]**: measured on real hardware by a third-party project.
- **[estimate]**: my own calculation. Measure it on the board before relying on it.
- **UNVERIFIED**: not confirmed by any primary source.

---

## Recommendation

1. **Firmware stack: ESP-IDF 5.4/5.5 + LVGL 9.x + `esp_lvgl_port` + `espressif/esp_lcd_st77922` + `espressif/esp_lcd_touch_st7123`.** A working open-source BSP for this exact board already exists ([andrewscherpbier/esp32-s3-display](https://github.com/andrewscherpbier/esp32-s3-display), ESP-IDF 5.5.5 + LVGL 9.4). Start from its `components/es3c35p_bsp` (display/touch/audio/SD/LED). Arduino is a worse fit here. The vendor Arduino demos are LVGL 8 with hand-rolled drivers, and the arduino-esp32 maintainers declined to support the panel ([issue #12694](https://github.com/espressif/arduino-esp32/issues/12694)).
2. **Display settings that work** ([community-probed], [jlmeredith/ES3C35P](https://github.com/jlmeredith/ES3C35P), [andrewscherpbier BSP](https://github.com/andrewscherpbier/esp32-s3-display/blob/main/components/es3c35p_bsp/bsp_display.c)):
   - Vendor init table.
   - Settings: RGB order (MADCTL 0x00), **color inversion ON**, RGB565, **byte-swap ON** (`swap_bytes = true`).
   - **QSPI 40 MHz**: the ST77922 write ceiling is 62.5 MHz. The factory firmware overclocks at 80 MHz.
   - **4-pixel-aligned draw areas**: add an LVGL rounder.
   - Rotation is software-only. The panel's native orientation is portrait 320x480, which suits this app, so use rotation 0.
3. **Ignore the `(p>>5)|(p<<11)` "pixel rotate" claim.** The correct fix is a normal RGB565 **byte swap**. The vendor's own code does `(color>>8)|(color<<8)` and declares the panel big-endian. Details are in section 3.
4. **Pin corrections:**
   - Touch is the **Sitronix touch controller integrated with the ST77922 panel at I2C 0x55** (ST7123-compatible register map), not an FT6336G.
   - Touch INT=GPIO47 and RST=GPIO48, as the user summary says. LCD RST is tied to CHIP_PU/EN (the summary's "NC" is effectively right).
   - **TE is on GPIO42**. The vendor pin tables omit it.
5. **Storage:**
   - Put the recipe DB, all thumbnails and the detail images in the **16 MB internal flash** (single factory app, no OTA, ~11 MB data partition). Use JPEG detail images (~20-30 KB each) and small thumbnails. Decode the thumbnails into the 8 MB PSRAM at boot for smooth scrolling.
   - microSD becomes optional (for user-added photos or expansion).
   - Store favorites and pantry as bitsets in **NVS**.
6. **Battery:**
   - Use a **protected** 1S LiPo. The board has no protection IC, only a TP4054 charger at ~290 mA.
   - Add an **inline power switch** on the battery lead. The board has no power switch, and a WS2812 LED plus two LDOs drain the battery permanently.
   - Idle policy: dim the screen → backlight off + panel SLPIN + light sleep with touch-INT wake (GPIO47 can wake from light sleep, **not** deep sleep) → after a long idle, deep sleep with wake from the BOOT button (GPIO0).
   - Expect roughly **12-25 h of screen-on time** from 3000 mAh, depending on brightness [estimate].
7. **Sound:** the board already has an **ES8311 codec + SC8002B (8002-class) mono amp**. Plug an 8 Ω 1-2 W speaker into the 1.25 mm 2-pin speaker socket and play short 16 kHz PCM clips through `esp_codec_dev`. Pull GPIO1 low to enable the amp, and keep it high when idle.

---

## 1. Module, memory, display/touch ICs, pin map

### SoC and memory
| Item | Finding | Source |
|---|---|---|
| SoC | **ESP32-S3R8 bare chip** (U7) on the PCB with a PCB antenna. It is *not* a WROOM module. "R8" = 8 MB octal PSRAM in-package. | [schematic](https://www.lcdwiki.com/res/ES3C35P/ESP32-S3%E5%8E%9F%E7%90%86%E5%9B%BE.pdf); [user manual](https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_User_Manual.pdf) p.15 |
| PSRAM | **8 MB OPI (octal) PSRAM**, embedded, runs at 80 MHz | [vendor spec](https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_Specification_V1.0.pdf) §3.1; [community-probed](https://github.com/jlmeredith/ES3C35P/blob/main/docs/HARDWARE.md) |
| Flash | **16 MB external quad SPI flash** (W1). The schematic symbol is labelled "W25X40", but that is a generic footprint name. Probed JEDEC ID: mfr 0x5E, dev 0x4018 = 16 MB. | [user manual](https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_User_Manual.pdf) p.15; [jlmeredith HARDWARE.md](https://github.com/jlmeredith/ES3C35P/blob/main/docs/HARDWARE.md) |
| Equivalent build target | "N16R8" | [LCDWiki page](https://www.lcdwiki.com/3.5inch_ESP32-S3_Display) |
| PlatformIO `memory_type qio_opi` | **Correct** (QIO flash + OPI PSRAM). In ESP-IDF, use `CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_SPIRAM_SPEED_80M=y`, `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, QIO flash mode. | [andrewscherpbier sdkconfig.defaults](https://github.com/andrewscherpbier/esp32-s3-display/blob/main/common/sdkconfig.defaults) |
| Octal PSRAM side-effect | GPIO33-37 are consumed by the PSRAM. They are not available. | [vendor spec](https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_Specification_V1.0.pdf) §4.2 |

### Display driver
- **ST77922** (Sitronix), QSPI, 320x480 native portrait, RGB565. This is confirmed by the vendor docs, two independent working projects and the Espressif driver. **Not ST77916.** [vendor spec](https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_Specification_V1.0.pdf), [esp_lcd_st77922](https://components.espressif.com/components/espressif/esp_lcd_st77922)
- The panel FPC is a 40-pin 0.5 mm "HMX035CTFT-001". It carries the QSPI lines, TE, the touch I2C lines and the backlight LEDK. [schematic]
- QSPI framing: cmd 0x02 + 24-bit address `reg<<8` for registers, and cmd 0x32 for pixel data in quad mode. [user manual](https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_User_Manual.pdf) p.7; [issue #12694](https://github.com/espressif/arduino-esp32/issues/12694)

### Touch
- **Not FT6336G.** The user manual's FT6336G text is copy-pasted from the 2.8" ES3C28P manual; the same paragraph even says "2.8 inches". The vendor spec says "I2C slave address 0x55". The manual also says the ST77922 has a built-in touch panel controller.
- [community-probed]: a bus scan shows **0x55** (touch), **0x18** (ES8311) and **0x28** (unknown, appears and disappears with 0x55). The touch register map matches Sitronix **ST7123**. It works with ESPHome's `st7123` platform and Espressif's [`esp_lcd_touch_st7123`](https://components.espressif.com/components/espressif/esp_lcd_touch_st7123). Raw range is 320x480, 5 points, no calibration needed. [jlmeredith HARDWARE.md](https://github.com/jlmeredith/ES3C35P/blob/main/docs/HARDWARE.md)
- 0x55 is absent on a cold-boot bus scan until GPIO48 releases the touch reset. [community-probed]
- The touch INT pin (GPIO47) has not been exercised by either open project. Both poll the controller. **The INT behaviour is UNVERIFIED.**

### Corrected pin map
Cross-checked against the schematic QFN pin numbers (e.g. pin 48 MTMS = GPIO42 = LCD_TE, pin 37 = GPIO47 = CTP_INT, pin 36 = GPIO48 = CTP_RST).

| Function | GPIO | User summary | Notes |
|---|---|---|---|
| LCD CS | 10 | correct | |
| LCD SCLK | 12 | correct | |
| LCD D0 / D1 / D2 / D3 | 11 / 13 / 14 / 9 | correct | |
| LCD RST | CHIP_PU (EN) | "NC", effectively correct | Shared with the MCU reset. **A USB/software reset does not reset the panel.** After flashing other firmware, unplug the board to power-cycle it, or the screen stays black. [community-probed] |
| **LCD TE** | **42** | missing | Not in the vendor pin tables, but present on the schematic, and the factory firmware uses it. |
| Backlight | 41 (active high, via BSS138 low-side) | correct | Has a 10 k pulldown, so the backlight is off by default. |
| Touch SDA / SCL | 38 / 39 | correct | **Shared** with the ES8311 codec and the P4 expansion header (10 k pull-ups). |
| Touch INT | 47 | correct | Not an RTC GPIO (see section 5) |
| Touch RST | 48 | correct | Active low |
| Touch address | **0x55** | correct | The vendor PDF that says 0x38 is wrong. [community-probed] |
| microSD CLK / CMD | 5 / 4 | correct | |
| microSD D0 / D1 / D2 / D3 | 6 / 7 / 2 / 3 | correct | 4-bit SDMMC with external pull-ups. No card-detect pin. |
| Amp enable (SC8002B SHUTDOWN) | 1 (**low = on**) | n/a | Has a 10 k pull-up, so the amp is off by default. |
| I2S MCLK / BCLK / LRCK | 17 / 18 / 21 | n/a | |
| I2S DOUT (to codec DSDIN) | 15 | n/a | Schematic net naming is codec-relative ("I2S_DI"). Direction confirmed by the [andrewscherpbier BSP](https://github.com/andrewscherpbier/esp32-s3-display). |
| I2S DIN (from codec ASDOUT) | 16 | n/a | |
| RGB LED (WS2812B, GRB order) | 40 | n/a | Powered from the +5 V rail. |
| Battery ADC | 8 (ADC1_CH7) | n/a | 100 k / 100 k divider, so Vbat = 2 × Vadc. |
| BOOT button | 0 | n/a | Usable as a user button after boot. |
| USB D- / D+ | 19 / 20 | n/a | Native USB-Serial/JTAG. |
| UART0 header | TX = 43, RX = 44 | n/a | The vendor text table has these swapped; the schematic is correct. |
| Spare GPIO (P3 header) | 45, 46 | n/a | **Both are strapping pins.** Avoid pulling them at boot. |

---

## 2. Onboard extras

All rows below are from the [schematic] and [vendor spec/manual](https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_User_Manual.pdf).

| Feature | Finding |
|---|---|
| Audio codec | **ES8311** (U5), I2C 0x18, I2S with MCLK. Has its own LDO (U4, ME6217) on a separate analog ground. |
| Amplifier | **SC8002B** on the schematic. The manual says "FM8002E", which is the same 8002-family mono class-AB bridge amp. Rated 1.5 W into 8 Ω / 2 W into 4 Ω at 5 V. On battery it is supplied from Vbat (~3.7 V), so expect noticeably less power [estimate]. |
| Speaker connector | JP3, **1.25 mm pitch 2-pin**. The ES3C35P variant ships with a speaker; ES3C35P-NS does not. The exact housing family (MX1.25 / PicoBlade-style vs JST GH) is **UNVERIFIED**; check it against the supplied speaker. |
| Microphone | Bottom-port MEMS mic (LMA2718B381-OA7) into ES8311 MIC1 (analog). The app does not need it. |
| Battery connector | JP1, **1.25 mm 2-pin** ("MX1.25"-style). It is **not PH2.0**, so most hobby LiPos with JST-PH 2.0 plugs need an adapter or re-crimp. Pin 1 = BAT+, pin 2 = GND per the schematic. **Verify polarity with a meter before plugging in.** |
| Charger | **TP4054** linear charger. R12 = 3.3 k sets ~300 mA (vendor states 290 mA actual, 4.24 V termination). A 3000 mAh cell takes ~10-12 h to fully charge [estimate]. The CHRG status pin is **not routed to a GPIO** (UNVERIFIED that no LED exists). |
| Battery protection | **None on the board.** Use a cell with a built-in PCM (protected). |
| Power path | SL2305 P-MOSFET (Q3). With no USB, the battery feeds the +5 V net; with USB present, the battery is disconnected from the load. The +5 V net feeds two ME6217C33 LDOs (main 3.3 V, audio 3.3 V), the WS2812 and the amp. VBUS is not routed to a GPIO, so "is USB connected?" cannot be read directly. |
| Battery voltage sense | GPIO8 / ADC1_CH7. The 200 k divider draws ~20 µA permanently. Vendor example: `ADC_ATTEN_DB_12`, curve-fitting calibration, × 2. ([vendor demo](https://github.com/ydedox/st77922/tree/main/Example_13_Get_Battery_Voltage)) |
| Power switch | **None.** Add one inline on the battery lead. |
| RGB LED | 1 × WS2812B (XL-5050RGBC) on GPIO40, GRB order. |
| Buttons | **RESET** (EN/CHIP_PU, also resets the LCD) and **BOOT** (GPIO0). Both sit on the board's short edge next to the UART socket ([outline drawing](https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_Specification_V1.0.pdf) §5). |
| USB-C | **Native ESP32-S3 USB** (USB-Serial/JTAG, VID 0x303A PID 0x1001). There is **no CH340** on this board, even though LCDWiki bundles the driver. Has ESD diodes, a B5819W Schottky and 5.1 k CC pull-downs. |
| Expansion | Three 1.25 mm 4-pin sockets: UART (P2: 5 V/GND/RX/TX), IO (P3: GPIO45/46/3V3/GND), I2C (P4: GPIO38/39/3V3/GND, shared bus). |
| Size | 54.5 × 101.5 × 10 mm module. Active area 48.96 × 73.44 mm. |

---

## 3. The "pixel rotate `(p>>5)|(p<<11)`" claim

**Not real. The actual requirement is an ordinary RGB565 byte swap.**

- The vendor's own demo code declares `BSP_LCD_BIGENDIAN (1)` and swaps every fill colour with `(color>>8)|(color<<8)`. Its TFT_eSPI sprite demos call `setSwapBytes(1)`. [ydedox/st77922 vendor demos](https://github.com/ydedox/st77922), files `Example_01_Simple_test/Simple_test.ino` and `Example_08_LVGL_Demos/display.h`.
- The working LVGL 9 BSP uses `swap_bytes = true` with the comment "LVGL is little-endian RGB565, the panel wants big-endian". It also uses RGB element order plus `esp_lcd_panel_invert_color(panel, true)`. [bsp_display.c](https://github.com/andrewscherpbier/esp32-s3-display/blob/main/components/es3c35p_bsp/bsp_display.c)
- `(p>>5)|(p<<11)` is a 16-bit rotate-right by 5. It turns `RRRRRGGGGGGBBBBB` into `BBBBBRRRRRGGGGGG`, which is not a meaningful RGB565 transform for any known panel setting. Most likely it is a corrupted transcription of `(p>>8)|(p<<8)`. It may also have been a workaround that happened to "look OK" while inversion or colour order was misconfigured. **No source supports it.**

What to use:
- **LVGL 9 + esp_lvgl_port:** `.flags.swap_bytes = true`. Or, without the port, call `lv_draw_sw_rgb565_swap(px_map, w*h)` in the flush callback.
- **LVGL 8:** `LV_COLOR_16_SWAP 1`.
- **Also needed:** RGB order (MADCTL 0x00) and **INVON**. Colours look like a photo negative without inversion.
- **Pre-converted image assets:** swap at build time, so that images bypass the runtime swap cost. Alternatively, keep everything little-endian and let the flush swap.

---

## 4. Firmware stack, LVGL, FPS, tearing

**Recommended:** ESP-IDF ≥ 5.4 (the reference BSP uses 5.5.5) with these components:
- `lvgl/lvgl ^9.4`
- `espressif/esp_lvgl_port ^2.9`
- `espressif/esp_lcd_st77922` (latest 2.1.1, [registry](https://components.espressif.com/components/espressif/esp_lcd_st77922))
- `espressif/esp_lcd_touch_st7123 ^1.0.2`
- `espressif/esp_codec_dev ~1.6`
- `espressif/led_strip`
- (versions from the [reference idf_component.yml](https://github.com/andrewscherpbier/esp32-s3-display/blob/main/components/es3c35p_bsp/idf_component.yml))

The app is offline, so Wi-Fi can stay disabled. That frees internal RAM and power.

Why IDF over Arduino:
- It has first-party drivers for both the panel and the touch controller.
- A board-specific LVGL 9 BSP already exists.
- Power-management APIs (auto light sleep, DFS, GPIO wake, `gpio_hold`), NVS, partitions and LittleFS are first-class.
- On Arduino you would use the vendor's LVGL 8 demos or `ESP32_Display_Panel` / Arduino_GFX, which is fine for a demo but a weaker base for a polished product.

**Buffers:**
- The ST77922 has its own GRAM, so the ESP32 needs **no framebuffer**. LVGL's partial render mode (`LV_DISPLAY_RENDER_MODE_PARTIAL`) is the right choice.
- Use two DMA-capable internal-RAM buffers of 1/10 to 1/5 of the screen:
  - 320 × 48 lines × 2 B ≈ 30 KB each
  - up to ~320 × 96 ≈ 61 KB each, because Wi-Fi is off
- The reference BSP uses 32 lines because Wi-Fi competes for internal RAM.
- PSRAM full-screen buffers (2 × 300 KB) are possible but slower to render and to DMA from. Use PSRAM for image caches instead.
- **Required:** a rounder callback that aligns x1/y1 down and x2/y2 up to multiples of 4. The ST77922 needs 4-pixel-aligned windows. [esp_lcd_st77922 README](https://components.espressif.com/components/espressif/esp_lcd_st77922)
- Rotation: the driver has no `swap_xy`, and the controller ignores MADCTL MX. Portrait 320x480 (native) is ideal for a recipe browser, so no rotation is needed.

**Achievable FPS** [estimate]:
- Link budget at 40 MHz × 4 lines = 20 MB/s:
  - full 320x480x2 = 307 KB frame ≈ **15 ms** → ~65 fps ceiling for full-screen pushes
  - at 80 MHz ≈ 7.7 ms. 80 MHz exceeds the controller's 62.5 MHz spec, though the factory firmware runs it.
- The ESP32-S3 SPI clock divides from 80 MHz, so nothing is available between 40 and 80 MHz.
- In practice, LVGL software rendering of a scrolling list with thumbnails at 240 MHz will be the bottleneck. Expect **~25-40 fps** for full-height list scrolling with double-buffered DMA overlap, and higher for small updates.
- Keep thumbnails pre-decoded (RGB565 in PSRAM, or better, internal RAM for the visible ones) and avoid per-frame JPEG decoding, gradients, shadows and opacity on list items.
- The only FPS evidence is the vendor claim. No community measurement was found (UNVERIFIED).

**Tearing:**
- **TE is wired to GPIO42.**
- The vendor BSP has a TE-sync implementation (`bsp_lcd_tear_t` semaphores with Tvdl/Tvdh timing) in [esp_bsp.c](https://github.com/ydedox/st77922/blob/main/Example_08_LVGL_Demos/LVGL_Demos/esp_bsp.c). The reference IDF BSP sends TEON (0x35 0x01) but does not wait on it.
- `esp_lcd` has no built-in TE support. Add a GPIO ISR on GPIO42 and start the flush of a full-screen refresh right after the TE edge. At 40 MHz a full frame (15 ms) roughly fits one ~16.7 ms panel refresh (panel refresh rate UNVERIFIED).
- For partial updates during list scrolling, tearing is usually only mildly visible. Treat TE sync as a polish item.

---

## 5. Battery operation

**Backlight PWM:**
- GPIO41 drives a BSS138 low-side switch on LEDK with R33 = 10 Ω. LEDC PWM works.
- Use **~25 kHz** to avoid audible whine from the inductor, with 10-bit duty and a squared (perceptual) curve, as the [reference BSP](https://github.com/andrewscherpbier/esp32-s3-display/blob/main/components/es3c35p_bsp/bsp_display.c) does. The vendor demo uses 5 kHz.
- The backlight is ~120 mA at full brightness (spec §3.5), the biggest single load.

**Vendor current figures** (spec §3.5, measured at the USB 5 V input):

| Load | Current | Power |
|---|---|---|
| Display only | 198 mA | 0.96 W |
| Display + speaker + charging | 401 mA | 1.76 W |

With linear LDOs, battery current ≈ load current, not power / 3.7 V.

**Runtime on 3000 mAh** (~2700 mAh usable) [estimate]:

| State | Estimated draw | Runtime |
|---|---|---|
| Screen on, 100 % backlight, CPU 240 MHz, Wi-Fi off | ~170-200 mA | **~13-16 h** |
| Screen on, ~40 % backlight, DFS 80-160 MHz | ~90-120 mA | **~22-30 h** |
| Screen off (SLPIN + backlight off), auto light sleep, waiting for touch | ~1.5-5 mA* | **~3-10 weeks** |
| Deep sleep, wake on BOOT | ~1-3 mA* | **~1-3 months** |

*The sleep floor is set by always-powered parts, not by the ESP32:
- WS2812B idle current (~0.5-1 mA typ, UNVERIFIED for this part)
- two ME6217 LDO quiescent currents
- the 20 µA ADC divider
- PSRAM retention in light sleep
- the panel/touch controller in sleep (UNVERIFIED)

Measure with a USB/battery power meter. This floor is why an **inline battery switch** is recommended for storage.

**Sleep and wake:**
- **Touch INT = GPIO47 is NOT an RTC GPIO** (the ESP32-S3 RTC GPIOs are 0-21). It cannot wake the chip from deep sleep (ext0/ext1). It *can* wake it from **light sleep** via `gpio_wakeup_enable(GPIO_NUM_47, GPIO_INTR_LOW_LEVEL)` + `esp_sleep_enable_gpio_wakeup()`.
- Whether the Sitronix touch controller asserts INT while the panel is in SLPIN is **UNVERIFIED**; neither open project uses INT. Test it early.
- Fallback: timer wake from light sleep every ~100-200 ms to poll touch at 0x55. This costs more current.
- **BOOT (GPIO0) is an RTC GPIO** → use `esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0)` as the deep-sleep wake button.
- On deep-sleep wake, the panel is *not* hardware-reset, because LCD RST = CHIP_PU and CHIP_PU is not toggled. Send SLPIN (0x10) before sleeping, and SLPOUT/re-init after wake.
- Pins to hold through sleep:
  - Backlight: R32 pulldown keeps it off.
  - Amp enable GPIO1: pull-up keeps it off. Also `gpio_hold_en` it high.
- Suggested policy:
  1. 30 s idle → dim to 20 %.
  2. 60 s → backlight off + SLPIN + light sleep on touch INT.
  3. 30 min → deep sleep, wake on BOOT.

**Battery %:**
- Read GPIO8 with ADC1 at 12 dB attenuation and curve-fit calibration, then × 2.
- Average 16-64 samples, and read during a stable load (e.g. with the backlight at a fixed level), because a LiPo sags ~50-100 mV under 150 mA.
- Map to % with a LiPo discharge table (e.g. 4.20 V = 100 %, 3.85 V ≈ 60 %, 3.70 V ≈ 30-35 %, 3.50 V ≈ 5-10 %, cut off and shut down at ~3.40 V) [estimate].
- Note the brownout risk: at Vbat ≈ 3.4-3.5 V the ME6217 (dropout ~100-200 mV at load, UNVERIFIED) leaves the 3.3 V rail in regulation only barely. Shut down gracefully at ~3.45 V.
- Charging cannot be detected directly (VBUS/CHRG are not on GPIOs). A rising voltage over time is the only hint.

---

## 6. Storage budget

**Flash layout** (16 MB) [estimate]:
- bootloader + partition table + NVS (24-64 KB) + phy
- one **factory app ~3 MB** (no OTA needed offline)
- **~12 MB `storage` partition** (LittleFS, or `esp_partition_mmap` of a packed asset blob for zero-copy reads)

**Per-image sizes:**

| Asset | Raw RGB565 | JPEG q≈80 [estimate] | Notes |
|---|---|---|---|
| Detail 320x240 | 153.6 KB | ~18-30 KB | Fills the top half of the portrait screen. |
| Detail 280x280 | 156.8 KB | ~20-32 KB | Square, centred with margins. |
| Thumbnail 96x96 | 18.4 KB | ~3-5 KB | |
| Thumbnail 96x96, generated icon as LVGL I8 (indexed 256-colour) | ~10 KB | n/a | Flat icons compress well in indexed or LZ4 formats. |

**What fits for 250 drinks:**

| Scheme | Total | Fits in ~12 MB flash? |
|---|---|---|
| All raw RGB565 (detail + thumb) | 250 × 172 KB ≈ **43 MB** | No → microSD only |
| JPEG detail + raw RGB565 thumb | 250 × (25 + 18.4) KB ≈ **10.9 MB** | Yes, tight |
| **JPEG detail + JPEG thumb** (recommended) | 250 × (25 + 4) KB ≈ **7.3 MB** | Yes, plenty of headroom |
| Recipe DB (JSON/CBOR or a generated C table) | ~0.1-0.3 MB | Yes |

**Runtime:**
- Decode all 250 thumbnails at boot into PSRAM: 250 × 18.4 KB = 4.6 MB of the 8 MB. Or keep an LRU cache of ~60. This gives smooth list scrolling with no decoding while scrolling.
- Decode the detail JPEG on demand. The ESP32-S3 has no hardware JPEG decoder, so use LVGL's TJPGD decoder or `espressif/esp_jpeg`. Expect ~30-80 ms per 320x240 decode [estimate, UNVERIFIED]. That is acceptable on screen transitions.
- **microSD** (4-bit SDMMC, FAT32 on MBR; GPT cards fail to mount per the [reference project](https://github.com/andrewscherpbier/esp32-s3-display)) is only needed for higher-resolution photos, more than ~400 drinks, or user-swappable content. It also adds failure modes (card unseated) and ~ mA-level current while mounted.

**Favorites and pantry:**
- Store them in **NVS** as small blobs: a favorites bitset (250 bits = 32 bytes) and a pantry bitset over ingredient IDs (~200 bits).
- Key them by stable recipe/ingredient IDs, not array indices, so that DB updates don't scramble them.
- NVS is wear-levelled and power-fail safe. Write on change, debounced ~2 s.

---

## 7. Sound

- **Hardware is already present:** ES8311 codec → SC8002B amp → 1.25 mm 2-pin speaker socket. The ES3C35P ships with a speaker; the -NS variant does not.
- **Code path:**
  - Use `esp_codec_dev` with the ES8311 at I2C 0x18 on the shared bus (GPIO38/39).
  - I2S std mode: MCLK 17, BCLK 18, WS 21, DOUT 15, DIN 16.
  - Drive **GPIO1 LOW to enable the amp**, and back high afterwards.
  - Working code: [bsp_audio.c](https://github.com/andrewscherpbier/esp32-s3-display/blob/main/components/es3c35p_bsp/bsp_audio.c). Vendor Arduino ES8311 demo: [Example_16_music](https://github.com/ydedox/st77922/tree/main/Example_16_music).
- **Assets:** a 16 kHz, 16-bit mono PCM "clink" of 250-400 ms is 8-13 KB. A tap-tick of 30 ms is 1 KB. Ten sounds total < 100 KB in flash. No MP3 decoder is needed.
- **Power:**
  - Keep the amp in shutdown (GPIO1 high) and the codec powered down between sounds.
  - 8002-class amps draw several mA quiescent when enabled (UNVERIFIED for SC8002B), and can pop when toggled. Enable ~20-50 ms before playback, mute the DAC first, then disable.
  - On battery, the amp runs from ~3.7 V, so volume and headroom are lower than on USB.
  - Short feedback sounds are fine on battery. Avoid long loud playback below ~3.6 V (risk of brownout).
- **Speaker:** an 8 Ω 1-2 W micro speaker (e.g. a 2830/3070 cavity type) with a 1.25 mm 2-pin plug. Confirm the plug family against the socket; housing is UNVERIFIED.

---

## Corrections to the user-supplied summary

| Claim | Verdict |
|---|---|
| QSPI display, ST77922, RGB565 | Correct |
| CS=10, SCLK=12, D0=11, D1=13, D2=14, D3=9, BL=41 | Correct |
| RST=NC | Effectively correct. The LCD reset is tied to CHIP_PU/EN, so it is not GPIO-controllable. |
| Touch FT6336G | **Wrong.** It is the Sitronix touch controller integrated with the panel (ST7123-compatible driver). |
| Touch at 0x55 | Correct |
| SDA=38, SCL=39 | Correct, but the bus is shared with the ES8311 and the expansion header. |
| Touch RST=48, INT=47 | Correct, matching the vendor table, schematic and community projects. |
| SD 4-bit CLK=5, CMD=4, D0=6, D1=7, D2=2, D3=3 | Correct |
| PlatformIO `qio_opi` | Correct |
| Pixel rotate `(p>>5)\|(p<<11)` | **Wrong.** Use a byte swap plus RGB order plus inversion ON. |
| Missing items | TE=GPIO42; audio (ES8311 + SC8002B, amp enable GPIO1 low, I2S 17/18/21/15/16); battery ADC=GPIO8; WS2812=GPIO40; BOOT=GPIO0; native USB on 19/20; spare GPIO45/46 (strapping pins). |

Note: the vendor spec PDF's figure captions mention "ES3C28P" and "E32R28T", and the manual's touch section mentions "2.8 inches" and FT6336G. Both are copy-paste errors from sibling boards. Prefer the schematic.

## Sources
- LCDWiki product page: https://www.lcdwiki.com/3.5inch_ESP32-S3_Display
- Spec PDF V1.0: https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_Specification_V1.0.pdf
- User manual: https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_User_Manual.pdf
- Schematic: https://www.lcdwiki.com/res/ES3C35P/ESP32-S3%E5%8E%9F%E7%90%86%E5%9B%BE.pdf
- Size drawing: https://www.lcdwiki.com/res/ES3C35P/3.5inch_ESP32-S3_touch_Size.pdf
- Community hardware reference (probed facts): https://github.com/jlmeredith/ES3C35P and https://github.com/jlmeredith/ES3C35P/blob/main/docs/HARDWARE.md
- ESP-IDF + LVGL 9 BSP for this board: https://github.com/andrewscherpbier/esp32-s3-display
- Vendor demo code mirror: https://github.com/ydedox/st77922
- arduino-esp32 discussion: https://github.com/espressif/arduino-esp32/issues/12694
- ESPHome model PR: https://github.com/esphome/esphome/pull/19011
- Espressif drivers: https://components.espressif.com/components/espressif/esp_lcd_st77922 and https://components.espressif.com/components/espressif/esp_lcd_touch_st7123
- LCD Wizard board page (secondary): https://lcdwizard.com/boards/lcdwiki-es3c35p
- Hosyond Amazon listing (ASIN B0H28X8SQ4 per jlmeredith; listing title wrongly says 240x320): UNVERIFIED, the listing itself could not be fetched.
