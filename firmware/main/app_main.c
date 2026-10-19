/*
 * Aaron's Drink Butler: device app.
 *
 * Boot: NVS -> map the recipe pack in the "drinks" partition and load it -> user
 * state from NVS -> BSP display/touch, audio, battery -> shared UI (components/ui).
 * The hardware test app is still available: see firmware/README.md (sdkconfig.hwtest).
 */
#include <inttypes.h>
#include "app.h"
#include "cJSON.h"
#include "bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app";

static adb_pack_t s_pack;

// cJSON makes thousands of small nodes; keep them out of internal RAM, which the
// display DMA buffers and task stacks need (malloc would put them there first).
static void *psram_malloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }

static adb_db_t s_db;
static adb_state_t s_state;
static adb_storage_t s_io;
static ui_platform_t s_platform;
static ui_image_provider_t s_images;

// Shown instead of the UI when there is no usable recipe pack.
static void show_pack_error(const char *why)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1410), 0);
    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_width(l, BSP_LCD_H_RES - 32);
    lv_obj_set_style_text_color(l, lv_color_hex(0xF2E6D8), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text_fmt(l, "No recipes loaded\n\n%s\n\nFlash the recipe pack from the firmware folder:\n\n"
                             "idf.py drinks-flash", why);
    lv_obj_center(l);
}

void app_main(void)
{
    int64_t t0 = esp_timer_get_time();
    bool have_io = app_storage_nvs_init(&s_io);

    char err[96] = "";
    adb_db_init(&s_db);
    int rc = adb_pack_map_partition(&s_pack, true, err, sizeof err);
    if (rc == 0) {
        cJSON_Hooks hooks = { .malloc_fn = psram_malloc, .free_fn = heap_caps_free };
        cJSON_InitHooks(&hooks);
        rc = adb_load_pack(&s_db, &s_pack);
        cJSON_InitHooks(NULL);
        if (rc != 0) snprintf(err, sizeof err, "the recipe pack could not be parsed");
    }
    if (rc == 0) {
        ESP_LOGI(TAG, "loaded %u drinks, %u ingredients in %u ms", s_db.n_recipes, s_db.n_ingredients,
                 (unsigned)((esp_timer_get_time() - t0) / 1000));
        for (int i = 0; i < s_db.n_warnings && i < 16; i++) ESP_LOGW(TAG, "data: %s", s_db.warnings[i]);
    } else {
        ESP_LOGE(TAG, "no recipes: %s", err);
    }

    adb_state_init(&s_state, &s_db);
    if (have_io && rc == 0) adb_state_load(&s_state, &s_db, &s_io);

    lv_display_t *disp = bsp_display_start();
    if (!disp) {
        ESP_LOGE(TAG, "display failed to start");
        return;
    }
    app_platform_init(&s_platform, &s_state.settings);
    app_images_init(&s_pack, &s_db, &s_images);

    lvgl_port_lock(0);
    if (rc == 0) {
        ui_config_t cfg = {
            .db = &s_db,
            .state = &s_state,
            .storage = have_io ? &s_io : NULL,
            .platform = &s_platform,
            .images = &s_images,
            .splash_ms = UI_SPLASH_UNTIL_TAP,
        };
        ui_init(&cfg);
    } else {
        show_pack_error(err);
    }
    lvgl_port_unlock();

    vTaskDelay(pdMS_TO_TICKS(60));       // let the first frame reach the panel
    app_platform_screen_on();
    ESP_LOGI(TAG, "UI up %u ms after boot; PSRAM free %u KB, internal free %u KB",
             (unsigned)((esp_timer_get_time() - t0) / 1000),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

    if (rc == 0) {
        app_images_prewarm_start();
        app_platform_idle_start();
    }
#if CONFIG_ADB_DEBUG_CONSOLE
    app_console_start(&s_db, &s_state);
#endif
}
