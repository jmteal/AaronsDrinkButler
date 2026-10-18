/*
 * Aaron's Drink Butler: recipe database (platform-neutral C).
 *
 * The database is filled by a loader (adb_load.h): JSON on the desktop,
 * a packed binary from the "drinks" flash partition on the device. Every
 * string and array lives in an arena owned by the database, so freeing the
 * database frees everything at once.
 *
 * Tags are stored as small integers / bitmasks so filtering 250 recipes is
 * a handful of integer ops per recipe.
 */
#ifndef ADB_DATA_H
#define ADB_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADB_MAX_RECIPES      512
#define ADB_MAX_INGREDIENTS  1024
#define ADB_MAX_SUBSTITUTES  8
#define ADB_NONE             0xFF     /* unknown / unset single-valued tag */
#define ADB_ING_NONE         0xFFFF

/* ---- Tag kinds ------------------------------------------------------- */

typedef enum {
    ADB_TAG_SPIRIT,      /* multi: base_spirit[]          */
    ADB_TAG_FLAVOR,      /* multi: flavor[]               */
    ADB_TAG_STRENGTH,    /* single: light/medium/strong   */
    ADB_TAG_SEASON,      /* multi                         */
    ADB_TAG_OCCASION,    /* multi                         */
    ADB_TAG_GLASS,       /* single                        */
    ADB_TAG_METHOD,      /* single                        */
    ADB_TAG_DIFFICULTY,  /* single: index 0..2 = level 1..3 */
    ADB_TAG_ALCOHOLIC,   /* single: alcoholic/low_abv/mocktail */
    ADB_TAG_COUNT,
    /* Not filterable, but share the same key/label tables: */
    ADB_TAG_ICE = ADB_TAG_COUNT,
    ADB_TAG_UNIT,
    ADB_TAG_ING_CATEGORY,
    ADB_TAG_ALL_COUNT
} adb_tag_kind_t;

/* A few indices the app logic refers to directly. They must match the
 * order of the key tables in adb_tags.c. */
enum { ADB_ALC_ALCOHOLIC = 0, ADB_ALC_LOW_ABV = 1, ADB_ALC_MOCKTAIL = 2 };
enum { ADB_SEASON_SPRING = 0, ADB_SEASON_SUMMER, ADB_SEASON_FALL, ADB_SEASON_WINTER,
       ADB_SEASON_HOLIDAY, ADB_SEASON_ALL_YEAR };
enum { ADB_UNIT_ML = 0, ADB_UNIT_DASH, ADB_UNIT_BARSPOON, ADB_UNIT_TSP, ADB_UNIT_CUBE,
       ADB_UNIT_PIECE, ADB_UNIT_LEAF, ADB_UNIT_TOP, ADB_UNIT_PINCH };
enum { ADB_CAT_SPIRIT = 0, ADB_CAT_LIQUEUR, ADB_CAT_WINE, ADB_CAT_MIXER, ADB_CAT_JUICE,
       ADB_CAT_SYRUP, ADB_CAT_BITTERS, ADB_CAT_DAIRY, ADB_CAT_EGG, ADB_CAT_GARNISH,
       ADB_CAT_OTHER, ADB_CAT_COUNT };

int         adb_tag_count(adb_tag_kind_t kind);
const char *adb_tag_key(adb_tag_kind_t kind, int idx);    /* "whiskey_bourbon" */
const char *adb_tag_label(adb_tag_kind_t kind, int idx);  /* "Bourbon"         */
int         adb_tag_lookup(adb_tag_kind_t kind, const char *key); /* -1 if unknown */
const char *adb_tag_kind_label(adb_tag_kind_t kind);      /* "Spirit"          */
bool        adb_tag_is_multi(adb_tag_kind_t kind);

/* ---- Records --------------------------------------------------------- */

typedef struct {
    const char *id;            /* stable slug, e.g. "lime_juice" */
    const char *name;          /* display name */
    uint8_t     category;      /* ADB_CAT_* */
    bool        pantry_essential;
    uint8_t     n_subs;
    uint16_t    subs[ADB_MAX_SUBSTITUTES]; /* ingredient indices usable instead */
    uint16_t    n_recipes;     /* how many recipes use it (filled by adb_db_finalize) */
} adb_ingredient_t;

