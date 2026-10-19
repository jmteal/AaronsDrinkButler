#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "adb_data.h"

/* ---- Arena ------------------------------------------------------------ */

struct adb_arena_block {
    adb_arena_block_t *next;
    size_t used, cap;
    /* data follows, max-aligned */
    _Alignas(max_align_t) unsigned char data[];
};

#define ARENA_BLOCK 16384

void *adb_db_alloc(adb_db_t *db, size_t size)
{
    size = (size + 7) & ~(size_t)7;
    adb_arena_block_t *b = db->arena;
    if (!b || b->used + size > b->cap) {
        size_t cap = size > ARENA_BLOCK ? size : ARENA_BLOCK;
        b = malloc(sizeof(*b) + cap);
        if (!b) return NULL;
        b->next = db->arena;
        b->used = 0;
        b->cap = cap;
        db->arena = b;
    }
    void *p = b->data + b->used;
    b->used += size;
    memset(p, 0, size);
    return p;
}

const char *adb_db_strdup(adb_db_t *db, const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = adb_db_alloc(db, n);
    if (p) memcpy(p, s, n);
    return p;
}

void adb_db_init(adb_db_t *db)
{
    memset(db, 0, sizeof(*db));
}

void adb_db_free(adb_db_t *db)
{
    adb_arena_block_t *b = db->arena;
    while (b) {
        adb_arena_block_t *n = b->next;
        free(b);
        b = n;
    }
    free(db->recipes);
    free(db->ingredients);
    free(db->ing_by_category);
    memset(db, 0, sizeof(*db));
}

void adb_db_warn(adb_db_t *db, const char *fmt, ...)
{
    if (db->n_warnings >= sizeof(db->warnings) / sizeof(db->warnings[0])) {
        db->n_warnings++; /* still count them */
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(db->warnings[db->n_warnings], sizeof(db->warnings[0]), fmt, ap);
    va_end(ap);
    db->n_warnings++;
}

/* ---- Builder ---------------------------------------------------------- */

int adb_db_find_ingredient(const adb_db_t *db, const char *id)
{
    if (!id) return -1;
    for (int i = 0; i < db->n_ingredients; i++)
        if (strcmp(db->ingredients[i].id, id) == 0) return i;
    return -1;
}

int adb_db_find_recipe(const adb_db_t *db, const char *id)
{
    if (!id) return -1;
    for (int i = 0; i < db->n_recipes; i++)
        if (strcmp(db->recipes[i].id, id) == 0) return i;
    return -1;
}

static const char *name_from_slug(adb_db_t *db, const char *id)
{
    char buf[64];
    size_t i = 0;
    for (; id[i] && i < sizeof(buf) - 1; i++) buf[i] = id[i] == '_' ? ' ' : id[i];
    buf[i] = 0;
    if (buf[0]) buf[0] = (char)toupper((unsigned char)buf[0]);
    return adb_db_strdup(db, buf);
}

uint16_t adb_db_ingredient(adb_db_t *db, const char *id, const char *name)
{
    int found = adb_db_find_ingredient(db, id);
    if (found >= 0) {
        if (name && db->ingredients[found].name == NULL) db->ingredients[found].name = adb_db_strdup(db, name);
        return (uint16_t)found;
    }
    if (db->n_ingredients >= ADB_MAX_INGREDIENTS) return ADB_ING_NONE;
    if ((db->n_ingredients & 63) == 0) {
        void *p = realloc(db->ingredients, (db->n_ingredients + 64) * sizeof(adb_ingredient_t));
        if (!p) return ADB_ING_NONE;
        db->ingredients = p;
    }
    adb_ingredient_t *ing = &db->ingredients[db->n_ingredients];
    memset(ing, 0, sizeof(*ing));
    ing->id = adb_db_strdup(db, id);
    ing->name = name ? adb_db_strdup(db, name) : NULL;
    ing->category = ADB_NONE;
    return db->n_ingredients++;
}

int adb_db_add_recipe(adb_db_t *db, const adb_recipe_t *r)
{
    if (db->n_recipes >= ADB_MAX_RECIPES) return -1;
    if (adb_db_find_recipe(db, r->id) >= 0) {
        adb_db_warn(db, "duplicate recipe id %s", r->id);
        return -1;
    }
    if ((db->n_recipes & 63) == 0) {
        void *p = realloc(db->recipes, (db->n_recipes + 64) * sizeof(adb_recipe_t));
        if (!p) return -1;
        db->recipes = p;
    }
    db->recipes[db->n_recipes] = *r;
    return db->n_recipes++;
}

uint8_t adb_guess_category(const char *id)
{
    /* first match wins: specific words before ones they contain ("ginger" > "gin") */
    static const struct { const char *needle; uint8_t cat; } rules[] = {
        {"ginger", ADB_CAT_MIXER}, {"bitters", ADB_CAT_BITTERS}, {"juice", ADB_CAT_JUICE}, {"syrup", ADB_CAT_SYRUP},
        {"grenadine", ADB_CAT_SYRUP}, {"sugar", ADB_CAT_SYRUP}, {"honey", ADB_CAT_SYRUP},
        {"agave", ADB_CAT_SYRUP}, {"egg", ADB_CAT_EGG}, {"cream", ADB_CAT_DAIRY},
        {"milk", ADB_CAT_DAIRY}, {"butter", ADB_CAT_DAIRY}, {"liqueur", ADB_CAT_LIQUEUR},
        {"triple_sec", ADB_CAT_LIQUEUR}, {"cointreau", ADB_CAT_LIQUEUR},
        {"campari", ADB_CAT_LIQUEUR}, {"aperol", ADB_CAT_LIQUEUR}, {"amaretto", ADB_CAT_LIQUEUR},
        {"chartreuse", ADB_CAT_LIQUEUR}, {"maraschino", ADB_CAT_LIQUEUR},
        {"vermouth", ADB_CAT_WINE}, {"wine", ADB_CAT_WINE}, {"champagne", ADB_CAT_WINE},
        {"prosecco", ADB_CAT_WINE}, {"sherry", ADB_CAT_WINE}, {"port", ADB_CAT_WINE},
        {"whisk", ADB_CAT_SPIRIT}, {"bourbon", ADB_CAT_SPIRIT}, {"rye", ADB_CAT_SPIRIT},
        {"scotch", ADB_CAT_SPIRIT}, {"gin", ADB_CAT_SPIRIT}, {"vodka", ADB_CAT_SPIRIT},
        {"rum", ADB_CAT_SPIRIT}, {"tequila", ADB_CAT_SPIRIT}, {"mezcal", ADB_CAT_SPIRIT},
        {"brandy", ADB_CAT_SPIRIT}, {"cognac", ADB_CAT_SPIRIT}, {"pisco", ADB_CAT_SPIRIT},
        {"cachaca", ADB_CAT_SPIRIT}, {"absinthe", ADB_CAT_SPIRIT},
        {"soda", ADB_CAT_MIXER}, {"tonic", ADB_CAT_MIXER}, {"ginger_ale", ADB_CAT_MIXER},
        {"ginger_beer", ADB_CAT_MIXER}, {"cola", ADB_CAT_MIXER}, {"coffee", ADB_CAT_MIXER},
        {"espresso", ADB_CAT_MIXER}, {"water", ADB_CAT_MIXER}, {"coconut", ADB_CAT_MIXER},
        {"salt", ADB_CAT_GARNISH}, {"mint", ADB_CAT_GARNISH}, {"peel", ADB_CAT_GARNISH},
        {"cherry", ADB_CAT_GARNISH}, {"olive", ADB_CAT_GARNISH}, {"wedge", ADB_CAT_GARNISH},
        {"wheel", ADB_CAT_GARNISH}, {"twist", ADB_CAT_GARNISH}, {"nutmeg", ADB_CAT_GARNISH},
    };
    if (!id) return ADB_CAT_OTHER;
    for (size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); i++)
        if (strstr(id, rules[i].needle)) return rules[i].cat;
    return ADB_CAT_OTHER;
}

