/* JSON loader (desktop / tests). Reads the readable dataset format described
 * in docs/research/drinks.md section 5. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "adb_load.h"

static const char *jstr(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static const char *jdup(adb_db_t *db, const cJSON *o, const char *key)
{
    return adb_db_strdup(db, jstr(o, key));
}

static uint8_t single_tag(adb_db_t *db, const char *rid, adb_tag_kind_t kind, const char *key, const char *field)
{
    if (!key) return ADB_NONE;
    int i = adb_tag_lookup(kind, key);
    if (i < 0) {
        adb_db_warn(db, "%s: unknown %s '%s'", rid, field, key);
        return ADB_NONE;
    }
    return (uint8_t)i;
}

static uint32_t multi_tag(adb_db_t *db, const char *rid, adb_tag_kind_t kind, const cJSON *arr, const char *field)
{
    uint32_t m = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (!cJSON_IsString(it)) continue;
        int i = adb_tag_lookup(kind, it->valuestring);
        if (i < 0) adb_db_warn(db, "%s: unknown %s '%s'", rid, field, it->valuestring);
        else m |= 1u << i;
    }
    return m;
}

static uint32_t parse_hex(const char *s)
{
    if (!s) return 0x806040;
    if (*s == '#') s++;
    return (uint32_t)strtoul(s, NULL, 16) & 0xFFFFFF;
}

static void load_ingredients(adb_db_t *db, const cJSON *root)
{
    const cJSON *arr = cJSON_IsArray(root) ? root : cJSON_GetObjectItemCaseSensitive(root, "ingredients");
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        const char *id = jstr(it, "id");
        if (!id) id = jstr(it, "ingredient_id");
        if (!id) continue;
        uint16_t ix = adb_db_ingredient(db, id, jstr(it, "name"));
        if (ix == ADB_ING_NONE) continue;
        const char *cat = jstr(it, "category");
        if (cat) {
            int c = adb_tag_lookup(ADB_TAG_ING_CATEGORY, cat);
            if (c < 0) adb_db_warn(db, "ingredient %s: unknown category '%s'", id, cat);
            db->ingredients[ix].category = c < 0 ? ADB_CAT_OTHER : (uint8_t)c;
        }
        db->ingredients[ix].pantry_essential = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(it, "pantry_essential"));
        const cJSON *s;
        cJSON_ArrayForEach(s, cJSON_GetObjectItemCaseSensitive(it, "substitutes")) {
            if (!cJSON_IsString(s)) continue;
            uint16_t sub = adb_db_ingredient(db, s->valuestring, NULL);
            /* re-fetch: adb_db_ingredient may realloc the table */
            adb_ingredient_t *ing = &db->ingredients[ix];
            if (sub != ADB_ING_NONE && sub != ix && ing->n_subs < ADB_MAX_SUBSTITUTES) ing->subs[ing->n_subs++] = sub;
        }
    }
}

