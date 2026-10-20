#!/usr/bin/env bash
# Records the simulator's demo tour and turns it into a video and a GIF:
#
#   tools/demo/make_demo.sh [OUT_DIR]        (default build/demo/, which git ignores)
#
# OUT_DIR/demo.mp4  H.264, 640x960 (2x, nearest neighbour), 15 fps, with the real Negroni
#                   clip as audio when the speech cache has it (build.py pack)
# OUT_DIR/demo.gif  10 fps, sized to stay under GIF_MAX_MB (8 MB)
# OUT_DIR/frames/   the raw 320x480 PNG frames from `drink_butler_sim --record`
#
# DEMO_CLIP=path/to/clip.opus overrides the cached clip.
# Needs CMake, ffmpeg (brew install ffmpeg) and, for the audio, opusdec (brew install opus-tools).
set -euo pipefail

REPO=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$REPO/build/demo}
BUILD=$REPO/simulator/build-headless
DRINK=negroni            # the drink the tour opens and speaks
GIF_MAX_MB=${GIF_MAX_MB:-8}

die() { echo "make_demo: $*" >&2; exit 1; }
command -v ffmpeg >/dev/null && command -v ffprobe >/dev/null || die "ffmpeg not found: brew install ffmpeg"
command -v cmake >/dev/null || die "cmake not found: brew install cmake"

echo "== building the headless simulator"
cmake -S "$REPO/simulator" -B "$BUILD" -DADB_SIM_HEADLESS=ON >/dev/null
cmake --build "$BUILD" -j >/dev/null

mkdir -p "$OUT"
WAV=$OUT/$DRINK.wav
rm -f "$WAV"
SPEECH_ARGS=()
# DEMO_CLIP=file.opus uses that recording instead of the cached one
if clip=${DEMO_CLIP:-$(python3 "$REPO/tools/data/build.py" say "$DRINK" --path 2>/dev/null)} && [ -f "$clip" ] \
        && command -v opusdec >/dev/null; then
    opusdec --quiet "$clip" "$WAV"
    ms=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$WAV" | awk '{printf "%d", $1 * 1000}')
    SPEECH_ARGS=(--speech-ms "$ms")   # the stop icon shows for as long as the real clip plays
    echo "== audio: $clip ($ms ms)"
else
    echo "== audio: no cached $DRINK clip (run tools/data/build.py pack, needs opus-tools); video will be silent"
fi

echo "== recording the tour"
rm -rf "$OUT/frames"
"$BUILD/drink_butler_sim" --record "$OUT/frames" ${SPEECH_ARGS[@]+"${SPEECH_ARGS[@]}"}
FPS=$(awk '$1 == "fps" {print $2}' "$OUT/frames/events.txt")
SPEAK_MS=$(awk '$1 == "speak_ms" {print $2}' "$OUT/frames/events.txt")
DURATION_MS=$(awk '$1 == "duration_ms" {print $2}' "$OUT/frames/events.txt")
FRAMES=("-framerate" "$FPS" "-i" "$OUT/frames/frame_%05d.png")

echo "== demo.mp4"
# 2x nearest neighbour in RGB, then 4:2:0 without dithering: dither noise differs in every
# frame and made the file ten times bigger.
UPSCALE="scale=640:960:flags=neighbor:sws_dither=none,format=yuv420p"
if [ -f "$WAV" ]; then
    ffmpeg -hide_banner -loglevel error -y "${FRAMES[@]}" -i "$WAV" \
        -vf "$UPSCALE" -c:v libx264 -crf 18 -preset slow \
        -af "adelay=${SPEAK_MS}:all=1,apad" -c:a aac -b:a 96k -shortest -movflags +faststart "$OUT/demo.mp4"
else
    ffmpeg -hide_banner -loglevel error -y "${FRAMES[@]}" \
        -vf "$UPSCALE" -c:v libx264 -crf 18 -preset slow \
        -movflags +faststart "$OUT/demo.mp4"
fi

echo "== demo.gif"
PAL=$OUT/palette.png
GIF_FRAMES=$((DURATION_MS / 100))
for w in 640 480 400 320; do   # the largest size that fits under GIF_MAX_MB
    SCALE="fps=10,scale=$w:-1:flags=lanczos"
    ffmpeg -hide_banner -loglevel error -y "${FRAMES[@]}" -vf "$SCALE,palettegen=stats_mode=diff" -update 1 "$PAL"
    # The palette is looped and the frame count given: ffmpeg 9 drops frames at random when
    # paletteuse's one-frame palette input reaches its end (same with split + palettegen).
    ffmpeg -hide_banner -loglevel error -y "${FRAMES[@]}" -loop 1 -i "$PAL" -lavfi \
        "[0:v]$SCALE[v];[v][1:v]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle" \
        -frames:v "$GIF_FRAMES" "$OUT/demo.gif"
    [ "$(stat -f %z "$OUT/demo.gif")" -le $((GIF_MAX_MB * 1024 * 1024)) ] && break
done
rm -f "$PAL"

ls -lh "$OUT/demo.mp4" "$OUT/demo.gif" | awk '{print $5, $NF}'
