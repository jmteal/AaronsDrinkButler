/* Aaron's Drink Butler device app: glue between the BSP and the shared UI/data components. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "adb_data.h"
#include "adb_pack.h"
#include "adb_storage.h"
#include "ui.h"

/* ---- NVS storage (app_storage_nvs.c) ---- */

// Opens the "adb" NVS namespace (initialising the partition) and fills io. False on failure.
bool app_storage_nvs_init(adb_storage_t *io);

/* ---- Drink photos (app_images.c) ---- */

typedef struct {
    uint32_t thumbs_cached, thumb_decodes, thumb_hits;
    uint32_t photo_decodes, photo_hits;
    uint32_t thumb_us_max, photo_us_max;    // slowest decode so far
    uint32_t failures;
    size_t   bytes;                         // PSRAM held by decoded images
} app_images_stats_t;

// JPEGs come straight out of the mapped pack; decoded RGB565 lives in PSRAM.
void app_images_init(const adb_pack_t *pack, const adb_db_t *db, ui_image_provider_t *out);
// Decodes every thumbnail in a low-priority task so lists scroll without decode stalls.
void app_images_prewarm_start(void);
void app_images_get_stats(app_images_stats_t *out);

/* ---- Platform: battery, backlight, sound, idle sleep (app_platform.c) ---- */

void app_platform_init(ui_platform_t *p, const adb_settings_t *settings);
// Turns the backlight on at the saved brightness (called once the first frame is drawn).
void app_platform_screen_on(void);
// Light-sleeps after the UI's screen-off timeout. Needs the BSP and UI running.
void app_platform_idle_start(void);
// Debug: keep the screen awake regardless of the timeout (until reboot).
void app_platform_stay_awake(bool on);

/* ---- Spoken recipes (app_speech.c) ---- */

// Starts the Opus worker task. Clips are speech/<id>.opk in the pack.
void app_speech_init(const adb_pack_t *pack);
bool app_speech_available(const char *drink_id);
// Starts playing (replacing any clip in progress). False if there is no clip or Sound is off.
bool app_speech_play(const char *drink_id);
// Never blocks unless wait: then up to 500 ms, until the audio output is free.
void app_speech_stop(bool wait);
bool app_speech_playing(void);
// Settings > Sound. Off stops playback and refuses new clips.
void app_speech_set_enabled(bool on);

/* ---- Serial debug console (app_console.c) ---- */

void app_console_start(adb_db_t *db, adb_state_t *st);