typedef struct {
    uint16_t    ing;           /* index into db->ingredients */
    const char *display;       /* "Bourbon or rye whiskey" */
    float       amount;        /* <0 = no amount */
    uint8_t     unit;          /* ADB_UNIT_* or ADB_NONE */
    bool        optional;
} adb_recipe_ing_t;

typedef struct {
    const char *file;          /* detail photo path (relative to data dir) or NULL */
    const char *thumb;         /* thumbnail path or NULL */
    bool        is_photo;      /* kind == "photo" */
    const char *author, *source_url, *license, *license_url, *modified;
} adb_image_t;

typedef struct {
    const char       *id;
    const char       *name;
    const char       *aka;       /* "; "-joined alternative names, or NULL */
    adb_recipe_ing_t *ings;
    uint8_t           n_ings;
    const char      **steps;
    uint8_t           n_steps;
    const char       *garnish;
    const char       *garnish_icon;
    const char       *color_name;
    uint32_t          color_rgb;  /* 0xRRGGBB */
    float             abv;        /* estimated % in glass */
    /* tags */
    uint32_t spirit_mask;
    uint32_t flavor_mask;
    uint32_t season_mask;
    uint32_t occasion_mask;
    uint8_t  strength, glass, method, difficulty /* 0..2 */, alcoholic, ice;
    const char *glass_key;        /* raw string, kept for unknown values */
    adb_image_t image;
} adb_recipe_t;

typedef struct adb_arena_block adb_arena_block_t;

typedef struct {
    adb_recipe_t     *recipes;
    uint16_t          n_recipes;
    adb_ingredient_t *ingredients;
    uint16_t          n_ingredients;
    /* ingredient indices sorted by category then name (for My Bar) */
    uint16_t         *ing_by_category;
    char              data_dir[192];   /* base for relative image paths */
    /* warnings collected while loading (unknown tag values etc.) */
    uint16_t          n_warnings;
    char              warnings[16][96];
    adb_arena_block_t *arena;
} adb_db_t;

/* ---- Building / lifetime --------------------------------------------- */

void  adb_db_init(adb_db_t *db);
void  adb_db_free(adb_db_t *db);
void *adb_db_alloc(adb_db_t *db, size_t size);           /* zeroed, arena-owned */
const char *adb_db_strdup(adb_db_t *db, const char *s);  /* NULL-safe */
void  adb_db_warn(adb_db_t *db, const char *fmt, ...);

/* Find or create an ingredient by slug. `name` may be NULL (derived from the slug). */
uint16_t adb_db_ingredient(adb_db_t *db, const char *id, const char *name);
int      adb_db_find_ingredient(const adb_db_t *db, const char *id); /* -1 */
int      adb_db_find_recipe(const adb_db_t *db, const char *id);     /* -1 */
/* Copies the record (strings must already be arena-owned). Returns index or -1. */
int      adb_db_add_recipe(adb_db_t *db, const adb_recipe_t *r);
/* Call once after loading: computes usage counts, category order, guesses
 * missing ingredient categories. */
void     adb_db_finalize(adb_db_t *db);

/* Heuristic category from a slug ("lime_juice" -> juice). Used when no
 * ingredients.json is available. */
uint8_t  adb_guess_category(const char *ingredient_id);

/* ---- Helpers ---------------------------------------------------------- */

/* Primary spirit label for list rows: first base_spirit, or "Mocktail". */
const char *adb_recipe_spirit_label(const adb_recipe_t *r);
/* First n flavor indices of a recipe; returns count. */
int  adb_recipe_flavors(const adb_recipe_t *r, int *out, int max);
/* Formats an amount: ml or US oz (with ¼ ½ ¾ fractions). */
void adb_format_amount(const adb_recipe_ing_t *ri, bool use_oz, char *buf, size_t len);
/* Case- and accent-insensitive substring match (UTF-8, Latin-1 folding). */
bool adb_text_contains(const char *haystack, const char *needle);

#ifdef __cplusplus
}
#endif
#endif
