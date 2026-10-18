# Board research: Waveshare ESP32-S3-Touch-LCD-4.3 / 4.3B

Researched 2026-10-18. Items marked **(unverified)** come from inference, secondary sources, or conflicting docs and should be checked on real hardware or in the schematic.

## Recommendation

- **Which board.** The "with case" SKU is the **ESP32-S3-Touch-LCD-4.3B-BOX** (Waveshare SKU 28141, about US$43.99). The plain **4.3** (SKU 25948, $27.99–32.99) ships with standoffs only, no case. Both boards use the same core: the same module, the same 800x480 RGB panel pin map, GT911 touch, the CH422G expander, and the same SD wiring. Firmware written for one should port to the other with almost no changes.
- **Firmware stack.** Use **ESP-IDF v5.5.x** and **LVGL v9.x**, built directly on Espressif components: `esp_lcd` RGB panel, `esp_lcd_touch_gt911`, `esp_io_expander_ch422g`, and `esp_lvgl_port`.
  - Waveshare tests against IDF 5.5.2 and ships an `09_lvgl_v9_demo`, so start from that demo.
  - Don't use Arduino. The precompiled Arduino core blocks the sdkconfig settings this panel needs: Octal PSRAM at 120 MHz, XIP from PSRAM, 64 B cache lines, and bounce buffers.
  - ESPHome and SquareLine are a poor fit. EEZ Studio or LVGL's own editor are optional; hand-written C plus the simulator is fine.
- **Image storage.** Keep the images in **internal flash**: a 16 MB part with a roughly 10–11 MB LittleFS (or memory-mapped assets) partition holds 250 images at 25–40 KB each. Leave the SD card as an optional "extra recipes" path. Fit the flash budget by keeping the images small, not by adding the SD card.
- **Image format.** Use two renditions per drink:
  - **Thumbnail for lists**, around 120x160. Store it either as pre-converted **RGB565 LVGL binary** (about 38 KB, no decode, smoothest scrolling) or as a small JPEG (about 6–10 KB).
  - **Detail photo**, around 300x400 or 240x320: a **baseline (non-progressive) JPEG, 4:2:0, quality ~75–80** (about 20–35 KB), decoded with **`esp_new_jpeg`** (about 15 ms at 320x240) and cached in PSRAM.
- **Favorites.** Store them in **NVS**: a single blob or bitset of recipe IDs is tiny, wear-levelled and power-fail safe.
- **Simulator.** **Yes, it's practical.** LVGL's SDL simulator (`lv_port_pc_vscode`) builds on macOS with Homebrew SDL2 and CMake. Keep the UI code platform-agnostic: a thin HAL for images, storage and favorites, and an 800x480 window. Expect the simulator to be much faster than the S3, so set a performance budget early and test it on hardware.

## 1. Hardware

