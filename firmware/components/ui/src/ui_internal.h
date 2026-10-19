/* Shared state and helpers between the UI screens. Not a public header. */
#ifndef ADB_UI_INTERNAL_H
#define ADB_UI_INTERNAL_H

#include "ui.h"
#include "adb_filter.h"
#include "adb_pantry.h"

/* ---- Layout ----------------------------------------------------------- */
#define UI_STATUS_H   24
#define UI_TABBAR_H   56
#define UI_CONTENT_H  (UI_VER_RES - UI_STATUS_H - UI_TABBAR_H)   /* 400 */
#define UI_OVERLAY_H  (UI_VER_RES - UI_STATUS_H)                 /* 456 */
#define UI_ROW_H      96
#define UI_THUMB      96
#define UI_TOUCH      44
#define UI_PAD        8
#define UI_PHOTO_W    320
#define UI_PHOTO_H    240

/* ---- Theme ------------------------------------------------------------ */
#define UI_C_BG        0x14110F   /* app background */
#define UI_C_SURFACE   0x1F1A17   /* rows, cards */
#define UI_C_SURFACE2  0x2B2420   /* raised controls, chips */
#define UI_C_DIVIDER   0x3A312B
#define UI_C_TEXT      0xF3E9DC
#define UI_C_MUTED     0xA8988A
#define UI_C_ACCENT    0xE3A548   /* warm amber */
#define UI_C_ACCENT2   0xC8743C   /* copper */
#define UI_C_ON_ACCENT 0x1A1410
#define UI_C_HEART     0xE5484D
#define UI_C_OK        0x7DBE6E
#define UI_C_BAD       0xE06A5A
#define UI_C_STATUS    0x0C0A09

LV_FONT_DECLARE(adb_font_12)
LV_FONT_DECLARE(adb_font_14)
LV_FONT_DECLARE(adb_font_16)
LV_FONT_DECLARE(adb_font_20)
LV_FONT_DECLARE(adb_font_24)
LV_FONT_DECLARE(adb_font_28)

/* Font Awesome glyphs added to our fonts (see fonts/gen_fonts.sh). */
#define UI_SYM_HEART    "\xEF\x80\x84"
#define UI_SYM_SEARCH   "\xEF\x80\x82"
#define UI_SYM_FILTER   "\xEF\x82\xB0"
#define UI_SYM_COCKTAIL "\xEF\x95\xA1"
#define UI_SYM_MARTINI  "\xEF\x95\xBB"
#define UI_SYM_SUN      "\xEF\x86\x85"
#define UI_SYM_MOON     "\xEF\x86\x86"
#define UI_SYM_STAR     "\xEF\x80\x85"
#define UI_SYM_INFO     "\xEF\x81\x9A"
#define UI_SYM_SLIDERS  "\xEF\x87\x9E"
#define UI_SYM_LEAF     "\xEF\x81\xAC"

static inline lv_color_t ui_c(uint32_t hex) { return lv_color_hex(hex); }

/* ---- Shared state ----------------------------------------------------- */
typedef struct {
    adb_db_t                  *db;
    adb_state_t               *st;
    const adb_storage_t       *io;
    const ui_platform_t       *plat;
    const ui_image_provider_t *img;

    lv_obj_t *root;
    lv_obj_t *status_bar;
    lv_obj_t *content;
    lv_obj_t *tabbar;
    lv_obj_t *pages[UI_TAB_COUNT];
    lv_obj_t *tab_btns[UI_TAB_COUNT];
    lv_obj_t *overlay_layer;
    ui_tab_t  tab;

    adb_filter_t browse_filter;
} ui_ctx_t;

extern ui_ctx_t g_ui;

/* Starts the screensaver idle watch (ui_splash.c). */
void ui_saver_init(void);