static const adb_db_t *s_sort_db;

static int cmp_ing_cat(const void *a, const void *b)
{
    const adb_ingredient_t *x = &s_sort_db->ingredients[*(const uint16_t *)a];
    const adb_ingredient_t *y = &s_sort_db->ingredients[*(const uint16_t *)b];
    if (x->category != y->category) return (int)x->category - (int)y->category;
    return strcasecmp(x->name, y->name);
}

void adb_db_finalize(adb_db_t *db)
{
    for (int i = 0; i < db->n_ingredients; i++) {
        adb_ingredient_t *ing = &db->ingredients[i];
        ing->n_recipes = 0;
        if (ing->category == ADB_NONE) ing->category = adb_guess_category(ing->id);
        if (!ing->name) ing->name = name_from_slug(db, ing->id);
    }
    for (int r = 0; r < db->n_recipes; r++) {
        const adb_recipe_t *rec = &db->recipes[r];
        for (int j = 0; j < rec->n_ings; j++) {
            /* count each ingredient once per recipe */
            bool dup = false;
            for (int k = 0; k < j; k++) dup |= rec->ings[k].ing == rec->ings[j].ing;
            if (!dup && rec->ings[j].ing < db->n_ingredients) db->ingredients[rec->ings[j].ing].n_recipes++;
        }
    }
    free(db->ing_by_category);
    db->ing_by_category = malloc((db->n_ingredients ? db->n_ingredients : 1) * sizeof(uint16_t));
    if (db->ing_by_category) {
        for (int i = 0; i < db->n_ingredients; i++) db->ing_by_category[i] = (uint16_t)i;
        s_sort_db = db;
        qsort(db->ing_by_category, db->n_ingredients, sizeof(uint16_t), cmp_ing_cat);
        s_sort_db = NULL;
    }
}

/* ---- Helpers ---------------------------------------------------------- */