| Item | ESP32-S3-Touch-LCD-4.3 | ESP32-S3-Touch-LCD-4.3B (and -BOX) |
|---|---|---|
| Module | ESP32-S3-WROOM-1-**N16R8**: 16 MB flash (QIO), 8 MB **Octal** PSRAM, dual LX7 at 240 MHz, 512 KB SRAM | same |
| Display | 4.3" IPS 800x480, 65K colours, **16-bit RGB565 parallel RGB** (HSYNC/VSYNC/DE/PCLK) | same panel interface; product page lists 550 cd/m² vs 270 cd/m² on the 4.3 wiki **(unverified that the panels differ)** |
| Panel driver | **ST7262** per the new Waveshare docs FAQ and the ESP32_Display_Panel board file. The old wiki FAQ text also mentions ST7701, which is a doc inconsistency. ST7262 is a pure RGB timing controller with no SPI init sequence. | ST7262 |
| Touch | **GT911**, 5-point capacitive, I2C **SDA=GPIO8, SCL=GPIO9**, INT=GPIO4, RST=CH422G EXIO1, address 0x5D | same |
| IO expander | **CH422G** on the same I2C bus (0x20–0x27 / 0x30–0x3F reserved). EXIO1=touch reset, EXIO2=backlight enable, EXIO3=LCD reset (per the BSP), EXIO4=SD CS (active low), **EXIO5=USB/CAN mux select** (low=USB, high=CAN) | CH422G: EXIO1 touch reset, EXIO2 backlight, EXIO4 SD CS, EXIO0/EXIO5 = isolated DI0/DI1, OD0/OD1 = isolated DO0/DO1 |
| Backlight | **On/off only**, through CH422G EXIO2 and an MP3302 boost converter. The ESP32_Display_Panel board file uses `BACKLIGHT_TYPE_SWITCH_EXPANDER`, so **no PWM dimming** out of the box. | same **(unverified whether any hardware PWM path exists)** |
| TF/SD | **SPI**: MOSI=GPIO11, SCK=GPIO12, MISO=GPIO13, CS=EXIO4. CS goes through the expander, so the demos use SPI mode. SDMMC 1-bit may be possible by reusing these pins, but it is not supported by Waveshare **(unverified)**. | SPI, same pins |
| USB | Two Type-C ports: one for native USB (GPIO19/20, muxed with CAN through FSUSB42 and EXIO5) and one for the CH343P USB-UART used for flashing and logs | Type-C native USB (GPIO19/20). The 4.3B wiki lists RS485 on GPIO43/44 (the UART0 pins), so whether it has a separate USB-UART chip is **unverified**. |
| RTC | none listed | **PCF85063** on I2C 8/9 with a CR927 backup cell |
| Battery | 3.7 V Li-ion header, CS8501 charge/boost, 580 mA charge current. Connector is listed as PH2.0 in one place and PH1.25 in another **(verify)**. | MX1.25 header with a battery switch, same CS8501 |
| Power | 5 V USB, about 450 mA | 5 V USB **or 7–36 V DC terminal** |
| CAN / RS485 | TJA1051 CAN and SP3485 RS485 **share GPIO15/16**, so only one can be used at a time | CAN on GPIO15/16, RS485 on GPIO43/44, both on screw terminals |
| Other | HY2.0 headers for I2C, ADC and sensor; BOOT and RESET buttons | 2 opto-isolated DI and 2 DO (5–36 V); I2C on a terminal |
| Size | 106.1 x 67.8 mm | 112.4 x 75.1 mm bare, **116.3 x 79 mm with case** |

RGB data pins (the same on both boards): B3..B7 = 14, 38, 18, 17, 10; G2..G7 = 39, 0, 45, 48, 47, 21; R3..R7 = 1, 2, 42, 41, 40; HSYNC=46, VSYNC=3, DE=5, PCLK=7. That leaves very few free GPIOs, which doesn't matter for this app.

Sources:
- 4.3 wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3
- 4.3B wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3B
- New docs FAQ (ST7262 / GT911, EXIO5): https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3/FAQ
- Product page, 4.3 (SKUs, no case): https://www.waveshare.com/esp32-s3-touch-lcd-4.3.htm
- Product page, 4.3B (Standard SKU 27848, Case SKU 28141): https://www.waveshare.com/product/esp32-s3-touch-lcd-4.3b.htm?sku=28141
- Board file with timings and expander pins: https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/src/board/supported/waveshare/BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_4_3.h
- Schematics: https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3/manual/ESP32-S3-Touch-LCD-4.3-Sch.pdf and https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3B/ESP32-S3-Touch-LCD-4.3B-Sch.pdf

## 2. Frameworks, versions, gotchas

**Official Waveshare support.** Waveshare does not publish a GitHub repo per board. The demos ship as a ZIP: https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3/ESP32-S3-Touch-LCD-4.3-Demo.zip. The 4.3B ZIP is named `...4.3B-BOX-Demo.zip`. I did not download or inspect either ZIP's `idf_component.yml`, so the exact component versions are **unverified**.

