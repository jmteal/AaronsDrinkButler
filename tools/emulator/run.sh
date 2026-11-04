#!/usr/bin/env bash
# Build the emulator variant of the firmware and run it in Espressif's QEMU (ESP32-S3).
# See firmware/EMULATOR.md.
#
#   tools/emulator/run.sh                 build, flash image, run, open the viewer in a browser
#   tools/emulator/run.sh --test [DIR]    headless check: boot, screenshot splash + Home, quit
#   options:
#     --fresh          start from erased flash (forgets favorites and settings)
#     --pack FILE      use this recipe pack (default: build one without speech audio)
#     --no-build       skip idf.py build (reuse firmware/build-emulator)
#     --port N         viewer port (default 8765)
#     --no-browser     don't open the browser
#     --clean          delete firmware/build-emulator first
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
FW="$ROOT/firmware"
BUILD="$FW/build-emulator"

fresh=() ; pack="" ; build=1 ; run_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --fresh) fresh=(--fresh) ;;
        --pack) pack="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"; shift ;;
        --no-build) build=0 ;;
        --port) run_args+=(--port "$2"); shift ;;
        --no-browser) run_args+=(--no-browser) ;;
        --clean) rm -rf "$BUILD" ;;
        --test)
            if [[ $# -gt 1 && "$2" != --* ]]; then dir="$2"; shift; else dir="$BUILD/test"; fi
            run_args+=(--test "$dir") ;;
        --verbose) run_args+=(--verbose) ;;
        -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
        *) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
    esac
    shift
done

say() { printf '\033[36m[emu] %s\033[0m\n' "$*" >&2; }

# ESP-IDF environment (idf.py, IDF's Python, and qemu-system-xtensa once installed)
if ! command -v idf.py >/dev/null 2>&1; then
    export IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"
    [[ -f "$IDF_PATH/export.sh" ]] || { echo "ESP-IDF not found at $IDF_PATH (set IDF_PATH)" >&2; exit 1; }
    say "loading ESP-IDF from $IDF_PATH"
    # shellcheck disable=SC1091
    . "$IDF_PATH/export.sh" >/dev/null
fi
if ! command -v qemu-system-xtensa >/dev/null 2>&1; then
    say "installing Espressif QEMU (qemu-xtensa)"
    python "$IDF_PATH/tools/idf_tools.py" install qemu-xtensa
    . "$IDF_PATH/export.sh" >/dev/null
fi
if ! qemu-system-xtensa --version >/dev/null 2>&1; then
    echo "qemu-system-xtensa won't start. It needs these Homebrew libraries:" >&2
    echo "    brew install libgcrypt glib pixman sdl2" >&2
    qemu-system-xtensa --version || true
    exit 1
fi

if [[ $build -eq 1 ]]; then
    say "building the emulator variant in firmware/build-emulator"
    # Own build dir and sdkconfig: the real-board build in firmware/build is not touched
    mkdir -p "$BUILD"
    (cd "$FW" && idf.py -B "$BUILD" -DSDKCONFIG="$BUILD/sdkconfig" \
        -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.emulator" -DIDF_TARGET=esp32s3 build \
        > "$BUILD/build.log" 2>&1) || { tail -40 "$BUILD/build.log"; echo "build failed, full log: $BUILD/build.log" >&2; exit 1; }
fi
[[ -f "$BUILD/flash_args" ]] || { echo "no emulator build in $BUILD (drop --no-build)" >&2; exit 1; }

if [[ -z "$pack" ]]; then
    pack="$BUILD/drinks.bin"
    say "packing recipes (no speech audio; the emulator is silent)"
    python "$ROOT/tools/data/build.py" pack --no-audio -o "$pack" >&2
fi

python "$ROOT/tools/emulator/emu.py" image --build "$BUILD" --pack "$pack" "${fresh[@]+"${fresh[@]}"}"
exec python "$ROOT/tools/emulator/emu.py" run --build "$BUILD" "${run_args[@]+"${run_args[@]}"}"
