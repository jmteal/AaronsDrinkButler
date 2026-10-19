/*
 * "What can I make?" — pantry matching.
 *
 * An ingredient counts as present when it, or one of its listed substitutes,
 * is in the pantry. Optional ingredients, water, ice and anything in the
 * garnish category are ignored. A recipe is "can make" with 0 missing,
 * "missing 1" / "missing 2" otherwise.
 */
#ifndef ADB_PANTRY_H
#define ADB_PANTRY_H

#include "adb_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pantry = ingredient bitset of ADB_BITSET_BYTES(db->n_ingredients). */

bool adb_pantry_has(const adb_db_t *db, const uint8_t *pantry, uint16_t ing);
/* Whether the ingredient is ignored by pantry matching. */
bool adb_pantry_ignored(const adb_db_t *db, const adb_recipe_ing_t *ri);

/* Missing required ingredients of one recipe; writes up to max indices into
 * missing (may be NULL) and returns the total count. */
int  adb_pantry_missing(const adb_db_t *db, const uint8_t *pantry, uint16_t recipe,
                        uint16_t *missing, int max);

typedef struct {
    uint16_t *idx[3];   /* [0]=can make, [1]=missing 1, [2]=missing 2; sorted by name */
    uint16_t  n[3];
} adb_pantry_result_t;

/* Buckets all recipes by missing count. Buffers are
 * malloc'd; free with adb_pantry_result_free. */
void adb_pantry_compute(const adb_db_t *db, const uint8_t *pantry, adb_pantry_result_t *res);
void adb_pantry_result_free(adb_pantry_result_t *res);

#ifdef __cplusplus
}
#endif
#endif
