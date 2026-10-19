/*
 * Combinable recipe filters.
 *
 * Semantics:
 *  - Within one tag kind, selected values are OR-ed ("gin or vodka").
 *    Flavor is the exception: every selected flavor must be present
 *    ("sour and fruity"), which is what people mean when they stack flavors.
 *  - Different kinds, ingredient include/exclude, text and the restrict set
 *    are AND-ed.
 *  - Include: the recipe must use every included ingredient (optional ones count).
 *  - Exclude: the recipe must not use any excluded ingredient, even optionally.
 *  - Text: case/accent-insensitive substring of name, aka, or any ingredient
 *    name/display text.
 */
#ifndef ADB_FILTER_H
#define ADB_FILTER_H

#include "adb_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADB_FILTER_MAX_INGS 16
#define ADB_FILTER_TEXT_MAX 48

typedef struct {
    uint32_t mask[ADB_TAG_COUNT];               /* bit i = value index i selected */
    uint16_t include[ADB_FILTER_MAX_INGS];
    uint8_t  n_include;
    uint16_t exclude[ADB_FILTER_MAX_INGS];
    uint8_t  n_exclude;
    char     text[ADB_FILTER_TEXT_MAX];
    const uint8_t *restrict_set;  /* optional recipe bitset (e.g. favorites); not owned */
} adb_filter_t;

void adb_filter_clear(adb_filter_t *f);
void adb_filter_toggle_tag(adb_filter_t *f, adb_tag_kind_t kind, int idx);
void adb_filter_set_tag(adb_filter_t *f, adb_tag_kind_t kind, int idx, bool on);
bool adb_filter_has_tag(const adb_filter_t *f, adb_tag_kind_t kind, int idx);

/* Ingredient state: 0 = none, 1 = include, -1 = exclude. */
int  adb_filter_ing_state(const adb_filter_t *f, uint16_t ing);
void adb_filter_set_ing(adb_filter_t *f, uint16_t ing, int state);
void adb_filter_set_text(adb_filter_t *f, const char *text);

/* Number of individual active conditions (each tag value, ingredient and
 * non-empty text counts as one). The restrict set is not counted. */
int  adb_filter_active_count(const adb_filter_t *f);

bool adb_filter_match(const adb_db_t *db, const adb_filter_t *f, uint16_t recipe);
/* Writes matching recipe indices (in db order) to out; returns the total
 * number of matches even if larger than max. out may be NULL to just count. */
size_t adb_filter_apply(const adb_db_t *db, const adb_filter_t *f, uint16_t *out, size_t max);

/* Sort recipe indices by name (case-insensitive). */
void adb_sort_by_name(const adb_db_t *db, uint16_t *idx, size_t n);

/* ---- Bitsets ---------------------------------------------------------- */

#define ADB_BITSET_BYTES(n) (((n) + 7) / 8)
static inline bool adb_bit_get(const uint8_t *s, unsigned i) { return (s[i >> 3] >> (i & 7)) & 1; }
static inline void adb_bit_set(uint8_t *s, unsigned i, bool v)
{
    if (v) s[i >> 3] |= (uint8_t)(1u << (i & 7));
    else   s[i >> 3] &= (uint8_t)~(1u << (i & 7));
}

#ifdef __cplusplus
}
#endif
#endif