static void load_recipe(adb_db_t *db, const cJSON *o)
{
    adb_recipe_t r;
    memset(&r, 0, sizeof r);
    const char *id = jstr(o, "id"), *name = jstr(o, "name");
    if (!id || !name) {
        adb_db_warn(db, "recipe without id/name skipped");
        return;
    }
    r.id = adb_db_strdup(db, id);
    r.name = adb_db_strdup(db, name);

    /* aka: join with "; " */
    const cJSON *aka = cJSON_GetObjectItemCaseSensitive(o, "aka"), *it;
    if (cJSON_IsArray(aka) && cJSON_GetArraySize(aka) > 0) {
        char buf[256] = "";
        cJSON_ArrayForEach(it, aka) {
            if (!cJSON_IsString(it)) continue;
            if (buf[0]) strncat(buf, "; ", sizeof(buf) - strlen(buf) - 1);
            strncat(buf, it->valuestring, sizeof(buf) - strlen(buf) - 1);
        }
        r.aka = adb_db_strdup(db, buf);
    }

    const cJSON *ings = cJSON_GetObjectItemCaseSensitive(o, "ingredients");
    int n = cJSON_GetArraySize(ings);
    if (n > 255) n = 255;
    r.ings = adb_db_alloc(db, (size_t)(n ? n : 1) * sizeof(adb_recipe_ing_t));
    cJSON_ArrayForEach(it, ings) {
        if (r.n_ings >= n) break;
        const char *iid = jstr(it, "ingredient_id");
        if (!iid) { adb_db_warn(db, "%s: ingredient without id", id); continue; }
        adb_recipe_ing_t *ri = &r.ings[r.n_ings];
        ri->ing = adb_db_ingredient(db, iid, NULL);
        if (ri->ing == ADB_ING_NONE) continue;
        ri->display = jdup(db, it, "display");
        if (!ri->display) ri->display = db->ingredients[ri->ing].id;
        /* first display text seen is a decent fallback name */
        if (!db->ingredients[ri->ing].name) db->ingredients[ri->ing].name = ri->display;
        const cJSON *amt = cJSON_GetObjectItemCaseSensitive(it, "amount");
        ri->amount = cJSON_IsNumber(amt) ? (float)amt->valuedouble : -1.0f;
        ri->unit = single_tag(db, id, ADB_TAG_UNIT, jstr(it, "unit"), "unit");
        ri->optional = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(it, "optional"));
        r.n_ings++;
    }

    const cJSON *steps = cJSON_GetObjectItemCaseSensitive(o, "steps");
    n = cJSON_GetArraySize(steps);
    if (n > 255) n = 255;
    r.steps = adb_db_alloc(db, (size_t)(n ? n : 1) * sizeof(char *));
    cJSON_ArrayForEach(it, steps) {
        if (r.n_steps < n && cJSON_IsString(it)) r.steps[r.n_steps++] = adb_db_strdup(db, it->valuestring);
    }

    r.glass_key = jdup(db, o, "glass");
    r.glass = single_tag(db, id, ADB_TAG_GLASS, r.glass_key, "glass");
    r.method = single_tag(db, id, ADB_TAG_METHOD, jstr(o, "method"), "method");
    r.ice = single_tag(db, id, ADB_TAG_ICE, jstr(o, "ice"), "ice");
    r.garnish = jdup(db, o, "garnish");
    r.garnish_icon = jdup(db, o, "garnish_icon");
    const cJSON *color = cJSON_GetObjectItemCaseSensitive(o, "color");
    r.color_name = jdup(db, color, "name");
    r.color_rgb = parse_hex(jstr(color, "hex"));
    const cJSON *abv = cJSON_GetObjectItemCaseSensitive(o, "abv_est");
    r.abv = cJSON_IsNumber(abv) ? (float)abv->valuedouble : -1.0f;

    const cJSON *tags = cJSON_GetObjectItemCaseSensitive(o, "tags");
    r.spirit_mask = multi_tag(db, id, ADB_TAG_SPIRIT, cJSON_GetObjectItemCaseSensitive(tags, "base_spirit"), "base_spirit");
    r.flavor_mask = multi_tag(db, id, ADB_TAG_FLAVOR, cJSON_GetObjectItemCaseSensitive(tags, "flavor"), "flavor");
    r.season_mask = multi_tag(db, id, ADB_TAG_SEASON, cJSON_GetObjectItemCaseSensitive(tags, "season"), "season");
    r.occasion_mask = multi_tag(db, id, ADB_TAG_OCCASION, cJSON_GetObjectItemCaseSensitive(tags, "occasion"), "occasion");
    r.strength = single_tag(db, id, ADB_TAG_STRENGTH, jstr(tags, "strength"), "strength");
    r.alcoholic = single_tag(db, id, ADB_TAG_ALCOHOLIC, jstr(tags, "alcoholic"), "alcoholic");
    const cJSON *diff = cJSON_GetObjectItemCaseSensitive(tags, "difficulty");
    r.difficulty = (cJSON_IsNumber(diff) && diff->valueint >= 1 && diff->valueint <= 3) ? (uint8_t)(diff->valueint - 1) : ADB_NONE;

    const cJSON *img = cJSON_GetObjectItemCaseSensitive(o, "image");
    r.image.file = jdup(db, img, "file");
    r.image.thumb = jdup(db, img, "thumb");
    const char *kind = jstr(img, "kind");
    r.image.is_photo = kind && strcmp(kind, "photo") == 0;
    r.image.author = jdup(db, img, "author");
    r.image.source_url = jdup(db, img, "source_url");
    r.image.license = jdup(db, img, "license");
    r.image.license_url = jdup(db, img, "license_url");
    r.image.modified = jdup(db, img, "modified");

    adb_db_add_recipe(db, &r);
}

int adb_load_json_buffers(adb_db_t *db, const char *recipes_json, const char *ingredients_json)
{
    if (ingredients_json) {
        cJSON *ing = cJSON_Parse(ingredients_json);
        if (!ing) adb_db_warn(db, "ingredients.json: parse error");
        else load_ingredients(db, ing);
        cJSON_Delete(ing);
    }
    cJSON *root = cJSON_Parse(recipes_json);
    if (!root) {
        adb_db_warn(db, "recipes.json: parse error near: %.40s", cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "?");
        return -1;
    }
    const cJSON *arr = cJSON_IsArray(root) ? root : cJSON_GetObjectItemCaseSensitive(root, "recipes");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        adb_db_warn(db, "recipes.json: no recipes array");
        return -2;
    }
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) load_recipe(db, it);
    cJSON_Delete(root);
    adb_db_finalize(db);
    return 0;
}

static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (buf) {
        size_t got = fread(buf, 1, (size_t)n, fp);
        buf[got] = 0;
    }
    fclose(fp);
    return buf;
}

int adb_load_json_file(adb_db_t *db, const char *recipes_path, const char *ingredients_path)
{
    char *rj = read_file(recipes_path);
    if (!rj) {
        adb_db_warn(db, "cannot read %s", recipes_path);
        return -1;
    }
    snprintf(db->data_dir, sizeof(db->data_dir), "%s", recipes_path);
    char *slash = strrchr(db->data_dir, '/');
    if (slash) *slash = 0;
    else snprintf(db->data_dir, sizeof(db->data_dir), ".");

    char ipath[256];
    if (!ingredients_path) {
        snprintf(ipath, sizeof ipath, "%s/ingredients.json", db->data_dir);
        ingredients_path = ipath;
    }
    char *ij = read_file(ingredients_path);
    int rc = adb_load_json_buffers(db, rj, ij);
    free(rj);
    free(ij);
    return rc;
}
