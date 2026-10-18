/*
 * microSD, 4-bit SDMMC. Optional: the app's data lives in flash. There is no
 * card-detect line, so a card is only found when bsp_sd_mount() runs.
 */
#include "bsp.h"

#include <string.h>

#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "bsp_sd";

static sdmmc_card_t *s_card;
static char s_mount_point[16];

esp_err_t bsp_sd_mount(const char *mount_point)
{
    ESP_RETURN_ON_FALSE(!s_card, ESP_ERR_INVALID_STATE, TAG, "already mounted");
    ESP_RETURN_ON_FALSE(mount_point && strlen(mount_point) < sizeof(s_mount_point),
                        ESP_ERR_INVALID_ARG, TAG, "bad mount point");

    const esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = BSP_SD_CLK;
    slot.cmd = BSP_SD_CMD;
    slot.d0 = BSP_SD_D0;
    slot.d1 = BSP_SD_D1;
    slot.d2 = BSP_SD_D2;
    slot.d3 = BSP_SD_D3;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        ESP_LOGW(TAG, "no card mounted: %s", esp_err_to_name(err));
        return err;
    }
    strcpy(s_mount_point, mount_point);
    ESP_LOGI(TAG, "mounted \"%s\" at %s, %llu MB", s_card->cid.name, mount_point,
             (unsigned long long)s_card->csd.capacity * s_card->csd.sector_size / (1024 * 1024));
    return ESP_OK;
}

esp_err_t bsp_sd_unmount(void)
{
    ESP_RETURN_ON_FALSE(s_card, ESP_ERR_INVALID_STATE, TAG, "not mounted");
    esp_err_t err = esp_vfs_fat_sdcard_unmount(s_mount_point, s_card);
    s_card = NULL;
    return err;
}
