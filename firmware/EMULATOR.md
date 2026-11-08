# Emulator (QEMU, macOS)

The firmware itself (not the LVGL desktop simulator in `simulator/`) runs on the Mac in
**Espressif's QEMU fork for the ESP32-S3**. It's the same app, data code, LVGL and
FreeRTOS as on the board, built as an emulator variant that swaps the board-specific
hardware for virtual or simulated parts. You see and drive the screen in a browser tab.

```bash
tools/emulator/run.sh
```

That builds the emulator variant in `firmware/build-emulator/` (the real-board build in
`firmware/build/` is never touched), packs the recipes, writes a 16 MB flash image,
starts QEMU and opens the viewer at <http://127.0.0.1:8765/>. Boot to the splash takes
about a second. **Ctrl-C** in the terminal stops it.

## Install (once)

ESP-IDF 5.5 at `~/esp/esp-idf` (see [README.md](README.md)), then:

```bash
brew install libgcrypt glib pixman sdl2      # QEMU's shared libraries
python ~/esp/esp-idf/tools/idf_tools.py install qemu-xtensa
```

`run.sh` installs `qemu-xtensa` itself if it's missing and prints the `brew` line if
QEMU can't start. Its binary links Homebrew's SDL2, but the emulator never opens an SDL
window (Homebrew's `sdl2` is now `sdl2-compat` on SDL3, which has crashed here before);
QEMU runs with `-display none` and the browser is the screen.

## Using it

| | |
|---|---|
| Click | tap |
| Click and drag | swipe (scrolls lists, as a finger would) |
| Scroll wheel | a short swipe per notch |
| Command box / the terminal | debug console commands: `help`, `stats`, `tab browse`, `open negroni`, `find gin`, `shot`, `stay on`, `reboot` |
| Save PNG | the current screen |

The serial log prints in the terminal and in the viewer. Favorites and settings are
saved to NVS in `build-emulator/qemu_flash.bin` and survive restarts and rebuilds.

One emulator runs at a time (per build dir). Running `run.sh` again while one is up
doesn't start a second copy, which would rewrite the flash image under the first. It
prints the running viewer's address and opens it. If port 8765 is taken by another
program, the viewer uses a free port and prints it.

Options for `tools/emulator/run.sh`:

| Option | |
|---|---|
| `--fresh` | start from erased flash (forgets favorites and settings) |
| `--pack FILE` | use this recipe pack, e.g. `build/drinks.bin` from `idf.py drinks-flash`. By default it packs `data/` with `--no-audio` (fast, no TTS) |
| `--no-build` | reuse the last emulator build |
| `--clean` | delete `build-emulator/` first |
| `--port N` | viewer port (default 8765) |
| `--no-browser` | don't open the browser |
| `--test [DIR]` | headless check for CI or after changes: boots, saves `splash.png`, taps, saves `home.png`, runs `stats`, quits. Exit status 0 on success |

To build by hand: `idf.py -B build-emulator -DSDKCONFIG=build-emulator/sdkconfig -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.emulator" build`.

## What's emulated and what isn't

The emulator variant is `CONFIG_ADB_TARGET_EMULATOR` ([sdkconfig.emulator](sdkconfig.emulator)).
It compiles [components/bsp/bsp_emulator.c](components/bsp/bsp_emulator.c) instead of the
board's BSP files; the app, `data` and `ui` components are the same code. The real-board
build doesn't set the option, compiles the same files as before, and produces the same
code and data (checked by comparing the images with the app description left out).

| Part | Board | Emulator |
|---|---|---|
| CPU, FreeRTOS, app, LVGL 9.4 | ESP32-S3 at 240 MHz | QEMU ESP32-S3 (Xtensa, both cores). Runs as fast as the Mac allows, not cycle-accurate |
| Flash | 16 MB, `drinks` partition | 16 MB image file, same partition table, pack at 0x310000 |
| PSRAM | 8 MB octal | 8 MB octal (QEMU `ssi_psram`) |
| NVS | flash | flash image (persists) |
| Display | ST77922 over QSPI, 320×480 | QEMU virtual RGB panel, 320×480 RGB565. LVGL renders full frames into the panel's VRAM (direct mode) instead of partial DMA buffers |
| Touch | Sitronix ST7123, I2C 0x55 | none. The browser sends press/move/release to the debug console's LVGL pointer (`down`/`move`/`up`, emulator only) |
| Backlight | LEDC PWM, 0-100 % | on/off only: 0 shows black, any other level shows the UI at full brightness |
| Audio | ES8311 + amp | silent. Sounds and speech still take their real playing time. The default pack has no speech, so the speak button is hidden; pass `--pack build/drinks.bin` for a pack with speech |
| Battery | ADC on GPIO8 | fixed 3950 mV (shows 72 %) |
| Light sleep | touch INT / BOOT / timer wake | simulated: LVGL pauses and the screen goes black; a click (swallowed, as on the board) or the timer wakes it |
| Deep sleep | BOOT wake, chip restart | simulated: black until a click, then `esp_restart()` |
| Console | USB-Serial/JTAG | UART0 (QEMU has no USB-Serial/JTAG). Idle sleep isn't held off by a "USB host" |
| microSD | SDMMC | not available (unused by the app) |
| Wi-Fi / BT | off | off |

Things it can't tell you: real speed (JPEG decode times, frame rate, QSPI bandwidth),
panel quirks (4-pixel windows, byte swap, inversion, tearing), touch feel and
controller behaviour, audio quality, battery readings, current draw and real sleep
wake-up. Test those on the board. The QEMU log's `rtcinit: o_code calibration fail` and
`MSPI Timing: tuning fail` warnings are expected under emulation.

## How it works

- [`tools/emulator/run.sh`](../tools/emulator/run.sh): loads ESP-IDF, builds, packs,
  then calls `emu.py`.
- [`tools/emulator/emu.py`](../tools/emulator/emu.py) (standard library only):
  `image` writes the bootloader, partition table, app (from `build-emulator/flash_args`)
  and the pack into `qemu_flash.bin` in place. `run` starts `qemu-system-xtensa -M esp32s3`
  paused, with the serial port and QMP on local TCP ports, then serves the viewer on
  127.0.0.1. Frames come from QMP `screendump`; mouse events go to the serial console.
- With no window, QEMU copies a frame from the panel's VRAM only when asked
  (`screendump`), so the emulator BSP never waits for the panel to finish an update
  (the `esp_lcd_qemu_rgb` driver does, which is one reason it isn't used).

## Why QEMU and not Wokwi

Both were checked in November 2026.

- **Espressif QEMU** ([espressif/qemu](https://github.com/espressif/qemu), ESP-IDF's
  [QEMU guide](https://github.com/espressif/esp-toolchain-docs/blob/main/qemu/esp32s3/README.md)):
  free and local, installed by `idf_tools.py`, and `idf.py qemu` supports the ESP32-S3 in
  IDF 5.5, including octal PSRAM and a 16 MB flash image. It has a virtual RGB display
  (up to 800×600, RGB565 or ARGB8888; [esp_rgb.c](https://github.com/espressif/qemu/blob/esp-develop/hw/display/esp_rgb.c),
  [esp_lcd_qemu_rgb](https://components.espressif.com/components/espressif/esp_lcd_qemu_rgb)).
  No touch or mouse device and no USB-Serial/JTAG, hence the console pointer and UART0.
  Its only display backends in the macOS build are `sdl` and `none` (no Cocoa, no VNC),
  hence the browser viewer.
- **Wokwi** ([VS Code](https://docs.wokwi.com/vscode/getting-started), [wokwi-cli](https://github.com/wokwi/wokwi-cli)):
  needs a Wokwi account and licence (VS Code) or an API token (CLI), and the simulation
  runs on Wokwi's servers. Its stock TFT ([ILI9341](https://docs.wokwi.com/parts/wokwi-ili9341))
  is 240×320, so the 320×480 UI wouldn't fit without a custom chip.
