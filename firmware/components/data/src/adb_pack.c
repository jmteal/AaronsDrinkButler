/* Recipe pack reader (platform-neutral). See adb_pack.h for the format. */
#include <stdio.h>
#include <string.h>
#include "adb_load.h"
#include "adb_pack.h"

#define HEADER_SIZE 16
#define ENTRY_SIZE  64

_Static_assert(sizeof(adb_pack_entry_t) == ENTRY_SIZE, "pack entry layout");

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t crc32(const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

size_t adb_pack_toc_size(const void *blob, size_t len)
{
    const uint8_t *b = blob;
    if (len < HEADER_SIZE || memcmp(b, "ADBP", 4) != 0) return 0;
    uint32_t n = rd32(b + 8);
    if (n > 65536) return 0;
    return HEADER_SIZE + (size_t)n * ENTRY_SIZE;
}

size_t adb_pack_image_size(const void *blob, size_t len)
{
    size_t toc = adb_pack_toc_size(blob, len);
    if (!toc || len < toc) return 0;
    const uint8_t *b = blob;
    size_t end = toc;
    for (size_t e = HEADER_SIZE; e < toc; e += ENTRY_SIZE) {
        size_t file_end = (size_t)rd32(b + e) + rd32(b + e + 4) + 1;
        if (file_end > end) end = file_end;
    }
    return (end + 3) & ~(size_t)3;
}

#define FAIL(...) do { if (err && err_len) snprintf(err, err_len, __VA_ARGS__); return -1; } while (0)

int adb_pack_open(adb_pack_t *p, const void *blob, size_t len, bool verify_crc, char *err, size_t err_len)
{
    memset(p, 0, sizeof(*p));
    const uint8_t *b = blob;
    size_t toc = adb_pack_toc_size(blob, len);
    if (!toc) FAIL("no pack header (flash the recipe pack)");
    if (rd32(b + 4) != ADB_PACK_VERSION) FAIL("pack version %u, firmware reads %u", (unsigned)rd32(b + 4), ADB_PACK_VERSION);
    size_t size = adb_pack_image_size(blob, len);
    if (!size || size > len) FAIL("pack is truncated (%u of %u bytes)", (unsigned)len, (unsigned)size);
    const adb_pack_entry_t *t = (const adb_pack_entry_t *)(b + HEADER_SIZE);
    uint32_t n = rd32(b + 8);
    for (uint32_t i = 0; i < n; i++) {
        if (t[i].offset < toc || memchr(t[i].name, 0, ADB_PACK_NAME_MAX) == NULL) FAIL("pack entry %u is corrupt", (unsigned)i);
        if (i && strcmp(t[i - 1].name, t[i].name) >= 0) FAIL("pack entries are not sorted");
    }
    if (verify_crc) {
        uint32_t want = rd32(b + 12), got = crc32(b + HEADER_SIZE, size - HEADER_SIZE);
        if (got != want) FAIL("pack CRC mismatch (%08x, expected %08x)", (unsigned)got, (unsigned)want);
    }
    p->base = b;
    p->len = size;
    p->n = n;
    p->toc = t;
    return 0;
}

const void *adb_pack_find(const adb_pack_t *p, const char *name, size_t *size)
{
    if (!p->base || !name) return NULL;
    uint32_t lo = 0, hi = p->n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        int c = strcmp(p->toc[mid].name, name);
        if (c == 0) {
            if (size) *size = p->toc[mid].size;
            return p->base + p->toc[mid].offset;
        }
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

int adb_load_pack(adb_db_t *db, const adb_pack_t *p)
{
    const char *rj = adb_pack_find(p, "recipes.json", NULL);
    const char *ij = adb_pack_find(p, "ingredients.json", NULL);
    if (!rj) {
        adb_db_warn(db, "pack has no recipes.json");
        return -1;
    }
    snprintf(db->data_dir, sizeof(db->data_dir), "pack:");
    return adb_load_json_buffers(db, rj, ij);
}