/* ---- Theme / widgets (ui_theme.c) ------------------------------------- */
void      ui_theme_init(void);
lv_obj_t *ui_box(lv_obj_t *parent);                       /* style-less container */
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, bool primary);
lv_obj_t *ui_icon_button(lv_obj_t *parent, const char *symbol, int size);
lv_obj_t *ui_chip(lv_obj_t *parent, const char *text, bool checkable);
void      ui_chip_set_checked(lv_obj_t *chip, bool on);
lv_obj_t *ui_small_tag(lv_obj_t *parent, const char *text); /* read-only flavor tag */
lv_obj_t *ui_header(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb);
lv_obj_t *ui_segmented(lv_obj_t *parent, const char *const *labels, int n, int selected,
                       lv_event_cb_t cb, void *user);
void      ui_segmented_select(lv_obj_t *seg, int selected);
static inline int ui_segmented_get(lv_obj_t *seg) { return (int)(intptr_t)lv_obj_get_user_data(seg); }

/* ---- Platform wrappers (ui_main.c) ------------------------------------ */
void ui_sound(ui_sound_t s);
void ui_save_favorites(void);
void ui_save_pantry(void);
void ui_save_settings(void);
bool ui_now(struct tm *out);
int  ui_current_season(void);   /* ADB_SEASON_* (holiday in December) */

/* ---- Overlays (ui_main.c) --------------------------------------------- */
lv_obj_t *ui_overlay_push(void);   /* full area below the status bar */
void      ui_overlay_pop(void);
lv_obj_t *ui_overlay_top(void);

/* ---- Images (ui_icon.c) ----------------------------------------------- */
/* Image source for a recipe thumbnail / photo: provider image or a
 * generated glass icon (cached). */
const void *ui_recipe_thumb(uint16_t recipe);
const void *ui_recipe_photo(uint16_t recipe);
void        ui_icon_cache_clear(void);

/* ---- Virtualized list (ui_vlist.c) ------------------------------------ */
typedef lv_obj_t *(*ui_vlist_create_cb)(lv_obj_t *list, void *user);
typedef void (*ui_vlist_bind_cb)(lv_obj_t *row, uint32_t index, void *user);
typedef void (*ui_vlist_click_cb)(uint32_t index, void *user);

lv_obj_t *ui_vlist_create(lv_obj_t *parent, int32_t row_h, int32_t max_view_h,
                          ui_vlist_create_cb create, ui_vlist_bind_cb bind,
                          ui_vlist_click_cb click, void *user);
void      ui_vlist_set_count(lv_obj_t *list, uint32_t count, bool keep_scroll);
void      ui_vlist_rebind(lv_obj_t *list);
uint32_t  ui_vlist_count(lv_obj_t *list);

/* ---- Drink rows (ui_browse.c), shared by Browse / Favorites / My Bar --- */
lv_obj_t *ui_drink_row_create(lv_obj_t *list, void *user);
/* subtitle_override: NULL for flavor chips, else text shown instead (e.g. "Missing: ..."). */
void      ui_drink_row_bind(lv_obj_t *row, uint16_t recipe, const char *subtitle_override);
/* Pill-shaped button that looks like a search field (opens the search overlay). */
lv_obj_t *ui_search_field(lv_obj_t *parent, lv_obj_t **label_out);

/* ---- Screens ---------------------------------------------------------- */
void ui_home_create(lv_obj_t *page);
void ui_home_refresh(void);
void ui_browse_create(lv_obj_t *page);
void ui_browse_refresh(void);          /* re-run filter */
void ui_favorites_create(lv_obj_t *page);
void ui_favorites_refresh(void);
void ui_mybar_create(lv_obj_t *page);
void ui_mybar_refresh(void);
void ui_detail_refresh_heart(void);
void ui_filter_create(lv_obj_t *overlay);
void ui_search_create(lv_obj_t *overlay, const char *initial);
void ui_settings_create(lv_obj_t *overlay);
void ui_credits_create(lv_obj_t *overlay);
void ui_owner_create(lv_obj_t *overlay);
void ui_home_set_owner(const char *possessive);
void ui_settings_set_owner(void);
void ui_detail_create(lv_obj_t *overlay, uint16_t recipe);

/* Called when favorites change anywhere. */
void ui_on_favorites_changed(void);

#endif
