# Firmware

ESP-IDF 5.5 + LVGL 9.4 firmware for the Hosyond ES3C35P (ESP32-S3R8, 3.5" 320×480 ST77922 QSPI touch display). Board facts and pin map: [docs/research/board_es3c35p.md](../docs/research/board_es3c35p.md).

Right now `main/` is a **hardware test app** (phase 1b). The real UI (`components/ui`, `components/data`) replaces it in phase 2.

## Set up ESP-IDF (macOS, once)

```bash
xcode-select --install          # if the command-line tools are missing
brew install cmake ninja dfu-util python3
mkdir -p ~/esp && cd ~/esp
git clone -b v5.5.5 --recursive --depth 1 --shallow-submodules https://github.com/espressif/esp-idf.git esp-idf
cd esp-idf && ./install.sh esp32s3
```

Then, in every new shell:

```bash
. ~/esp/esp-idf/export.sh
```

Tested with ESP-IDF **v5.5.5** (installed at `~/esp/esp-idf`). Any 5.4 or 5.5 release should work.

## Build, flash, monitor

```bash
cd firmware
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor     # Ctrl-] quits the monitor
```

The first build downloads the managed components (LVGL, esp_lvgl_port, panel and touch drivers, esp_codec_dev) into `managed_components/`. `sdkconfig` is generated from `sdkconfig.defaults`; edit the defaults, not `sdkconfig`.

USB is the chip's own USB-Serial/JTAG (no CH340), so the port is `/dev/cu.usbmodem*`. If flashing can't connect, hold **BOOT**, tap **RESET**, release BOOT, and flash again.

**Black screen after flashing?** The LCD reset is tied to the chip's power-on reset, so a USB reset does not reset the panel. Unplug the board (and switch off the battery) for a few seconds.

## Layout

| Path | What |
|---|---|
| `components/bsp/` | Board support: display + touch + LVGL, backlight, audio, battery, microSD, sleep. API in [`bsp.h`](components/bsp/include/bsp.h), pins in [`bsp_pins.h`](components/bsp/include/bsp_pins.h) |
| `main/` | Hardware test app |
| `partitions.csv` | 16 MB layout (below) |
| `sdkconfig.defaults` | Octal PSRAM, 16 MB QIO flash, USB console, LVGL options |

### Flash layout

| Name | Type | Offset | Size | Use |
|---|---|---|---|---|
| nvs | data/nvs | 0x9000 | 24 KB | favorites, pantry, settings |
| phy_init | data/phy | 0xF000 | 4 KB | (unused, radio is off) |
| factory | app | 0x10000 | 3 MB | firmware (the test app is ~0.8 MB) |
| drinks | data/0x40, readonly | 0x310000 | 12.9 MB (0xCF0000) | recipe pack built by `tools/data` |

Flash a pack image with `esptool.py --chip esp32s3 write_flash 0x310000 drinks.bin` (or `parttool.py write_partition --partition-name drinks --input drinks.bin`). The app finds it with `esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "drinks")` and can `esp_partition_mmap()` it.

## BSP notes

- **Display**: QSPI at 40 MHz, two 320×48 partial buffers (30 KB each) in internal DMA RAM, an LVGL rounder to 4-pixel boundaries, `swap_bytes` (the panel is big-endian RGB565) and colour inversion on. No rotation (native portrait).
- **Touch**: ST7123 driver at I2C 0x55, polled by esp_lvgl_port. INT (GPIO47) is used only for light-sleep wake.
- **Backlight**: LEDC 25 kHz, 10-bit, squared curve. Off until `bsp_backlight_set()`.
- **Audio**: ES8311 at 16 kHz mono. The BSP switches the amp (GPIO1, low = on) on only while `bsp_audio_play()` runs.
- **Battery**: ADC1 on GPIO8, 12 dB, curve-fit calibration, 32-sample average, ×2. With no battery it reads the charger output (~4.2 V).
- **Sleep**: `bsp_sleep_light()` turns the backlight off, sends SLPIN, pauses LVGL and light-sleeps until touch INT, BOOT or a timer. `bsp_sleep_deep()` wakes only on BOOT and restarts the chip.
- **Wi-Fi / BT**: off. `MINIMAL_BUILD` keeps them out of the build entirely.

## Hardware test app

The screen shows colour bars and a grey ramp, a live readout (battery, last touch, touch-INT edges, free memory), a backlight slider, **Beep / Light sleep / Deep sleep** buttons, and five green crosshairs. Every touch leaves a red dot. Each result is also logged to serial as a line starting with `TEST`.

What to check on the device:

1. **Colours**: the bars read, left to right, red, green, blue, white, black, cyan, magenta, yellow, matching their labels, and the grey ramp goes smoothly from black to white. Inverted colours (a photo negative) mean inversion is wrong; red and blue swapped mean the colour order is wrong; noisy, speckled colours mean the byte swap is wrong.
2. **Touch**: tap each green cross. The red dot should land on its centre (within a few pixels) at all four corners and in the middle.
3. **Backlight**: the slider dims smoothly from 5% to 100% with no flicker or whine.
4. **Sound**: **Beep** plays a short 880 Hz tone through the speaker (needs the speaker plugged in).
5. **Battery**: on battery, the mV reading should match a multimeter on the battery leads within about 50 mV.
6. **Light sleep**: tap **Light sleep**, lift your finger. The screen goes dark. A touch should wake it (status says "Woke by touch"). Try again and wake with **BOOT**. If touch doesn't wake it, the status says so and it wakes after 30 s.
7. **Deep sleep**: tap **Deep sleep**; the screen goes dark. Press **BOOT**: the board restarts into the test app and the log says `woke by BOOT`.
8. Serial log: `TEST i2c: found 0x18 0x55` (0x28 may appear too), `drinks partition found`, PSRAM close to 8 MB (about 8190 KB).
