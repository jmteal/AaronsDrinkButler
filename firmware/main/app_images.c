/*
 * Drink photos for the UI: JPEGs read in place from the mapped recipe pack, decoded
 * (esp_jpeg / TJpgDec) to RGB565 in PSRAM and handed to LVGL as lv_image_dsc_t.
 *
 *  - Thumbnails (96x96, 18 KB decoded): cached per recipe, LRU-evicted only past
 *    THUMB_CACHE_MAX (the 150 photos of today's dataset take ~2.7 MB and all stay).
 *    A low-priority task decodes them all after boot.
 *  - Detail photos (320x240, 150 KB): a small LRU of PHOTO_CACHE entries.
 *
 * The UI calls the provider on the LVGL task; the prewarm task only fills empty
 * thumbnail slots, under s_lock. Drinks without a photo return NULL and the UI
 * draws its generated glass icon.
 */
#include <string.h>
#include "app.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "jpeg_decoder.h"
#include "src/misc/cache/instance/lv_image_cache.h"

#define THUMB_CACHE_MAX 240
#define PHOTO_CACHE     6

static const char *TAG = "images";

typedef struct {
    lv_image_dsc_t dsc;
    uint32_t       stamp;    // LRU clock; 0 = empty
} slot_t;

static const adb_pack_t *s_pack;
static const adb_db_t *s_db;
static SemaphoreHandle_t s_lock;
static slot_t *s_thumbs;                  // one per recipe (PSRAM)
static struct { slot_t s; int32_t recipe; } s_photos[PHOTO_CACHE];
static uint32_t s_clock;
static app_images_stats_t s_stats;

static void slot_free(slot_t *s)
{
    if (!s->stamp) return;
    lv_image_cache_drop(&s->dsc);         // LVGL may cache by descriptor address
    s_stats.bytes -= s->dsc.data_size;
    heap_caps_free((void *)s->dsc.data);
    memset(s, 0, sizeof(*s));
}

// Decodes a pack JPEG into a new PSRAM buffer; fills dsc. Doesn't touch shared state.
static bool decode(const char *name, lv_image_dsc_t *dsc, uint32_t *us)
{
    size_t len = 0;
    const uint8_t *jpg = name ? adb_pack_find(s_pack, name, &len) : NULL;
    if (!jpg) return false;
    int64_t t0 = esp_timer_get_time();
    esp_jpeg_image_cfg_t cfg = {
        .indata = (uint8_t *)jpg,
        .indata_size = len,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,   // little-endian, as LVGL renders
        .out_scale = JPEG_IMAGE_SCALE_0,
    };
    esp_jpeg_image_output_t info;
    if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK || !info.width || !info.height) {
        ESP_LOGW(TAG, "%s: not a JPEG we can read", name);
        return false;
    }
    size_t size = (size_t)info.width * info.height * 2;
    uint8_t *px = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!px) {
        ESP_LOGW(TAG, "%s: no PSRAM for %u bytes", name, (unsigned)size);
        return false;
    }
    cfg.outbuf = px;
    cfg.outbuf_size = size;
    cfg.priv.read = 0;
    if (esp_jpeg_decode(&cfg, &info) != ESP_OK) {
        ESP_LOGW(TAG, "%s: decode failed", name);
        heap_caps_free(px);
        return false;
    }
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565;
    dsc->header.w = info.width;
    dsc->header.h = info.height;
    dsc->header.stride = info.width * 2;
    dsc->data_size = size;
    dsc->data = px;
    *us = (uint32_t)(esp_timer_get_time() - t0);
    return true;
}

static void note_failure(void) { s_stats.failures++; }

static void publish_thumb(uint16_t r, const lv_image_dsc_t *dsc, uint32_t us)
{
    if (s_stats.thumbs_cached >= THUMB_CACHE_MAX) {   // evict the least recently used
        slot_t *victim = NULL;
        for (int i = 0; i < s_db->n_recipes; i++)
            if (s_thumbs[i].stamp && (!victim || s_thumbs[i].stamp < victim->stamp)) victim = &s_thumbs[i];
        if (victim) {
            slot_free(victim);
            s_stats.thumbs_cached--;
        }
    }
    s_thumbs[r].dsc = *dsc;
    s_thumbs[r].stamp = ++s_clock;
    s_stats.thumbs_cached++;
    s_stats.thumb_decodes++;
    s_stats.bytes += dsc->data_size;
    if (us > s_stats.thumb_us_max) s_stats.thumb_us_max = us;
}