const char *adb_recipe_spirit_label(const adb_recipe_t *r)
{
    if (r->alcoholic == ADB_ALC_MOCKTAIL) return "Mocktail";
    for (int i = 0; i < adb_tag_count(ADB_TAG_SPIRIT); i++)
        if (r->spirit_mask & (1u << i)) return adb_tag_label(ADB_TAG_SPIRIT, i);
    return "";
}

int adb_recipe_flavors(const adb_recipe_t *r, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < adb_tag_count(ADB_TAG_FLAVOR) && n < max; i++)
        if (r->flavor_mask & (1u << i)) out[n++] = i;
    return n;
}

/* Number with ¼ ½ ¾ fractions, value already rounded to quarters. */
static void quarters(float v, char *buf, size_t len)
{
    int q = (int)lroundf(v * 4.0f);
    int whole = q / 4, frac = q % 4;
    static const char *fr[] = {"", "\xC2\xBC", "\xC2\xBD", "\xC2\xBE"}; /* ¼ ½ ¾ */
    if (whole && frac) snprintf(buf, len, "%d%s", whole, fr[frac]);
    else if (whole) snprintf(buf, len, "%d", whole);
    else snprintf(buf, len, "%s", frac ? fr[frac] : "0");
}

static void num(float v, char *buf, size_t len)
{
    if (fabsf(v - lroundf(v)) < 0.01f) snprintf(buf, len, "%ld", lroundf(v));
    else quarters(v, buf, len);
}

void adb_format_amount(const adb_recipe_ing_t *ri, bool use_oz, char *buf, size_t len)
{
    char n[16];
    buf[0] = 0;
    if (ri->unit == ADB_UNIT_TOP) { snprintf(buf, len, "Top up"); return; }
    if (ri->amount < 0) {
        if (ri->unit == ADB_UNIT_PINCH) snprintf(buf, len, "Pinch");
        else if (ri->unit != ADB_NONE && ri->unit != ADB_UNIT_ML) snprintf(buf, len, "%s", adb_tag_label(ADB_TAG_UNIT, ri->unit));
        return;
    }
    if (ri->unit == ADB_UNIT_ML || ri->unit == ADB_NONE) {
        if (ri->unit == ADB_NONE) { num(ri->amount, n, sizeof n); snprintf(buf, len, "%s", n); return; }
        if (!use_oz) { snprintf(buf, len, "%ld ml", lroundf(ri->amount)); return; }
        if (ri->amount < 7.5f) { /* small amounts read better as teaspoons */
            float tsp = roundf(ri->amount / 4.93f * 2.0f) / 2.0f;
            if (tsp < 0.5f) tsp = 0.5f;
            num(tsp, n, sizeof n);
            snprintf(buf, len, "%s tsp", n);
            return;
        }
        float oz = ri->amount / 29.5735f;
        if (oz < 0.25f) oz = 0.25f;
        quarters(oz, n, sizeof n);
        snprintf(buf, len, "%s oz", n);
        return;
    }
    static const char *plural[] = {
        [ADB_UNIT_DASH] = "dashes", [ADB_UNIT_BARSPOON] = "barspoons", [ADB_UNIT_TSP] = "tsp",
        [ADB_UNIT_CUBE] = "cubes", [ADB_UNIT_PIECE] = "pieces", [ADB_UNIT_LEAF] = "leaves",
        [ADB_UNIT_PINCH] = "pinches",
    };
    num(ri->amount, n, sizeof n);
    bool one = fabsf(ri->amount - 1.0f) < 0.01f || ri->amount < 1.0f;
    const char *u = (!one && ri->unit < sizeof(plural) / sizeof(plural[0]) && plural[ri->unit])
                        ? plural[ri->unit] : adb_tag_label(ADB_TAG_UNIT, ri->unit);
    snprintf(buf, len, "%s %s", n, u);
}

/* Fold UTF-8 to lowercase ASCII where possible (Latin-1 letters lose accents). */
static size_t fold(const char *s, char *out, size_t cap)
{
    static const char latin1[] = "aaaaaaaceeeeiiiidnoooooxouuuuytsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n + 1 < cap) {
        unsigned c = *p;
        if (c < 0x80) { out[n++] = (char)tolower((int)c); p++; continue; }
        if ((c & 0xE0) == 0xC0 && p[1]) {
            unsigned cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
            out[n++] = (cp >= 0xC0 && cp <= 0xFF) ? latin1[cp - 0xC0] : '\x01';
            p += 2;
            continue;
        }
        /* other multi-byte sequences: keep as a non-matching placeholder */
        out[n++] = '\x01';
        p++;
        while ((*p & 0xC0) == 0x80) p++;
    }
    out[n] = 0;
    return n;
}

bool adb_text_contains(const char *haystack, const char *needle)
{
    if (!needle || !*needle) return true;
    if (!haystack) return false;
    char h[256], nd[64];
    fold(haystack, h, sizeof h);
    fold(needle, nd, sizeof nd);
    return strstr(h, nd) != NULL;
}