- **ESP-IDF.** The docs screenshots use **v5.5.2**, and the FAQ recommends 5.5.2 for an RS485 fix. Older benchmarks were run on v5.3. The demos include `08_lvgl_v8_demo` (LVGL 8.4) and `09_lvgl_v9_demo` (LVGL 9.x). Source: https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3/ESP-IDF
- **Arduino.** Uses arduino-esp32 **3.0.7** (3.0.6+ adds board selection; else use "ESP32S3 Dev Module", 16 MB flash, PSRAM "OPI"). Libraries: ESP32_Display_Panel >= 0.1.4, ESP32_IO_Expander >= 0.0.4, LVGL 8.4.0; there is also a v9 demo. Source: https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3/Arduino
  - ESP32_Display_Panel (currently v1.0.x) lists both the 4.3 and the 4.3B as supported boards: https://github.com/esp-arduino-libs/ESP32_Display_Panel
- **Third-party IDF BSP.** `bobscott45/waveshare_esp32_s3_touch_lcd_4_3` v1.0.7 supports IDF 5.1 and 6.x and LVGL 8 or 9, and recommends a bounce buffer height of 120 lines. It does not cover the SD card. https://components.espressif.com/components/bobscott45/waveshare_esp32_s3_touch_lcd_4_3/versions/1.0.7/readme
- **Current upstream** (for context): ESP-IDF v6.0 shipped 2026-03-27, with v6.0.1 after; it has breaking changes such as removed legacy drivers and Picolibc (https://esp32.co.uk/esp-idf-6-0-released-biggest-changes-for-esp32-developers/). LVGL v9.5.0 shipped 2026-02-18 (https://lvgl.io/docs/open/9.5/CHANGELOG). **Pin to IDF 5.5.x first**, because that is what Waveshare validated, and move to 6.x later.
- **PlatformIO.** The official `platform-espressif32` still lacks arduino-esp32 3.x (issue #1225). Use the community **pioarduino** fork, or plain ESP-IDF through PlatformIO. Using `idf.py` and the VS Code ESP-IDF extension directly is simpler. Sources: https://community.platformio.org/t/esp32-arduino-core-3-x-and-platformio-status/42008 and https://github.com/sivar2311/platform-espressif32-versions

**RGB panel gotchas** (from Espressif):
- **Drift or shifted image.** The RGB DMA streams the framebuffer from PSRAM continuously. PSRAM shares the SPI bus with flash, so a flash write disables cache, and Wi-Fi/BLE/OTA/NVS/LittleFS writes or heavy PSRAM reads can starve the LCD DMA. The image then shifts permanently.
  - Fixes: XIP from PSRAM (`CONFIG_SPIRAM_FETCH_INSTRUCTIONS` and `CONFIG_SPIRAM_RODATA`), a **bounce buffer** (about 10 lines or more of internal SRAM; 10 x 800 x 4 bytes per Espressif), a 64 B data cache line, and `CONFIG_LCD_RGB_RESTART_IN_VSYNC` as a last-resort safety net.
  - For short flash operations, an alternative is to drop PCLK to about 6 MHz during the operation.
  - Sources: https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html and https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html
- **PCLK ceilings.** The limit is about 22 MHz with Octal PSRAM at 80 MHz and about 30 MHz at 120 MHz. Waveshare uses **16 MHz** (board file) or **21 MHz** (benchmark). At 800x480 plus porches, 16 MHz gives about 39 Hz refresh, which is fine for a menu UI.
- **Bounce-buffer mode** requires `CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y`. Avoid long critical sections while it is active.
- **Wi-Fi.** If Wi-Fi still causes drift after the steps above, try disabling `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`. This app doesn't strictly need Wi-Fi, so leaving it off removes most of the risk.
- **Recommended sdkconfig** (Waveshare and Espressif agree):

```
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_120M=y        # must match PSRAM speed
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_IDF_EXPERIMENTAL_FEATURES=y
CONFIG_SPIRAM_SPEED_120M=y               # experimental; fall back to 80M if unstable (>65 C risk)
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
CONFIG_SPIRAM_RODATA=y
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_LCD_RGB_RESTART_IN_VSYNC=y        # safety net
# LVGL 9: CONFIG_LV_OS_FREERTOS=y, LV_USE_CLIB_MALLOC/STRING/SPRINTF=y, LV_DEF_REFR_PERIOD=15
```

- **Partition table** (16 MB) suggestion. Factory-only gives more asset room; use OTA only if the device will get Wi-Fi updates.

```
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x6000
phy_init, data, phy,     0xf000,  0x1000
factory,  app,  factory, 0x10000, 4M
storage,  data, littlefs,,        11M      # images + recipes.json (~11.9 MB left)
```

  With OTA instead (2 x 3 MB `ota_0`/`ota_1` plus `otadata`), about 9.9 MB remains for assets.

## 3. Performance at 800x480

- **Waveshare benchmark** (IDF 5.3, LVGL 8): single-core LVGL benchmark about **26 FPS**, with the panel interface at about 41 Hz at PCLK 21 MHz. Source: the 4.3 wiki and https://files.waveshare.com/wiki/common/Performance.pdf
- **Espressif ESP32-S3-LCD-EV-Board 800x480 RGB**, `lvgl_demos` average FPS: **12 to 16 FPS on LVGL 8 and 9 to 11 FPS on LVGL 9.2** with a full-screen Direct (avoid-tearing) buffer in PSRAM. Buffer placement and size dominate; PSRAM buffers are slower than SRAM partial buffers. Source: https://github.com/espressif/esp-bsp/blob/master/components/esp_lvgl_port/docs/performance.md
- **Community reports.** Scrolling large widgets such as tables or long lists on S3 RGB boards visibly drops FPS. Forum consensus is that the S3's PSRAM bandwidth is the limit, and the ESP32-P4 is the board for "smooth" UIs. Source: https://forum.lvgl.io/t/fps-drop-during-scrolling/23425
- **Realistic expectation:**
  - **About 20–30 FPS for simple scrolling lists**: flat styles, no opacity or shadows, pre-decoded RGB565 thumbnails, a small number of visible children.
  - **About 10–15 FPS during heavy full-screen redraws.**
  - Mitigations:
    - Use `lv_obj` recycling or virtualised list items; don't create 250 rows.
    - Use a grid of about 12 cards per page or paginated swipes instead of one long scroll.
    - Avoid gradients, shadows and alpha.
    - Pin LVGL to core 1 with the RGB ISR on the same core.
    - Avoid-tearing mode uses two full PSRAM framebuffers (2 x 768 KB = 1.5 MB). Partial SRAM draw buffers combined with bounce buffers can render faster but may tear. Benchmark both.
- **JPEG decoding:**
  - **`esp_new_jpeg`** (Espressif, uses S3 SIMD) decodes **baseline only** to RGB565. At 320x240 4:2:0 it runs about **66 FPS, roughly 15 ms**; at 480x320 about 28 ms; at 800x480 about 59 ms; with about 10 KB of DRAM. It also supports 90/180/270 rotation. Output buffers must be aligned (`jpeg_calloc_align`). Sources: https://components.espressif.com/components/espressif/esp_new_jpeg/versions/1.0.2/readme and https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/solution-introduction/multimedia/component-description/esp-new-jpeg.html
  - **LVGL TJPGD** (built in, `LV_USE_TJPGD`) is portable and works in the simulator but is slower. It is baseline-only and decodes in tiles. **(No S3 timing found; expect several times slower than esp_new_jpeg; unverified.)**
  - libjpeg-turbo is desktop/Linux only.
  - Plan: write a small LVGL image decoder (or a pre-load step) that decodes with `esp_new_jpeg` into a PSRAM `lv_draw_buf` on device and with TJPGD or libjpeg-turbo in the simulator. Use the LVGL image cache (PSRAM) for recently viewed photos.
- **PNG:** lodepng (`LV_USE_LODEPNG`) is slow and RAM-hungry on MCUs, so avoid it for photos. Use PNG only for small icons with alpha, or convert icons to LVGL C arrays or binary at build time.

## 4. Storage

- **Flash budget:** 16 MB total, an app of about 2–3 MB (LVGL plus fonts, no Wi-Fi), leaving about 10–12 MB for assets.

| Plan | Per drink | 250 drinks | Fits in an 11 MB partition? |
|---|---|---|---|
| Detail JPEG at 40 KB only | 40 KB | 10.0 MB | barely (LittleFS overhead about 5–10%) |
| Detail JPEG at 25 KB + JPEG thumbnail at 8 KB | 33 KB | 8.3 MB | yes |
| Detail JPEG at 25 KB + RGB565 120x160 thumbnail at 38 KB | 63 KB | 15.8 MB | **no**; use an 96x128 RGB565 thumbnail (24.6 KB) for 12.4 MB, or put the thumbnails on SD |
| 100 drinks, any of the above | | 3–6 MB | yes |

- **Conclusion:** 250 detail JPEGs at about 25–30 KB plus small JPEG thumbnails fit comfortably in internal flash. The SD card is only *needed* if the thumbnails are pre-decoded RGB565 or the collection grows past about 300 drinks.
- **Filesystem choice:** LittleFS (`joltwallet/littlefs` component) or **memory-mapped assets** (`espressif/esp_mmap_assets`, which packs files into a raw partition and reads them via mmap). Mmap avoids the filesystem and the SPI1 read path. Both suit read-only assets. Flash *writes* disable cache and can cause drift unless XIP-PSRAM plus bounce buffers are enabled, so keep the asset partition read-only at runtime.
- **SD card** (SPI, CS through the expander) runs on a separate SPI bus and does not touch the flash cache. Throughput is roughly 1–2 MB/s at 20 MHz SPI **(unverified on this board)**, so a 30 KB file takes about 20 ms, which is acceptable. Downsides: the card can be removed, the BOX case's slot accessibility is **unverified**, and it adds FAT plus card-quality variability.
- **Favorites:** **NVS**, stored as a single blob such as a 256-bit bitset or a list of uint16 recipe IDs, under a namespace like `fav`. NVS is designed for small key/value data, is wear-levelled, and tolerates power loss. Writes are rare and tiny, so a short cache-disable is acceptable; with XIP-PSRAM enabled it is a non-issue. LittleFS is fine too but adds nothing here. Only put favorites on SD if they must travel with the card.
- **Recipe data:** JSON or a compact binary (recipes, ingredients, season and flavor tags) in the same flash partition, loaded into PSRAM at boot. 250 recipes are well under 1 MB, and filtering in RAM is instant.

## 5. Stack comparison

| Option | Verdict |
|---|---|
| **ESP-IDF 5.5 + LVGL 9 (esp_lvgl_port)** | **Recommended.** Gives full sdkconfig control (PSRAM 120 MHz, XIP, bounce buffers, cache line), uses the Waveshare-validated IDF version, provides `esp_new_jpeg`, and gives a clean partition table and component manager. |
| Arduino + ESP32_Display_Panel + LVGL | Fastest to "hello world", but the prebuilt IDF libs fix many of the critical Kconfig options. Most community drift complaints come from Arduino builds, and Waveshare's Arduino path is pinned to core 3.0.7 and LVGL 8.4. Acceptable for prototyping only. |
| ESPHome (LVGL component) | Built for HA dashboards. Wrong fit for a 250-image browser with custom filtering. |
| SquareLine Studio | LVGL ended its partnership (https://forum.lvgl.io/t/lvgl-ends-its-collaboration-with-squareline-studio/14638), the free tier was cut back, and LVGL 9 support is unclear. Avoid. |
| EEZ Studio | Free and open-source LVGL designer (https://forum.lvgl.io/t/free-and-open-source-eez-studio/12954). Optional for layout mock-ups. A list/detail/filter UI is easy to hand-code. |
| LVGL Pro / XML editor | Integrated with `lv_port_pc_vscode` (`-DLVGL_PRO_PROJECT_DIR`). Optional; worth a look if a visual editor is wanted. |

**Desktop simulator:** https://github.com/lvgl/lv_port_pc_vscode runs on macOS (`brew install sdl2 cmake`) with SDL, CMake and VS Code. Practical approach:
- Share `ui/` and `app/` (filtering, favorites, model) between the simulator and firmware.
- Put a `hal_storage`, `hal_favorites` and `hal_image` interface behind them: POSIX files plus JSON on desktop, LittleFS plus NVS plus `esp_new_jpeg` on device.
- Lock the window to 800x480.

Caveat: the simulator says nothing about S3 frame rates. Get a scrolling-list benchmark running on hardware in week 1.