static const void *thumb(void *ctx, const adb_db_t *db, uint16_t r)
{
    const adb_recipe_t *rec = &db->recipes[r];
    if (!rec->image.is_photo || !rec->image.thumb || !s_thumbs) return NULL;
    const void *src = NULL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_thumbs[r].stamp) {
        s_thumbs[r].stamp = ++s_clock;
        s_stats.thumb_hits++;
        src = &s_thumbs[r].dsc;
    } else {
        lv_image_dsc_t dsc;
        uint32_t us;
        if (decode(rec->image.thumb, &dsc, &us)) {
            publish_thumb(r, &dsc, us);
            src = &s_thumbs[r].dsc;
        } else {
            note_failure();
        }
    }
    xSemaphoreGive(s_lock);
    return src;
}

static const void *photo_locked(const adb_recipe_t *rec, uint16_t r);

static const void *photo(void *ctx, const adb_db_t *db, uint16_t r)
{
    const adb_recipe_t *rec = &db->recipes[r];
    if (!rec->image.is_photo || !rec->image.file) return NULL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const void *src = photo_locked(rec, r);
    xSemaphoreGive(s_lock);
    return src;
}

static const void *photo_locked(const adb_recipe_t *rec, uint16_t r)
{
    int victim = 0;
    for (int i = 0; i < PHOTO_CACHE; i++) {
        if (s_photos[i].s.stamp && s_photos[i].recipe == r) {
            s_photos[i].s.stamp = ++s_clock;
            s_stats.photo_hits++;
            return &s_photos[i].s.dsc;
        }
        if (s_photos[i].s.stamp < s_photos[victim].s.stamp) victim = i;
    }
    lv_image_dsc_t dsc;
    uint32_t us;
    if (!decode(rec->image.file, &dsc, &us)) {
        note_failure();
        return NULL;
    }
    slot_free(&s_photos[victim].s);
    s_photos[victim].s.dsc = dsc;
    s_photos[victim].s.stamp = ++s_clock;
    s_photos[victim].recipe = r;
    s_stats.photo_decodes++;
    s_stats.bytes += dsc.data_size;
    if (us > s_stats.photo_us_max) s_stats.photo_us_max = us;
    ESP_LOGI(TAG, "photo %s decoded in %u ms", rec->id, (unsigned)(us / 1000));
    return &s_photos[victim].s.dsc;
}

static void prewarm_task(void *arg)
{
    int64_t t0 = esp_timer_get_time();
    int n = 0;
    for (uint16_t r = 0; r < s_db->n_recipes && s_stats.thumbs_cached < THUMB_CACHE_MAX; r++) {
        const adb_recipe_t *rec = &s_db->recipes[r];
        if (!rec->image.is_photo || !rec->image.thumb || s_thumbs[r].stamp) continue;
        lv_image_dsc_t dsc;
        uint32_t us;
        if (!decode(rec->image.thumb, &dsc, &us)) {   // outside the lock: the UI keeps going
            note_failure();
            continue;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_thumbs[r].stamp || s_stats.thumbs_cached >= THUMB_CACHE_MAX) {
            // the UI decoded it meanwhile, or the cache filled up (evicting calls into
            // LVGL, which only the LVGL task may do)
            heap_caps_free((void *)dsc.data);
        } else {
            publish_thumb(r, &dsc, us);
            n++;
        }
        xSemaphoreGive(s_lock);
        vTaskDelay(1);
    }
    ESP_LOGI(TAG, "prewarm: %d thumbnails in %u ms, %u KB PSRAM, slowest %u us",
             n, (unsigned)((esp_timer_get_time() - t0) / 1000), (unsigned)(s_stats.bytes / 1024),
             (unsigned)s_stats.thumb_us_max);
    vTaskDelete(NULL);
}

void app_images_init(const adb_pack_t *pack, const adb_db_t *db, ui_image_provider_t *out)
{
    s_pack = pack;
    s_db = db;
    s_lock = xSemaphoreCreateMutex();
    s_thumbs = heap_caps_calloc(db->n_recipes ? db->n_recipes : 1, sizeof(slot_t), MALLOC_CAP_SPIRAM);
    memset(out, 0, sizeof(*out));
    out->thumb = thumb;
    out->photo = photo;
}

void app_images_prewarm_start(void)
{
    if (s_thumbs) xTaskCreatePinnedToCore(prewarm_task, "img_prewarm", 6144, NULL, 1, NULL, 1);
}

void app_images_get_stats(app_images_stats_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_stats;
    xSemaphoreGive(s_lock);
}
