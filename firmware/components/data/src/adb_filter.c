#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "adb_filter.h"

void adb_filter_clear(adb_filter_t *f)
{
    memset(f, 0, sizeof(*f));
}

void adb_filter_set_tag(adb_filter_t *f, adb_tag_kind_t kind, int idx, bool on)
{
    if (kind >= ADB_TAG_COUNT || idx < 0 || idx >= 32) return;
    if (on) f->mask[kind] |= 1u << idx;
    else    f->mask[kind] &= ~(1u << idx);
}

void adb_filter_toggle_tag(adb_filter_t *f, adb_tag_kind_t kind, int idx)
{
    adb_filter_set_tag(f, kind, idx, !adb_filter_has_tag(f, kind, idx));
}

bool adb_filter_has_tag(const adb_filter_t *f, adb_tag_kind_t kind, int idx)
{
    if (kind >= ADB_TAG_COUNT || idx < 0 || idx >= 32) return false;
    return (f->mask[kind] >> idx) & 1;
}

static int find_ing(const uint16_t *list, int n, uint16_t ing)
{
    for (int i = 0; i < n; i++)
        if (list[i] == ing) return i;
    return -1;
}

static void remove_at(uint16_t *list, uint8_t *n, int pos)
{
    memmove(&list[pos], &list[pos + 1], (size_t)(*n - pos - 1) * sizeof(uint16_t));
    (*n)--;
}

int adb_filter_ing_state(const adb_filter_t *f, uint16_t ing)
{
    if (find_ing(f->include, f->n_include, ing) >= 0) return 1;
    if (find_ing(f->exclude, f->n_exclude, ing) >= 0) return -1;
    return 0;
}

void adb_filter_set_ing(adb_filter_t *f, uint16_t ing, int state)
{
    int p;
    if ((p = find_ing(f->include, f->n_include, ing)) >= 0) remove_at(f->include, &f->n_include, p);
    if ((p = find_ing(f->exclude, f->n_exclude, ing)) >= 0) remove_at(f->exclude, &f->n_exclude, p);
    if (state > 0 && f->n_include < ADB_FILTER_MAX_INGS) f->include[f->n_include++] = ing;
    if (state < 0 && f->n_exclude < ADB_FILTER_MAX_INGS) f->exclude[f->n_exclude++] = ing;
}

void adb_filter_set_text(adb_filter_t *f, const char *text)
{
    strncpy(f->text, text ? text : "", sizeof(f->text) - 1);
    f->text[sizeof(f->text) - 1] = 0;
}

int adb_filter_active_count(const adb_filter_t *f)
{
    int n = f->n_include + f->n_exclude + (f->text[0] ? 1 : 0);
    for (int k = 0; k < ADB_TAG_COUNT; k++) n += __builtin_popcount(f->mask[k]);
    return n;
}

static bool single_ok(uint32_t mask, uint8_t value)
{
    if (!mask) return true;
    return value != ADB_NONE && value < 32 && ((mask >> value) & 1);
}

static bool uses(const adb_recipe_t *r, uint16_t ing)
{
    for (int i = 0; i < r->n_ings; i++)
        if (r->ings[i].ing == ing) return true;
    return false;
}

static bool text_ok(const adb_db_t *db, const adb_recipe_t *r, const char *t)
{
    if (adb_text_contains(r->name, t) || adb_text_contains(r->aka, t)) return true;
    for (int i = 0; i < r->n_ings; i++) {
        if (adb_text_contains(r->ings[i].display, t)) return true;
        if (r->ings[i].ing < db->n_ingredients && adb_text_contains(db->ingredients[r->ings[i].ing].name, t))
            return true;
    }
    return false;
}

bool adb_filter_match(const adb_db_t *db, const adb_filter_t *f, uint16_t idx)
{
    if (idx >= db->n_recipes) return false;
    const adb_recipe_t *r = &db->recipes[idx];
    if (f->restrict_set && !adb_bit_get(f->restrict_set, idx)) return false;

    if (f->mask[ADB_TAG_SPIRIT] && !(f->mask[ADB_TAG_SPIRIT] & r->spirit_mask)) return false;
    if (f->mask[ADB_TAG_FLAVOR] && (f->mask[ADB_TAG_FLAVOR] & r->flavor_mask) != f->mask[ADB_TAG_FLAVOR]) return false;
    if (f->mask[ADB_TAG_SEASON] && !(f->mask[ADB_TAG_SEASON] & r->season_mask)) return false;
    if (f->mask[ADB_TAG_OCCASION] && !(f->mask[ADB_TAG_OCCASION] & r->occasion_mask)) return false;
    if (!single_ok(f->mask[ADB_TAG_STRENGTH], r->strength)) return false;
    if (!single_ok(f->mask[ADB_TAG_GLASS], r->glass)) return false;
    if (!single_ok(f->mask[ADB_TAG_METHOD], r->method)) return false;
    if (!single_ok(f->mask[ADB_TAG_DIFFICULTY], r->difficulty)) return false;
    if (!single_ok(f->mask[ADB_TAG_ALCOHOLIC], r->alcoholic)) return false;

    for (int i = 0; i < f->n_include; i++)
        if (!uses(r, f->include[i])) return false;
    for (int i = 0; i < f->n_exclude; i++)
        if (uses(r, f->exclude[i])) return false;

    if (f->text[0] && !text_ok(db, r, f->text)) return false;
    return true;
}

size_t adb_filter_apply(const adb_db_t *db, const adb_filter_t *f, uint16_t *out, size_t max)
{
    size_t n = 0;
    for (uint16_t i = 0; i < db->n_recipes; i++) {
        if (!adb_filter_match(db, f, i)) continue;
        if (out && n < max) out[n] = i;
        n++;
    }
    return n;
}

static const adb_db_t *s_db;

static int cmp_name(const void *a, const void *b)
{
    return strcasecmp(s_db->recipes[*(const uint16_t *)a].name, s_db->recipes[*(const uint16_t *)b].name);
}

void adb_sort_by_name(const adb_db_t *db, uint16_t *idx, size_t n)
{
    s_db = db;
    qsort(idx, n, sizeof(uint16_t), cmp_name);
    s_db = NULL;
}
