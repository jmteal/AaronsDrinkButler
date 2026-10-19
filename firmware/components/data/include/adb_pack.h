/*
 * Recipe pack: the image `tools/data/build.py pack` writes for the read-only
 * "drinks" flash partition. The device maps the partition and reads files in
 * place (no copies); the host can read the same image from a buffer.
 *
 * Format (little-endian; tools/data/adb/pack.py is the writer):
 *   header   "ADBP", u32 version, u32 entry count, u32 crc32 (zlib) of the rest
 *   entries  count x { u32 offset, u32 size, char name[56] }, sorted by name
 *   data     each file 4-byte aligned and followed by at least one NUL byte
 */
#ifndef ADB_PACK_H
#define ADB_PACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "adb_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADB_PACK_VERSION   1
#define ADB_PACK_NAME_MAX  56

typedef struct {
    uint32_t offset;
    uint32_t size;
    char     name[ADB_PACK_NAME_MAX];
} adb_pack_entry_t;

typedef struct {
    const uint8_t          *base;
    size_t                  len;     /* bytes of the image (header + entries + data) */
    uint32_t                n;
    const adb_pack_entry_t *toc;
} adb_pack_t;

/* Size of the header plus table of contents, or 0 if blob doesn't start with a
 * pack header. Lets a caller map just the header first and learn how much to map. */
size_t adb_pack_toc_size(const void *blob, size_t len);
/* Total image size from the header and table of contents (needs toc_size bytes). */
size_t adb_pack_image_size(const void *blob, size_t len);

/* Checks the header, the table and (if verify_crc) the CRC. 0 on success,
 * negative otherwise; err (may be NULL) gets a one-line reason. */
int adb_pack_open(adb_pack_t *p, const void *blob, size_t len, bool verify_crc,
                  char *err, size_t err_len);

/* File contents by name ("recipes.json", "images/thumb/negroni.jpg"), or NULL.
 * The data is followed by a NUL byte. */
const void *adb_pack_find(const adb_pack_t *p, const char *name, size_t *size);

/* Fills db from the pack's recipes.json and ingredients.json (JSON loader).
 * db->data_dir is set to "pack:". Returns 0 on success. */
int adb_load_pack(adb_db_t *db, const adb_pack_t *p);

#ifdef ESP_PLATFORM
/* Maps the "drinks" partition (data, subtype 0x40) and opens the pack in it.
 * The mapping stays for the life of the app. */
int adb_pack_map_partition(adb_pack_t *p, bool verify_crc, char *err, size_t err_len);
#endif

#ifdef __cplusplus
}
#endif
#endif
