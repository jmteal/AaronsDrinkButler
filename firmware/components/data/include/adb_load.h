/*
 * Loaders fill an adb_db_t through the builder API in adb_data.h
 * (adb_db_ingredient / adb_db_add_recipe / adb_db_finalize), so the rest of
 * the app never knows where the data came from.
 *
 *  - adb_load_json_*: desktop simulator and tests (needs cJSON). Reads the
 *    readable dataset: recipes.json and, if present, ingredients.json.
 *  - Device (phase 2): the firmware maps the "drinks" flash partition and
 *    calls a packed-binary loader with the same builder API (planned:
 *    adb_load_packed(db, blob, len), format written by tools/data). Nothing
 *    above this layer changes.
 */
#ifndef ADB_LOAD_H
#define ADB_LOAD_H

#include "adb_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parses JSON text. ingredients_json may be NULL. Returns 0 on success,
 * negative on a fatal parse error. Non-fatal issues go to db->warnings. */
int adb_load_json_buffers(adb_db_t *db, const char *recipes_json, const char *ingredients_json);

/* Reads recipes.json from a path; looks for ingredients.json next to it
 * unless ingredients_path is given. Sets db->data_dir to the file's folder. */
int adb_load_json_file(adb_db_t *db, const char *recipes_path, const char *ingredients_path);

#ifdef __cplusplus
}
#endif
#endif
