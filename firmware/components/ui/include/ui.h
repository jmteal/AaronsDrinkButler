/*
 * Aaron's Drink Butler: LVGL user interface (320x480 portrait).
 *
 * Shared by the firmware and the desktop simulator. Everything platform
 * specific comes in through ui_config_t:
 *  - ui_platform_t: battery, brightness, sound, sleep timeout, clock
 *  - ui_image_provider_t: drink photos/thumbnails (simulator: files on disk;
 *    device: JPEGs decoded into a PSRAM cache)
 *  - adb_storage_t: favorites / pantry / settings persistence
 * Every callback is optional; missing ones fall back to sensible defaults.
 *
 * Call ui_init() once after lv_init() and the display/indev are set up.
 */
#ifndef ADB_UI_H
#define ADB_UI_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "lvgl.h"
#include "adb_data.h"
#include "adb_filter.h"
#include "adb_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_HOR_RES 320
#define UI_VER_RES 480

typedef enum {
    UI_SOUND_TAP,      /* generic button */
    UI_SOUND_TOGGLE,   /* checkbox / chip */
    UI_SOUND_CLINK,    /* favorite added, "surprise me" */
    UI_SOUND_BACK,
} ui_sound_t;

typedef struct {
    int  (*battery_percent)(void *ctx);           /* 0..100, -1 = unknown */
    int  (*battery_mv)(void *ctx);                /* -1 = unknown */
    bool (*is_charging)(void *ctx);
    void (*set_brightness)(void *ctx, uint8_t percent);
    void (*set_sleep_timeout)(void *ctx, uint16_t seconds);   /* 0 = never */
    void (*set_sound)(void *ctx, bool on, uint8_t volume);
    void (*play_sound)(void *ctx, ui_sound_t s);
    bool (*get_time)(void *ctx, struct tm *out);  /* false = clock not set */
    const char *version;                          /* shown in Settings */
    void *ctx;
} ui_platform_t;

/*
 * Images. Return any source accepted by lv_image_set_src() (a path such as
 * "A:/data/images/negroni.jpg", or an lv_image_dsc_t / lv_draw_buf_t
 * pointer that stays valid until the next call for the same kind), or NULL
 * to use the generated glass icon.
 */
typedef struct {
    const void *(*thumb)(void *ctx, const adb_db_t *db, uint16_t recipe);   /* 96x96 */
    const void *(*photo)(void *ctx, const adb_db_t *db, uint16_t recipe);   /* 320x240 */
    void *ctx;
} ui_image_provider_t;

typedef struct {
    adb_db_t                  *db;
    adb_state_t               *state;
    const adb_storage_t       *storage;   /* may be NULL: nothing is saved */
    const ui_platform_t       *platform;  /* may be NULL */
    const ui_image_provider_t *images;    /* may be NULL: generated icons */
} ui_config_t;

void ui_init(const ui_config_t *cfg);

/* Re-read battery/clock now (also refreshed by a 10 s timer). */
void ui_refresh_status(void);

/* ---- Navigation (used by the simulator's screenshot script and tests) ---- */

typedef enum { UI_TAB_HOME, UI_TAB_BROWSE, UI_TAB_MYBAR, UI_TAB_FAVORITES, UI_TAB_COUNT } ui_tab_t;

void ui_show_tab(ui_tab_t tab);
void ui_close_overlays(void);
void ui_open_detail(uint16_t recipe);
void ui_open_filter(void);
void ui_open_search(const char *initial_text);
void ui_open_settings(void);
void ui_open_credits(void);
/* My Bar: false = ingredient checklist, true = "what can I make" results;
 * bucket 0..2 = can make / missing 1 / missing 2. */
void ui_mybar_show(bool results, int bucket);
/* Browse filter access (e.g. to preset filters before a screenshot). */
adb_filter_t *ui_browse_filter(void);
void ui_browse_apply_filter(void);
/* Expand a filter-sheet section by tag kind (ADB_TAG_COUNT = ingredients). */
void ui_filter_set_expanded(int kind, bool expanded);
/* Call after changing db/state outside the UI. */
void ui_refresh_all(void);
/* Scroll the topmost overlay's scrolling body (detail, filter, settings) to y
 * (or to the end if y < 0). For screenshots and UI tests. */
void ui_scroll_overlay_to(int32_t y);
/* The Browse page's recycled list (for scroll benchmarks). */
lv_obj_t *ui_browse_list(void);
/* Number of row objects the list keeps alive (the rest are virtual). */
uint32_t ui_list_pool_size(lv_obj_t *list);

#ifdef __cplusplus
}
#endif
#endif
