/* adb_storage_t over NVS: one blob per key in the "adb" namespace. */
#include "app.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "storage";
static nvs_handle_t s_nvs;

static bool nvs_read(void *ctx, const char *key, void *buf, size_t *len)
{
    size_t size = 0;
    esp_err_t e = nvs_get_blob(s_nvs, key, NULL, &size);
    if (e != ESP_OK) {
        if (e != ESP_ERR_NVS_NOT_FOUND) ESP_LOGW(TAG, "read %s: %s", key, esp_err_to_name(e));
        return false;
    }
    if (size > *len) {
        ESP_LOGW(TAG, "read %s: %u bytes stored, room for %u", key, (unsigned)size, (unsigned)*len);
        return false;
    }
    e = nvs_get_blob(s_nvs, key, buf, &size);
    if (e != ESP_OK) return false;
    *len = size;
    return true;
}

static bool nvs_write(void *ctx, const char *key, const void *buf, size_t len)
{
    esp_err_t e = nvs_set_blob(s_nvs, key, buf, len);
    if (e == ESP_OK) e = nvs_commit(s_nvs);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "write %s (%u bytes): %s", key, (unsigned)len, esp_err_to_name(e));
        return false;
    }
    ESP_LOGI(TAG, "saved %s (%u bytes)", key, (unsigned)len);
    return true;
}

bool app_storage_nvs_init(adb_storage_t *io)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erasing (%s); favorites and settings are reset", esp_err_to_name(e));
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    if (e == ESP_OK) e = nvs_open("adb", NVS_READWRITE, &s_nvs);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "NVS unavailable: %s; nothing will be saved", esp_err_to_name(e));
        return false;
    }
    io->read = nvs_read;
    io->write = nvs_write;
    io->ctx = NULL;
    return true;
}
