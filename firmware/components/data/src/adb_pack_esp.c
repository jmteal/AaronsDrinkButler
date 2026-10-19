/* Device: map the read-only "drinks" partition and open the recipe pack in it. */
#include <stdio.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "adb_pack.h"

#define DRINKS_SUBTYPE 0x40
#define HEADER_MAP     (64 * 1024)   /* room for ~1000 entries; mmap rounds to 64 KB pages anyway */

static const char *TAG = "adb_pack";

#define FAIL(...) do { if (err && err_len) snprintf(err, err_len, __VA_ARGS__); return -1; } while (0)

int adb_pack_map_partition(adb_pack_t *p, bool verify_crc, char *err, size_t err_len)
{
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, DRINKS_SUBTYPE, "drinks");
    if (!part) FAIL("no \"drinks\" partition (flash the partition table)");

    /* Map the header first to learn the image size, then map only that much, so a
     * small pack doesn't take 13 MB of the cache's address space. */
    const void *ptr;
    esp_partition_mmap_handle_t h;
    size_t head = part->size < HEADER_MAP ? part->size : HEADER_MAP;
    esp_err_t e = esp_partition_mmap(part, 0, head, ESP_PARTITION_MMAP_DATA, &ptr, &h);
    if (e != ESP_OK) FAIL("mmap failed: %s", esp_err_to_name(e));
    size_t toc = adb_pack_toc_size(ptr, head);
    size_t size = toc && toc <= head ? adb_pack_image_size(ptr, head) : 0;
    esp_partition_munmap(h);
    if (!toc) FAIL("no recipe pack in the drinks partition (see firmware/README.md, \"Recipe pack\")");
    if (!size || size > part->size) FAIL("recipe pack header is corrupt");

    e = esp_partition_mmap(part, 0, size, ESP_PARTITION_MMAP_DATA, &ptr, &h);
    if (e != ESP_OK) FAIL("mmap of %u bytes failed: %s", (unsigned)size, esp_err_to_name(e));
    int rc = adb_pack_open(p, ptr, size, verify_crc, err, err_len);
    if (rc != 0) {
        esp_partition_munmap(h);
        return rc;
    }
    ESP_LOGI(TAG, "pack mapped: %u files, %u KB at %p", (unsigned)p->n, (unsigned)(size / 1024), ptr);
    return 0;
}
