#include <stdlib.h>
#include <string.h>
#include "adb_filter.h"
#include "adb_pantry.h"

bool adb_pantry_has(const adb_db_t *db, const uint8_t *pantry, uint16_t ing)
{
    if (ing >= db->n_ingredients) return false;
    if (adb_bit_get(pantry, ing)) return true;
    const adb_ingredient_t *in = &db->ingredients[ing];
    for (int i = 0; i < in->n_subs; i++)
        if (in->subs[i] < db->n_ingredients && adb_bit_get(pantry, in->subs[i])) return true;
    return false;
}

bool adb_pantry_ignored(const adb_db_t *db, const adb_recipe_ing_t *ri)
{
    if (ri->optional) return true;
    if (ri->ing >= db->n_ingredients) return true;
    const adb_ingredient_t *in = &db->ingredients[ri->ing];
    if (in->category == ADB_CAT_GARNISH) return true;
    return strcmp(in->id, "water") == 0 || strcmp(in->id, "ice") == 0;
}

int adb_pantry_missing(const adb_db_t *db, const uint8_t *pantry, uint16_t recipe,
                       uint16_t *missing, int max)
{
    if (recipe >= db->n_recipes) return 0;
    const adb_recipe_t *r = &db->recipes[recipe];
    int n = 0;
    for (int i = 0; i < r->n_ings; i++) {
        const adb_recipe_ing_t *ri = &r->ings[i];
        if (adb_pantry_ignored(db, ri) || adb_pantry_has(db, pantry, ri->ing)) continue;
        bool dup = false; /* same ingredient listed twice counts once */
        for (int k = 0; k < i; k++) dup |= r->ings[k].ing == ri->ing && !adb_pantry_ignored(db, &r->ings[k]);
        if (dup) continue;
        if (missing && n < max) missing[n] = ri->ing;
        n++;
    }
    return n;
}

void adb_pantry_compute(const adb_db_t *db, const uint8_t *pantry, adb_pantry_result_t *res)
{
    memset(res, 0, sizeof(*res));
    for (int b = 0; b < 3; b++) res->idx[b] = malloc((db->n_recipes ? db->n_recipes : 1) * sizeof(uint16_t));
    for (uint16_t i = 0; i < db->n_recipes; i++) {
        int m = adb_pantry_missing(db, pantry, i, NULL, 0);
        if (m <= 2 && res->idx[m]) res->idx[m][res->n[m]++] = i;
    }
    for (int b = 0; b < 3; b++)
        if (res->idx[b]) adb_sort_by_name(db, res->idx[b], res->n[b]);
}

void adb_pantry_result_free(adb_pantry_result_t *res)
{
    for (int b = 0; b < 3; b++) free(res->idx[b]);
    memset(res, 0, sizeof(*res));
}
