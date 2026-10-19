#!/bin/sh
# Regenerates the adb_font_<size>.c files: Montserrat Medium with ASCII + Latin-1
# (so "Piña Colada" and "½" render), a few typographic quotes/dashes, LVGL's
# built-in symbols and some extra Font Awesome icons (heart, search, filter...).
#
# Needs: npm install lv_font_conv@1.5.3, and an LVGL 9.4 checkout for the TTF/WOFF.
# Usage: LVGL_DIR=/path/to/lvgl ./gen_fonts.sh
set -e
: "${LVGL_DIR:?set LVGL_DIR to an lvgl checkout}"
CONV=${LV_FONT_CONV:-lv_font_conv}
FONTS="$LVGL_DIR/scripts/built_in_font"
SYMS="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"
# heart, search, filter, cocktail, glass-martini-alt, sun, moon, star, info, sliders, flask, leaf
EXTRA="0xf004,0xf002,0xf0b0,0xf561,0xf57b,0xf185,0xf186,0xf005,0xf05a,0xf1de,0xf0c3,0xf06c"
cd "$(dirname "$0")"
for sz in 12 14 16 20 24 28; do
  "$CONV" --no-compress --no-prefilter --bpp 4 --size $sz \
    --font "$FONTS/Montserrat-Medium.ttf" \
      -r 0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2018-0x2019,0x201C-0x201D,0x2022,0x2026 \
    --font "$FONTS/FontAwesome5-Solid+Brands+Regular.woff" -r $SYMS,$EXTRA \
    --format lvgl -o adb_font_$sz.c --force-fast-kern-format \
    --lv-include lvgl.h --lv-font-name adb_font_$sz
  sed -i.bak "s#$FONTS/#lvgl/scripts/built_in_font/#g" adb_font_$sz.c && rm -f adb_font_$sz.c.bak
done
